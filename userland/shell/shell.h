#ifndef OS_USERLAND_SHELL_H
#define OS_USERLAND_SHELL_H

#include <stdint.h>

typedef enum {
    SHELL_EMPTY,
    SHELL_HELP,
    SHELL_ECHO,
    SHELL_PWD,
    SHELL_CD,
    SHELL_LS,
    SHELL_CAT,
    SHELL_MKDIR,
    SHELL_RM,
    SHELL_RMDIR,
    SHELL_TOUCH,
    SHELL_WRITE,
    SHELL_RUN,
    SHELL_EXIT,
    SHELL_UNKNOWN
} shell_command_t;

shell_command_t shell_parse(const char *line, uint32_t length,
                            char *argument, uint32_t capacity);

#endif
