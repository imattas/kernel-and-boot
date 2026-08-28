#include <assert.h>
#include <string.h>
#include "../../../userland/shell/shell.h"

int main(void) {
    char argument[32] = {0};
    assert(shell_parse("help", 4, argument, sizeof(argument)) == SHELL_HELP);
    assert(shell_parse("echo hello", 10, argument, sizeof(argument)) == SHELL_ECHO);
    assert(strcmp(argument, "hello") == 0);
    assert(shell_parse("pwd", 3, argument, sizeof(argument)) == SHELL_PWD);
    assert(shell_parse("cd /tmp", 7, argument, sizeof(argument)) == SHELL_CD);
    assert(strcmp(argument, "/tmp") == 0);
    assert(shell_parse("ls", 2, argument, sizeof(argument)) == SHELL_LS);
    assert(shell_parse("exit", 4, argument, sizeof(argument)) == SHELL_EXIT);
    assert(shell_parse("wat", 3, argument, sizeof(argument)) == SHELL_UNKNOWN);
    assert(shell_parse("", 0, argument, sizeof(argument)) == SHELL_EMPTY);
    return 0;
}
