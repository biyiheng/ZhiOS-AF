/*
 * firewall.h - ZhiOS-AF 通信防火墙（安全子模块）
 *
 * 对应《20-安全性设计文档》《21-威胁模型与风险评估文档》。
 * 在通信层与云端/Agent 消息进入核心前做统一过滤：
 *   - 命令白名单（allowlist）
 *   - 源白名单（allowlist）
 *   - 令牌桶限速（防暴力/洪泛 DoS）
 *   - 载荷长度上限（防缓冲区膨胀 / 栈溢出利用）
 * 由内核初始化时调用 iFirewallInit() 启用；所有外部入包
 * （UART/CAN/WiFi/WebSocket/云端适配器）均应经 xFirewallCheck() 校验。
 */
#ifndef ZHIO_FIREWALL_H
#define ZHIO_FIREWALL_H

#include <stdint.h>
#include "zhios_err.h"
#include "zhios_rtos.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 防火墙裁决结果 */
typedef enum {
    ZHIO_FW_ALLOW      = 0,   /* 放行 */
    ZHIO_FW_BLOCK_CMD  = 1,   /* 命令不在白名单 */
    ZHIO_FW_BLOCK_SRC  = 2,   /* 源不在白名单 */
    ZHIO_FW_BLOCK_RATE = 3,   /* 超出限速（令牌耗尽） */
    ZHIO_FW_BLOCK_LEN  = 4,   /* 载荷长度超上限 */
} ZhioFwVerdict_t;

/* 入包描述（由通信层/适配器填充后提交检查） */
typedef struct {
    uint32_t cmd;            /* 命令/协议号 */
    uint32_t src;            /* 源标识（0 表示未知/未配置） */
    uint32_t payload_len;    /* 载荷字节数 */
} ZhioFwPacket_t;

/* ---- 生命周期 ---- */
int  iFirewallInit(void);
void vFirewallDeinit(void);

/* ---- 规则配置（默认：拒绝一切，显式放行） ---- */
int iFirewallSetRate(uint32_t tokens_per_sec, uint32_t burst); /* 限速，0 关闭限速 */
int iFirewallAllowCmd(uint32_t cmd);                            /* 白名单命令 */
int iFirewallAllowSrc(uint32_t src);                            /* 白名单源 */
int iFirewallSetMaxPayload(uint32_t max_bytes);                 /* 载荷上限，0 关闭 */

/* ---- 入包检查 ---- */
ZhioFwVerdict_t xFirewallCheck(const ZhioFwPacket_t *pkt);

/* ---- 统计 ---- */
void vFirewallGetStats(uint32_t *allowed, uint32_t *blocked, uint32_t *dropped);

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_FIREWALL_H */
