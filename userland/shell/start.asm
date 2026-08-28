bits 64

global _start
extern shell_main

section .text
_start:
    call shell_main
    ud2
