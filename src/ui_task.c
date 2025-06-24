#include "ui_task.h"
#include "app_config.h"
#include "state_machine.h"
#include "audio_task.h" // 用于 audio_set_mic_volume

#include "cyhal.h"
#include "cybsp.h"
#include "cycfg.h" // 用于 CapSense 生成的配置 (cycfg_capsense.h)
#include "cycfg_capsense.h" // 用于 CapSense 小部件 ID 等

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h"

#include <stdio.h> // 用于 printf，替换为适当的日志记录

// 日志记录占位符
#define APP_LOG_UI_INFO(format, ...) printf("[UI] " format "\n", ##__VA_ARGS__)
#define APP_LOG_UI_ERROR(format, ...) printf("[UI ERROR] " format "\n", ##__VA_ARGS__)

// CapSense 任务定义 (来自示例)
#define CAPSENSE_SCAN_INTERVAL_MS   (20u) // 扫描间隔
static TimerHandle_t capsense_scan_timer_handle;

// UI 任务中 CapSense 处理部分的内部命令枚举
typedef enum {
    CAPSENSE_CMD_SCAN,    // 开始 CapSense 扫描的命令
    CAPSENSE_CMD_PROCESS  // 处理已扫描数据的命令
} capsense_internal_cmd_t;
static QueueHandle_t capsense_internal_cmd_queue;

// LED 控制变量
static led_indicator_state_t current_led_state = LED_STATE_OFF;
static TimerHandle_t led_blink_timer_handle = NULL;
static bool led_state = false; // 如果 LED 物理上亮起，则为 true

// 按钮 BTN1 长按检测变量
#define BTN1_LONG_PRESS_THRESHOLD_MS (2000) // 长按阈值为 2 秒
static TickType_t btn1_press_start_time = 0;
static bool btn1_is_pressed = false;

// Forward declarations
static void capsense_init(void);
static void capsense_isr_callback(void);
static void capsense_eoc_callback(cy_stc_active_scan_sns_t *active_scan_sns_ptr);
static void capsense_timer_cb(TimerHandle_t xTimer);
static void process_touch_input(void);
static void led_timer_callback(TimerHandle_t xTimer);
static void update_led_physical_state(void);

void ui_task(void *pvParameters) {
    (void)pvParameters;
    APP_LOG_UI_INFO("UI Task Started.");

    capsense_internal_cmd_queue = xQueueCreate(5, sizeof(capsense_internal_cmd_t));
    if (capsense_internal_cmd_queue == NULL) {
        APP_LOG_UI_ERROR("Failed to create CapSense internal command queue.");
        vTaskDelete(NULL);
        return;
    }

    // Initialize CapSense
    capsense_init();

    // Create a timer for periodic CapSense scans
    capsense_scan_timer_handle = xTimerCreate("CapScanTmr", pdMS_TO_TICKS(CAPSENSE_SCAN_INTERVAL_MS),
                                              pdTRUE, (void *)0, capsense_timer_cb);
    if (capsense_scan_timer_handle == NULL) {
        APP_LOG_UI_ERROR("Failed to create CapSense scan timer.");
        // 在没有 capsense 的情况下继续或删除任务
    } else {
        xTimerStart(capsense_scan_timer_handle, 0);
    }

    // Create a timer for LED blinking patterns
    // Initially stopped, period will be set when blinking starts // 初始停止，周期将在闪烁开始时设置
    led_blink_timer_handle = xTimerCreate("LedBlinkTmr", pdMS_TO_TICKS(1000), 
                                           pdTRUE, (void *)0, led_timer_callback);
    if (led_blink_timer_handle == NULL) {
        APP_LOG_UI_ERROR("Failed to create LED blink timer.");
    }

    // Initialize LED to current_led_state (which is initially OFF after state machine calls ui_set_led_state)
    // The state machine calls ui_set_led_state(LED_STATE_FAST_BLINK) on init. // 状态机在初始化时调用 ui_set_led_state(LED_STATE_FAST_BLINK)。
    // So no need to call it explicitly here if state machine init runs first. // 因此，如果状态机初始化首先运行，则无需在此处显式调用它。

    capsense_internal_cmd_t cmd;
    while (1) {
        // Wait for a command to scan or process CapSense data // 等待命令以扫描或处理 CapSense 数据
        // APP_LOG_UI_INFO("UI_TASK: Waiting for CapSense command...");
        if (xQueueReceive(capsense_internal_cmd_queue, &cmd, portMAX_DELAY) == pdPASS) {
            // APP_LOG_UI_INFO("UI_TASK: Received command: %d (0=SCAN, 1=PROCESS)", cmd);
            if (Cy_CapSense_IsBusy(&cy_capsense_context) == CY_CAPSENSE_NOT_BUSY) {
                if (cmd == CAPSENSE_CMD_SCAN) {
                    // APP_LOG_UI_INFO("UI_TASK: Processing SCAN command.");
                    Cy_CapSense_ScanAllWidgets(&cy_capsense_context);
                } else if (cmd == CAPSENSE_CMD_PROCESS) {
                    // APP_LOG_UI_INFO("UI_TASK: Processing PROCESS command.");
                    Cy_CapSense_ProcessAllWidgets(&cy_capsense_context);
                    process_touch_input(); // 处理检测到的触摸
                    Cy_CapSense_RunTuner(&cy_capsense_context); // 如果使用，则用于调谐器 GUI
                }
            } else {
                // APP_LOG_UI_INFO("CapSense is busy, command %d not processed immediately.", cmd); // 新增日志
            }
        }
        // BTN1 long press check (could also be done in process_touch_input or a separate timer) // BTN1 长按检查 (也可以在 process_touch_input 或单独的计时器中完成)
        if (btn1_is_pressed && (xTaskGetTickCount() - btn1_press_start_time >= pdMS_TO_TICKS(BTN1_LONG_PRESS_THRESHOLD_MS))) {
            APP_LOG_UI_INFO("BTN1 long press detected.");
            report_btn1_long_pressed_event();
            btn1_is_pressed = false; // 重置以避免一次按住触发多次长按事件
        }
        // taskYIELD();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void capsense_init(void) {
    cy_status result;

    // CapSense interrupt configuration. EZI2C interrupt priority should be higher if used for tuner. // CapSense 中断配置。如果用于调谐器，EZI2C 中断优先级应更高。
    const cy_stc_sysint_t capsense_interrupt_config = {
        .intrSrc = csd_interrupt_IRQn,
        .intrPriority = 7u, // 根据需要调整优先级
    };

    result = Cy_CapSense_Init(&cy_capsense_context);
    if (result != CY_RSLT_SUCCESS) {
        APP_LOG_UI_ERROR("CapSense initialization failed: 0x%08X", (unsigned int)result);
        CY_ASSERT(0); // 失败时停止
        return;
    }

    Cy_SysInt_Init(&capsense_interrupt_config, &capsense_isr_callback);
    NVIC_ClearPendingIRQ(csd_interrupt_IRQn);
    NVIC_EnableIRQ(csd_interrupt_IRQn);

    result = Cy_CapSense_RegisterCallback(CY_CAPSENSE_END_OF_SCAN_E, 
                                          capsense_eoc_callback, &cy_capsense_context);
    if (result != CY_RSLT_SUCCESS) {
        APP_LOG_UI_ERROR("Failed to register CapSense EOC callback: 0x%08X", (unsigned int)result);
    }

    result = Cy_CapSense_Enable(&cy_capsense_context);
    if (result != CY_RSLT_SUCCESS) {
        APP_LOG_UI_ERROR("CapSense enable failed: 0x%08X", (unsigned int)result);
        CY_ASSERT(0); // 失败时停止
        return;
    }
    APP_LOG_UI_INFO("CapSense initialized.");

    // EZI2C for tuner (optional, from example) // 用于调谐器的 EZI2C (可选，来自示例)
    // cyhal_ezi2c_t ezi2c_obj;
    // cy_rslt_t ezi2c_result = cyhal_ezi2c_init(&ezi2c_obj, CYBSP_I2C_SDA, CYBSP_I2C_SCL, NULL, &slEzI2C_config);
    // if (ezi2c_result == CY_RSLT_SUCCESS) {
    //     APP_LOG_UI_INFO("EZI2C for CapSense Tuner initialized.");
    // } else {
    //     APP_LOG_UI_ERROR("EZI2C for CapSense Tuner init failed: 0x%08X", (unsigned int)ezi2c_result);
    // }
}

// This is the ISR registered with NVIC // 这是在 NVIC 中注册的 ISR
static void capsense_isr_callback(void) {
    Cy_CapSense_InterruptHandler(CYBSP_CSD_HW, &cy_capsense_context);
}

// This is the callback registered with CapSense for end of scan // 这是为扫描结束在 CapSense 中注册的回调
static void capsense_eoc_callback(cy_stc_active_scan_sns_t *active_scan_sns_ptr) {
    (void)active_scan_sns_ptr;
    // APP_LOG_UI_INFO("CAPSENSE_EOC_CALLBACK: End of scan detected.");
    capsense_internal_cmd_t cmd = CAPSENSE_CMD_PROCESS;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xQueueSendToFrontFromISR(capsense_internal_cmd_queue, &cmd, &xHigherPriorityTaskWoken) != pdPASS) {
        // APP_LOG_UI_ERROR("CAPSENSE_EOC_CALLBACK: Failed to send PROCESS command to queue!");
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void capsense_timer_cb(TimerHandle_t xTimer) {
    (void)xTimer;
    // APP_LOG_UI_INFO("CAPSENSE_TIMER_CB: Timer fired, sending SCAN command.");
    capsense_internal_cmd_t cmd = CAPSENSE_CMD_SCAN;
    if (xQueueSend(capsense_internal_cmd_queue, &cmd, 0) != pdPASS) {
        APP_LOG_UI_ERROR("CAPSENSE_TIMER_CB: Failed to send SCAN command to CapSense queue (timer)!");
    }
}

static void process_touch_input(void) {
    // APP_LOG_UI_INFO("PROCESS_TOUCH_INPUT: Entered function.");
    static uint32_t prev_btn0_status = 0;
    static uint32_t prev_btn1_status = 0;
    static uint16_t prev_slider_pos = 0xFFFF; // 初始化为无效位置

    uint32_t btn0_status = Cy_CapSense_IsSensorActive(
        CY_CAPSENSE_BUTTON0_WDGT_ID,
        CY_CAPSENSE_BUTTON0_SNS0_ID,
        &cy_capsense_context);
    uint32_t btn1_status = Cy_CapSense_IsSensorActive(
        CY_CAPSENSE_BUTTON1_WDGT_ID,
        CY_CAPSENSE_BUTTON1_SNS0_ID,
        &cy_capsense_context);
    
    // APP_LOG_UI_INFO("PROCESS_TOUCH_INPUT: BTN0 status_raw=%u, BTN1 status_raw=%u", (unsigned int)btn0_status, (unsigned int)btn1_status);

    // Updated slider processing based on CAPSENSE_Buttons_and_Slider_FreeRTOS example // 基于 CAPSENSE_Buttons_and_Slider_FreeRTOS 示例更新的滑块处理
    cy_stc_capsense_touch_t* slider_touch_info_ptr = Cy_CapSense_GetTouchInfo(
        CY_CAPSENSE_LINEARSLIDER0_WDGT_ID,
        &cy_capsense_context
    );
    uint16_t slider_pos = 0; // 如果没有触摸或错误，则默认为 0
    bool slider_is_touched = false;

    if (slider_touch_info_ptr != NULL && slider_touch_info_ptr->numPosition > 0) {
        slider_pos = slider_touch_info_ptr->ptrPosition[0].x;
        slider_is_touched = true;
    }

    // BTN0 Press Detection // BTN0 按下检测
    if ((0u != btn0_status) && (0u == prev_btn0_status)) {
        APP_LOG_UI_INFO("BTN0 Pressed.");
        report_btn0_pressed_event();
    }

    // BTN1 Press/Release Detection for Long Press Logic // 用于长按逻辑的 BTN1 按下/释放检测
    if (btn1_status && !prev_btn1_status) { // Rising edge of BTN1 press // BTN1 按下的上升沿
        APP_LOG_UI_INFO("BTN1 pressed (for long press check).");
        btn1_is_pressed = true;
        btn1_press_start_time = xTaskGetTickCount();
    }
    if (!btn1_status && prev_btn1_status) { // Falling edge of BTN1 press (released) // BTN1 按下的下降沿 (已释放)
        APP_LOG_UI_INFO("BTN1 Released.");
        if(btn1_is_pressed){ // Check if it was a short press (long press not yet triggered) // 检查是否为短按 (长按尚未触发)
            // If story.md only defines long press for BTN1, short press does nothing. // 如果 story.md 仅为 BTN1 定义了长按，则短按不执行任何操作。
            // If short press had a function, it would be here. // 如果短按有功能，它会在这里。
            // APP_LOG_UI_INFO("BTN1 Short Press (action if any).");
        }
        btn1_is_pressed = false; 
    }

    // Slider Change Detection // 滑块变化检测
    if (slider_is_touched && (slider_pos != prev_slider_pos)) {
        uint8_t slider_percentage = 0;
        if (cy_capsense_context.ptrWdConfig[CY_CAPSENSE_LINEARSLIDER0_WDGT_ID].xResolution > 0) {
           slider_percentage = (uint8_t)(( (uint32_t)slider_pos * 100) / 
                                cy_capsense_context.ptrWdConfig[CY_CAPSENSE_LINEARSLIDER0_WDGT_ID].xResolution);
        }
        APP_LOG_UI_INFO("Slider position: %d (%d%%)", slider_pos, slider_percentage);
        audio_set_mic_volume(slider_percentage);
        ui_set_mic_volume_display(slider_percentage); // For any visual feedback // 用于任何视觉反馈
        prev_slider_pos = slider_pos; // Update previous position only when touched and changed // 仅在触摸和更改时更新先前位置
    } else if (!slider_is_touched && prev_slider_pos != 0xFFFF) { // Use 0xFFFF or another sentinel for no touch // 对无触摸使用 0xFFFF 或其他标记值
        prev_slider_pos = 0xFFFF; // Reset previous position when touch is released // 触摸释放时重置先前位置
    }

    prev_btn0_status = btn0_status;
    prev_btn1_status = btn1_status;
    // prev_slider_pos is updated conditionally above // prev_slider_pos 在上面有条件地更新
}


void ui_set_led_state(led_indicator_state_t new_led_state) {
    if (current_led_state == new_led_state && led_blink_timer_handle != NULL && xTimerIsTimerActive(led_blink_timer_handle) && (new_led_state == LED_STATE_FAST_BLINK || new_led_state == LED_STATE_SLOW_BLINK) ) {
        // 如果已处于闪烁状态且计时器处于活动状态，则除非周期更改，否则无需重新启动
        // 如果相同状态的闪烁周期可变，则此简单检查可能无法涵盖所有边缘情况。
    } else {
        // 如果先前状态的当前闪烁计时器处于活动状态，则停止它
        if (led_blink_timer_handle != NULL && xTimerIsTimerActive(led_blink_timer_handle)) {
            xTimerStop(led_blink_timer_handle, 0);
        }
    }
    current_led_state = new_led_state;
    APP_LOG_UI_INFO("UI Set LED State: %d", new_led_state);

    switch (current_led_state) {
        case LED_STATE_OFF:
            led_state = false;
            update_led_physical_state();
            break;
        case LED_STATE_SOLID_ON:
            led_state = true;
            update_led_physical_state();
            break;
        case LED_STATE_SLOW_BLINK:
            if (led_blink_timer_handle != NULL) {
                xTimerChangePeriod(led_blink_timer_handle, pdMS_TO_TICKS(1000), 0); // 1秒间隔
                xTimerStart(led_blink_timer_handle, 0);
                led_state = true; // 开始时 LED 亮起以进行闪烁
                update_led_physical_state();
            }
            break;
        case LED_STATE_FAST_BLINK:
            if (led_blink_timer_handle != NULL) {
                xTimerChangePeriod(led_blink_timer_handle, pdMS_TO_TICKS(250), 0); // 250ms interval // 250毫秒间隔
                xTimerStart(led_blink_timer_handle, 0);
                led_state = true; // Start with LED on for blink // 开始时 LED 亮起以进行闪烁
                update_led_physical_state();
            }
            break;
        default:
            break;
    }
}

static void led_timer_callback(TimerHandle_t xTimer) {
    (void)xTimer;
    if (current_led_state == LED_STATE_SLOW_BLINK || current_led_state == LED_STATE_FAST_BLINK) {
        led_state = !led_state;
        update_led_physical_state();
    }
}

static void update_led_physical_state(void) {
    // CYBSP_USER_LED is P13.7, low active // CYBSP_USER_LED 为 P13.7，低电平有效
    if (led_state) {
        cyhal_gpio_write(CYBSP_USER_LED, CYBSP_LED_STATE_ON); // BSP typically defines ON as logic level for on // BSP 通常将 ON 定义为开启的逻辑电平
    } else {
        cyhal_gpio_write(CYBSP_USER_LED, CYBSP_LED_STATE_OFF); // BSP typically defines OFF as logic level for off // BSP 通常将 OFF 定义为关闭的逻辑电平
    }
}

// Placeholder for potential future display of mic volume (e.g. on a screen or segmented LED) // (占位符) 将来可能显示麦克风音量 (例如在屏幕或分段 LED 上)
void ui_set_mic_volume_display(uint8_t percentage) {
    // APP_LOG_UI_INFO("Mic volume display update: %d%% (Placeholder)", percentage);
    // This function could, for example, update a bar graph on an LCD or a series of LEDs. // 例如，此函数可以更新 LCD 上的条形图或一系列 LED。
} 