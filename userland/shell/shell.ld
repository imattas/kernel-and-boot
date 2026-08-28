ENTRY(_start)

SECTIONS
{
    . = 0x8000021000;
    .text : ALIGN(0x1000) { *(.text .text.*) }
    .rodata : ALIGN(0x1000) { *(.rodata .rodata.*) }
    .bss : ALIGN(0x1000) { *(.bss .bss.*) }
}
