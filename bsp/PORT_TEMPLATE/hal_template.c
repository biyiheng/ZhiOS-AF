/*
 * hal_template.c - 新开发板 HAL 实现模板（可复制到 bsp/<your_board>/hal_<your_board>.c）
 *
 * 对应《14-硬件抽象层-HAL接口规范》。
 * 必须实现 include/hal.h 声明的全部接口；未实现前先返回 ZHIO_E_NOSUPPORT，
 * 确保上层编译链接通过，再逐个用真实寄存器驱动替换（提高硬件利用率与响应）。
 */
#include <stdint.h>
#include <string.h>
#include "hal.h"
#include "zhios_err.h"

#define UNIMPL()  return ZHIO_E_NOSUPPORT

/* ============ UART HAL ============ */
int hal_uart_init(uint32_t port, const HalUartConfig_t *cfg)
{
    (void)port; (void)cfg;
    /* TODO: 配置 UART 时钟、波特率分频、收发中断使能 */
    return ZHIO_OK;
}
int hal_uart_send(uint32_t port, const uint8_t *data, uint32_t len, uint32_t timeout_ms)
{
    (void)port; (void)timeout_ms;
    /* TODO: 轮询/中断发送；建议用发送 FIFO/DMA 提升吞吐 */
    uint32_t i;
    for (i = 0; i < len; i++) { /* 写数据寄存器 data[i] */ }
    (void)data;
    return ZHIO_OK;
}
int hal_uart_receive(uint32_t port, uint8_t *data, uint32_t len, uint32_t timeout_ms)
{
    (void)port; (void)data; (void)len; (void)timeout_ms;
    /* TODO: 从接收 FIFO/环形缓冲取出 len 字节 */
    UNIMPL();
}
int hal_uart_rx_inject(uint32_t port, const uint8_t *data, uint32_t len)
{
    (void)port; (void)data; (void)len;
    UNIMPL();   /* 测试钩子：真实板卡通常不支持软件注入 */
}

/* ============ GPIO HAL ============ */
int hal_gpio_init(uint32_t pin, int dir)  { (void)pin; (void)dir;  /* TODO */ return ZHIO_OK; }
int hal_gpio_write(uint32_t pin, int val) { (void)pin; (void)val;  /* TODO */ return ZHIO_OK; }
int hal_gpio_read(uint32_t pin)           { (void)pin; /* TODO */ return 0; }
int hal_gpio_toggle(uint32_t pin)         { (void)pin; /* TODO */ return ZHIO_OK; }

/* ============ Timer HAL ============ */
int hal_timer_start(uint32_t timer, uint32_t period_us, void (*cb)(void))
{ (void)timer; (void)period_us; (void)cb; /* TODO */ return ZHIO_OK; }
int hal_timer_stop(uint32_t timer)        { (void)timer; /* TODO */ return ZHIO_OK; }
uint64_t hal_timer_get_counter_us(void)   { /* TODO */ return 0; }

/* ============ 中断 HAL ============ */
int hal_irq_register(uint32_t irq, void (*handler)(void *), void *arg)
{ (void)irq; (void)handler; (void)arg; /* TODO: NVIC 注册 */ return ZHIO_OK; }
int hal_irq_enable(uint32_t irq)  { (void)irq;  /* TODO */ return ZHIO_OK; }
int hal_irq_disable(uint32_t irq) { (void)irq;  /* TODO */ return ZHIO_OK; }

/* ============ CAN HAL ============ */
int hal_can_init(uint32_t port, const HalCanConfig_t *cfg)
{ (void)port; (void)cfg; /* TODO: CAN 位时序/过滤器/邮箱 */ return ZHIO_OK; }
int hal_can_send(uint32_t port, uint32_t id, int extended, const uint8_t *data, uint32_t len)
{ (void)port; (void)id; (void)extended; (void)data; (void)len; /* TODO */ return ZHIO_OK; }
int hal_can_receive(uint32_t port, uint32_t *id, int *extended, uint8_t *data,
                    uint32_t *len, uint32_t timeout_ms)
{ (void)port; (void)id; (void)extended; (void)data; (void)len; (void)timeout_ms; UNIMPL(); }

/* ============ SPI / I2C / Flash HAL ============ */
int hal_spi_transfer(uint32_t port, const uint8_t *tx, uint8_t *rx, uint32_t len)
{ (void)port; (void)tx; (void)rx; (void)len; /* TODO */ return ZHIO_OK; }
int hal_i2c_write(uint32_t port, uint8_t addr, const uint8_t *data, uint32_t len)
{ (void)port; (void)addr; (void)data; (void)len; /* TODO */ return ZHIO_OK; }
int hal_i2c_read(uint32_t port, uint8_t addr, uint8_t *data, uint32_t len)
{ (void)port; (void)addr; (void)data; (void)len; UNIMPL(); }
int hal_flash_read(uint32_t offset, void *buf, uint32_t len)
{ (void)offset; (void)buf; (void)len; /* TODO */ return ZHIO_OK; }
int hal_flash_write(uint32_t offset, const void *buf, uint32_t len)
{ (void)offset; (void)buf; (void)len; /* TODO */ return ZHIO_OK; }

/* ============ 网络 / 通信 HAL ============ */
int hal_wifi_init(void)         { /* TODO */ return ZHIO_OK; }
int hal_wifi_is_connected(void) { /* TODO */ return 0; }
int hal_wifi_http_post(const char *url, const char *body, uint32_t body_len,
                       char *resp, uint32_t *resp_len, uint32_t timeout_ms)
{ (void)url; (void)body; (void)body_len; (void)resp; (void)timeout_ms;
  if (resp_len) *resp_len = 0;
  return ZHIO_E_NOCLOUD; }
int hal_eth_send(const uint8_t *data, uint32_t len, uint32_t timeout_ms)
{ (void)data; (void)len; (void)timeout_ms; /* TODO */ return ZHIO_OK; }
