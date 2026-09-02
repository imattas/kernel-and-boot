#include "../lib/os.h"

static void init_backoff(void) {
    for (uint32_t attempt = 0; attempt < 256U; ++attempt)
        (void)os_yield();
}

static int init_wait_and_reap(uint64_t process) {
    int32_t status = 1;
    for (;;) {
        if (os_wait(process, &status) == 0) break;
        (void)os_yield();
    }
    for (;;) {
        if (os_reap(process) == 0) return 1;
        (void)os_yield();
    }
}

void init_main(void) {
    static const char shell_path[] = "/shell.elf";
    for (;;) {
        uint64_t process = os_spawn(shell_path, sizeof(shell_path) - 1U, 0);
        if (process == OS_SYSCALL_ERROR) {
            init_backoff();
            continue;
        }
        (void)init_wait_and_reap(process);
    }
}
