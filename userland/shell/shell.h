#ifndef OS_USERLAND_SHELL_H
#define OS_USERLAND_SHELL_H

#include <stdint.h>

typedef enum {
    SHELL_EMPTY,
    SHELL_HELP,
    SHELL_CLEAR,
    SHELL_ID,
    SHELL_PS,
    SHELL_ENV,
    SHELL_JOBS,
    SHELL_HISTORY,
    SHELL_FG,
    SHELL_WHICH,
    SHELL_INHERIT,
    SHELL_ECHO,
    SHELL_PWD,
    SHELL_CD,
    SHELL_LS,
    SHELL_CAT,
    SHELL_HEAD,
    SHELL_WC,
    SHELL_GREP,
    SHELL_STAT,
    SHELL_CHMOD,
    SHELL_KILL,
    SHELL_SLEEP,
    SHELL_MV,
    SHELL_CP,
    SHELL_SETENV,
    SHELL_UNSETENV,
    SHELL_STATUS,
    SHELL_TRUE,
    SHELL_FALSE,
    SHELL_MKDIR,
    SHELL_RM,
    SHELL_RMDIR,
    SHELL_TOUCH,
    SHELL_WRITE,
    SHELL_RUN,
    SHELL_WAIT,
    SHELL_EXIT,
    SHELL_UNKNOWN
} shell_command_t;

typedef enum {
    SHELL_EDIT_CONTINUE,
    SHELL_EDIT_SUBMIT,
    SHELL_EDIT_CANCEL
} shell_edit_result_t;

#define SHELL_HISTORY_LINE_CAPACITY 128

uint32_t shell_history_push(char history[][SHELL_HISTORY_LINE_CAPACITY],
                            uint32_t capacity, uint32_t count,
                            const char *line, uint32_t length);
int shell_history_get(char history[][SHELL_HISTORY_LINE_CAPACITY],
                      uint32_t count, uint32_t offset, char *line,
                      uint32_t capacity, uint32_t *length);

shell_command_t shell_parse(const char *line, uint32_t length,
                            char *argument, uint32_t capacity);

int shell_unquote_argument(char *argument, uint32_t *length);

int shell_split_sequence(char *line, uint32_t *length,
                         char *remainder, uint32_t capacity,
                         uint32_t *remainder_length);

shell_edit_result_t shell_edit_line(char *line, uint32_t *length,
                                    uint32_t capacity, char value);

#endif
