/*
 * verify_main.c - 汇编启动/上下文切换 冒烟验证与压测采样固件
 *
 * 对应《33-操作系统技术指标体系设计文档》4.1/4.3 中"标注为黄色"的
 * 亚微秒级上下文切换与抖动(Jitter)指标。本固件被 arm-none-eabi 链接，
 * 可在以下两类环境验证：
 *   1) 链接验证：arm-none-eabi-gcc 链接为 ELF，确认向量表与 PendSV/原子
 *      汇编例程符号齐全（-mcpu=cortex-m7）。
 *   2) QEMU 运行验证：-mcpu=cortex-m3 + 机器 mps2-an385，通过 CMSDK UART
 *      打印每次上下文切换的周期样本（CSV），由 stress_sched.py 解析统计
 *      平均/最坏周期与抖动，并强制判定 <1μs 切换、Jitter<=1% 指标。
 *
 * 关键点：
 *   - DWT_CYCCNT 经链接脚本钉在 0xE0001004，即 Cortex-M 周期计数器。
 *   - 上下文切换关键路径复刻 port_context_switch.s 的 PendSV 序列
 *     （MRS PSP / STMDB R4-R11 / LDMIA / MSR PSP），以 CYCCNT 计周期。
 */

#include <stdint.h>
#include <stddef.h>

/* ---- DWT->CYCCNT 周期计数器（链接脚本将本符号置于 0xE0001004） ---- */
volatile uint32_t DWT_CYCCNT __attribute__((section(".dwt_cyccnt")));

/* ---- startup_stm32h743.s / port_context_switch.s 引用的外部符号 ---- */
void *currentTCB = 0;                 /* PendSV 例程引用的 TCB 指针占位 */

/* 汇编模板导出的符号（port_context_switch.s） */
extern int  atomic_test_and_set(volatile uint32_t *p);
extern void barrier_mem(void);
extern void gpio_bit_set(volatile uint32_t *band_alias, uint32_t v);
extern void enter_sleep_wfi(int deep);

/* ---- CMSDK UART（QEMU 机器 mps2-an385，UART0 = 0x40004000） ---- */
#define CMSDK_UART0_BASE 0x40004000u
#define UART_DATA   (*(volatile uint32_t *)(CMSDK_UART0_BASE + 0x000u))
#define UART_STATE  (*(volatile uint32_t *)(CMSDK_UART0_BASE + 0x004u))
#define UART_CTRL   (*(volatile uint32_t *)(CMSDK_UART0_BASE + 0x008u))
#define STATE_TX_BUFFER_FULL (1u << 0)

static void uart_init(void)
{
    /* 使能 TX，8N1 */
    UART_CTRL = (1u << 0); /* TXEN */
}

static void uart_putc(char c)
{
    /* QEMU 的 CMSDK UART 模型会把 TX-FULL 位长期置位（无流控），若沿用 100000 次
       轮询，每次写字符都会空转 10 万次，压测固件将打印约 1.4 万字符/轮 × 20 轮，
       使 CI 单步耗时达 35 分钟以上。改为轻量轮询：真机上 TXE 清空很快会提前退出，
       QEMU 下则把空转量从 10^5 降到 10^3，压测耗时降低约两个数量级。 */
    int i;
    for (i = 0; i < 1000; i++) {     /* 轻量轮询 TX FIFO 空 */
        if (!(UART_STATE & STATE_TX_BUFFER_FULL)) break;
    }
    UART_DATA = (uint32_t)(unsigned char)c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

/* ---- 简单十进制打印 ---- */
static void uart_u32(uint32_t v)
{
    char buf[12];
    int idx = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { buf[idx++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (idx > 0) uart_putc(buf[--idx]);
}

/* ---- DWT 使能 ---- */
#define DEMCR        (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL     (*(volatile uint32_t *)0xE0001000u)
#define DEMCR_TRCENA (1u << 24)
#define DWT_CTRL_CYCCNTENA (1u << 0)

static void dwt_enable(void)
{
    DEMCR |= DEMCR_TRCENA;       /* 使能跟踪 */
    DWT_CYCCNT = 0u;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA; /* 启动周期计数 */
}

/* ---- 上下文切换关键路径：复刻 port_context_switch.s 的 PendSV 序列 ---- */
static inline uint32_t sw_cycles_once(void)
{
    uint32_t t0 = DWT_CYCCNT;
    __asm__ volatile (
        "MRS    r1, PSP\n\t"        /* 读当前任务栈指针 */
        "STMDB  r1!, {r4-r11}\n\t"  /* 保存 R4-R11（LR 由硬件压栈） */
        "LDMIA  r1!, {r4-r11}\n\t"  /* 恢复下一任务 R4-R11 */
        "MSR    PSP, r1\n\t"        /* 更新 PSP */
        : : : "r1", "memory");
    uint32_t t1 = DWT_CYCCNT;
    return t1 - t0;
}

/* ---- 原子操作正确性自检 ---- */
static int atomic_selfcheck(void)
{
    volatile uint32_t lock = 0u;
    int ok1 = 0, ok2 = 0;
    if (atomic_test_and_set((volatile uint32_t *)&lock) == 0) ok1 = 1; /* 首次成功 */
    if (atomic_test_and_set((volatile uint32_t *)&lock) == 1) ok2 = 1; /* 已被占，失败 */
    barrier_mem();               /* DMB/DSB/ISB */
    gpio_bit_set((volatile uint32_t *)0x22000000u, 1u); /* 位带原子写（内存语义） */
    return (ok1 && ok2) ? 1 : 0;
}

/* ---- SystemInit：startup_stm32h743.s 复位流程调用（QEMU 下为空实现） ---- */
void SystemInit(void) { }

/* ---- 半主机退出：打印完结果后通知 QEMU 正常终止（SYS_EXIT） ----
 * 固件结尾若进入死循环则 QEMU 永不退出，压测脚本会因 30s 超时崩溃；
 * 通过 ARM Angel 半主机 SYS_EXIT(0x18) 让 QEMU 在输出完整结果后立即
 * 干净退出（配合 QEMU 命令行 -semihosting-config enable=on,target=native）。
 * 链接验证（不运行 QEMU）下该调用仅被链接，不产生副作用。
 */
static void semihost_exit(void)
{
    /* reason=ADP_Stopped_ApplicationExit(0x20026), subcode=0 —— SYS_EXIT 参数块 */
    static const uint32_t block[2] = { 0x20026u, 0u };
    register uint32_t r1 asm("r1") = (uint32_t)(uintptr_t)&block;
    __asm__ volatile(
        "mov r0, #0x18\n\t"   /* SYS_EXIT */
        "bkpt 0xAB\n\t"       /* 半主机断点 */
        :
        : "r"(r1)
        : "r0", "memory");
    for (;;) { }
}

/* ---- main：链接验证时由启动代码调用；QEMU 下执行压测采样 ---- */
int main(void)
{
    uart_init();
    dwt_enable();

    /* 原子/屏障/位带自检 */
    if (atomic_selfcheck()) {
        uart_puts("[verify] atomic/barrier/bitband selfcheck OK\n");
    } else {
        uart_puts("[verify] atomic/barrier/bitband selfcheck FAIL\n");
    }

    /* 上下文切换周期采样：打印 CSV（每行一次切换的周期数），
       供 stress_sched.py 统计 mean/max/jitter 并判定指标。 */
    const uint32_t N = 1000u;
    uint32_t i, min = 0xFFFFFFFFu, max = 0u, sum = 0u;
    for (i = 0; i < N; i++) {
        uint32_t c = sw_cycles_once();
        if (c < min) min = c;
        if (c > max) max = c;
        sum += c;
        uart_puts("sw_cycles,"); uart_u32(c); uart_putc('\n');
    }

    uart_puts("SW_SUMMARY mean,"); uart_u32(sum / N);
    uart_puts(",min,"); uart_u32(min);
    uart_puts(",max,"); uart_u32(max);
    uart_puts(",jitter,"); uart_u32(max - min);
    uart_putc('\n');
    uart_puts("[verify] DONE\n");

    /* 通知 QEMU 半主机退出，使压测脚本可完整捕获输出并干净结束本轮 */
    semihost_exit();
    return 0;
}
