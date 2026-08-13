# =============================================================================
# build_verify.ps1 - arm-none-eabi 本地编译 + QEMU 链接/运行验证脚本 (Windows)
#
# 等价于 build_verify.sh。自动探测 STM32CubeIDE 内嵌 arm-none-eabi-gcc，
# 汇编 bsp/stm32h743 的启动/上下文切换模板并链接为 ELF；有 QEMU 时可选运行。
#
# 用法：
#   .\build_verify.ps1                  # 编译+链接（M7 链接验证 + M3 QEMU 镜像）
#   .\build_verify.ps1 -Run             # 若本机有 qemu-system-arm 则继续运行
#   $env:ARM_GCC="D:\gcc\arm-none-eabi-gcc.exe"; .\build_verify.ps1
# =============================================================================
param([switch]$Run)

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoDir   = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$BspDir    = Join-Path $RepoDir "bsp\stm32h743"
$OutDir    = Join-Path $ScriptDir "build"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# ---- 定位 arm-none-eabi-gcc ----
$Gcc = $env:ARM_GCC
if (-not $Gcc -or -not (Test-Path $Gcc)) {
    $found = Get-ChildItem -Path "C:\ST" -Recurse -Filter "arm-none-eabi-gcc.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { $Gcc = $found.FullName }
}
if (-not $Gcc -or -not (Test-Path $Gcc)) {
    Write-Error "未找到 arm-none-eabi-gcc。请安装 Arm GNU Toolchain 或设置环境变量 ARM_GCC"
    exit 2
}
Write-Host "toolchain: $Gcc"
$Bin = Split-Path -Parent $Gcc

function Invoke-Cross {
    param([string[]]$Extra, [string[]]$Objs, [string]$Out)
    & $Gcc @Extra -nostdlib -T (Join-Path $ScriptDir "verify_flash.ld") -o $Out @Objs -lgcc
    if ($LASTEXITCODE -ne 0) { Write-Error "link failed: $Out (exit=$LASTEXITCODE)"; exit $LASTEXITCODE }
}

# ---- 1) Cortex-M7 链接验证 ----
Write-Host "--- [1/3] Cortex-M7 链接验证 ---"
& $Gcc -c -mcpu=cortex-m7 -mthumb -Wall -Wextra -o (Join-Path $OutDir "startup_m7.o") (Join-Path $BspDir "startup_stm32h743.s")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Gcc -c -mcpu=cortex-m7 -mthumb -Wall -Wextra -o (Join-Path $OutDir "portctx_m7.o") (Join-Path $BspDir "port_context_switch.s")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Gcc -c -mcpu=cortex-m7 -mthumb -Os -ffreestanding -Wall -Wextra -I (Join-Path $RepoDir "include") -o (Join-Path $OutDir "verify_main_m7.o") (Join-Path $ScriptDir "verify_main.c")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Invoke-Cross -Extra @("-mcpu=cortex-m7","-mthumb") `
    -Objs @((Join-Path $OutDir "startup_m7.o"),(Join-Path $OutDir "portctx_m7.o"),(Join-Path $OutDir "verify_main_m7.o")) `
    -Out (Join-Path $OutDir "verify_stm32h743.elf")
Write-Host "OK -> $OutDir\verify_stm32h743.elf"

# ---- 2) Cortex-M3 QEMU 运行镜像 ----
Write-Host "--- [2/3] Cortex-M3 QEMU 镜像 ---"
& $Gcc -c -mcpu=cortex-m3 -mthumb -Wall -Wextra -o (Join-Path $OutDir "startup_m3.o") (Join-Path $BspDir "startup_stm32h743.s")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Gcc -c -mcpu=cortex-m3 -mthumb -Wall -Wextra -o (Join-Path $OutDir "portctx_m3.o") (Join-Path $BspDir "port_context_switch.s")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Gcc -c -mcpu=cortex-m3 -mthumb -Os -ffreestanding -Wall -Wextra -I (Join-Path $RepoDir "include") -o (Join-Path $OutDir "verify_main_m3.o") (Join-Path $ScriptDir "verify_main.c")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Invoke-Cross -Extra @("-mcpu=cortex-m3","-mthumb") `
    -Objs @((Join-Path $OutDir "startup_m3.o"),(Join-Path $OutDir "portctx_m3.o"),(Join-Path $OutDir "verify_main_m3.o")) `
    -Out (Join-Path $OutDir "verify_mps2.elf")
Write-Host "OK -> $OutDir\verify_mps2.elf"

# ---- 3) 可选 QEMU 运行 ----
if ($Run) {
    $Qemu = (Get-Command qemu-system-arm -ErrorAction SilentlyContinue)
    if (-not $Qemu) {
        Write-Error "-Run 需要 qemu-system-arm（请安装并加入 PATH）"
        exit 3
    }
    Write-Host "--- [3/3] QEMU 运行 (mps2-an385, Cortex-M3) ---"
    & $Qemu.Source -M mps2-an385 -cpu cortex-m3 -nographic -serial stdio -monitor none `
        -kernel (Join-Path $OutDir "verify_mps2.elf")
}

Write-Host "BUILD VERIFY DONE"
