bits 16

section .text.smp_trampoline
global smp_trampoline_start
global smp_trampoline_end
global smp_trampoline_gdt_base
global smp_trampoline_gdt_start
global smp_trampoline_pml4
global smp_trampoline_stack
global smp_trampoline_entry
global smp_trampoline_argument

smp_trampoline_start:
    cli
    xor ax, ax
    mov ds, ax
    call .get_base
.get_base:
    pop bx
    sub bx, .get_base - smp_trampoline_start
    add bx, 0x8000
    mov si, gdt_ptr - smp_trampoline_start
    add si, bx
    lgdt [si]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    mov ax, bx
    add ax, protected_mode - smp_trampoline_start
    mov si, 0x08
    push si
    push ax
    retf

bits 32
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x7000
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax
    mov ecx, 0xc0000080
    rdmsr
    or eax, 0x100
    wrmsr
    mov eax, [ebx + smp_trampoline_pml4 - smp_trampoline_start]
    mov cr3, eax
    mov eax, cr0
    and eax, 0x9fffffff
    or eax, 0x80000001
    mov cr0, eax
    jmp 0x18:(0x8000 + long_mode - smp_trampoline_start)

bits 64
long_mode:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, [rbx + smp_trampoline_stack - smp_trampoline_start]
    mov rdi, [rbx + smp_trampoline_argument - smp_trampoline_start]
    call [rbx + smp_trampoline_entry - smp_trampoline_start]
.halt:
    cli
    hlt
    jmp .halt

align 8
smp_trampoline_gdt_start:
    dq 0
    dq 0x00cf9a000000ffff
    dq 0x00cf92000000ffff
    dq 0x00af9a000000ffff
    dq 0x00af92000000ffff
gdt_end:
gdt_ptr:
    dw gdt_end - smp_trampoline_gdt_start - 1
smp_trampoline_gdt_base:
    dd 0

align 8
smp_trampoline_pml4: dq 0
smp_trampoline_stack: dq 0
smp_trampoline_entry: dq 0
smp_trampoline_argument: dq 0
smp_trampoline_end:
