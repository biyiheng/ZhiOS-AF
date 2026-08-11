/*
 * cloud_transport.h - 云端适配器共享传输层
 *
 * 对应《19-云端协议适配器规范》。各协议适配器通过统一的传输回调发送请求。
 * 生产环境注入真实 HTTP/TLS 客户端（hal_wifi_http_post 等）；
 * 测试环境注入 mock 传输以验证请求/响应解析。
 */
#ifndef ZHIO_CLOUD_TRANSPORT_H
#define ZHIO_CLOUD_TRANSPORT_H

#include <stdint.h>
#include "zhios_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*CloudTransportFn)(const char *url, const char *body, uint32_t body_len,
                                char *resp, uint32_t *resp_len, uint32_t timeout_ms);

/* 设置/获取当前传输回调 */
void           zhio_cloud_transport_set(CloudTransportFn fn);
CloudTransportFn zhio_cloud_transport_get(void);

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_CLOUD_TRANSPORT_H */
