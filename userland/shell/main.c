#include "../lib/os.h"
#include "shell.h"

typedef struct {
    char name[32];
    uint32_t type;
} shell_dirent_t;

static const char shell_unknown[] = "unknown command\r\n";
static int32_t *shell_active_status;

static void print(const char *text, uint64_t length) {
    if (shell_active_status && text == shell_unknown)
        *shell_active_status = 1;
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

static void print_status(int32_t status) {
    if (status < 0) {
        print("-", 1);
        print_number((uint64_t)(-(int64_t)status));
    } else {
        print_number((uint64_t)status);
    }
}

static void print_stat(const os_stat_t *stat) {
    print("uid=", 4); print_number(stat->owner_uid);
    print(" gid=", 5); print_number(stat->owner_gid);
    print(" mode=", 6); print_number(stat->mode);
    print(" type=", 6); print_number(stat->type);
    print("\r\n", 2);
}

static int parse_octal(const char *text, uint32_t length, uint64_t *value) {
    if (!text || !value || length == 0 || length > 4) return 0;
    uint64_t result = 0;
    for (uint32_t index = 0; index < length; ++index) {
        if (text[index] < '0' || text[index] > '7') return 0;
        result = (result << 3) | (uint64_t)(text[index] - '0');
    }
    *value = result;
    return 1;
}

static int parse_number(const char *text, uint32_t length, uint64_t *value) {
    if (!text || !value || length == 0) return 0;
    uint64_t result = 0;
    for (uint32_t index = 0; index < length; ++index) {
        if (text[index] < '0' || text[index] > '9' || result >
            (UINT64_MAX - (uint64_t)(text[index] - '0')) / 10U) return 0;
        result = result * 10U + (uint64_t)(text[index] - '0');
    }
    *value = result;
    return 1;
}

typedef struct {
    uint64_t process_id;
    uint64_t peer_id;
} shell_job_t;

static shell_job_t jobs[16];
static uint32_t job_count;

static void job_add(uint64_t process_id, uint64_t peer_id) {
    if (job_count < sizeof(jobs) / sizeof(jobs[0]))
        jobs[job_count++] = (shell_job_t){process_id, peer_id};
}

static int job_find(uint64_t process_id, uint32_t *index) {
    for (uint32_t i = 0; i < job_count; ++i)
        if (jobs[i].process_id == process_id || jobs[i].peer_id == process_id) {
            if (index) *index = i;
            return 1;
        }
    return 0;
}

static int job_get(uint64_t process_id, uint64_t *leader, uint64_t *peer) {
    uint32_t index = 0;
    if (!job_find(process_id, &index)) return 0;
    if (leader) *leader = jobs[index].process_id;
    if (peer) *peer = jobs[index].peer_id;
    return 1;
}

static void job_remove(uint64_t process_id) {
    uint32_t found = 0;
    if (!job_find(process_id, &found)) return;
    jobs[found] = jobs[--job_count];
}

static int job_contains(uint64_t process_id) {
    return job_find(process_id, 0);
}

static int shell_wait_job(uint64_t process_id, int32_t *status) {
    uint64_t leader = 0;
    uint64_t peer = 0;
    if (!job_get(process_id, &leader, &peer)) return 0;
    int32_t leader_status = -1;
    if (peer != 0 && (os_wait(leader, &leader_status) == OS_SYSCALL_ERROR ||
                      os_reap(leader) == OS_SYSCALL_ERROR)) return 0;
    if (os_wait(peer != 0 ? peer : leader, status) == OS_SYSCALL_ERROR ||
        os_reap(peer != 0 ? peer : leader) == OS_SYSCALL_ERROR) return 0;
    return 1;
}


static uint32_t resolve_command(const char *name, uint32_t name_length,
                                char *path, uint32_t capacity) {
    if (!name || !path || capacity == 0 || name_length == 0) return 0;
    for (uint32_t index = 0; index < name_length; ++index) {
        if (name[index] == '/') {
            if (name_length >= capacity) return 0;
            for (uint32_t copy = 0; copy < name_length; ++copy)
                path[copy] = name[copy];
            path[name_length] = 0;
            uint64_t descriptor = os_open(path, name_length, 1);
            if (descriptor == OS_SYSCALL_ERROR) return 0;
            (void)os_close(descriptor);
            return name_length;
        }
    }

    char environment[128];
    uint64_t environment_length = os_getenv("PATH", environment,
                                             sizeof(environment));
    if (environment_length == OS_SYSCALL_ERROR ||
        environment_length >= sizeof(environment)) return 0;
    uint32_t start = 0;
    while (start <= environment_length) {
        uint32_t end = start;
        while (end < environment_length && environment[end] != ':') ++end;
        uint32_t directory_length = end - start;
        uint32_t separator = directory_length == 0 ? 0 : 1;
        if (directory_length + separator + name_length < capacity) {
            uint32_t candidate_length = 0;
            for (uint32_t index = 0; index < directory_length; ++index)
                path[candidate_length++] = environment[start + index];
            if (separator) path[candidate_length++] = '/';
            for (uint32_t index = 0; index < name_length; ++index)
                path[candidate_length++] = name[index];
            path[candidate_length] = 0;
            uint64_t descriptor = os_open(path, candidate_length, 1);
            if (descriptor != OS_SYSCALL_ERROR) {
                (void)os_close(descriptor);
                return candidate_length;
            }
        }
        if (end == environment_length) break;
        start = end + 1;
    }
    return 0;
}

static int shell_run_pipeline(char *text, uint32_t length, int background,
                              uint64_t *leader_id, uint64_t *consumer_id,
                              int32_t *status) {
    uint32_t separator = length;
    for (uint32_t index = 0; index < length; ++index) {
        if (text[index] != '|') continue;
        if (separator != length) return 0;
        separator = index;
    }
    if (separator == length) return 0;
    text[separator] = 0;
    uint32_t left_start = 0;
    while (left_start < separator && (text[left_start] == ' ' ||
                                      text[left_start] == '\t')) ++left_start;
    uint32_t left_length = separator;
    while (left_length > left_start && (text[left_length - 1U] == ' ' ||
                                        text[left_length - 1U] == '\t'))
        --left_length;
    uint32_t right_start = separator + 1U;
    while (right_start < length && (text[right_start] == ' ' ||
                                    text[right_start] == '\t')) ++right_start;
    if (left_start == left_length || right_start == length) return 0;
    uint32_t left_name_length = left_start;
    while (left_name_length < left_length && text[left_name_length] != ' ' &&
           text[left_name_length] != '\t') ++left_name_length;
    uint32_t right_length = length - right_start;
    uint32_t right_name_length = 0;
    while (right_name_length < right_length && text[right_start + right_name_length] != ' ' &&
           text[right_start + right_name_length] != '\t') ++right_name_length;
    char left_path[128];
    char right_path[128];
    uint32_t resolved_left = resolve_command(text + left_start, left_name_length - left_start,
                                              left_path, sizeof(left_path));
    uint32_t resolved_right = resolve_command(text + right_start, right_name_length,
                                               right_path, sizeof(right_path));
    if (resolved_left == 0 || resolved_right == 0) return 0;
    uint32_t read_handle = 0;
    uint32_t write_handle = 0;
    if (os_pipe(&read_handle, &write_handle) == OS_SYSCALL_ERROR ||
        os_set_inheritable(read_handle, 0) == OS_SYSCALL_ERROR)
        return 0;
    uint32_t left_args_start = left_name_length;
    while (left_args_start < left_length && (text[left_args_start] == ' ' ||
                                             text[left_args_start] == '\t')) ++left_args_start;
    uint64_t left_process = os_spawn_redirected(
        left_path, resolved_left, text + left_args_start, 0, write_handle);
    (void)os_set_inheritable(read_handle, 1);
    if (left_process == OS_SYSCALL_ERROR ||
        os_set_inheritable(write_handle, 0) == OS_SYSCALL_ERROR) {
        (void)os_close(read_handle);
        (void)os_close(write_handle);
        return 0;
    }
    uint32_t right_args_start = right_start + right_name_length;
    while (right_args_start < length && (text[right_args_start] == ' ' ||
                                         text[right_args_start] == '\t')) ++right_args_start;
    uint64_t right_process = os_spawn_redirected(
        right_path, resolved_right, text + right_args_start, read_handle, 0);
    (void)os_set_inheritable(write_handle, 1);
    (void)os_close(read_handle);
    (void)os_close(write_handle);
    if (right_process == OS_SYSCALL_ERROR) return 0;
    if (background) {
        if (leader_id) *leader_id = left_process;
        if (consumer_id) *consumer_id = right_process;
        return 1;
    }
    int32_t left_status = -1;
    int32_t right_status = -1;
    if (os_wait(left_process, &left_status) == OS_SYSCALL_ERROR ||
        os_reap(left_process) == OS_SYSCALL_ERROR ||
        os_wait(right_process, &right_status) == OS_SYSCALL_ERROR ||
        os_reap(right_process) == OS_SYSCALL_ERROR) return 0;
    *status = right_status;
    return 1;
}

static int shell_run_redirect(char *text, uint32_t length, int32_t *status) {
    uint32_t separator = length;
    for (uint32_t index = 0; index < length; ++index) {
        if (text[index] != '>') continue;
        if (separator != length) return 0;
        separator = index;
    }
    if (separator == length) return 0;
    text[separator] = 0;
    uint32_t left_length = separator;
    while (left_length != 0 && (text[left_length - 1U] == ' ' ||
                                text[left_length - 1U] == '\t')) --left_length;
    uint32_t path_start = separator + 1U;
    while (path_start < length && (text[path_start] == ' ' ||
                                   text[path_start] == '\t')) ++path_start;
    if (left_length == 0 || path_start == length) return 0;
    uint32_t name_length = 0;
    while (name_length < left_length && text[name_length] != ' ' &&
           text[name_length] != '\t') ++name_length;
    char resolved_path[128];
    uint32_t resolved_length = resolve_command(text, name_length,
                                                resolved_path,
                                                sizeof(resolved_path));
    if (resolved_length == 0) return 0;
    uint32_t output_length = length - path_start;
    uint64_t output = os_create(text + path_start, output_length, 3);
    if (output == OS_SYSCALL_ERROR) return 0;
    uint32_t arguments_start = name_length;
    while (arguments_start < left_length && (text[arguments_start] == ' ' ||
                                              text[arguments_start] == '\t')) ++arguments_start;
    uint64_t process_id = os_spawn_redirected(resolved_path, resolved_length,
                                              text + arguments_start, 0,
                                              (uint32_t)output);
    (void)os_close(output);
    if (process_id == OS_SYSCALL_ERROR) return 0;
    int32_t child_status = -1;
    if (os_wait(process_id, &child_status) == OS_SYSCALL_ERROR ||
        os_reap(process_id) == OS_SYSCALL_ERROR) return 0;
    *status = child_status;
    return 1;
}

static int shell_run_input_redirect(char *text, uint32_t length,
                                    int32_t *status) {
    uint32_t separator = length;
    for (uint32_t index = 0; index < length; ++index) {
        if (text[index] != '<') continue;
        if (separator != length) return 0;
        separator = index;
    }
    if (separator == length) return 0;
    text[separator] = 0;
    uint32_t left_length = separator;
    while (left_length != 0 && (text[left_length - 1U] == ' ' ||
                                text[left_length - 1U] == '\t')) --left_length;
    uint32_t path_start = separator + 1U;
    while (path_start < length && (text[path_start] == ' ' ||
                                   text[path_start] == '\t')) ++path_start;
    if (left_length == 0 || path_start == length) return 0;
    uint32_t name_length = 0;
    while (name_length < left_length && text[name_length] != ' ' &&
           text[name_length] != '\t') ++name_length;
    char resolved_path[128];
    uint32_t resolved_length = resolve_command(text, name_length,
                                                resolved_path,
                                                sizeof(resolved_path));
    if (resolved_length == 0) return 0;
    uint32_t input_length = length - path_start;
    uint64_t input = os_open(text + path_start, input_length, 1);
    if (input == OS_SYSCALL_ERROR) return 0;
    uint32_t arguments_start = name_length;
    while (arguments_start < left_length && (text[arguments_start] == ' ' ||
                                              text[arguments_start] == '\t')) ++arguments_start;
    uint64_t process_id = os_spawn_redirected(resolved_path, resolved_length,
                                              text + arguments_start,
                                              (uint32_t)input, 0);
    (void)os_close(input);
    if (process_id == OS_SYSCALL_ERROR) return 0;
    int32_t child_status = -1;
    if (os_wait(process_id, &child_status) == OS_SYSCALL_ERROR ||
        os_reap(process_id) == OS_SYSCALL_ERROR) return 0;
    *status = child_status;
    return 1;
}

void shell_main(void) {
    static const char prompt[] = "os> ";
    static const char help[] = "help id ps env setenv unsetenv status true false jobs fg which inherit echo pwd cd ls cat stat chmod kill sleep mv mkdir rm rmdir touch write run wait exit\r\n";
    static const char *unknown = shell_unknown;
    static char line[128];
    static char argument[128];
    uint32_t length = 0;
    int32_t last_status = 0;
    shell_active_status = &last_status;
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
            uint32_t previous_length = length;
            shell_edit_result_t edit = shell_edit_line(line, &length,
                                                        sizeof(line) - 1U,
                                                        value);
            if (value == '\b' || (unsigned char)value == 0x7f) {
                if (previous_length != length) print("\b \b", 3);
                continue;
            }
            if (value == 0x15) {
                while (previous_length-- != 0) print("\b \b", 3);
                continue;
            }
            if (edit == SHELL_EDIT_CANCEL) {
                while (previous_length-- != 0) print("\b \b", 3);
                print("^C\r\n", 4);
                print(prompt, sizeof(prompt) - 1U);
                continue;
            }
            if (edit != SHELL_EDIT_SUBMIT) continue;
            shell_command_t command = shell_parse(line, length, argument,
                                                   sizeof(argument));
            if (command != SHELL_EMPTY && command != SHELL_STATUS)
                last_status = 0;
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
            else if (command == SHELL_PS) {
                uint64_t ids[64];
                uint64_t count = os_process_list(ids, 64);
                if (count == OS_SYSCALL_ERROR) {
                    print(unknown, sizeof(unknown) - 1U);
                } else {
                    for (uint64_t index = 0; index < count; ++index) {
                        os_process_info_t info;
                        if (os_process_status(ids[index], &info) != 0) continue;
                        print("pid=", 4);
                        print_number(info.id);
                        print(" ppid=", 6);
                        print_number(info.parent_id);
                        print(" state=", 7);
                        print_number(info.state);
                        print(" exit=", 6);
                        print_number((uint32_t)info.exit_status);
                        print("\r\n", 2);
                    }
                }
            }
            else if (command == SHELL_ENV) {
                char value[128];
                uint64_t length = os_getenv("PATH", value, sizeof(value));
                if (length == OS_SYSCALL_ERROR) {
                    print(unknown, sizeof(unknown) - 1U);
                } else {
                    print("PATH=", 5);
                    print(value, length);
                    print("\r\n", 2);
                }
            }
            else if (command == SHELL_JOBS) {
                for (uint32_t index = 0; index < job_count; ++index) {
                    os_process_info_t info;
                    if (os_process_status(jobs[index].process_id, &info) != 0) continue;
                    print("pid=", 4);
                    print_number(info.id);
                    if (jobs[index].peer_id != 0) {
                        print(" peer=", 6);
                        print_number(jobs[index].peer_id);
                    }
                    print(" state=", 7);
                    print_number(info.state);
                    print("\r\n", 2);
                }
            }
            else if (command == SHELL_FG) {
                uint32_t pid_length = 0;
                while (argument[pid_length]) ++pid_length;
                uint64_t process_id = 0;
                int32_t status = -1;
                if (!parse_number(argument, pid_length, &process_id) ||
                    !job_contains(process_id) ||
                    !shell_wait_job(process_id, &status)) {
                    print(unknown, sizeof(unknown) - 1U);
                } else {
                    last_status = status;
                    job_remove(process_id);
                    print("exit=", 5);
                    print_status(status);
                    print("\r\n", 2);
                }
            }
            else if (command == SHELL_WHICH) {
                uint32_t name_length = 0;
                while (argument[name_length]) ++name_length;
                char resolved_path[128];
                uint32_t resolved_length = resolve_command(argument, name_length,
                                                            resolved_path,
                                                            sizeof(resolved_path));
                if (resolved_length == 0) {
                    print(unknown, sizeof(unknown) - 1U);
                } else {
                    print(resolved_path, resolved_length);
                    print("\r\n", 2);
                }
            }
            else if (command == SHELL_INHERIT) {
                uint32_t descriptor_length = 0;
                while (argument[descriptor_length] && argument[descriptor_length] != ' ' &&
                       argument[descriptor_length] != '\t') ++descriptor_length;
                uint32_t mode_start = descriptor_length;
                while (argument[mode_start] == ' ' || argument[mode_start] == '\t')
                    ++mode_start;
                uint32_t mode_length = 0;
                while (argument[mode_start + mode_length]) ++mode_length;
                uint64_t descriptor = 0;
                int enabled = mode_length == 2 && argument[mode_start] == 'o' &&
                              argument[mode_start + 1] == 'n';
                int disabled = mode_length == 3 && argument[mode_start] == 'o' &&
                               argument[mode_start + 1] == 'f' &&
                               argument[mode_start + 2] == 'f';
                if (!parse_number(argument, descriptor_length, &descriptor) ||
                    (!enabled && !disabled) ||
                    os_set_inheritable(descriptor, enabled) == OS_SYSCALL_ERROR)
                    print(unknown, sizeof(unknown) - 1U);
            }
            else if (command == SHELL_ECHO) {
                uint32_t argument_length = 0;
                while (argument[argument_length]) ++argument_length;
                print(argument, argument_length);
                print("\r\n", 2);
            } else if (command == SHELL_PWD) {
                uint64_t result = os_getcwd(argument, sizeof(argument));
                if (result != OS_SYSCALL_ERROR) print(argument, result);
                else print(unknown, sizeof(unknown) - 1U);
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
                    uint64_t read_result;
                    while ((read_result = os_readdir(descriptor, &entry)) == 1) {
                        uint32_t name_length = 0;
                        while (name_length < sizeof(entry.name) &&
                               entry.name[name_length] != 0) ++name_length;
                        print(entry.name, name_length);
                        print("\r\n", 2);
                    }
                    if (read_result == OS_SYSCALL_ERROR)
                        print(unknown, sizeof(unknown) - 1U);
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
                    if (count == OS_SYSCALL_ERROR)
                        print(unknown, sizeof(unknown) - 1U);
                    (void)os_close(descriptor);
                }
            } else if (command == SHELL_STAT) {
                uint32_t argument_length = 0;
                while (argument[argument_length]) ++argument_length;
                uint64_t descriptor = os_open(argument, argument_length, 1);
                os_stat_t stat;
                if (descriptor == OS_SYSCALL_ERROR ||
                    os_fstat(descriptor, &stat) == OS_SYSCALL_ERROR) {
                    print(unknown, sizeof(unknown) - 1U);
                } else {
                    print_stat(&stat);
                }
                if (descriptor != OS_SYSCALL_ERROR) (void)os_close(descriptor);
            } else if (command == SHELL_CHMOD) {
                uint32_t argument_length = 0;
                while (argument[argument_length]) ++argument_length;
                uint32_t separator = 0;
                while (separator < argument_length && argument[separator] != ' ' &&
                       argument[separator] != '\t') ++separator;
                uint32_t path_start = separator;
                while (path_start < argument_length &&
                       (argument[path_start] == ' ' || argument[path_start] == '\t'))
                    ++path_start;
                uint64_t mode;
                if (!parse_octal(argument, separator, &mode) ||
                    path_start == argument_length ||
                    os_chmod(argument + path_start, argument_length - path_start,
                             mode) == OS_SYSCALL_ERROR)
                    print(unknown, sizeof(unknown) - 1U);
            } else if (command == SHELL_KILL) {
                uint32_t argument_length = 0;
                while (argument[argument_length]) ++argument_length;
                uint32_t separator = 0;
                while (separator < argument_length && argument[separator] != ' ' &&
                       argument[separator] != '\t') ++separator;
                uint32_t signal_start = separator;
                while (signal_start < argument_length &&
                       (argument[signal_start] == ' ' || argument[signal_start] == '\t'))
                    ++signal_start;
                uint64_t process_id;
                uint64_t signal_number;
                if (!parse_number(argument, separator, &process_id) ||
                    !parse_number(argument + signal_start,
                                  argument_length - signal_start, &signal_number) ||
                    os_signal_send_to(process_id, signal_number) == OS_SYSCALL_ERROR)
                    print(unknown, sizeof(unknown) - 1U);
            } else if (command == SHELL_SLEEP) {
                uint32_t argument_length = 0;
                while (argument[argument_length]) ++argument_length;
                uint64_t milliseconds;
                uint64_t now = os_clock_monotonic();
                if (!parse_number(argument, argument_length, &milliseconds) ||
                    milliseconds > (UINT64_MAX - now) / 1000000U) {
                    print(unknown, sizeof(unknown) - 1U);
                } else {
                    uint64_t deadline = now + milliseconds * 1000000U;
                    while ((now = os_clock_monotonic()) < deadline)
                        (void)os_yield();
                }
            } else if (command == SHELL_MV) {
                uint32_t argument_length = 0;
                while (argument[argument_length]) ++argument_length;
                uint32_t separator = 0;
                while (separator < argument_length && argument[separator] != ' ' &&
                       argument[separator] != '\t') ++separator;
                uint32_t new_start = separator;
                while (new_start < argument_length &&
                       (argument[new_start] == ' ' || argument[new_start] == '\t'))
                    ++new_start;
                uint32_t new_length = argument_length - new_start;
                if (separator == 0 || new_length == 0 ||
                    os_rename(argument, separator, argument + new_start,
                              new_length) == OS_SYSCALL_ERROR)
                    print(unknown, sizeof(unknown) - 1U);
            } else if (command == SHELL_SETENV) {
                uint32_t argument_length = 0;
                while (argument[argument_length]) ++argument_length;
                uint32_t separator = 0;
                while (separator < argument_length && argument[separator] != ' ' &&
                       argument[separator] != '\t') ++separator;
                uint32_t value_start = separator;
                while (value_start < argument_length &&
                       (argument[value_start] == ' ' || argument[value_start] == '\t'))
                    ++value_start;
                if (separator == 0 || value_start == argument_length ||
                    os_setenv(argument, argument + value_start) == OS_SYSCALL_ERROR)
                    print(unknown, sizeof(unknown) - 1U);
            } else if (command == SHELL_UNSETENV) {
                uint32_t argument_length = 0;
                while (argument[argument_length]) ++argument_length;
                if (argument_length == 0 ||
                    os_unsetenv(argument) == OS_SYSCALL_ERROR)
                    print(unknown, sizeof(unknown) - 1U);
            } else if (command == SHELL_STATUS) {
                print_status(last_status);
                print("\r\n", 2);
            } else if (command == SHELL_TRUE) {
                /* The command-loop reset already published success. */
            } else if (command == SHELL_FALSE) {
                last_status = 1;
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
            } else if (command == SHELL_WAIT) {
                uint32_t pid_length = 0;
                while (argument[pid_length]) ++pid_length;
                uint64_t process_id = 0;
                int32_t status = -1;
                int waited = parse_number(argument, pid_length, &process_id) &&
                    (job_contains(process_id) ? shell_wait_job(process_id, &status) :
                     (os_wait(process_id, &status) != OS_SYSCALL_ERROR &&
                      os_reap(process_id) != OS_SYSCALL_ERROR));
                if (!waited) {
                    last_status = 1;
                    print(unknown, sizeof(unknown) - 1U);
                } else {
                    last_status = status;
                    print("exit=", 5);
                    print_status(status);
                    print("\r\n", 2);
                    job_remove(process_id);
                }
            } else if (command == SHELL_RUN) {
                uint32_t argument_length = 0;
                while (argument[argument_length]) ++argument_length;
                uint32_t pipeline_count = 0;
                for (uint32_t index = 0; index < argument_length; ++index)
                    if (argument[index] == '|') ++pipeline_count;
                int pipeline_background = pipeline_count != 0 &&
                    argument[argument_length - 1U] == '&';
                if (pipeline_background) {
                    --argument_length;
                    while (argument_length != 0 &&
                           (argument[argument_length - 1U] == ' ' ||
                            argument[argument_length - 1U] == '\t')) --argument_length;
                    argument[argument_length] = 0;
                }
                if (pipeline_count != 0) {
                    int32_t pipeline_status = 1;
                    uint64_t pipeline_leader = 0;
                    uint64_t pipeline_consumer = 0;
                    if (!shell_run_pipeline(argument, argument_length,
                                             pipeline_background,
                                             &pipeline_leader, &pipeline_consumer,
                                             &pipeline_status))
                        print(unknown, sizeof(unknown) - 1U);
                    else if (pipeline_background) {
                        job_add(pipeline_leader, pipeline_consumer);
                        print("pid=", 4);
                        print_number(pipeline_consumer);
                        print(" peer=", 6);
                        print_number(pipeline_leader);
                        print("\r\n", 2);
                    }
                    else {
                        last_status = pipeline_status;
                        print("exit=", 5);
                        print_status(pipeline_status);
                        print("\r\n", 2);
                    }
                } else {
                uint32_t redirect_count = 0;
                for (uint32_t index = 0; index < argument_length; ++index)
                    if (argument[index] == '>') ++redirect_count;
                if (redirect_count != 0) {
                    int32_t redirect_status = 1;
                    if (!shell_run_redirect(argument, argument_length,
                                             &redirect_status))
                        print(unknown, sizeof(unknown) - 1U);
                    else {
                        last_status = redirect_status;
                        print("exit=", 5);
                        print_status(redirect_status);
                        print("\r\n", 2);
                    }
                } else {
                uint32_t input_redirect_count = 0;
                for (uint32_t index = 0; index < argument_length; ++index)
                    if (argument[index] == '<') ++input_redirect_count;
                if (input_redirect_count != 0) {
                    int32_t input_status = 1;
                    if (!shell_run_input_redirect(argument, argument_length,
                                                  &input_status))
                        print(unknown, sizeof(unknown) - 1U);
                    else {
                        last_status = input_status;
                        print("exit=", 5);
                        print_status(input_status);
                        print("\r\n", 2);
                    }
                } else {
                int background = 0;
                if (argument_length != 0 && argument[argument_length - 1U] == '&') {
                    background = 1;
                    --argument_length;
                    while (argument_length != 0 &&
                           (argument[argument_length - 1U] == ' ' ||
                            argument[argument_length - 1U] == '\t')) --argument_length;
                    argument[argument_length] = 0;
                }
                uint32_t path_length = 0;
                while (path_length < argument_length && argument[path_length] != ' ' &&
                       argument[path_length] != '\t') ++path_length;
                uint32_t run_arguments = path_length;
                while (run_arguments < argument_length &&
                       (argument[run_arguments] == ' ' ||
                        argument[run_arguments] == '\t')) ++run_arguments;
                char resolved_path[128];
                uint32_t resolved_length = resolve_command(argument, path_length,
                                                            resolved_path,
                                                            sizeof(resolved_path));
                uint64_t process_id = resolved_length == 0 ||
                                      (background && job_count == 16) ? OS_SYSCALL_ERROR :
                    os_spawn(resolved_path, resolved_length,
                                                argument + run_arguments);
                int32_t status = -1;
                if (process_id == OS_SYSCALL_ERROR) {
                    last_status = 1;
                    print(unknown, sizeof(unknown) - 1U);
                } else if (background) {
                    job_add(process_id, 0);
                    print("pid=", 4);
                    print_number(process_id);
                    print("\r\n", 2);
                } else if (os_wait(process_id, &status) == OS_SYSCALL_ERROR ||
                           os_reap(process_id) == OS_SYSCALL_ERROR) {
                    last_status = 1;
                    print(unknown, sizeof(unknown) - 1U);
                } else {
                    last_status = status;
                    print("exit=", 5);
                    print_status(status);
                    print("\r\n", 2);
                }
                }
                }
                }
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
