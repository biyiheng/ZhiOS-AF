/*
 * zhios_rtos.h - ZhiOS-AF RTOS 抽象层（可替换模块接口）
 *
 * 这是 ZhiOS-AF 跨平台可移植性的核心接口。所有内核/AI/Agent 代码
 * 只依赖本抽象层，不直接依赖 FreeRTOS 或任何具体 RTOS，从而支持：
 *   - FreeRTOS V11.2.0+（真实 MCU 目标）
 *   - 主机仿真（pthread/模拟，用于单元测试 ZTEST / QEMU ZSim）
 *   - 未来替换为 Zephyr / RT-Thread 等其他内核
 *
 * 移植方式：为每种 RTOS 提供一个《rtos/<name>/zhios_rtos_port.c》，
 * 实现本头文件声明的全部函数即可（见 rtos/freertos 与 rtos/host）。
 */
#ifndef ZHIO_RTOS_H
#define ZHIO_RTOS_H

#include <stdint.h>
#include <stddef.h>
#include "zhios_config.h"
#include "zhios_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 句柄类型 ---- */
typedef void *ZhiosTaskHandle_t;
typedef void *ZhiosQueueHandle_t;
typedef void *ZhiosMutexHandle_t;
typedef void *ZhiosSemHandle_t;
typedef void *ZhiosTimerHandle_t;

/* ---- tick 类型与宏 ---- */
typedef uint32_t ZhiosTick_t;
#define ZHIO_MAX_DELAY       0xFFFFFFFFUL
#define ZHIO_MS_TO_TICKS(ms) zhio_ms_to_ticks((ms))

/* ---- 任务 ---- */
typedef void (*ZhiosTaskFn_t)(void *param);

/* 任务创建参数（含优先级；本抽象层支持 0..ZHIO_RTOS_MAX_PRIO-1） */
typedef struct {
    const char *name;
    uint32_t    stack_size;    /* 栈大小（字） */
    uint32_t    priority;      /* 0 最低 */
    ZhiosTaskFn_t fn;
    void        *param;
} ZhiosTaskParams_t;

int         zhio_task_create(ZhiosTaskHandle_t *handle, const ZhiosTaskParams_t *p);
void        zhio_task_delete(ZhiosTaskHandle_t handle);
void        zhio_task_sleep(ZhiosTick_t ticks);
uint32_t    zhio_task_priority_get(ZhiosTaskHandle_t handle);
void        zhio_task_priority_set(ZhiosTaskHandle_t handle, uint32_t prio);
ZhiosTaskHandle_t zhio_task_current(void);

/* ---- 时间 ---- */
ZhiosTick_t zhio_get_tick(void);
ZhiosTick_t zhio_ms_to_ticks(uint32_t ms);

/* ---- 队列 ---- */
ZhiosQueueHandle_t zhio_queue_create(uint32_t item_count, uint32_t item_size);
int   zhio_queue_send(ZhiosQueueHandle_t q, const void *item, ZhiosTick_t timeout);
int   zhio_queue_send_isr(ZhiosQueueHandle_t q, const void *item);
int   zhio_queue_receive(ZhiosQueueHandle_t q, void *item, ZhiosTick_t timeout);
void  zhio_queue_reset(ZhiosQueueHandle_t q);

/* ---- 互斥量 ---- */
ZhiosMutexHandle_t zhio_mutex_create(void);
int  zhio_mutex_lock(ZhiosMutexHandle_t m, ZhiosTick_t timeout);
void zhio_mutex_unlock(ZhiosMutexHandle_t m);

/* ---- 信号量 ---- */
ZhiosSemHandle_t zhio_sem_create(void);
int  zhio_sem_give(ZhiosSemHandle_t s);
int  zhio_sem_give_isr(ZhiosSemHandle_t s);
int  zhio_sem_take(ZhiosSemHandle_t s, ZhiosTick_t timeout);

/* ---- 临界区 / 中断 ---- */
void        zhio_critical_enter(void);
void        zhio_critical_exit(void);
int         zhio_in_isr(void);

/* ---- 系统内存堆（用于初始化期一次性分配，非推理热路径） ---- */
void       *zhio_malloc(size_t size);
void        zhio_free(void *ptr);

/* ---- 日志（可被 BSP/监控替换） ---- */
void zhio_log(const char *fmt, ...);

/* ---- 看门狗（可选，BSP 可实现硬件/软看门狗） ---- */
int  zhio_watchdog_start(uint32_t period_ms);
void zhio_watchdog_feed(void);

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_RTOS_H */
