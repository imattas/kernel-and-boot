#include "../../lib/os.h"

typedef struct {
    char name[32];
    uint32_t type;
} dirent_t;

static uint64_t length(const char *text) {
    uint64_t result = 0;
    while (text[result]) ++result;
    return result;
}

int ls_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc > 2) os_exit(2);
    const char *path = argc == 2 && argv[1] ? argv[1] : ".";
    uint64_t descriptor = os_open(path, length(path), 1);
    if (descriptor == OS_SYSCALL_ERROR) os_exit(1);
    dirent_t entry;
    while (os_readdir(descriptor, &entry) == 1) {
        uint64_t name_length = 0;
        while (name_length < sizeof(entry.name) && entry.name[name_length])
            ++name_length;
        if (os_write(1, entry.name, name_length) != name_length ||
            os_write(1, "\r\n", 2) != 2) {
            (void)os_close(descriptor);
            os_exit(1);
        }
    }
    if (os_close(descriptor) == OS_SYSCALL_ERROR) os_exit(1);
    os_exit(0);
}
