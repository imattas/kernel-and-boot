#ifndef OS_KERNEL_DRIVERS_STORAGE_H
#define OS_KERNEL_DRIVERS_STORAGE_H

#include <stdint.h>

#define STORAGE_DEVICE_CAPACITY 8U

typedef int (*storage_read_fn)(uint64_t lba, uint32_t count, void *buffer);
typedef int (*storage_write_fn)(uint64_t lba, uint32_t count, const void *buffer);

typedef struct {
    const char *name;
    uint32_t block_size;
    uint64_t block_count;
    storage_read_fn read;
    storage_write_fn write;
} storage_device_t;

void storage_initialize(void);
int storage_register(const storage_device_t *device);
uint32_t storage_device_count(void);
const storage_device_t *storage_device_at(uint32_t index);
int storage_read(uint32_t device, uint64_t lba, uint32_t count, void *buffer);
int storage_write(uint32_t device, uint64_t lba, uint32_t count,
                  const void *buffer);

#endif
