#!/usr/bin/env bash
# =============================================================================
# run_wsl_deploy.sh - ZhiOS-AF WSL2 环境一键部署脚本
#
# 目标：在 WSL2 环境下完成"环境校验 -> (可选)拉取 release 分支 -> 构建 ->
#       启动 Agent 控制软件 -> 验证防火墙/驱动 -> 端到端 + 压力验证"的全流程。
#
# 与 run_integration.sh 的区别：
#   - 额外做 WSL2 环境前置校验（内核版本 / OSType / compose 插件）；
#   - 支持 --fresh-clone 从本地/远端仓库干净拉取 release 分支到临时目录部署，
#     用于验证 release 分支的"完整可独立部署性"；
#   - 支持 --keep 部署后保持 agent-console 常驻，便于人工访问。
#
# 用法（在 WSL2 终端内，仓库根目录或任意位置均可）：
#   bash tools/run_wsl_deploy.sh                       # 用当前目录部署
#   bash tools/run_wsl_deploy.sh --fresh-clone [URL]   # 干净拉取 release 分支后部署
#   bash tools/run_wsl_deploy.sh --keep                # 部署后保持服务常驻
#
# 退出码：0 = 全部通过；非 0 = 存在失败。
# =============================================================================
set -uo pipefail

# ---------- 参数解析 ----------
KEEP=0
FRESH_CLONE=""
REPO_URL=""
while [ $# -gt 0 ]; do
  case "$1" in
    --keep) KEEP=1 ;;
    --fresh-clone) FRESH_CLONE=1; REPO_URL="$2"; shift ;;
    --*) echo "[错误] 未知参数: $1" >&2; exit 2 ;;
    *) REPO_URL="$1" ;;
  esac
  shift
done

log()  { echo -e "$*"; }
ok()   { echo -e "  \e[32m[OK]\e[0m $*"; }
err()  { echo -e "  \e[31m[错误]\e[0m $*" >&2; }

# ---------- 0) WSL2 环境校验 ----------
echo "======================================================================"
echo " ZhiOS-AF · WSL2 一键部署"
echo "======================================================================"

if ! command -v docker >/dev/null 2>&1; then
  err "未检测到 docker，请先在 WSL2 中安装 Docker（或启用 Docker Desktop 的 WSL2 后端）。"; exit 1
fi
docker info >/dev/null 2>&1 || { err "Docker 引擎不可用，请确认已启动（systemctl start docker 或 Docker Desktop）。"; exit 1; }

# 1) OSType 必须为 linux（WSL2 内的 Docker 后端）
ostype="$(docker info --format '{{.OSType}}' 2>/dev/null || echo unknown)"
if [ "$ostype" != "linux" ]; then
  err "Docker OSType 为 '$ostype'，不是 linux；请确认使用 Linux 容器（WSL2）模式。"; exit 1
fi

# 2) 内核应带 microsoft/WSL2 标记（uname 校验在 WSL 内才成立，非 WSL 时仅提示）
kernel="$(uname -r 2>/dev/null || echo unknown)"
case "$kernel" in
  *microsoft*|*WSL2*|*wsl2*)
    ok "检测到 WSL 内核: $kernel" ;;
  *)
    log "  [提示] 当前 uname 内核 '$kernel' 未带 microsoft 标记；若非 WSL2 环境，请确认使用 Linux 容器。" ;;
esac
major="$(echo "$kernel" | grep -oE '^[0-9]+\.[0-9]+' | head -1)"
if [ -n "$major" ]; then
  m="${major%%.*}"; n="${major##*.}"
  if [ "$m" -gt 5 ] || { [ "$m" -eq 5 ] && [ "$n" -ge 10 ]; }; then
    ok "内核版本 $major（>=5.10，满足 Docker WSL2 要求）"
  else
    err "内核版本 $major < 5.10，WSL2 需 >=5.10，请更新 WSL（wsl --update）。"; exit 1
  fi
fi

# 3) compose 插件
if ! docker compose version >/dev/null 2>&1; then
  err "未检测到 docker compose 插件，请安装 docker-compose-plugin。"; exit 1
fi
ok "docker + compose 就绪: $(docker compose version | head -1)"

# ---------- 1) 选定部署目录（默认当前目录；--fresh-clone 则干净拉取 release 分支） ----------
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ -n "$FRESH_CLONE" ]; then
  [ -n "$REPO_URL" ] || REPO_URL="$ROOT"
  TMP="$(mktemp -d)"
  log "[fresh-clone] 从 '$REPO_URL' 拉取 release 分支到 $TMP ..."
  git clone --branch release --depth 1 "$REPO_URL" "$TMP" \
    || { err "拉取 release 分支失败（URL: $REPO_URL）"; rm -rf "$TMP"; exit 1; }
  ROOT="$TMP"
  log "[fresh-clone] release 分支已就绪：$(git -C "$ROOT" log --oneline -1)"
fi
cd "$ROOT" || { err "无法进入目录 $ROOT"; exit 1; }

# ---------- 2) 构建镜像 ----------
echo "[1/6] 构建镜像（agent-console / firewall / driver / e2e）..."
docker compose build agent-console firewall driver e2e \
  || { err "镜像构建失败"; exit 1; }
ok "镜像构建完成"

# ---------- 3) 启动 Agent 控制软件（常驻） ----------
echo "[2/6] 启动 Agent 控制软件（端口 8000）..."
docker compose up -d agent-console || { err "agent-console 启动失败"; exit 1; }
up=0
for i in $(seq 1 30); do
  sleep 1
  if curl -fsS -m 2 http://localhost:8000/api/monitor >/dev/null 2>&1; then up=1; break; fi
done
if [ "$up" = "1" ]; then ok "agent-console 就绪 (http://localhost:8000)"; else err "agent-console 未就绪"; exit 1; fi

fail=0

# ---------- 4) 防火墙模块（一次性） ----------
echo "[3/6] 运行防火墙模块（独立容器）..."
docker compose run --rm firewall; c=$?
if [ "$c" = "0" ]; then ok "防火墙模块验证通过"; else err "防火墙模块验证失败（exit=$c）"; fail=1; fi

# ---------- 5) 驱动模块（一次性） ----------
echo "[4/6] 运行驱动模块（独立容器）..."
docker compose run --rm driver; c=$?
if [ "$c" = "0" ]; then ok "驱动模块验证通过"; else err "驱动模块验证失败（exit=$c）"; fail=1; fi

# ---------- 6) 端到端 + 压力验证（一次性，e2e 容器） ----------
echo "[5/6] 运行端到端综合验证（e2e：控制软件 + 防火墙 + 驱动 + 压力）..."
docker compose run --rm e2e; c=$?
if [ "$c" = "0" ]; then ok "端到端综合验证通过"; else err "端到端综合验证失败（exit=$c）"; fail=1; fi

# ---------- 7) 附加：主机侧本地 e2e+压力（进程内，可复核断言数） ----------
echo "[6/6] 主机侧进程内端到端 + 压力复核（可选，仅当 python3 可用）..."
if command -v python3 >/dev/null 2>&1 && [ -f tools/agent_console/e2e_test.py ]; then
  python3 tools/agent_console/e2e_test.py >/tmp/zhi_e2e_console.log 2>&1
  c=$?
  tail -6 /tmp/zhi_e2e_console.log
  if [ "$c" = "0" ]; then ok "主机侧进程内复核通过"; else err "主机侧进程内复核失败（exit=$c）"; fail=1; fi
else
  log "  [跳过] 未检测到 python3 或缺少 e2e_test.py，跳过主机侧复核。"
fi

# ---------- 清理 ----------
if [ "$KEEP" = "0" ]; then
  log "[清理] 停止并移除本次启动的容器..."
  docker compose stop agent-console >/dev/null 2>&1 || true
  docker compose rm -f agent-console >/dev/null 2>&1 || true
  log "[清理] 完成（如需保留请使用 --keep）。"
fi
if [ -n "$FRESH_CLONE" ]; then
  rm -rf "$ROOT" 2>/dev/null || true
  log "[fresh-clone] 已清理临时目录。"
fi

# ---------- 汇总 ----------
echo "======================================================================"
if [ "$fail" = "0" ]; then
  echo "  WSL2 一键部署：ALL PASS"
  if [ "$KEEP" = "1" ]; then
    echo "  Agent 控制软件: http://localhost:8000"
    echo "  停止:           docker compose -f $ROOT/docker-compose.yml stop agent-console"
  fi
else
  echo "  WSL2 一键部署：存在失败项"
fi
echo "======================================================================"
exit "$fail"
