#ifndef NETWORK_TASK_H_
#define NETWORK_TASK_H_

#include "FreeRTOS.h"
#include "queue.h"
#include "state_machine.h" // 用于报告网络事件

// 网络任务的命令枚举 (如果需要，例如从状态机发送)
// typedef enum {
//     NETWORK_CMD_CONNECT_WIFI,       // 连接 Wi-Fi 命令
//     NETWORK_CMD_DISCONNECT_WIFI,    // 断开 Wi-Fi 命令
//     NETWORK_CMD_CONNECT_MQTT,       // 连接 MQTT 命令
//     NETWORK_CMD_DISCONNECT_MQTT     // 断开 MQTT 命令
// } network_command_t;

// extern QueueHandle_t network_command_queue; // 可选：用于发送命令到网络任务的队列

void network_task(void *pvParameters);

// 由状态机调用以影响网络行为的函数
void network_notify_wifi_lost(void); // 当状态机进入 WIFI_DISCONNECTED 状态时调用
void network_notify_server_lost(void); // 当状态机进入 SERVER_DISCONNECTED 状态时调用

#endif /* NETWORK_TASK_H_ */ 