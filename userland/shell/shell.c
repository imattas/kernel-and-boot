#include "shell.h"

int shell_split_next(char *line, uint32_t *length,
                     char *remainder, uint32_t capacity,
                     uint32_t *remainder_length,
                     shell_sequence_operator_t *operator) {
    if (!line || !length || !remainder || !remainder_length || !operator)
        return 0;
    *remainder_length = 0;
    *operator = SHELL_SEQUENCE_NONE;
    char quote = 0;
    int escaped = 0;
    uint32_t separator = *length;
    uint32_t separator_width = 0;
    for (uint32_t index = 0; index < *length; ++index) {
        char value = line[index];
        if (escaped) escaped = 0;
        else if (value == '\\') escaped = 1;
        else if (quote) {
            if (value == quote) quote = 0;
        } else if (value == '\'' || value == '"') quote = value;
        else if (value == ';') {
            separator = index;
            separator_width = 1;
            *operator = SHELL_SEQUENCE_SEMICOLON;
            break;
        } else if (value == '&' && index + 1U < *length &&
                   line[index + 1U] == '&') {
            separator = index;
            separator_width = 2;
            *operator = SHELL_SEQUENCE_AND;
            break;
        } else if (value == '|' && index + 1U < *length &&
                   line[index + 1U] == '|') {
            separator = index;
            separator_width = 2;
            *operator = SHELL_SEQUENCE_OR;
            break;
        }
    }
    if (separator == *length) return 1;
    uint32_t tail_start = separator + separator_width;
    while (tail_start < *length && (line[tail_start] == ' ' ||
                                    line[tail_start] == '\t')) ++tail_start;
    uint32_t tail_length = *length - tail_start;
    while (tail_length != 0 && (line[tail_start + tail_length - 1U] == ' ' ||
                                line[tail_start + tail_length - 1U] == '\t'))
        --tail_length;
    if (tail_length >= capacity) return 0;
    for (uint32_t index = 0; index < tail_length; ++index)
        remainder[index] = line[tail_start + index];
    remainder[tail_length] = 0;
    uint32_t first_length = separator;
    while (first_length != 0 && (line[first_length - 1U] == ' ' ||
                                 line[first_length - 1U] == '\t')) --first_length;
    line[first_length] = 0;
    *length = first_length;
    *remainder_length = tail_length;
    return 1;
}

int shell_split_sequence(char *line, uint32_t *length,
                         char *remainder, uint32_t capacity,
                         uint32_t *remainder_length) {
    shell_sequence_operator_t operator;
    if (!shell_split_next(line, length, remainder, capacity,
                          remainder_length, &operator)) return 0;
    return operator == SHELL_SEQUENCE_NONE ||
           operator == SHELL_SEQUENCE_SEMICOLON;
}

uint32_t shell_history_push(char history[][SHELL_HISTORY_LINE_CAPACITY],
                            uint32_t capacity, uint32_t count,
                            const char *line, uint32_t length) {
    if (!history || capacity == 0 || !line || length == 0 ||
        length >= SHELL_HISTORY_LINE_CAPACITY) return count;
    if (count != 0 && history[count - 1U][0] == line[0]) {
        uint32_t index = 0;
        while (index < length && history[count - 1U][index] == line[index])
            ++index;
        if (index == length && history[count - 1U][index] == 0) return count;
    }
    if (count == capacity) {
        for (uint32_t index = 1; index < count; ++index)
            for (uint32_t character = 0;
                 character < SHELL_HISTORY_LINE_CAPACITY; ++character)
                history[index - 1U][character] = history[index][character];
        --count;
    }
    for (uint32_t index = 0; index < length; ++index)
        history[count][index] = line[index];
    history[count][length] = 0;
    return count + 1U;
}

int shell_history_get(char history[][SHELL_HISTORY_LINE_CAPACITY],
                      uint32_t count, uint32_t offset, char *line,
                      uint32_t capacity, uint32_t *length) {
    if (!history || !line || !length || capacity == 0 || offset >= count)
        return 0;
    const char *source = history[count - 1U - offset];
    uint32_t size = 0;
    while (source[size] && size + 1U < SHELL_HISTORY_LINE_CAPACITY) ++size;
    if (size >= capacity) return 0;
    for (uint32_t index = 0; index < size; ++index) line[index] = source[index];
    line[size] = 0;
    *length = size;
    return 1;
}

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

static int shell_has_operator(const char *line, uint32_t length) {
    char quote = 0;
    int escaped = 0;
    for (uint32_t index = 0; index < length; ++index) {
        char value = line[index];
        if (escaped) escaped = 0;
        else if (value == '\\') escaped = 1;
        else if (quote) {
            if (value == quote) quote = 0;
        } else if (value == '\'' || value == '"') quote = value;
        else if (value == '|' || value == '>' || value == '<' || value == '&') return 1;
    }
    return 0;
}

static int shell_operator_syntax_valid(const char *line, uint32_t length) {
    char quote = 0;
    int escaped = 0;
    uint32_t operators = 0;
    for (uint32_t index = 0; index < length; ++index) {
        char value = line[index];
        if (escaped) { escaped = 0; continue; }
        if (value == '\\') { escaped = 1; continue; }
        if (quote) { if (value == quote) quote = 0; continue; }
        if (value == '\'' || value == '"') { quote = value; continue; }
        if (value != '|' && value != '>' && value != '<' && value != '&') continue;
        uint32_t before = index;
        while (before != 0 && (line[before - 1U] == ' ' ||
                               line[before - 1U] == '\t')) --before;
        uint32_t after = index + 1U;
        while (after < length && (line[after] == ' ' || line[after] == '\t')) ++after;
        if (before == 0 || after == length) {
            if (value != '&' || after != length) return 0;
        }
        if (value == '&' && after != length) return 0;
        ++operators;
    }
    return operators != 0 && !escaped && quote == 0;
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
    if (same_word(line, command_length, "version")) return SHELL_VERSION;
    if (same_word(line, command_length, "clear")) return SHELL_CLEAR;
    if (same_word(line, command_length, "id")) return SHELL_ID;
    if (same_word(line, command_length, "ps")) return SHELL_PS;
    if (same_word(line, command_length, "env")) {
        if (command_length == length) return SHELL_ENV;
        if (length >= capacity) return SHELL_UNKNOWN;
        for (uint32_t index = 0; index <= length; ++index)
            argument[index] = line[index];
        return SHELL_RUN;
    }
    if (same_word(line, command_length, "jobs")) return SHELL_JOBS;
    if (same_word(line, command_length, "history")) {
        if (command_length == length) return SHELL_HISTORY;
        uint32_t start = command_length;
        while (start < length && (line[start] == ' ' || line[start] == '\t')) ++start;
        if (start == length || length - start >= capacity) return SHELL_UNKNOWN;
        for (uint32_t index = start; index <= length; ++index)
            argument[index - start] = line[index];
        return SHELL_HISTORY;
    }
    if (same_word(line, command_length, "exit")) return SHELL_EXIT;
    if (!same_word(line, command_length, "cat") &&
        !same_word(line, command_length, "head") &&
        !same_word(line, command_length, "wc") &&
        !same_word(line, command_length, "grep") &&
        !same_word(line, command_length, "tee") &&
        !same_word(line, command_length, "tail") &&
        !same_word(line, command_length, "sort") &&
        !same_word(line, command_length, "uniq") &&
        !same_word(line, command_length, "printf") &&
        !same_word(line, command_length, "basename") &&
        !same_word(line, command_length, "dirname") &&
        !same_word(line, command_length, "cut") &&
        !same_word(line, command_length, "tr") &&
        !same_word(line, command_length, "cmp") &&
        !same_word(line, command_length, "find") &&
        !same_word(line, command_length, "mkdir") &&
        !same_word(line, command_length, "rm") &&
        !same_word(line, command_length, "rmdir") &&
        !same_word(line, command_length, "touch") &&
        !same_word(line, command_length, "write") &&
        !same_word(line, command_length, "uptime") &&
        !same_word(line, command_length, "date") &&
        !same_word(line, command_length, "run") &&
        !same_word(line, command_length, "wait") &&
        !same_word(line, command_length, "which") &&
        !same_word(line, command_length, "inherit") &&
        !same_word(line, command_length, "echo") &&
        !same_word(line, command_length, "pwd") &&
        !same_word(line, command_length, "ls") &&
        !same_word(line, command_length, "cd") &&
        !same_word(line, command_length, "stat") &&
        !same_word(line, command_length, "chmod") &&
        !same_word(line, command_length, "kill") &&
        !same_word(line, command_length, "sleep") &&
        !same_word(line, command_length, "mv") &&
        !same_word(line, command_length, "cp") &&
        !same_word(line, command_length, "setenv") &&
        !same_word(line, command_length, "export") &&
        !same_word(line, command_length, "unsetenv") &&
        !same_word(line, command_length, "read") &&
        !same_word(line, command_length, "uname") &&
        !same_word(line, command_length, "status") &&
        !same_word(line, command_length, "true") &&
        !same_word(line, command_length, "false") &&
        !same_word(line, command_length, "alias") &&
        !same_word(line, command_length, "unalias") &&
        !same_word(line, command_length, "fg") &&
        !same_word(line, command_length, "test")) {
        if (!shell_has_operator(line, length) ||
            !shell_operator_syntax_valid(line, length) || length >= capacity)
            return SHELL_UNKNOWN;
        for (uint32_t index = 0; index <= length; ++index)
            argument[index] = line[index];
        return SHELL_RUN;
    }
    uint32_t start = command_length;
    while (start < length && (line[start] == ' ' || line[start] == '\t')) ++start;
    uint32_t argument_length = length - start;
    if (argument_length >= capacity) return SHELL_UNKNOWN;
    for (uint32_t index = 0; index < argument_length; ++index)
        argument[index] = line[start + index];
    argument[argument_length] = 0;
    if (same_word(line, command_length, "basename") &&
        !shell_has_operator(line, length)) return SHELL_BASENAME;
    if (same_word(line, command_length, "dirname") &&
        !shell_has_operator(line, length)) return SHELL_DIRNAME;
    if (same_word(line, command_length, "uptime") &&
        !shell_has_operator(line, length)) return SHELL_UPTIME;
    if ((same_word(line, command_length, "echo") ||
         same_word(line, command_length, "cat") ||
         same_word(line, command_length, "pwd") ||
         same_word(line, command_length, "ls") ||
         same_word(line, command_length, "head") ||
         same_word(line, command_length, "wc") ||
         same_word(line, command_length, "grep") ||
         same_word(line, command_length, "tee") ||
         same_word(line, command_length, "tail") ||
         same_word(line, command_length, "sort") ||
         same_word(line, command_length, "uniq") ||
         same_word(line, command_length, "printf") ||
         same_word(line, command_length, "basename") ||
         same_word(line, command_length, "dirname") ||
         same_word(line, command_length, "cut") ||
        same_word(line, command_length, "tr") ||
        same_word(line, command_length, "cmp") ||
        same_word(line, command_length, "find") ||
        same_word(line, command_length, "uptime") ||
        same_word(line, command_length, "date")) &&
        shell_has_operator(line, length)) {
        if (!shell_operator_syntax_valid(line, length) || length >= capacity)
            return SHELL_UNKNOWN;
        for (uint32_t index = 0; index <= length; ++index)
            argument[index] = line[index];
        return SHELL_RUN;
    }
    if (same_word(line, command_length, "cd")) return SHELL_CD;
    if (same_word(line, command_length, "cat")) return SHELL_CAT;
    if (same_word(line, command_length, "pwd")) return SHELL_PWD;
    if (same_word(line, command_length, "ls")) return SHELL_LS;
    if (same_word(line, command_length, "head")) return SHELL_HEAD;
    if (same_word(line, command_length, "wc")) return SHELL_WC;
    if (same_word(line, command_length, "grep")) return SHELL_GREP;
    if (same_word(line, command_length, "tee") ||
        same_word(line, command_length, "tail") ||
        same_word(line, command_length, "sort") ||
        same_word(line, command_length, "uniq") ||
        same_word(line, command_length, "printf") ||
        same_word(line, command_length, "basename") ||
        same_word(line, command_length, "dirname") ||
        same_word(line, command_length, "cut") ||
        same_word(line, command_length, "tr") ||
        same_word(line, command_length, "cmp")) return SHELL_RUN;
    if (same_word(line, command_length, "find")) return SHELL_RUN;
    if (same_word(line, command_length, "date")) return SHELL_RUN;
    if (same_word(line, command_length, "test")) return SHELL_RUN;
    if (same_word(line, command_length, "stat")) return SHELL_STAT;
    if (same_word(line, command_length, "chmod")) return SHELL_CHMOD;
    if (same_word(line, command_length, "kill")) return SHELL_KILL;
    if (same_word(line, command_length, "sleep")) return SHELL_SLEEP;
    if (same_word(line, command_length, "mv")) return SHELL_MV;
    if (same_word(line, command_length, "cp")) return SHELL_CP;
    if (same_word(line, command_length, "setenv")) return SHELL_SETENV;
    if (same_word(line, command_length, "export")) return SHELL_EXPORT;
    if (same_word(line, command_length, "unsetenv")) return SHELL_UNSETENV;
    if (same_word(line, command_length, "read")) return SHELL_READ;
    if (same_word(line, command_length, "uname")) return SHELL_UNAME;
    if (same_word(line, command_length, "status")) return SHELL_STATUS;
    if (same_word(line, command_length, "true")) return SHELL_TRUE;
    if (same_word(line, command_length, "false")) return SHELL_FALSE;
    if (same_word(line, command_length, "alias")) return SHELL_ALIAS;
    if (same_word(line, command_length, "unalias")) return SHELL_UNALIAS;
    if (same_word(line, command_length, "fg")) return SHELL_FG;
    if (same_word(line, command_length, "mkdir")) return SHELL_MKDIR;
    if (same_word(line, command_length, "rm")) return SHELL_RM;
    if (same_word(line, command_length, "rmdir")) return SHELL_RMDIR;
    if (same_word(line, command_length, "touch")) return SHELL_TOUCH;
    if (same_word(line, command_length, "write")) return SHELL_WRITE;
    if (same_word(line, command_length, "run")) return SHELL_RUN;
    if (same_word(line, command_length, "wait")) return SHELL_WAIT;
    if (same_word(line, command_length, "which")) return SHELL_WHICH;
    if (same_word(line, command_length, "inherit")) return SHELL_INHERIT;
    return SHELL_ECHO;
}

int shell_unquote_argument(char *argument, uint32_t *length) {
    if (!argument || !length) return 0;
    uint32_t input = 0;
    uint32_t output = 0;
    char quote = 0;
    int escaped = 0;
    while (input < *length) {
        char value = argument[input++];
        if (escaped) {
            argument[output++] = value;
            escaped = 0;
        } else if (value == '\\') {
            escaped = 1;
        } else if (quote) {
            if (value == quote) quote = 0;
            else argument[output++] = value;
        } else if (value == '\'' || value == '"') {
            quote = value;
        } else {
            argument[output++] = value;
        }
    }
    if (quote || escaped) return 0;
    argument[output] = 0;
    *length = output;
    return 1;
}
