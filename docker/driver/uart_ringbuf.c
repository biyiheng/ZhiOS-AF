/*
 * uart_ringbuf.c - UART 环形缓冲驱动实现（非阻塞接收）
 *
 * 逻辑源自 hal_host.c 中修复后的 UART 驱动（环形缓冲 + 短轮询释放 CPU）：
 *   - push：写指针前进；满（head+1==tail）时丢弃，绝不覆盖未读数据。
 *   - pop ：读指针前进；空返回 0。
 * 由于容量为 2 的幂，使用掩码代替取模，性能更高（利于 MCU 热路径）。
 */
#include "uart_ringbuf.h"

static const uint32_t g_mask = UART_RB_SIZE - 1;

void uart_rb_init(UartRingBuf_t *rb)
{
    rb->head = 0;
    rb->tail = 0;
}

int uart_rb_push(UartRingBuf_t *rb, uint8_t byte)
{
    uint32_t nh = (rb->head + 1) & g_mask;
    if (nh == rb->tail) {
        return 0;                      /* 缓冲满，丢弃（非阻塞） */
    }
    rb->buf[rb->head] = byte;
    rb->head = nh;
    return 1;
}

int uart_rb_pop(UartRingBuf_t *rb, uint8_t *byte)
{
    if (rb->head == rb->tail) {
        return 0;                      /* 缓冲空 */
    }
    *byte = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) & g_mask;
    return 1;
}

uint32_t uart_rb_used(const UartRingBuf_t *rb)
{
    return (rb->head - rb->tail) & g_mask;
}

uint32_t uart_rb_free(const UartRingBuf_t *rb)
{
    return UART_RB_SIZE - 1 - uart_rb_used(rb);
}
