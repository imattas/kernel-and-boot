#ifndef OS_KERNEL_EXEC_H
#define OS_KERNEL_EXEC_H

#include <stdint.h>
#include "../core/process/user_image.h"

int exec_load_image(address_space_t *space, const void *image,
                    uint64_t image_size, user_image_t *loaded);
void exec_unload_image(address_space_t *space, user_image_t *loaded);

#endif
