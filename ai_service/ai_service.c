/*
 * ai_service.c - 混合 AI 服务接口层（适配器注册 + 路由）
 *
 * 对应《10-混合AI服务接口层设计文档》《17-API接口规范文档》第 7 节。
 * 本地推理 + 云端路由（OpenAI/Claude/A2A/MCP 等协议适配器）。
 */
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "ai_service.h"
#include "agent.h"
#include "capability.h"
#include "tensor_mem.h"
#include "inference_scheduler.h"
#include "zhios_config.h"
#include "zhios_rtos.h"

static CloudAdapter_t *g_adapters[ZHIO_CFG_MAX_CLOUD_ADAPTERS];
static int g_adapter_count = 0;

int iAIServiceInit(void) { g_adapter_count = 0; memset(g_adapters, 0, sizeof(g_adapters)); return ZHIO_OK; }
void vAIServiceDeinit(void) { g_adapter_count = 0; memset(g_adapters, 0, sizeof(g_adapters)); }

static int protocol_valid(const char *proto)
{
    if (!proto) return 0;
    return (strcmp(proto, "openai") == 0 || strcmp(proto, "claude") == 0 ||
            strcmp(proto, "a2a") == 0 || strcmp(proto, "mcp") == 0 ||
            strcmp(proto, "openai-compatible") == 0 || strcmp(proto, "rest") == 0);
}

int xRegisterCloudAdapter(CloudAdapterHandle_t adapter, const char *protocol)
{
    CloudAdapter_t *a = (CloudAdapter_t *)adapter;
    if (!a || !protocol_valid(protocol)) return ZHIO_E_INVAL;
    if (g_adapter_count >= ZHIO_CFG_MAX_CLOUD_ADAPTERS) return ZHIO_E_NOMEM;
    uint32_t i;
    for (i = 0; i < (uint32_t)g_adapter_count; i++)
        if (g_adapters[i] == a) return ZHIO_OK;   /* 已注册 */
    a->protocol_name = protocol;
    g_adapters[g_adapter_count++] = a;
    return ZHIO_OK;
}

int iCloudAdapterInit(CloudAdapterHandle_t adapter, const void *config)
{
    CloudAdapter_t *a = (CloudAdapter_t *)adapter;
    if (!a || !a->init) return ZHIO_E_INVAL;
    if (config) a->priv_data = (void *)config;
    return a->init(a->priv_data);
}

int iCloudAdapterDeinit(CloudAdapterHandle_t adapter)
{
    CloudAdapter_t *a = (CloudAdapter_t *)adapter;
    if (!a) return ZHIO_E_INVAL;
    if (a->deinit) return a->deinit(a->priv_data);
    return ZHIO_OK;
}

int iCloudIsReachable(CloudAdapterHandle_t adapter)
{
    CloudAdapter_t *a = (CloudAdapter_t *)adapter;
    /* 适配器可通过 priv_data 标记可达性；未实现传输则默认不可达 */
    if (!a) return 0;
    return a->chat_completion ? 1 : 0;
}

/* 从本地推理输出估计置信度（host 仿真：输出[0]/255） */
static float local_confidence(TensorHandle_t out)
{
    const uint8_t *d = (const uint8_t *)vTensorGetData(out);
    uint32_t sz = ulTensorGetSize(out);
    if (!d || sz == 0) return 0.0f;
    return (float)d[0] / 255.0f;
}

int xRouteInference(ModelHandle_t local_model, CloudAdapterHandle_t cloud,
                    TensorHandle_t input, TensorHandle_t output,
                    uint32_t route_flags, ZhiosTick_t timeout)
{
    if (!local_model || !input || !output) return ZHIO_E_INVAL;
    RoutingMode_t mode = (RoutingMode_t)(route_flags & ZHIO_ROUTE_FLAG_MODE_MASK);
    CloudAdapter_t *a = (CloudAdapter_t *)cloud;

    /* 本地推理 */
    ZhiosTick_t t0 = zhio_get_tick();
    int rc = xRunInference(local_model, input, output, timeout);
    ZhiosTick_t local_ms = zhio_get_tick() - t0;
    if (rc != ZHIO_OK) return rc;
    float conf = local_confidence(output);

    switch (mode) {
        case ZHIO_ROUTE_LOCAL_ONLY:
            return ZHIO_OK;                        /* 仅本地 */

        case ZHIO_ROUTE_LOCAL_FIRST:
            if (conf >= 0.7f) return ZHIO_OK;      /* 本地置信度足够，不上云 */
            /* 否则降级云端 */
            break;

        case ZHIO_ROUTE_CLOUD_FIRST:
        case ZHIO_ROUTE_CLOUD_ONLY:
            break;

        case ZHIO_ROUTE_AUTO:
            if (!a || !a->chat_completion) return ZHIO_OK;   /* 无云端则本地兜底 */
            if (conf >= 0.85f && local_ms <= 60u) return ZHIO_OK;
            break;

        default:
            return ZHIO_E_INVAL;
    }

    /* 需要云端 */
    if (!a || !a->chat_completion) return ZHIO_E_NOCLOUD;
    if (!iCloudIsReachable(cloud)) return ZHIO_E_NOCLOUD;

    /* 云端调用：将输入摘要发送给云端模型，结果写入输出 */
    const uint8_t *idata = (const uint8_t *)vTensorGetData(input);
    uint32_t isize = ulTensorGetSize(input);
    char prompt[160];
    uint32_t sum = 0, i;
    for (i = 0; i < isize && i < 1024; i++) sum += idata[i];
    int n = snprintf(prompt, sizeof(prompt),
                     "{\"task\":\"infer\",\"sum\":%u,\"mode\":\"%s\"}",
                     (unsigned)sum, xCapabilityRoutingName(mode));
    if (n < 0 || (uint32_t)n >= sizeof(prompt)) return ZHIO_E_INVAL;

    char resp[64];
    uint32_t rlen = sizeof(resp);
    uint32_t ctimeout_ms = (timeout == ZHIO_MAX_DELAY) ? ZHIO_CFG_SWITCH_CLOUD_MS : timeout;
    int crc = a->chat_completion(a->priv_data, prompt, resp, &rlen, ctimeout_ms);
    if (crc != ZHIO_OK) return (crc == ZHIO_E_NOCLOUD) ? ZHIO_E_NOCLOUD : ZHIO_E_TIMEOUT;

    /* 将云端结果写回输出张量 */
    uint8_t *odata = (uint8_t *)vTensorGetData(output);
    uint32_t osize = ulTensorGetSize(output);
    if (odata && osize > 0) odata[0] = (uint8_t)(rlen & 0xFF);
    return ZHIO_OK;
}

int iCloudChatCompletion(CloudAdapterHandle_t adapter, const char *messages,
                         char *response, uint32_t *resp_len, ZhiosTick_t timeout)
{
    CloudAdapter_t *a = (CloudAdapter_t *)adapter;
    if (!a || !a->chat_completion) return ZHIO_E_INVAL;
    return a->chat_completion(a->priv_data, messages, response, resp_len, timeout);
}

int iCloudAgentRun(CloudAdapterHandle_t adapter, AgentHandle_t agent,
                   const void *task, uint32_t task_len, ZhiosTick_t timeout)
{
    CloudAdapter_t *a = (CloudAdapter_t *)adapter;
    if (!a || !a->agent_run) return ZHIO_E_INVAL;
    return a->agent_run(a->priv_data, task, task_len, timeout);
}

int iCloudToolCall(CloudAdapterHandle_t adapter, const char *tool_name, const void *args,
                   uint32_t args_len, void *result, uint32_t *result_len, ZhiosTick_t timeout)
{
    CloudAdapter_t *a = (CloudAdapter_t *)adapter;
    if (!a || !a->tool_call) return ZHIO_E_INVAL;
    return a->tool_call(a->priv_data, tool_name, args, args_len, result, result_len, timeout);
}
