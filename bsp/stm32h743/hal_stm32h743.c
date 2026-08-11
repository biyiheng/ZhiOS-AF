/*
 * bsp/stm32h743/hal_stm32h743.c - STM32H743 HAL 端口（可替换模块）
 *
 * 对应《14-硬件抽象层-HAL接口规范》《13-引脚矩阵-PinMux配置规范》。
 * 实现 hal.h 声明的全部接口，供上层（内核/Agent/AI 服务/通信）无差别调用。
 * 更换开发板时，以新的 bsp/<board>/hal_<board>.c 替换本文件即可。
 *
 * 本文件为可编译骨架：UART/GPIO/Timer/CAN/Flash 等以 HAL 库调用占位，
 * 生产环境填充 stm32h7xx_hal_* 的寄存器级实现。
 */
#include <string.h>
#include <stdint.h>
#include "hal.h"
#include "zhios_rtos.h"

/* ---- 板级指示灯（PA1 示例，按 PinMux 调整） ---- */
static int g_led = 0;
void board_led_set(int on)    { g_led = on; /* HAL_GPIO_WritePin(LED_GPIO, LED_PIN, on); */ }
void board_led_toggle(void)   { board_led_set(g_led ? 0 : 1); }
void board_delay_ms(uint32_t ms) { zhio_task_sleep(ms); }

/* ---- UART（USART1 示例，波特率 115200） ---- */
int hal_uart_init(uint32_t port, const HalUartConfig_t *cfg)
{ (void)port; (void)cfg; return ZHIO_OK; /* HAL_UART_Init(...); */ }
int hal_uart_send(uint32_t port, const uint8_t *data, uint32_t len, uint32_t timeout_ms)
{ (void)port; (void)data; (void)len; (void)timeout_ms; return ZHIO_OK; /* HAL_UART_Transmit(...); */ }
int hal_uart_receive(uint32_t port, uint8_t *data, uint32_t len, uint32_t timeout_ms)
{ (void)port; (void)data; (void)len; (void)timeout_ms; return ZHIO_E_TIMEOUT; }
/* 测试钩子：真实板卡不支持软件注入 */
int hal_uart_rx_inject(uint32_t port, const uint8_t *data, uint32_t len)
{ (void)port; (void)data; (void)len; return ZHIO_E_NOSUPPORT; }

/* ---- GPIO ---- */
int hal_gpio_init(uint32_t pin, int dir) { (void)pin; (void)dir; return ZHIO_OK; }
int hal_gpio_write(uint32_t pin, int val) { (void)pin; (void)val; return ZHIO_OK; }
int hal_gpio_read(uint32_t pin) { (void)pin; return 0; }
int hal_gpio_toggle(uint32_t pin) { (void)pin; return ZHIO_OK; }

/* ---- Timer ---- */
int hal_timer_start(uint32_t timer, uint32_t period_us, void (*cb)(void))
{ (void)timer; (void)period_us; (void)cb; return ZHIO_OK; /* HAL_TIM_Base_Start_IT(...); */ }
int hal_timer_stop(uint32_t timer) { (void)timer; return ZHIO_OK; }
uint64_t hal_timer_get_counter_us(void) { return (uint64_t)0; }

/* ---- 中断 ---- */
int hal_irq_register(uint32_t irq, void (*handler)(void *), void *arg)
{ (void)irq; (void)handler; (void)arg; return ZHIO_OK; /* HAL_NVIC_SetPriority/EnableIRQ(...); */ }
int hal_irq_enable(uint32_t irq)  { (void)irq; return ZHIO_OK; }
int hal_irq_disable(uint32_t irq) { (void)irq; return ZHIO_OK; }

/* ---- CAN（FDCAN1 示例，位速率见 HalCanConfig_t） ---- */
int hal_can_init(uint32_t port, const HalCanConfig_t *cfg)
{ (void)port; (void)cfg; return ZHIO_OK; /* HAL_FDCAN_Init(...); */ }
int hal_can_send(uint32_t port, uint32_t id, int extended, const uint8_t *data, uint32_t len)
{ (void)port; (void)id; (void)extended; (void)data; (void)len; return ZHIO_OK; }
int hal_can_receive(uint32_t port, uint32_t *id, int *extended, uint8_t *data,
                    uint32_t *len, uint32_t timeout_ms)
{ (void)port; (void)id; (void)extended; (void)data; (void)len; (void)timeout_ms; return ZHIO_E_TIMEOUT; }

/* ---- SPI/I2C/Flash ---- */
int hal_spi_transfer(uint32_t port, const uint8_t *tx, uint8_t *rx, uint32_t len)
{ (void)port; (void)tx; (void)rx; (void)len; return ZHIO_OK; }
int hal_i2c_write(uint32_t port, uint8_t addr, const uint8_t *data, uint32_t len)
{ (void)port; (void)addr; (void)data; (void)len; return ZHIO_OK; }
int hal_i2c_read(uint32_t port, uint8_t addr, uint8_t *data, uint32_t len)
{ (void)port; (void)addr; (void)data; (void)len; return ZHIO_OK; }
int hal_flash_read(uint32_t offset, void *buf, uint32_t len)
{ (void)offset; (void)buf; (void)len; return ZHIO_OK; /* FLASH_Read(...); */ }
int hal_flash_write(uint32_t offset, const void *buf, uint32_t len)
{ (void)offset; (void)buf; (void)len; return ZHIO_OK; }

/* ---- 网络（STM32H743 常用外置 WiFi/ETH，未集成则返回未连接） ---- */
int hal_wifi_init(void) { return ZHIO_OK; }
int hal_wifi_is_connected(void) { return 0; }
int hal_wifi_http_post(const char *url, const char *body, uint32_t body_len,
                       char *resp, uint32_t *resp_len, uint32_t timeout_ms)
{
    (void)url; (void)body; (void)body_len; (void)resp; (void)timeout_ms;
    if (resp_len) *resp_len = 0;
    return ZHIO_E_NOCLOUD;
}
int hal_eth_send(const uint8_t *data, uint32_t len, uint32_t timeout_ms)
{ (void)data; (void)len; (void)timeout_ms; return ZHIO_OK; }
