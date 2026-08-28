#include "../lib/os.h"

void init_main(void) {
    static const char message[] = "os userland init\r\n";
    uint64_t result = os_write(1, message, sizeof(message) - 1U);
    os_exit(result == sizeof(message) - 1U ? 0 : 1);
}
