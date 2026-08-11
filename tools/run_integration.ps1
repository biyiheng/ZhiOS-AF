# =============================================================================
# run_integration.ps1 - ZhiOS-AF 一键端到端集成启动脚本（本地 Docker，单文件编排）
#
# 基于合并后的单文件 docker-compose.yml，同时启动并验证多个模块：
#   1) Agent 控制软件（agent-console）  —— 常驻服务（前端 + 后端 + SQLite）
#   2) 防火墙模块（firewall）            —— 独立容器内逻辑验证（一次性）
#   3) 驱动模块（driver）                —— 独立容器内逻辑验证（一次性）
#   4) 端到端综合验证（e2e）             —— 控制软件 + 防火墙 + 驱动
#
# 用法（在 zhi-os-af/ 仓库根目录执行）：
#   powershell -ExecutionPolicy Bypass -File tools/run_integration.ps1
#
# 退出码：0 = 全部通过；非 0 = 存在失败。
# =============================================================================
$ErrorActionPreference = "Stop"

$ROOT = Split-Path -Parent $PSScriptRoot
Set-Location $ROOT

Write-Host "================================================================" -ForegroundColor Cyan
Write-Host " ZhiOS-AF 端到端集成测试启动"
Write-Host " 目录: $ROOT"
Write-Host "================================================================" -ForegroundColor Cyan

$fail = 0

# ---------- 0) 检查 docker ----------
if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    Write-Host "[错误] 未检测到 docker，请先安装并启动 Docker Desktop/WSL2。" -ForegroundColor Red
    exit 1
}
docker info *> $null
if ($LASTEXITCODE -ne 0) {
    Write-Host "[错误] Docker 引擎不可用，请确认 Docker Desktop 已启动。" -ForegroundColor Red
    exit 1
}

# ---------- 1) 构建镜像 ----------
Write-Host "`n[1/5] 构建镜像（agent-console / firewall / driver / e2e）..." -ForegroundColor Yellow
docker compose build agent-console firewall driver e2e
if ($LASTEXITCODE -ne 0) { Write-Host "[错误] 镜像构建失败" -ForegroundColor Red; exit 1 }

# ---------- 2) 启动 Agent 控制软件（常驻） ----------
Write-Host "`n[2/5] 启动 Agent 控制软件（端口 8000）..." -ForegroundColor Yellow
docker compose up -d agent-console
if ($LASTEXITCODE -ne 0) { Write-Host "[错误] agent-console 启动失败" -ForegroundColor Red; $fail = 1 }

# 等待健康检查
Write-Host "  等待 agent-console 就绪..."
$up = $false
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Seconds 1
    try {
        $r = Invoke-RestMethod -Uri "http://localhost:8000/api/monitor" -TimeoutSec 2
        if ($r.agents_total -ge 0) { $up = $true; break }
    } catch { }
}
if ($up) {
    Write-Host "  [OK] agent-console 就绪，monitor 接口返回正常。" -ForegroundColor Green
} else {
    Write-Host "  [错误] agent-console 未在预期时间内就绪" -ForegroundColor Red
    $fail = 1
}

# ---------- 3) 运行防火墙模块（一次性） ----------
Write-Host "`n[3/5] 运行防火墙模块（独立容器）..." -ForegroundColor Yellow
$fwOut = docker compose run --rm firewall
$fwCode = $LASTEXITCODE
Write-Host $fwOut
if ($fwCode -eq 0) {
    Write-Host "  [OK] 防火墙模块逻辑验证通过" -ForegroundColor Green
} else {
    Write-Host "  [错误] 防火墙模块验证失败（exit=$fwCode）" -ForegroundColor Red
    $fail = 1
}

# ---------- 4) 运行驱动模块（一次性） ----------
Write-Host "`n[4/5] 运行驱动模块（独立容器）..." -ForegroundColor Yellow
$drvOut = docker compose run --rm driver
$drvCode = $LASTEXITCODE
Write-Host $drvOut
if ($drvCode -eq 0) {
    Write-Host "  [OK] 驱动模块逻辑验证通过" -ForegroundColor Green
} else {
    Write-Host "  [错误] 驱动模块验证失败（exit=$drvCode）" -ForegroundColor Red
    $fail = 1
}

# ---------- 5) 运行端到端综合验证（一次性） ----------
Write-Host "`n[5/5] 运行端到端综合验证（e2e：控制软件+防火墙+驱动）..." -ForegroundColor Yellow
$e2eOut = docker compose run --rm e2e
$e2eCode = $LASTEXITCODE
Write-Host $e2eOut
if ($e2eCode -eq 0) {
    Write-Host "  [OK] 端到端综合验证通过" -ForegroundColor Green
} else {
    Write-Host "  [错误] 端到端综合验证失败（exit=$e2eCode）" -ForegroundColor Red
    $fail = 1
}

# ---------- 汇总 ----------
Write-Host "`n================================================================" -ForegroundColor Cyan
if ($fail -eq 0) {
    Write-Host " 端到端集成测试：ALL PASS" -ForegroundColor Green
    Write-Host " Agent 控制软件:  http://localhost:8000"
    Write-Host " 停止控制软件:    docker compose stop agent-console"
} else {
    Write-Host " 端到端集成测试：存在失败项" -ForegroundColor Red
}
Write-Host "================================================================" -ForegroundColor Cyan
exit $fail
