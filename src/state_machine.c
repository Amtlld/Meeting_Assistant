#include "state_machine.h"
#include "ui_task.h" // 用于 LED 控制函数 (稍后创建)
#include "audio_task.h" // 用于控制音频录制 (稍后创建)
#include "network_task.h" // 用于网络操作 (稍后创建)
#include <stdio.h> // 用于 printf，应替换为正确的日志记录

// 应用的当前状态
static app_state_t current_state;

// 日志记录占位符 - 替换为正确的日志记录机制
#define APP_LOG_INFO(format, ...) printf(format "\n", ##__VA_ARGS__)
#define APP_LOG_ERROR(format, ...) printf("ERROR: " format "\n", ##__VA_ARGS__)

// --- 状态进入/退出操作原型 (将在下面定义) ---
static void on_enter_wifi_disconnected(void);
static void on_enter_server_disconnected(void);
static void on_enter_idle(void);
static void on_enter_meeting_in_progress(void);
static void on_enter_meeting_paused(void);

// --- 实际的状态转换逻辑 ---
void state_machine_init(void) {
    current_state = APP_STATE_WIFI_DISCONNECTED;
    APP_LOG_INFO("State Machine Initialized. Current state: WIFI_DISCONNECTED");
    on_enter_wifi_disconnected();
}

app_state_t state_machine_get_current_state(void) {
    return current_state;
}

void state_machine_handle_event(app_event_t event) {
    app_state_t previous_state = current_state;
    APP_LOG_INFO("Handling event: %d in state: %d", event, current_state);

    switch (current_state) {
        case APP_STATE_WIFI_DISCONNECTED:
            if (event == EVENT_WIFI_CONNECTED) {
                current_state = APP_STATE_SERVER_DISCONNECTED;
                on_enter_server_disconnected();
            }
            break;

        case APP_STATE_SERVER_DISCONNECTED:
            if (event == EVENT_SERVER_CONNECTED) {
                current_state = APP_STATE_IDLE;
                on_enter_idle();
            } else if (event == EVENT_WIFI_DISCONNECTED) {
                current_state = APP_STATE_WIFI_DISCONNECTED;
                // 如果服务器也隐式断开连接，则无需调用 on_enter_wifi_disconnected()
                // 如果 WiFi 中断，network_task 应首先确保服务器标记为已断开连接。
                // 目前，直接进入 WIFI_DISCONNECTED 状态，并让其 on_enter 处理 LED。
                on_enter_wifi_disconnected(); 
            } else if (event == EVENT_SERVER_DISCONNECTED){
                current_state = APP_STATE_SERVER_DISCONNECTED;
                on_enter_server_disconnected();
            }
            break;

        case APP_STATE_IDLE:
            if (event == EVENT_BTN0_PRESSED) {
                current_state = APP_STATE_MEETING_IN_PROGRESS;
                on_enter_meeting_in_progress();
            } else if (event == EVENT_WIFI_DISCONNECTED) {
                current_state = APP_STATE_WIFI_DISCONNECTED;
                // 如果服务器也隐式断开连接，则无需调用 on_enter_wifi_disconnected()
                // 如果 WiFi 中断，network_task 应首先确保服务器标记为已断开连接。
                // 目前，直接进入 WIFI_DISCONNECTED 状态，并让其 on_enter 处理 LED。
                on_enter_wifi_disconnected(); 
            } else if (event == EVENT_SERVER_DISCONNECTED){
                current_state = APP_STATE_SERVER_DISCONNECTED;
                on_enter_server_disconnected();
            }
            break;

        case APP_STATE_MEETING_IN_PROGRESS:
            if (event == EVENT_BTN0_PRESSED) {
                current_state = APP_STATE_MEETING_PAUSED;
                on_enter_meeting_paused();
            } else if (event == EVENT_WIFI_DISCONNECTED) {
                // 数据缓存应由音频/网络任务处理
                current_state = APP_STATE_WIFI_DISCONNECTED;
                on_enter_wifi_disconnected();
            } else if (event == EVENT_SERVER_DISCONNECTED) {
                // 数据缓存应由音频/网络任务处理
                current_state = APP_STATE_SERVER_DISCONNECTED;
                on_enter_server_disconnected();
            }
            break;

        case APP_STATE_MEETING_PAUSED:
            if (event == EVENT_BTN0_PRESSED) { // 再次按下 BTN0 恢复会议
                current_state = APP_STATE_MEETING_IN_PROGRESS;
                on_enter_meeting_in_progress();
            } else if (event == EVENT_BTN1_LONG_PRESSED) {
                current_state = APP_STATE_IDLE;
                on_enter_idle();
            } else if (event == EVENT_WIFI_DISCONNECTED) {
                current_state = APP_STATE_WIFI_DISCONNECTED;
                on_enter_wifi_disconnected();
            } else if (event == EVENT_SERVER_DISCONNECTED) {
                current_state = APP_STATE_SERVER_DISCONNECTED;
                on_enter_server_disconnected();
            }
            break;

        default:
            APP_LOG_ERROR("Unknown current state: %d", current_state);
            break;
    }

    if (previous_state != current_state) {
        APP_LOG_INFO("State changed from %d to %d", previous_state, current_state);
    } else {
        APP_LOG_INFO("Event %d did not cause a state change from state %d", event, current_state);
    }
}

// --- State entry action implementations ---
static void on_enter_wifi_disconnected(void) {
    APP_LOG_INFO("Entering WIFI_DISCONNECTED state");
    // 触发 LED：快闪
    ui_set_led_state(LED_STATE_FAST_BLINK);
    // 如果正在进行音频流式传输，则停止
    audio_stop_recording(); 
    // 命令网络任务断开 MQTT (如果已连接)，并停止 Wi-Fi 连接尝试或断开连接。
    network_notify_wifi_lost();
}

static void on_enter_server_disconnected(void) {
    APP_LOG_INFO("Entering SERVER_DISCONNECTED state");
    // 触发 LED：快闪 - 根据 story.md 与 Wi-Fi 断开连接时相同
    ui_set_led_state(LED_STATE_FAST_BLINK);
    // 如果正在进行音频流式传输，则停止 (可能在会议期间服务器断开连接时发生)
    audio_stop_recording(); 
    // 命令网络任务断开 MQTT 并尝试重新连接到服务器。
    network_notify_server_lost();
}

static void on_enter_idle(void) {
    APP_LOG_INFO("Entering IDLE state");
    // Trigger LED:灭 (off) // 触发 LED：熄灭
    ui_set_led_state(LED_STATE_OFF);
    // Stop audio streaming // 停止音频流式传输
    audio_stop_recording();
}

static void on_enter_meeting_in_progress(void) {
    APP_LOG_INFO("Entering MEETING_IN_PROGRESS state");
    // Trigger LED:慢闪 (slow blink) // 触发 LED：慢闪
    ui_set_led_state(LED_STATE_SLOW_BLINK);
    // Start audio streaming // 开始音频流式传输
    audio_start_recording();
}

static void on_enter_meeting_paused(void) {
    APP_LOG_INFO("Entering MEETING_PAUSED state");
    // Trigger LED:常亮 (solid on) // 触发 LED：常亮
    ui_set_led_state(LED_STATE_SOLID_ON);
    // Pause audio streaming (stop sending, but PDM might still be running to be quickly resumed) // 暂停音频流式传输 (停止发送，但 PDM 可能仍在运行以便快速恢复)
    // Or just stop and restart. For now, let's use stop. // 或者直接停止并重新启动。目前，我们使用停止。
    audio_pause_recording(); // Or audio_stop_recording(); ui_set_mic_volume() may still be active // 或 audio_stop_recording(); ui_set_mic_volume() 可能仍处于活动状态
}

// --- Event reporting functions (to be called by other tasks) --- // --- 事件报告函数 (由其他任务调用) ---
// These would typically put an event into a queue processed by a state machine task // 这些函数通常会将事件放入由状态机任务处理的队列中
// For simplicity here, we call handle_event directly. This needs to be thread-safe if called from multiple tasks. // 为简单起见，我们直接调用 handle_event。如果从多个任务调用，则需要确保线程安全。
// Consider using a FreeRTOS queue to pass events to a dedicated state machine task. // 考虑使用 FreeRTOS 队列将事件传递给专用的状态机任务。

void report_wifi_connected_event(void) {
    state_machine_handle_event(EVENT_WIFI_CONNECTED);
}

void report_wifi_disconnected_event(void) {
    state_machine_handle_event(EVENT_WIFI_DISCONNECTED);
}

void report_server_connected_event(void) {
    state_machine_handle_event(EVENT_SERVER_CONNECTED);
}

void report_server_disconnected_event(void) {
    state_machine_handle_event(EVENT_SERVER_DISCONNECTED);
}

void report_btn0_pressed_event(void) {
    APP_LOG_INFO("report_btn0_pressed_event called from UI.");
    state_machine_handle_event(EVENT_BTN0_PRESSED);
}

void report_btn1_long_pressed_event(void) {
    APP_LOG_INFO("report_btn1_long_pressed_event called from UI.");
    state_machine_handle_event(EVENT_BTN1_LONG_PRESSED);
} 