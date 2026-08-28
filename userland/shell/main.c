#include "../lib/os.h"
#include "shell.h"

static void print(const char *text, uint64_t length) {
    (void)os_write(1, text, length);
}

void shell_main(void) {
    static const char prompt[] = "os> ";
    static const char help[] = "help echo pwd\r\n";
    static const char unknown[] = "unknown command\r\n";
    static char line[128];
    static char argument[128];
    uint32_t length = 0;
    print(prompt, sizeof(prompt) - 1U);
    for (;;) {
        uint32_t start = length;
        uint64_t received = os_read(0, line + length, sizeof(line) - 1U - length);
        if (received == 0 || received == OS_SYSCALL_ERROR) {
            os_yield();
            continue;
        }
        for (uint64_t index = 0; index < received; ++index) {
            char value = line[start + index];
            if (value != '\n' && value != '\r' && length < sizeof(line) - 1U)
                line[length++] = value;
            if (value != '\n' && value != '\r') continue;
            shell_command_t command = shell_parse(line, length, argument,
                                                   sizeof(argument));
            if (command == SHELL_HELP) print(help, sizeof(help) - 1U);
            else if (command == SHELL_ECHO) {
                uint32_t argument_length = 0;
                while (argument[argument_length]) ++argument_length;
                print(argument, argument_length);
                print("\r\n", 2);
            } else if (command == SHELL_PWD) {
                uint64_t result = os_getcwd(argument, sizeof(argument));
                if (result != OS_SYSCALL_ERROR) print(argument, result);
                print("\r\n", 2);
            } else if (command != SHELL_EMPTY) {
                print(unknown, sizeof(unknown) - 1U);
            }
            length = 0;
            print(prompt, sizeof(prompt) - 1U);
        }
    }
}
