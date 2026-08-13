/*
 * board_template.c - 新开发板 BSP 模板（可复制到 bsp/<你的板卡>/board.c）
 *
 * 二次开发步骤（对应《12-BSP板级支持包开发指南》《04-项目结构文档》）：
 *   1. 复制本目录到 bsp/<your_board>/
 *   2. 将 board_template.c 重命名为 board.c，hal_template.c 重命名为 hal_<your_board>.c
 *   3. 在 CMakeLists.txt 的 ZHIO_PLATFORM 分支中新增 <your_board> 并接入上述两文件
 *   4. 按需实现下列初始化与 hal.h 中全部接口
 * 上层代码只依赖 include/hal.h，与具体开发板解耦（可替换模块设计）。
 */
#include <stdint.h>
#include "hal.h"
#include "zhios_err.h"

/* ---------------- 时钟配置（示例：替换为你的芯片的时钟树） ---------------- */
static void bsp_system_clock_config(void)
{
    /* TODO: 配置系统主频 / PLL / Flash 等待周期，例如：
     *   stm32_clock_pll(HSE, PLL_M, PLL_N, PLL_P, PLL_Q); */
}

/* ---------------- 板级初始化（内核启动时调用） ---------------- */
int board_init(void)
{
    bsp_system_clock_config();

    /* TODO: 使能并配置 GPIO / UART / CAN / Timer / 中断控制器
     *   hal_gpio_init(..., HAL_GPIO_OUTPUT);   // 指示灯
     *   hal_uart_init(0, &(HalUartConfig_t){ .baudrate = 115200 }); */

    /* TODO: 若使用 FreeRTOS，此处调用 vPortSetupTimerInterrupt() 等 */
    return ZHIO_OK;
}

void board_reset(void)
{
    /* TODO: 触发系统软复位（如 NVIC_SystemReset()） */
    for (;;) {}
}

/* ---------------- 指示灯 / 延时 ---------------- */
void board_led_set(int on)    { (void)on;  /* TODO: GPIO 置位/清零 */ }
void board_led_toggle(void)   { /* TODO: GPIO 翻转 */ }
void board_delay_ms(uint32_t ms) { (void)ms; /* TODO: 忙等或 SysTick 延时 */ }
