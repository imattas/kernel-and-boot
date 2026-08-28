#include "../../lib/runtime.h"

static int exists(const char *path) {
    uint64_t length = os_userland_length(path);
    uint64_t descriptor = os_open(path, length, 1);
    if (descriptor == OS_SYSCALL_ERROR) return 0;
    return os_close(descriptor) != OS_SYSCALL_ERROR;
}

static int type_is(const char *path, uint32_t type) {
    uint64_t descriptor = os_open(path, os_userland_length(path), 1);
    if (descriptor == OS_SYSCALL_ERROR) return 0;
    os_stat_t stat;
    int result = os_fstat(descriptor, &stat) != OS_SYSCALL_ERROR &&
                 stat.type == type;
    (void)os_close(descriptor);
    return result;
}

static int numeric_compare(const char *left, const char *operator,
                           const char *right) {
    uint64_t a;
    uint64_t b;
    if (!os_userland_parse_u64(left, &a) ||
        !os_userland_parse_u64(right, &b)) return 0;
    if (operator[0] != '-' || operator[3] != 0) return 0;
    if (operator[1] == 'e' && operator[2] == 'q') return a == b;
    if (operator[1] == 'n' && operator[2] == 'e') return a != b;
    if (operator[1] == 'l' && operator[2] == 't') return a < b;
    if (operator[1] == 'l' && operator[2] == 'e') return a <= b;
    if (operator[1] == 'g' && operator[2] == 't') return a > b;
    if (operator[1] == 'g' && operator[2] == 'e') return a >= b;
    return 0;
}

static int predicate(uint64_t argc, char **argv) {
    if (argc == 2) {
        if (argv[1][0] == 0) return 0;
        return exists(argv[1]);
    }
    if (argc == 3 && argv[1][0] == '-' && argv[1][1] == 'e' &&
        argv[1][2] == 0) return exists(argv[2]);
    if (argc == 3 && argv[1][0] == '-' &&
        (argv[1][1] == 'f' || argv[1][1] == 'd') && argv[1][2] == 0)
        return type_is(argv[2], argv[1][1] == 'd' ? 0U : 1U);
    if (argc == 3 && argv[1][0] == '-' && argv[1][1] == 'n' &&
        argv[1][2] == 0) return argv[2][0] != 0;
    if (argc == 3 && argv[1][0] == '-' && argv[1][1] == 'z' &&
        argv[1][2] == 0) return argv[2][0] == 0;
    if (argc == 4 && argv[1][0] != 0 &&
        ((argv[2][0] == '=' && argv[2][1] == 0) ||
         (argv[2][0] == '!' && argv[2][1] == '=' && argv[2][2] == 0))) {
        uint64_t left = os_userland_length(argv[1]);
        uint64_t right = os_userland_length(argv[3]);
        int equal = left == right;
        for (uint64_t index = 0; equal && index < left; ++index)
            equal = argv[1][index] == argv[3][index];
        return argv[2][1] == '!' ? !equal : equal;
    }
    if (argc == 4)
        return numeric_compare(argv[1], argv[2], argv[3]);
    return -1;
}

int test_main(uint64_t argc, char **argv, char **environment) {
    (void)environment;
    int inverted = 0;
    if (argc > 1 && argv[1][0] == '!' && argv[1][1] == 0) {
        inverted = 1;
        ++argv;
        --argc;
    }
    if (argc == 2 && argv[1][0] == 0) os_exit(inverted ? 0 : 1);
    int result = predicate(argc, argv);
    if (result < 0) os_exit(2);
    os_exit((inverted ? !result : result) ? 0 : 1);
}
