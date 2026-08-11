/*
 * firewall.c - 通信防火墙实现
 *
 * 对应《20-安全性设计文档》《21-威胁模型与风险评估文档》。
 * 采用"默认拒绝、显式放行"的最小权限策略：
 *   1) 命令白名单  2) 源白名单  3) 令牌桶限速  4) 载荷长度上限。
 * 令牌桶基于 zhio_get_tick()（ms）实现，时间回绕安全（差值计算）。
 */
#include <string.h>
#include <stdint.h>
#include "firewall.h"
#include "zhios_config.h"

#define ZHIO_FW_MAX_ALLOWED  16

typedef struct {
    uint32_t cmd;
    uint32_t valid;
} FwCmdRule_t;

typedef struct {
    uint32_t src;
    uint32_t valid;
} FwSrcRule_t;

typedef struct {
    int       enabled;          /* 1 = 启用 */
    uint32_t  rate;             /* tokens/sec，0 = 关闭 */
    uint32_t  capacity;         /* burst */
    uint32_t  tokens;           /* 当前令牌（定点放大 1000 倍避免浮点） */
    uint32_t  last_tick;
} TokenBucket_t;

typedef struct {
    FwCmdRule_t cmds[ZHIO_FW_MAX_ALLOWED];
    FwSrcRule_t srcs[ZHIO_FW_MAX_ALLOWED];
    uint32_t    max_payload;    /* 0 = 关闭 */
    TokenBucket_t bucket;
    uint32_t    allowed, blocked, dropped;
} Firewall_t;

static Firewall_t g_fw;

int iFirewallInit(void)
{
    memset(&g_fw, 0, sizeof(g_fw));
    /* 默认限速：100 tokens/s，突发 10 */
    g_fw.bucket.enabled  = 1;
    g_fw.bucket.rate     = 100u;
    g_fw.bucket.capacity = 10u * 1000u;   /* 放大 1000 倍 */
    g_fw.bucket.tokens   = g_fw.bucket.capacity;
    g_fw.bucket.last_tick = zhio_get_tick();
    zhio_log("[firewall] init: default-deny, rate=100/s burst=10");
    return ZHIO_OK;
}

void vFirewallDeinit(void)
{
    zhio_log("[firewall] deinit: allowed=%u blocked=%u dropped=%u",
             (unsigned)g_fw.allowed, (unsigned)g_fw.blocked, (unsigned)g_fw.dropped);
    memset(&g_fw, 0, sizeof(g_fw));
}

int iFirewallSetRate(uint32_t tokens_per_sec, uint32_t burst)
{
    if (tokens_per_sec == 0) {
        g_fw.bucket.enabled = 0;
        return ZHIO_OK;
    }
    if (burst == 0) burst = 1;
    g_fw.bucket.enabled  = 1;
    g_fw.bucket.rate     = tokens_per_sec;
    g_fw.bucket.capacity = burst * 1000u;
    g_fw.bucket.tokens   = g_fw.bucket.capacity;
    g_fw.bucket.last_tick = zhio_get_tick();
    return ZHIO_OK;
}

int iFirewallAllowCmd(uint32_t cmd)
{
    uint32_t i;
    for (i = 0; i < ZHIO_FW_MAX_ALLOWED; i++) {
        if (!g_fw.cmds[i].valid) {
            g_fw.cmds[i].valid = 1;
            g_fw.cmds[i].cmd = cmd;
            return ZHIO_OK;
        }
        if (g_fw.cmds[i].cmd == cmd) return ZHIO_OK; /* 已放行 */
    }
    return ZHIO_E_NOMEM;
}

int iFirewallAllowSrc(uint32_t src)
{
    uint32_t i;
    for (i = 0; i < ZHIO_FW_MAX_ALLOWED; i++) {
        if (!g_fw.srcs[i].valid) {
            g_fw.srcs[i].valid = 1;
            g_fw.srcs[i].src = src;
            return ZHIO_OK;
        }
        if (g_fw.srcs[i].src == src) return ZHIO_OK;
    }
    return ZHIO_E_NOMEM;
}

int iFirewallSetMaxPayload(uint32_t max_bytes)
{
    g_fw.max_payload = max_bytes;
    return ZHIO_OK;
}

/* 令牌桶补充：按经过的毫秒数补令牌（定点） */
static void bucket_refill(void)
{
    if (!g_fw.bucket.enabled) return;
    ZhiosTick_t now = zhio_get_tick();
    ZhiosTick_t el = now - g_fw.bucket.last_tick;   /* 回绕安全 */
    g_fw.bucket.last_tick = now;
    if (el == 0) return;
    /* 经过 el ms 应得令牌 = rate * el/1000，放大 1000 倍即 rate*el */
    uint32_t add = (uint32_t)((uint64_t)g_fw.bucket.rate * el);
    g_fw.bucket.tokens += add;
    if (g_fw.bucket.tokens > g_fw.bucket.capacity)
        g_fw.bucket.tokens = g_fw.bucket.capacity;
}

ZhioFwVerdict_t xFirewallCheck(const ZhioFwPacket_t *pkt)
{
    if (!pkt) return ZHIO_FW_BLOCK_CMD;
    g_fw.allowed++;

    /* 1) 命令白名单（默认拒绝） */
    uint32_t i, cmd_ok = 0;
    for (i = 0; i < ZHIO_FW_MAX_ALLOWED; i++)
        if (g_fw.cmds[i].valid && g_fw.cmds[i].cmd == pkt->cmd) { cmd_ok = 1; break; }
    if (!cmd_ok) { g_fw.blocked++; g_fw.allowed--; return ZHIO_FW_BLOCK_CMD; }

    /* 2) 源白名单：仅当配置了至少一条源规则时才强制（默认不校验源） */
    int has_src_rule = 0, src_ok = 0;
    for (i = 0; i < ZHIO_FW_MAX_ALLOWED; i++) {
        if (g_fw.srcs[i].valid) {
            has_src_rule = 1;
            if (g_fw.srcs[i].src == pkt->src) src_ok = 1;
        }
    }
    if (has_src_rule && !src_ok) { g_fw.blocked++; g_fw.allowed--; return ZHIO_FW_BLOCK_SRC; }

    /* 3) 载荷长度上限 */
    if (g_fw.max_payload && pkt->payload_len > g_fw.max_payload) {
        g_fw.blocked++; g_fw.allowed--; return ZHIO_FW_BLOCK_LEN;
    }

    /* 4) 令牌桶限速 */
    bucket_refill();
    if (g_fw.bucket.enabled) {
        if (g_fw.bucket.tokens < 1000u) {      /* 不足 1 令牌 */
            g_fw.dropped++; g_fw.allowed--; return ZHIO_FW_BLOCK_RATE;
        }
        g_fw.bucket.tokens -= 1000u;
    }
    return ZHIO_FW_ALLOW;
}

void vFirewallGetStats(uint32_t *allowed, uint32_t *blocked, uint32_t *dropped)
{
    if (allowed) *allowed = g_fw.allowed;
    if (blocked) *blocked = g_fw.blocked;
    if (dropped) *dropped = g_fw.dropped;
}
