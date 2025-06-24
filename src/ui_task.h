#ifndef UI_TASK_H_
#define UI_TASK_H_

#include "FreeRTOS.h"
#include "queue.h"
#include "state_machine.h" // 用于 app_state_t 和报告事件

// 用于 ui_set_led_state 的 LED 状态
typedef enum {
    LED_STATE_OFF,
    LED_STATE_SOLID_ON,
    LED_STATE_SLOW_BLINK, // 例如，亮1秒，灭1秒
    LED_STATE_FAST_BLINK  // 例如，亮250毫秒，灭250毫秒
} led_indicator_state_t;

// UI 事件 (例如，来自 CapSense)
// 这些是概念性的，可能直接处理或通过队列处理
typedef enum {
    UI_EVENT_BTN0_PRESS,
    UI_EVENT_BTN1_PRESS,      // 普通按压
    UI_EVENT_BTN1_LONG_PRESS,
    UI_EVENT_SLIDER_CHANGE
} ui_event_type_t;

typedef struct {
    ui_event_type_t type;
    uint32_t value; // 例如，UI_EVENT_SLIDER_CHANGE 事件的滑块位置
} ui_event_t;

// 如果需要更复杂的交互或 ISR 之外的去抖动，则用于 UI 事件的队列
// extern QueueHandle_t ui_event_queue;

void ui_task(void *pvParameters);

// 由状态机或其他任务调用以更新 LED 的函数
void ui_set_led_state(led_indicator_state_t new_led_state);

// 由状态机调用以设置麦克风音量（尽管 audio_task 将实现实际更改）的函数
void ui_set_mic_volume_display(uint8_t percentage); // 用于将来可能在 LED/OLED 上的显示


#endif /* UI_TASK_H_ */ 