/*
 * bsp/stm32h743/board.c - STM32H743 板级初始化（BSP 可替换模块）
 *
 * 对应《12-BSP板级支持包开发指南》《16-交叉编译与工具链配置文档》。
 * 这是"可替换模块"设计的关键：更换开发板只需新增/替换一个 BSP 目录，
 * 提供同名 board_init/board_reset 与 hal_* 接口，上层代码无需改动。
 *
 * 本文件为可编译骨架（真实寄存器操作由各项目按 STM32H743 HAL 填充）。
 */
#include <stdint.h>
#include "hal.h"
#include "zhios_rtos.h"
#include "zhios_config.h"

/* ---- 系统主频（H743 典型 480MHz @ 8MHz HSE） ---- */
#define BSP_HSE_HZ          8000000u
#define BSP_SYSCLK_HZ       480000000u

/* 占位：时钟/Flash 等待状态/电源配置。生产按 STM32CubeMX 生成的
 * SystemClock_Config() 移植即可，保持 board_init 签名不变。 */
static void bsp_system_clock_config(void)
{
    (void)BSP_HSE_HZ;
    (void)BSP_SYSCLK_HZ;
    /* SystemClock_Config(); */
}

int board_init(void)
{
    bsp_system_clock_config();
    /* GPIO/UART/CAN/Timer 等外设初始化由各自 hal_stm32h743.c 完成 */

    /* 关闭并复位看门狗（若使能） */
    zhio_log("[bsp:stm32h743] board_init done @%uMHz", BSP_SYSCLK_HZ / 1000000u);
    return ZHIO_OK;
}

void board_reset(void)
{
    /* NVIC_SystemReset(); 生产替换为真实软复位 */
    zhio_log("[bsp:stm32h743] board_reset");
}
