#include <assert.h>
#include <stdint.h>

#include "../../../userland/apps/test/main.c"

uint64_t os_syscall3(uint64_t number, uint64_t arg1, uint64_t arg2,
                     uint64_t arg3) {
    (void)number;
    (void)arg1;
    (void)arg2;
    (void)arg3;
    return OS_SYSCALL_ERROR;
}

int main(void) {
    char *equal[] = {"test", "foo", "=", "foo", 0};
    char *not_equal[] = {"test", "foo", "!=", "bar", 0};
    char *numeric[] = {"test", "!", "3", "-lt", "4", 0};
    char *empty[] = {"test", "!", "-z", "value", 0};
    char *invalid[] = {"test", "!", 0};

    assert(test_result(4, equal) == 1);
    assert(test_result(4, not_equal) == 1);
    assert(test_result(5, numeric) == 0);
    assert(test_result(4, empty) == 1);
    assert(test_result(2, invalid) == 2);
    return 0;
}
