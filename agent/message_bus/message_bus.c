/*
 * message_bus.c - Agent 零拷贝消息总线实现
 *
 * 对应《09-Agent自治框架设计文档》。
 * 消息以 AgentMessage_t 结构入队（data 为零拷贝共享指针），
 * priority(0-7)/deadline 作为元数据随消息传递。
 * 注：当前使用 RTOS FIFO 队列实现；优先级抢占式排队列为可替换增强
 *     （可通过为每个 Agent 维护 8 个优先级队列实现，见消息总线设计文档）。
 */
#include <string.h>
#include <stdint.h>
#include "message_bus.h"
#include "zhios_rtos.h"

static uint32_t g_sent, g_dropped;

int iMessageBusInit(void) { g_sent = 0; g_dropped = 0; return ZHIO_OK; }
void vMessageBusDeinit(void) {}

int iMessageBusSend(ZhiosQueueHandle_t inbox, const AgentMessage_t *msg, ZhiosTick_t timeout)
{
    if (!inbox || !msg) return ZHIO_E_INVAL;
    if (msg->priority > 7) return ZHIO_E_INVAL;
    int rc = zhio_queue_send(inbox, msg, timeout);
    if (rc == ZHIO_OK) g_sent++;
    else g_dropped++;
    return rc;
}

int iMessageBusBroadcast(ZhiosQueueHandle_t *inboxes, uint32_t n,
                         const AgentMessage_t *msg, ZhiosTick_t timeout)
{
    if (!inboxes || !msg || n == 0) return ZHIO_E_INVAL;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int rc = iMessageBusSend(inboxes[i], msg, timeout);
        if (rc != ZHIO_OK) return rc;
    }
    return ZHIO_OK;
}

int iMessageBusReceive(ZhiosQueueHandle_t inbox, AgentMessage_t *msg,
                       void *buf, uint32_t *buf_len, ZhiosTick_t timeout)
{
    if (!inbox || !msg) return ZHIO_E_INVAL;
    int rc = zhio_queue_receive(inbox, msg, timeout);
    if (rc != ZHIO_OK) return rc;

    if (buf && buf_len) {
        if (*buf_len < msg->data_len) return ZHIO_E_INVAL; /* 缓冲过小 */
        if (msg->data && msg->data_len) memcpy(buf, msg->data, msg->data_len);
        *buf_len = msg->data_len;
    } else if (buf_len) {
        *buf_len = msg->data_len;
    }
    return ZHIO_OK;
}

int iMessageBusStats(uint32_t *sent, uint32_t *dropped, uint32_t *pending)
{
    if (sent) *sent = g_sent;
    if (dropped) *dropped = g_dropped;
    if (pending) *pending = 0;   /* 各 Agent 队列深度，由 Agent 层统计 */
    return ZHIO_OK;
}
