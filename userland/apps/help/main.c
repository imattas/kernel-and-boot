#include "../../lib/runtime.h"

int help_main(uint64_t argc, char **argv, char **environment) {
    (void)argv;
    (void)environment;
    static const char commands[] =
        "help id ps env setenv unsetenv status true false jobs history fg "
        "which inherit echo pwd cd ls cat stat chmod kill sleep mv cp mkdir "
        "rm rmdir touch write run wait exit\r\n";
    if (argc != 1 || !os_userland_write_all(1, commands,
                                             sizeof(commands) - 1U))
        os_exit(2);
    os_exit(0);
}
