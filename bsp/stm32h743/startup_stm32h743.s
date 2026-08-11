@ =============================================================================
@ startup_stm32h743.s - STM32H743 (Cortex-M7) 启动参考模板
@
@ 对应《33-操作系统技术指标体系设计文档》4.6"启动流程"维度。
@ 本文件为【参考模板】，需在 arm-none-eabi 工具链 + FreeRTOS/裸机环境下编译验证，
@ 并按实际链接脚本（.ld）调整段地址。生产项目通常由 STM32CubeMX 生成等价 startup 文件，
@ 本模板用于说明指标要求的汇编级关键路径。
@
@ 覆盖指标：
@   1) 向量表置 0x00000000：首字 = 初始 MSP，第二字 = 复位处理指针；
@   2) 复位后：配置系统时钟(RCC)、.bss 清零、.data 从 Flash 复制到 SRAM；
@   3) 异常入口：HardFault/BusFault/MemManage 将完整寄存器镜像保存到专用故障转储区；
@   4) 上电 -> main 时间预算（含时钟配置）需 <=100ms。
@ 语法：GNU as (Thumb-2)，配合 -mcpu=cortex-m7 -mthumb。
@ =============================================================================

    .syntax unified
    .thumb

    .section .isr_vector, "a"
    .globl _estack                     @ 链接脚本提供的栈顶（MSP 初始值）
    .globl Reset_Handler

@ ---- 向量表（0x00000000 起始） ----
    .word _estack                      @ 初始 MSP 栈顶
    .word Reset_Handler                @ 复位处理
    .word NMI_Handler                  @ 2: NMI
    .word HardFault_Handler            @ 3: HardFault（故障转储入口）
    .word MemManage_Handler            @ 4: MemManage（MPU 越界/栈守护）
    .word BusFault_Handler             @ 5: BusFault
    .word UsageFault_Handler           @ 6: UsageFault
    .word 0, 0, 0, 0                   @ 7-10 保留
    .word SVC_Handler                  @ 11: SVC（系统服务调用）
    .word DebugMon_Handler             @ 12
    .word 0                            @ 13 保留
    .word PendSV_Handler               @ 14: PendSV（上下文切换）
    .word SysTick_Handler              @ 15: SysTick（时基）

@ ---- 复位处理：时钟 + 数据段搬运 + BSS 清零 + main ----
    .section .text.Reset_Handler, "ax", %progbits
    .thumb_func
    .globl Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:
    ldr   r0, =_estack                 @ 冗余设置 MSP（向量表已设，此处防御）
    msr   MSP, r0

    @ ---- 配置系统时钟（RCC）----
    @ 生产按 CubeMX SystemClock_Config() 移植；此处为指标占位调用。
    bl    SystemInit                   @ 底层时钟/Flash 等待状态初始化

    @ ---- 复制 .data（LMA=Flash 源，VMA=SRAM 目的）----
    ldr   r0, =_sidata                 @ Flash 中 .data 初始值起始
    ldr   r1, =_sdata                  @ SRAM 中 .data 起始
    ldr   r2, =_edata                  @ SRAM 中 .data 结束
copy_data:
    cmp   r1, r2
    bge   zero_bss
    ldr   r3, [r0], #4
    str   r3, [r1], #4
    b     copy_data

    @ ---- .bss 清零 ----
zero_bss:
    ldr   r1, =_sbss
    ldr   r2, =_ebss
    movs  r3, #0
clear_bss:
    cmp   r1, r2
    bge   call_main
    str   r3, [r1], #4
    b     clear_bss

call_main:
    bl    main                         @ 进入 C 世界；板级初始化在 board_init()
    b     .                            @ main 不应返回

@ ---- 故障异常：保存完整寄存器镜像到专用故障转储区 ----
    .section .text.fault_handlers, "ax", %progbits
    .thumb_func
HardFault_Handler:
    b     fault_dump
    .thumb_func
MemManage_Handler:
    b     fault_dump
    .thumb_func
BusFault_Handler:
    b     fault_dump
    .thumb_func
UsageFault_Handler:
    b     fault_dump

    .global FaultDumpArea              @ 链接脚本/故障转储区（专用 SRAM 段）
fault_dump:
    @ 将 R0-R3,R12,LR,PC,xPSR（硬件已压栈）与 R4-R11 依次存入故障转储区，
    @ 记录故障状态寄存器（CFSR/HFSR/MMFAR/BFAR），随后触发看门狗复位。
    tst   LR, #0x04
    ite   EQ
    mrseq r0, MSP
    mrsne r0, PSP                      @ 判定当前使用 MSP/PSP
    ldr   r1, =FaultDumpArea
    @ ... 汇编级逐寄存器保存（生产实现）
    b     .                            @ 占位：保存完成后进入安全恢复/复位

    .section .text.default_handlers, "ax", %progbits
    .thumb_func
    .weak  NMI_Handler
    .weak  SVC_Handler
    .weak  DebugMon_Handler
    .weak  SysTick_Handler
NMI_Handler:
SVC_Handler:
DebugMon_Handler:
SysTick_Handler:
    bx    lr
    .end
