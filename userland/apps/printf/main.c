#include "../../lib/runtime.h"

static int emit(const char *text, uint64_t length) {
    return os_userland_write_all(1, text, length);
}

int printf_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc < 2) os_exit(2);
    const char *format = argv[1];
    uint64_t argument = 2;
    for (uint64_t index = 0; format[index]; ++index) {
        if (format[index] == '\\') {
            char value = format[++index];
            if (!value) break;
            if (value == 'n') value = '\n';
            else if (value == 'r') value = '\r';
            else if (value == 't') value = '\t';
            if (!emit(&value, 1)) os_exit(1);
        } else if (format[index] == '%' && format[index + 1U]) {
            char specifier = format[++index];
            if (specifier == '%') {
                if (!emit("%", 1)) os_exit(1);
            } else if ((specifier == 's' || specifier == 'd') &&
                       argument < argc) {
                const char *value = argv[argument++];
                if (!emit(value, os_userland_length(value))) os_exit(1);
            } else {
                if (!emit("%", 1) || !emit(&specifier, 1)) os_exit(1);
            }
        } else if (!emit(&format[index], 1)) {
            os_exit(1);
        }
    }
    os_exit(0);
}
