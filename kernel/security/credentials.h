#ifndef OS_KERNEL_SECURITY_CREDENTIALS_H
#define OS_KERNEL_SECURITY_CREDENTIALS_H

#include <stdint.h>

enum {
    SECURITY_CAP_SYS_ADMIN = 1ULL << 0,
    SECURITY_CAP_SYS_RAWIO = 1ULL << 1,
    SECURITY_CAP_SYS_BOOT = 1ULL << 2,
    SECURITY_CAP_IPC_OWNER = 1ULL << 3
};

typedef struct {
    uint64_t uid;
    uint64_t gid;
    uint64_t capabilities;
} security_context_t;

void security_context_initialize(security_context_t *context,
                                 uint64_t uid, uint64_t gid,
                                 uint64_t capabilities);
int security_has_capability(const security_context_t *context,
                            uint64_t capability);
int security_can_access(const security_context_t *context,
                        uint64_t owner_uid, uint64_t owner_gid,
                        uint32_t mode, uint32_t requested);

#endif
