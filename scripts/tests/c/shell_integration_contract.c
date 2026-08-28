#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../../../userland/shell/shell.h"

static void expect_command(const char *line, shell_command_t expected,
                           const char *argument) {
    char parsed[128] = {0};
    uint32_t length = (uint32_t)strlen(line);
    assert(shell_parse(line, length, parsed, sizeof(parsed)) == expected);
    if (argument) assert(strcmp(parsed, argument) == 0);
}

int main(void) {
    expect_command("mkdir /tmp/work", SHELL_MKDIR, "/tmp/work");
    expect_command("touch /tmp/work/file", SHELL_TOUCH, "/tmp/work/file");
    expect_command("write /tmp/work/file payload", SHELL_WRITE,
                   "/tmp/work/file payload");
    expect_command("mv /tmp/work/file /tmp/work/renamed", SHELL_MV,
                   "/tmp/work/file /tmp/work/renamed");
    expect_command("cp /tmp/work/renamed /tmp/work/copy", SHELL_CP,
                   "/tmp/work/renamed /tmp/work/copy");
    expect_command("rm /tmp/work/copy", SHELL_RM, "/tmp/work/copy");
    expect_command("rmdir /tmp/work", SHELL_RMDIR, "/tmp/work");

    expect_command("echo input | grep input | wc | head", SHELL_RUN,
                   "echo input | grep input | wc | head");
    expect_command("echo input > /tmp/out", SHELL_RUN,
                   "echo input > /tmp/out");
    expect_command("echo input >> /tmp/out", SHELL_RUN,
                   "echo input >> /tmp/out");
    expect_command("wc < /tmp/out", SHELL_RUN, "wc < /tmp/out");
    expect_command("run true &", SHELL_RUN, "true &");
    expect_command("run true | wc", SHELL_RUN, "true | wc");

    expect_command("export MODE=test", SHELL_EXPORT, "MODE=test");
    expect_command("env MODE=test ECHO.ELF hello", SHELL_RUN,
                   "env MODE=test ECHO.ELF hello");
    expect_command("unsetenv MODE OTHER", SHELL_UNSETENV, "MODE OTHER");

    expect_command("echo x |", SHELL_UNKNOWN, 0);
    expect_command("echo x >>", SHELL_UNKNOWN, 0);
    expect_command("echo x & extra", SHELL_UNKNOWN, 0);
    return 0;
}
