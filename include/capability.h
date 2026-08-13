/*
 * capability.h - 能力偏好引擎
 *
 * 对应《11-能力偏好引擎与关键词配置设计文档》。
 * 关键词 → 能力矩阵映射 → CapabilityPreference_t；支持热加载。
 */
#ifndef ZHIO_CAPABILITY_H
#define ZHIO_CAPABILITY_H

#include <stdint.h>
#include "zhios_types.h"
#include "zhios_err.h"
#include "zhios_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 能力偏好结构（关键词解析后的产物） */
typedef struct {
    SafetyLevel_t safety_level;              /* 谨慎模式→HIGH/CRITICAL */
    float         vision_weight;             /* 视觉优先→0.8 */
    float         motion_weight;
    float         safety_weight;
    float         force_weight;
    float         quality_weight;
    RoutingMode_t routing_mode;              /* 本地/云端路由 */
    float         local_confidence_threshold;/* 本地置信度阈值（0-1），>阈值不上云 */
    int           cloud_allowed;             /* 是否允许云端辅助 */
    int           cloud_data_privacy;        /* 1=云端数据脱敏 */
    int           latency_priority;          /* 延迟优先 */
    int           accuracy_priority;         /* 精度优先 */
    float         force_limit_scale;         /* 力控保守→限幅比例 0-1 */
} CapabilityPreference_t;

/* 关键词条目：词 → 映射规则 */
typedef struct {
    const char *keyword;
    int         apply;      /* 未用，保留 */
} CapabilityKeyword_t;

int  iCapabilityInit(void);
void vCapabilityDeinit(void);

/* 关键词解析并应用到 Agent（核心入口） */
int  iConfigureAutoAgentByKeywords(AgentHandle_t agent, const char **keywords,
                                   uint32_t keyword_count, uint32_t flags);

/* 直接设置偏好 */
int  iAgentSetCapability(AgentHandle_t agent, const CapabilityPreference_t *pref);
int  iAgentGetCapability(AgentHandle_t agent, CapabilityPreference_t *pref);

/* 安全等级便捷设置（forward 到 agent） */
int  iSetAgentSafetyLevel(AgentHandle_t agent, uint32_t level);

/* 默认偏好 */
const CapabilityPreference_t *xCapabilityDefaults(void);

/* 路由模式字符串 ↔ 枚举 */
const char *xCapabilityRoutingName(RoutingMode_t mode);
int         iCapabilityRoutingFromName(const char *name, RoutingMode_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_CAPABILITY_H */
