#include <stdint.h>
#include "exceptions.h"
#include "../../../core/panic/panic.h"
void arch_exception_panic(uint64_t vector) {
    kernel_panic_exception(vector);
}
