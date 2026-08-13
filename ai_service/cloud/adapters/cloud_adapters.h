/*
 * cloud_adapters.h - 云端协议适配器汇总头文件
 *
 * 对应《19-云端协议适配器规范》。集中声明 OpenAI / Claude / A2A / MCP
 * 四个云端协议适配器的工厂函数，供上层服务注册与路由使用。
 */
#ifndef ZHIO_CLOUD_ADAPTERS_H
#define ZHIO_CLOUD_ADAPTERS_H

#include "ai_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取 OpenAI 适配器实例。
 * @return CloudAdapterHandle_t 指向 CloudAdapter_t 的句柄
 */
CloudAdapterHandle_t zhio_openai_adapter_new(void);

/**
 * @brief 获取 Claude 适配器实例。
 * @return CloudAdapterHandle_t 指向 CloudAdapter_t 的句柄
 */
CloudAdapterHandle_t zhio_claude_adapter_new(void);

/**
 * @brief 获取 A2A 适配器实例。
 * @return CloudAdapterHandle_t 指向 CloudAdapter_t 的句柄
 */
CloudAdapterHandle_t zhio_a2a_adapter_new(void);

/**
 * @brief 获取 MCP 适配器实例。
 * @return CloudAdapterHandle_t 指向 CloudAdapter_t 的句柄
 */
CloudAdapterHandle_t zhio_mcp_adapter_new(void);

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_CLOUD_ADAPTERS_H */
