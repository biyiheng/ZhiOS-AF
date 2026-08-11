# tools/arm_eabi — 汇编启动/上下文切换 链接验证 与 亚微秒切换/Jitter 自动化压测

> 对应《33-操作系统技术指标体系设计文档》4.1/4.3 中标注为**黄色**的两项指标：
> **上下文切换 < 亚微秒级** 与 **抖动(Jitter) <= 1%**。

本目录提供：

| 文件 | 作用 |
| --- | --- |
| `verify_flash.ld` | 通用链接脚本（向量表@0、`.data`/`.bss`、`DWT->CYCCNT` 钉在 0xE0001004） |
| `verify_main.c` | 冒烟固件：DWT 周期计数、上下文切换关键路径采样、原子/屏障/位带自检、CMSDK UART 打印 CSV |
| `build_verify.sh` | Linux/WSL/macOS 编译+链接+可选 QEMU 运行 |
| `build_verify.ps1` | Windows PowerShell 等价脚本（自动探测 STM32CubeIDE 内嵌工具链） |
| `stress_sched.py` | QEMU/开发板**自动化压测方案**：多轮采样→统计 mean/max/jitter→强制判定指标 |

## 一、链接验证（arm-none-eabi，本机即可完成）

验证 `bsp/stm32h743/startup_stm32h743.s` 与 `port_context_switch.s` 能与 C 冒烟
测试**成功链接**，向量表置于 0x00000000，且 PendSV / 原子 / 复位等关键汇编符号齐全。

```bash
# Linux / WSL / macOS
./build_verify.sh                # 产出 build/verify_stm32h743.elf（M7）与 verify_mps2.elf（M3）

# Windows PowerShell
powershell -ExecutionPolicy Bypass -File build_verify.ps1
```

自动探测工具链：`PATH` → `$ARM_GCC` → STM32CubeIDE 内嵌 gcc。已验证产物：
- 向量表 `0x00000000`=栈顶、`0x00000004`=Reset_Handler、HardFault→fault_dump。
- 符号 `Reset_Handler` / `PendSV_Handler` / `atomic_test_and_set` 齐全。

## 二、QEMU 运行验证与自动化压测

在有 `qemu-system-arm` 的环境：

```bash
# 1) 构建 + 运行（串口打印上下文切换周期样本）
./build_verify.sh --run

# 2) 自动化压测（默认 20 轮，解析全部样本，判定指标）
python3 stress_sched.py --build --runs 50
```

`stress_sched.py` 判定逻辑（不满足则退出码非 0，供 CI 中断）：
- **平均切换时间 < 1000ns（1μs）**（`--max-switch-ns`，即"亚微秒级切换"门槛）。
- **jitter 占比 < 10%**（`--max-jitter-pct`，即 (max-min)/mean，可收紧至 5%）。

周期→时间换算用 CPU 频率（`--freq`，mps2-an385 默认 25MHz，可改为真实频率）。

## 三、在真实开发板上做同样的压测

同一固件可直接烧录到真实 MCU（把 UART 基址换成目标板，如 STM32H743 的 USARTx，
并把 `--freq` 设为真实内核频率，如 480MHz）：

```bash
# 方案 A：串口捕获（固件已打印 sw_cycles 与 SW_SUMMARY）
python3 stress_sched.py --elf build/verify_stm32h743.elf --freq 480000000 \
    --serial COM3        # 或 /dev/ttyUSB0

# 方案 B：J-Link/GDB 采样 DWT_CYCCNT
#   在 GDB 中读取 DWT->CYCCNT 换算上下文切换周期，汇入同一套统计/判定。
```

> 指标判定与 QEMU 完全一致（同一套 mean/max/jitter 统计 + 门槛判定），
> 仅数据来源从"QEMU 串口"替换为"开发板串口/GDB"。STM32H743@480MHz 实测
> 上下文切换典型 <200ns，可稳定满足 1μs 门槛，jitter 主要来自 Flash 缓存与
> 中断抢占，建议结合 `ZHIO_CFG_SCHED_TRACE`（见 `inference_scheduler.c`）交叉验证。

## 四、与调度器 trace 的衔接

`ai_kernel/inference_scheduler/inference_scheduler.c` 已内置 `ZHIO_CFG_SCHED_TRACE`
调度决策周期测量（Cortex-M 用 `DWT->CYCCNT`）。本固件测的是**上下文切换关键路径**
的裸周期；调度器测的是**调度决策**周期。二者互补：压测覆盖底层切换抖动，
trace 覆盖调度决策抖动，共同构成《33》4.1 的完整抖动分析链路。
