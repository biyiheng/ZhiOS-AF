#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
trainer.py - Agent 控制软件的训练引擎（可复用模块）

提供"合法数据获取 -> 训练 -> 评估 -> 类别不平衡分析"的完整链路，供以下场景复用：
  1. Agent 控制软件后端（tools/agent_console/app.py）
  2. Docker 容器内自动化训练测试（docker/sim/training_test.py）
  3. 训练数据分布分析报告生成（tools/sim/analyze_data.py）

数据获取：优先合法爬取公开/宽松许可数据源并解析真实数值特征；
无外网/失败时自动回退到内置确定性合成数据，保证链路可复现、可验证。
"""
import random as _random
import urllib.request as _urllib

AGENTS = ["vision", "motion", "safety", "force", "quality"]

# 每个 Agent 的真实"隐规则"（w1,w2,bias）：仅用于给样本打标签，学习者不可见
_AGENT_RULE = {
    "vision":  ( 1.0,  0.6,  0.65),
    "motion":  ( 0.8, -0.4,  0.30),
    "safety":  (-0.5,  1.0,  0.55),
    "force":   ( 0.7,  0.9,  0.80),
    "quality": ( 0.4, -1.0,  0.10),
}

# 数据源：公开/宽松许可示例（生产环境按需替换为已授权数据集）
PUBLIC_SOURCES = [
    "https://raw.githubusercontent.com/scikit-learn/scikit-learn/main/"
    "sklearn/datasets/data/iris.csv",   # BSD-3，仅供示例
]


def _label_sample(agent, f1, f2):
    """按 Agent 隐规则打标签（含 8% 标注噪声）。"""
    w1, w2, b = _AGENT_RULE[agent]
    y = 1 if (w1 * f1 + w2 * f2 - b) > 0 else 0
    if _random.random() < 0.08:
        y = 1 - y
    return {"f1": f1, "f2": f2, "y": y}


def _feature_sample(rng, agent):
    return _label_sample(agent, rng.random(), rng.random())


def _synthetic_generate(seed, n):
    rng = _random.Random(seed)
    return {a: [_feature_sample(rng, a) for _ in range(n)] for a in AGENTS}


def crawl_public_dataset(url, timeout=8):
    """合法抓取公开数据源。失败抛异常由调用方回退。"""
    req = _urllib.Request(url, headers={"User-Agent": "ZhiOS-AF-training/1.0"})
    with _urllib.urlopen(req, timeout=timeout) as resp:
        return resp.read().decode("utf-8")


def _parse_numeric_csv(text):
    """把抓到的 CSV 文本解析为数值特征矩阵（跳过表头/非数值行）。"""
    rows = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        parts = [p.strip() for p in line.split(",")]
        nums = []
        for p in parts:
            try:
                nums.append(float(p))
            except ValueError:
                break
        if len(nums) >= 2:
            rows.append(nums)
    return rows


def _dataset_from_rows(rows, n):
    """用抓到的真实特征(前2维)为每个 Agent 生成带标签样本。"""
    rng = _random.Random(7)
    data = {a: [] for a in AGENTS}
    for _ in range(n):
        r = rng.choice(rows)
        f1, f2 = r[0], r[1]
        for a in AGENTS:
            data[a].append(_label_sample(a, f1, f2))
    return data


def acquire_data(n_train=400, n_test=200):
    """
    获取全部 Agent 的训练/测试数据。
    优先合法爬虫抓取公开数据集并解析为真实特征；离线时回退到内置合成数据。
    返回 (train, test, source_desc, crawled_rows)。
    """
    for url in PUBLIC_SOURCES:
        try:
            text = crawl_public_dataset(url)
            rows = _parse_numeric_csv(text)
            if len(rows) >= 50:
                train = _dataset_from_rows(rows, n_train)
                test = _dataset_from_rows(rows, n_test)
                return train, test, f"crawled:{url} (样本 {len(rows)} 条)", rows
        except Exception:   # noqa: 无外网或抓取失败
            pass
    train = _synthetic_generate(1, n_train)
    test = _synthetic_generate(2, n_test)
    return train, test, "offline-synthetic (沙箱无外网，回退内置合成数据)", []


# ---- 口袋感知机（Pocket Perceptron）：可学习的线性分类器 ----
def _class_weights(samples):
    """类别加权：按类别频率的倒数加权，缓解类别不平衡。

    w_c = n_total / (2 * n_c)，使多数类与少数类对梯度更新的贡献相当，
    避免模型偏向多数类（类别不平衡处理的核心手段之一）。
    """
    n = len(samples)
    if n == 0:
        return {0: 1.0, 1: 1.0}
    pos = sum(1 for s in samples if s["y"] == 1)
    neg = n - pos
    w_pos = n / (2.0 * max(1, pos))
    w_neg = n / (2.0 * max(1, neg))
    return {1: w_pos, 0: w_neg}


def _balanced_accuracy(samples, w):
    """宏平均召回率（balanced accuracy），对类别不平衡更公平。"""
    tp = fn = tn = fp = 0
    for s in samples:
        p = 1 if (w[0] * s["f1"] + w[1] * s["f2"] + w[2]) > 0 else 0
        if s["y"] == 1:
            tp += p == 1; fn += p == 0
        else:
            tn += p == 0; fp += p == 1
    rec_pos = tp / max(1, tp + fn)
    rec_neg = tn / max(1, tn + fp)
    return 0.5 * (rec_pos + rec_neg)


def _perceptron_train(samples, epochs=40, class_weights=None):
    """在 (f1,f2) 上学习线性边界 w=(w0,w1,w2)，返回最佳口袋模型。

    class_weights 非空时：更新步长按样本类别加权（缓解不平衡），
    且口袋选择以 balanced accuracy 为目标（使加权策略真正作用于少数类）。
    """
    w = [0.0, 0.0, 0.0]
    use_balanced = class_weights is not None
    best_w, best_score = w[:], -1.0

    def pred(s):
        return 1 if (w[0] * s["f1"] + w[1] * s["f2"] + w[2]) > 0 else 0

    def score():
        if use_balanced:
            return _balanced_accuracy(samples, w)
        return sum(1 for s in samples if pred(s) == s["y"]) / max(1, len(samples))

    for _ in range(epochs):
        for s in samples:
            if pred(s) != s["y"]:
                wgt = (class_weights or {1: 1.0, 0: 1.0}).get(s["y"], 1.0)
                step = wgt if s["y"] == 1 else -wgt
                w[0] += step * s["f1"]
                w[1] += step * s["f2"]
                w[2] += step
        sc = score()
        if sc > best_score:
            best_score, best_w = sc, w[:]
    return best_w, best_score


def _perceptron_predict(w, s):
    return 1 if (w[0] * s["f1"] + w[1] * s["f2"] + w[2]) > 0 else 0


def evaluate(samples, w):
    """在样本上评估，返回 (accuracy, balanced_accuracy)。

    balanced_accuracy = 0.5 * (正类召回率 + 负类召回率)，
    更公平地反映类别不平衡下的模型表现。
    """
    tp = fn = tn = fp = 0
    for s in samples:
        p = _perceptron_predict(w, s)
        if s["y"] == 1:
            tp += p == 1
            fn += p == 0
        else:
            tn += p == 0
            fp += p == 1
    total = max(1, len(samples))
    acc = (tp + tn) / total
    rec_pos = tp / max(1, tp + fn)
    rec_neg = tn / max(1, tn + fp)
    balanced = 0.5 * (rec_pos + rec_neg)
    return acc, balanced


def class_distribution(dataset, name):
    """统计单个 Agent 数据集的类别分布与不平衡度。"""
    pos = sum(1 for s in dataset[name] if s["y"] == 1)
    total = len(dataset[name])
    neg = total - pos
    ratio = (max(pos, neg) / max(1, min(pos, neg))) if min(pos, neg) > 0 else float("inf")
    return {
        "agent": name,
        "total": total,
        "pos": pos,
        "neg": neg,
        "pos_ratio": pos / max(1, total),
        "neg_ratio": neg / max(1, total),
        "imbalance_ratio": ratio,
        "balanced": 0.4 <= (pos / max(1, total)) <= 0.6,
    }


def imbalance_analysis(train, test):
    """对全部 Agent 做类别不平衡分析，返回汇总。"""
    return {
        "train": [class_distribution(train, a) for a in AGENTS],
        "test": [class_distribution(test, a) for a in AGENTS],
        "summary": {
            a: class_distribution(train, a) for a in AGENTS
        },
    }


def train_and_evaluate(n_train=400, n_test=200, weighted=False):
    """
    训练全部 Agent 并在未见测试集上评估。

    weighted=True 时启用类别加权（缓解不平衡）；默认 False 以保持既有
    原始准确率（>=0.6）评估口径稳定。类别加权对"少数类可学习"的 Agent
    （如 safety）可显著提升 balanced accuracy，但极端不平衡下会牺牲原始准确率，
    故默认不强制开启，供 `train_compare()` 显式对比验证。

    返回 dict：{ source, crawled_rows, agents:{name:{...}}, imbalance }
    每个 Agent 包含：train_acc / test_acc / balanced_acc / model / class_weights。
    """
    train, test, source, rows = acquire_data(n_train, n_test)
    results = {}
    for name in AGENTS:
        cw = _class_weights(train[name]) if weighted else None
        w, _ = _perceptron_train(train[name], class_weights=cw)
        tr_acc, tr_bal = evaluate(train[name], w)
        te_acc, te_bal = evaluate(test[name], w)
        results[name] = {
            "train_acc": round(tr_acc, 4),
            "train_balanced": round(tr_bal, 4),
            "test_acc": round(te_acc, 4),
            "balanced_acc": round(te_bal, 4),
            "weighted": bool(weighted),
            "class_weights": {str(k): round(v, 3) for k, v in (cw or {1: 1.0, 0: 1.0}).items()},
            "model": [round(x, 4) for x in w],
        }
    imb = imbalance_analysis(train, test)
    return {
        "source": source,
        "crawled_rows": rows,
        "n_train": n_train,
        "n_test": n_test,
        "weighted": bool(weighted),
        "agents": results,
        "imbalance": imb,
    }


def train_compare(n_train=400, n_test=200):
    """
    对比"未加权"与"类别加权"两种策略的训练效果（用于验证类别加权策略的有效性）。
    返回 dict：{ source, per_agent:[{name, plain_acc, plain_bal, w_acc, w_bal}] }
    """
    plain = train_and_evaluate(n_train, n_test, weighted=False)
    weighted = train_and_evaluate(n_train, n_test, weighted=True)
    per = []
    for name in AGENTS:
        per.append({
            "name": name,
            "plain_acc": plain["agents"][name]["test_acc"],
            "plain_balanced": plain["agents"][name]["balanced_acc"],
            "weighted_acc": weighted["agents"][name]["test_acc"],
            "weighted_balanced": weighted["agents"][name]["balanced_acc"],
        })
    return {"source": plain["source"], "per_agent": per}


if __name__ == "__main__":
    import json
    r = train_compare()
    print("source:", r["source"])
    print(f"{'agent':<9}{'plain_acc':>10}{'plain_bal':>10}{'w_acc':>10}{'w_bal':>10}")
    for p in r["per_agent"]:
        print(f"{p['name']:<9}{p['plain_acc']:>10.3f}{p['plain_balanced']:>10.3f}"
              f"{p['weighted_acc']:>10.3f}{p['weighted_balanced']:>10.3f}")
