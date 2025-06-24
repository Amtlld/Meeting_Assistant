#include "cybsp.h"
#include "cyhal.h"
#include "cy_retarget_io.h"

#include "FreeRTOS.h"
#include "task.h"

#include "app_config.h"
#include "state_machine.h"
#include "audio_task.h"
#include "network_task.h"
#include "ui_task.h"

#include <stdio.h>

// 日志记录占位符
#define APP_LOG_MAIN_INFO(format, ...) printf("[MAIN] " format "\n", ##__VA_ARGS__)
#define APP_LOG_MAIN_ERROR(format, ...) printf("[MAIN ERROR] " format "\n", ##__VA_ARGS__)

// 空闲任务的堆栈，如果需要明确提供或增加
// configMINIMAL_STACK_SIZE 通常在 FreeRTOSConfig.h 中定义

// 初始化系统组件、Retarget IO 等的函数
static void system_init(void) {
    cy_rslt_t result;

    // 初始化设备和板级外设
    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS) {
        APP_LOG_MAIN_ERROR("BSP initialization failed: 0x%08X", (unsigned int)result);
        CY_ASSERT(0); // 失败时暂停
    }

    // 初始化 Retarget IO 以支持 printf 功能
    result = cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, CY_RETARGET_IO_BAUDRATE);
    if (result != CY_RSLT_SUCCESS) {
        // 即使 retarget_io 失败，如果基本硬件正常，我们可能仍希望继续。
        // 对于调试来说，这很关键。考虑如何在生产环境中处理此问题。
        // CY_ASSERT(0); 
        // 如果 retarget_io 失败，在此处打印错误将不起作用。
    }
    
    // 使能全局中断。应在所有关键初始化之后进行。
    __enable_irq();

    APP_LOG_MAIN_INFO("System initialization complete.");
}

int main(void) {
    // 初始化系统资源
    system_init();

    APP_LOG_MAIN_INFO("Meeting Assistant Starting...");

    // 初始化应用程序状态机
    state_machine_init(); 
    // 初始状态将通过 ui_set_led_state() 设置 LED，因此 UI 任务需要准备就绪，或者 ui_set_led_state 具有鲁棒性。

    // 创建应用程序任务
    BaseType_t rtos_result;

    rtos_result = xTaskCreate(audio_task, "AudioTask", AUDIO_TASK_STACK_SIZE, NULL, AUDIO_TASK_PRIORITY, NULL);
    if (rtos_result != pdPASS) {
        APP_LOG_MAIN_ERROR("Failed to create Audio task.");
    }

    rtos_result = xTaskCreate(network_task, "NetworkTask", NETWORK_TASK_STACK_SIZE, NULL, NETWORK_TASK_PRIORITY, NULL);
    if (rtos_result != pdPASS) {
        APP_LOG_MAIN_ERROR("Failed to create Network task.");
    }

    rtos_result = xTaskCreate(ui_task, "UITask", UI_TASK_STACK_SIZE, NULL, UI_TASK_PRIORITY, NULL);
    if (rtos_result != pdPASS) {
        APP_LOG_MAIN_ERROR("Failed to create UI task.");
    }

    APP_LOG_MAIN_INFO("All tasks created. Starting scheduler.");

    // 启动 FreeRTOS 调度器
    vTaskStartScheduler();

    // 正常情况下不应执行到此处
    APP_LOG_MAIN_ERROR("Scheduler unexpectedly exited!");
    CY_ASSERT(0);

    return 0;
}

// 如果启用了 malloc 失败的 FreeRTOS Hook 函数 (configUSE_MALLOC_FAILED_HOOK)
// void vApplicationMallocFailedHook(void) {
//     taskDISABLE_INTERRUPTS();
//     for(;;);
// }

// 如果启用了堆栈溢出的 FreeRTOS Hook 函数 (configCHECK_FOR_STACK_OVERFLOW > 0)
// void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName) {
//     (void)pxTask;
//     (void)pcTaskName;
//     taskDISABLE_INTERRUPTS();
//     for(;;);
// } 