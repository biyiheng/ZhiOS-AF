/*
 * inference_scheduler.h - 推理任务调度器
 *
 * 对应《05-实时内核与调度设计文档》《17-API接口规范文档》。
 * EDF（最早截止期优先）+ 固定优先级抢占的混合调度。
 * InferenceTaskTCB_t 为 FreeRTOS TCB 的扩展，用于推理任务。
 */
#ifndef ZHIO_INFERENCE_SCHEDULER_H
#define ZHIO_INFERENCE_SCHEDULER_H

#include <stdint.h>
#include "zhios_types.h"
#include "zhios_err.h"
#include "zhios_rtos.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 推理任务控制块（FreeRTOS TCB 扩展） */
typedef struct {
    char               name[16];
    uint32_t           base_priority;    /* 基础优先级 */
    uint32_t           boost_priority;   /* 提升后的优先级（>= base） */
    ZhiosTick_t        deadline_ticks;   /* 相对截止期 */
    ZhiosTick_t        period_ticks;     /* 周期 */
    ModelHandle_t      loaded_model;     /* 绑定的模型 */
    InferenceTaskState_t state;
    void              *inference_context; /* 后端上下文 */
    AgentHandle_t      owner_agent;      /* 所属 Agent */
    int                is_agent_task;    /* 是否 Agent 决策任务 */
    ZhiosTick_t        agent_deadline_ms;/* Agent 决策截止（ms） */

    /* 统计 */
    uint32_t           ran_count;
    uint32_t           deadline_miss_count;
    uint32_t           exec_ticks;
} InferenceTaskTCB_t;

/* 初始化/反初始化 */
int  iInferenceSchedulerInit(void);
void vInferenceSchedulerDeinit(void);

/* 创建/删除推理任务，返回 task_id（>=0）或负错误码 */
int  xCreateInferenceTask(const char *name,
                          uint32_t    priority,
                          ZhiosTick_t deadline_ticks,
                          ZhiosTick_t period_ticks,
                          AgentHandle_t owner);
int  xDeleteInferenceTask(int task_id);

/* 调度控制：激活（就绪）、完成；返回当前最高优先/最早截止期就绪任务 id */
int  xInferenceTaskActivate(int task_id);
int  xInferenceTaskComplete(int task_id);
int  xInferenceSchedulerGetNext(int *next_id);

/* 查询/设置 */
int  xInferenceTaskSetDeadline(int task_id, ZhiosTick_t deadline_ticks);
int  xInferenceTaskSetBoost(int task_id, uint32_t boost_priority);
int  xInferenceTaskGetTCB(int task_id, InferenceTaskTCB_t *out);

/* 统计：抖动用 exec_ticks 与 ran_count 计算 */
int  xInferenceSchedulerStats(int task_id, uint32_t *ran, uint32_t *miss);

/* 调度器审计（供 AI 辅助决策与性能分析）：最坏决策周期/累计决策次数/异步并发上限 */
int  xInferenceSchedulerAudit(uint32_t *worst_cycles, uint32_t *decision_count,
                              uint32_t *async_capacity);

/* ================= 内核推理执行 API（对应文档 17 第 4 节） ================= */
typedef void (*InferenceCallback_t)(ModelHandle_t model, TensorHandle_t output, int result);

/* 同步推理 */
int xRunInference(ModelHandle_t  model,
                  TensorHandle_t input,
                  TensorHandle_t output,
                  ZhiosTick_t    timeout);
/* 异步推理 */
int xRunInferenceAsync(ModelHandle_t       model,
                       TensorHandle_t      input,
                       TensorHandle_t      output,
                       InferenceCallback_t  cb,
                       ZhiosTick_t          timeout);
/* 批量推理 */
int xRunBatchInference(ModelHandle_t  model,
                       TensorHandle_t inputs[],
                       TensorHandle_t outputs[],
                       uint32_t       batch_size,
                       ZhiosTick_t    timeout);
/* 绝对截止时刻推理 */
int xRunInferenceTimeout(ModelHandle_t  model,
                         TensorHandle_t input,
                         TensorHandle_t output,
                         ZhiosTick_t    deadline);
/* 取消推理 */
int xCancelInference(ModelHandle_t model);

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_INFERENCE_SCHEDULER_H */
