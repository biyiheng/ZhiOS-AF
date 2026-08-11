/*
 * cloud_transport.c - 云端传输层默认实现
 *
 * 默认使用 HAL 的 WiFi HTTP POST（主机上返回 NOCLOUD）。
 * 应用/测试可调用 zhio_cloud_transport_set 注入真实或 mock 传输。
 */
#include "cloud_transport.h"
#include "hal.h"

static CloudTransportFn g_transport = NULL;

void zhio_cloud_transport_set(CloudTransportFn fn) { g_transport = fn; }
CloudTransportFn zhio_cloud_transport_get(void)
{
    if (g_transport) return g_transport;
    return hal_wifi_http_post;   /* 默认：主机返回 NOCLOUD */
}
