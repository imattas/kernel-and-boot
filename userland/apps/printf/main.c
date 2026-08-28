#include "../../lib/runtime.h"

static int emit(const char *text, uint64_t length) {
    return os_userland_write_all(1, text, length);
}

static int emit_number(const char *text, int signed_value, uint64_t base,
                       int uppercase) {
    uint64_t index = 0;
    int negative = 0;
    if (signed_value && text[0] == '-') {
        negative = 1;
        index = 1;
    } else if (signed_value && text[0] == '+') {
        index = 1;
    }
    uint64_t value = 0;
    int digits = 0;
    for (; text[index] >= '0' && text[index] <= '9'; ++index) {
        uint64_t digit = (uint64_t)(text[index] - '0');
        if (value > (UINT64_MAX - digit) / 10U) return 0;
        value = value * 10U + digit;
        digits = 1;
    }
    if (!digits || text[index] != 0) return 0;
    char output[24];
    uint64_t length = 0;
    do {
        uint64_t digit = value % base;
        output[length++] = digit < 10U ? (char)('0' + digit) :
            (char)((uppercase ? 'A' : 'a') + digit - 10U);
        value /= base;
    } while (value != 0);
    if (negative) output[length++] = '-';
    for (uint64_t left = 0, right = length - 1U; left < right;
         ++left, --right) {
        char swap = output[left];
        output[left] = output[right];
        output[right] = swap;
    }
    return emit(output, length);
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
            } else if (specifier == 's' && argument < argc) {
                const char *value = argv[argument++];
                if (!emit(value, os_userland_length(value))) os_exit(1);
            } else if ((specifier == 'd' || specifier == 'i') &&
                       argument < argc) {
                if (!emit_number(argv[argument++], 1, 10U, 0)) os_exit(2);
            } else if ((specifier == 'u' || specifier == 'x' ||
                        specifier == 'X' || specifier == 'o') &&
                       argument < argc) {
                uint64_t base = specifier == 'o' ? 8U :
                    (specifier == 'u' ? 10U : 16U);
                if (!emit_number(argv[argument++], 0, base,
                                 specifier == 'X')) os_exit(2);
            } else if (specifier == 'c' && argument < argc) {
                const char *value = argv[argument++];
                if (!value[0] || !emit(value, 1)) os_exit(2);
            } else {
                if (!emit("%", 1) || !emit(&specifier, 1)) os_exit(1);
            }
        } else if (!emit(&format[index], 1)) {
            os_exit(1);
        }
    }
    os_exit(0);
}
