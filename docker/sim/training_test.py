#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
training_test.py - Docker 容器内自动化训练测试（环境隔离验证）

在纯净容器中完整运行"合法爬虫取数 + 口袋感知机训练 + 评估"链路，
验证在环境隔离（无宿主依赖、无本地 Python 包、仅标准库）下的执行结果。

- 通过断言校验各 Agent 测试准确率 >= 0.6。
- 退出码 0 = 全部通过；非 0 = 失败（供 CI/`docker compose` 判定）。
"""
import json
import os
import sys

# 兼容两种定位：容器内 trainer.py 与训练脚本同目录；本地则位于 tools/agent_console
_HERE = os.path.dirname(os.path.abspath(__file__))
for _cand in (_HERE, os.path.join(_HERE, "..", "..", "tools", "agent_console"),
              os.path.join(_HERE, "..", "..", "..", "tools", "agent_console")):
    _cand = os.path.abspath(_cand)
    if os.path.isfile(os.path.join(_cand, "trainer.py")):
        sys.path.insert(0, _cand)
        break
import trainer

TARGET = 0.6


def main():
    print("=" * 64)
    print("ZhiOS-AF 训练链路 · Docker 隔离环境自动化测试")
    print("=" * 64)
    print(f"[env] python={sys.version.split()[0]}")
    print(f"[env] cwd={os.getcwd()}")

    n_train = int(os.environ.get("N_TRAIN", 400))
    n_test = int(os.environ.get("N_TEST", 200))

    res = trainer.train_and_evaluate(n_train, n_test)
    print(f"[data] 来源: {res['source']}")
    print(f"[data] 抓取样本: {len(res['crawled_rows'])} 条")

    failures = 0
    print("\n[result]")
    for name in trainer.AGENTS:
        acc = res["agents"][name]["test_acc"]
        ir = res["imbalance"]["summary"][name]["imbalance_ratio"]
        flag = "PASS" if acc >= TARGET else "FAIL"
        if acc < TARGET:
            failures += 1
        print(f"  {name:<8} test_acc={acc:.3f} train_acc="
              f"{res['agents'][name]['train_acc']:.3f} "
              f"imbalance={ir:.2f}  [{flag}]")

    # 隔离性自检：确认未引入第三方 ML 依赖（仅标准库可运行）
    for mod in ("numpy", "sklearn", "torch"):
        if mod in sys.modules:
            print(f"[warn] 检测到第三方依赖 {mod}（应仅使用标准库）")

    print("-" * 64)
    if failures == 0:
        print("==> TRAINING TEST ALL PASS")
        return 0
    print(f"==> {failures} agent(s) FAILED (need >= {TARGET})")
    return 1


if __name__ == "__main__":
    sys.exit(main())
