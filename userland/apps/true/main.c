#include "../../lib/os.h"

int true_main(uint64_t argc, char **argv, char **environment) {
    (void)argc;
    (void)argv;
    (void)environment;
    os_exit(0);
}
