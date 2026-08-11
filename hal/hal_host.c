/*
 * hal_host.c - HAL 通用接口的"主机仿真"实现
 *
 * 对应《14-硬件抽象层-HAL接口规范》。
 * 在无硬件的开发环境（ZTEST/ZSim/演示）中提供可运行实现：
 *   - UART 发送 → 终端输出，接收 → 超时返回
 *   - GPIO/指示灯 → 终端显示状态
 *   - WiFi 默认不可达（可由测试/演示注入 mock 传输）
 *   - Flash → 静态内存仿真
 * 更换真实开发板时，以对应 BSP（bsp/<board>/hal_<board>.c）替换本文件即可。
 *
 * =============================================================================
 * 模块说明（维护入口）
 * -----------------------------------------------------------------------------
 * 职责     ：主机端 HAL 全接口仿真，保证无硬件时可运行/可测试。
 * 依赖     ：hal.h、zhios_rtos（zhio_task_sleep 用于非阻塞轮询）。
 * 被谁调用 ：内核/Agent/AI/通信各层，以及测试与演示程序。
 * 驱动指标 ：UART 采用"环形缓冲 + 非阻塞接收"（单生产者单消费者，head/tail 无锁），
 *             对应《33-操作系统技术指标体系设计文档》4.4"设备驱动"维度；
 *             真实板卡需以 volatile + DMB/DSB 内存屏障保证寄存器/缓冲区顺序性
 *             （见 bsp/stm32h743/hal_stm32h743.c 说明）。
 * =============================================================================
 */
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "hal.h"
#include "zhios_rtos.h"

/* ---- 板级 ---- */
static int g_led = 0;
void board_led_set(int on) { g_led = on; printf("[hal] LED %s\n", on ? "ON" : "OFF"); }
void board_led_toggle(void) { board_led_set(g_led ? 0 : 1); }
void board_delay_ms(uint32_t ms) { zhio_task_sleep(ms); }

/* ---- UART（环形缓冲，非阻塞接收，提升利用率与响应） ---- */
#define ZHIO_HOST_UART_PORTS  4
#define ZHIO_HOST_UART_RB_SZ  256

typedef struct {
    uint8_t  buf[ZHIO_HOST_UART_RB_SZ];
    uint32_t head;              /* 写指针 */
    uint32_t tail;              /* 读指针 */
} UartRb_t;

static UartRb_t g_uart[ZHIO_HOST_UART_PORTS];
static const uint32_t g_uart_mask = ZHIO_HOST_UART_RB_SZ - 1;

static inline uint32_t rb_used(const UartRb_t *r)
{ return (r->head - r->tail) & g_uart_mask; }

static int rb_push(UartRb_t *r, uint8_t b)
{
    uint32_t nh = (r->head + 1) & g_uart_mask;
    if (nh == r->tail) return 0;                 /* 满 */
    r->buf[r->head] = b; r->head = nh;
    return 1;
}
static int rb_pop(UartRb_t *r, uint8_t *b)
{
    if (r->head == r->tail) return 0;            /* 空 */
    *b = r->buf[r->tail]; r->tail = (r->tail + 1) & g_uart_mask;
    return 1;
}

int hal_uart_init(uint32_t port, const HalUartConfig_t *cfg)
{
    (void)cfg;
    if (port >= ZHIO_HOST_UART_PORTS) return ZHIO_E_INVAL;
    memset(&g_uart[port], 0, sizeof(g_uart[port]));
    return ZHIO_OK;
}
int hal_uart_send(uint32_t port, const uint8_t *data, uint32_t len, uint32_t timeout_ms)
{
    (void)port; (void)timeout_ms;
    if (data && len) fwrite(data, 1, len, stdout);
    fflush(stdout);
    return ZHIO_OK;
}
int hal_uart_rx_inject(uint32_t port, const uint8_t *data, uint32_t len)
{
    if (port >= ZHIO_HOST_UART_PORTS || !data) return ZHIO_E_INVAL;
    uint32_t i, injected = 0;
    for (i = 0; i < len; i++)
        if (rb_push(&g_uart[port], data[i])) injected++;
    return (injected == len) ? ZHIO_OK : ZHIO_E_BUSY;   /* 队列满返回 BUSY */
}
/* 非阻塞式接收：先读缓冲，缓冲不足时在超时窗口内短轮询，避免忙等阻塞 */
int hal_uart_receive(uint32_t port, uint8_t *data, uint32_t len, uint32_t timeout_ms)
{
    if (port >= ZHIO_HOST_UART_PORTS || !data || len == 0) return ZHIO_E_INVAL;
    ZhiosTick_t start = zhio_get_tick();
    uint32_t got = 0;
    while (got < len) {
        uint8_t b;
        if (rb_pop(&g_uart[port], &b)) {
            data[got++] = b;
            continue;
        }
        if (timeout_ms != ZHIO_MAX_DELAY && zhio_get_tick() - start > timeout_ms) break;
        zhio_task_sleep(1);                          /* 释放 CPU，提升利用率 */
    }
    return (got == len) ? ZHIO_OK : ZHIO_E_TIMEOUT;
}

/* ---- GPIO ---- */
static uint32_t g_gpio_out[8];
int hal_gpio_init(uint32_t pin, int dir) { (void)pin; (void)dir; return ZHIO_OK; }
int hal_gpio_write(uint32_t pin, int val) { if (pin < 8) g_gpio_out[pin] = val; return ZHIO_OK; }
int hal_gpio_read(uint32_t pin) { return (pin < 8) ? (int)g_gpio_out[pin] : 0; }
int hal_gpio_toggle(uint32_t pin) { if (pin < 8) { g_gpio_out[pin] ^= 1u; return ZHIO_OK; } return ZHIO_E_INVAL; }

/* ---- Timer ---- */
int hal_timer_start(uint32_t timer, uint32_t period_us, void (*cb)(void)) { (void)timer; (void)period_us; (void)cb; return ZHIO_OK; }
int hal_timer_stop(uint32_t timer) { (void)timer; return ZHIO_OK; }
uint64_t hal_timer_get_counter_us(void) { return (uint64_t)zhio_get_tick() * 1000ULL; }

/* ---- 中断 ---- */
int hal_irq_register(uint32_t irq, void (*handler)(void *), void *arg) { (void)irq; (void)handler; (void)arg; return ZHIO_OK; }
int hal_irq_enable(uint32_t irq) { (void)irq; return ZHIO_OK; }
int hal_irq_disable(uint32_t irq) { (void)irq; return ZHIO_OK; }

/* ---- CAN ---- */
int hal_can_init(uint32_t port, const HalCanConfig_t *cfg) { (void)port; (void)cfg; return ZHIO_OK; }
int hal_can_send(uint32_t port, uint32_t id, int extended, const uint8_t *data, uint32_t len)
{ (void)port; (void)extended; printf("[can] TX id=0x%X len=%u\n", (unsigned)id, (unsigned)len); if (data && len) fwrite(data, 1, len, stdout); return ZHIO_OK; }
int hal_can_receive(uint32_t port, uint32_t *id, int *extended, uint8_t *data, uint32_t *len, uint32_t timeout_ms)
{ (void)port; (void)id; (void)extended; (void)data; (void)len; (void)timeout_ms; return ZHIO_E_TIMEOUT; }

/* ---- SPI/I2C/Flash ---- */
int hal_spi_transfer(uint32_t port, const uint8_t *tx, uint8_t *rx, uint32_t len) { (void)port; (void)tx; (void)rx; (void)len; return ZHIO_OK; }
int hal_i2c_write(uint32_t port, uint8_t addr, const uint8_t *data, uint32_t len) { (void)port; (void)addr; (void)data; (void)len; return ZHIO_OK; }
int hal_i2c_read(uint32_t port, uint8_t addr, uint8_t *data, uint32_t len) { (void)port; (void)addr; (void)data; (void)len; return ZHIO_OK; }

static uint8_t g_flash[4096];
int hal_flash_read(uint32_t offset, void *buf, uint32_t len) { if (offset + len > sizeof(g_flash)) return ZHIO_E_INVAL; memcpy(buf, g_flash + offset, len); return ZHIO_OK; }
int hal_flash_write(uint32_t offset, const void *buf, uint32_t len) { if (offset + len > sizeof(g_flash)) return ZHIO_E_INVAL; memcpy(g_flash + offset, buf, len); return ZHIO_OK; }

/* ---- 网络 ---- */
/* 可通过链接期注入 mock：提供 hal_wifi_http_post 的强实现以覆盖（见 tests/mock） */
int hal_wifi_init(void) { return ZHIO_OK; }
int hal_wifi_is_connected(void) { return 0; }
int hal_wifi_http_post(const char *url, const char *body, uint32_t body_len,
                       char *resp, uint32_t *resp_len, uint32_t timeout_ms)
{ (void)url; (void)body; (void)body_len; (void)resp; (void)timeout_ms;
  if (resp_len) *resp_len = 0;
  return ZHIO_E_NOCLOUD; }
int hal_eth_send(const uint8_t *data, uint32_t len, uint32_t timeout_ms) { (void)data; (void)len; (void)timeout_ms; return ZHIO_OK; }
