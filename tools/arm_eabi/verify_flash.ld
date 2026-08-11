/*
 * verify_flash.ld - 汇编启动/上下文切换 链接验证 + QEMU 运行验证 通用链接脚本
 *
 * 用途：
 *   1) 链接验证（arm-none-eabi，-mcpu=cortex-m7）：确认 startup_stm32h743.s 与
 *      port_context_switch.s 可与 C 冒烟测试链接为合法 ELF，向量表置于 0x00000000。
 *   2) QEMU 运行验证（-mcpu=cortex-m3，机器 mps2-an385）：同一份源码链接后烧入
 *      QEMU，通过 CMSDK UART 打印上下文切换周期/抖动样本，供 stress_sched.py 解析。
 *
 * 说明：DWT->CYCCNT 位于固定地址 0xE0001004，此处用 .dwt_cyccnt 段把它"钉"到
 *       该地址，使 C 代码中的 `DWT_CYCCNT` 直接映射到硬件周期计数器寄存器。
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

/* 栈顶（MSP 初始值）：RAM 顶端 */
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    /* 向量表置于 0x00000000（FLASH 起始），首字 = _estack，次字 = Reset_Handler */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        . = ALIGN(4);
    } > FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /* .data：LMA 在 Flash，VMA 在 SRAM（启动代码负责从 _sidata 搬运到 _sdata） */
    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> FLASH

    /* .bss：启动代码清零 [_sbss, _ebss) */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* DWT->CYCCNT 周期计数器寄存器（固定地址 0xE0001004） */
    .dwt_cyccnt 0xE0001004 (NOLOAD) :
    {
        KEEP(*(.dwt_cyccnt))
    }

    /* 故障转储区符号（startup_stm32h743.s 的 fault_dump 引用） */
    FaultDumpArea = 0x20010000;
}
