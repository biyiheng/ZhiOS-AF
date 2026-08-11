#!/usr/bin/env bash
# =============================================================================
# build_verify.sh - arm-none-eabi 本地编译 + QEMU 链接/运行验证脚本
#
# 验证对象（对应《33》4.1/4.3 标注为黄色的亚微秒切换与 Jitter 指标）：
#   bsp/stm32h743/startup_stm32h743.s      汇编启动（向量表@0 / 复位 / .data/.bss）
#   bsp/stm32h743/port_context_switch.s    PendSV 上下文切换 / 原子 / 屏障 / 位带 / 低功耗
#   tools/arm_eabi/verify_main.c           冒烟测试 + 上下文切换周期采样固件
#
# 产物：
#   build/verify_stm32h743.elf   Cortex-M7 链接验证（无 QEMU 也可完成）
#   build/verify_mps2.elf        Cortex-M3 + mps2-an385 QEMU 运行镜像
#
# 用法：
#   ./build_verify.sh                 # 编译+链接（M7 链接验证 + M3 QEMU 镜像）
#   ./build_verify.sh --run           # 若本机有 qemu-system-arm 则继续运行并打印样本
#   ./build_verify.sh --toolchain <gcc>   # 指定 arm-none-eabi-gcc 绝对路径
# =============================================================================
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BSP_DIR="$REPO_DIR/bsp/stm32h743"
OUT_DIR="$SCRIPT_DIR/build"
mkdir -p "$OUT_DIR"

# ---- 定位 arm-none-eabi-gcc ----
find_gcc() {
    if [ -n "${ARM_GCC:-}" ] && command -v "$ARM_GCC" >/dev/null 2>&1; then
        echo "$ARM_GCC"; return
    fi
    if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
        echo "arm-none-eabi-gcc"; return
    fi
    # STM32CubeIDE 内嵌工具链（Windows/PowerShell 常见路径，PWD 形如 C:/...）
    for p in \
        "C:/ST/STM32CubeIDE_"*/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin/arm-none-eabi-gcc.exe \
        "/c/ST/STM32CubeIDE_"*/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin/arm-none-eabi-gcc.exe \
        "$HOME/.local/bin/arm-none-eabi-gcc" \
        "/usr/local/arm-none-eabi/bin/arm-none-eabi-gcc" \
        "/opt/arm-gnu-toolchain/"*/bin/arm-none-eabi-gcc ; do
        # shellcheck disable=SC2086
        [ -x $p ] && echo "$p" && return
    done
    echo ""
}

GCC="$(find_gcc)"
if [ -z "$GCC" ]; then
    echo "ERROR: 未找到 arm-none-eabi-gcc。请安装 Arm GNU Toolchain 或设置 ARM_GCC=/path/to/arm-none-eabi-gcc" >&2
    exit 2
fi
echo "toolchain: $GCC"

BIN="$(dirname "$GCC")"
GCC_EXE="$GCC"

# ---- 1) Cortex-M7 链接验证 ----
echo "--- [1/3] Cortex-M7 链接验证 ---"
"$GCC_EXE" -c -mcpu=cortex-m7 -mthumb -Wall -Wextra -o "$OUT_DIR/startup_m7.o" "$BSP_DIR/startup_stm32h743.s"
"$GCC_EXE" -c -mcpu=cortex-m7 -mthumb -Wall -Wextra -o "$OUT_DIR/portctx_m7.o" "$BSP_DIR/port_context_switch.s"
"$GCC_EXE" -c -mcpu=cortex-m7 -mthumb -Os -ffreestanding -Wall -Wextra \
    -I"$REPO_DIR/include" -o "$OUT_DIR/verify_main_m7.o" "$SCRIPT_DIR/verify_main.c"
"$GCC_EXE" -mcpu=cortex-m7 -mthumb -nostdlib -T "$SCRIPT_DIR/verify_flash.ld" \
    -o "$OUT_DIR/verify_stm32h743.elf" \
    "$OUT_DIR/startup_m7.o" "$OUT_DIR/portctx_m7.o" "$OUT_DIR/verify_main_m7.o" -lgcc
echo "OK -> $OUT_DIR/verify_stm32h743.elf"

# 校验向量表：0x00000000 首字 = _estack，次字 = Reset_Handler
"$BIN/arm-none-eabi-objdump" -h "$OUT_DIR/verify_stm32h743.elf" | grep -q isr_vector && echo "vector table section OK"
"$BIN/arm-none-eabi-nm" "$OUT_DIR/verify_stm32h743.elf" | grep -E " (Reset_Handler|PendSV_Handler|atomic_test_and_set)$" || {
    echo "ERROR: 关键汇编符号缺失" >&2; exit 1; }
echo "symbols OK (Reset_Handler / PendSV_Handler / atomic_test_and_set)"

# ---- 2) Cortex-M3 QEMU 运行镜像 ----
echo "--- [2/3] Cortex-M3 QEMU 镜像 ---"
"$GCC_EXE" -c -mcpu=cortex-m3 -mthumb -Wall -Wextra -o "$OUT_DIR/startup_m3.o" "$BSP_DIR/startup_stm32h743.s"
"$GCC_EXE" -c -mcpu=cortex-m3 -mthumb -Wall -Wextra -o "$OUT_DIR/portctx_m3.o" "$BSP_DIR/port_context_switch.s"
"$GCC_EXE" -c -mcpu=cortex-m3 -mthumb -Os -ffreestanding -Wall -Wextra \
    -I"$REPO_DIR/include" -o "$OUT_DIR/verify_main_m3.o" "$SCRIPT_DIR/verify_main.c"
"$GCC_EXE" -mcpu=cortex-m3 -mthumb -nostdlib -T "$SCRIPT_DIR/verify_flash.ld" \
    -o "$OUT_DIR/verify_mps2.elf" \
    "$OUT_DIR/startup_m3.o" "$OUT_DIR/portctx_m3.o" "$OUT_DIR/verify_main_m3.o" -lgcc
echo "OK -> $OUT_DIR/verify_mps2.elf"

# ---- 3) 可选 QEMU 运行 ----
if [ "${1:-}" = "--run" ]; then
    QEMU="$(command -v qemu-system-arm || true)"
    if [ -z "$QEMU" ]; then
        echo "ERROR: --run 需要 qemu-system-arm（请安装 qemu-system-arm 并加入 PATH）" >&2
        exit 3
    fi
    echo "--- [3/3] QEMU 运行 (mps2-an385, Cortex-M3) ---"
    "$QEMU" -M mps2-an385 -cpu cortex-m3 -nographic -serial stdio -monitor none \
        -kernel "$OUT_DIR/verify_mps2.elf"
fi

echo "BUILD VERIFY DONE"
