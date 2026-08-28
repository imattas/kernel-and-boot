ENTRY(_start)

SECTIONS
{
    . = 0x80000e1000;
    .text : ALIGN(0x1000) { *(.text .text.*) }
    .rodata : ALIGN(0x1000) { *(.rodata .rodata.*) }
}
