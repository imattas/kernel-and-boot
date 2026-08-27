#include "exec.h"

int exec_load_image(address_space_t *space, const void *image,
                    uint64_t image_size, user_image_t *loaded) {
    return user_image_load(space, image, image_size, loaded);
}

void exec_unload_image(address_space_t *space, user_image_t *loaded) {
    user_image_destroy(space, loaded);
}
