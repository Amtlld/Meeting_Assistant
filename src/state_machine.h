#ifndef STATE_MACHINE_H_
#define STATE_MACHINE_H_

#include <stdint.h>
#include <stdbool.h>

// 应用状态，定义在 story.md 中
typedef enum {
    APP_STATE_WIFI_DISCONNECTED,
    APP_STATE_SERVER_DISCONNECTED,
    APP_STATE_IDLE,
    APP_STATE_MEETING_IN_PROGRESS,
    APP_STATE_MEETING_PAUSED
} app_state_t;

// 可触发状态转换的事件
typedef enum {
    EVENT_WIFI_CONNECTED,
    EVENT_WIFI_DISCONNECTED,
    EVENT_SERVER_CONNECTED,
    EVENT_SERVER_DISCONNECTED,
    EVENT_BTN0_PRESSED,
    EVENT_BTN1_LONG_PRESSED,
    EVENT_NONE // 无事件占位符
} app_event_t;

void state_machine_init(void);
app_state_t state_machine_get_current_state(void);
void state_machine_handle_event(app_event_t event);

// 状态进入/退出动作的回调函数类型
typedef void (*state_action_callback_t)(void);

// 由其他任务调用以通知状态机外部事件的函数
void report_wifi_connected_event(void);
void report_wifi_disconnected_event(void);
void report_server_connected_event(void);
void report_server_disconnected_event(void);
void report_btn0_pressed_event(void);
void report_btn1_long_pressed_event(void);

#endif /* STATE_MACHINE_H_ */ 