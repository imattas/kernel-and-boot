#include "../../lib/runtime.h"



int kill_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    uint64_t process_id, signal_number;
    if (argc != 3 || !argv[1] || !argv[2] ||
        !os_userland_parse_u64(argv[1], &process_id) || !os_userland_parse_u64(argv[2], &signal_number) ||
        os_signal_send_to(process_id, signal_number) == OS_SYSCALL_ERROR)
        os_exit(2);
    os_exit(0);
}
