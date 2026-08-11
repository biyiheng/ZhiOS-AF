/*
 * ai_service.h - 混合 AI 服务接口层
 *
 * 对应《10-混合AI服务接口层设计文档》《17-API接口规范文档》第 7 节。
 * 本地推理 + 云端路由（OpenAI/Claude/A2A/MCP 协议适配器）。
 */
#ifndef ZHIO_AI_SERVICE_H
#define ZHIO_AI_SERVICE_H

#include <stdint.h>
#include "zhios_types.h"
#include "zhios_err.h"
#include "zhios_config.h"
#include "zhios_rtos.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 云端适配器（CloudAdapter_t）统一接口 */
typedef int (*CloudInitFn)(void *ctx);
typedef int (*CloudChatFn)(void *ctx, const char *messages, char *response,
                           uint32_t *resp_len, uint32_t timeout_ms);
typedef int (*CloudAgentRunFn)(void *ctx, const void *task, uint32_t task_len,
                               uint32_t timeout_ms);
typedef int (*CloudToolCallFn)(void *ctx, const char *tool_name, const void *args,
                               uint32_t args_len, void *result, uint32_t *result_len,
                               uint32_t timeout_ms);

typedef struct CloudAdapter {
    const char      *protocol_name;   /* "openai"/"claude"/"a2a"/"mcp"/"openai-compatible"/"rest" */
    CloudInitFn      init;
    CloudInitFn      deinit;
    CloudChatFn      chat_completion;
    CloudAgentRunFn  agent_run;
    CloudToolCallFn  tool_call;
    void            *priv_data;       /* 端点/凭据引用/传输等 */
} CloudAdapter_t;

/* 路由标志（route_flags 低 8 位为 RoutingMode_t） */
#define ZHIO_ROUTE_FLAG_MODE_MASK  0x00FFu

int  iAIServiceInit(void);
void vAIServiceDeinit(void);

/* 注册/初始化适配器 */
int  xRegisterCloudAdapter(CloudAdapterHandle_t adapter, const char *protocol);
int  iCloudAdapterInit(CloudAdapterHandle_t adapter, const void *config);
int  iCloudAdapterDeinit(CloudAdapterHandle_t adapter);

/* 路由推理（本地/云端，目标切换 <200ms） */
int  xRouteInference(ModelHandle_t       local_model,
                     CloudAdapterHandle_t cloud,
                     TensorHandle_t       input,
                     TensorHandle_t       output,
                     uint32_t             route_flags,
                     ZhiosTick_t          timeout);

/* 云端操作 */
int  iCloudChatCompletion(CloudAdapterHandle_t adapter, const char *messages,
                          char *response, uint32_t *resp_len, ZhiosTick_t timeout);
int  iCloudAgentRun(CloudAdapterHandle_t adapter, AgentHandle_t agent,
                    const void *task, uint32_t task_len, ZhiosTick_t timeout);
int  iCloudToolCall(CloudAdapterHandle_t adapter, const char *tool_name,
                    const void *args, uint32_t args_len,
                    void *result, uint32_t *result_len, ZhiosTick_t timeout);

/* 云端连通性 */
int  iCloudIsReachable(CloudAdapterHandle_t adapter);

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_AI_SERVICE_H */
