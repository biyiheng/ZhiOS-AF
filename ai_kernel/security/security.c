/*
 * security.c - 安全子模块
 *
 * 对应《20-安全性设计文档》《17-API接口规范文档》第 8 节。
 * 安全推理、推理看门狗、安全校验器、Agent 安全等级。
 */
#include <string.h>
#include <stdint.h>
#include "security.h"
#include "model_runtime.h"
#include "inference_scheduler.h"
#include "capability.h"
#include "zhios_config.h"
#include "zhios_rtos.h"

/* 看门狗状态表 */
typedef struct {
    ModelHandle_t model;
    ZhiosTick_t   period;
    ZhiosTick_t   last_feed;
    uint32_t      valid;
} WatchdogEntry_t;

static WatchdogEntry_t g_wd[ZHIO_CFG_MAX_LOCAL_MODELS];

int iSecurityInit(void) { memset(g_wd, 0, sizeof(g_wd)); return ZHIO_OK; }
void vSecurityDeinit(void) { memset(g_wd, 0, sizeof(g_wd)); }

/* 绑定安全校验器到模型 */
int xSetSafetyValidator(ModelHandle_t model, SafetyValidator_t validator)
{
    if (!model) return ZHIO_E_INVAL;
    return iModelBindValidator(model, (void *)validator);
}

/* 推理前安全校验：调用模型绑定的校验器 */
int zhio_safety_check(ModelHandle_t model, TensorHandle_t input, TensorHandle_t output)
{
    SafetyValidator_t v = (SafetyValidator_t)zhio_model_get_validator(model);
    if (v && v(model, input, output) != 0) {
        return ZHIO_E_SAFETY;
    }
    return ZHIO_OK;
}

/* 带安全校验的推理（xRunInference 内部已含安全校验，这里显式包装） */
int xSafeInference(ModelHandle_t model, TensorHandle_t input,
                   TensorHandle_t output, ZhiosTick_t timeout)
{
    if (!model || !input || !output) return ZHIO_E_INVAL;
    return xRunInference(model, input, output, timeout);
}

/* 推理看门狗 */
int xInferenceWatchdogStart(ModelHandle_t model, ZhiosTick_t period)
{
    if (!model) return ZHIO_E_INVAL;
    uint32_t i;
    for (i = 0; i < ZHIO_CFG_MAX_LOCAL_MODELS; i++) {
        if (!g_wd[i].valid) {
            g_wd[i].model = model;
            g_wd[i].period = period;
            g_wd[i].last_feed = zhio_get_tick();
            g_wd[i].valid = 1;
            return ZHIO_OK;
        }
    }
    return ZHIO_E_NOMEM;
}
void xInferenceWatchdogFeed(ModelHandle_t model)
{
    uint32_t i;
    for (i = 0; i < ZHIO_CFG_MAX_LOCAL_MODELS; i++)
        if (g_wd[i].valid && g_wd[i].model == model) g_wd[i].last_feed = zhio_get_tick();
}

/* 内部：检测看门狗是否超时（供测试/监控） */
int zhio_watchdog_expired(ModelHandle_t model)
{
    uint32_t i;
    for (i = 0; i < ZHIO_CFG_MAX_LOCAL_MODELS; i++)
        if (g_wd[i].valid && g_wd[i].model == model)
            return (zhio_get_tick() - g_wd[i].last_feed > g_wd[i].period) ? 1 : 0;
    return 0;
}
