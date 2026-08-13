/*
 * capability.c - 能力偏好引擎
 *
 * 对应《11-能力偏好引擎与关键词配置设计文档》。
 * 关键词 → 能力矩阵映射 → CapabilityPreference_t，支持热加载。
 */
#include <string.h>
#include <stdint.h>
#include "capability.h"
#include "agent.h"
#include "zhios_config.h"

static CapabilityPreference_t g_defaults = {
    .safety_level               = ZHIO_SAFETY_MEDIUM,
    .vision_weight              = 0.5f,
    .motion_weight              = 0.5f,
    .safety_weight              = 0.5f,
    .force_weight               = 0.5f,
    .quality_weight             = 0.5f,
    .routing_mode               = ZHIO_ROUTE_LOCAL_FIRST,
    .local_confidence_threshold = 0.7f,
    .cloud_allowed              = 1,
    .cloud_data_privacy         = 0,
    .latency_priority           = 0,
    .accuracy_priority          = 0,
    .force_limit_scale          = 1.0f,
};

int iCapabilityInit(void) { return ZHIO_OK; }
void vCapabilityDeinit(void) {}

const CapabilityPreference_t *xCapabilityDefaults(void) { return &g_defaults; }

/* ---- 关键词 → 能力矩阵映射 ---- */
static void apply_keyword(CapabilityPreference_t *p, const char *kw)
{
    if (strcmp(kw, "谨慎模式") == 0)       { p->safety_level = ZHIO_SAFETY_HIGH; }
    else if (strcmp(kw, "最高安全") == 0)  { p->safety_level = ZHIO_SAFETY_CRITICAL; p->cloud_allowed = 0; }
    else if (strcmp(kw, "视觉优先") == 0)  { p->vision_weight = 0.8f; p->local_confidence_threshold = 0.6f; }
    else if (strcmp(kw, "视觉增强") == 0)  { p->vision_weight = 0.9f; }
    else if (strcmp(kw, "运动优先") == 0)  { p->motion_weight = 0.8f; }
    else if (strcmp(kw, "安全优先") == 0)  { p->safety_weight = 0.8f; p->safety_level = ZHIO_SAFETY_HIGH; }
    else if (strcmp(kw, "力控保守") == 0)  { p->force_weight = 0.8f; p->force_limit_scale = 0.5f; }
    else if (strcmp(kw, "质检优先") == 0)  { p->quality_weight = 0.8f; p->accuracy_priority = 1; }
    else if (strcmp(kw, "允许云端") == 0)  { p->cloud_allowed = 1; p->cloud_data_privacy = 0; }
    else if (strcmp(kw, "允许云端辅助") == 0){ p->cloud_allowed = 1; }
    else if (strcmp(kw, "云端脱敏") == 0)  { p->cloud_allowed = 1; p->cloud_data_privacy = 1; }
    else if (strcmp(kw, "仅本地") == 0)    { p->cloud_allowed = 0; p->routing_mode = ZHIO_ROUTE_LOCAL_ONLY; }
    else if (strcmp(kw, "本地优先") == 0)  { p->routing_mode = ZHIO_ROUTE_LOCAL_FIRST; p->local_confidence_threshold = 0.7f; }
    else if (strcmp(kw, "云端优先") == 0)  { p->routing_mode = ZHIO_ROUTE_CLOUD_FIRST; p->cloud_allowed = 1; }
    else if (strcmp(kw, "仅云端") == 0)    { p->routing_mode = ZHIO_ROUTE_CLOUD_ONLY; p->cloud_allowed = 1; }
    else if (strcmp(kw, "自动") == 0)      { p->routing_mode = ZHIO_ROUTE_AUTO; }
    else if (strcmp(kw, "延迟优先") == 0)  { p->latency_priority = 1; }
    else if (strcmp(kw, "精度优先") == 0)  { p->accuracy_priority = 1; }
    /* 未知关键词忽略 */
}

int iConfigureAutoAgentByKeywords(AgentHandle_t agent, const char **keywords,
                                  uint32_t keyword_count, uint32_t flags)
{
    (void)flags;
    if (!agent) return ZHIO_E_INVAL;
    if (!keywords || keyword_count == 0) return ZHIO_E_INVAL;

    /* 从默认偏好开始，叠加关键词映射 */
    CapabilityPreference_t pref = g_defaults;
    uint32_t i;
    for (i = 0; i < keyword_count; i++) {
        if (keywords[i]) apply_keyword(&pref, keywords[i]);
    }
    return iAgentSetCapability(agent, &pref);
}

/* 直接设置偏好（由 agent 层实现） */
int iSetAgentSafetyLevel(AgentHandle_t agent, uint32_t level)
{
    if (!agent || level > ZHIO_SAFETY_CRITICAL) return ZHIO_E_INVAL;
    CapabilityPreference_t pref;
    int rc = iAgentGetCapability(agent, &pref);
    if (rc != ZHIO_OK) return rc;
    pref.safety_level = (SafetyLevel_t)level;
    /* 高安全等级默认禁用云端、提高本地阈值 */
    if (pref.safety_level >= ZHIO_SAFETY_HIGH) {
        if (pref.safety_level == ZHIO_SAFETY_CRITICAL) {
            pref.cloud_allowed = 0;
            pref.routing_mode = ZHIO_ROUTE_LOCAL_ONLY;
        }
        pref.local_confidence_threshold = 0.85f;
    }
    return iAgentSetCapability(agent, &pref);
}

const char *xCapabilityRoutingName(RoutingMode_t mode)
{
    switch (mode) {
        case ZHIO_ROUTE_LOCAL_ONLY:  return "LOCAL_ONLY";
        case ZHIO_ROUTE_LOCAL_FIRST: return "LOCAL_FIRST";
        case ZHIO_ROUTE_CLOUD_FIRST: return "CLOUD_FIRST";
        case ZHIO_ROUTE_CLOUD_ONLY:  return "CLOUD_ONLY";
        case ZHIO_ROUTE_AUTO:        return "AUTO";
        default:                     return "UNKNOWN";
    }
}

int iCapabilityRoutingFromName(const char *name, RoutingMode_t *out)
{
    if (!name || !out) return ZHIO_E_INVAL;
    if (strcmp(name, "LOCAL_ONLY") == 0)  { *out = ZHIO_ROUTE_LOCAL_ONLY; return ZHIO_OK; }
    if (strcmp(name, "LOCAL_FIRST") == 0) { *out = ZHIO_ROUTE_LOCAL_FIRST; return ZHIO_OK; }
    if (strcmp(name, "CLOUD_FIRST") == 0) { *out = ZHIO_ROUTE_CLOUD_FIRST; return ZHIO_OK; }
    if (strcmp(name, "CLOUD_ONLY") == 0)  { *out = ZHIO_ROUTE_CLOUD_ONLY; return ZHIO_OK; }
    if (strcmp(name, "AUTO") == 0)        { *out = ZHIO_ROUTE_AUTO; return ZHIO_OK; }
    return ZHIO_E_INVAL;
}
