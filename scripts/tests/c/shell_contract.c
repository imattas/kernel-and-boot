#include <assert.h>
#include <string.h>
#include "../../../userland/shell/shell.h"

int main(void) {
    char argument[32] = {0};
    char line[16] = {0};
    uint32_t line_length = 0;
    assert(shell_edit_line(line, &line_length, sizeof(line) - 1U, 'a') == SHELL_EDIT_CONTINUE);
    assert(shell_edit_line(line, &line_length, sizeof(line) - 1U, 'b') == SHELL_EDIT_CONTINUE);
    assert(line_length == 2 && strcmp(line, "ab") == 0);
    assert(shell_edit_line(line, &line_length, sizeof(line) - 1U, '\b') == SHELL_EDIT_CONTINUE);
    assert(line_length == 1 && strcmp(line, "a") == 0);
    assert(shell_edit_line(line, &line_length, sizeof(line) - 1U, 0x7f) == SHELL_EDIT_CONTINUE);
    assert(line_length == 0 && line[0] == 0);
    shell_edit_line(line, &line_length, sizeof(line) - 1U, 'x');
    shell_edit_line(line, &line_length, sizeof(line) - 1U, 'y');
    assert(shell_edit_line(line, &line_length, sizeof(line) - 1U, 0x15) == SHELL_EDIT_CONTINUE);
    assert(line_length == 0);
    shell_edit_line(line, &line_length, sizeof(line) - 1U, 'z');
    assert(shell_edit_line(line, &line_length, sizeof(line) - 1U, '\n') == SHELL_EDIT_SUBMIT);
    assert(line_length == 1 && line[0] == 'z');
    assert(shell_parse("help", 4, argument, sizeof(argument)) == SHELL_HELP);
    assert(shell_parse("id", 2, argument, sizeof(argument)) == SHELL_ID);
    assert(shell_parse("ps", 2, argument, sizeof(argument)) == SHELL_PS);
    assert(shell_parse("env", 3, argument, sizeof(argument)) == SHELL_ENV);
    assert(shell_parse("which ARGS.ELF", 14, argument, sizeof(argument)) == SHELL_WHICH);
    assert(strcmp(argument, "ARGS.ELF") == 0);
    assert(shell_parse("inherit 3 off", 13, argument, sizeof(argument)) == SHELL_INHERIT);
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
    assert(shell_parse("run INIT.ELF hello", 18, argument, sizeof(argument)) == SHELL_RUN);
    assert(strcmp(argument, "INIT.ELF hello") == 0);
    assert(shell_parse("exit", 4, argument, sizeof(argument)) == SHELL_EXIT);
    assert(shell_parse("wat", 3, argument, sizeof(argument)) == SHELL_UNKNOWN);
    assert(shell_parse("", 0, argument, sizeof(argument)) == SHELL_EMPTY);
    return 0;
}
