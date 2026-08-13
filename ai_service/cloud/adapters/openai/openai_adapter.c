/*
 * openai_adapter.c - OpenAI Chat Completions 协议适配器
 *
 * 对应《19-云端协议适配器规范》。通过共享传输层发送 HTTP 请求，
 * 采用 OpenAI 官方 Chat Completions 请求/响应格式。
 *
 * 请求体示例：
 *   {"model":"gpt-4o-mini","messages":[{"role":"user","content":"<messages>"}]}
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "ai_service.h"
#include "cloud_transport.h"

/* OpenAI Chat Completions 默认模型名 */
#define ZHIO_OPENAI_DEFAULT_MODEL  "gpt-4o-mini"
/* OpenAI Chat Completions 默认端点 */
#define ZHIO_OPENAI_DEFAULT_ENDPOINT \
    "https://api.openai.com/v1/chat/completions"
/* 本地请求体构造缓冲区大小 */
#define ZHIO_OPENAI_BODY_BUF_SIZE  512u

/**
 * @brief OpenAI 适配器私有上下文。
 *
 * 存放端点、密钥引用、模型名与超时等运行时字段，作为 CloudAdapter_t::priv_data。
 * 注意：仅保存密钥引用（api_key_ref），不保存明文密钥，避免硬编码敏感信息。
 */
typedef struct OpenAIAdapterCtx {
    const char *endpoint;        /**< 请求端点 URL */
    const char *api_key_ref;     /**< 密钥引用（指向安全存储中的密钥，禁止明文） */
    const char *model;           /**< 模型名 */
    uint32_t    timeout_ms;      /**< 默认超时（毫秒） */
    void       *config;          /**< 初始化配置（可选，透传自上层） */
} OpenAIAdapterCtx_t;

/** OpenAI 适配器全局单例上下文 */
static OpenAIAdapterCtx_t s_openai_ctx = {
    .endpoint   = ZHIO_OPENAI_DEFAULT_ENDPOINT,
    .api_key_ref = NULL,
    .model      = ZHIO_OPENAI_DEFAULT_MODEL,
    .timeout_ms = 30000u,
    .config     = NULL,
};

/**
 * @brief 初始化 OpenAI 适配器。
 *
 * 读取上下文已有字段并复位为可用状态。当前端点/模型等均由编译期默认值
 * 或上层配置提供，此处仅做可运行校验。
 *
 * @param ctx 适配器上下文（priv_data）
 * @return ZHIO_OK 成功；ZHIO_E_INVAL 参数非法
 */
static int openai_init(void *ctx)
{
    OpenAIAdapterCtx_t *c = (OpenAIAdapterCtx_t *)ctx;
    if (c == NULL) {
        return ZHIO_E_INVAL;
    }
    if (c->endpoint == NULL || c->model == NULL) {
        return ZHIO_E_INVAL;
    }
    return ZHIO_OK;
}

/**
 * @brief 反初始化 OpenAI 适配器。
 *
 * @param ctx 适配器上下文（priv_data）
 * @return ZHIO_OK 成功；ZHIO_E_INVAL 参数非法
 */
static int openai_deinit(void *ctx)
{
    if (ctx == NULL) {
        return ZHIO_E_INVAL;
    }
    return ZHIO_OK;
}

/**
 * @brief 构造 OpenAI Chat Completions 请求体。
 *
 * 使用 snprintf 构造 JSON，不依赖外部 JSON 库。调用方需保证 buf 大小足够，
 * 返回值用于判断是否发生截断。
 *
 * @param buf      输出缓冲区
 * @param buf_size 缓冲区大小
 * @param model    模型名
 * @param messages 用户消息内容
 * @return 期望写入的字符数；若 >= buf_size 说明被截断
 */
static int openai_build_chat_body(char *buf, size_t buf_size,
                                  const char *model, const char *messages)
{
    return snprintf(buf, buf_size,
        "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
        model, messages);
}

/**
 * @brief 执行 OpenAI 对话补全。
 *
 * 构造请求体并经由共享传输层发送；若传输失败则透传其错误码。
 *
 * @param ctx        适配器上下文
 * @param messages   用户消息内容（文本）
 * @param response   输出响应缓冲区；允许为 NULL（仅发送不接收）
 * @param resp_len   输入时为 response 容量，输出时为实际字节数；允许为 NULL
 * @param timeout_ms 超时（毫秒）
 * @return ZHIO_OK 成功；否则为 ZHIO_E_INVAL / 传输层错误码
 */
static int openai_chat_completion(void *ctx, const char *messages,
                                  char *response, uint32_t *resp_len,
                                  uint32_t timeout_ms)
{
    OpenAIAdapterCtx_t *c = (OpenAIAdapterCtx_t *)ctx;
    CloudTransportFn     t;
    char                 body[ZHIO_OPENAI_BODY_BUF_SIZE];
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

    n = openai_build_chat_body(body, sizeof(body), c->model, messages);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return ZHIO_E_INVAL;
    }

    return t(c->endpoint, body, (uint32_t)n, response, resp_len, timeout_ms);
}

/**
 * @brief 执行 OpenAI Agent 任务。
 *
 * Agent 任务与普通对话共用 Chat Completions 通道，将任务载荷按文本方式
 * 组装为用户消息后发送。若后续协议升级为原生 Agent 接口，可在此扩展。
 *
 * @param ctx        适配器上下文
 * @param task       任务载荷（二进制/文本）
 * @param task_len   任务长度（字节）
 * @param timeout_ms 超时（毫秒）
 * @return ZHIO_OK 成功；否则为错误码
 */
static int openai_agent_run(void *ctx, const void *task, uint32_t task_len,
                            uint32_t timeout_ms)
{
    OpenAIAdapterCtx_t *c = (OpenAIAdapterCtx_t *)ctx;
    CloudTransportFn     t;
    char                 body[ZHIO_OPENAI_BODY_BUF_SIZE];
    char                 msgbuf[ZHIO_OPENAI_BODY_BUF_SIZE];
    int                  n, m;
    uint32_t             resp_len = 0u;

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

    /* 将任务载荷转为文本消息 */
    memcpy(msgbuf, task, task_len);
    msgbuf[task_len] = '\0';

    n = openai_build_chat_body(body, sizeof(body), c->model, msgbuf);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return ZHIO_E_INVAL;
    }

    m = t(c->endpoint, body, (uint32_t)n, NULL, &resp_len, timeout_ms);
    return m;
}

/**
 * @brief 执行 OpenAI 工具调用。
 *
 * 将工具调用转换为一次对话请求，把工具名与参数编码到消息内容中。
 * 生产环境的原生 function calling 可在此基础上扩展。
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
static int openai_tool_call(void *ctx, const char *tool_name, const void *args,
                            uint32_t args_len, void *result, uint32_t *result_len,
                            uint32_t timeout_ms)
{
    OpenAIAdapterCtx_t *c = (OpenAIAdapterCtx_t *)ctx;
    CloudTransportFn     t;
    char                 body[ZHIO_OPENAI_BODY_BUF_SIZE];
    char                 msgbuf[ZHIO_OPENAI_BODY_BUF_SIZE];
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
        "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"工具调用 %s 参数 %s\"}]}",
        c->model, tool_name, msgbuf);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return ZHIO_E_INVAL;
    }

    return t(c->endpoint, body, (uint32_t)n, (char *)result, result_len, timeout_ms);
}

/**
 * @brief 创建 OpenAI 适配器上下文。
 *
 * 分配并初始化私有上下文，供调用方自行管理生命周期。
 *
 * @return 新上下文指针；失败返回 NULL
 */
void *openai_ctx_new(void)
{
    OpenAIAdapterCtx_t *c = (OpenAIAdapterCtx_t *)calloc(1u, sizeof(OpenAIAdapterCtx_t));
    if (c == NULL) {
        return NULL;
    }
    c->endpoint   = ZHIO_OPENAI_DEFAULT_ENDPOINT;
    c->model      = ZHIO_OPENAI_DEFAULT_MODEL;
    c->api_key_ref = NULL;
    c->timeout_ms = 30000u;
    return c;
}

/** OpenAI 适配器静态实例（协议名与回调绑定） */
static CloudAdapter_t s_openai_adapter = {
    .protocol_name   = "openai",
    .init            = openai_init,
    .deinit          = openai_deinit,
    .chat_completion = openai_chat_completion,
    .agent_run       = openai_agent_run,
    .tool_call       = openai_tool_call,
    .priv_data       = &s_openai_ctx,
};

/**
 * @brief 工厂函数：获取 OpenAI 适配器实例。
 *
 * @return CloudAdapterHandle_t 适配器句柄（指向 CloudAdapter_t）
 */
CloudAdapterHandle_t zhio_openai_adapter_new(void)
{
    return (CloudAdapterHandle_t)&s_openai_adapter;
}
