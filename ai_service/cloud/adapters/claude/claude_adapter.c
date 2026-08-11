/*
 * claude_adapter.c - Anthropic Claude Messages API 协议适配器
 *
 * 对应《19-云端协议适配器规范》。通过共享传输层发送 HTTP 请求，
 * 采用 Anthropic Messages API 请求/响应格式。
 *
 * 请求体示例：
 *   {"model":"claude-3-haiku","max_tokens":256,"messages":[{"role":"user","content":"<messages>"}]}
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "ai_service.h"
#include "cloud_transport.h"

/* Claude 默认模型名 */
#define ZHIO_CLAUDE_DEFAULT_MODEL   "claude-3-haiku"
/* Claude Messages API 默认端点 */
#define ZHIO_CLAUDE_DEFAULT_ENDPOINT \
    "https://api.anthropic.com/v1/messages"
/* 默认最大输出 token 数 */
#define ZHIO_CLAUDE_DEFAULT_MAX_TOKENS 256u
/* 本地请求体构造缓冲区大小 */
#define ZHIO_CLAUDE_BODY_BUF_SIZE   512u

/**
 * @brief Claude 适配器私有上下文。
 *
 * 存放端点、密钥引用、模型名、max_tokens 与超时字段。仅保存密钥引用
 * （api_key_ref），不保存明文密钥。
 */
typedef struct ClaudeAdapterCtx {
    const char *endpoint;        /**< 请求端点 URL */
    const char *api_key_ref;     /**< 密钥引用（指向安全存储，禁止明文） */
    const char *model;           /**< 模型名 */
    uint32_t    max_tokens;      /**< 最大输出 token 数 */
    uint32_t    timeout_ms;      /**< 默认超时（毫秒） */
    void       *config;          /**< 初始化配置（可选） */
} ClaudeAdapterCtx_t;

/** Claude 适配器全局单例上下文 */
static ClaudeAdapterCtx_t s_claude_ctx = {
    .endpoint    = ZHIO_CLAUDE_DEFAULT_ENDPOINT,
    .api_key_ref = NULL,
    .model       = ZHIO_CLAUDE_DEFAULT_MODEL,
    .max_tokens  = ZHIO_CLAUDE_DEFAULT_MAX_TOKENS,
    .timeout_ms  = 30000u,
    .config      = NULL,
};

/**
 * @brief 初始化 Claude 适配器。
 *
 * @param ctx 适配器上下文（priv_data）
 * @return ZHIO_OK 成功；ZHIO_E_INVAL 参数非法
 */
static int claude_init(void *ctx)
{
    ClaudeAdapterCtx_t *c = (ClaudeAdapterCtx_t *)ctx;
    if (c == NULL) {
        return ZHIO_E_INVAL;
    }
    if (c->endpoint == NULL || c->model == NULL || c->max_tokens == 0u) {
        return ZHIO_E_INVAL;
    }
    return ZHIO_OK;
}

/**
 * @brief 反初始化 Claude 适配器。
 *
 * @param ctx 适配器上下文（priv_data）
 * @return ZHIO_OK 成功；ZHIO_E_INVAL 参数非法
 */
static int claude_deinit(void *ctx)
{
    if (ctx == NULL) {
        return ZHIO_E_INVAL;
    }
    return ZHIO_OK;
}

/**
 * @brief 构造 Claude Messages API 请求体。
 *
 * @param buf        输出缓冲区
 * @param buf_size   缓冲区大小
 * @param model      模型名
 * @param max_tokens 最大输出 token 数
 * @param messages   用户消息内容
 * @return 期望写入的字符数；若 >= buf_size 说明被截断
 */
static int claude_build_chat_body(char *buf, size_t buf_size,
                                  const char *model, uint32_t max_tokens,
                                  const char *messages)
{
    return snprintf(buf, buf_size,
        "{\"model\":\"%s\",\"max_tokens\":%u,\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
        model, (unsigned)max_tokens, messages);
}

/**
 * @brief 执行 Claude 对话补全。
 *
 * @param ctx        适配器上下文
 * @param messages   用户消息内容
 * @param response   输出响应缓冲区；允许为 NULL
 * @param resp_len   响应长度指针；允许为 NULL
 * @param timeout_ms 超时（毫秒）
 * @return ZHIO_OK 成功；否则为错误码
 */
static int claude_chat_completion(void *ctx, const char *messages,
                                  char *response, uint32_t *resp_len,
                                  uint32_t timeout_ms)
{
    ClaudeAdapterCtx_t *c = (ClaudeAdapterCtx_t *)ctx;
    CloudTransportFn     t;
    char                 body[ZHIO_CLAUDE_BODY_BUF_SIZE];
    int                  n;

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

    n = claude_build_chat_body(body, sizeof(body), c->model, c->max_tokens, messages);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return ZHIO_E_INVAL;
    }

    return t(c->endpoint, body, (uint32_t)n, response, resp_len, timeout_ms);
}

/**
 * @brief 执行 Claude Agent 任务。
 *
 * 将任务载荷转换为用户消息并经 Messages API 发送。
 *
 * @param ctx        适配器上下文
 * @param task       任务载荷
 * @param task_len   任务长度（字节）
 * @param timeout_ms 超时（毫秒）
 * @return ZHIO_OK 成功；否则为错误码
 */
static int claude_agent_run(void *ctx, const void *task, uint32_t task_len,
                            uint32_t timeout_ms)
{
    ClaudeAdapterCtx_t *c = (ClaudeAdapterCtx_t *)ctx;
    CloudTransportFn     t;
    char                 body[ZHIO_CLAUDE_BODY_BUF_SIZE];
    char                 msgbuf[ZHIO_CLAUDE_BODY_BUF_SIZE];
    int                  n;

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

    n = claude_build_chat_body(body, sizeof(body), c->model, c->max_tokens, msgbuf);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return ZHIO_E_INVAL;
    }

    return t(c->endpoint, body, (uint32_t)n, NULL, NULL, timeout_ms);
}

/**
 * @brief 执行 Claude 工具调用。
 *
 * 将工具调用转换为一次 Messages API 请求，把工具名与参数编码进消息内容。
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
static int claude_tool_call(void *ctx, const char *tool_name, const void *args,
                            uint32_t args_len, void *result, uint32_t *result_len,
                            uint32_t timeout_ms)
{
    ClaudeAdapterCtx_t *c = (ClaudeAdapterCtx_t *)ctx;
    CloudTransportFn     t;
    char                 body[ZHIO_CLAUDE_BODY_BUF_SIZE];
    char                 msgbuf[ZHIO_CLAUDE_BODY_BUF_SIZE];
    int                  n;

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

    n = snprintf(body, sizeof(body),
        "{\"model\":\"%s\",\"max_tokens\":%u,\"messages\":[{\"role\":\"user\",\"content\":\"工具调用 %s 参数 %s\"}]}",
        c->model, (unsigned)c->max_tokens, tool_name, msgbuf);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return ZHIO_E_INVAL;
    }

    return t(c->endpoint, body, (uint32_t)n, (char *)result, result_len, timeout_ms);
}

/**
 * @brief 创建 Claude 适配器上下文。
 *
 * @return 新上下文指针；失败返回 NULL
 */
void *claude_ctx_new(void)
{
    ClaudeAdapterCtx_t *c = (ClaudeAdapterCtx_t *)calloc(1u, sizeof(ClaudeAdapterCtx_t));
    if (c == NULL) {
        return NULL;
    }
    c->endpoint    = ZHIO_CLAUDE_DEFAULT_ENDPOINT;
    c->model       = ZHIO_CLAUDE_DEFAULT_MODEL;
    c->max_tokens  = ZHIO_CLAUDE_DEFAULT_MAX_TOKENS;
    c->api_key_ref = NULL;
    c->timeout_ms  = 30000u;
    return c;
}

/** Claude 适配器静态实例（协议名与回调绑定） */
static CloudAdapter_t s_claude_adapter = {
    .protocol_name   = "claude",
    .init            = claude_init,
    .deinit          = claude_deinit,
    .chat_completion = claude_chat_completion,
    .agent_run       = claude_agent_run,
    .tool_call       = claude_tool_call,
    .priv_data       = &s_claude_ctx,
};

/**
 * @brief 工厂函数：获取 Claude 适配器实例。
 *
 * @return CloudAdapterHandle_t 适配器句柄（指向 CloudAdapter_t）
 */
CloudAdapterHandle_t zhio_claude_adapter_new(void)
{
    return (CloudAdapterHandle_t)&s_claude_adapter;
}
