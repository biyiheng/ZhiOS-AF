/*
 * board.c - host 平台板级初始化（BSP）
 *
 * 对应《12-BSP板级支持包开发指南》。
 * host 平台为无硬件仿真环境，board_init 主要完成 RTOS 底层与日志初始化。
 * 真实开发板（stm32h743 等）以各自 BSP 提供同名接口替换。
 */
#include "hal.h"
#include "zhios_rtos.h"
#include "zhios_config.h"

int board_init(void)
{
    zhio_log("[bsp:host] board_init done");
    return ZHIO_OK;
}

void board_reset(void)
{
    zhio_log("[bsp:host] board_reset (sim)");
}
