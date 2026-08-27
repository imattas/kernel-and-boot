#include <stdint.h>
#include "exceptions.h"
#include "../../../core/panic/panic.h"

typedef struct {
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
} exception_frame_t;

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
} exception_registers_t;

void arch_exception_panic(uint64_t vector) {
    kernel_panic_exception(vector);
}

__attribute__((noreturn)) void arch_exception_panic_frame(
    uint64_t vector, const exception_frame_t *frame,
    const exception_registers_t *registers) {
    if (!frame) kernel_panic_exception(vector);
    kernel_panic_exception_frame(vector, frame->error_code, frame->rip,
                                 frame->cs, frame->rflags,
                                 (const uint64_t *)registers);
}
