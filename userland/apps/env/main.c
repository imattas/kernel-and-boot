#include "../../lib/runtime.h"


int env_main(uint64_t argc, char **argv, char **environment) {
    (void)argc;
    (void)argv;
    if (!environment) os_exit(1);
    for (uint64_t index = 0; environment[index]; ++index) {
        uint64_t size = os_userland_length(environment[index]);
        if (os_write(1, environment[index], size) != size ||
            os_write(1, "\r\n", 2) != 2) os_exit(1);
    }
    os_exit(0);
}
