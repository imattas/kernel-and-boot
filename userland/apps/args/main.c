#include "../../lib/os.h"

static uint64_t length(const char *text) {
    uint64_t result = 0;
    while (text[result]) ++result;
    return result;
}

int args_main(uint64_t argc, char **argv) {
    for (uint64_t index = 1; index < argc; ++index) {
        uint64_t size = length(argv[index]);
        if (os_write(1, argv[index], size) != size) os_exit(1);
        if (index + 1 < argc && os_write(1, " ", 1) != 1) os_exit(1);
    }
    if (os_write(1, "\r\n", 2) != 2) os_exit(1);
    os_exit(0);
}
