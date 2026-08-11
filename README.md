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
