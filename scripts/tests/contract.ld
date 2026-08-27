OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(contract_entry)

SECTIONS {
    . = 0x100000;
    .text : ALIGN(16) {
        KEEP(*(.text.start))
        *(.text*)
    }
    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}
