/*
 * zhio_rtos_port.c - RTOS 抽象层 主机仿真端口
 *
 * 使用 pthread + 条件变量实现，用于：
 *   - ZTEST 单元测试在主机上运行
 *   - ZSim (QEMU) 仿真
 *   - 无硬件环境的功能验证
 *
 * zhio_get_tick() 返回自进程启动以来的毫秒数（单调时钟）。
 * 队列/信号量/互斥量基于线程安全实现。
 *
 * =============================================================================
 * 模块说明（维护入口）
 * -----------------------------------------------------------------------------
 * 职责     ：把跨平台 RTOS 抽象层（zhios_rtos.h）映射到 pthread/host 语义，
 *             与 rtos/freertos/zhio_rtos_port.c 互为可替换实现。
 * 依赖     ：pthread、time、errno（Windows 用 QueryPerformanceCounter 高精度时钟）。
 * 被谁调用 ：上层所有通过 zhios_rtos.h 使用内核服务的模块；host BSP 与 ZTEST/ZSim。
 * 调度/内存：任务创建/信号量/互斥/队列在此实现；zhio_malloc 桥接 libc 堆。
 *             主机端用于逻辑/功能验证，真实硬实时语义由 FreeRTOS 端口提供
 *             （对应《33》4.1 调度 / 4.2 内存 维度的落地载体）。
 * =============================================================================
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include "zhios_rtos.h"

#ifdef _WIN32
  #define ZHIO_WIN 1
#else
  #include <unistd.h>
  #define usleep_us(us) usleep(us)
#endif

/* ---------------- 时间 ---------------- */
static uint64_t zhio_now_ms(void)
{
#if defined(_WIN32)
    /* Windows 高精度时钟 */
    static int inited = 0;
    static LARGE_INTEGER freq, start;
    LARGE_INTEGER now;
    if (!inited) { QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&start); inited = 1; }
    QueryPerformanceCounter(&now);
    return (uint64_t)((now.QuadPart - start.QuadPart) * 1000ULL / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
#endif
}

ZhiosTick_t zhio_get_tick(void) { return (ZhiosTick_t)zhio_now_ms(); }

ZhiosTick_t zhio_ms_to_ticks(uint32_t ms) { return (ZhiosTick_t)ms; }

/* ---------------- 内存 ---------------- */
void *zhio_malloc(size_t s) { return malloc(s); }
void  zhio_free(void *p)    { free(p); }

/* ---------------- 日志 ---------------- */
void zhio_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ---------------- 临界区 / 中断标志 ---------------- */
static pthread_mutex_t g_crit = PTHREAD_MUTEX_INITIALIZER;
void zhio_critical_enter(void) { pthread_mutex_lock(&g_crit); }
void zhio_critical_exit(void)  { pthread_mutex_unlock(&g_crit); }
int  zhio_in_isr(void)         { return 0; }   /* host 无中断上下文 */

/* ---------------- 任务 ---------------- */
typedef struct {
    pthread_t      thread;
    ZhiosTaskFn_t  fn;
    void          *param;
    uint32_t       priority;
    int            running;
} HostTask_t;

static void *host_task_entry(void *arg)
{
    HostTask_t *t = (HostTask_t *)arg;
    t->running = 1;
    t->fn(t->param);
    t->running = 0;
    return NULL;
}

int zhio_task_create(ZhiosTaskHandle_t *handle, const ZhiosTaskParams_t *p)
{
    HostTask_t *t = (HostTask_t *)calloc(1, sizeof(HostTask_t));
    if (!t) return ZHIO_E_NOMEM;
    t->fn = p->fn;
    t->param = p->param;
    t->priority = p->priority;
    t->running = 0;
    if (pthread_create(&t->thread, NULL, host_task_entry, t) != 0) {
        free(t);
        return ZHIO_E_UNKNOWN;
    }
    *handle = t;
    return ZHIO_OK;
}

void zhio_task_delete(ZhiosTaskHandle_t handle)
{
    HostTask_t *t = (HostTask_t *)handle;
    if (!t) return;
    if (t->running) pthread_join(t->thread, NULL);
    free(t);
}

void zhio_task_sleep(ZhiosTick_t ticks)
{
#if defined(_WIN32)
    Sleep((DWORD)ticks);
#else
    usleep(ticks * 1000UL);
#endif
}

uint32_t zhio_task_priority_get(ZhiosTaskHandle_t h) { return ((HostTask_t*)h)->priority; }
void     zhio_task_priority_set(ZhiosTaskHandle_t h, uint32_t p) { ((HostTask_t*)h)->priority = p; }
ZhiosTaskHandle_t zhio_task_current(void) { return NULL; }

/* ---------------- 队列 ---------------- */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
    uint32_t        count, max, item_size;
    uint8_t        *items;
    uint32_t        head, tail;
} HostQueue_t;

ZhiosQueueHandle_t zhio_queue_create(uint32_t item_count, uint32_t item_size)
{
    HostQueue_t *q = (HostQueue_t*)calloc(1, sizeof(HostQueue_t));
    if (!q) return NULL;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    q->max = item_count; q->item_size = item_size; q->count = 0; q->head = q->tail = 0;
    q->items = (uint8_t*)malloc(item_count * item_size);
    if (!q->items) { free(q); return NULL; }
    return q;
}

int zhio_queue_send(ZhiosQueueHandle_t h, const void *item, ZhiosTick_t timeout)
{
    HostQueue_t *q = (HostQueue_t*)h;
    if (!q || !item) return ZHIO_E_INVAL;
    pthread_mutex_lock(&q->lock);
    while (q->count >= q->max) {
        if (timeout == 0) { pthread_mutex_unlock(&q->lock); return ZHIO_E_TIMEOUT; }
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += (long)(timeout % 1000) * 1000000L + (timeout/1000) * 1000000000L;
        ts.tv_sec += (time_t)(timeout / 1000) + (ts.tv_nsec >= 1000000000L ? 1 : 0);
        ts.tv_nsec %= 1000000000L;
        if (pthread_cond_timedwait(&q->not_full, &q->lock, &ts) != 0) {
            pthread_mutex_unlock(&q->lock); return ZHIO_E_TIMEOUT;
        }
    }
    memcpy(q->items + q->tail * q->item_size, item, q->item_size);
    q->tail = (q->tail + 1) % q->max; q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return ZHIO_OK;
}

int zhio_queue_send_isr(ZhiosQueueHandle_t h, const void *item)
{
    HostQueue_t *q = (HostQueue_t*)h;
    pthread_mutex_lock(&q->lock);
    if (q->count >= q->max) { pthread_mutex_unlock(&q->lock); return ZHIO_E_BUSY; }
    memcpy(q->items + q->tail * q->item_size, item, q->item_size);
    q->tail = (q->tail + 1) % q->max; q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return ZHIO_OK;
}

int zhio_queue_receive(ZhiosQueueHandle_t h, void *item, ZhiosTick_t timeout)
{
    HostQueue_t *q = (HostQueue_t*)h;
    if (!q || !item) return ZHIO_E_INVAL;
    pthread_mutex_lock(&q->lock);
    while (q->count == 0) {
        if (timeout == 0) { pthread_mutex_unlock(&q->lock); return ZHIO_E_TIMEOUT; }
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += (long)(timeout % 1000) * 1000000L + (timeout/1000) * 1000000000L;
        ts.tv_sec += (time_t)(timeout / 1000) + (ts.tv_nsec >= 1000000000L ? 1 : 0);
        ts.tv_nsec %= 1000000000L;
        if (pthread_cond_timedwait(&q->not_empty, &q->lock, &ts) != 0) {
            pthread_mutex_unlock(&q->lock); return ZHIO_E_TIMEOUT;
        }
    }
    memcpy(item, q->items + q->head * q->item_size, q->item_size);
    q->head = (q->head + 1) % q->max; q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    return ZHIO_OK;
}

void zhio_queue_reset(ZhiosQueueHandle_t h)
{
    HostQueue_t *q = (HostQueue_t*)h;
    pthread_mutex_lock(&q->lock); q->count = 0; q->head = q->tail = 0;
    pthread_cond_broadcast(&q->not_full); pthread_mutex_unlock(&q->lock);
}

/* ---------------- 互斥量 ---------------- */
ZhiosMutexHandle_t zhio_mutex_create(void)
{
    pthread_mutex_t *m = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    if (!m) return NULL;
    pthread_mutex_init(m, NULL);
    return m;
}
int  zhio_mutex_lock(ZhiosMutexHandle_t m, ZhiosTick_t timeout)
{
    if (!m) return ZHIO_E_INVAL;
    return pthread_mutex_lock((pthread_mutex_t*)m) == 0 ? ZHIO_OK : ZHIO_E_UNKNOWN;
}
void zhio_mutex_unlock(ZhiosMutexHandle_t m)
{
    if (m) pthread_mutex_unlock((pthread_mutex_t*)m);
}

/* ---------------- 信号量 ---------------- */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    int             count;
} HostSem_t;

ZhiosSemHandle_t zhio_sem_create(void)
{
    HostSem_t *s = (HostSem_t*)calloc(1, sizeof(HostSem_t));
    if (!s) return NULL;
    pthread_mutex_init(&s->lock, NULL); pthread_cond_init(&s->cond, NULL); s->count = 0;
    return s;
}
int zhio_sem_give(ZhiosSemHandle_t h)
{
    HostSem_t *s = (HostSem_t*)h;
    if (!s) return ZHIO_E_INVAL;
    pthread_mutex_lock(&s->lock); s->count++; pthread_cond_signal(&s->cond); pthread_mutex_unlock(&s->lock);
    return ZHIO_OK;
}
int zhio_sem_give_isr(ZhiosSemHandle_t h) { return zhio_sem_give(h); }
int zhio_sem_take(ZhiosSemHandle_t h, ZhiosTick_t timeout)
{
    HostSem_t *s = (HostSem_t*)h;
    if (!s) return ZHIO_E_INVAL;
    pthread_mutex_lock(&s->lock);
    while (s->count == 0) {
        if (timeout == 0) { pthread_mutex_unlock(&s->lock); return ZHIO_E_TIMEOUT; }
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += (long)(timeout % 1000) * 1000000L + (timeout/1000) * 1000000000L;
        ts.tv_sec += (time_t)(timeout / 1000) + (ts.tv_nsec >= 1000000000L ? 1 : 0);
        ts.tv_nsec %= 1000000000L;
        if (pthread_cond_timedwait(&s->cond, &s->lock, &ts) != 0) {
            pthread_mutex_unlock(&s->lock); return ZHIO_E_TIMEOUT;
        }
    }
    s->count--; pthread_mutex_unlock(&s->lock);
    return ZHIO_OK;
}

/* ---------------- 看门狗（host 为软实现，空操作） ---------------- */
int  zhio_watchdog_start(uint32_t period_ms) { (void)period_ms; return ZHIO_OK; }
void zhio_watchdog_feed(void) {}
