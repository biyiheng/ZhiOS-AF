#!/usr/bin/env bash
# 在 Docker 容器中完整运行"爬虫取数 + Agent 训练"自动化测试（环境隔离验证）
# 用法：
#   bash tools/sim/run_training_test.sh          # 仅本地（不经 Docker）
#   bash tools/sim/run_training_test.sh --docker # 在 Docker 容器中运行
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

if [[ "${1:-}" == "--docker" ]]; then
  echo "==> 构建并运行 Docker 隔离训练测试镜像"
  docker build -f docker/sim/Dockerfile -t zhi-af/sim-training .
  docker run --rm -e N_TRAIN=400 -e N_TEST=200 zhi-af/sim-training
else
  echo "==> 本地直接运行（等价容器内 entrypoint）"
  python docker/sim/training_test.py
fi
