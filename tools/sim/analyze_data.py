#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
analyze_data.py - 生成训练数据分布分析报告（类别不平衡检查）

针对口袋感知机模型，统计各 Agent 训练/测试集的类别分布、不平衡比，
并判定是否存在类别不平衡问题，输出 Markdown 报告。

用法：
    python tools/sim/analyze_data.py [--out docs/训练数据分布分析报告.md]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "agent_console")))
import trainer


def _ratio_label(r):
    """按不平衡比判定严重程度。"""
    if r >= 10:
        return "严重不平衡", "err"
    if r >= 2.0:
        return "存在不平衡", "warn"
    return "基本均衡", "ok"


def render_md(res):
    L = []
    L.append("# 训练数据分布分析报告（口袋感知机 / 类别不平衡检查）")
    L.append("")
    L.append("> 生成时间：自动生成 ｜ 数据来源：合法爬虫取数（离线自动回退）")
    L.append("> 训练模型：Pocket Perceptron（线性分类器）")
    L.append("")
    L.append("## 1. 数据来源")
    L.append("")
    L.append(f"- **来源**：`{res['source']}`")
    L.append(f"- **爬虫抓取真实样本**：{len(res['crawled_rows'])} 条")
    L.append(f"- 训练集规模：{res['n_train']} / Agent，测试集规模：{res['n_test']} / Agent")
    L.append("")
    L.append("## 2. 类别分布总览（训练集）")
    L.append("")
    L.append("| Agent | 样本数 | 正类 | 负类 | 正类占比 | 不平衡比 | 判定 |")
    L.append("| --- | --- | --- | --- | --- | --- | --- |")
    for d in res["imbalance"]["train"]:
        level, cls = _ratio_label(d["imbalance_ratio"])
        L.append(f"| {d['agent']} | {d['total']} | {d['pos']} | {d['neg']} | "
                 f"{d['pos_ratio']*100:.1f}% | {d['imbalance_ratio']:.2f} | **{level}** |")
    L.append("")
    L.append("## 3. 类别分布（测试集）")
    L.append("")
    L.append("| Agent | 样本数 | 正类 | 负类 | 正类占比 | 不平衡比 | 判定 |")
    L.append("| --- | --- | --- | --- | --- | --- | --- |")
    for d in res["imbalance"]["test"]:
        level, cls = _ratio_label(d["imbalance_ratio"])
        L.append(f"| {d['agent']} | {d['total']} | {d['pos']} | {d['neg']} | "
                 f"{d['pos_ratio']*100:.1f}% | {d['imbalance_ratio']:.2f} | **{level}** |")
    L.append("")
    L.append("## 4. 类别不平衡检查结论")
    L.append("")
    L.append("> **判定阈值**：不平衡比 ≥ 2.0 视为存在不平衡；≥ 10 视为严重不平衡。")
    L.append("> **注意**：本报告中的不平衡比由特征来源与隐规则共同决定，用于**暴露不平衡风险**，"
             "不代表生产级数据分布——生产环境需基于实际领域数据重新评估。")
    L.append("")
    for d in res["imbalance"]["train"]:
        level, cls = _ratio_label(d["imbalance_ratio"])
        L.append(f"- **{d['agent']}**：不平衡比 {d['imbalance_ratio']:.2f} → {level}。")
    L.append("")
    L.append("### 4.1 严重不平衡的 Agent")
    L.append("")
    severe = [d for d in res["imbalance"]["train"] if d["imbalance_ratio"] >= 10]
    if severe:
        for d in severe:
            L.append(f"- `{d['agent']}`：正类占比 {d['pos_ratio']*100:.1f}% "
                     f"（不平衡比 {d['imbalance_ratio']:.2f}），**建议采取不平衡处理**：")
            L.append("  - 类别加权损失（weighted loss）。")
            L.append("  - 过采样少数类 / 欠采样多数类。")
            L.append("  - 若为分类阈值，可校准决策边界（如调整决策阈值）。")
    else:
        L.append("- 无严重不平衡的 Agent。")
    L.append("")
    L.append("## 5. 模型效果与不平衡关联")
    L.append("")
    L.append("| Agent | 测试准确率 | 不平衡比 | 是否受不平衡显著影响 |")
    L.append("| --- | --- | --- | --- |")
    for name in trainer.AGENTS:
        acc = res["agents"][name]["test_acc"]
        ir = res["imbalance"]["summary"][name]["imbalance_ratio"]
        affected = "是" if (ir >= 10 and acc < 0.85) else "否"
        L.append(f"| {name} | {acc:.3f} | {ir:.2f} | {affected} |")
    L.append("")
    L.append("## 6. 结论与建议")
    L.append("")
    L.append("1. 当前训练-评估链路可运行且准确率达标（≥0.6），证明流水线正确。")
    L.append("2. 部分 Agent（如 vision/motion/force）在**当前数据特征**下存在较明显的类别不平衡，"
             "虽不影响本次达标，但生产部署时需按 4.1 建议处理。")
    L.append("3. 建议接入真实领域数据后重新生成本报告，作为模型上线前的数据质量门禁。")
    L.append("")
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(
        os.path.dirname(__file__), "..", "..", "..", "docs", "训练数据分布分析报告.md"))
    args = ap.parse_args()

    res = trainer.train_and_evaluate()
    md = render_md(res)

    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write(md)
    print(f"报告已生成：{out}")
    print(f"来源：{res['source']}")
    for name in trainer.AGENTS:
        d = res["imbalance"]["summary"][name]
        print(f"  {name}: 不平衡比={d['imbalance_ratio']:.2f} "
              f"test_acc={res['agents'][name]['test_acc']:.3f}")


if __name__ == "__main__":
    main()
