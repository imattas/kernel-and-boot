#include "../lib/os.h"

void init_main(void) {
    static const char shell_path[] = "/shell.elf";
    for (;;) {
        uint64_t process = os_spawn(shell_path, sizeof(shell_path) - 1U, 0);
        if (process == OS_SYSCALL_ERROR) os_exit(1);
        int32_t status = 1;
        if (os_wait(process, &status) == OS_SYSCALL_ERROR ||
            os_reap(process) == OS_SYSCALL_ERROR) os_exit(1);
    }
}
