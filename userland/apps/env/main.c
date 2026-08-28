#include "../../lib/os.h"

static uint64_t length(const char *text) {
    uint64_t result = 0;
    while (text[result]) ++result;
    return result;
}

int env_main(uint64_t argc, char **argv, char **environment) {
    (void)argc;
    (void)argv;
    if (!environment) os_exit(1);
    for (uint64_t index = 0; environment[index]; ++index) {
        uint64_t size = length(environment[index]);
        if (os_write(1, environment[index], size) != size ||
            os_write(1, "\r\n", 2) != 2) os_exit(1);
    }
    os_exit(0);
}
