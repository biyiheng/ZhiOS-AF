/*
 * comm.h - 通信协议层（帧协议）
 *
 * 对应《18-通信协议规范文档》。统一帧格式 zhios_frame_t，
 * 支持 UART / CAN / WiFi / WebSocket 通道；config agent 关键词配置命令。
 */
#ifndef ZHIO_COMM_H
#define ZHIO_COMM_H

#include <stdint.h>
#include "zhios_types.h"
#include "zhios_err.h"
#include "zhios_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 帧类型 */
typedef enum {
    ZHIO_FRAME_CMD    = 0x01,   /* 命令（含 config agent） */
    ZHIO_FRAME_STATUS = 0x02,   /* 状态上报 */
    ZHIO_FRAME_LOG    = 0x03,   /* 日志 */
    ZHIO_FRAME_EVENT  = 0x04,   /* 事件 */
    ZHIO_FRAME_CONFIG = 0x05,   /* 配置下发 */
    ZHIO_FRAME_ACK    = 0x06,   /* 应答 */
} ZhiosFrameType_t;

/* 帧格式（网络字节/小端按实现约定；CRC32 覆盖 LEN+TYPE+载荷） */
#define ZHIO_FRAME_SOF  0xAAu
#define ZHIO_FRAME_EOF  0x55u
#define ZHIO_FRAME_MAX_PAYLOAD  (ZHIO_CFG_MAX_MSG_SIZE)

typedef struct {
    uint8_t  sof;
    uint16_t len;                 /* 载荷长度（小端） */
    uint8_t  type;
    uint8_t  payload[ZHIO_FRAME_MAX_PAYLOAD];
    uint32_t crc32;
    uint8_t  eof;
} ZhiosFrame_t;

/* 编解码（无状态，线程安全调用方保证） */
int comm_frame_encode(uint8_t type, const void *payload, uint16_t len,
                      uint8_t *out, uint16_t *out_len);
int comm_frame_decode(const uint8_t *buf, uint16_t buf_len, ZhiosFrame_t *frame);

/* CRC32（IEEE 802.3 多项式 0xEDB88320 反射表） */
uint32_t comm_crc32(const void *data, uint32_t len);

/* config agent 命令解析（关键词配置）：
 *   命令格式：config agent "谨慎模式+视觉优先+允许云端辅助"
 *   返回：OK: preference updated  或  ERR: <reason> */
int comm_config_parse(const char *cmd, AgentHandle_t auto_agent,
                      char *response, uint32_t resp_len);

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_COMM_H */
