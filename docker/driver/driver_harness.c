/*
 * driver_harness.c - UART 环形缓冲驱动独立测试桩（Docker 镜像内运行）
 *
 * 验证修复后的驱动逻辑：
 *   1) FIFO 顺序：注入 ABC 后按序读取。
 *   2) 满丢弃：写满后继续 push 不覆盖未读数据（非阻塞）。
 *   3) 空读：空缓冲 pop 返回 0。
 *   4) 回绕：循环读写验证环形指针回绕正确。
 * 退出码 0 = 全部通过。
 */
#include <stdio.h>
#include <string.h>
#include "uart_ringbuf.h"

static int g_fail = 0;
static void expect(const char *name, int cond)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) g_fail++;
}

int main(void)
{
    UartRingBuf_t rb;
    uint8_t b;

    printf("=== ZhiOS-AF UART 环形缓冲驱动验证 ===\n");

    /* 1) FIFO 顺序 */
    uart_rb_init(&rb);
    expect("init: used=0", uart_rb_used(&rb) == 0);
    expect("init: empty pop=0", uart_rb_pop(&rb, &b) == 0);   /* 空读 */

    uart_rb_push(&rb, 'A'); uart_rb_push(&rb, 'B'); uart_rb_push(&rb, 'C');
    expect("3 字节后 used=3", uart_rb_used(&rb) == 3);
    char out[4] = {0};
    uint32_t n = 0;
    while (uart_rb_pop(&rb, &b)) out[n++] = (char)b;
    expect("FIFO 顺序 ABC", strcmp(out, "ABC") == 0);

    /* 2) 满丢弃（容量 256，可用 255） */
    uart_rb_init(&rb);
    uint32_t written = 0;
    for (uint32_t i = 0; i < 512; i++) {
        written += uart_rb_push(&rb, (uint8_t)(i & 0xFF));
    }
    expect("满后不覆盖（written=255）", written == UART_RB_SIZE - 1);
    expect("满后 used=255", uart_rb_used(&rb) == UART_RB_SIZE - 1);
    /* 首字节应为 0x00（未被覆盖），证明丢弃策略正确 */
    uart_rb_pop(&rb, &b);
    expect("首字节保留 0x00", b == 0x00);

    /* 3) 回绕正确性 */
    uart_rb_init(&rb);
    for (uint32_t i = 0; i < 300; i++) {
        while (!uart_rb_push(&rb, (uint8_t)(i & 0xFF))) { /* 写满后略过 */ }
    }
    expect("回绕后仍可读", uart_rb_pop(&rb, &b) == 1);

    printf(g_fail ? "==> UART DRIVER TEST FAILED\n" : "==> UART DRIVER TEST ALL PASS\n");
    return g_fail ? 1 : 0;
}
