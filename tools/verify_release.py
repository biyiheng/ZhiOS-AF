#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_release.py - release 分支可部署性静态校验

在无 Docker 引擎的环境下，通过静态分析校验 release 分支"完整可独立部署"：
  1) 校验当前 git 分支为 release（可用 --dir 指定其它目录/分支）；
  2) 解析 docker-compose.yml 每个服务的 build context + dockerfile；
  3) 解析每个 Dockerfile 的 COPY 源路径，确认其在构建上下文内真实存在；
  4) 校验 compose 的 bind 挂载源（非命名卷）路径存在；
  5) 校验 e2e/agent-console 关键运行文件齐全。

用法（在 zhi-os-af/ 仓库根目录）：
  python tools/verify_release.py
  python tools/verify_release.py --dir <仓库路径>

退出码：0 = 全部通过；非 0 = 存在缺失文件。
"""
import argparse
import os
import re
import subprocess
import sys

# ---- 轻量 YAML 解析（仅需 compose 的 build.context / dockerfile / volumes）----
def parse_compose_builds(text):
    """返回 [(service, context, dockerfile_or_None, volumes_src_list)]"""
    services = {}
    # 定位 services: 顶层键（缩进 0）且以空格开头的 map
    current_service = None
    context = None
    dockerfile = None
    volumes = []
    in_build = False

    lines = text.splitlines()
    for raw in lines:
        if not raw.strip() or raw.lstrip().startswith('#'):
            continue
        indent = len(raw) - len(raw.lstrip())
        line = raw.strip()
        if indent == 0:
            # 顶层键，可能为 services / volumes / version ...
            key = line.rstrip(':')
            if key in ('services', 'volumes', 'version', 'name', 'networks', 'configs', 'secrets'):
                # flush previous service
                if current_service:
                    services[current_service] = (context, dockerfile, volumes)
                current_service = None; context = None; dockerfile = None; volumes = []; in_build = False
            continue
        if indent == 2:
            # 服务名
            if current_service:
                services[current_service] = (context, dockerfile, volumes)
            current_service = line.rstrip(':')
            context = None; dockerfile = None; volumes = []; in_build = False
            continue
        if indent == 4:
            key = line.split(':', 1)[0]
            if key == 'build':
                in_build = True
                if line.count(':') == 1 and not line.endswith(':'):
                    context = line.split(':', 1)[1].strip()
                continue
            elif key == 'volumes':
                in_build = False
                continue
            else:
                in_build = False
            continue
        if indent == 6:
            if in_build:
                key = line.split(':', 1)[0]
                val = line.split(':', 1)[1].strip() if ':' in line else ''
                if key == 'context':
                    context = val
                elif key == 'dockerfile':
                    dockerfile = val
            else:
                # volumes 子项（bind 源）
                v = line.rstrip(':')
                if v and not v.startswith('-'):
                    src = v.split(':')[0]
                    if src and not src.startswith('.'):
                        pass
                elif line.startswith('- '):
                    src = line[2:].split(':')[0]
                    volumes.append(src)
    if current_service:
        services[current_service] = (context, dockerfile, volumes)
    return services


def parse_dockerfile_copy_sources(df_path):
    """返回 Dockerfile 中所有 COPY 的源路径（跳过 --from= 阶段拷贝）。"""
    srcs = []
    with open(df_path, 'r', encoding='utf-8', errors='ignore') as f:
        for raw in f:
            line = raw.strip()
            if line.startswith('COPY '):
                # 跳过多行续行暂不处理（本仓库 Dockerfile 均单行 COPY）
                tokens = line[len('COPY '):].split()
                if not tokens:
                    continue
                if tokens[0].startswith('--from'):
                    continue  # 跨阶段拷贝，非文件系统源
                # 最后一个 token 为目标；其余为源
                for s in tokens[:-1]:
                    srcs.append(s.strip().strip('"'))
    return srcs


def verify_dir(root):
    problems = []
    checks = 0

    compose_path = os.path.join(root, 'docker-compose.yml')
    if not os.path.exists(compose_path):
        return 0, [('docker-compose.yml', '缺失')], 0

    with open(compose_path, 'r', encoding='utf-8') as f:
        compose_text = f.read()
    services = parse_compose_builds(compose_text)
    print(f"[compose] 发现 {len(services)} 个服务")
    for name, (ctx, df, vol_srcs) in sorted(services.items()):
        ctx = ctx or '.'
        base = os.path.normpath(os.path.join(root, ctx))
        print(f"  服务 {name}: context={ctx!r} dockerfile={df!r}")
        if not df:
            continue
        df_path = os.path.join(root, ctx, df)
        checks += 1
        if not os.path.exists(df_path):
            problems.append((f"{name} 的 Dockerfile", df_path))
            continue
        for src in parse_dockerfile_copy_sources(df_path):
            src_path = os.path.normpath(os.path.join(base, src))
            checks += 1
            if not os.path.exists(src_path):
                problems.append((f"{name}/{df} COPY {src}", src_path))
        for vsrc in vol_srcs:
            if vsrc.startswith('.'):
                vpath = os.path.normpath(os.path.join(root, vsrc))
                checks += 1
                if not os.path.exists(vpath):
                    problems.append((f"{name} 挂载 {vsrc}", vpath))

    # 关键运行文件
    for p in ('tools/agent_console/trainer.py',
              'tools/agent_console/e2e_test.py',
              'tools/agent_console/schema.sql',
              'tools/agent_console/app.py',
              'tools/agent_console/static/index.html',
              'tools/run_integration.sh',
              'tools/run_wsl_deploy.sh',
              'ai_kernel/security/firewall.c',
              'include/firewall.h',
              'Makefile',
              'Dockerfile'):
        checks += 1
        if not os.path.exists(os.path.join(root, p)):
            problems.append(('关键运行文件', p))
    return checks, problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default=None, help='仓库目录（默认当前目录）')
    args = ap.parse_args()

    root = os.path.abspath(args.dir or os.getcwd())
    print("=" * 64)
    print("ZhiOS-AF · release 分支可部署性静态校验")
    print(f"目录: {root}")
    print("=" * 64)

    # 分支校验（若为 git 仓库）
    try:
        br = subprocess.run(['git', '-C', root, 'branch', '--show-current'],
                            capture_output=True, text=True, timeout=10)
        branch = br.stdout.strip() or '(detached)'
        print(f"[git] 当前分支: {branch}")
        if branch != 'release':
            print(f"[警告] 当前分支为 '{branch}'，建议在 release 分支上执行本校验。")
    except Exception:
        print("[git] 非 git 仓库或 git 不可用，跳过分支校验。")

    checks, problems = verify_dir(root)

    print("-" * 64)
    if not problems:
        print(f"校验通过：共检查 {checks} 处引用，无缺失文件。")
        print("==> RELEASE VERIFY ALL PASS")
        return 0
    print(f"校验失败：共检查 {checks} 处引用，发现 {len(problems)} 处缺失：")
    for where, p in problems:
        print(f"  - [{where}] 缺失: {p}")
    print("==> RELEASE VERIFY FAILED")
    return 1


if __name__ == '__main__':
    sys.exit(main())
