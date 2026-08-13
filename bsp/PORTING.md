# ZhiOS-AF 可替换接口 / 二次开发移植指南

> 对应《12-BSP板级支持包开发指南》《14-硬件抽象层-HAL接口规范》《05-实时内核与调度设计文档》。
> 本目录提供一套**可复制的移植模板**，让不同核心开发板快速接入 ZhiOS-AF。

## 1. 可替换接口一览

| 接口 | 契约头文件 | 现状端口 | 作用 |
| --- | --- | --- | --- |
| RTOS 抽象 | `include/zhios_rtos.h` | `rtos/host/`、`rtos/freertos/` | 屏蔽具体内核（FreeRTOS/主机） |
| 硬件抽象 | `include/hal.h` | `hal/hal_host.c`、`bsp/stm32h743/` | 屏蔽外设差异（UART/GPIO/CAN…） |
| 加速器 | `include/npu_dsp.h` | `ai_kernel/npu_dsp/` | 屏蔽 NPU/DSP 差异 |
| 板级支持 | `bsp/<board>/` | `bsp/host/`、`bsp/stm32h743/` | 时钟/引脚/外设初始化 |

**原则**：上层代码只依赖上述头文件；更换开发板只改 `bsp/` 与 `rtos/`，不改上层逻辑。

## 2. 为一块新开发板做移植（5 步）

1. **复制模板**
   ```
   cp -r bsp/PORT_TEMPLATE bsp/my_board
   cd bsp/my_board
   mv board_template.c board.c
   mv hal_template.c hal_my_board.c
   ```
2. **实现 `board.c`**：配置时钟、初始化引脚与外设、使能中断，返回 `ZHIO_OK`。
3. **实现 `hal_my_board.c`**：用真实寄存器驱动替换 `hal.h` 中全部 `TODO`/`UNIMPL`，
   未实现前保持返回 `ZHIO_E_NOSUPPORT` 以保证链接通过。
4. **接入 CMake**：在 `CMakeLists.txt` 的 `ZHIO_PLATFORM` 分支新增 `my_board`，
   将其 `board.c` 与 `hal_my_board.c` 加入 `ZHIO_PLATFORM_SOURCES`。
5. **选 RTOS 端口**：若用 FreeRTOS，`ZHIO_PLATFORM_SOURCES` 选 `rtos/freertos/zhio_rtos_port.c`，
   并在板卡侧提供 `FreeRTOSConfig.h`。

## 3. 移植自检清单

- [ ] `make`/`cmake` 编译通过（0 错误）
- [ ] `hal.h` 中所有接口均已实现（无不返回 `ZHIO_E_NOSUPPORT` 的路径）
- [ ] 时钟与串口波特率正确，`zhio_log` 可输出
- [ ] `zhio_system_init()` 返回 `ZHIO_OK`
- [ ] 运行 `tools/sim/zhio_sim.py` 逻辑用例全部通过
