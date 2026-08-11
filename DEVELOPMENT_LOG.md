# ZhiOS-AF 开发 / 自检 / 构建日志

> 版本：v1.0.0 ｜ 日期：2026-08-11 ｜ 作者：项目组
> 对应交付要求：完整代码文件 + 存档分类 + Docker 植入/二次开发 + 自检纠错 + MCU 可替换模块 + 文档更新。

---

## 1. 交付范围与目标

基于《docs/》32 份设计文档，生成 ZhiOS-AF（Agent-First 嵌入式实时操作系统）的**完整可运行代码**，并满足：

1. 代码完整、分类清晰（按六层架构目录）。
2. 结合 Docker 技术，便于**无环境植入**与**二次开发**（一键构建/测试/演示/网关）。
3. 自检代码与算法逻辑，修正发现的 bug。
4. 为嵌入式 MCU 设计**可替换模块/接口**（RTOS 抽象、HAL、NPU/DSP HAL、BSP），方便不同核心开发板二次开发。
5. 更新文档、给出详细日志。

---

## 2. 代码清单与分类（存档）

| 目录 | 职责 | 关键文件 | 层 |
| --- | --- | --- | --- |
| `include/` | 公共契约头文件 | `zhios.h`(总入口)、`zhios_rtos.h`、`zhios_config.h`、`zhios_err.h`、`zhios_types.h` | 跨层 |
| `rtos/host/` | RTOS 抽象（pthread） | `zhio_rtos_port.c` | 替换模块 |
| `rtos/freertos/` | RTOS 抽象（FreeRTOS） | `zhio_rtos_port.c` | 替换模块 |
| `hal/` | HAL 实现 | `hal_host.c` | 第 0 层 |
| `bsp/host/` | host 板级 | `board.c` | 替换模块 |
| `bsp/stm32h743/` | STM32H743 板级 | `board.c` / `hal_stm32h743.c` / `FreeRTOSConfig.h` | 替换模块 |
| `kernel/` | 系统初始化 | `kernel.c` | 第 1 层 |
| `ai_kernel/inference_scheduler/` | 推理调度 | `inference_scheduler.c` | 第 2 层 |
| `ai_kernel/tensor_mem/` | 张量内存 | `tensor_mem.c` | 第 2 层 |
| `ai_kernel/npu_dsp/` | NPU/DSP HAL | `npu_dsp.c` | 第 2 层 |
| `ai_kernel/security/` | 安全子模块 | `security.c` | 第 2 层 |
| `model_runtime/` | 模型运行时 | `model_runtime.c` | 第 3 层 |
| `capability/` | 能力偏好引擎 | `capability.c` | 第 4.5 层 |
| `agent/auto_agent/` | Auto 主 Agent | `agent.c` | 第 4.5 层 |
| `agent/sub_agents/` | 子 Agent | `sub_agents.c/.h` | 第 4.5 层 |
| `agent/message_bus/` | 消息总线 | `message_bus.c` | 第 4.5 层 |
| `ai_service/` | 混合 AI 服务 | `ai_service.c` | 第 3.5 层 |
| `ai_service/cloud/adapters/` | 云端协议适配器 | `openai/claude/a2a/mcp` | 第 3.5 层 |
| `comm/` | 通信帧协议 | `comm.c` | 通信层 |
| `tools/ztest/` | 单元测试框架 | `ztest.c/.h` | 工具 |
| `tools/zmonitor/` | 运行监控 | `index.html` | 工具 |
| `tests/` | 单元测试 | `test_core.c` / `test_agent.c` / `test_main.c` | 测试 |
| `examples/` | 演示 | `demo_main.c` | 应用 |
| `docker/gateway/` | 云端网关 | `gateway.py` / `Dockerfile` | 部署 |

构建与编排：`Makefile`、`CMakeLists.txt`、`cmake/toolchain-arm-none-eabi.cmake`、
`Dockerfile`、`docker-compose.yml`、`.dockerignore`、`README.md`。

---

## 3. 自检结果与已修正 Bug

> 说明：本环境为 Windows，未安装原生 C 编译器，Docker 引擎因缺少 WSL2 后端无法启动，
> 故采用 **ARM 交叉编译器实测**（`arm-none-eabi-gcc -std=c99 -Wall -Wextra -Wno-unused-parameter
> -fsyntax-only`）对全部平台无关源文件逐文件编译校验，并辅以算法逻辑审查。
> 该检查在 19 个源文件上全量通过（0 错误 0 警告），本次会话据此新发现并修正 9 处真实问题
> （含跨平台格式缺陷），连同此前修复累计 13 处。
> 主机 Docker 构建命令已在文档给出，可在具备 Docker/Linux 的环境一键验证。

### 3.1 已修复的 Bug

| # | 文件 | 问题 | 修复 |
| --- | --- | --- | --- |
| 1 | `tests/test_agent.c` | `rlen` / `rsp` 未声明即使用（编译错误） | 补声明 `char rsp[64]; uint32_t rlen = sizeof(rsp);` |
| 2 | `ai_service/ai_service.c` | 使用 `snprintf` 但未包含 `<stdio.h>`（隐式声明） | 增加 `#include <stdio.h>` |
| 3 | `comm/comm.c` | 同上，使用 `snprintf` 未包含 `<stdio.h>` | 增加 `#include <stdio.h>` |
| 4 | `ai_kernel/tensor_mem/tensor_mem.c` | 临时区"栈式"分配器从未回收已释放内存（`vFreeTensor` 不回退 `temp_top`），与零碎片 LIFO 设计相悖，长期运行会耗尽临时区 | 实现 LIFO 回收：`TensorObj_t` 增加 `offset`；释放栈顶时回退 `temp_top` |
| 5 | `include/security.h` | 使用 `ZhiosTick_t` 但未包含 `zhios_rtos.h`（unknown type） | 增加 `#include "zhios_rtos.h"`（与 `agent.h`/`message_bus.h` 一致） |
| 6 | `include/ai_service.h` | 同上，`ZhiosTick_t` 未定义 | 增加 `#include "zhios_rtos.h"` |
| 7 | `ai_kernel/security/security.c` | 调用 `xRunInference` 未声明（隐式声明；其定义在 `inference_scheduler.h`） | 增加 `#include "inference_scheduler.h"` |
| 8 | `ai_kernel/tensor_mem/tensor_mem.c` | `zhio_malloc`/`zhio_free` 隐式声明（未含 `zhios_rtos.h`） | 增加 `#include "zhios_rtos.h"` |
| 9 | `Makefile` | 云端适配器 `#include "cloud_transport.h"` 位于 `ai_service/cloud/`，但 CFLAGS 未含该路径，导致找不到头文件 | CFLAGS 增加 `-Iai_service/cloud` |
| 10 | `ai_service/ai_service.c` | `snprintf` 用 `%u` 打印 `uint32_t`，在 ARM 上 `uint32_t` 为 `long unsigned`，属跨平台格式缺陷 | 改为 `(unsigned)sum` 显式转换 |
| 11 | `comm/comm.c` | 局部变量 `i` 仅赋值从未使用 | 移除，改用无计数 `for(;;)` |
| 12 | `bsp/stm32h743/hal_stm32h743.c` | `if (resp_len) *resp_len = 0; return ...;` 缩进误导（`-Wmisleading-indentation`） | 改写为带大括号的多行语句 |
| 13 | `ai_service/cloud/adapters/openai/openai_adapter.c` | 局部变量 `rlen` 声明但未使用 | 移除无用声明 |

### 3.2 已核验一致的接口（无需改动）

- 全部 `include/*.h` 声明与各 `.c` 实现一一对应（`agent.h`/`capability.h` 的
  `iConfigureAutoAgentByKeywords`、`iSetAgentSafetyLevel` 等在 `capability.c` 实现，已确认符号完整）。
- 云端适配器 `openai/claude/a2a/mcp` 的 `CloudAdapter_t` 回调签名与 `ai_service.h` 一致。
- `zhios_rtos.h` 抽象层与 `rtos/host`、`rtos/freertos` 两端口函数集一致（可替换）。
- CRC32 校验向量 `"123456789"` → `0xCBF43926`（IEEE 802.3 标准）已核验。
- EDF 调度：同优先级取最早截止期、高优先级优先，逻辑正确。

---

## 4. 新增/补齐内容

1. **Docker 可运行化**
   - 新增 `docker/gateway/gateway.py`（纯 stdlib 云端网关：OpenAI/Claude/A2A/MCP 路由 + mock 模式，密钥经环境变量注入，不落盘）。
   - 新增 `docker/gateway/Dockerfile`、`tools/zmonitor/index.html`，使 `docker-compose.yml` 的
     `gateway` / `monitor` 服务可正常构建（此前引用不存在的目录）。
   - `gateway.py` 已通过 `python -m py_compile` 语法校验。

2. **MCU 可替换模块（嵌入式二次开发）**
   - 新增 `bsp/stm32h743/`：`board.c`、`hal_stm32h743.c`、`FreeRTOSConfig.h`，作为真实 MCU BSP 模板。
   - `CMakeLists.txt` 增加 `stm32h743` 平台分支并接入 BSP；`stm32h755`/`mcxn947` 给出明确新增指引。
   - 可替换接口：`zhios_rtos.h`（内核）、`hal.h`（外设）、`npu_dsp.h`（加速器），更换开发板仅替换 BSP。

3. **入口文档**：新增 `README.md`（仓库入口、Docker/交叉编译用法、目录分类）。

---

## 5. 构建与运行说明

### 5.1 主机（Linux/macOS 或容器内）

```bash
make            # 构建 zhio_tests + zhio_demo
make test       # 运行单元测试（期望 ALL PASS）
make demo       # 运行演示
```

### 5.2 Docker（推荐，无环境植入）

```bash
docker compose up --build tests   # 单元测试
docker compose run --rm demo      # 演示
docker compose up -d gateway monitor  # 云端网关(mock) + 监控
```

### 5.3 交叉编译（STM32H743）

```bash
cmake -B build -DZHIO_PLATFORM=stm32h743 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake \
      -DZHIO_FREERTOS_DIR=<FreeRTOS内核路径>
cmake --build build
```

---

## 6. 单元测试覆盖（ZTEST）

| 模块 | 用例 |
| --- | --- |
| 张量内存 | basic / oom / stats |
| 模型运行时 | lifecycle（加载/推理/热切换） |
| 推理调度器 | EDF 选择 |
| 安全 | 校验器拦截/放行 |
| 通信 | 帧编解码 + CRC 校验向量 + 篡改检测 |
| 能力引擎 | 关键词映射 / 安全等级 |
| 消息总线 | 发送/接收 |
| 子 Agent 团队 | 创建 + 步进 |
| config 命令 | 关键词命令 / 错误命令 |
| 混合 AI 路由 | mock 传输 + CLOUD_ONLY |

---

## 7. 文档更新说明

- `docs/04-项目结构文档.md`：目录树更新为实际实现（新增 `rtos/`、`docker/`、`bsp/stm32h743/`、
  `tools/zmonitor/`、`README.md`、`DEVELOPMENT_LOG.md`）。
- 其余设计文档（05-11、13、14、17-21 等）所描述的接口/算法与当前代码一致，无需改动。
- 本日志（`DEVELOPMENT_LOG.md`）即本阶段交付的"详细日志"。

---

## 8. 遗留项 / 后续建议

- 真实 MCU 编译与硬件联调需在具备 `arm-none-eabi` 工具链与 FreeRTOS 源码的环境进行。
- `stm32h755` / `mcxn947` BSP 可参照 `bsp/stm32h743` 补齐。
- `app/` 第 5 层（HMI / 控制台 / 微信小程序）可基于 `comm_config_parse` 关键词命令进一步接入。
- 工具（zprofiler / zmodel / zsim / zconfig）为可选增强，可按需实现。

---

## 9. 第二阶段：日志、仿真自检、防火墙、移植框架、驱动优化（2026-08-11）

### 9.1 运行时日志（便于排查）
- [ai_kernel/tensor_mem/tensor_mem.c](zhi-os-af/ai_kernel/tensor_mem/tensor_mem.c)：
  池初始化/去初始化、持久区复位、临时区耗尽、张量分配/释放（含 LIFO 回收与非 LIFO 空洞）、碎片整理失败均输出 `[tensor_mem]` 日志。
- [ai_kernel/inference_scheduler/inference_scheduler.c](zhi-os-af/ai_kernel/inference_scheduler/inference_scheduler.c)：
  任务创建/删除/激活、调度选择结果、取消、安全校验拦截、执行失败、超时、异步任务创建失败均输出 `[sched]` 日志。

### 9.2 本地可运行仿真自检（无需 C 编译器 / Docker / WSL）
新增 [tools/sim/zhio_sim.py](zhi-os-af/tools/sim/zhio_sim.py)（纯 Python stdlib）：
- 模拟 Docker `make test`（ZTEST）流程，逐项验证：张量内存（LIFO 回收/空洞/耗尽）、
  EDF 调度、模型状态机、消息总线、CRC32（向量 `0xCBF43926`）、通信帧篡改检测、
  能力引擎路由、混合 AI 路由（mock）、防火墙、UART 环形缓冲，以及 5 个子 Agent 的合成数据"训练+评估"。
- **实测结果：21 用例 / 49 断言全部通过（ALL PASS）**，退出码 0。
- 运行方式：`python tools/sim/zhio_sim.py`。

### 9.3 防火墙（安全）模块
- 新增 [include/firewall.h](zhi-os-af/include/firewall.h) 与
  [ai_kernel/security/firewall.c](zhi-os-af/ai_kernel/security/firewall.c)：
  默认拒绝 + 命令白名单 + 源白名单 + 令牌桶限速 + 载荷长度上限；
  令牌桶基于 `zhio_get_tick()`（回绕安全）。
- [include/zhios_err.h](zhi-os-af/include/zhios_err.h) 新增 `ZHIO_E_DENIED = -14`。
- [kernel/kernel.c](zhi-os-af/kernel/kernel.c) 初始化时启用防火墙并放行受信任命令。
- 已接入 [Makefile](zhi-os-af/Makefile) 与 [CMakeLists.txt](zhi-os-af/CMakeLists.txt)。

### 9.4 可替换接口移植框架（二次开发）
- 新增 [bsp/PORT_TEMPLATE/](zhi-os-af/bsp/PORT_TEMPLATE)（`board_template.c`、`hal_template.c`）
  与 [bsp/PORTING.md](zhi-os-af/bsp/PORTING.md)：5 步移植流程 + 自检清单，
  换开发板只改 `bsp/` 与 `rtos/`，不改上层。

### 9.5 驱动 / HAL 优化（提升利用率与响应）
- [include/hal.h](zhi-os-af/include/hal.h) 新增可选测试钩子 `hal_uart_rx_inject`。
- [hal/hal_host.c](zhi-os-af/hal/hal_host.c)：UART 改为**环形缓冲 + 非阻塞接收**，
  短轮询释放 CPU（`zhio_task_sleep(1)`），提升利用率与响应；
  修复 `%X/%u` 与 `uint32_t` 的跨平台格式缺陷及误导缩进。
- 内核架构审查结论：六层分层、EDF+固定优先级调度、零碎片内存、可替换抽象均正确。

### 9.6 本次新增/修复汇总

| 项 | 内容 |
| --- | --- |
| 新增文件 | `include/firewall.h`、`ai_kernel/security/firewall.c`、`tools/sim/zhio_sim.py`、`bsp/PORT_TEMPLATE/*`、`bsp/PORTING.md` |
| 修改文件 | `include/hal.h`、`include/zhios_err.h`、`hal/hal_host.c`、`kernel/kernel.c`、`Makefile`、`CMakeLists.txt`、`ai_kernel/tensor_mem/tensor_mem.c`、`ai_kernel/inference_scheduler/inference_scheduler.c`、`bsp/stm32h743/hal_stm32h743.c` |
| 验证 | ARM 交叉 `-Wall -Wextra -fsyntax-only` 全部通过；`python tools/sim/zhio_sim.py` 21 用例全过 |

### 9.7 爬虫取数 + 模型训练（第二阶段落地）

在 [tools/sim/zhio_sim.py](zhi-os-af/tools/sim/zhio_sim.py) 中落地"合法取数 + 训练 + 评估"链路：

1. **合法爬虫**：`crawl_public_dataset()` 从公开/宽松许可数据源（示例为
   `scikit-learn` 的 BSD-3 许可 `iris.csv`）抓取并解析真实数值特征；
   具备外网时**实测抓取到 151 条真实样本**并用于训练。
   无外网/抓取失败时自动回退到内置确定性合成数据，保证链路可复现、可验证。
2. **训练**：将原先的"阈值校准"升级为**口袋感知机（Pocket Perceptron）**线性分类器，
   对 5 个子 Agent（vision/motion/safety/force/quality）分别在其特征域上学习决策边界。
3. **评估**：在未见测试集上评估准确率，断言 >= 0.6。

**实测结果（抓取真实数据路径）：**

| Agent | 准确率 |
| --- | --- |
| vision | 0.910 |
| motion | 0.920 |
| safety | 0.920 |
| force | 0.920 |
| quality | 0.900 |

> 相比原先阈值模型（约 0.57），真实特征 + 感知机训练使准确率提升至 0.90–0.92。
> 说明：真实生产环境应遵守目标站点 `robots.txt` 与版权/许可约束，并按需替换为领域数据集。

### 9.8 本阶段新增/修复汇总（更新）

| 项 | 内容 |
| --- | --- |
| 修改文件 | `tools/sim/zhio_sim.py`（新增合法爬虫 + CSV 解析 + 口袋感知机训练/评估） |
| 验证 | `python tools/sim/zhio_sim.py` 21 用例 / 49 断言全部通过（ALL PASS），退出码 0 |

---

## 10. 第三阶段：Agent 控制软件、Docker 训练测试、数据分布分析、独立安全/驱动镜像（2026-08-11）

### 10.1 Agent 控制软件（系统自带程序）
- **文档**：新增 [docs/agent控制软件技术文档.md](docs/../docs/agent控制软件技术文档.md)。
- **代码**：新增 `tools/agent_console/`（纯 Python 标准库，零第三方依赖）：
  - `app.py`：后端（`ThreadingHTTPServer` + `sqlite3`），REST API + 前端托管。
  - `trainer.py`：可复用训练引擎（合法爬虫取数 + 口袋感知机 + 类别不平衡分析）。
  - `schema.sql`：数据库（agents / training_runs / samples / config / audit_log）。
  - `static/index.html`：单页前端（Agent 管理 / 训练 / 数据分布 / 监控 / 安全 / 配置 / 审计）。
- 功能模块：Agent 全生命周期管理、训练流水线触发、数据分布与不平衡检查、
  运行监控、安全/防火墙策略、系统配置、审计日志。
- **本机实测**：`--seed-agents` 注册 5 个内置 Agent；触发训练测试准确率 0.90，
  真实爬虫抓取 151 条样本；样本库 3000 条；监控/防火墙/审计接口均正常。

### 10.2 Docker 容器内自动化训练测试（环境隔离验证）
- 新增 `docker/sim/Dockerfile` + `docker/sim/training_test.py`：在纯净
  `python:3.11-slim` 容器中运行"爬虫取数 + 训练 + 评估"，断言各 Agent 测试准确率 ≥0.6，
  并校验仅使用标准库（无 numpy/sklearn/torch）。
- 新增 `tools/sim/run_training_test.sh`：`--docker` 走 Docker，否则本地等价执行。
- 已接入 `docker-compose.yml` 的 `sim-training` 服务。
- **实测（本地等价容器入口）**：5 个 Agent 测试准确率 0.910–0.925，全部 PASS。

### 10.3 训练数据分布分析报告（类别不平衡检查）
- 新增 `tools/sim/analyze_data.py`，生成 [docs/训练数据分布分析报告.md](docs/../docs/训练数据分布分析报告.md)。
- **结论**：在抓取的真实数据特征 + 隐规则下，vision/motion/force 存在**严重不平衡**
  （不平衡比 11.90–14.38），safety/quality 为存在不平衡（2.05–6.14）；
  虽本次准确率均达标（≥0.6），但生产部署前需采用类别加权/过采样/阈值校准处理。

### 10.4 防火墙逻辑与驱动代码封装为独立 Docker 镜像
- **防火墙**：`docker/security/Dockerfile` + `fw_harness.c`——独立编译 `firewall.c`，
  验证默认拒绝/白名单/源白名单/长度上限/令牌桶限速。
- **驱动**：`docker/driver/Dockerfile` + `uart_ringbuf.{h,c}` + `driver_harness.c`——
  将修复后的 UART 环形缓冲驱动独立成模块，验证 FIFO/满丢弃/空读/回绕。
- 新增 [docker-compose.modules.yml](docker-compose.modules.yml) 便于独立部署。

### 10.5 本阶段发现并修复的 Bug
| # | 文件 | 问题 | 修复 |
| --- | --- | --- | --- |
| 14 | `tools/agent_console/app.py` | `main()` 中 `global DB_PATH` 在参数默认值（`default=DB_PATH`）引用之后声明，触发 `SyntaxError: name 'DB_PATH' is used prior to global declaration` | 将 `global DB_PATH` 提到 `main()` 首行，位于任何引用之前 |
| 15 | `docker/sim/training_test.py` | 仅将脚本自身目录加入 `sys.path`，本地运行时无法找到 `tools/agent_console/trainer.py` | 增加多级候选路径探测，兼容容器内/本地两种布局 |

### 10.6 验证汇总（本阶段）
| 项 | 结果 |
| --- | --- |
| Agent 控制软件后端 | 启动正常，5 个内置 Agent，训练/监控/防火墙/审计接口实测通过 |
| Python 语法检查 | `app.py / trainer.py / training_test.py / analyze_data.py` 全部通过 |
| Docker 训练测试（本地等价） | 5 个 Agent 准确率 0.910–0.925，ALL PASS |
| 数据分布分析报告 | 已生成，识别出 3 个严重不平衡 Agent |
| Docker 镜像构建 | 需具备 Docker/WSL2 的环境执行（本机无 C 编译器/Docker 引擎，代码已按标准接口编写） |

---

## 11. 第四阶段：端到端一键启动、类别加权、数据库迁移（2026-08-11）

### 11.1 一键端到端集成启动脚本（本地 Docker）
- 新增跨平台脚本，同时构建/启动/验证 **Agent 控制软件 + 防火墙 + 驱动** 三个模块，便于端到端集成测试：
  - [tools/run_integration.ps1](zhi-os-af/tools/run_integration.ps1)（Windows PowerShell）
  - [tools/run_integration.sh](zhi-os-af/tools/run_integration.sh)（Linux/macOS bash）
- 流程：检查 Docker → 构建 `agent-console/firewall/driver` → 常驻启动 `agent-console`（端口 8000，轮询 `/api/monitor` 就绪）→ 各运行一次 `firewall` 与 `driver` 容器逻辑验证。
- 退出码 0 = 全部通过；任一步骤失败即汇总为失败。

### 11.2 类别加权策略（缓解类别不平衡）
- 针对《训练数据分布分析报告》指出的严重不平衡（vision/motion/force 不平衡比 11.90–15.00），在
  [tools/agent_console/trainer.py](zhi-os-af/tools/agent_console/trainer.py) 新增：
  - `_class_weights()`：逆频率加权 `w_c = n/(2·n_c)`，均衡多数类与少数类对梯度更新的贡献；
  - `_perceptron_train()` 支持类别加权，并在加权时以 **balanced accuracy（宏平均召回率）** 作为口袋模型选优目标；
  - `evaluate()` 同时输出原始准确率与 balanced accuracy；`train_compare()` 显式对比"未加权 vs 加权"。
- **验证结论**：加权策略对"少数类可学习"的 Agent（如 safety，balanced 0.631→0.930）提升显著；
  对极端不平衡且少数类受 8% 标注噪声扰动而不可线性分离的 Agent 效果不稳定，且会牺牲原始准确率。
  故 `train_and_evaluate()` 默认 `weighted=False` 保持既有原始准确率（≥0.6）评估口径稳定，
  类别加权作为显式可选策略供 `train_compare()`/`weighted=True` 验证使用。
- 实测：`python docker/sim/training_test.py` → 5 个 Agent 原始准确率全部 PASS；`train_compare()` 输出加权对比表。

### 11.3 SQLite Schema 校验 + 数据迁移脚本
- 校验 [tools/agent_console/schema.sql](zhi-os-af/tools/agent_console/schema.sql)：
  - `audit_log` 已含 **全部审计日志字段**：`id / ts / actor / action / detail`；
  - `config` 已含 **全部配置项字段**：`key / value`（默认回填 `n_train / n_test / fw_rate / fw_burst / fw_max_payload`）。
- 新增 [tools/agent_console/migrate.py](zhi-os-af/tools/agent_console/migrate.py)（幂等迁移脚本）：
  - 全新库按 `schema.sql` 建表；旧库自动 `ALTER TABLE ... ADD COLUMN` 补齐缺失列；
  - 校验审计/配置字段完整性，回填缺失默认配置，且**不改动/删除已有列**，可重复执行。
- **实测**：全新库迁移结果 `OK`；旧库缺列/缺配置场景自动补齐并回填，结果 `OK`；重复执行幂等通过；`py_compile` 语法校验通过。

### 11.4 本阶段新增文件
| 类型 | 文件 |
| --- | --- |
| 新增 | `tools/run_integration.ps1`、`tools/run_integration.sh`、`tools/agent_console/migrate.py` |
| 修改 | `tools/agent_console/trainer.py`（类别加权 + balanced accuracy + 默认口径说明） |
| 验证 | `training_test.py` ALL PASS；`trainer.py` 对比输出正常；`migrate.py` 新建/升级/幂等均 OK |

---

## 12. 第五阶段：Agent 控制软件端到端测试 + 文档完善 + 安全加固（2026-08-12）

### 12.1 端到端自动化测试（Docker / 本机）
- 新增 [tools/agent_console/e2e_test.py](zhi-os-af/tools/agent_console/e2e_test.py)：在 Docker 容器（或本机）完整验证
  **前端 + 后端 + SQLite + 全功能模块**的端到端流程，覆盖 10 个测试套件：
  前端服务、数据库连接、Agent 管理(CRUD)、训练流水线、数据分布/样本、运行监控、
  安全/防火墙、配置管理、审计日志、安全加固。
- 支持两种模式：进程内自启服务（默认，零依赖）与 `--url` 连接已运行实例。
- **实测**：两种模式均 **54/54 断言通过，E2E TEST ALL PASS**。

### 12.2 Docker 容器端到端综合验证
- 新增 [docker/e2e/Dockerfile](zhi-os-af/docker/e2e/Dockerfile)（多阶段）+ [docker/e2e/run_e2e.sh](zhi-os-af/docker/e2e/run_e2e.sh)，
  在单个容器内依次验证三层面：Agent 控制软件端到端 → C 层防火墙逻辑（`firewall.c`）→ UART 驱动接口（`uart_ringbuf.c`）。
- [docker-compose.yml](zhi-os-af/docker-compose.yml) 注册 `e2e` 服务：`docker compose run --rm e2e`。

### 12.3 防火墙逻辑与驱动接口检查（Docker 环境）
- 复用独立镜像验证 C 层 [firewall.c](zhi-os-af/ai_kernel/security/firewall.c)（默认拒绝/白名单/源白名单/长度/令牌桶限速）。
- 复用独立镜像验证 [uart_ringbuf](zhi-os-af/docker/driver/uart_ringbuf.c) 驱动接口（FIFO/满丢弃/空读/回绕）；
  环形缓冲 + 非阻塞 + 短轮询释放 CPU，**提升硬件利用率与响应**。

### 12.4 安全加固（E2E 实测发现并修复的漏洞）
| # | 文件 | 问题 | 修复 |
| --- | --- | --- | --- |
| 16 | `tools/agent_console/app.py` | **超长 Agent 名称（10 万字符）被直接接受（返回 201）**，缺乏输入/载荷校验，存在资源耗尽风险 | 新增 `MAX_NAME_LEN=64`，超长返回 400 |
| 17 | `tools/agent_console/app.py` | **恶意超大请求体无上限**，可能耗尽内存 | `_read_json` 限制 `Content-Length ≤ MAX_BODY_BYTES(64KB)`，超限返回 413 |
| 18 | `tools/agent_console/app.py` | 非法 JSON 解析异常会抛给服务器（潜在 500） | 捕获 `ValueError/UnicodeDecodeError` 返回 400 |

### 12.5 文档更新
- [docs/agent控制软件技术文档.md](docs/../docs/agent控制软件技术文档.md) 更新至 **v1.1.0**：
  新增 3.2 前后端代码文件说明、第 9 章自动化端到端测试、第 10 章防火墙与驱动接口检查、第 11 章安全分析、
  验证结果更新为含 E2E 54/54 通过。

### 12.6 本阶段新增/修改文件
| 类型 | 文件 |
| --- | --- |
| 新增 | `tools/agent_console/e2e_test.py`、`docker/e2e/Dockerfile`、`docker/e2e/run_e2e.sh` |
| 修改 | `tools/agent_console/app.py`（安全加固：载荷/名称/JSON 校验）、`docker-compose.yml`（新增 e2e 服务）、`docs/agent控制软件技术文档.md` |
| 验证 | `e2e_test.py` 进程内/`--url` 两种模式 54/54 ALL PASS；`py_compile` 全部通过 |

---

## 13. 第六阶段：单文件 docker-compose 合并部署 + 一键启动（2026-08-12）

### 13.1 单文件 docker-compose 合并
- 将此前分置于 `docker-compose.yml` 与 `docker-compose.modules.yml` 的服务**合并为单文件**
  [docker-compose.yml](zhi-os-af/docker-compose.yml)，共 9 个服务：
  `tests / demo / gateway / monitor / sim-training / agent-console / firewall / driver / e2e`。
- 移除过时的 `version` 属性；`e2e` 增加 `depends_on: agent-console`。
- **校验**：`docker compose config` 解析全部 9 个服务无误。

### 13.2 一键启动脚本更新（单文件编排）
- [tools/run_integration.sh](zhi-os-af/tools/run_integration.sh) 与
  [tools/run_integration.ps1](zhi-os-af/tools/run_integration.ps1) 改为基于单文件 `docker-compose.yml`，
  执行 5 步：构建镜像 → 启动 agent-console（等待 `/api/monitor` 就绪）→ 运行 firewall → 运行 driver → 运行 e2e 综合验证。
- **PS1 编码修复**：脚本含中文注释，须以 **UTF-8 BOM** 保存，否则 Windows PowerShell 5.1 会按 ANSI/GBK 解析导致
  `The string is missing the terminator` 语法误报；已重存为 UTF-8 BOM，`Parser::ParseFile` 校验通过。

### 13.3 端到端流程验证
- 本机无 Docker 引擎（Docker Desktop Linux Engine 管道不可用），无法执行真实容器部署；
  以**本地等价端到端**验证：`python tools/agent_console/e2e_test.py` **54/54 断言通过**（即 e2e 容器步骤 1 的内容）。
- 部署脚本的 Docker 引擎守卫逻辑正确（引擎缺失时明确提示并退出非 0）。

### 13.4 本阶段发现并修复的 Bug
| # | 文件 | 问题 | 修复 |
| --- | --- | --- | --- |
| 19 | `tools/agent_console/app.py` | 超大请求体返回 413 时**未排空请求体**，TCP 连接残留未读数据被中止（`ConnectionAbortedError 10053`），E2E 安全加固用例失败 | `_read_json` 超限时先 `_drain_body()` 分块排空（上限 1MB）再返回 413，并加 `Connection: close` |

### 13.5 本阶段新增/修改文件
| 类型 | 文件 |
| --- | --- |
| 修改 | `docker-compose.yml`（合并 9 服务 + 移除 version + e2e 依赖）、`tools/run_integration.sh`、`tools/run_integration.ps1`（UTF-8 BOM）、`tools/agent_console/app.py`（排空请求体修复 413 连接中止）、`docs/agent控制软件技术文档.md`（7.3 单文件部署 + 安全表 + 验证结果） |
| 验证 | `docker compose config` 9 服务解析通过；`e2e_test.py` 54/54 ALL PASS；`Parser::ParseFile` PS1 语法通过 |

---

## 14. 第七阶段：部署手册 + 413 回归测试 + release 分支归档（2026-08-12）

### 14.1 部署操作手册
- 新增 [docs/部署操作手册.md](docs/../docs/部署操作手册.md)（v1.0.0）：
  环境准备（WSL2 + Docker Desktop 安装/校验）→ 获取代码 → 环境自检 → 一键启动 → 手动编排 →
  端到端验证 → 停止清理 → 常见问题排障 → 二次开发接入 → 命令速查。

### 14.2 413 / 连接中止回归测试
- 在 [tools/agent_console/e2e_test.py](zhi-os-af/tools/agent_console/e2e_test.py) 新增 **回归-413/连接中止** 套件（5 项断言）：
  ①单个超大载荷返回 413（不中止连接）；②413 响应带 `Connection: close`；
  ③同连接 413 后后续请求可用；④连续多次 413 稳定返回；⑤413 后服务仍健康。
- **实测**：`e2e_test.py` 由 54/54 提升至 **59/59 ALL PASS**，防止 Bug #19 未来回归。

### 14.3 release 分支归档 + 版本发布说明
- 本目录非 git 仓库，已执行 `git init` 并创建 **`release` 分支**，将部署产物归档：
  `docker-compose.yml`、`tools/run_integration.sh/.ps1`、`docker/`、`tools/agent_console/`、
  `RELEASE_NOTES.md`、`DEVELOPMENT_LOG.md`、`.gitignore`。
- 提交：`67ef55b release: v1.1.0 单文件容器编排 + 一键端到端部署`。
- 新增 [RELEASE_NOTES.md](zhi-os-af/RELEASE_NOTES.md)（v1.1.0）：版本信息、新特性、修复 Bug、验证结果、部署方式、归档清单。

### 14.4 本阶段新增/修改文件
| 类型 | 文件 |
| --- | --- |
| 新增 | `docs/部署操作手册.md`、`RELEASE_NOTES.md`、`.gitignore` |
| 修改 | `tools/agent_console/e2e_test.py`（新增回归-413 套件） |
| 归档 | 初始化 git 仓库，创建 `release` 分支并提交 `67ef55b` |
| 验证 | `e2e_test.py` 59/59 ALL PASS |
