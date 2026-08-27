#ifndef OS_CORE_PROCESS_USER_IMAGE_H
#define OS_CORE_PROCESS_USER_IMAGE_H

#include <stdint.h>
#include "../../mm/virtual/address_space.h"

#define USER_IMAGE_MAX_PAGES 128

typedef struct {
    uint64_t entry;
    uint32_t page_count;
    uint64_t pages[USER_IMAGE_MAX_PAGES];
    uint64_t virtual_pages[USER_IMAGE_MAX_PAGES];
} user_image_t;

int user_image_load(address_space_t *space, const void *image, uint64_t image_size,
                    user_image_t *loaded);
void user_image_destroy(address_space_t *space, user_image_t *loaded);

#endif
