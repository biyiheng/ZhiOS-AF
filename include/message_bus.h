/*
 * message_bus.h - Agent 零拷贝消息总线
 *
 * 对应《09-Agent自治框架设计文档》。AgentMessage_t 使用共享内存指针（零拷贝），
 * 优先级 0-7，支持 deadline。本层只依赖 RTOS 队列句柄（即各 Agent 的 inbox），
 * 与 Agent 结构解耦；Agent 层负责把 AgentHandle_t 解析为其 inbox 队列。
 */
#ifndef ZHIO_MESSAGE_BUS_H
#define ZHIO_MESSAGE_BUS_H

#include <stdint.h>
#include "zhios_types.h"
#include "zhios_err.h"
#include "zhios_config.h"
#include "zhios_rtos.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 消息（data 为共享内存指针，零拷贝；生命周期由发送方保证） */
typedef struct {
    uint32_t          id;
    uint32_t          src;          /* 源 Agent id */
    uint32_t          dst;          /* 目标 Agent id */
    AgentMessageType_t type;
    uint8_t           priority;     /* 0-7，7 最高 */
    ZhiosTick_t       deadline;
    uint32_t          data_len;
    const void       *data;         /* 零拷贝共享指针 */
} AgentMessage_t;

int  iMessageBusInit(void);
void vMessageBusDeinit(void);

/* 发送到指定 inbox 队列（队列满按 timeout 等待） */
int iMessageBusSend(ZhiosQueueHandle_t inbox, const AgentMessage_t *msg, ZhiosTick_t timeout);
/* 广播到多个 inbox 队列 */
int iMessageBusBroadcast(ZhiosQueueHandle_t *inboxes, uint32_t n,
                         const AgentMessage_t *msg, ZhiosTick_t timeout);
/* 接收（buf 拷贝接收体；buf 可为 NULL 仅取元数据） */
int iMessageBusReceive(ZhiosQueueHandle_t inbox, AgentMessage_t *msg,
                       void *buf, uint32_t *buf_len, ZhiosTick_t timeout);

/* 统计 */
int iMessageBusStats(uint32_t *sent, uint32_t *dropped, uint32_t *pending);

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_MESSAGE_BUS_H */
