bits 64

global _start
extern init_main

section .text
_start:
    call init_main
    ud2
