#include "../../lib/os.h"

int dup_main(uint64_t argc, char **argv, char **environment) {
    (void)argv;
    (void)environment;
    char data[8] = {0};
    uint64_t source = os_open("KERNEL.ELF", 10, 1);
    uint64_t copy = source == OS_SYSCALL_ERROR ? OS_SYSCALL_ERROR :
                    os_dup(source, 1);
    if (argc != 1 || source == OS_SYSCALL_ERROR || copy == OS_SYSCALL_ERROR ||
        os_close(source) == OS_SYSCALL_ERROR ||
        os_file_read(copy, data, sizeof(data)) == OS_SYSCALL_ERROR ||
        os_close(copy) == OS_SYSCALL_ERROR)
        os_exit(1);
    os_exit(0);
}
