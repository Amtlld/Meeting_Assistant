#include "network_task.h"
#include "audio_task.h" // 用于 audio_queue
#include "app_config.h"
#include "state_machine.h"

#include "cyhal.h"
#include "cybsp.h"
#include "cy_wcm.h"
#include "cy_mqtt_api.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"

#include <stdio.h> // 用于 printf，请替换为正确的日志记录
#include <string.h>

// 日志记录占位符
#define APP_LOG_NET_INFO(format, ...) printf("[NET] " format "\n", ##__VA_ARGS__)
#define APP_LOG_NET_ERROR(format, ...) printf("[NET ERROR] " format "\n", ##__VA_ARGS__)

#define MQTT_HANDLE_DESCRIPTOR            "MQTThandleID"

// MQTT 配置 (来自 app_config.h，但在此处为 cy_mqtt_broker_info_t 定义)
static cy_mqtt_broker_info_t broker_info = {
    .hostname = MQTT_BROKER_ADDRESS,
    .hostname_len = (sizeof(MQTT_BROKER_ADDRESS) - 1),
    .port = MQTT_PORT
};

static cy_mqtt_connect_info_t connection_info = {
    .client_id = NULL, // 将被生成
    .client_id_len = 0,
    .keep_alive_sec = 60,
    .username = MQTT_USERNAME,
    .password = MQTT_PASSWORD,
    .username_len = (sizeof(MQTT_USERNAME) > 1 ? sizeof(MQTT_USERNAME) - 1 : 0),
    .password_len = (sizeof(MQTT_PASSWORD) > 1 ? sizeof(MQTT_PASSWORD) - 1 : 0),
    .clean_session = true
};

#if (MQTT_SECURE_CONNECTION == 1)
// 如果启用 MQTT_SECURE_CONNECTION，则为安全凭证占位符
// 确保这些在 app_config.h 或安全位置正确定义
// static const char client_certificate[] = "...";
// static const char client_private_key[] = "...";
// static const char root_ca_certificate[] = "...";

static cy_awsport_ssl_credentials_t security_info = {
    // .client_cert = client_certificate,
    // .client_cert_size = sizeof(client_certificate),
    // .private_key = client_private_key,
    // .private_key_size = sizeof(client_private_key),
    // .root_ca = root_ca_certificate,
    // .root_ca_size = sizeof(root_ca_certificate)
    // 如果使用安全连接，请适当初始化
    .client_cert = NULL, .client_cert_size = 0, .private_key = NULL, .private_key_size = 0, .root_ca = NULL, .root_ca_size = 0
};
#else
static cy_awsport_ssl_credentials_t *security_info = NULL;
#endif

static cy_mqtt_t mqtt_connection_handle;
static uint8_t mqtt_network_buffer[1024 * 2]; // MQTT 库网络缓冲区
static char mqtt_client_id_buffer[64];

// Wi-Fi 和 MQTT 连接状态
static volatile bool wifi_connected = false;
static volatile bool mqtt_server_connected = false;

// 用于在网络任务内部发信号通知连接事件的事件组
#define WIFI_CONNECTED_BIT (1 << 0)
#define MQTT_CONNECTED_BIT (1 << 1)
#define WIFI_DISCONNECTED_BIT (1 << 2)
#define MQTT_DISCONNECTED_BIT (1 << 3)
#define SHUTDOWN_BIT (1 << 4) // 用于通知任务关闭

static EventGroupHandle_t network_event_group;

// 前向声明
static cy_rslt_t connect_to_wifi(void);
static cy_rslt_t connect_to_mqtt_broker(void);
static void mqtt_event_callback(cy_mqtt_t mqtt_handle, cy_mqtt_event_t event, void *user_data);
static void generate_client_id(void);

void network_task(void *pvParameters) {
    (void)pvParameters;
    cy_rslt_t result;
    audio_data_t received_audio_frame;
    cy_mqtt_publish_info_t publish_info;

    APP_LOG_NET_INFO("Network task started.");

    network_event_group = xEventGroupCreate();
    if (network_event_group == NULL) {
        APP_LOG_NET_ERROR("Failed to create network event group.");
        vTaskDelete(NULL);
        return;
    }

    // 初始化 Wi-Fi 连接管理器
    cy_wcm_config_t wcm_config = {.interface = CY_WCM_INTERFACE_TYPE_STA};
    result = cy_wcm_init(&wcm_config);
    if (result != CY_RSLT_SUCCESS) {
        APP_LOG_NET_ERROR("Wi-Fi Connection Manager initialization failed: 0x%08X", (unsigned int)result);
        // 向状态机报告，尽管它以 WIFI_DISCONNECTED 状态启动
        report_wifi_disconnected_event();
        vEventGroupDelete(network_event_group);
        vTaskDelete(NULL);
        return;
    }
    APP_LOG_NET_INFO("Wi-Fi Connection Manager initialized.");

    // 初始化 MQTT 库
    result = cy_mqtt_init();
    if (result != CY_RSLT_SUCCESS) {
        APP_LOG_NET_ERROR("MQTT library initialization failed: 0x%08X", (unsigned int)result);
        cy_wcm_deinit();
        report_wifi_disconnected_event(); // MQTT 初始化失败意味着无法访问服务器
        report_server_disconnected_event();
        vEventGroupDelete(network_event_group);
        vTaskDelete(NULL);
        return;
    }
    APP_LOG_NET_INFO("MQTT library initialized.");
    
    generate_client_id();
    connection_info.client_id = mqtt_client_id_buffer;
    connection_info.client_id_len = strlen(mqtt_client_id_buffer);

    APP_LOG_NET_INFO("Broker Hostname: %s", broker_info.hostname);
    APP_LOG_NET_INFO("Broker Hostname Len: %u", (unsigned int)broker_info.hostname_len);
    APP_LOG_NET_INFO("Broker Port: %u", (unsigned int)broker_info.port);

    result = cy_mqtt_create(mqtt_network_buffer, sizeof(mqtt_network_buffer),
                            security_info, &broker_info, MQTT_HANDLE_DESCRIPTOR,
                            &mqtt_connection_handle);
    if (result != CY_RSLT_SUCCESS) {
        APP_LOG_NET_ERROR("MQTT instance creation failed: 0x%08X", (unsigned int)result);
        cy_mqtt_deinit();
        cy_wcm_deinit();
        report_server_disconnected_event();
        vEventGroupDelete(network_event_group);
        vTaskDelete(NULL);
        return;
    }
    APP_LOG_NET_INFO("MQTT instance created.");

    cy_mqtt_register_event_callback(mqtt_connection_handle, mqtt_event_callback, NULL);

    // 初始连接尝试
    if (connect_to_wifi() == CY_RSLT_SUCCESS) {
        connect_to_mqtt_broker();
    }

    while (1) {
        while (uxQueueMessagesWaiting(audio_queue) > 0) {
            // 如果 MQTT 已连接，则处理音频队列
            if (mqtt_server_connected && wifi_connected && audio_queue != NULL) {
                if (xQueueReceive(audio_queue, &received_audio_frame, 0) == pdPASS) { // 非阻塞读取
                    if (mqtt_server_connected && wifi_connected) {
                        publish_info.qos = CY_MQTT_QOS0; // 或根据要求的 QOS_1
                        publish_info.retain = false;
                        publish_info.dup = false;
                        publish_info.topic = MQTT_TOPIC_AUDIO_STREAM;
                        publish_info.topic_len = strlen(MQTT_TOPIC_AUDIO_STREAM);
                        publish_info.payload = (const void*)received_audio_frame.samples;
                        publish_info.payload_len = received_audio_frame.num_samples * AUDIO_CHANNELS * (AUDIO_BIT_RESOLUTION / 8);

                        result = cy_mqtt_publish(mqtt_connection_handle, &publish_info);
                        if (result != CY_RSLT_SUCCESS) {
                            APP_LOG_NET_ERROR("MQTT publish failed: 0x%08X", (unsigned int)result);
                            // 如果发布失败，可能表示连接问题已由回调处理
                        }
                    } else {
                        // 如果网络断开，可以选择丢弃帧或做其他处理
                        APP_LOG_NET_INFO("Network not connected, discarding audio frame.");
                        break; // 跳出内层while，去处理网络事件
                    }
                }
            }
        }
        
        EventBits_t bits = xEventGroupWaitBits(network_event_group,
                                               WIFI_CONNECTED_BIT | MQTT_CONNECTED_BIT | WIFI_DISCONNECTED_BIT | MQTT_DISCONNECTED_BIT | SHUTDOWN_BIT,
                                               pdTRUE, // 退出时清除
                                               pdFALSE, // 等待任一位
                                               pdMS_TO_TICKS(100)); // 处理音频队列超时

        if (bits & SHUTDOWN_BIT) {
            APP_LOG_NET_INFO("Shutdown signal received.");
            break;
        }

        if (bits & WIFI_DISCONNECTED_BIT) {
            APP_LOG_NET_INFO("Wi-Fi disconnected bit set.");
            if (mqtt_server_connected) {
                 cy_mqtt_disconnect(mqtt_connection_handle); // MQTT 也将断开连接
                 mqtt_server_connected = false;
                 report_server_disconnected_event();
            }
            wifi_connected = false;
            report_wifi_disconnected_event(); 
            // 尝试重新连接 Wi-Fi
            if (connect_to_wifi() == CY_RSLT_SUCCESS) {
                 // 如果 Wi-Fi 重新连接，则再次尝试 MQTT
                 connect_to_mqtt_broker(); 
            }
        }

        if (bits & MQTT_DISCONNECTED_BIT) {
            APP_LOG_NET_INFO("MQTT disconnected bit set (Wi-Fi may still be connected).");
            mqtt_server_connected = false;
            report_server_disconnected_event();
            // 如果 Wi-Fi 仍处于连接状态，则尝试重新连接 MQTT
            if (wifi_connected) {
                connect_to_mqtt_broker();
            }
        }
        
        if (bits & WIFI_CONNECTED_BIT) {
            APP_LOG_NET_INFO("Wi-Fi connected bit set.");
            // 如果 Wi-Fi 刚刚连接，并且 MQTT 未连接，则尝试连接 MQTT
            if (!mqtt_server_connected) {
                connect_to_mqtt_broker();
            }
        }

        if (bits & MQTT_CONNECTED_BIT) {
             APP_LOG_NET_INFO("MQTT connected bit set.");
             // 已由回调函数设置标志并报告事件来处理
        }

        
    } // while(1) 循环结束

    // 清理
    APP_LOG_NET_INFO("Network task shutting down...");
    if (mqtt_server_connected) {
        cy_mqtt_disconnect(mqtt_connection_handle);
    }
    cy_mqtt_delete(mqtt_connection_handle);
    cy_mqtt_deinit();
    if (wifi_connected) {
        cy_wcm_disconnect_ap();
    }
    cy_wcm_deinit();
    vEventGroupDelete(network_event_group);
    APP_LOG_NET_INFO("Network task finished.");
    vTaskDelete(NULL);
}

static void generate_client_id(void){
    // 简单的客户端 ID 生成：前缀 + MAC 地址的最后几个字节
    // 生产环境可能需要更健壮的唯一 ID 生成方式。
    uint8_t mac_address[6];
    if (cy_wcm_get_mac_addr(CY_WCM_INTERFACE_TYPE_STA, (cy_wcm_mac_t*)mac_address) == CY_RSLT_SUCCESS) {
         snprintf(mqtt_client_id_buffer, sizeof(mqtt_client_id_buffer), "%s-%02X%02X%02X", 
                 MQTT_CLIENT_ID_PREFIX, mac_address[3], mac_address[4], mac_address[5]);
    } else {
        // 如果无法读取 MAC 地址的备用方案
        snprintf(mqtt_client_id_buffer, sizeof(mqtt_client_id_buffer), "%s-%lu", MQTT_CLIENT_ID_PREFIX, (unsigned long)xTaskGetTickCount());
    }
    APP_LOG_NET_INFO("Generated MQTT Client ID: %s", mqtt_client_id_buffer);
}

static cy_rslt_t connect_to_wifi(void) {
    if (wifi_connected) return CY_RSLT_SUCCESS;

    APP_LOG_NET_INFO("Connecting to Wi-Fi AP: %s", WIFI_SSID);
    cy_wcm_connect_params_t connect_params = {
        .ap_credentials.SSID = WIFI_SSID,
        .ap_credentials.password = WIFI_PASSWORD,
        .ap_credentials.security = WIFI_SECURITY,
        // .BSSID - 未指定，连接到具有该 SSID 的任何 AP
    };

    cy_rslt_t result = CY_RSLT_SUCCESS; 
    for (int retries = 0; retries < 5; retries++) { // 在再次报告失败前的有限重试次数
        result = cy_wcm_connect_ap(&connect_params, NULL);
        if (result == CY_RSLT_SUCCESS) {
            APP_LOG_NET_INFO("Successfully connected to Wi-Fi AP.");
            wifi_connected = true;
            report_wifi_connected_event();
            xEventGroupSetBits(network_event_group, WIFI_CONNECTED_BIT);
            return CY_RSLT_SUCCESS;
        } else {
            APP_LOG_NET_ERROR("Wi-Fi connection failed (attempt %d): 0x%08X. Retrying in 5s...", retries + 1, (unsigned int)result);
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
    
    APP_LOG_NET_ERROR("Failed to connect to Wi-Fi after multiple retries.");
    wifi_connected = false;
    report_wifi_disconnected_event();
    return result; // 返回最后一个错误
}

static cy_rslt_t connect_to_mqtt_broker(void) {
    if (!wifi_connected) {
        APP_LOG_NET_INFO("Wi-Fi not connected, cannot connect to MQTT broker.");
        // 使用 WCM 基础错误。应检查 cy_wcm_error.h 以获取最合适的代码。
        return CY_RSLT_MODULE_WCM_BASE; 
    }
    if (mqtt_server_connected) return CY_RSLT_SUCCESS;

    APP_LOG_NET_INFO("Connecting to MQTT broker: %s", MQTT_BROKER_ADDRESS);
    cy_rslt_t result = CY_RSLT_SUCCESS;

    for (int retries = 0; retries < 3; retries++) { // 有限重试次数
        result = cy_mqtt_connect(mqtt_connection_handle, &connection_info);
        if (result == CY_RSLT_SUCCESS) {
            APP_LOG_NET_INFO("Successfully connected to MQTT Broker.");
            mqtt_server_connected = true;
            report_server_connected_event();
            xEventGroupSetBits(network_event_group, MQTT_CONNECTED_BIT);
            return CY_RSLT_SUCCESS;
        }
        APP_LOG_NET_ERROR("MQTT connection failed (attempt %d): 0x%08X. Retrying in 3s...", retries + 1, (unsigned int)result);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    APP_LOG_NET_ERROR("Failed to connect to MQTT broker after multiple retries.");
    mqtt_server_connected = false;
    report_server_disconnected_event();
    return result; // 返回最后一个错误
}

static void mqtt_event_callback(cy_mqtt_t mqtt_handle, cy_mqtt_event_t event, void *user_data) {
    (void)mqtt_handle;
    (void)user_data;

    switch (event.type) {
        case CY_MQTT_EVENT_TYPE_DISCONNECT:
            // 根据提供的 cy_mqtt_event_t 结构定义，通过 event.data.reason 访问断开连接原因
            APP_LOG_NET_INFO("MQTT Event: Disconnected. Reason code: %d", (int)event.data.reason);
            // 此回调可由各种断开连接原因触发。
            // 如果是意外断开连接（非用户启动）：
            if (mqtt_server_connected) { // 此事件发生前已连接
                mqtt_server_connected = false;
                // 向状态机报告，网络任务循环将处理重连尝试逻辑
                xEventGroupSetBits(network_event_group, MQTT_DISCONNECTED_BIT);
            }
            // 如果 Wi-Fi 也已关闭，则 Wi-Fi 断开连接事件应处理相关事宜。
            // 检查 WCM 是否仍报告 AP 已连接。
            if(cy_wcm_is_connected_to_ap() == 0 && wifi_connected){
                APP_LOG_NET_INFO("MQTT disconnected and Wi-Fi also seems down.");
                wifi_connected = false;
                xEventGroupSetBits(network_event_group, WIFI_DISCONNECTED_BIT);
            }
            break;
        // 根据文档使用 CY_MQTT_EVENT_TYPE_SUBSCRIPTION_MESSAGE_RECEIVE
        case CY_MQTT_EVENT_TYPE_SUBSCRIPTION_MESSAGE_RECEIVE: 
            // 根据 cy_mqtt_api.h (v4.6.1) 的定义，通过 event.data.pub_msg.received_message 访问接收到的消息信息
            APP_LOG_NET_INFO("MQTT Event: Publish message received on topic '%.*s'", 
                             (int)event.data.pub_msg.received_message.topic_len, 
                             (const char*)event.data.pub_msg.received_message.topic);
            // 如果订阅了任何主题，则处理传入消息
            break;
        // 在提供的文档的事件类型中没有明确的 PUBACK 事件。
        // PINGRESP 是列出的另一种事件类型，但当前未处理。
        /* case CY_MQTT_EVENT_TYPE_PINGRESP:
            APP_LOG_NET_INFO("MQTT Event: PINGRESP received");
            break; */
        default:
            APP_LOG_NET_INFO("MQTT Event: Unknown event type %d", event.type);
            break;
    }
}

// 当状态机进入 WIFI_DISCONNECTED 状态时调用
void network_notify_wifi_lost(void) {
    APP_LOG_NET_INFO("State machine notification: Wi-Fi lost.");
    // 如果事件组已捕获此事件，这可能是多余的，但可确保一致性。
    if (wifi_connected || mqtt_server_connected) { // 如果我们认为已连接
        xEventGroupSetBits(network_event_group, WIFI_DISCONNECTED_BIT);
    }
}

// 当状态机进入 SERVER_DISCONNECTED 状态时调用
// (例如，如果显式服务器健康检查失败，或者 Wi-Fi 已连接但 MQTT 在重试后无法连接)
void network_notify_server_lost(void) {
    APP_LOG_NET_INFO("State machine notification: Server lost.");
    if (mqtt_server_connected) { // 如果我们认为 MQTT 已连接
        xEventGroupSetBits(network_event_group, MQTT_DISCONNECTED_BIT);
    }
} 