/*
 * inference_scheduler.c - 推理任务调度器 + 内核推理执行 API
 *
 * =============================================================================
 * 模块说明（维护入口）
 * -----------------------------------------------------------------------------
 * 职责      ：在应用层提供"推理任务"的实时调度与同步/异步/批量推理执行，
 *             是 Agent 自治层与底层推理内核之间的调度网关。
 * 依赖      ：model_runtime（模型执行）、security（安全校验）、tensor_mem（张量）、
 *             zhios_rtos（FreeRTOS/host 抽象）。
 * 被谁调用  ：agent 模块、capability 模块、ai_service 模块，以及外部通过
 *             include/inference_scheduler.h 调用。
 * 算法      ：EDF（最早截止期）+ 固定优先级抢占 的混合选择
 *             （详见下方"算法与复杂度"注释）。
 * 实时性指标：对应《33-操作系统技术指标体系设计文档》"内核调度"维度。
 * =============================================================================
 */
#include <string.h>
#include <stdint.h>
#include "inference_scheduler.h"
#include "model_runtime.h"
#include "security.h"
#include "tensor_mem.h"
#include "zhios_config.h"
#include "zhios_rtos.h"

/* =============================================================================
 * 算法与复杂度（评审依据）
 * -----------------------------------------------------------------------------
 * xInferenceSchedulerGetNext() 采用单趟线性扫描选"最高优先级 + 同级最早截止期"：
 *   复杂度 O(N)，N = ZHIO_CFG_MAX_INFERENCE_TASKS（编译期小常数，典型 8~32）。
 * 决策：对 N 有上界且极小的硬实时任务集，线性扫描缓存友好、无堆维护开销，
 *       实测通常优于 O(log n) 二叉堆（N<32 时）。OS 级抢占（上下文切换）由
 *       底层 FreeRTOS 的 O(1) 就绪位图调度完成，故"调度决策"整体可满足
 *       O(1)/O(log n) 指标体系（见《33》4.1）。如需在 N 很大时退化为 O(log n)，
 *       可在此处改接最小堆，但需同步维护 deadline 的优先级队列。
 * 抖动测量：xInferenceSchedulerGetNext() 用 sched_cycles_now() 采集单次决策周期，
 *       统计最坏决策延迟（g_sched_worst_cycles），供抖动/截止期分析使用。
 * ============================================================================= */

/* 调度决策周期测量开关（0 = 关闭，省去读周期计数器的开销） */
#ifndef ZHIO_CFG_SCHED_TRACE
#define ZHIO_CFG_SCHED_TRACE 1
#endif

static InferenceTaskTCB_t g_tasks[ZHIO_CFG_MAX_INFERENCE_TASKS];
static uint32_t g_sched_inited = 0;
static uint32_t g_sched_worst_cycles = 0;   /* 单次调度决策最坏周期数（抖动分析） */
static uint32_t g_sched_decision_count = 0; /* 累计调度决策次数（审计） */
static ZhiosSemHandle_t g_async_gate = NULL; /* 异步推理并发闸门（限流防线程爆炸） */

/* 平台周期计数：Cortex-M 用 DWT->CYCCNT，其余回退到 tick（用于相对估算） */
static inline uint32_t sched_cycles_now(void)
{
#if defined(__CORTEX_M) || defined(ARMV8M_MAINLINE) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_6M__)
    extern volatile uint32_t DWT_CYCCNT;   /* 由 BSP 链接 DWT->CYCCNT */
    return DWT_CYCCNT;
#else
    return (uint32_t)zhio_get_tick();      /* 非 Cortex-M 平台仅作近似 */
#endif
}

int iInferenceSchedulerInit(void)
{
    memset(g_tasks, 0, sizeof(g_tasks));
    g_sched_inited = 1;
    g_sched_worst_cycles = 0;
    g_sched_decision_count = 0;
    /* 初始化异步推理并发闸门：容量 = ZHIO_CFG_MAX_ASYNC_INFERENCE */
    if (!g_async_gate) {
        g_async_gate = zhio_sem_create();
        if (g_async_gate) {
            uint32_t i;
            for (i = 0; i < ZHIO_CFG_MAX_ASYNC_INFERENCE; i++) zhio_sem_give(g_async_gate);
        }
    }
    return ZHIO_OK;
}
void vInferenceSchedulerDeinit(void)
{
    memset(g_tasks, 0, sizeof(g_tasks));
    g_sched_inited = 0;
    if (g_async_gate) { zhio_free(g_async_gate); g_async_gate = NULL; }
}

int xCreateInferenceTask(const char *name, uint32_t priority,
                         ZhiosTick_t deadline_ticks, ZhiosTick_t period_ticks,
                         AgentHandle_t owner)
{
    if (!g_sched_inited || !name) return ZHIO_E_INVAL;
    uint32_t i;
    for (i = 0; i < ZHIO_CFG_MAX_INFERENCE_TASKS; i++) {
        /* 空闲槽：name 为空 */
        if (g_tasks[i].name[0] == '\0') {
            memset(&g_tasks[i], 0, sizeof(g_tasks[i]));
            strncpy(g_tasks[i].name, name, sizeof(g_tasks[i].name) - 1);
            g_tasks[i].base_priority   = priority;
            g_tasks[i].boost_priority  = priority;
            g_tasks[i].deadline_ticks  = deadline_ticks;
            g_tasks[i].period_ticks    = period_ticks;
            g_tasks[i].owner_agent     = owner;
            g_tasks[i].state           = ZHIO_ITASK_CREATED;
            zhio_log("[sched] task created: id=%u name=%s prio=%u deadline=%u",
                     (unsigned)i, name, (unsigned)priority, (unsigned)deadline_ticks);
            return (int)i;
        }
    }
    zhio_log("[sched] xCreateInferenceTask FAILED: no free slot (%u)", ZHIO_CFG_MAX_INFERENCE_TASKS);
    return ZHIO_E_NOMEM;
}

int xDeleteInferenceTask(int task_id)
{
    if (task_id < 0 || task_id >= (int)ZHIO_CFG_MAX_INFERENCE_TASKS) return ZHIO_E_INVAL;
    if (g_tasks[task_id].state == ZHIO_ITASK_RUNNING) return ZHIO_E_BUSY;
    zhio_log("[sched] task deleted: id=%d", task_id);
    memset(&g_tasks[task_id], 0, sizeof(g_tasks[task_id]));
    return ZHIO_OK;
}

int xInferenceTaskActivate(int task_id)
{
    if (task_id < 0 || task_id >= (int)ZHIO_CFG_MAX_INFERENCE_TASKS) return ZHIO_E_INVAL;
    if (g_tasks[task_id].name[0] == '\0') return ZHIO_E_NOTFOUND;
    if (g_tasks[task_id].state == ZHIO_ITASK_CREATED || g_tasks[task_id].state == ZHIO_ITASK_COMPLETED)
        g_tasks[task_id].state = ZHIO_ITASK_READY;
    zhio_log("[sched] task activated: id=%d -> READY", task_id);
    return ZHIO_OK;
}

int xInferenceTaskComplete(int task_id)
{
    if (task_id < 0 || task_id >= (int)ZHIO_CFG_MAX_INFERENCE_TASKS) return ZHIO_E_INVAL;
    g_tasks[task_id].state = ZHIO_ITASK_COMPLETED;
    return ZHIO_OK;
}

/* EDF + 优先级抢占混合选择：优先级高者优先，同级取最早截止期 */
int xInferenceSchedulerGetNext(int *next_id)
{
    if (!next_id || !g_sched_inited) return ZHIO_E_INVAL;
    uint32_t t0 = 0;
#if ZHIO_CFG_SCHED_TRACE
    t0 = sched_cycles_now();
#endif
    int best = -1;
    uint32_t best_prio = 0;
    ZhiosTick_t best_deadline = 0xFFFFFFFFu;
    uint32_t i;
    for (i = 0; i < ZHIO_CFG_MAX_INFERENCE_TASKS; i++) {
        InferenceTaskTCB_t *t = &g_tasks[i];
        if (t->name[0] == '\0' || t->state != ZHIO_ITASK_READY) continue;
        uint32_t prio = (t->boost_priority > t->base_priority) ? t->boost_priority : t->base_priority;
        if (prio > best_prio) {
            best = (int)i; best_prio = prio; best_deadline = t->deadline_ticks;
        } else if (prio == best_prio) {
            /* EDF 决胜 */
            if (t->deadline_ticks < best_deadline) {
                best = (int)i; best_deadline = t->deadline_ticks;
            }
        }
    }
#if ZHIO_CFG_SCHED_TRACE
    {
        uint32_t dt = sched_cycles_now() - t0;
        if (dt > g_sched_worst_cycles) {
            g_sched_worst_cycles = dt;
            zhio_log("[sched] new worst decision=%u cycles (jitter analysis)", (unsigned)dt);
        }
    }
#endif
    g_sched_decision_count++;
    *next_id = best;
    if (best >= 0) {
        zhio_log("[sched] next task selected: id=%d prio=%u deadline=%u",
                 best, (unsigned)best_prio, (unsigned)best_deadline);
        return ZHIO_OK;
    }
    return ZHIO_E_NOTFOUND;
}

int xInferenceTaskSetDeadline(int task_id, ZhiosTick_t d)
{
    if (task_id < 0 || task_id >= (int)ZHIO_CFG_MAX_INFERENCE_TASKS) return ZHIO_E_INVAL;
    g_tasks[task_id].deadline_ticks = d;
    return ZHIO_OK;
}
int xInferenceTaskSetBoost(int task_id, uint32_t boost)
{
    if (task_id < 0 || task_id >= (int)ZHIO_CFG_MAX_INFERENCE_TASKS) return ZHIO_E_INVAL;
    g_tasks[task_id].boost_priority = boost;
    return ZHIO_OK;
}
int xInferenceTaskGetTCB(int task_id, InferenceTaskTCB_t *out)
{
    if (task_id < 0 || task_id >= (int)ZHIO_CFG_MAX_INFERENCE_TASKS || !out) return ZHIO_E_INVAL;
    *out = g_tasks[task_id];
    return ZHIO_OK;
}
int xInferenceSchedulerStats(int task_id, uint32_t *ran, uint32_t *miss)
{
    if (task_id < 0 || task_id >= (int)ZHIO_CFG_MAX_INFERENCE_TASKS) return ZHIO_E_INVAL;
    if (ran) *ran = g_tasks[task_id].ran_count;
    if (miss) *miss = g_tasks[task_id].deadline_miss_count;
    return ZHIO_OK;
}

/* 调度器审计：汇总最坏决策周期/累计决策次数/并发上限，供 AI 辅助决策与性能分析 */
int xInferenceSchedulerAudit(uint32_t *worst_cycles, uint32_t *decision_count,
                             uint32_t *async_capacity)
{
    if (!g_sched_inited) return ZHIO_E_INVAL;
    if (worst_cycles)    *worst_cycles    = g_sched_worst_cycles;
    if (decision_count)  *decision_count  = g_sched_decision_count;
    if (async_capacity)  *async_capacity  = ZHIO_CFG_MAX_ASYNC_INFERENCE;
    return ZHIO_OK;
}

/* ================= 取消机制（简化：取消标志列表） ================= */
#define ZHIO_MAX_CANCEL 8
static struct { ModelHandle_t model; uint32_t valid; } g_cancel[ZHIO_MAX_CANCEL];

static int is_cancelled(ModelHandle_t m)
{
    uint32_t i;
    for (i = 0; i < ZHIO_MAX_CANCEL; i++)
        if (g_cancel[i].valid && g_cancel[i].model == m) return 1;
    return 0;
}
static void cancel_mark(ModelHandle_t m)
{
    uint32_t i;
    for (i = 0; i < ZHIO_MAX_CANCEL; i++)
        if (!g_cancel[i].valid) { g_cancel[i].valid = 1; g_cancel[i].model = m; return; }
}
static void cancel_clear(ModelHandle_t m)
{
    uint32_t i;
    for (i = 0; i < ZHIO_MAX_CANCEL; i++)
        if (g_cancel[i].valid && g_cancel[i].model == m) g_cancel[i].valid = 0;
}

int xCancelInference(ModelHandle_t model)
{
    if (!model) return ZHIO_E_INVAL;
    cancel_mark(model);
    zhio_log("[sched] inference cancel requested for model=%p", (void *)model);
    return ZHIO_OK;
}

/* ---------------- 同步推理 ---------------- */
int xRunInference(ModelHandle_t model, TensorHandle_t input, TensorHandle_t output, ZhiosTick_t timeout)
{
    if (!model || !input || !output) return ZHIO_E_INVAL;
    if (is_cancelled(model)) { cancel_clear(model); return ZHIO_E_CANCELED; }

    /* 安全校验 */
    int rc = zhio_safety_check(model, input, output);
    if (rc != ZHIO_OK) {
        zhio_log("[sched] xRunInference BLOCKED by safety validator (rc=%d)", rc);
        return rc;   /* ZHIO_E_SAFETY */
    }

    ZhiosTick_t start = zhio_get_tick();
    rc = xModelExecute(model, input, output);
    ZhiosTick_t elapsed = zhio_get_tick() - start;

    if (rc != ZHIO_OK) {
        zhio_log("[sched] xRunInference exec FAILED (rc=%d, elapsed=%u)", rc, (unsigned)elapsed);
        return rc;
    }
    if (timeout != ZHIO_MAX_DELAY && elapsed > timeout) {
        zhio_log("[sched] xRunInference TIMEOUT: elapsed=%u > timeout=%u",
                 (unsigned)elapsed, (unsigned)timeout);
        return ZHIO_E_TIMEOUT;
    }
    return ZHIO_OK;
}

/* ---------------- 异步推理 ---------------- */
typedef struct {
    ModelHandle_t     model;
    TensorHandle_t    input, output;
    InferenceCallback_t cb;
    ZhiosTick_t       timeout;
} AsyncInferCtx_t;

static void async_infer_task(void *p)
{
    AsyncInferCtx_t *ctx = (AsyncInferCtx_t *)p;
    int rc = xRunInference(ctx->model, ctx->input, ctx->output, ctx->timeout);
    if (ctx->cb) ctx->cb(ctx->model, ctx->output, rc);
    /* 释放并发闸门，允许下一个异步推理进入 */
    if (g_async_gate) zhio_sem_give(g_async_gate);
    zhio_free(ctx);
}

int xRunInferenceAsync(ModelHandle_t model, TensorHandle_t input, TensorHandle_t output,
                       InferenceCallback_t cb, ZhiosTick_t timeout)
{
    if (!model || !input || !output) return ZHIO_E_INVAL;

    /* 并发闸门限流：超过 ZHIO_CFG_MAX_ASYNC_INFERENCE 个在途异步推理则拒绝，
     * 避免低配置环境下线程/内存被并发推理耗尽而卡死或 OOM。 */
    if (g_async_gate && zhio_sem_take(g_async_gate, 1) != ZHIO_OK) {
        zhio_log("[sched] xRunInferenceAsync BUSY: async concurrency limit (%u) reached",
                 ZHIO_CFG_MAX_ASYNC_INFERENCE);
        return ZHIO_E_BUSY;
    }

    AsyncInferCtx_t *ctx = (AsyncInferCtx_t *)zhio_malloc(sizeof(*ctx));
    if (!ctx) {
        if (g_async_gate) zhio_sem_give(g_async_gate);
        return ZHIO_E_NOMEM;
    }
    ctx->model = model; ctx->input = input; ctx->output = output;
    ctx->cb = cb; ctx->timeout = timeout;
    ZhiosTaskHandle_t h;
    ZhiosTaskParams_t tp;
    memset(&tp, 0, sizeof(tp));
    tp.name = "ainf"; tp.stack_size = 2048; tp.priority = 5;
    tp.fn = async_infer_task; tp.param = ctx;
    if (zhio_task_create(&h, &tp) != ZHIO_OK) {
        zhio_log("[sched] xRunInferenceAsync FAILED: cannot create task");
        if (g_async_gate) zhio_sem_give(g_async_gate);
        zhio_free(ctx);
        return ZHIO_E_NOMEM;
    }
    return ZHIO_OK;
}

/* ---------------- 批量推理 ---------------- */
int xRunBatchInference(ModelHandle_t model, TensorHandle_t inputs[], TensorHandle_t outputs[],
                       uint32_t batch_size, ZhiosTick_t timeout)
{
    if (!model || batch_size == 0 || batch_size > 32) return ZHIO_E_INVAL;
    if (!inputs || !outputs) return ZHIO_E_INVAL;
    uint32_t i;
    for (i = 0; i < batch_size; i++) {
        int rc = xRunInference(model, inputs[i], outputs[i], timeout);
        if (rc != ZHIO_OK) return rc;
    }
    return ZHIO_OK;
}

/* ---------------- 绝对截止时刻推理 ---------------- */
int xRunInferenceTimeout(ModelHandle_t model, TensorHandle_t input, TensorHandle_t output, ZhiosTick_t deadline)
{
    ZhiosTick_t now = zhio_get_tick();
    ZhiosTick_t remain;
    if (deadline <= now) return ZHIO_E_TIMEOUT;
    remain = deadline - now;
    return xRunInference(model, input, output, remain);
}
