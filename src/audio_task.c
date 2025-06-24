#include "audio_task.h"
#include "app_config.h"
#include "state_machine.h"
#include "cyhal.h"
#include "cybsp.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h> // 用于 printf，替换为适当的日志记录

// 日志记录占位符 - 替换为适当的日志记录机制
#define APP_LOG_AUDIO_INFO(format, ...) printf("[AUDIO] " format "\n", ##__VA_ARGS__)
#define APP_LOG_AUDIO_ERROR(format, ...) printf("[AUDIO ERROR] " format "\n", ##__VA_ARGS__)

QueueHandle_t audio_queue = NULL;

static cyhal_pdm_pcm_t pdm_pcm_obj;
static cyhal_clock_t audio_clock_obj;
static cyhal_clock_t pll_clock_obj; 

// 用于 PDM/PCM 数据采集的乒乓缓冲器
// AUDIO_SAMPLES_PER_FRAME 用于单通道采样点数，缓冲区保存交错的立体声数据。
static int16_t pdm_pcm_buffer[AUDIO_SAMPLES_PER_FRAME * AUDIO_CHANNELS]; 

static volatile bool is_recording = false;
static volatile bool audio_initialized = false;

// PDM/PCM 数据采集回调函数
static void pdm_pcm_isr_handler(void *callback_arg, cyhal_pdm_pcm_event_t event) {
    if (event == CYHAL_PDM_PCM_ASYNC_COMPLETE) {
        if (is_recording && audio_queue != NULL) {
            audio_data_t audio_frame;
            // pdm_pcm_buffer 现在包含新的一帧交错立体声采样数据。
            // 如果配置为立体声，PDM 驱动程序会以交错立体声格式存储数据。
            memcpy(audio_frame.samples, pdm_pcm_buffer, AUDIO_BUFFER_SIZE_BYTES);
            audio_frame.num_samples = AUDIO_SAMPLES_PER_FRAME; // 每通道采样点数

            if (xQueueSendFromISR(audio_queue, &audio_frame, NULL) != pdPASS) {
                // APP_LOG_AUDIO_ERROR("从 ISR 发送音频帧到队列失败");
                // ISR 不应直接调用阻塞函数，如 printf 或大多数日志记录函数。
                // 如果需要，请考虑使用其他机制从 ISR 报告错误。
            }
        }

        // 如果仍在录制，则安排下一次异步读取
        if (is_recording) {
            cy_rslt_t result = cyhal_pdm_pcm_read_async(&pdm_pcm_obj, pdm_pcm_buffer, AUDIO_SAMPLES_PER_FRAME * AUDIO_CHANNELS);
            if (result != CY_RSLT_SUCCESS) {
                // APP_LOG_AUDIO_ERROR("ISR 中的 PDM 异步读取失败：0x%08X", (unsigned int)result);
                is_recording = false; // 出错时停止录制
            }
        }
    }
}

static cy_rslt_t initialize_audio_clocks(void) {
    // 基于 Audio_Streaming 示例
    cy_rslt_t result;

    // PLL：通常 CLK_HF[0] 已由系统为 CM4 启用和配置。
    // 我们需要一个特定频率的音频子系统时钟（示例中为 CLK_HF[1]）。
    // 让我们保留并配置一个由 PLL 提供的 CLK_HF[1]。
    // PSoC 6 TRM 表明 PDM/PCM 通常使用 CLK_HF[x]，其中 x > 0。
    // CY8CPROTO-062-4343W 开发板的 PDM_CLK 为 P10_4。audioss_pclk_pdm 可以由 HFCLK1 提供。

    // 保留并配置 PLL（例如，如果 PLL0 已被系统使用，则为 PLL1）
    // 为简单起见，假设一个合适的 PLL 可用或可以配置。
    // Audio_Streaming 示例使用 CYHAL_CLOCK_PLL[0] 并对其进行配置。
    // 如果 CYHAL_CLOCK_PLL[0] 是主系统 PLL，重新配置它可能会有问题。
    // 假设我们可以从 PLL 获得一个用于 16kHz 的 24.576 MHz 时钟。

    result = cyhal_clock_reserve(&pll_clock_obj, &CYHAL_CLOCK_PLL[1]); // 尝试 PLL[1]
    if (result != CY_RSLT_SUCCESS) {
        APP_LOG_AUDIO_ERROR("Failed to reserve PLL clock: 0x%08X", (unsigned int)result);
        // 如果 PLL[1] 失败且它不是系统时钟，则尝试 PLL[0]，或处理错误
        result = cyhal_clock_reserve(&pll_clock_obj, &CYHAL_CLOCK_PLL[0]);
        if (result != CY_RSLT_SUCCESS) { 
            APP_LOG_AUDIO_ERROR("Failed to reserve PLL[0] clock: 0x%08X", (unsigned int)result);
            return result; 
        }
    }
    // 配置 PLL 以输出所需频率（例如，用于基于 16kHz 或 48kHz PDM 的 24.576 MHz）
    // 此频率取决于 PDM/PCM 配置（采样率，抽取率）
    // 对于 16kHz 采样率，抽取率为 64，PDM_CLK = 16000 * 64 * 1 = 1.024 MHz（如果为立体声）
    // 音频子系统时钟（Audio_Streaming 示例中的 AUDIO_SYS_CLOCK_HZ）通常更高，例如 24.576 MHz。
    // PDM_CLK = AUDIO_SYS_CLOCK_HZ / PDM_CLK_DIVIDER。PDM_CLK_DIVIDER 是 PDM 硬件模块的一部分。
    // 目前，使用示例的 AUDIO_SYS_CLOCK_HZ。
    const uint32_t audio_system_clock_hz = 24576000u; 
    result = cyhal_clock_set_frequency(&pll_clock_obj, audio_system_clock_hz, NULL);
    if (result != CY_RSLT_SUCCESS) {
        APP_LOG_AUDIO_ERROR("Failed to set PLL frequency: 0x%08X", (unsigned int)result);
        cyhal_clock_free(&pll_clock_obj);
        return result;
    }
    cyhal_clock_set_enabled(&pll_clock_obj, true, true);

    // 保留并配置音频时钟（例如 CLK_HF[1]）
    result = cyhal_clock_reserve(&audio_clock_obj, &CYHAL_CLOCK_HF[1]);
    if (result != CY_RSLT_SUCCESS) {
        APP_LOG_AUDIO_ERROR("Failed to reserve audio clock (HFCLK1): 0x%08X", (unsigned int)result);
        cyhal_clock_free(&pll_clock_obj);
        return result;
    }
    result = cyhal_clock_set_source(&audio_clock_obj, &pll_clock_obj);
    if (result != CY_RSLT_SUCCESS) {
        APP_LOG_AUDIO_ERROR("Failed to set audio clock source: 0x%08X", (unsigned int)result);
        cyhal_clock_free(&audio_clock_obj);
        cyhal_clock_free(&pll_clock_obj);
        return result;
    }
    // 如果需要，PDM 外设将进一步分频此时钟。
    cyhal_clock_set_enabled(&audio_clock_obj, true, true);
    APP_LOG_AUDIO_INFO("Audio clocks initialized.");
    return CY_RSLT_SUCCESS;
}

static cy_rslt_t initialize_pdm_pcm(void) {
    const cyhal_pdm_pcm_cfg_t pdm_pcm_cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE,
        .decimation_rate = 64, // 常用值，PDM_CLK = 采样率 * 抽取率
        .mode = AUDIO_MODE, // 根据 story.md (双麦克风) 和 kit_info.md (共享数据线)
        .word_length = AUDIO_BIT_RESOLUTION, // 位
        .left_gain = AUDIO_LEFT_GAIN_DB * 2,  // left_gain*0.5dB，根据需要调整。来自 kit_info.md，PDM 麦克风是 SPK0838HT4H-B。
        .right_gain = AUDIO_RIGHT_GAIN_DB * 2, // right_gain*0.5dB，根据需要调整。
    };

    cy_rslt_t result = cyhal_pdm_pcm_init(&pdm_pcm_obj, CYBSP_PDM_DATA, CYBSP_PDM_CLK, &audio_clock_obj, &pdm_pcm_cfg);
    if (result != CY_RSLT_SUCCESS) {
        APP_LOG_AUDIO_ERROR("PDM/PCM initialization failed: 0x%08X", (unsigned int)result);
        return result;
    }

    cyhal_pdm_pcm_register_callback(&pdm_pcm_obj, pdm_pcm_isr_handler, NULL);
    /* result = */ cyhal_pdm_pcm_enable_event(&pdm_pcm_obj, CYHAL_PDM_PCM_ASYNC_COMPLETE, CYHAL_ISR_PRIORITY_DEFAULT, true);
    // if (result != CY_RSLT_SUCCESS) { // 此检查已移除，因为函数返回 void
    //     APP_LOG_AUDIO_ERROR("PDM/PCM 使能事件失败：0x%08X", (unsigned int)result);
    //     cyhal_pdm_pcm_free(&pdm_pcm_obj);
    //     return result;
    // }
    // 对于 void 返回类型，无法直接检查 enable_event 的错误。
    // 如果在此调用期间/之后没有发生断言或硬故障，则假定成功。
    // 对于稳健的错误处理，如果可用，底层驱动程序错误可能需要不同的检查方法。

    APP_LOG_AUDIO_INFO("PDM/PCM initialized.");
    audio_initialized = true;
    return CY_RSLT_SUCCESS;
}

void audio_task(void *pvParameters) {
    (void)pvParameters;
    cy_rslt_t result;

    APP_LOG_AUDIO_INFO("Audio task started.");

    // 创建用于发送音频数据的队列
    // 缓冲区存储 audio_data_t 结构体。大小在 app_config.h 中定义
    audio_queue = xQueueCreate(AUDIO_QUEUE_LENGTH, sizeof(audio_data_t));
    if (audio_queue == NULL) {
        APP_LOG_AUDIO_ERROR("Failed to create audio queue.");
        vTaskDelete(NULL); // 或适当处理错误
        return;
    }

    result = initialize_audio_clocks();
    if (result != CY_RSLT_SUCCESS) {
        APP_LOG_AUDIO_ERROR("Audio clock initialization failed. Deleting task.");
        vQueueDelete(audio_queue);
        audio_queue = NULL;
        vTaskDelete(NULL);
        return;
    }

    result = initialize_pdm_pcm();
    if (result != CY_RSLT_SUCCESS) {
        APP_LOG_AUDIO_ERROR("PDM/PCM initialization failed. Deleting task.");
        // 如果 PDM 初始化失败，则释放时钟
        cyhal_clock_free(&audio_clock_obj);
        cyhal_clock_free(&pll_clock_obj);
        vQueueDelete(audio_queue);
        audio_queue = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    // 任务本身在循环中不做太多事情，因为 PDM 是异步的，ISR 处理数据。
    // 它主要用于初始化和管理音频资源。
    // 控制函数 (启动/停止) 将管理 'is_recording' 标志和 PDM 外设。
    while (1) {
        // 主要工作由 PDM ISR 和控制函数完成。
        // 如果需要，此任务可以监视命令或执行清理操作。
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}

void audio_start_recording(void) {
    if (!audio_initialized) {
        APP_LOG_AUDIO_ERROR("Cannot start recording, audio not initialized.");
        return;
    }
    if (is_recording) {
        APP_LOG_AUDIO_INFO("Recording already in progress.");
        return;
    }

    APP_LOG_AUDIO_INFO("Starting audio recording...");
    // 清除 PDM 外设 FIFO 中的任何挂起数据
    cyhal_pdm_pcm_clear(&pdm_pcm_obj);
    
    is_recording = true; // Set recording flag

    // 首先发起第一次异步读取
    cy_rslt_t result = cyhal_pdm_pcm_read_async(&pdm_pcm_obj, pdm_pcm_buffer, AUDIO_SAMPLES_PER_FRAME * AUDIO_CHANNELS);
    if (result != CY_RSLT_SUCCESS) {
        APP_LOG_AUDIO_ERROR("Initial PDM async read failed: 0x%08X", (unsigned int)result);
        is_recording = false; // Reset flag if read_async fails
        // 不需要在这里调用 stop，因为 PDM 可能还未成功启动
        return;
    }

    // 然后启动 PDM/PCM 操作
    result = cyhal_pdm_pcm_start(&pdm_pcm_obj);
    if (result != CY_RSLT_SUCCESS) {
        APP_LOG_AUDIO_ERROR("PDM/PCM start failed: 0x%08X", (unsigned int)result);
        is_recording = false; // Reset flag
        // 如果 start 失败，可能需要中止之前成功的 read_async (如果 HAL 支持 abort 且有必要)
        // 但通常，如果 start 失败，说明外设本身就有问题，read_async 的回调也不会触发。
        // For now, just setting is_recording to false and returning should be okay.
        // The ISR checks is_recording before queueing new reads.
        cyhal_pdm_pcm_abort_async(&pdm_pcm_obj); // 尝试中止挂起的异步读取
        return;
    }
    // 如果两个调用都成功，录音正式开始，ISR将处理后续的读取
}

void audio_stop_recording(void) {
    if (!audio_initialized || !is_recording) {
        // APP_LOG_AUDIO_INFO("Recording not in progress or audio not initialized.");
        return;
    }
    APP_LOG_AUDIO_INFO("Stopping audio recording...");
    is_recording = false; // ISR 将看到此标志并停止链接读取

    // 首先尝试中止任何正在进行的异步操作
    cy_rslt_t abort_result = cyhal_pdm_pcm_abort_async(&pdm_pcm_obj);
    if (abort_result != CY_RSLT_SUCCESS) {
        // 如果 abort 失败，可能意味着没有正在进行的异步操作，或者出现了其他错误
        // 对于 "already in progress" 的问题，即使 abort 报告错误（例如"没有操作可中止"）
        // 也可能不是关键问题，关键是确保它被调用了。
        // 可以选择性地打印日志，但主要目标是调用它。
        APP_LOG_AUDIO_INFO("PDM/PCM abort_async result: 0x%08X (may be OK if no async was pending)", (unsigned int)abort_result);
    }

    // 然后停止 PDM/PCM 硬件
    cy_rslt_t stop_result = cyhal_pdm_pcm_stop(&pdm_pcm_obj);
    if (stop_result != CY_RSLT_SUCCESS) {
        APP_LOG_AUDIO_ERROR("PDM/PCM stop failed: 0x%08X", (unsigned int)stop_result);
    }
    
    // （可选）可以稍微延迟一下，确保硬件和HAL状态有时间更新
    // vTaskDelay(pdMS_TO_TICKS(1)); // 非常短的延迟，作为诊断手段

    // 如果需要，可选择清除队列，但通常应由消费者耗尽队列
}

void audio_pause_recording(void) {
    APP_LOG_AUDIO_INFO("Pausing audio recording (currently same as stop).");
    audio_stop_recording(); 
    // 未来：可以只设置一个标志来停止发送到队列，但保持 PDM 运行以便快速恢复。
}

void audio_set_mic_volume(uint8_t percentage) {
    if (!audio_initialized) {
        APP_LOG_AUDIO_ERROR("Cannot set volume, audio not initialized.");
        return;
    }
    // PSoC 6 PDM/PCM HAL 在 cyhal_pdm_pcm_cfg_t 中具有 .left_gain 和 .right_gain。
    // 这些是在初始化时设置的。动态更改增益可能需要重新初始化或特定的 API。
    // cyhal_pdm_pcm_set_gain() 函数存在。
    // 增益以 dB 为单位。将百分比映射到 dB 并非易事。
    // 目前，这是一个占位符。PDM 增益的典型范围可能是 -12dB 到 +12dB 或更大。
    // 示例：将 0-100% 映射到一个合理的 dB 范围，例如 -12dB 到 +12dB。
    // 最小增益 dB = -12，最大增益 dB = 12。范围 = 24 dB。
    // gain_db = ((percentage / 100.0f) * 24.0f) - 12.0f;
    // int8_t gain_db_int = (int8_t)gain_db; 

    // 占位符：如果 API 允许动态更改，则将百分比转换为支持的增益值。
    // 目前，这是一个概念性函数。
    int8_t new_gain_db = 0; // 默认为 0dB
    if (percentage == 0) new_gain_db = CYHAL_PDM_PCM_MIN_GAIN; // 静音或最小增益
    else if (percentage >= 100) new_gain_db = CYHAL_PDM_PCM_MAX_GAIN; // 最大增益
    else {
        // 简单线性映射到增益范围的子集，例如 50-100% 对应 0dB 到 12dB
        // 这需要适当的校准和对增益步长的理解。
        // 实际增益范围是从 CYHAL_PDM_PCM_MIN_GAIN 到 CYHAL_PDM_PCM_MAX_GAIN
        // 根据 HAL 文档，某些设备为 -12dB 到 12dB (请针对 PSoC 6进行验证)
        // 让我们假设一个简单的映射用于演示
        float mapped_gain = ( (float)percentage / 100.0f ) * (CYHAL_PDM_PCM_MAX_GAIN - CYHAL_PDM_PCM_MIN_GAIN) + CYHAL_PDM_PCM_MIN_GAIN;
        if(mapped_gain < CYHAL_PDM_PCM_MIN_GAIN) mapped_gain = CYHAL_PDM_PCM_MIN_GAIN;
        if(mapped_gain > CYHAL_PDM_PCM_MAX_GAIN) mapped_gain = CYHAL_PDM_PCM_MAX_GAIN;
        new_gain_db = (int8_t)mapped_gain;
    }

    APP_LOG_AUDIO_INFO("Set microphone volume (percentage: %d, mapped gain_db: %d dB is conceptual).", percentage, new_gain_db);
    // cy_rslt_t result = cyhal_pdm_pcm_set_gain(&pdm_pcm_obj, new_gain_db, new_gain_db); // 如果是立体声，则同时设置两者
    // if (result != CY_RSLT_SUCCESS) {
    // APP_LOG_AUDIO_ERROR("设置 PDM 增益失败：0x%08X", (unsigned int)result);
    // }
    APP_LOG_AUDIO_INFO("Note: Actual dynamic gain control via cyhal_pdm_pcm_set_gain() needs validation and careful mapping.");
} 