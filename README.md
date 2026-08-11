# ZhiOS-AF — 智脑实时操作系统（Agent 框架版）

Agent-First 嵌入式实时操作系统，基于 FreeRTOS V11.2.0+ 二次开发。
六层架构：HAL → 实时内核 → AI 内核 → 模型运行时 + 混合 AI 服务 → Agent 自治层 → 应用服务层。

## 特性

- **可替换模块设计**：RTOS 抽象层（`zhios_rtos.h` + `rtos/host|freertos`）、HAL（`hal.h` + 各 BSP）、NPU/DSP HAL（`npu_dsp.h`）均为可替换接口，更换开发板/内核/加速器无需改上层。
- **零碎片张量内存**：Bump（持久）+ 栈式 LIFO（临时）双区，30 天碎片率 <1% 目标。
- **EDF + 固定优先级混合调度**：推理任务调度器。
- **Agent 自治框架**：1 个 Auto 主 Agent + Vision/Motion/Safety/Force/Quality 子 Agent，消息总线零拷贝通信。
- **混合 AI 服务**：本地推理 + 云端路由（OpenAI / Claude / A2A / MCP 协议适配器）。
- **安全子模块**：安全校验器、推理看门狗。
- **Docker 容器化**：一键构建、单元测试、演示、云端网关与监控。

## 目录总览

```
zhi-os-af/
├── include/        # 公共契约头文件（API 规范）
├── rtos/           # RTOS 抽象层端口（host=pthread, freertos=FreeRTOS）
├── hal/ bsp/       # HAL 实现与板级支持包（host, stm32h743）
├── kernel/         # 系统初始化编排
├── ai_kernel/      # 推理调度 / 张量内存 / NPU-DSP HAL / 安全
├── model_runtime/  # 模型运行时（模型即对象）
├── capability/     # 能力偏好引擎（关键词配置）
├── agent/          # Auto 主 Agent / 子 Agent / 消息总线
├── ai_service/     # 混合 AI 服务 + 云端适配器
├── comm/           # 通信帧协议 + config agent 命令
├── tools/          # ZTEST 单元测试 / ZMonitor 监控
├── tests/          # 单元测试用例
├── examples/       # 演示程序
├── docker/         # 云端网关容器
├── Makefile        # 主机快速构建
├── CMakeLists.txt  # 多平台构建（host / stm32h743 / ...）
├── Dockerfile      # 容器构建（host）
└── docker-compose.yml
```

## 快速开始（Docker，推荐）

```bash
# 1) 构建并运行单元测试
docker compose up --build tests

# 2) 运行演示程序
docker compose run --rm demo

# 3) 启动云端网关（mock 模式）与监控
docker compose up -d gateway monitor
```

## 一键部署（Docker / WSL2）

采用**单文件 `docker-compose.yml`**（9 个服务）编排全部模块。支持三种方式一键部署
Agent 控制软件 + 通信防火墙 + UART 驱动 + 端到端综合验证：

```bash
# 方式 A：WSL2 环境一键部署（推荐，含环境校验与可选 clean clone）
bash tools/run_wsl_deploy.sh                        # 用当前目录部署
bash tools/run_wsl_deploy.sh --fresh-clone <URL>    # 干净拉取 release 分支后部署
bash tools/run_wsl_deploy.sh --keep                 # 部署后保持控制软件常驻

# 方式 B：通用一键集成（Linux/macOS / Windows PowerShell）
bash tools/run_integration.sh                                   # Linux/macOS
powershell -ExecutionPolicy Bypass -File tools/run_integration.ps1   # Windows

# 方式 C：手动编排
docker compose up -d agent-console      # 常驻 Agent 控制软件，端口 8000
docker compose run --rm firewall        # 防火墙逻辑验证（一次性）
docker compose run --rm driver          # 驱动接口验证（一次性）
docker compose run --rm e2e             # 端到端 + 压力综合验证（一次性）
```

> 部署前置要求：Docker 引擎可用（Linux 建议 Docker Desktop + WSL2，或 WSL2 内原生 Docker）。
> 一键脚本会自动校验 Docker/OSType/WSL2 内核（>=5.10）。完整环境准备、排障与二次开发接入
> 见《[部署操作手册](../docs/部署操作手册.md)》。

### 可部署性自检

无需 Docker 引擎即可静态校验 release 分支是否"完整可独立部署"（检查每个服务构建上下文、
Dockerfile 的 COPY 源、bind 挂载路径与关键运行文件是否齐全）：

```bash
python tools/verify_release.py          # 期望输出 RELEASE VERIFY ALL PASS
```

## 发布说明（Release）

当前版本 **v1.1.0**，归档于 `release` 分支（完整可独立部署快照）。详见
[RELEASE_NOTES.md](RELEASE_NOTES.md) 与 [DEVELOPMENT_LOG.md](DEVELOPMENT_LOG.md)。

| 版本 | 分支 | 重点 |
| --- | --- | --- |
| v1.1.0 | `release` | 单文件容器编排 + 一键部署（WSL2/Windows/Linux）+ 安全加固（413/名称/JSON）+ 回归与压力测试（66 项断言）+ 完整源码归档 |

## 本地主机构建（Linux/macOS）

```bash
make            # 构建 zhio_tests 与 zhio_demo
make test       # 构建并运行单元测试
make demo       # 运行演示
```

## 交叉编译（MCU，STM32H743 示例）

```bash
cmake -B build -DZHIO_PLATFORM=stm32h743 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake \
      -DZHIO_FREERTOS_DIR=<path/to/FreeRTOS-kernel>
cmake --build build
```

更换开发板：新增 `bsp/<board>/board.c` + `hal_<board>.c` 并在
`CMakeLists.txt` 平台分支注册即可，上层代码零改动（见《12-BSP板级支持包开发指南》）。

## 文档

32 份设计/规范/部署文档见 `../docs/`（仓库外同级目录），开发日志见 `DEVELOPMENT_LOG.md`。

## 许可证

Apache 2.0
