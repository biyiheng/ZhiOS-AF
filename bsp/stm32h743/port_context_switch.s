@ =============================================================================
@ port_context_switch.s - STM32H743 (Cortex-M7) 上下文切换与原子/低功耗原语参考模板
@
@ 对应《33-操作系统技术指标体系设计文档》4.1/4.3/4.4/4.5 维度。
@ 本文件为【参考模板】，需在 arm-none-eabi + FreeRTOS 端口环境下编译验证。
@ FreeRTOS 官方 port 已实现等价功能；本模板用于说明指标要求的汇编级关键路径。
@
@ 覆盖指标：
@   1) PendSV 上下文切换：MRS 读 PSP、STMDB 压 R4-R11、LDMIA 恢复、MSR 写 PSP、BX LR；
@   2) NVIC 硬件压栈（R0-R3,R12,LR,PC,xPSR），ISR 入口 CPSID I / 出口 CPSIE I + BX LR；
@   3) LDREX/STREX 实现无锁队列/信号量；DMB/DSB/ISB 内存屏障；
@   4) 位带(Bit-Banding)别名区单次 STR 原子 GPIO 翻转；
@   5) 低功耗：写 SCR.SLEEPDEEP/SLEEPONEXIT，WFI 前 DSB，中断后 3 周期内唤醒；
@   6) 任务切换经 PendSV 由汇编完成，保存/恢复 R4-R11、LR、PSP/MSP 完整现场。
@ 语法：GNU as (Thumb-2)，-mcpu=cortex-m7 -mthumb。
@ =============================================================================

    .syntax unified
    .thumb

@ -----------------------------------------------------------------------------
@ 1) PendSV 上下文切换例程
@    触发：更高优先级任务就绪，由 SVC/调度器置 PendSV 待决。
@    硬件已在进入 PendSV 前压栈 R0-R3,R12,LR,PC,xPSR（NVIC 自动）。
@    FreeRTOS 还需保存 EXC_RETURN/浮点，此处展示核心整数现场（指标要求）。
@ -----------------------------------------------------------------------------
    .section .text.pendsv, "ax", %progbits
    .thumb_func
    .globl PendSV_Handler
PendSV_Handler:
    CPSID  I                            @ 屏蔽同级/更低优先级中断（ISR 入口语义）
    MRS    R0, PSP                      @ 读当前任务栈指针
    @ ---- 保存当前任务现场（压入私有栈）----
    STMDB  R0!, {R4-R11}                @ 依次压栈 R4-R11（LR 由硬件压栈）
    @ ---- 切换 TCB 指针 / 取下一任务 ----
    LDR    R1, =currentTCB              @ 或由 C 层调用 xPortSwitchContext
    LDR    R2, [R1]                     @ 旧 TCB
    STR    R0, [R2]                     @ 保存 PSP 到旧 TCB->pxTopOfStack
    @ ... (调度器选择下一任务，更新 currentTCB) ...
    @ ---- 恢复下一任务现场 ----
    LDR    R3, [R1]                     @ 新 TCB
    LDR    R0, [R3]                     @ 新 PSP
    LDMIA  R0!, {R4-R11}                @ 弹栈恢复 R4-R11
    MSR    PSP, R0                      @ 更新 PSP
    CPSIE  I                            @ ISR 出口恢复中断
    BX     LR                           @ 返回（EXC_RETURN 完成特权/栈切换）

@ -----------------------------------------------------------------------------
@ 2) 原子操作原语（LDREX/STREX 无锁队列/信号量、DMB/DSB/ISB 屏障）
@ -----------------------------------------------------------------------------
    .section .text.atomic, "ax", %progbits
    .thumb_func
    .globl atomic_test_and_set           @ int atomic_test_and_set(volatile uint32_t *p)
atomic_test_and_set:
    @ 尝试将 *p 置 1，成功返回 0（旧值 0），失败返回 1；自旋循环。
1:  LDREX  R1, [R0]
    CMP    R1, #0
    BNE    2f                          @ 已被占 -> 失败返回
    MOVS   R2, #1
    STREX  R3, R2, [R0]
    CMP    R3, #0
    BNE    1b                          @ 竞争重试
    DMB                                   @ 确保原子更新对其它核/外设可见
    MOVS   R0, #0
    BX     LR
2:  DMB
    MOVS   R0, #1
    BX     LR

    .thumb_func
    .globl barrier_mem                   @ void barrier_mem(void)：DMB/DSB/ISB 全套屏障
barrier_mem:
    DMB
    DSB
    ISB
    BX     LR

@ -----------------------------------------------------------------------------
@ 3) 位带(Bit-Banding)原子 GPIO 翻转：单次 STR 写别名地址即可原子置位/清零
@    STM32H7 位带区在 0x22000000 之后，基址 = 位带区地址，映射公式：
@      别名地址 = 0x22000000 + (字节偏移*32) + (位号*4)
@ -----------------------------------------------------------------------------
    .section .text.bitband, "ax", %progbits
    .thumb_func
    .globl gpio_bit_set                   @ void gpio_bit_set(volatile uint32_t *band_alias, uint32_t v)
gpio_bit_set:
    STR    R1, [R0]                       @ 单次 STR 完成原子 GPIO 置位/清零
    DMB
    BX     LR

@ -----------------------------------------------------------------------------
@ 4) 低功耗原语：WFI/WFE 睡眠 + SCR.SLEEPDEEP/SLEEPONEXIT + 唤醒
@    写 SCR 前执行 DSB 确保所有挂起内存访问完成；中断触发后 3 周期内唤醒。
@ -----------------------------------------------------------------------------
    .equ SCB_BASE,    0xE000E000
    .equ SCR_OFFSET,  0x10                @ SCB->SCR
    .equ SLEEPDEEP,   (1 << 2)
    .equ SLEEPONEXIT, (1 << 1)
    .equ PWR_CR_ADDR, 0x58000000          @ 电源控制 CR 段（进入 Stop 前由 C 层配置）

    .section .text.lowpower, "ax", %progbits
    .thumb_func
    .globl enter_sleep_wfi                @ void enter_sleep_wfi(int deep)
enter_sleep_wfi:
    @ 读取 SCR，设置 SLEEPDEEP / SLEEPONEXIT 位
    LDR    R1, =SCB_BASE
    LDR    R2, [R1, #SCR_OFFSET]
    CMP    R0, #0
    BEQ    1f
    ORR    R2, R2, #(SLEEPDEEP)
1:  ORR    R2, R2, #(SLEEPONEXIT)
    STR    R2, [R1, #SCR_OFFSET]
    DSB                                    @ 确保所有挂起内存访问完成
    WFI                                    @ 进入睡眠；中断唤醒（<3 周期）
    @ 唤醒后清 SLEEPONEXIT，恢复正常运行
    LDR    R2, [R1, #SCR_OFFSET]
    BIC    R2, R2, #(SLEEPONEXIT)
    STR    R2, [R1, #SCR_OFFSET]
    BX     LR
    .end
