#include <assert.h>
#include <stdint.h>
#include "../../../userland/lib/runtime.h"

int main(void) {
    uint64_t value = 0;
    assert(os_userland_length("") == 0);
    assert(os_userland_length("hello") == 5);
    assert(os_userland_length(0) == 0);

    assert(os_userland_parse_u64("0", &value) && value == 0);
    assert(os_userland_parse_u64("18446744073709551615", &value));
    assert(value == UINT64_MAX);
    assert(!os_userland_parse_u64("18446744073709551616", &value));
    assert(!os_userland_parse_u64("12x", &value));
    assert(!os_userland_parse_u64("", &value));

    assert(os_userland_parse_octal("755", &value, 4) && value == 0755);
    assert(os_userland_parse_octal("0000", &value, 4) && value == 0);
    assert(!os_userland_parse_octal("7555", &value, 3));
    assert(!os_userland_parse_octal("758", &value, 4));
    assert(!os_userland_parse_octal(0, &value, 4));
    char assignment[32] = "MODE=test";
    uint64_t value_offset = 0;
    assert(os_userland_split_assignment(assignment, &value_offset));
    assert(strcmp(assignment, "MODE") == 0 && value_offset == 5 &&
           strcmp(assignment + value_offset, "test") == 0);
    strcpy(assignment, "=missing");
    assert(!os_userland_split_assignment(assignment, &value_offset));
    return 0;
}
