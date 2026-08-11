/*
 * fw_harness.c - 通信防火墙独立测试桩（Docker 镜像内运行）
 *
 * 编译独立的防火墙模块（firewall.c），在无内核/无 RTOS 的环境下验证其逻辑：
 *   1) 默认拒绝未授权命令
 *   2) 命令白名单放行
 *   3) 源白名单强制
 *   4) 载荷长度上限
 *   5) 令牌桶限速（通过 fw_advance() 推进时间）
 *
 * 为 firewall.c 提供 zhio_get_tick()/zhio_log() 最小实现（可替换模块的宿主侧）。
 * 退出码 0 = 全部通过。
 */
#include <stdio.h>
#include <string.h>
#include "firewall.h"

/* ---- 宿主侧最小 RTOS 抽象实现（可替换模块的宿主） ---- */
static volatile ZhiosTick_t g_tick = 0;

ZhiosTick_t zhio_get_tick(void) { return g_tick; }

void zhio_log(const char *fmt, ...)
{
    (void)fmt;   /* 静默，仅保证链接；可在生产 BSP 中接入日志系统 */
}

void fw_advance(ZhiosTick_t ms) { g_tick += ms; }

/* ---- 极简断言 ---- */
static int g_fail = 0;
static void expect(const char *name, int cond)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) g_fail++;
}

int main(void)
{
    ZhioFwPacket_t pkt;
    uint32_t allowed = 0, blocked = 0, dropped = 0;

    printf("=== ZhiOS-AF 防火墙逻辑独立验证 ===\n");

    /* 1) 默认拒绝 */
    iFirewallInit();
    iFirewallAllowCmd(0x01);
    memset(&pkt, 0, sizeof(pkt));
    pkt.cmd = 0xFF;
    expect("未授权命令被默认拒绝", xFirewallCheck(&pkt) == ZHIO_FW_BLOCK_CMD);

    /* 2) 命令白名单放行 */
    pkt.cmd = 0x01;
    expect("白名单命令放行", xFirewallCheck(&pkt) == ZHIO_FW_ALLOW);

    /* 3) 源白名单强制 */
    iFirewallAllowSrc(7);
    pkt.cmd = 0x01; pkt.src = 7;
    expect("白名单源放行", xFirewallCheck(&pkt) == ZHIO_FW_ALLOW);
    pkt.src = 99;
    expect("非白名单源被拒绝", xFirewallCheck(&pkt) == ZHIO_FW_BLOCK_SRC);

    /* 4) 载荷长度上限 */
    iFirewallSetMaxPayload(64);
    pkt.cmd = 0x01; pkt.src = 7; pkt.payload_len = 32;
    expect("合法长度放行", xFirewallCheck(&pkt) == ZHIO_FW_ALLOW);
    pkt.payload_len = 512;
    expect("超长载荷被拒绝", xFirewallCheck(&pkt) == ZHIO_FW_BLOCK_LEN);
    iFirewallSetMaxPayload(0);

    /* 5) 令牌桶限速：低速率下验证突发上限与时间补充 */
    vFirewallDeinit();
    iFirewallInit();
    iFirewallAllowCmd(0x01);
    iFirewallSetRate(1, 2);          /* 1 token/s，突发 2 */
    fw_advance(100);                  /* 初始化后已过 100ms，补 1 令牌 */
    pkt.cmd = 0x01; pkt.src = 0; pkt.payload_len = 0;
    expect("突发第1个放行", xFirewallCheck(&pkt) == ZHIO_FW_ALLOW);
    expect("突发第2个放行", xFirewallCheck(&pkt) == ZHIO_FW_ALLOW);
    expect("超出突发被限速", xFirewallCheck(&pkt) == ZHIO_FW_BLOCK_RATE);
    fw_advance(1000);                 /* 过 1s 补 1 令牌 */
    expect("补充令牌后放行", xFirewallCheck(&pkt) == ZHIO_FW_ALLOW);

    vFirewallGetStats(&allowed, &blocked, &dropped);
    printf("  统计：allowed=%u blocked=%u dropped=%u\n",
           (unsigned)allowed, (unsigned)blocked, (unsigned)dropped);

    vFirewallDeinit();
    printf(g_fail ? "==> FIREWALL TEST FAILED\n" : "==> FIREWALL TEST ALL PASS\n");
    return g_fail ? 1 : 0;
}
