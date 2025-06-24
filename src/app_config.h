#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_

// 音频配置
#define AUDIO_SAMPLE_RATE         (16000u)  // 16 kHz
#define AUDIO_MODE                CYHAL_PDM_PCM_MODE_LEFT
// #define AUDIO_MODE                CYHAL_PDM_PCM_MODE_RIGHT
// #define AUDIO_MODE                CYHAL_PDM_PCM_MODE_STEREO

#define AUDIO_LEFT_GAIN_DB        10        //增益10分贝
#define AUDIO_RIGHT_GAIN_DB       10

#if AUDIO_MODE == CYHAL_PDM_PCM_MODE_LEFT || AUDIO_MODE == CYHAL_PDM_PCM_MODE_RIGHT
#define AUDIO_CHANNELS            (1)       // 单声道
#else
#define AUDIO_CHANNELS            (2)       // 立体声
#endif

#define AUDIO_BIT_RESOLUTION      (16)      // 16位
#define AUDIO_FRAME_DURATION_MS   (40)      // 40毫秒
#define AUDIO_SAMPLES_PER_FRAME   ((AUDIO_SAMPLE_RATE * AUDIO_FRAME_DURATION_MS) / 1000)
#define AUDIO_BUFFER_SIZE_BYTES   (AUDIO_SAMPLES_PER_FRAME * AUDIO_CHANNELS * (AUDIO_BIT_RESOLUTION / 8))

// MQTT 配置 (占位符，后续需要用户配置)
//#define MQTT_BROKER_ADDRESS       "192.168.5.246"
#define MQTT_BROKER_ADDRESS       "111.229.213.23"
#define MQTT_PORT                 (1883)
#define MQTT_CLIENT_ID_PREFIX     "meeting_assistant"
//#define MQTT_TOPIC_AUDIO_STREAM   "meeting_audio/stream"
#define MQTT_TOPIC_AUDIO_STREAM   "audio/stream"
#define MQTT_USERNAME             "" // 可选
#define MQTT_PASSWORD             "" // 可选
#define MQTT_SECURE_CONNECTION    (0) // 0 表示非安全连接，1 表示安全连接 (TLS)

// Wi-Fi 配置 (占位符，后续需要用户配置)
#define WIFI_SSID                 "603"
#define WIFI_PASSWORD             "USST_3.1.603"
#define WIFI_SECURITY             CY_WCM_SECURITY_WPA2_AES_PSK // 根据实际情况修改

// 用户界面
// LED4 (CY8CPROTO-062-4343W 上的用户 LED 是 P13.7，低电平有效)
// #define USER_LED_PIN            (P13_7) // 通常由 BSP 定义为 CYBSP_USER_LED
// #define USER_LED_ON_STATE       (0)
// #define USER_LED_OFF_STATE      (1)

// SW2 (CY8CPROTO-062-4343W 上的用户按钮是 P0.4)
// #define USER_BUTTON_PIN         (P0_4) // 通常由 BSP 定义为 CYBSP_USER_BTN

// PDM 麦克风引脚 (由 BSP 定义为 CYBSP_PDM_CLK, CYBSP_PDM_DATA)
// #define PDM_MIC_CLK             (P10_4)
// #define PDM_MIC_DATA            (P10_5)

// CAPSENSE IDs (通常在 cycfg_capsense.h 中定义)
// #define CAPSENSE_BTN0_ID        (CY_CAPSENSE_BUTTON0_WDGT_ID)
// #define CAPSENSE_BTN1_ID        (CY_CAPSENSE_BUTTON1_WDGT_ID)
// #define CAPSENSE_SLIDER_ID      (CY_CAPSENSE_LINEARSLIDER0_WDGT_ID)

// 任务优先级
#define MAIN_TASK_PRIORITY        (tskIDLE_PRIORITY + 1)
#define AUDIO_TASK_PRIORITY       (tskIDLE_PRIORITY + 3)
#define UI_TASK_PRIORITY          (tskIDLE_PRIORITY + 2) // UI 任务具有较高优先级以确保响应性
#define NETWORK_TASK_PRIORITY     (tskIDLE_PRIORITY + 1) // 网络任务优先级较低

// 任务堆栈大小
#define AUDIO_TASK_STACK_SIZE     (1024 * 2)
#define NETWORK_TASK_STACK_SIZE   (1024 * 4) // MQTT/WCM 可能需要更大堆栈
#define UI_TASK_STACK_SIZE        (1024 * 4)

// 队列长度
#define AUDIO_QUEUE_LENGTH        (50) // 可容纳5个音频帧
#define UI_EVENT_QUEUE_LENGTH     (10)
#define NETWORK_STATUS_QUEUE_LENGTH (5)


#endif /* APP_CONFIG_H_ */ 