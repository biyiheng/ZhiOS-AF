/*
 * a2a_adapter.c - A2A (Agent2Agent) Task 协议适配器
 *
 * 对应《19-云端协议适配器规范》。通过共享传输层发送 JSON-RPC 2.0 风格的
 * A2A Task 请求，用于 Agent 任务下发。
 *
 * 请求体示例：
 *   {"jsonrpc":"2.0","method":"tasks/send",
 *    "params":{"task":{"message":{"role":"user","parts":[{"text":"<messages>"}]}}}}
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "ai_service.h"
#include "cloud_transport.h"

/* A2A 默认端点（占位，实际生产由上层配置注入） */
#define ZHIO_A2A_DEFAULT_ENDPOINT "https://api.a2a.example/v1"
/* 本地请求体构造缓冲区大小 */
#define ZHIO_A2A_BODY_BUF_SIZE    512u

/**
 * @brief A2A 适配器私有上下文。
 *
 * 存放端点、密钥引用与超时字段。仅保存密钥引用（api_key_ref），不保存明文密钥。
 */
typedef struct A2AAdapterCtx {
    const char *endpoint;        /**< 请求端点 URL */
    const char *api_key_ref;     /**< 密钥引用（指向安全存储，禁止明文） */
    uint32_t    timeout_ms;      /**< 默认超时（毫秒） */
    void       *config;          /**< 初始化配置（可选） */
} A2AAdapterCtx_t;

/** A2A 适配器全局单例上下文 */
static A2AAdapterCtx_t s_a2a_ctx = {
    .endpoint    = ZHIO_A2A_DEFAULT_ENDPOINT,
    .api_key_ref = NULL,
    .timeout_ms  = 30000u,
    .config      = NULL,
};

/**
 * @brief 初始化 A2A 适配器。
 *
 * @param ctx 适配器上下文（priv_data）
 * @return ZHIO_OK 成功；ZHIO_E_INVAL 参数非法
 */
static int a2a_init(void *ctx)
{
    A2AAdapterCtx_t *c = (A2AAdapterCtx_t *)ctx;
    if (c == NULL) {
        return ZHIO_E_INVAL;
    }
    if (c->endpoint == NULL) {
        return ZHIO_E_INVAL;
    }
    return ZHIO_OK;
}

/**
 * @brief 反初始化 A2A 适配器。
 *
 * @param ctx 适配器上下文（priv_data）
 * @return ZHIO_OK 成功；ZHIO_E_INVAL 参数非法
 */
static int a2a_deinit(void *ctx)
{
    if (ctx == NULL) {
        return ZHIO_E_INVAL;
    }
    return ZHIO_OK;
}

/**
 * @brief 构造 A2A Task 请求体（JSON-RPC 2.0）。
 *
 * @param buf        输出缓冲区
 * @param buf_size   缓冲区大小
 * @param text       消息文本内容
 * @return 期望写入的字符数；若 >= buf_size 说明被截断
 */
static int a2a_build_task_body(char *buf, size_t buf_size, const char *text)
{
    return snprintf(buf, buf_size,
        "{\"jsonrpc\":\"2.0\",\"method\":\"tasks/send\",\"params\":"
        "{\"task\":{\"message\":{\"role\":\"user\",\"parts\":[{\"text\":\"%s\"}]}}}}",
        text);
}

/**
 * @brief 执行 A2A 对话补全。
 *
 * A2A 协议以任务下发为核心，对话补全同样复用 tasks/send 通道，
 * 将消息内容作为任务文本发送。
 *
 * @param ctx        适配器上下文
 * @param messages   用户消息内容
 * @param response   输出响应缓冲区；允许为 NULL
 * @param resp_len   响应长度指针；允许为 NULL
 * @param timeout_ms 超时（毫秒）
 * @return ZHIO_OK 成功；否则为错误码
 */
static int a2a_chat_completion(void *ctx, const char *messages,
                               char *response, uint32_t *resp_len,
                               uint32_t timeout_ms)
{
    A2AAdapterCtx_t *c = (A2AAdapterCtx_t *)ctx;
    CloudTransportFn  t;
    char              body[ZHIO_A2A_BODY_BUF_SIZE];
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

    n = a2a_build_task_body(body, sizeof(body), messages);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return ZHIO_E_INVAL;
    }

    return t(c->endpoint, body, (uint32_t)n, response, resp_len, timeout_ms);
}

/**
 * @brief 执行 A2A Agent 任务。
 *
 * 将任务载荷转换为任务文本并经由 tasks/send 下发。
 *
 * @param ctx        适配器上下文
 * @param task       任务载荷
 * @param task_len   任务长度（字节）
 * @param timeout_ms 超时（毫秒）
 * @return ZHIO_OK 成功；否则为错误码
 */
static int a2a_agent_run(void *ctx, const void *task, uint32_t task_len,
                         uint32_t timeout_ms)
{
    A2AAdapterCtx_t *c = (A2AAdapterCtx_t *)ctx;
    CloudTransportFn  t;
    char              body[ZHIO_A2A_BODY_BUF_SIZE];
    char              msgbuf[ZHIO_A2A_BODY_BUF_SIZE];
    int               n;

    if (c == NULL || task == NULL || task_len == 0u) {
        return ZHIO_E_INVAL;
    }
    if (task_len >= sizeof(msgbuf)) {
        return ZHIO_E_INVAL;
    }

    t = zhio_cloud_transport_get();
    if (t == NULL) {
        return ZHIO_E_NOCLOUD;
    }

    memcpy(msgbuf, task, task_len);
    msgbuf[task_len] = '\0';

    n = a2a_build_task_body(body, sizeof(body), msgbuf);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return ZHIO_E_INVAL;
    }

    return t(c->endpoint, body, (uint32_t)n, NULL, NULL, timeout_ms);
}

/**
 * @brief 执行 A2A 工具调用。
 *
 * 将工具调用编码为任务文本并经 tasks/send 下发。
 *
 * @param ctx        适配器上下文
 * @param tool_name  工具名
 * @param args       工具参数（JSON 文本）
 * @param args_len   参数长度（字节）
 * @param result     输出结果缓冲区；允许为 NULL
 * @param result_len 结果长度指针；允许为 NULL
 * @param timeout_ms 超时（毫秒）
 * @return ZHIO_OK 成功；否则为错误码
 */
static int a2a_tool_call(void *ctx, const char *tool_name, const void *args,
                         uint32_t args_len, void *result, uint32_t *result_len,
                         uint32_t timeout_ms)
{
    A2AAdapterCtx_t *c = (A2AAdapterCtx_t *)ctx;
    CloudTransportFn  t;
    char              body[ZHIO_A2A_BODY_BUF_SIZE];
    char              msgbuf[ZHIO_A2A_BODY_BUF_SIZE];
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

    if (args_len >= sizeof(msgbuf)) {
        return ZHIO_E_INVAL;
    }
    memcpy(msgbuf, args, args_len);
    msgbuf[args_len] = '\0';

    n = a2a_build_task_body(body, sizeof(body), msgbuf);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return ZHIO_E_INVAL;
    }

    return t(c->endpoint, body, (uint32_t)n, (char *)result, result_len, timeout_ms);
}

/**
 * @brief 创建 A2A 适配器上下文。
 *
 * @return 新上下文指针；失败返回 NULL
 */
void *a2a_ctx_new(void)
{
    A2AAdapterCtx_t *c = (A2AAdapterCtx_t *)calloc(1u, sizeof(A2AAdapterCtx_t));
    if (c == NULL) {
        return NULL;
    }
    c->endpoint    = ZHIO_A2A_DEFAULT_ENDPOINT;
    c->api_key_ref = NULL;
    c->timeout_ms  = 30000u;
    return c;
}

/** A2A 适配器静态实例（协议名与回调绑定） */
static CloudAdapter_t s_a2a_adapter = {
    .protocol_name   = "a2a",
    .init            = a2a_init,
    .deinit          = a2a_deinit,
    .chat_completion = a2a_chat_completion,
    .agent_run       = a2a_agent_run,
    .tool_call       = a2a_tool_call,
    .priv_data       = &s_a2a_ctx,
};

/**
 * @brief 工厂函数：获取 A2A 适配器实例。
 *
 * @return CloudAdapterHandle_t 适配器句柄（指向 CloudAdapter_t）
 */
CloudAdapterHandle_t zhio_a2a_adapter_new(void)
{
    return (CloudAdapterHandle_t)&s_a2a_adapter;
}
