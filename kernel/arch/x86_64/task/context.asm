bits 64

global task_context_switch
global arch_task_bootstrap
global arch_user_task_bootstrap
extern scheduler_task_exit
extern arch_user_task_start

task_context_switch:
    lea rax, [rsp + 8]
    mov [rdi + 0], rax
    mov rax, [rsp]
    mov [rdi + 8], rax
    mov [rdi + 16], rbx
    mov [rdi + 24], rbp
    mov [rdi + 32], r12
    mov [rdi + 40], r13
    mov [rdi + 48], r14
    mov [rdi + 56], r15
    mov rsp, [rsi + 0]
    mov rbx, [rsi + 16]
    mov rbp, [rsi + 24]
    mov r12, [rsi + 32]
    mov r13, [rsi + 40]
    mov r14, [rsi + 48]
    mov r15, [rsi + 56]
    jmp [rsi + 8]

arch_task_bootstrap:
    sti
    mov rdi, r12
    call r13
    call scheduler_task_exit
.halt:
    cli
    hlt
    jmp .halt

arch_user_task_bootstrap:
    mov rdi, r12
    mov rsi, r13
    mov rdx, r14
    jmp arch_user_task_start
