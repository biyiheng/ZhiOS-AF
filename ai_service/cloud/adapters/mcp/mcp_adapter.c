/*
 * mcp_adapter.c - MCP (Model Context Protocol) 工具调用协议适配器
 *
 * 对应《19-云端协议适配器规范》。通过共享传输层发送 JSON-RPC 2.0 风格的
 * MCP tools/call 请求，用于远程工具调用。
 *
 * 请求体示例：
 *   {"jsonrpc":"2.0","method":"tools/call","params":{"name":"<tool_name>","arguments":{<args>}}}
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "ai_service.h"
#include "cloud_transport.h"

/* MCP 默认端点（占位，实际生产由上层配置注入） */
#define ZHIO_MCP_DEFAULT_ENDPOINT "https://api.mcp.example/v1"
/* 本地请求体构造缓冲区大小 */
#define ZHIO_MCP_BODY_BUF_SIZE    512u

/**
 * @brief MCP 适配器私有上下文。
 *
 * 存放端点、密钥引用与超时字段。仅保存密钥引用（api_key_ref），不保存明文密钥。
 */
typedef struct MCPAdapterCtx {
    const char *endpoint;        /**< 请求端点 URL */
    const char *api_key_ref;     /**< 密钥引用（指向安全存储，禁止明文） */
    uint32_t    timeout_ms;      /**< 默认超时（毫秒） */
    void       *config;          /**< 初始化配置（可选） */
} MCPAdapterCtx_t;

/** MCP 适配器全局单例上下文 */
static MCPAdapterCtx_t s_mcp_ctx = {
    .endpoint    = ZHIO_MCP_DEFAULT_ENDPOINT,
    .api_key_ref = NULL,
    .timeout_ms  = 30000u,
    .config      = NULL,
};

/**
 * @brief 初始化 MCP 适配器。
 *
 * @param ctx 适配器上下文（priv_data）
 * @return ZHIO_OK 成功；ZHIO_E_INVAL 参数非法
 */
static int mcp_init(void *ctx)
{
    MCPAdapterCtx_t *c = (MCPAdapterCtx_t *)ctx;
    if (c == NULL) {
        return ZHIO_E_INVAL;
    }
    if (c->endpoint == NULL) {
        return ZHIO_E_INVAL;
    }
    return ZHIO_OK;
}

/**
 * @brief 反初始化 MCP 适配器。
 *
 * @param ctx 适配器上下文（priv_data）
 * @return ZHIO_OK 成功；ZHIO_E_INVAL 参数非法
 */
static int mcp_deinit(void *ctx)
{
    if (ctx == NULL) {
        return ZHIO_E_INVAL;
    }
    return ZHIO_OK;
}

/**
 * @brief 构造 MCP tools/call 请求体（JSON-RPC 2.0）。
 *
 * @param buf        输出缓冲区
 * @param buf_size   缓冲区大小
 * @param tool_name  工具名
 * @param args       工具参数字符串（以 JSON 对象内容形式嵌入）
 * @return 期望写入的字符数；若 >= buf_size 说明被截断
 */
static int mcp_build_tool_body(char *buf, size_t buf_size,
                               const char *tool_name, const char *args)
{
    return snprintf(buf, buf_size,
        "{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":"
        "{\"name\":\"%s\",\"arguments\":{%s}}}",
        tool_name, args);
}

/**
 * @brief 执行 MCP 工具调用。
 *
 * @param ctx        适配器上下文
 * @param tool_name  工具名
 * @param args       工具参数（JSON 对象内容文本）
 * @param args_len   参数长度（字节）
 * @param result     输出结果缓冲区；允许为 NULL
 * @param result_len 结果长度指针；允许为 NULL
 * @param timeout_ms 超时（毫秒）
 * @return ZHIO_OK 成功；否则为错误码
 */
static int mcp_tool_call(void *ctx, const char *tool_name, const void *args,
                         uint32_t args_len, void *result, uint32_t *result_len,
                         uint32_t timeout_ms)
{
    MCPAdapterCtx_t *c = (MCPAdapterCtx_t *)ctx;
    CloudTransportFn  t;
    char              body[ZHIO_MCP_BODY_BUF_SIZE];
    char              argbuf[ZHIO_MCP_BODY_BUF_SIZE];
    int               n;

    if (c == NULL || tool_name == NULL || args == NULL || args_len == 0u) {
        return ZHIO_E_INVAL;
    }
    if (result != NULL && result_len == NULL) {
        return ZHIO_E_INVAL;
    }

    t = zhio_cloud_transport_get();
    if (t == NULL) {
        return ZHIO_E_NOCLOUD;
    }

    if (args_len >= sizeof(argbuf)) {
        return ZHIO_E_INVAL;
    }
    memcpy(argbuf, args, args_len);
    argbuf[args_len] = '\0';

    n = mcp_build_tool_body(body, sizeof(body), tool_name, argbuf);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return ZHIO_E_INVAL;
    }

    return t(c->endpoint, body, (uint32_t)n, (char *)result, result_len, timeout_ms);
}

/**
 * @brief 执行 MCP 对话补全。
 *
 * MCP 以工具调用为核心，对话补全在此走工具调用通道，将消息作为文本参数下发。
 * 若后续需要接入 MCP 的 resources/prompts 接口，可在此扩展。
 *
 * @param ctx        适配器上下文
 * @param messages   用户消息内容
 * @param response   输出响应缓冲区；允许为 NULL
 * @param resp_len   响应长度指针；允许为 NULL
 * @param timeout_ms 超时（毫秒）
 * @return ZHIO_OK 成功；否则为错误码
 */
static int mcp_chat_completion(void *ctx, const char *messages,
                               char *response, uint32_t *resp_len,
                               uint32_t timeout_ms)
{
    MCPAdapterCtx_t *c = (MCPAdapterCtx_t *)ctx;
    CloudTransportFn  t;
    char              body[ZHIO_MCP_BODY_BUF_SIZE];
    int               n;

    if (c == NULL || messages == NULL) {
        return ZHIO_E_INVAL;
    }
    if (response != NULL && resp_len == NULL) {
        return ZHIO_E_INVAL;
    }

    t = zhio_cloud_transport_get();
    if (t == NULL) {
        return ZHIO_E_NOCLOUD;
    }

    /* 以 "chat" 作为虚拟工具名，将消息文本作为参数内容下发 */
    n = mcp_build_tool_body(body, sizeof(body), "chat", messages);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return ZHIO_E_INVAL;
    }

    return t(c->endpoint, body, (uint32_t)n, response, resp_len, timeout_ms);
}

/**
 * @brief 执行 MCP Agent 任务。
 *
 * 将任务载荷转换为工具参数文本并经 tools/call 下发。
 *
 * @param ctx        适配器上下文
 * @param task       任务载荷
 * @param task_len   任务长度（字节）
 * @param timeout_ms 超时（毫秒）
 * @return ZHIO_OK 成功；否则为错误码
 */
static int mcp_agent_run(void *ctx, const void *task, uint32_t task_len,
                         uint32_t timeout_ms)
{
    MCPAdapterCtx_t *c = (MCPAdapterCtx_t *)ctx;
    CloudTransportFn  t;
    char              body[ZHIO_MCP_BODY_BUF_SIZE];
    char              argbuf[ZHIO_MCP_BODY_BUF_SIZE];
    int               n;

    if (c == NULL || task == NULL || task_len == 0u) {
        return ZHIO_E_INVAL;
    }
    if (task_len >= sizeof(argbuf)) {
        return ZHIO_E_INVAL;
    }

    t = zhio_cloud_transport_get();
    if (t == NULL) {
        return ZHIO_E_NOCLOUD;
    }

    memcpy(argbuf, task, task_len);
    argbuf[task_len] = '\0';

    n = mcp_build_tool_body(body, sizeof(body), "agent_run", argbuf);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return ZHIO_E_INVAL;
    }

    return t(c->endpoint, body, (uint32_t)n, NULL, NULL, timeout_ms);
}

/**
 * @brief 创建 MCP 适配器上下文。
 *
 * @return 新上下文指针；失败返回 NULL
 */
void *mcp_ctx_new(void)
{
    MCPAdapterCtx_t *c = (MCPAdapterCtx_t *)calloc(1u, sizeof(MCPAdapterCtx_t));
    if (c == NULL) {
        return NULL;
    }
    c->endpoint    = ZHIO_MCP_DEFAULT_ENDPOINT;
    c->api_key_ref = NULL;
    c->timeout_ms  = 30000u;
    return c;
}

/** MCP 适配器静态实例（协议名与回调绑定） */
static CloudAdapter_t s_mcp_adapter = {
    .protocol_name   = "mcp",
    .init            = mcp_init,
    .deinit          = mcp_deinit,
    .chat_completion = mcp_chat_completion,
    .agent_run       = mcp_agent_run,
    .tool_call       = mcp_tool_call,
    .priv_data       = &s_mcp_ctx,
};

/**
 * @brief 工厂函数：获取 MCP 适配器实例。
 *
 * @return CloudAdapterHandle_t 适配器句柄（指向 CloudAdapter_t）
 */
CloudAdapterHandle_t zhio_mcp_adapter_new(void)
{
    return (CloudAdapterHandle_t)&s_mcp_adapter;
}
