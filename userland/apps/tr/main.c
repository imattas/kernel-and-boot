#include "../../lib/runtime.h"

int tr_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 3) os_exit(2);
    uint64_t from_length = os_userland_length(argv[1]);
    uint64_t to_length = os_userland_length(argv[2]);
    if (from_length == 0 || to_length == 0) os_exit(2);
    char input[256];
    for (;;) {
        uint64_t count = os_read(0, input, sizeof(input));
        if (count == OS_SYSCALL_ERROR) os_exit(1);
        if (count == 0) break;
        for (uint64_t index = 0; index < count; ++index) {
            char value = input[index];
            for (uint64_t source = 0; source < from_length; ++source) {
                if (value == argv[1][source]) {
                    value = argv[2][source < to_length ? source : to_length - 1U];
                    break;
                }
            }
            if (!os_userland_write_all(1, &value, 1)) os_exit(1);
        }
    }
    os_exit(0);
}
