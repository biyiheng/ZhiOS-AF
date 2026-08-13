# 为 ZhiOS-AF 项目做贡献

欢迎为 ZhiOS-AF 贡献力量！请遵循本指南，确保协作顺畅、代码可控、可维护。

## 目录
- [代码规范](#代码规范)
- [开发流程](#开发流程)
- [提交信息规范](#提交信息规范)
- [分支与发布](#分支与发布)
- [测试要求](#测试要求)
- [安全问题](#安全问题)

## 代码规范
- 语言：C99（嵌入式约束），必要时 C11。
- 编码：UTF-8，LF 换行，缩进 4 空格（不用 Tab）。
- 命名：文件/函数小写下划线；结构体类型大驼峰并以 `_t` 结尾；
  公共 API 采用 FreeRTOS 风格返回类型前缀（`x`/`i`/`v`/`u`）。
- 头文件使用 include guard；公共接口使用 Doxygen 风格注释。
- 关键模块需带「模块说明（维护入口）」头块（职责/依赖/被谁调用/对应指标）。
- 硬实时关键路径禁止运行时动态大块堆分配；不引入硬编码密钥/凭据。

## 开发流程
1. 先 fork 本仓库并基于最新 `main` 创建特性分支。
2. 在独立目录自包含实现，通过公共头文件与上层解耦。
3. 换开发板/内核/加速器只改可替换模块（`bsp/`、`rtos/`、`ai_kernel/npu_dsp/`）。
4. 提交前自检：`make test`、`python3 tools/sim/zhio_sim.py`、
   `python3 tools/agent_console/e2e_test.py`、`python3 tools/verify_release.py` 全部通过。
5. 涉及汇编/调度的改动，运行 `tools/arm_eabi/build_verify.sh` 与
   `python3 tools/arm_eabi/stress_sched.py` 验证链接与抖动指标。
6. 发起 Pull Request 到 `main`，说明改动动机、验证结果与对指标的影响。

## 提交信息规范
采用 Conventional Commits 风格：
- `feat:` 新功能　`fix:` 缺陷修复　`docs:` 文档　`test:` 测试
- `refactor:` 重构　`perf:` 性能　`ci:` CI　`chore:` 杂项
示例：`fix(sched): 修复同优先级 EDF 决胜在 deadline 回绕时的选择错误`

## 分支与发布
- `main`：开发主线。
- `release`：可独立部署的归档分支（含单文件 `docker-compose.yml` 与完整源码快照）。
- 发布前在 `release` 分支通过 `tools/verify_release.py` 的完整可部署性自检。

## 测试要求
- 新增功能优先在 `tests/` 或 `tools/sim/zhio_sim.py` 补充用例。
- 安全/边界修复（如 413、连接中止）需在 `tools/agent_console/e2e_test.py`
  补充回归与压力用例。
- 性能敏感改动使用 `ZHIO_CFG_SCHED_TRACE` / `ZHIO_CFG_MEM_TRACE` 验证抖动/延迟。

## 安全问题
- 发现安全漏洞请**不要**公开提交 issue，直接联系维护者或提交到私有渠道。
- 漏洞处置遵循 SECURITY.md 的披露流程。
