#include "shell.h"

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
    if (same_word(line, command_length, "pwd")) return SHELL_PWD;
    if (same_word(line, command_length, "ls")) return SHELL_LS;
    if (same_word(line, command_length, "exit")) return SHELL_EXIT;
    if (!same_word(line, command_length, "echo") &&
        !same_word(line, command_length, "cd")) return SHELL_UNKNOWN;
    uint32_t start = command_length;
    while (start < length && (line[start] == ' ' || line[start] == '\t')) ++start;
    uint32_t argument_length = length - start;
    if (argument_length >= capacity) return SHELL_UNKNOWN;
    for (uint32_t index = 0; index < argument_length; ++index)
        argument[index] = line[start + index];
    argument[argument_length] = 0;
    return same_word(line, command_length, "cd") ? SHELL_CD : SHELL_ECHO;
}
