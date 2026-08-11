/*
 * security.h - 安全子模块
 *
 * 对应《20-安全性设计文档》《17-API接口规范文档》第 8 节。
 * 安全推理、推理看门狗、安全校验器、Agent 安全等级。
 */
#ifndef ZHIO_SECURITY_H
#define ZHIO_SECURITY_H

#include <stdint.h>
#include "zhios_types.h"
#include "zhios_err.h"
#include "zhios_rtos.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 安全校验器回调：返回 0 表示通过，非 0 拒绝 */
typedef int (*SafetyValidator_t)(ModelHandle_t model,
                                 TensorHandle_t input,
                                 TensorHandle_t output);

int  iSecurityInit(void);
void vSecurityDeinit(void);

/* 绑定安全校验器到模型 */
int  xSetSafetyValidator(ModelHandle_t model, SafetyValidator_t validator);

/* 带安全校验的推理（未通过返回 ZHIO_E_SAFETY） */
int  xSafeInference(ModelHandle_t model, TensorHandle_t input,
                    TensorHandle_t output, ZhiosTick_t timeout);

/* 推理看门狗 */
int  xInferenceWatchdogStart(ModelHandle_t model, ZhiosTick_t period);
void xInferenceWatchdogFeed(ModelHandle_t model);

/* 内部：推理前安全校验（供 xRunInference 调用） */
int  zhio_safety_check(ModelHandle_t model, TensorHandle_t input, TensorHandle_t output);

/* 设置 Agent 安全等级（0..ZHIO_SAFETY_CRITICAL），forward 到 capability */
int  iSetAgentSafetyLevel(AgentHandle_t agent, uint32_t level);

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_SECURITY_H */
