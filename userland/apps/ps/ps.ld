ENTRY(_start)

SECTIONS
{
    . = 0x8000137000;
    .text : ALIGN(0x1000) { *(.text .text.*) }
    .rodata : ALIGN(0x1000) { *(.rodata .rodata.*) }
}
