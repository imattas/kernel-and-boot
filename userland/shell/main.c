#include "../lib/os.h"
#include "shell.h"

typedef struct {
    char name[32];
    uint32_t type;
} shell_dirent_t;

static void print(const char *text, uint64_t length) {
    (void)os_write(1, text, length);
}

static void print_number(uint64_t value) {
    char digits[20];
    uint32_t length = 0;
    if (value == 0) {
        print("0", 1);
        return;
    }
    while (value != 0) {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (length != 0) print(&digits[--length], 1);
}

void shell_main(void) {
    static const char prompt[] = "os> ";
    static const char help[] = "help id echo pwd cd ls cat mkdir rm rmdir touch write run exit\r\n";
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
            else if (command == SHELL_ID) {
                print("pid=", 4);
                print_number(os_getpid());
                print(" ppid=", 6);
                print_number(os_getppid());
                print(" uid=", 5);
                print_number(os_getuid());
                print(" gid=", 5);
                print_number(os_getgid());
                print("\r\n", 2);
            }
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
            } else if (command == SHELL_WRITE) {
                uint32_t path_length = 0;
                while (argument[path_length] && argument[path_length] != ' ' &&
                       argument[path_length] != '\t') ++path_length;
                uint32_t content_start = path_length;
                while (argument[content_start] == ' ' ||
                       argument[content_start] == '\t') ++content_start;
                uint32_t content_length = 0;
                while (argument[content_start + content_length])
                    ++content_length;
                uint64_t descriptor = path_length != 0 && content_length != 0 ?
                    os_create(argument, path_length, 3) : OS_SYSCALL_ERROR;
                if (descriptor == OS_SYSCALL_ERROR || content_length > 256 ||
                    os_file_write(descriptor, argument + content_start,
                                  content_length) != content_length) {
                    if (descriptor != OS_SYSCALL_ERROR)
                        (void)os_close(descriptor);
                    print(unknown, sizeof(unknown) - 1U);
                } else if (os_close(descriptor) == OS_SYSCALL_ERROR) {
                    print(unknown, sizeof(unknown) - 1U);
                }
            } else if (command == SHELL_RUN) {
                uint32_t argument_length = 0;
                while (argument[argument_length]) ++argument_length;
                uint64_t process_id = os_spawn(argument, argument_length);
                int32_t status = -1;
                if (process_id == OS_SYSCALL_ERROR ||
                    os_wait(process_id, &status) == OS_SYSCALL_ERROR)
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
