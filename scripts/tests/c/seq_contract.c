#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../../../userland/apps/seq/seq_logic.h"

int main(void) {
    int64_t value = 0;
    char text[32];
    assert(seq_parse_number("0", &value) && value == 0);
    assert(seq_parse_number("-42", &value) && value == -42);
    assert(seq_parse_number("9223372036854775807", &value) &&
           value == INT64_MAX);
    assert(seq_parse_number("-9223372036854775808", &value) &&
           value == INT64_MIN);
    assert(!seq_parse_number("", &value));
    assert(!seq_parse_number("12x", &value));
    assert(!seq_parse_number("9223372036854775808", &value));
    assert(!seq_parse_number("-9223372036854775809", &value));
    assert(seq_format_number(text, sizeof(text), INT64_MIN) == 20 &&
           strcmp(text, "-9223372036854775808") == 0);
    assert(seq_format_number(text, sizeof(text), -7) == 2 &&
           strcmp(text, "-7") == 0);
    assert(seq_format_number(text, sizeof(text), 0) == 1 &&
           strcmp(text, "0") == 0);
    assert(seq_format_number(text, 3, 100) == 0);
    return 0;
}
