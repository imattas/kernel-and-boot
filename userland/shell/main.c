#include "../lib/os.h"
#include "shell.h"

typedef struct {
    char name[32];
    uint32_t type;
} shell_dirent_t;

static void print(const char *text, uint64_t length) {
    (void)os_write(1, text, length);
}

void shell_main(void) {
    static const char prompt[] = "os> ";
    static const char help[] = "help echo pwd cd ls cat mkdir rm rmdir touch exit\r\n";
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
            } else if (command == SHELL_CD) {
                uint32_t argument_length = 0;
                while (argument[argument_length]) ++argument_length;
                if (os_chdir(argument, argument_length) == OS_SYSCALL_ERROR)
                    print(unknown, sizeof(unknown) - 1U);
            } else if (command == SHELL_LS) {
                static const char current[] = ".";
                shell_dirent_t entry;
                uint64_t descriptor = os_open(current, 1, 1);
                if (descriptor == OS_SYSCALL_ERROR) {
                    print(unknown, sizeof(unknown) - 1U);
                } else {
                    while (os_readdir(descriptor, &entry) == 1) {
                        uint32_t name_length = 0;
                        while (name_length < sizeof(entry.name) &&
                               entry.name[name_length] != 0) ++name_length;
                        print(entry.name, name_length);
                        print("\r\n", 2);
                    }
                    (void)os_close(descriptor);
                }
            } else if (command == SHELL_CAT) {
                uint32_t argument_length = 0;
                while (argument[argument_length]) ++argument_length;
                uint64_t descriptor = os_open(argument, argument_length, 1);
                if (descriptor == OS_SYSCALL_ERROR) {
                    print(unknown, sizeof(unknown) - 1U);
                } else {
                    static char buffer[256];
                    uint64_t count;
                    while ((count = os_file_read(descriptor, buffer,
                                                  sizeof(buffer))) !=
                           OS_SYSCALL_ERROR && count != 0)
                        print(buffer, count);
                    (void)os_close(descriptor);
                }
            } else if (command == SHELL_MKDIR) {
                uint32_t argument_length = 0;
                while (argument[argument_length]) ++argument_length;
                if (os_mkdir(argument, argument_length, 0755) ==
                    OS_SYSCALL_ERROR)
                    print(unknown, sizeof(unknown) - 1U);
            } else if (command == SHELL_RM) {
                uint32_t argument_length = 0;
                while (argument[argument_length]) ++argument_length;
                if (os_unlink(argument, argument_length) == OS_SYSCALL_ERROR)
                    print(unknown, sizeof(unknown) - 1U);
            } else if (command == SHELL_RMDIR) {
                uint32_t argument_length = 0;
                while (argument[argument_length]) ++argument_length;
                if (os_rmdir(argument, argument_length) == OS_SYSCALL_ERROR)
                    print(unknown, sizeof(unknown) - 1U);
            } else if (command == SHELL_TOUCH) {
                uint32_t argument_length = 0;
                while (argument[argument_length]) ++argument_length;
                uint64_t descriptor = os_create(argument, argument_length, 3);
                if (descriptor == OS_SYSCALL_ERROR)
                    print(unknown, sizeof(unknown) - 1U);
                else if (os_close(descriptor) == OS_SYSCALL_ERROR)
                    print(unknown, sizeof(unknown) - 1U);
            } else if (command == SHELL_EXIT) {
                os_exit(0);
            } else if (command != SHELL_EMPTY) {
                print(unknown, sizeof(unknown) - 1U);
            }
            length = 0;
            print(prompt, sizeof(prompt) - 1U);
        }
    }
}
