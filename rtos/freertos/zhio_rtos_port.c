/*
 * zhio_rtos_port.c - RTOS 抽象层 FreeRTOS 端口
 *
 * 将 zhios_rtos.h 抽象层映射到 FreeRTOS V11.2.0+ 原生 API。
 * 配合各 BSP（stm32h743/h755/mcxn947 等）即可在真实 MCU 上运行。
 * 需在 FreeRTOSConfig.h 中启用相应功能（队列/互斥量/信号量/软件定时器）。
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include "zhios_rtos.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* ---------------- 时间 ---------------- */
ZhiosTick_t zhio_get_tick(void) { return (ZhiosTick_t)xTaskGetTickCount(); }
ZhiosTick_t zhio_ms_to_ticks(uint32_t ms) { return (ZhiosTick_t)pdMS_TO_TICKS(ms); }

/* ---------------- 内存 ---------------- */
void *zhio_malloc(size_t s) { return pvPortMalloc(s); }
void  zhio_free(void *p)    { vPortFree(p); }

/* ---------------- 日志 ---------------- */
void zhio_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
}

/* ---------------- 临界区 / 中断 ---------------- */
void zhio_critical_enter(void) { taskENTER_CRITICAL(); }
void zhio_critical_exit(void)  { taskEXIT_CRITICAL(); }
int  zhio_in_isr(void)         { return xPortIsInsideInterrupt(); }

/* ---------------- 任务 ---------------- */
int zhio_task_create(ZhiosTaskHandle_t *handle, const ZhiosTaskParams_t *p)
{
    TaskHandle_t h = NULL;
    BaseType_t rc = xTaskCreate((TaskFunction_t)p->fn, (const char *)p->name,
                                (configSTACK_DEPTH_TYPE)(p->stack_size),
                                p->param, (UBaseType_t)p->priority, &h);
    if (rc != pdPASS) return ZHIO_E_NOMEM;
    if (handle) *handle = (ZhiosTaskHandle_t)h;
    return ZHIO_OK;
}
void zhio_task_delete(ZhiosTaskHandle_t handle) { vTaskDelete((TaskHandle_t)handle); }
void zhio_task_sleep(ZhiosTick_t ticks) { vTaskDelay(ticks); }
uint32_t zhio_task_priority_get(ZhiosTaskHandle_t h) { return (uint32_t)uxTaskPriorityGet((TaskHandle_t)h); }
void zhio_task_priority_set(ZhiosTaskHandle_t h, uint32_t p) { vTaskPrioritySet((TaskHandle_t)h, (UBaseType_t)p); }
ZhiosTaskHandle_t zhio_task_current(void) { return (ZhiosTaskHandle_t)xTaskGetCurrentTaskHandle(); }

/* ---------------- 队列 ---------------- */
ZhiosQueueHandle_t zhio_queue_create(uint32_t item_count, uint32_t item_size)
{
    return (ZhiosQueueHandle_t)xQueueCreate(item_count, item_size);
}
int zhio_queue_send(ZhiosQueueHandle_t q, const void *item, ZhiosTick_t timeout)
{
    return (xQueueSend((QueueHandle_t)q, item, timeout) == pdTRUE) ? ZHIO_OK : ZHIO_E_TIMEOUT;
}
int zhio_queue_send_isr(ZhiosQueueHandle_t q, const void *item)
{
    BaseType_t w = pdFALSE;
    BaseType_t ok = xQueueSendFromISR((QueueHandle_t)q, item, &w);
    portYIELD_FROM_ISR(w);
    return ok == pdTRUE ? ZHIO_OK : ZHIO_E_BUSY;
}
int zhio_queue_receive(ZhiosQueueHandle_t q, void *item, ZhiosTick_t timeout)
{
    return (xQueueReceive((QueueHandle_t)q, item, timeout) == pdTRUE) ? ZHIO_OK : ZHIO_E_TIMEOUT;
}
void zhio_queue_reset(ZhiosQueueHandle_t q) { xQueueReset((QueueHandle_t)q); }

/* ---------------- 互斥量 ---------------- */
ZhiosMutexHandle_t zhio_mutex_create(void) { return (ZhiosMutexHandle_t)xSemaphoreCreateMutex(); }
int  zhio_mutex_lock(ZhiosMutexHandle_t m, ZhiosTick_t timeout)
{
    return (xSemaphoreTake((SemaphoreHandle_t)m, timeout) == pdTRUE) ? ZHIO_OK : ZHIO_E_TIMEOUT;
}
void zhio_mutex_unlock(ZhiosMutexHandle_t m) { xSemaphoreGive((SemaphoreHandle_t)m); }

/* ---------------- 信号量 ---------------- */
ZhiosSemHandle_t zhio_sem_create(void) { return (ZhiosSemHandle_t)xSemaphoreCreateBinary(); }
int zhio_sem_give(ZhiosSemHandle_t s) { return (xSemaphoreGive((SemaphoreHandle_t)s) == pdTRUE) ? ZHIO_OK : ZHIO_E_UNKNOWN; }
int zhio_sem_give_isr(ZhiosSemHandle_t s)
{
    BaseType_t w = pdFALSE;
    BaseType_t ok = xSemaphoreGiveFromISR((SemaphoreHandle_t)s, &w);
    portYIELD_FROM_ISR(w);
    return ok == pdTRUE ? ZHIO_OK : ZHIO_E_UNKNOWN;
}
int zhio_sem_take(ZhiosSemHandle_t s, ZhiosTick_t timeout)
{
    return (xSemaphoreTake((SemaphoreHandle_t)s, timeout) == pdTRUE) ? ZHIO_OK : ZHIO_E_TIMEOUT;
}

/* ---------------- 看门狗（软实现，可用硬件看门狗替换） ---------------- */
int  zhio_watchdog_start(uint32_t period_ms) { (void)period_ms; return ZHIO_OK; }
void zhio_watchdog_feed(void) {}
