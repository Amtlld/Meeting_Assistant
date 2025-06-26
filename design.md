# Meeting Assistant 设计文档

## 1. 项目概述

"智音会助"是一款基于英飞凌 PSoC 6 开发板 (CY8CPROTO-062-4343W) 的智能会议助手。其核心功能包括：使用双 PDM 麦克风进行实时高质量音频采集；通过 Wi-Fi 将音频数据实时上传至云端服务器进行语音转写（音频帧通过 MQTT 协议发送）；提供基于 CapSense 触控（BTN0, BTN1, Slider）和 LED 的直观用户界面。项目设计支持网络自动重连和数据本地缓存（具体缓存实现细节需进一步确认）。

**项目状态定义**

| 状态名                      | LED (LED4) 行为 | 触发条件/说明                                     |
| :-------------------------- | :-------------- | :------------------------------------------------ |
| WiFi 未连接 (WIFI_DISCONNECTED) | 快闪            | 启动后，无法连接到指定 WiFi 时进入此状态            |
| 服务器未连接 (SERVER_DISCONNECTED) | 快闪            | 连接 WiFi 后，无法连接到指定服务器时进入此状态      |
| 空闲 (IDLE)                 | 熄灭            | 成功连接 WiFi 和服务器后进入此状态                  |
| 会议中 (MEETING_IN_PROGRESS) | 慢闪            | 空闲状态下按下 BTN0 进入此状态                      |
| 会议暂停 (MEETING_PAUSED)   | 常亮            | 会议中状态下按下 BTN0 进入此状态；此状态下长按 BTN1 结束会议，返回空闲状态 |

CapSense Slider 用于调节麦克风音量。

## 2. 系统架构

系统基于 FreeRTOS 实现多任务并发处理，主要包含以下任务和模块：

**任务概览**

| 任务名称           | 主要职责                                                                 |
| :----------------- | :----------------------------------------------------------------------- |
| UI 任务 (`ui_task`) | 处理用户输入（CapSense 按钮和滑块）和状态输出（LED 指示灯）                    |
| 音频任务 (`audio_task`) | 初始化和管理 PDM/PCM 麦克风，进行音频数据的采集、处理和缓冲                    |
| 网络任务 (`network_task`) | 负责 Wi-Fi 连接、MQTT 通信，将音频数据发送到云端服务器，并处理网络状态变化 |
| 状态机 (`state_machine`) | 管理应用的整体运行状态，并根据外部事件驱动状态转换，协调各任务模块的行为         |

### 2.1 任务交互

*   `ui_task` 检测到 CapSense 事件后，通过 `report_*_event()` 函数通知 `state_machine`。
*   `state_machine` 根据当前状态和接收到的事件，调用 `audio_task` 中的 `audio_start_recording()`, `audio_stop_recording()`, `audio_pause_recording()` 控制音频流。
*   `state_machine` 调用 `ui_task` 中的 `ui_set_led_state()` 更新 LED 显示。
*   `audio_task` 将采集到的音频帧放入 `audio_queue`。
*   `network_task` 从 `audio_queue` 获取音频帧，并通过 MQTT 发送。
*   `network_task` 检测到网络状态变化后，通过 `report_*_event()` 通知 `state_machine`，并可能被 `state_machine` 通过 `network_notify_*_lost()` 通知。

## 3. 硬件抽象层 (HAL) 和外设驱动库 (PDL) 使用情况

### 3.1 通用初始化 (`main.c`)

| 组件/外设        | 初始化函数/接口                                                                                      | 描述                                                                 |
| :--------------- | :--------------------------------------------------------------------------------------------------- | :------------------------------------------------------------------- |
| 板级支持包 (BSP) | `cybsp_init()`                                                                                       | 初始化开发板和板级外设                                                   |
| Retarget IO      | `cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, CY_RETARGET_IO_BAUDRATE)`              | 初始化 UART，用于 `printf` 日志输出                                    |
| GPIO (用户 LED)  | `cyhal_gpio_init(CYBSP_USER_LED, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF)` | 初始化用户 LED (LED4, `CYBSP_USER_LED`) 为输出模式，初始状态为灭           |
| GPIO (用户 LED)  | `cyhal_gpio_write(CYBSP_USER_LED, state)`                                                            | 在 `ui_task.c` 中用于控制 LED 亮灭 (state: `CYBSP_LED_STATE_ON`/`OFF`) |

### 3.2 音频子系统 (`audio_task.c`)

*   **使用外设**: PDM/PCM 数字麦克风 (通过 `CYBSP_PDM_DATA` 和 `CYBSP_PDM_CLK` BSP 定义的引脚)。

**HAL 层接口 - 时钟 (`cyhal_clock_t`)**

| 函数名                    | 描述                                                                                             |
| :------------------------ | :----------------------------------------------------------------------------------------------- |
| `cyhal_clock_reserve()`   | 预留 PLL 时钟 (尝试 `CYHAL_CLOCK_PLL[1]` 或 `CYHAL_CLOCK_PLL[0]`) 和高频时钟 (`CYHAL_CLOCK_HF[1]`) |
| `cyhal_clock_set_frequency()` | 设置 PLL 时钟频率 (目标频率如 `24576000u` Hz)                                                      |
| `cyhal_clock_set_source()`  | 设置高频时钟 (`audio_clock_obj`) 的源为 PLL 时钟 (`pll_clock_obj`)                                 |
| `cyhal_clock_set_enabled()` | 使能所配置的时钟                                                                                     |
| `cyhal_clock_free()`      | 释放时钟资源                                                                                       |

**HAL 层接口 - PDM/PCM (`cyhal_pdm_pcm_t pdm_pcm_obj`)**

| 函数名                                  | 描述                                                                                                 |
| :-------------------------------------- | :--------------------------------------------------------------------------------------------------- |
| `cyhal_pdm_pcm_init()`                  | 初始化 PDM/PCM 模块 (引脚: `CYBSP_PDM_DATA`, `CYBSP_PDM_CLK`, 时钟: `&audio_clock_obj`, 配置: `&pdm_pcm_cfg`) |
| `cyhal_pdm_pcm_register_callback()`     | 注册 PDM/PCM 异步操作完成的回调函数 (`pdm_pcm_isr_handler`)                                              |
| `cyhal_pdm_pcm_enable_event()`          | 使能 PDM/PCM 异步完成事件中断 (`CYHAL_PDM_PCM_ASYNC_COMPLETE`)                                           |
| `cyhal_pdm_pcm_read_async()`            | 异步读取 PDM 数据到 `pdm_pcm_buffer`                                                                   |
| `cyhal_pdm_pcm_start()`                 | 启动 PDM/PCM 操作                                                                                      |
| `cyhal_pdm_pcm_stop()`                  | 停止 PDM/PCM 操作                                                                                      |
| `cyhal_pdm_pcm_clear()`                 | 清除 PDM FIFO                                                                                          |
| `cyhal_pdm_pcm_abort_async()`           | 中止正在进行的异步读取操作                                                                                 |
| `cyhal_pdm_pcm_set_gain()`              | (在 `audio_set_mic_volume` 中提及，待验证) 用于动态调整麦克风音量                                            |

**关键 HAL 数据结构**

| 数据结构名              | 描述                                                                                                                                                                                            |
| :---------------------- | :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `cyhal_pdm_pcm_cfg_t`   | PDM/PCM 配置结构体。包含 `.sample_rate` (`AUDIO_SAMPLE_RATE`), `.decimation_rate` (如 64), `.mode` (`AUDIO_MODE`), `.word_length` (`AUDIO_BIT_RESOLUTION`), `.left_gain`, `.right_gain` (0.5dB 单位)。 |
| `cyhal_pdm_pcm_event_t` | PDM/PCM 事件类型 (在回调 `pdm_pcm_isr_handler` 中判断 `CYHAL_PDM_PCM_ASYNC_COMPLETE`)。                                                                                                                |

**项目特定数据结构**

| 数据结构名         | 描述                                                                       |
| :----------------- | :------------------------------------------------------------------------- |
| `audio_data_t`     | 用于在任务间传递的音频帧数据，包含 `int16_t samples[]` 和 `size_t num_samples`。 |
| `pdm_pcm_buffer[]` | `int16_t` 数组，作为 PDM/PCM 异步读取的缓冲区。                                |

*   **初始化流程**:
    1.  `initialize_audio_clocks()`: 配置并使能 PLL 和音频高频时钟。
    2.  `initialize_pdm_pcm()`: 使用 `app_config.h` 中的参数配置 PDM/PCM 模块，并注册回调。
*   **中断处理 (`pdm_pcm_isr_handler`)**:
    *   当 `CYHAL_PDM_PCM_ASYNC_COMPLETE` 事件发生时，将采集到的音频数据 (`pdm_pcm_buffer`) 封装成 `audio_data_t` 帧，并通过 `audio_queue` 发送给网络任务。
    *   若仍在录制状态，则继续调用 `cyhal_pdm_pcm_read_async()` 采集下一帧。

### 3.3 用户界面 (`ui_task.c`)

*   **使用外设**:
    *   CapSense: BTN0 (`CY_CAPSENSE_BUTTON0_WDGT_ID`), BTN1 (`CY_CAPSENSE_BUTTON1_WDGT_ID`), 线性滑块 (`CY_CAPSENSE_LINEARSLIDER0_WDGT_ID`)。这些 ID 由 CapSense Configurator 在 `cycfg_capsense.h` 中定义。
    *   用户 LED: `CYBSP_USER_LED` (由 `main.c` 初始化，由 `ui_task.c` 控制)。

**PSoC 外设驱动库 (PDL) 接口 - CapSense (`Cy_CapSense_`)**

| 函数名                                  | 描述                                                                             |
| :-------------------------------------- | :------------------------------------------------------------------------------- |
| `Cy_CapSense_Init()`                    | 初始化 CapSense 中间件 (上下文: `&cy_capsense_context`)                             |
| `Cy_CapSense_Enable()`                  | 使能 CapSense (上下文: `&cy_capsense_context`)                                     |
| `Cy_CapSense_RegisterCallback()`        | 注册扫描结束回调 (`CY_CAPSENSE_END_OF_SCAN_E`, `capsense_eoc_callback`)              |
| `Cy_CapSense_ScanAllWidgets()`          | 启动扫描所有 CapSense 小部件                                                         |
| `Cy_CapSense_ProcessAllWidgets()`       | 处理小部件数据                                                                     |
| `Cy_CapSense_IsSensorActive()`          | 检测传感器是否被触摸 (参数: `widget_id`, `sensor_id`)                                |
| `Cy_CapSense_GetTouchInfo()`            | 获取滑块等小部件的触摸信息 (返回 `cy_stc_capsense_touch_t*`)                           |
| `Cy_CapSense_InterruptHandler()`        | CapSense CSD 中断的核心处理函数 (参数: `CYBSP_CSD_HW`, `&cy_capsense_context`)       |
| `Cy_CapSense_IsBusy()`                  | 检查 CapSense 是否忙碌                                                             |
| `Cy_CapSense_RunTuner()`                | (可选) 支持 CapSense 调谐器                                                        |

**系统调用 (SysInt) - CapSense 中断**

| 函数名                | 描述                                                                 |
| :-------------------- | :------------------------------------------------------------------- |
| `Cy_SysInt_Init()`    | 配置 CapSense (CSD) 中断 (中断配置: `&capsense_interrupt_config`, ISR: `&capsense_isr_callback`) |
| `NVIC_EnableIRQ()`    | 使能 CapSense 中断 (`csd_interrupt_IRQn`)                              |

**HAL 接口 - GPIO (LED)**

| 函数名                | 描述                                      |
| :-------------------- | :---------------------------------------- |
| `cyhal_gpio_write()`  | 控制用户 LED 的状态 (`CYBSP_USER_LED`, state) |
| `cyhal_ezi2c_init()`  | (注释代码中提及) 可用于初始化 EZI2C 以支持 CapSense 调谐器 |

**关键数据结构 (UI)**

| 数据结构名                      | 描述                                                                                               |
| :------------------------------ | :------------------------------------------------------------------------------------------------- |
| `cy_stc_capsense_context_t`     | CapSense 全局上下文，由 Configurator 生成 (`cy_capsense_context`)                                      |
| `led_indicator_state_t`         | 枚举，定义 LED 的显示状态 (`LED_STATE_OFF`, `LED_STATE_SOLID_ON`, `LED_STATE_SLOW_BLINK`, `LED_STATE_FAST_BLINK`) |
| `cy_stc_capsense_touch_t`       | 包含滑块位置等触摸信息。                                                                                 |

*   **初始化流程**:
    1.  `capsense_init()`: 调用 `Cy_CapSense_Init` 和 `Cy_CapSense_Enable`，配置中断并注册扫描结束回调。
*   **中断与回调处理**:
    *   `capsense_isr_callback()`: 作为 CSD 中断的 ISR，调用 `Cy_CapSense_InterruptHandler()`。
    *   `capsense_eoc_callback()`: 当 CapSense 扫描完成时被调用，发送 `CAPSENSE_CMD_PROCESS` 命令到 `capsense_internal_cmd_queue`。
    *   `capsense_timer_cb()`: CapSense 扫描定时器的回调，周期性发送 `CAPSENSE_CMD_SCAN` 命令。
    *   `led_timer_callback()`: LED 闪烁定时器的回调，用于切换 LED 状态以实现闪烁效果。

## 4. 中间件/库使用情况

### 4.1 FreeRTOS

**任务创建与管理**

| 函数名                  | 描述                                                | 涉及任务                                  |
| :---------------------- | :-------------------------------------------------- | :---------------------------------------- |
| `xTaskCreate()`         | 创建 FreeRTOS 任务                                  | `audio_task`, `network_task`, `ui_task` |
| `vTaskStartScheduler()` | 启动 FreeRTOS 调度器                                | -                                         |
| `vTaskDelete()`         | 任务自我删除或在出错时删除                            | 各任务内部                                  |
| `vTaskDelay()`          | 任务延时                                            | 各任务内部                                  |

**队列 (`QueueHandle_t`)**

| 队列名                          | 用途                                                                   | 管理函数 (部分)                                                               |
| :------------------------------ | :--------------------------------------------------------------------- | :---------------------------------------------------------------------------- |
| `audio_queue`                   | 从 `audio_task` (ISR) 发送 `audio_data_t` 帧到 `network_task`            | `xQueueCreate()`, `xQueueSendFromISR()`, `xQueueReceive()`, `vQueueDelete()`  |
| `capsense_internal_cmd_queue`   | 在 `ui_task` 内部传递 `capsense_internal_cmd_t` (扫描/处理命令)        | `xQueueCreate()`, `xQueueSendToFrontFromISR()`, `xQueueSend()`, `xQueueReceive()` |

**软件定时器 (`TimerHandle_t`)**

| 定时器句柄名                 | 用途                               | 回调函数             | 管理函数 (部分)                                                                 |
| :--------------------------- | :--------------------------------- | :------------------- | :------------------------------------------------------------------------------ |
| `capsense_scan_timer_handle` | 周期性触发 CapSense 扫描           | `capsense_timer_cb`  | `xTimerCreate()`, `xTimerStart()`, `xTimerStop()`, `xTimerChangePeriod()`       |
| `led_blink_timer_handle`     | 控制 LED 闪烁                      | `led_timer_callback` | `xTimerCreate()`, `xTimerStart()`, `xTimerStop()`, `xTimerChangePeriod()`       |

**事件组 (`EventGroupHandle_t`)**

| 事件组句柄名          | 用途                                     | 定义的事件位                                                                                             | 管理函数                                                                                      |
| :-------------------- | :--------------------------------------- | :------------------------------------------------------------------------------------------------------- | :-------------------------------------------------------------------------------------------- |
| `network_event_group` | 在 `network_task` 中同步网络连接/断开事件 | `WIFI_CONNECTED_BIT`, `MQTT_CONNECTED_BIT`, `WIFI_DISCONNECTED_BIT`, `MQTT_DISCONNECTED_BIT`, `SHUTDOWN_BIT` | `xEventGroupCreate()`, `xEventGroupSetBits()`, `xEventGroupWaitBits()`, `vEventGroupDelete()` |

### 4.2 Wi-Fi 连接管理器 (WCM) (`network_task.c`)

**WCM 接口 (`cy_wcm_`)**

| 函数名                          | 描述                                                                                              |
| :------------------------------ | :------------------------------------------------------------------------------------------------ |
| `cy_wcm_init()`                 | 初始化 WCM，配置为 STA 模式 (`.interface = CY_WCM_INTERFACE_TYPE_STA`)                               |
| `cy_wcm_deinit()`               | 反初始化 WCM                                                                                        |
| `cy_wcm_connect_ap()`           | 连接到 Wi-Fi AP (参数: `cy_wcm_connect_params_t*`, 使用 `app_config.h` 中的 SSID, PWD, Security)      |
| `cy_wcm_disconnect_ap()`        | 从 AP 断开                                                                                          |
| `cy_wcm_get_mac_addr()`         | 获取 STA 接口的 MAC 地址，用于生成 MQTT Client ID                                                       |
| `cy_wcm_is_connected_to_ap()`   | 检查 Wi-Fi 连接状态                                                                                 |

**WCM 数据结构**

| 数据结构名                | 描述                                                                                              |
| :------------------------ | :------------------------------------------------------------------------------------------------ |
| `cy_wcm_connect_params_t` | 包含 `.ap_credentials` (SSID, 密码, 安全类型如 `CY_WCM_SECURITY_WPA2_AES_PSK`)，值来自 `app_config.h`。 |

### 4.3 MQTT 客户端库 (`network_task.c`)

**MQTT 接口 (`cy_mqtt_`)**

| 函数名                            | 描述                                                                                                                              |
| :-------------------------------- | :-------------------------------------------------------------------------------------------------------------------------------- |
| `cy_mqtt_init()`                  | 初始化 MQTT 库                                                                                                                      |
| `cy_mqtt_deinit()`                | 反初始化 MQTT 库                                                                                                                    |
| `cy_mqtt_create()`                | 创建 MQTT 客户端实例 (参数: buffer, security_info, broker_info, handle_out)                                                        |
| `cy_mqtt_delete()`                | 删除 MQTT 实例                                                                                                                      |
| `cy_mqtt_register_event_callback()` | 注册 MQTT 事件回调函数 (`mqtt_event_callback`)                                                                                        |
| `cy_mqtt_connect()`               | 连接到 MQTT Broker (参数: `cy_mqtt_connect_info_t*`)                                                                              |
| `cy_mqtt_disconnect()`            | 从 Broker 断开                                                                                                                    |
| `cy_mqtt_publish()`               | 发布消息到 MQTT 主题 (参数: `cy_mqtt_publish_info_t*`)                                                                              |

**MQTT 数据结构**

| 数据结构名                       | 描述                                                                                                                                                             |
| :------------------------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `cy_mqtt_t`                      | MQTT 连接句柄 (`mqtt_connection_handle`)                                                                                                                           |
| `cy_mqtt_broker_info_t`          | Broker 地址 (`MQTT_BROKER_ADDRESS`) 和端口 (`MQTT_PORT`)，来自 `app_config.h`。                                                                                       |
| `cy_mqtt_connect_info_t`         | 连接参数，包括客户端 ID (基于 `MQTT_CLIENT_ID_PREFIX` 和 MAC 地址生成)、用户名/密码 (来自 `app_config.h`)、keep-alive 时间、clean session 标志。                       |
| `cy_awsport_ssl_credentials_t`   | (当前配置为非安全连接，`MQTT_SECURE_CONNECTION = 0`) 用于 TLS 安全连接的凭证。                                                                                       |
| `cy_mqtt_publish_info_t`         | 发布消息的参数，包括 QoS (`CY_MQTT_QOS0`)、主题 (`MQTT_TOPIC_AUDIO_STREAM` 来自 `app_config.h`)、payload (音频数据) 和 payload 长度。                                 |
| `cy_mqtt_event_t`                | 在 `mqtt_event_callback` 中使用，包含事件类型 (如 `CY_MQTT_EVENT_TYPE_DISCONNECT`, `CY_MQTT_EVENT_TYPE_SUBSCRIPTION_MESSAGE_RECEIVE`) 和相关数据。                 |

*   **回调处理 (`mqtt_event_callback`)**:
    *   处理 `CY_MQTT_EVENT_TYPE_DISCONNECT`: 当 MQTT 断开时被调用，设置 `network_event_group` 中的 `MQTT_DISCONNECTED_BIT`，触发重连逻辑。
    *   处理 `CY_MQTT_EVENT_TYPE_SUBSCRIPTION_MESSAGE_RECEIVE`: (当前项目似乎未订阅主题) 处理接收到的 MQTT 消息。

## 5. 关键项目特定数据结构

| 数据结构名              | 定义文件          | 描述                                                                                                                                                             |
| :---------------------- | :---------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `app_state_t`           | `state_machine.h` | 枚举，定义应用的主要状态: `APP_STATE_WIFI_DISCONNECTED`, `APP_STATE_SERVER_DISCONNECTED`, `APP_STATE_IDLE`, `APP_STATE_MEETING_IN_PROGRESS`, `APP_STATE_MEETING_PAUSED`。 |
| `app_event_t`           | `state_machine.h` | 枚举，定义可以触发状态转换的事件: `EVENT_WIFI_CONNECTED`, `EVENT_WIFI_DISCONNECTED`, `EVENT_SERVER_CONNECTED`, `EVENT_SERVER_DISCONNECTED`, `EVENT_BTN0_PRESSED`, `EVENT_BTN1_LONG_PRESSED`。 |
| `audio_data_t`          | `audio_task.h`    | 结构体，用于封装和传递音频数据: `int16_t samples[]`, `size_t num_samples`。                                                                                          |
| `led_indicator_state_t` | `ui_task.h`       | 枚举，定义 LED 的不同显示模式: `LED_STATE_OFF`, `LED_STATE_SOLID_ON`, `LED_STATE_SLOW_BLINK`, `LED_STATE_FAST_BLINK`。                                                   |

## 6. 配置文件 (`app_config.h`)

此文件集中管理了项目中的重要配置参数。

**音频参数**

| 参数名                      | 示例值/描述                                |
| :-------------------------- | :----------------------------------------- |
| `AUDIO_SAMPLE_RATE`         | 16000 Hz (采样率)                          |
| `AUDIO_MODE`                | `CYHAL_PDM_PCM_MODE_LEFT` (PDM/PCM 模式)     |
| `AUDIO_LEFT_GAIN_DB`        | 10 (左声道麦克风增益, dB)                    |
| `AUDIO_RIGHT_GAIN_DB`       | 10 (右声道麦克风增益, dB)                    |
| `AUDIO_BIT_RESOLUTION`      | 16 (音频位深, bit)                         |
| `AUDIO_FRAME_DURATION_MS`   | 40 ms (每帧音频时长)                       |

**MQTT 参数**

| 参数名                      | 示例值/描述                             |
| :-------------------------- | :-------------------------------------- |
| `MQTT_BROKER_ADDRESS`       | "111.229.213.23" (服务器地址)           |
| `MQTT_PORT`                 | 1883 (服务器端口)                       |
| `MQTT_CLIENT_ID_PREFIX`     | "meeting_assistant" (MQTT 客户端 ID 前缀) |
| `MQTT_TOPIC_AUDIO_STREAM`   | "audio/stream" (音频流发布的 MQTT 主题)   |
| `MQTT_USERNAME`             | "" (MQTT 用户名, 可选)                  |
| `MQTT_PASSWORD`             | "" (MQTT 密码, 可选)                    |
| `MQTT_SECURE_CONNECTION`    | 0 (0: 非安全连接, 1: TLS 安全连接)      |

**Wi-Fi 参数**

| 参数名            | 示例值/描述                                  |
| :---------------- | :------------------------------------------- |
| `WIFI_SSID`       | "603" (Wi-Fi 名称)                           |
| `WIFI_PASSWORD`   | "USST_3.1.603" (Wi-Fi 密码)                  |
| `WIFI_SECURITY`   | `CY_WCM_SECURITY_WPA2_AES_PSK` (Wi-Fi 安全类型) |

**RTOS 参数**

| 参数名                        | 示例值/描述                      |
| :---------------------------- | :------------------------------- |
| `MAIN_TASK_PRIORITY`        | `tskIDLE_PRIORITY + 1`           |
| `AUDIO_TASK_PRIORITY`       | `tskIDLE_PRIORITY + 3`           |
| `UI_TASK_PRIORITY`          | `tskIDLE_PRIORITY + 3`           |
| `NETWORK_TASK_PRIORITY`     | `tskIDLE_PRIORITY + 1`           |
| `AUDIO_TASK_STACK_SIZE`     | `(1024 * 2)` (字节)              |
| `NETWORK_TASK_STACK_SIZE`   | `(1024 * 4)` (字节)              |
| `UI_TASK_STACK_SIZE`        | `(1024 * 4)` (字节)              |
| `AUDIO_QUEUE_LENGTH`        | 50 (条目数)                      |
| `UI_EVENT_QUEUE_LENGTH`     | 10 (条目数, 当前未使用)            |
| `NETWORK_STATUS_QUEUE_LENGTH` | 5 (条目数, 当前未使用)             |

这些参数直接影响 HAL 初始化 (如 PDM/PCM 配置) 和库的行为 (如 Wi-Fi 和 MQTT 连接参数)。
