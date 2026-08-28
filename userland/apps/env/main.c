#include "../../lib/runtime.h"


int env_main(uint64_t argc, char **argv, char **environment) {
    if (!environment) os_exit(1);

    uint64_t command = 1;
    while (command < argc) {
        char *assignment = argv[command];
        uint64_t value_offset = 0;
        if (!os_userland_split_assignment(assignment, &value_offset)) break;
        if (os_setenv(assignment, assignment + value_offset) == OS_SYSCALL_ERROR)
            os_exit(2);
        ++command;
    }
    if (command != 1) {
        if (command == argc) os_exit(0);
        char arguments[256];
        uint64_t arguments_length = 0;
        for (uint64_t index = command + 1U; index < argc; ++index) {
            uint64_t length = os_userland_length(argv[index]);
            if (length == 0 || arguments_length + length + 1U >= sizeof(arguments))
                os_exit(2);
            if (arguments_length != 0) arguments[arguments_length++] = ' ';
            for (uint64_t character = 0; character < length; ++character)
                arguments[arguments_length++] = argv[index][character];
        }
        arguments[arguments_length] = 0;
        uint64_t process = os_spawn(argv[command],
                                    os_userland_length(argv[command]), arguments);
        if (process == OS_SYSCALL_ERROR) os_exit(127);
        int32_t status = 127;
        if (os_wait(process, &status) == OS_SYSCALL_ERROR ||
            os_reap(process) == OS_SYSCALL_ERROR)
            os_exit(127);
        os_exit(status);
    }
    for (uint64_t index = 0; environment[index]; ++index) {
        uint64_t size = os_userland_length(environment[index]);
        if (os_write(1, environment[index], size) != size ||
            os_write(1, "\r\n", 2) != 2) os_exit(1);
    }
    os_exit(0);
}
