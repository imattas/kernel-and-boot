#include "../../lib/runtime.h"

static int parse_number(const char *text, int64_t *value) {
    if (!text || !text[0] || !value) return 0;
    uint64_t index = 0;
    int negative = text[0] == '-';
    if (negative) {
        if (!text[1]) return 0;
        index = 1;
    }
    uint64_t magnitude = 0;
    for (; text[index]; ++index) {
        if (text[index] < '0' || text[index] > '9') return 0;
        uint64_t digit = (uint64_t)(text[index] - '0');
        uint64_t limit = negative ? UINT64_C(9223372036854775808) :
                                    UINT64_C(9223372036854775807);
        if (magnitude > (limit - digit) / 10U) return 0;
        magnitude = magnitude * 10U + digit;
    }
    *value = negative ? (magnitude == UINT64_C(9223372036854775808) ?
        INT64_MIN : -(int64_t)magnitude) : (int64_t)magnitude;
    return 1;
}

static int write_number(int64_t value) {
    char digits[20];
    uint32_t length = 0;
    uint64_t magnitude;
    if (value < 0) {
        if (!os_userland_write_all(1, "-", 1)) return 0;
        magnitude = (uint64_t)(-(value + 1)) + 1U;
    } else magnitude = (uint64_t)value;
    do {
        digits[length++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0);
    while (length != 0 &&
           !os_userland_write_all(1, &digits[--length], 1)) return 0;
    return os_userland_write_all(1, "\r\n", 2);
}

int expr_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    if (argc != 4) os_exit(2);
    int64_t left, right, result;
    if (!parse_number(argv[1], &left) || !parse_number(argv[3], &right) ||
        os_userland_length(argv[2]) != 1) os_exit(2);
    switch (argv[2][0]) {
        case '+': if (__builtin_add_overflow(left, right, &result)) os_exit(2); break;
        case '-': if (__builtin_sub_overflow(left, right, &result)) os_exit(2); break;
        case '*': if (__builtin_mul_overflow(left, right, &result)) os_exit(2); break;
        case '/': if (right == 0 || (left == INT64_MIN && right == -1)) os_exit(2);
                  result = left / right; break;
        case '%': if (right == 0 || (left == INT64_MIN && right == -1)) os_exit(2);
                  result = left % right; break;
        default: os_exit(2);
    }
    if (!write_number(result)) os_exit(1);
    os_exit(0);
}
