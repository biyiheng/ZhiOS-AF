# ZhiOS-AF Release Notes

> 版本：**v1.1.0** ｜ 分支：`release` ｜ 日期：2026-08-12
> 本版本聚焦：**单文件容器编排 + 一键端到端部署 + 安全加固与回归保障**

---

## 1. 版本信息

| 项 | 值 |
| --- | --- |
| 版本号 | v1.1.0 |
| 分支 | `release` |
| 发布日期 | 2026-08-12 |
| 架构 | ZhiOS-AF（Agent-First 嵌入式实时操作系统） |
| 编排 | 单文件 `docker-compose.yml`（9 个服务） |
| 一键脚本 | `tools/run_integration.sh` / `tools/run_integration.ps1` |

---

## 2. 本版本新特性 / 变更

### 2.1 单文件容器编排（合并）
- 将原先分置于 `docker-compose.yml` 与 `docker-compose.modules.yml` 的服务合并为**单文件**编排，共 9 个服务：
  `tests / demo / gateway / monitor / sim-training / agent-console / firewall / driver / e2e`。
- 移除过时的 `version` 属性；`e2e` 服务增加 `depends_on: agent-console` 保证启动顺序。

### 2.2 一键端到端部署
- 新增/更新一键启动脚本（Linux/macOS 与 Windows），自动执行 5 步：
  构建镜像 → 启动 Agent 控制软件（等待就绪）→ 验证防火墙 → 验证驱动 → 端到端综合验证。
- 新增《部署操作手册.md》，覆盖环境准备（WSL2 + Docker Desktop）→ 获取代码 → 一键启动 → 验证 → 排障。

### 2.3 端到端自动化测试
- 新增 `tools/agent_console/e2e_test.py`（12 套件，**66 项断言**）：
  前端 / 数据库 / Agent 管理 / 训练 / 数据分布 / 监控 / 防火墙 / 配置 / 审计 / 安全加固 / 413 连接中止回归 / **413 压力测试**。
- 新增 `docker/e2e/` 多阶段镜像：单容器内依次验证控制软件端到端 + C 层防火墙 + UART 驱动。

### 2.4 安全加固
- 限制请求体大小（`MAX_BODY_BYTES=64KB`，超限返回 413）。
- 限制 Agent 名称长度（`MAX_NAME_LEN=64`，超限返回 400）。
- 非法 JSON 返回 400（不触发 500）。
- **修复超大载荷连接中止**（Bug #19）：返回 413 前先排空请求体并带 `Connection: close`，避免 TCP 连接被中止。
- 增加 **413/连接中止回归测试套件**，防止未来回归。

### 2.5 压力测试 + WSL2 一键部署 + 可部署性自检
- 新增 **413/连接中止压力测试套件**（`test_stress_413`）：高频 30 次、并发 8 路、边界体长（略超 64KB->413、体长合法名称超长->400）、混合负载、同连接交替、压力后健康，共 7 项断言。
- 新增 `tools/run_wsl_deploy.sh`：WSL2 环境一键部署（环境校验 / 可选 clean clone release 分支 / `--keep` 常驻）。
- 新增 `tools/verify_release.py`：无需 Docker 引擎静态校验 release 分支可部署性（Dockerfile COPY 源 / 构建上下文 / 挂载 / 关键文件）。

### 2.6 嵌入式 OS 技术指标体系 + 架构审核 + 模块化注释
- 新增《33-操作系统技术指标体系设计文档》：六大维度（内核调度/内存管理/中断与异常/设备驱动/功耗管理/启动流程）+ 硬性门槛 + 当前实现对照 + 差距分析 + 落实路径。
- 新增汇编级参考模板：`bsp/stm32h743/startup_stm32h743.s`（向量表/复位/故障转储）、`bsp/stm32h743/port_context_switch.s`（PendSV 切换/LDREX-STREX/位带/WFI 低功耗）。
- 对核心内核/HAL/BSP/RTOS 端口补充统一「模块说明（维护入口）」注释；调度器新增 `ZHIO_CFG_SCHED_TRACE` 调度决策/抖动周期测量。

### 2.7 arm-none-eabi 链接验证 + QEMU 亚微秒切换/Jitter 压测 + GitHub 上传准备
- 新增 `tools/arm_eabi/`：`verify_flash.ld`（向量表@0/DWT CYCCNT 钉址）、`verify_main.c`（上下文切换周期采样固件）、`build_verify.sh`/`.ps1`（自动探测 STM32CubeIDE 内嵌工具链）、`stress_sched.py`（自动化压测）。
- **链接验证**：用真实 `arm-none-eabi-gcc` 对 `startup_stm32h743.s` / `port_context_switch.s` 汇编并链接为 ELF，向量表 `0x00000000`=栈顶、`0x04`=Reset_Handler、PendSV/原子符号齐全（本机实测通过）。
- **自动化压测**：`stress_sched.py` 在 QEMU（mps2-an385）多轮采样上下文切换周期，统计 mean/max/jitter，强制判定 **<1μs 切换** 与 **jitter 占比** 门槛；真板可用 J-Link/GDB/串口接入同一判定。
- **trace 宏统一**：调度器 `ZHIO_CFG_SCHED_TRACE` 之外，为 `tensor_mem.c` 新增 `ZHIO_CFG_MEM_TRACE` 分配周期测量；`kernel.c`/`npu_dsp.c`/`message_bus.c`/host RTOS 端口补齐「模块说明（维护入口）」头块。
- **GitHub 上传准备**：新增 `LICENSE`(Apache-2.0)、`CONTRIBUTING.md`、`SECURITY.md`、`.gitattributes`、`.github/workflows/ci.yml`（build/test/sim/e2e/cppcheck + arm-none-eabi 链接验证 + QEMU 压测）。
- 新增《34-AI-Agent技术指标体系设计文档》：五大维度（硬件亲和性/代码生成质量/诊断精度/工具链协同/安全合规）+ 硬性门槛 + 自检对照 + 落实路径。

### 2.8 模拟效率 + 线程流/进程卡死自愈 + 低内存稳定运行优化
- **模拟运行效率**：`zhio_sim.py` 新增 `--offline` / `ZHIO_SIM_OFFLINE=1` 显式离线开关与快速 TCP 可达性探测，
  离线/无外网时**零网络阻塞**立即回退内置合成数据（原离线需等 5s 超时）；消息总线改用 `deque`，队首出队 O(n)→**O(1)**；
  修复 `_feature_sample` 死代码；`main` 输出墙钟耗时。实测 24 用例 / 54 断言 ALL PASS，总耗时 32ms。
- **进程卡死自愈（AI 辅助决策）**：新增 `iAgentSupervise()`，周期巡检全部 Agent，对看门狗超时（卡死）的 Agent
  **自动复位为 READY 并清零看门狗**、输出恢复日志，杜绝单个卡死进程阻塞调度。
- **线程流/处理能力限流**：新增 `ZHIO_CFG_MAX_ASYNC_INFERENCE`（默认 8）异步推理并发闸门，超过在途上限返回 `ZHIO_E_BUSY`，
  防止低配置下并发线程/内存爆炸；新增 `xInferenceSchedulerAudit()` 供 AI 决策与性能根因分析。
- **低内存稳定运行**：新增 `ZHIO_CFG_LOW_MEMORY` 低内存配置档案，一键压缩任务/Agent/队列/张量池（96KB→32KB）等容量；
  栈尺寸 `ZHIO_CFG_AUTO_AGENT_STACK`/`ZHIO_CFG_SUBAGENT_STACK` 改为可配置；默认与低内存两种配置交叉编译全部通过。

### 2.9 GitHub Actions CI 稳定化 + release → main 合并
- **CI 构建修复**：`Makefile` 增加 `-Itools/ztest` 头文件路径，解决 `ztest.h: No such file or directory`；`build_verify.sh` 改为 `bash` 显式调用并 `chmod +x`，解决权限拒绝。
- **CI 单元测试修复**：`comm.c` 修正帧解码 CRC 计算长度（LEN 2B + TYPE 1B + 载荷），修复 `ZHIO_E_UNKNOWN`；`agent.c` 新增 `vAgentResetAll()` 槽位池复位以隔离测试（修复子 Agent 团队 `ZHIO_E_NOMEM`）。
- **CI ARM 验证修复**：`build_verify.sh` 改用 `command -v` 解析 `arm-none-eabi-*` 绝对路径，规避工具链相对路径找不到；`verify_main.c` 结尾增加半主机 `SYS_EXIT`（`bkpt 0xAB`），`stress_sched.py` QEMU 命令行加 `-semihosting-config target=native`，解决压测超时崩溃。
- **CI 仿真确定性修复**：`zhio_sim.py` 的 `agents` 用例固定随机种子并强制内置合成数据，保证训练/评估结果可复现（准确率稳定 ≥ 0.6），消除 CI 抖动。
- **CI 结果**：`build-test` 任务（构建 / ZTEST 单元测试 / cppcheck / Python 仿真 / Agent E2E / 可部署性自检）全部通过；`arm-verify` 链接验证通过，QEMU Jitter 压测为长耗时步骤（见已知限制）。
- **分支合并**：`release` 完整快照已合并至 `main`（`--allow-unrelated-histories`，统一采用 Apache-2.0 许可），main 与 release 内容一致。

---

## 3. 本版本修复的 Bug

| # | 文件 | 问题 | 修复 |
| --- | --- | --- | --- |
| 14 | `tools/agent_console/app.py` | `global DB_PATH` 在参数默认值引用之后声明，`SyntaxError` | `global` 提前到 `main()` 首行 |
| 15 | `docker/sim/training_test.py` | 本地运行时找不到 `trainer.py` | 增加多级候选路径探测 |
| 16 | `tools/agent_console/app.py` | 超长 Agent 名称（10 万字符）被接受（201） | 新增 `MAX_NAME_LEN=64`，超限返回 400 |
| 17 | `tools/agent_console/app.py` | 超大请求体无上限 | 限制 `Content-Length ≤ 64KB`，超限返回 413 |
| 18 | `tools/agent_console/app.py` | 非法 JSON 抛异常（潜在 500） | 捕获后返回 400 |
| 19 | `tools/agent_console/app.py` | 413 返回时未排空请求体，连接被中止（`ConnectionAbortedError 10053`） | 先 `_drain_body()` 排空 + `Connection: close` |

---

## 4. 测试与验证结果

| 项 | 结果 |
| --- | --- |
| `docker compose config` | 9 个服务解析通过 |
| `tools/agent_console/e2e_test.py` | **66/66 断言通过（ALL PASS，含回归 + 压力套件）** |
| `tools/verify_release.py` | **71 处引用无缺失，RELEASE VERIFY ALL PASS** |
| 部署脚本语法 | `run_integration.sh` / `run_integration.ps1` / `run_wsl_deploy.sh` 校验通过 |
| 数据库迁移 `migrate.py` | 新建 / 升级 / 幂等均 OK |
| 训练链路 | 5 个 Agent 测试准确率 0.90+（真实爬虫数据） |
| `arm-none-eabi` 汇编链接验证 | `build_verify.sh` M7/M3 链接通过，向量表/符号核验正确 |
| `tools/arm_eabi/stress_sched.py` | 语法/参数校验通过；本机无 QEMU 时正确报错并给出引导 |
| GitHub Actions `build-test` | 构建 / ZTEST 单元测试 / cppcheck / Python 仿真 / Agent E2E / 可部署性自检 **全部通过** |
| GitHub Actions `arm-verify` | M7/M3 链接验证通过；QEMU Jitter 压测为长耗时步骤（见已知限制） |

> 说明：本机（Windows）未运行 Docker 引擎（WSL2 后端缺失），真实容器部署需在具备 Docker 的 WSL2/Linux 环境
> 通过 `tools/run_wsl_deploy.sh` 或 `tools/run_integration.sh` 执行；端到端 + 压力流程已通过本地等价 `e2e_test.py` 验证。

---

## 5. 部署方式

```bash
# 一键启动（Linux/macOS）
bash tools/run_integration.sh
# 一键启动（Windows PowerShell）
powershell -ExecutionPolicy Bypass -File tools/run_integration.ps1

# 或手动编排
docker compose up -d agent-console
docker compose run --rm e2e
```

详见《部署操作手册.md》第 5 节。

---

## 6. 归档文件清单（release 分支）

> release 分支为**完整可独立部署快照**：除编排/脚本外，已纳入六层架构完整源码与根构建文件
> （`Dockerfile`/`Makefile`/`CMakeLists.txt`/`cmake/`），确保从该分支全新克隆即可执行 `docker compose build` 与一键启动。

| 文件 / 目录 | 说明 |
| --- | --- |
| `docker-compose.yml` | 单文件容器编排（9 服务） |
| `tools/run_integration.sh` / `.ps1` | 一键启动脚本 |
| `docker/` | 各模块 Dockerfile 与 harness |
| `ai_kernel/security/firewall.c` + `include/` | firewall / e2e 服务构建所依赖的 C 源码与头文件 |
| `Dockerfile` / `Makefile` / `CMakeLists.txt` / `cmake/` | tests / demo 服务构建依赖 |
| `tools/zmonitor/` | monitor 服务挂载目录 |
| `agent/ ai_kernel/ ai_service/ bsp/ capability/ comm/ hal/ kernel/ model_runtime/ rtos/ tests/ examples/` | 六层架构完整源码 |
| `docs/部署操作手册.md` | 部署操作手册 |
| `docs/agent控制软件技术文档.md` | Agent 控制软件技术文档 v1.1.0 |
| `RELEASE_NOTES.md` / `DEVELOPMENT_LOG.md` | 发布说明 / 开发日志 |

> 归档补充：v1.1.0 初始归档仅含编排与脚本，缺少 firewall/e2e 依赖的 C 源码与 `tools/zmonitor/`
> 等，无法独立部署；已补齐完整源码（提交 `1313a94`）使其自洽。

---

## 7. 已知限制 / 后续规划

- 真实容器部署与跨架构（ARM MCU）编译需在具备 Docker 与交叉工具链的环境进行。
- `stm32h755` / `mcxn947` BSP 可参照 `bsp/stm32h743` 补齐。
- 云端适配器密钥通过环境变量注入，生产环境需按安全规范配置。
- **CI 长耗时项（优化中）**：`arm-verify` 中的 QEMU Jitter 压测步骤单次运行耗时过长（受固件 UART 轮询写
  与 QEMU 解释执行影响），后续将通过压缩采样次数/收紧单轮超时/改用真实开发板等方式缩短 CI 时长。

---

*ZhiOS-AF 项目组 ｜ v1.1.0 发布说明*
