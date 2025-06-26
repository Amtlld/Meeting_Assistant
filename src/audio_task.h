#ifndef AUDIO_TASK_H_
#define AUDIO_TASK_H_

#include "FreeRTOS.h"
#include "queue.h"
#include "app_config.h"
#include <stdint.h>
#include <stddef.h>

// 音频数据包结构体
typedef struct {
    int16_t samples[AUDIO_SAMPLES_PER_FRAME * AUDIO_CHANNELS]; // 在 app_config.h 中定义
    size_t  num_samples; // 此数据包中的采样点数 (每通道)
    // uint32_t timestamp; // 可选：如果需要时间戳
} audio_data_t;

extern QueueHandle_t audio_queue; // 用于发送音频数据到 network_task 的队列

void audio_task(void *pvParameters);

// 音频录制控制函数，由状态机或UI调用
void audio_start_recording(void);
void audio_stop_recording(void);
void audio_pause_recording(void); // 目前与停止类似，将来可能有区别

// 可能用于通过滑块控制音量
void audio_set_mic_volume(uint8_t percentage); // 0-100

// 控制静音帧发送的函数
void audio_start_sending_silent_frames(void);
void audio_stop_sending_silent_frames(void);

#endif /* AUDIO_TASK_H_ */ 