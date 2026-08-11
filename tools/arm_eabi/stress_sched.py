#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
stress_sched.py - 上下文切换亚微秒切换与 Jitter 自动化压测方案
================================================================
对应《33-操作系统技术指标体系设计文档》4.1/4.3 中标注为黄色的
"亚微秒级切换"与"抖动(Jitter)"指标。

原理：
  1) 用 arm-none-eabi 构建 verify_mps2.elf（Cortex-M3 + QEMU 机器 mps2-an385）。
  2) 固件以 DWT->CYCCNT 对上下文切换关键路径（MRS PSP/STMDB R4-R11/LDMIA/MSR PSP）
     采样 1000 次，逐行打印 `sw_cycles,<周期数>` CSV 及 SW_SUMMARY 汇总。
  3) 本脚本在 QEMU 上重复运行 NUM_RUNS 轮，解析全部样本，统计：
       - mean / min / max 切换周期
       - 切换时间（周期数 / CPU 频率）
       - jitter = max - min（周期数），及 jitter 占比 = (max-min)/mean
  4) 强制判定硬性门槛：
       - 平均切换时间 < --max-switch-ns（默认 1000 ns = 1μs，即"亚微秒级切换"）
       - jitter 占比 < --max-jitter-pct（默认 10%，可收紧）
  5) 任一门槛不满足则退出码非 0，供 CI 中断。

在真实开发板上的替代：
  - 将本脚本的 QEMU 运行步骤替换为：J-Link/GDB 在开发板运行同一固件，
    通过 GDB 读取 DWT_CYCCNT 或串口捕获 sw_cycles CSV，送入同一套统计/判定。
    频率参数 --freq 改为真实内核频率（如 STM32H743 @480MHz 填 480000000）。

用法：
  python3 stress_sched.py                      # 默认构建并压测
  python3 stress_sched.py --runs 50 --freq 25000000
  python3 stress_sched.py --elf build/verify_mps2.elf --qemu qemu-system-arm
  python3 stress_sched.py --max-switch-ns 1000 --max-jitter-pct 5
"""
import argparse
import os
import re
import statistics
import subprocess
import sys

SW_RE = re.compile(r"^sw_cycles,(\d+)\s*$")
SUMMARY_RE = re.compile(
    r"SW_SUMMARY mean,(\d+),min,(\d+),max,(\d+),jitter,(\d+)")

DEFAULT_ELF = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "build", "verify_mps2.elf")
BUILD_SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "build_verify.sh")


def run_qemu(qemu, elf, freq_hz, timeout):
    """运行一次 QEMU，返回切换周期样本列表与固件汇总字典。"""
    cmd = [qemu, "-M", "mps2-an385", "-cpu", "cortex-m3",
           "-nographic", "-serial", "stdio", "-monitor", "none", "-kernel", elf]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    out = proc.stdout or ""
    samples = [int(m.group(1)) for m in SW_RE.finditer(out)]
    summary = None
    m = SUMMARY_RE.search(out)
    if m:
        summary = {
            "mean": int(m.group(1)),
            "min": int(m.group(2)),
            "max": int(m.group(3)),
            "jitter": int(m.group(4)),
        }
    return samples, summary


def main():
    ap = argparse.ArgumentParser(description="上下文切换亚微秒/Jitter 自动化压测")
    ap.add_argument("--elf", default=DEFAULT_ELF, help="QEMU 可执行镜像路径")
    ap.add_argument("--qemu", default="qemu-system-arm", help="qemu-system-arm 可执行名")
    ap.add_argument("--freq", type=int, default=25000000,
                    help="CPU 频率 Hz（mps2-an385 默认 25MHz；STM32H743 真机填 480000000）")
    ap.add_argument("--runs", type=int, default=20, help="重复运行的轮数")
    ap.add_argument("--max-switch-ns", type=float, default=1000.0,
                    help="平均切换时间硬性上限（ns，默认 1000ns=1μs）")
    ap.add_argument("--max-jitter-pct", type=float, default=10.0,
                    help="jitter 占比上限（默认 10%%）")
    ap.add_argument("--build", action="store_true",
                    help="压测前先执行 build_verify.sh 重新构建")
    ap.add_argument("--timeout", type=int, default=30, help="单轮 QEMU 超时秒数")
    args = ap.parse_args()

    if args.build or not os.path.exists(args.elf):
        print("[stress] building firmware first ...")
        r = subprocess.run(["bash", BUILD_SCRIPT], capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stdout, r.stderr)
            sys.exit("BUILD FAILED")

    if not os.path.exists(args.elf):
        sys.exit(f"ERROR: ELF not found: {args.elf}")
    if subprocess.run(["sh", "-c", f"command -v {args.qemu}"],
                      capture_output=True).returncode != 0:
        sys.exit(f"ERROR: qemu-system-arm not found ({args.qemu})，"
                 "请安装 qemu-system-arm 或改用真实开发板（见 README）")

    all_samples = []
    worst_run_mean_ns = 0.0
    overall_min = 10 ** 9
    overall_max = 0

    print(f"[stress] runs={args.runs} freq={args.freq}Hz "
          f"threshold switch<{args.max_switch_ns}ns jitter<{args.max_jitter_pct}%")
    for r in range(args.runs):
        samples, summary = run_qemu(args.qemu, args.elf, args.freq, args.timeout)
        if not samples:
            print(f"  run {r:3d}: NO samples captured (QEMU 输出为空)")
            continue
        all_samples.extend(samples)
        overall_min = min(overall_min, min(samples))
        overall_max = max(overall_max, max(samples))
        run_mean = statistics.mean(samples)
        run_mean_ns = run_mean / args.freq * 1e9
        worst_run_mean_ns = max(worst_run_mean_ns, run_mean_ns)
        print(f"  run {r:3d}: n={len(samples):4d} mean={run_mean_ns:7.1f}ns "
              f"min={min(samples)} max={max(samples)} cycles")

    if not all_samples:
        sys.exit("FATAL: 未能从 QEMU 捕获任何切换周期样本")

    mean = statistics.mean(all_samples)
    mean_ns = mean / args.freq * 1e9
    jitter = overall_max - overall_min
    jitter_pct = (jitter / mean * 100.0) if mean else 0.0

    print("\n================ STRESS RESULT ================")
    print(f"samples     : {len(all_samples)}")
    print(f"mean switch : {mean} cycles = {mean_ns:.1f} ns  (worst-run mean {worst_run_mean_ns:.1f} ns)")
    print(f"min / max   : {overall_min} / {overall_max} cycles")
    print(f"jitter      : {jitter} cycles = {jitter_pct:.2f} % of mean")
    print("-----------------------------------------------")

    switch_ok = mean_ns < args.max_switch_ns
    jitter_ok = jitter_pct < args.max_jitter_pct
    print(f"[PASS] sub-microsecond switch  : {mean_ns:.1f}ns < {args.max_switch_ns}ns -> {switch_ok}")
    print(f"[PASS] jitter ratio            : {jitter_pct:.2f}% < {args.max_jitter_pct}% -> {jitter_ok}")
    print("================================================")

    if switch_ok and jitter_ok:
        print("STRESS SCHED ALL PASS")
        return 0
    print("STRESS SCHED FAIL")
    return 1


if __name__ == "__main__":
    sys.exit(main())
