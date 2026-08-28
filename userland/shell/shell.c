#include "shell.h"

shell_edit_result_t shell_edit_line(char *line, uint32_t *length,
                                    uint32_t capacity, char value) {
    if (!line || !length || capacity == 0 || *length > capacity)
        return SHELL_EDIT_CONTINUE;
    if (value == '\r' || value == '\n') return SHELL_EDIT_SUBMIT;
    if (value == 0x03 || value == 0x15) {
        *length = 0;
        line[0] = 0;
        return value == 0x03 ? SHELL_EDIT_CANCEL : SHELL_EDIT_CONTINUE;
    }
    if (value == '\b' || (unsigned char)value == 0x7f) {
        if (*length != 0) {
            --*length;
            line[*length] = 0;
        }
        return SHELL_EDIT_CONTINUE;
    }
    if ((unsigned char)value < 0x20) return SHELL_EDIT_CONTINUE;
    if (*length == capacity) return SHELL_EDIT_CONTINUE;
    line[(*length)++] = value;
    line[*length] = 0;
    return SHELL_EDIT_CONTINUE;
}

static int same_word(const char *line, uint32_t length, const char *word) {
    uint32_t index = 0;
    while (word[index] != 0) {
        if (index >= length || line[index] != word[index]) return 0;
        ++index;
    }
    return index == length;
}

shell_command_t shell_parse(const char *line, uint32_t length,
                            char *argument, uint32_t capacity) {
    if (!line || !argument || capacity == 0) return SHELL_UNKNOWN;
    argument[0] = 0;
    while (length && (line[0] == ' ' || line[0] == '\t' ||
                      line[0] == '\r' || line[0] == '\n')) {
        ++line;
        --length;
    }
    while (length && (line[length - 1U] == '\r' || line[length - 1U] == '\n' ||
                      line[length - 1U] == ' ' || line[length - 1U] == '\t'))
        --length;
    if (length == 0) return SHELL_EMPTY;
    uint32_t command_length = 0;
    while (command_length < length && line[command_length] != ' ' &&
           line[command_length] != '\t') ++command_length;
    if (same_word(line, command_length, "help")) return SHELL_HELP;
    if (same_word(line, command_length, "id")) return SHELL_ID;
    if (same_word(line, command_length, "ps")) return SHELL_PS;
    if (same_word(line, command_length, "env")) return SHELL_ENV;
    if (same_word(line, command_length, "pwd")) return SHELL_PWD;
    if (same_word(line, command_length, "ls")) return SHELL_LS;
    if (same_word(line, command_length, "exit")) return SHELL_EXIT;
    if (!same_word(line, command_length, "cat") &&
        !same_word(line, command_length, "mkdir") &&
        !same_word(line, command_length, "rm") &&
        !same_word(line, command_length, "rmdir") &&
        !same_word(line, command_length, "touch") &&
        !same_word(line, command_length, "write") &&
        !same_word(line, command_length, "run") &&
        !same_word(line, command_length, "which") &&
        !same_word(line, command_length, "inherit") &&
        !same_word(line, command_length, "echo") &&
        !same_word(line, command_length, "cd")) return SHELL_UNKNOWN;
    uint32_t start = command_length;
    while (start < length && (line[start] == ' ' || line[start] == '\t')) ++start;
    uint32_t argument_length = length - start;
    if (argument_length >= capacity) return SHELL_UNKNOWN;
    for (uint32_t index = 0; index < argument_length; ++index)
        argument[index] = line[start + index];
    argument[argument_length] = 0;
    if (same_word(line, command_length, "cd")) return SHELL_CD;
    if (same_word(line, command_length, "cat")) return SHELL_CAT;
    if (same_word(line, command_length, "mkdir")) return SHELL_MKDIR;
    if (same_word(line, command_length, "rm")) return SHELL_RM;
    if (same_word(line, command_length, "rmdir")) return SHELL_RMDIR;
    if (same_word(line, command_length, "touch")) return SHELL_TOUCH;
    if (same_word(line, command_length, "write")) return SHELL_WRITE;
    if (same_word(line, command_length, "run")) return SHELL_RUN;
    if (same_word(line, command_length, "which")) return SHELL_WHICH;
    if (same_word(line, command_length, "inherit")) return SHELL_INHERIT;
    return SHELL_ECHO;
}
