/*
 * FreeRTOS Kernel V10.5.0
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 * Copyright (C) 2019-2024 Cypress Semiconductor Corporation, or a subsidiary of
 * Cypress Semiconductor Corporation.  All Rights Reserved.
 *
 * 已更新配置以支持 CM4。
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 * http://www.cypress.com
 *
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*-----------------------------------------------------------
 * 特定于应用的定义。
 *
 * 这些定义应根据您的特定硬件和应用需求进行调整。
 *
 * 这些参数在 FreeRTOS.org 网站上的 FreeRTOS API 文档的"配置"部分中有描述。
 *
 * 请参阅 http://www.freertos.org/a00110.html。
 *----------------------------------------------------------*/

#include "cy_utils.h"

/* 从 ModusToolbox 设备配置器生成的源文件中获取低功耗配置参数：
 * CY_CFG_PWR_SYS_IDLE_MODE     - 系统空闲功耗模式
 * CY_CFG_PWR_DEEPSLEEP_LATENCY - 深度睡眠延迟 (毫秒)
 */
#include "cycfg_system.h"

// #warning 这是一个模板。请将此文件复制到您的项目中并删除此行。有关用法详情，请参阅 FreeRTOS README.md。

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
extern uint32_t SystemCoreClock;
#define configCPU_CLOCK_HZ                      SystemCoreClock
#define configTICK_RATE_HZ                      1000u
#define configMAX_PRIORITIES                    7
#define configMINIMAL_STACK_SIZE                128
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               10
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIME_SLICING                  1
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 5

/* 内存分配相关定义。 */
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   10240
#define configAPPLICATION_ALLOCATED_HEAP        0

/* Hook 函数相关定义。 */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0

/* 运行时和任务统计信息收集相关定义。 */
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

/* 协程相关定义。 */
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         1

/* 软件定时器相关定义。 */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               3
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            ( configMINIMAL_STACK_SIZE * 2 )

/*
中断嵌套行为配置。
相关说明请见：http://www.freertos.org/a00110.html

优先级由两个宏控制：
- configKERNEL_INTERRUPT_PRIORITY 决定 RTOS 守护进程任务的优先级
- configMAX_API_CALL_INTERRUPT_PRIORITY 指定进行 API 调用的 ISR 的优先级

注意：
1. 不调用 API 函数的中断应具有 >= configKERNEL_INTERRUPT_PRIORITY 的优先级，并且会嵌套。
2. 调用 API 函数的中断必须具有介于 KERNEL_INTERRUPT_PRIORITY 和 MAX_API_CALL_INTERRUPT_PRIORITY（含两者）之间的优先级。
3. 运行优先级高于 MAX_API_CALL_INTERRUPT_PRIORITY 的中断永远不会被操作系统延迟。
*/
/*
PSoC 6 __NVIC_PRIO_BITS = 3

0 (高)
1           MAX_API_CALL_INTERRUPT_PRIORITY 001xxxxx (0x3F)
2
3
4
5
6
7 (低)     KERNEL_INTERRUPT_PRIORITY       111xxxxx (0xFF)


CAT3 XMC devices __NVIC_PRIO_BITS = 6

0 (高)
1           MAX_API_CALL_INTERRUPT_PRIORITY 000001xx (0x07)
..
..
..
..
63 (低)    KERNEL_INTERRUPT_PRIORITY       111111xx (0xFF)

!!!! configMAX_SYSCALL_INTERRUPT_PRIORITY 不能设置为零 !!!!
请参阅 http://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html

*/

/* 将 KERNEL_INTERRUPT_PRIORITY 放入 CM4 寄存器的最高 __NVIC_PRIO_BITS 位 */
#define configKERNEL_INTERRUPT_PRIORITY         0xFF
/*
将 MAX_SYSCALL_INTERRUPT_PRIORITY 放入 CM4 寄存器的最高 __NVIC_PRIO_BITS 位
注意：对于 IAR 编译器，请确保此宏的更改反映在
文件 portable\TOOLCHAIN_IAR\COMPONENT_CM4\portasm.s 中的 PendSV_Handler: 例程中
*/
#ifdef COMPONENT_CAT3
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    0x07
#else
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    0x3F
#endif
/* configMAX_API_CALL_INTERRUPT_PRIORITY 是 configMAX_SYSCALL_INTERRUPT_PRIORITY 的新名称，
仅由较新的移植版本使用。两者是等效的。 */
#define configMAX_API_CALL_INTERRUPT_PRIORITY   configMAX_SYSCALL_INTERRUPT_PRIORITY


/* 将以下定义设置为 1 以包含 API 函数，或设置为 0 以排除 API 函数。 */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xResumeFromISR                  1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     0
#define INCLUDE_xTaskGetIdleTaskHandle          0
#define INCLUDE_eTaskGetState                   0
#define INCLUDE_xEventGroupSetBitFromISR        1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskAbortDelay                 0
#define INCLUDE_xTaskGetHandle                  0
#define INCLUDE_xTaskResumeFromISR              1

/* 普通 assert() 语义，不依赖于 assert.h 头文件的提供。 */
#if defined(NDEBUG)
#define configASSERT( x ) CY_UNUSED_PARAMETER( x )
#else
#define configASSERT( x ) if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); CY_HALT(); }
#endif

/* 将 FreeRTOS 端口中断处理程序映射到其 CMSIS 标准名称的定义 
- 或者至少是未修改的向量表中使用的那些名称。 */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

/* 动态内存分配方案 */
#define HEAP_ALLOCATION_TYPE1                   (1)     /* heap_1.c */
#define HEAP_ALLOCATION_TYPE2                   (2)     /* heap_2.c */
#define HEAP_ALLOCATION_TYPE3                   (3)     /* heap_3.c */
#define HEAP_ALLOCATION_TYPE4                   (4)     /* heap_4.c */
#define HEAP_ALLOCATION_TYPE5                   (5)     /* heap_5.c */
#define NO_HEAP_ALLOCATION                      (0)

#define configHEAP_ALLOCATION_SCHEME            (HEAP_ALLOCATION_TYPE3)

/* 检查 ModusToolbox 设备配置器电源特性参数
 * "系统空闲功耗模式"是否设置为"CPU 睡眠"或"系统深度睡眠"。
 */
#if defined(CY_CFG_PWR_SYS_IDLE_MODE) && \
    ((CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_SLEEP) || \
     (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP))

/* 使能低功耗 tickless 功能。RTOS 抽象库
 * 提供了 vApplicationSleep Hook 函数的兼容实现：
 * https://github.com/infineon/abstraction-rtos#freertos
 * 低功耗助手库为 PSoC 6 设备支持的低功耗特性
 * 提供了额外的可移植配置层：
 * https://github.com/infineon/lpa
 */
extern void vApplicationSleep( uint32_t xExpectedIdleTime );
#define portSUPPRESS_TICKS_AND_SLEEP( xIdleTime ) vApplicationSleep( xIdleTime )
#define configUSE_TICKLESS_IDLE                 2

#else
#define configUSE_TICKLESS_IDLE                 0
#endif

/* 深度睡眠延迟配置 */
#if( CY_CFG_PWR_DEEPSLEEP_LATENCY > 0 )
#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP   CY_CFG_PWR_DEEPSLEEP_LATENCY
#endif

/* 为每个 RTOS 任务分配 newlib 可重入结构。
 * 系统行为特定于工具链。
 *
 * GCC 工具链：应用程序必须为所需的 newlib Hook 函数提供实现：
 * __malloc_lock, __malloc_unlock, __env_lock, __env_unlock。
 * clib-support 库提供了 FreeRTOS 兼容的实现：
 * https://github.com/infineon/clib-support
 *
 * ARM/IAR 工具链：应用程序必须提供 reent.h 头文件，以使 FreeRTOS 的
 * configUSE_NEWLIB_REENTRANT 适应工具链特定的 C 库。
 * clib-support 库也提供了兼容的实现。
 */
#define configUSE_NEWLIB_REENTRANT              1

#endif /* FREERTOS_CONFIG_H */
