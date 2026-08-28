#include <assert.h>
#include <string.h>
#include "../../../userland/shell/shell.h"

int main(void) {
    char argument[32] = {0};
    assert(shell_parse("help", 4, argument, sizeof(argument)) == SHELL_HELP);
    assert(shell_parse("id", 2, argument, sizeof(argument)) == SHELL_ID);
    assert(shell_parse("ps", 2, argument, sizeof(argument)) == SHELL_PS);
    assert(shell_parse("echo hello", 10, argument, sizeof(argument)) == SHELL_ECHO);
    assert(strcmp(argument, "hello") == 0);
    assert(shell_parse("pwd", 3, argument, sizeof(argument)) == SHELL_PWD);
    assert(shell_parse("cd /tmp", 7, argument, sizeof(argument)) == SHELL_CD);
    assert(strcmp(argument, "/tmp") == 0);
    assert(shell_parse("ls", 2, argument, sizeof(argument)) == SHELL_LS);
    assert(shell_parse("cat /hello", 10, argument, sizeof(argument)) == SHELL_CAT);
    assert(strcmp(argument, "/hello") == 0);
    assert(shell_parse("mkdir /tmp", 10, argument, sizeof(argument)) == SHELL_MKDIR);
    assert(strcmp(argument, "/tmp") == 0);
    assert(shell_parse("rm /tmp/file", 12, argument, sizeof(argument)) == SHELL_RM);
    assert(strcmp(argument, "/tmp/file") == 0);
    assert(shell_parse("rmdir /tmp", 10, argument, sizeof(argument)) == SHELL_RMDIR);
    assert(strcmp(argument, "/tmp") == 0);
    assert(shell_parse("touch /tmp/file", 15, argument, sizeof(argument)) == SHELL_TOUCH);
    assert(strcmp(argument, "/tmp/file") == 0);
    assert(shell_parse("write /tmp/file hello", 21, argument, sizeof(argument)) == SHELL_WRITE);
    assert(strcmp(argument, "/tmp/file hello") == 0);
    assert(shell_parse("run INIT.ELF", 12, argument, sizeof(argument)) == SHELL_RUN);
    assert(strcmp(argument, "INIT.ELF") == 0);
    assert(shell_parse("exit", 4, argument, sizeof(argument)) == SHELL_EXIT);
    assert(shell_parse("wat", 3, argument, sizeof(argument)) == SHELL_UNKNOWN);
    assert(shell_parse("", 0, argument, sizeof(argument)) == SHELL_EMPTY);
    return 0;
}
