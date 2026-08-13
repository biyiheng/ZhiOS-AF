/*
 * uart_ringbuf.h - UART 环形缓冲驱动接口（独立可移植模块）
 *
 * 对应《14-硬件抽象层-HAL接口规范》与 hal_host.c 中修复后的 UART 驱动逻辑。
 * 本驱动将 UART 接收改为"环形缓冲 + 非阻塞"模型：中断/注入将数据写入缓冲，
 * 应用层轮询读取，避免忙等阻塞，提升 CPU 利用率与响应效率。
 * 可作为任意平台 UART 驱动的模板（可替换模块）。
 */
#ifndef UART_RINGBUF_H
#define UART_RINGBUF_H

#include <stdint.h>

/* 环形缓冲容量（2 的幂，简化掩码运算） */
#define UART_RB_SIZE 256u

typedef struct {
    uint8_t  buf[UART_RB_SIZE];
    uint32_t head;      /* 写指针 */
    uint32_t tail;      /* 读指针 */
} UartRingBuf_t;

void    uart_rb_init(UartRingBuf_t *rb);
int     uart_rb_push(UartRingBuf_t *rb, uint8_t byte);   /* 满返回 0 */
int     uart_rb_pop(UartRingBuf_t *rb, uint8_t *byte);   /* 空返回 0 */
uint32_t uart_rb_used(const UartRingBuf_t *rb);
uint32_t uart_rb_free(const UartRingBuf_t *rb);

#endif /* UART_RINGBUF_H */
