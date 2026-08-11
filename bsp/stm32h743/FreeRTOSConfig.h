/*
 * bsp/stm32h743/FreeRTOSConfig.h - STM32H743 平台 FreeRTOS 配置
 *
 * 对应《16-交叉编译与工具链配置文档》。ZhiOS-AF 各容量上限见 include/zhios_config.h，
 * 此处配置 FreeRTOS 内核本身的裁剪项（队列/互斥量/信号量/软件定时器/堆）。
 * 生产项目通常由 FreeRTOS 内核源码携带一份默认配置，本项目在此提供面向
 * Cortex-M7 的推荐参数。
 *
 * =============================================================================
 * 模块说明（维护入口）
 * -----------------------------------------------------------------------------
 * 职责     ：裁剪 FreeRTOS 内核以匹配 STM32H743（Cortex-M7）。
 * 实时指标 ：对应《33-操作系统技术指标体系设计文档》4.1/4.2：
 *             - configPRIO_BITS=4、configMAX_SYSCALL_INTERRUPT_PRIORITY 保证
 *               内核临界区可屏蔽可调度中断，ISR 可安全调用 FromISR API；
 *             - configCHECK_FOR_STACK_OVERFLOW=2（方法 2）做栈溢出检测，
 *               生产建议进一步用 MPU 栈守护子区域 + 汇编 Canary（见《33》4.2/4.6）；
 *             - configASSERT 失败即关中断死循环（便于看门狗复位，见 4.6）。
 * 待落地    ：configTOTAL_HEAP_SIZE=128KB 为动态堆；硬实时关键任务请改用静态栈
 *             （configSUPPORT_STATIC_ALLOCATION=1 已启用）。
 * =============================================================================
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ---- 内核基础 ---- */
#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ                      (480000000UL)
#define configTICK_RATE_HZ                      (1000)
#define configMAX_PRIORITIES                    (32)
#define configMINIMAL_STACK_SIZE                (128)
#define configMAX_TASK_NAME_LEN                 (16)
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1

/* ---- 内存 ---- */
#define configTOTAL_HEAP_SIZE                   (128UL * 1024UL)  /* 128KB 堆 */
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_APPLICATION_TASK_TAG          0

/* ---- 队列/互斥量/信号量 ---- */
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_QUEUE_SETS                    1

/* ---- 软件定时器（看门狗/超时用） ---- */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            256

/* ---- 任务通知（消息总线/信号量可选用） ---- */
#define configUSE_TASK_NOTIFICATIONS            1

/* ---- 内存管理 ---- */
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1

/* ---- 跟踪/断言 ---- */
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configASSERT(x)                         if ((x) == 0) { taskDISABLE_INTERRUPTS(); for (;;); }

/* ---- 中断优先级（Cortex-M7） ---- */
#define configPRIO_BITS                         4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY         (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* ---- 可选：向 ZhiOS-AF 暴露 tick 转换 ---- */
#define portmacro.h_portTICK_TYPE_IS_ATOMIC 1

#endif /* FREERTOS_CONFIG_H */
