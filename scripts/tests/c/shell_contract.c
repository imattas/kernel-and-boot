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
    assert(shell_parse("clear", 5, argument, sizeof(argument)) == SHELL_CLEAR);
    assert(shell_parse("id", 2, argument, sizeof(argument)) == SHELL_ID);
    assert(shell_parse("ps", 2, argument, sizeof(argument)) == SHELL_PS);
    assert(shell_parse("env", 3, argument, sizeof(argument)) == SHELL_ENV);
    assert(shell_parse("jobs", 4, argument, sizeof(argument)) == SHELL_JOBS);
    assert(shell_parse("history", 7, argument, sizeof(argument)) == SHELL_HISTORY);
    assert(shell_parse("fg 4", 4, argument, sizeof(argument)) == SHELL_FG);
    assert(strcmp(argument, "4") == 0);
    assert(shell_parse("which ARGS.ELF", 14, argument, sizeof(argument)) == SHELL_WHICH);
    assert(strcmp(argument, "ARGS.ELF") == 0);
    assert(shell_parse("inherit 3 off", 13, argument, sizeof(argument)) == SHELL_INHERIT);
    assert(shell_parse("echo hello", 10, argument, sizeof(argument)) == SHELL_ECHO);
    assert(strcmp(argument, "hello") == 0);
    assert(shell_parse("echo hello > /tmp/out", 21, argument,
                       sizeof(argument)) == SHELL_RUN);
    assert(strcmp(argument, "echo hello > /tmp/out") == 0);
    assert(shell_parse("cat /hello > /tmp/out", 21, argument,
                       sizeof(argument)) == SHELL_RUN);
    assert(shell_parse("pwd > /tmp/out", 14, argument,
                       sizeof(argument)) == SHELL_RUN);
    assert(shell_parse("ls > /tmp/out", 13, argument,
                       sizeof(argument)) == SHELL_RUN);
    assert(shell_parse("pwd", 3, argument, sizeof(argument)) == SHELL_PWD);
    assert(shell_parse("cd /tmp", 7, argument, sizeof(argument)) == SHELL_CD);
    assert(strcmp(argument, "/tmp") == 0);
    assert(shell_parse("ls", 2, argument, sizeof(argument)) == SHELL_LS);
    assert(shell_parse("cat /hello", 10, argument, sizeof(argument)) == SHELL_CAT);
    assert(strcmp(argument, "/hello") == 0);
    assert(shell_parse("head /hello", 11, argument, sizeof(argument)) == SHELL_HEAD);
    assert(strcmp(argument, "/hello") == 0);
    assert(shell_parse("wc /hello", 9, argument, sizeof(argument)) == SHELL_WC);
    assert(strcmp(argument, "/hello") == 0);
    assert(shell_parse("grep needle /hello", 18, argument, sizeof(argument)) == SHELL_GREP);
    assert(strcmp(argument, "needle /hello") == 0);
    assert(shell_parse("stat /hello", 11, argument, sizeof(argument)) == SHELL_STAT);
    assert(strcmp(argument, "/hello") == 0);
    assert(shell_parse("chmod 755 /hello", 16, argument, sizeof(argument)) == SHELL_CHMOD);
    assert(strcmp(argument, "755 /hello") == 0);
    assert(shell_parse("kill 4 9", 8, argument, sizeof(argument)) == SHELL_KILL);
    assert(strcmp(argument, "4 9") == 0);
    assert(shell_parse("sleep 25", 8, argument, sizeof(argument)) == SHELL_SLEEP);
    assert(strcmp(argument, "25") == 0);
    assert(shell_parse("mv /old /new", 12, argument, sizeof(argument)) == SHELL_MV);
    assert(strcmp(argument, "/old /new") == 0);
    assert(shell_parse("cp /old /new", 12, argument, sizeof(argument)) == SHELL_CP);
    assert(strcmp(argument, "/old /new") == 0);
    assert(shell_parse("setenv MODE test", 16, argument, sizeof(argument)) == SHELL_SETENV);
    assert(strcmp(argument, "MODE test") == 0);
    assert(shell_parse("unsetenv MODE", 13, argument, sizeof(argument)) == SHELL_UNSETENV);
    assert(strcmp(argument, "MODE") == 0);
    assert(shell_parse("status", 6, argument, sizeof(argument)) == SHELL_STATUS);
    assert(shell_parse("true", 4, argument, sizeof(argument)) == SHELL_TRUE);
    assert(shell_parse("false", 5, argument, sizeof(argument)) == SHELL_FALSE);
    uint32_t argument_length = 13;
    memcpy(argument, "\"hello world\"", argument_length + 1);
    assert(shell_unquote_argument(argument, &argument_length));
    assert(argument_length == 11 && strcmp(argument, "hello world") == 0);
    argument_length = 6;
    memcpy(argument, "'open'", argument_length + 1);
    assert(shell_unquote_argument(argument, &argument_length));
    assert(argument_length == 4 && strcmp(argument, "open") == 0);
    argument_length = 4;
    memcpy(argument, "bad\\", argument_length + 1);
    assert(!shell_unquote_argument(argument, &argument_length));
    char history[2][SHELL_HISTORY_LINE_CAPACITY] = {{0}};
    uint32_t history_count = shell_history_push(history, 2, 0, "first", 5);
    history_count = shell_history_push(history, 2, history_count, "second", 6);
    history_count = shell_history_push(history, 2, history_count, "second", 6);
    assert(history_count == 2);
    uint32_t history_length = 0;
    assert(shell_history_get(history, history_count, 0, argument,
                             sizeof(argument), &history_length));
    assert(history_length == 6 && strcmp(argument, "second") == 0);
    history_count = shell_history_push(history, 2, history_count, "third", 5);
    assert(history_count == 2);
    assert(shell_history_get(history, history_count, 1, argument,
                             sizeof(argument), &history_length));
    assert(history_length == 6 && strcmp(argument, "second") == 0);
    assert(shell_parse("mkdir /tmp", 10, argument, sizeof(argument)) == SHELL_MKDIR);
    assert(strcmp(argument, "/tmp") == 0);
    assert(shell_parse("mkdir -p /tmp/a/b", 17, argument,
                       sizeof(argument)) == SHELL_MKDIR);
    assert(strcmp(argument, "-p /tmp/a/b") == 0);
    assert(shell_parse("rm /tmp/file", 12, argument, sizeof(argument)) == SHELL_RM);
    assert(strcmp(argument, "/tmp/file") == 0);
    assert(shell_parse("rm -r /tmp/tree", 15, argument, sizeof(argument)) == SHELL_RM);
    assert(strcmp(argument, "-r /tmp/tree") == 0);
    assert(shell_parse("rmdir /tmp", 10, argument, sizeof(argument)) == SHELL_RMDIR);
    assert(strcmp(argument, "/tmp") == 0);
    assert(shell_parse("rmdir -p /tmp/a/b", 17, argument,
                       sizeof(argument)) == SHELL_RMDIR);
    assert(strcmp(argument, "-p /tmp/a/b") == 0);
    assert(shell_parse("touch /tmp/file", 15, argument, sizeof(argument)) == SHELL_TOUCH);
    assert(strcmp(argument, "/tmp/file") == 0);
    assert(shell_parse("write /tmp/file hello", 21, argument, sizeof(argument)) == SHELL_WRITE);
    assert(strcmp(argument, "/tmp/file hello") == 0);
    assert(shell_parse("run INIT.ELF hello", 18, argument, sizeof(argument)) == SHELL_RUN);
    assert(strcmp(argument, "INIT.ELF hello") == 0);
    assert(shell_parse("wait 4", 6, argument, sizeof(argument)) == SHELL_WAIT);
    assert(strcmp(argument, "4") == 0);
    assert(shell_parse("exit", 4, argument, sizeof(argument)) == SHELL_EXIT);
    assert(shell_parse("wat", 3, argument, sizeof(argument)) == SHELL_UNKNOWN);
    char sequence[32] = "true; false";
    char remainder[32] = {0};
    uint32_t sequence_length = 11;
    uint32_t remainder_length = 0;
    assert(shell_split_sequence(sequence, &sequence_length, remainder,
                                sizeof(remainder), &remainder_length));
    assert(sequence_length == 4 && strcmp(sequence, "true") == 0);
    assert(remainder_length == 5 && strcmp(remainder, "false") == 0);
    memcpy(sequence, "echo 'a;b'", 11);
    sequence_length = 10;
    remainder_length = 99;
    assert(shell_split_sequence(sequence, &sequence_length, remainder,
                                sizeof(remainder), &remainder_length));
    assert(sequence_length == 10 && remainder_length == 0 &&
           strcmp(sequence, "echo 'a;b'") == 0);
    assert(shell_parse("external | wc /hello", 20, argument,
                       sizeof(argument)) == SHELL_RUN);
    assert(strcmp(argument, "external | wc /hello") == 0);
    assert(shell_parse("", 0, argument, sizeof(argument)) == SHELL_EMPTY);
    return 0;
}
