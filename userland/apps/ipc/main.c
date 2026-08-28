#include "../../lib/runtime.h"

int ipc_main(uint64_t argc, char **argv, char **environment) {
    (void)argv;
    (void)environment;
    static const char message[] = "ipc";
    char received[sizeof(message)] = {0};
    uint64_t channel = os_channel_create();
    if (argc != 1 || channel == OS_SYSCALL_ERROR ||
        os_channel_send_wait(channel, message, sizeof(message) - 1U) !=
            sizeof(message) - 1U ||
        os_channel_receive_wait(channel, received, sizeof(received)) !=
            sizeof(message) - 1U || received[0] != 'i' || received[1] != 'p' ||
        received[2] != 'c' || os_close(channel) == OS_SYSCALL_ERROR)
        os_exit(1);
    if (os_write(1, "ipc ok\r\n", 8) != 8) os_exit(1);
    os_exit(0);
}
