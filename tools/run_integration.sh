#!/usr/bin/env bash
# =============================================================================
# run_integration.sh - ZhiOS-AF 一键端到端集成启动脚本（本地 Docker，单文件编排）
#
# 基于合并后的单文件 docker-compose.yml，同时启动并验证多个模块：
#   1) Agent 控制软件（agent-console）  —— 常驻服务（前端 + 后端 + SQLite）
#   2) 防火墙模块（firewall）            —— 独立容器内逻辑验证（一次性）
#   3) 驱动模块（driver）                —— 独立容器内逻辑验证（一次性）
#   4) 端到端综合验证（e2e）             —— 控制软件 + 防火墙 + 驱动
#
# 用法（在 zhi-os-af/ 仓库根目录执行）：
#   bash tools/run_integration.sh
#
# 退出码：0 = 全部通过；非 0 = 存在失败。
# =============================================================================
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "======================================================================"
echo " ZhiOS-AF 端到端集成测试启动  目录: $ROOT"
echo "======================================================================"

fail=0

# ---------- 0) 检查 docker ----------
if ! command -v docker >/dev/null 2>&1; then
  echo "[错误] 未检测到 docker，请先安装并启动 Docker/WSL2。" >&2; exit 1
fi
docker info >/dev/null 2>&1 || { echo "[错误] Docker 引擎不可用。" >&2; exit 1; }

# ---------- 1) 构建镜像 ----------
echo "[1/5] 构建镜像（agent-console / firewall / driver / e2e）..."
docker compose build agent-console firewall driver e2e \
  || { echo "[错误] 镜像构建失败" >&2; exit 1; }

# ---------- 2) 启动 Agent 控制软件（常驻） ----------
echo "[2/5] 启动 Agent 控制软件（端口 8000）..."
docker compose up -d agent-console || { echo "[错误] agent-console 启动失败" >&2; fail=1; }

echo "  等待 agent-console 就绪..."
up=0
for i in $(seq 1 30); do
  sleep 1
  if curl -fsS -m 2 http://localhost:8000/api/monitor >/dev/null 2>&1; then up=1; break; fi
done
if [ "$up" = "1" ]; then echo "  [OK] agent-console 就绪"; else echo "  [错误] agent-console 未就绪"; fail=1; fi

# ---------- 3) 运行防火墙模块（一次性） ----------
echo "[3/5] 运行防火墙模块（独立容器）..."
fw_out="$(docker compose run --rm firewall)"; fw_code=$?
echo "$fw_out"
if [ "$fw_code" = "0" ]; then echo "  [OK] 防火墙模块验证通过"; else echo "  [错误] 防火墙模块验证失败（exit=$fw_code）"; fail=1; fi

# ---------- 4) 运行驱动模块（一次性） ----------
echo "[4/5] 运行驱动模块（独立容器）..."
drv_out="$(docker compose run --rm driver)"; drv_code=$?
echo "$drv_out"
if [ "$drv_code" = "0" ]; then echo "  [OK] 驱动模块验证通过"; else echo "  [错误] 驱动模块验证失败（exit=$drv_code）"; fail=1; fi

# ---------- 5) 运行端到端综合验证（一次性） ----------
echo "[5/5] 运行端到端综合验证（e2e：控制软件 + 防火墙 + 驱动）..."
e2e_out="$(docker compose run --rm e2e)"; e2e_code=$?
echo "$e2e_out"
if [ "$e2e_code" = "0" ]; then echo "  [OK] 端到端综合验证通过"; else echo "  [错误] 端到端综合验证失败（exit=$e2e_code）"; fail=1; fi

# ---------- 汇总 ----------
echo "======================================================================"
if [ "$fail" = "0" ]; then
  echo " 端到端集成测试：ALL PASS"
  echo " Agent 控制软件:  http://localhost:8000"
  echo " 停止控制软件:    docker compose stop agent-console"
else
  echo " 端到端集成测试：存在失败项"
fi
echo "======================================================================"
exit "$fail"
