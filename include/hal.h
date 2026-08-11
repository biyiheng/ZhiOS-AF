/*
 * hal.h - 硬件抽象层（HAL）通用接口
 *
 * 对应《14-硬件抽象层-HAL接口规范》。所有上层代码只依赖本接口，
 * 具体实现由各 BSP（hal/<port> 或 bsp/<board>）提供。
 * 这是"可替换模块"设计的关键：更换开发板只需更换 BSP，无需改上层。
 */
#ifndef ZHIO_HAL_H
#define ZHIO_HAL_H

#include <stdint.h>
#include <stddef.h>
#include "zhios_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============ 板级初始化 ============ */
/* 由各 BSP 实现：时钟、外设、引脚、RTOS 底层初始化 */
int  board_init(void);
void board_reset(void);

/* 指示灯/调试（host 打印到终端） */
void board_led_set(int on);
void board_led_toggle(void);
void board_delay_ms(uint32_t ms);

/* ============ UART HAL ============ */
typedef struct {
    uint32_t baudrate;
    uint8_t  data_bits;
    uint8_t  stop_bits;
    uint8_t  parity;
} HalUartConfig_t;

int  hal_uart_init(uint32_t port, const HalUartConfig_t *cfg);
int  hal_uart_send(uint32_t port, const uint8_t *data, uint32_t len, uint32_t timeout_ms);
int  hal_uart_receive(uint32_t port, uint8_t *data, uint32_t len, uint32_t timeout_ms);

/* 可选测试钩子：向接收路径注入数据（host 仿真用）。真实板卡可不实现/返回 ZHIO_E_NOSUPPORT。 */
int  hal_uart_rx_inject(uint32_t port, const uint8_t *data, uint32_t len);

/* ============ GPIO HAL ============ */
#define HAL_GPIO_OUTPUT  0
#define HAL_GPIO_INPUT   1
int  hal_gpio_init(uint32_t pin, int dir);
int  hal_gpio_write(uint32_t pin, int val);
int  hal_gpio_read(uint32_t pin);
int  hal_gpio_toggle(uint32_t pin);

/* ============ Timer HAL ============ */
int  hal_timer_start(uint32_t timer, uint32_t period_us, void (*cb)(void));
int  hal_timer_stop(uint32_t timer);
uint64_t hal_timer_get_counter_us(void);

/* ============ 中断 HAL ============ */
int  hal_irq_register(uint32_t irq, void (*handler)(void *), void *arg);
int  hal_irq_enable(uint32_t irq);
int  hal_irq_disable(uint32_t irq);

/* ============ CAN HAL ============ */
typedef struct {
    uint32_t bitrate;
} HalCanConfig_t;
int hal_can_init(uint32_t port, const HalCanConfig_t *cfg);
int hal_can_send(uint32_t port, uint32_t id, int extended, const uint8_t *data, uint32_t len);
int hal_can_receive(uint32_t port, uint32_t *id, int *extended, uint8_t *data, uint32_t *len, uint32_t timeout_ms);

/* ============ SPI/I2C/Flash HAL（占位，按需实现） ============ */
int hal_spi_transfer(uint32_t port, const uint8_t *tx, uint8_t *rx, uint32_t len);
int hal_i2c_write(uint32_t port, uint8_t addr, const uint8_t *data, uint32_t len);
int hal_i2c_read(uint32_t port, uint8_t addr, uint8_t *data, uint32_t len);
int hal_flash_read(uint32_t offset, void *buf, uint32_t len);
int hal_flash_write(uint32_t offset, const void *buf, uint32_t len);

/* ============ 网络/通信 HAL ============ */
int hal_wifi_init(void);
int hal_wifi_is_connected(void);
int hal_wifi_http_post(const char *url, const char *body, uint32_t body_len,
                       char *resp, uint32_t *resp_len, uint32_t timeout_ms);
int hal_eth_send(const uint8_t *data, uint32_t len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* ZHIO_HAL_H */
