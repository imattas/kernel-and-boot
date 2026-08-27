#include "credentials.h"

void security_context_initialize(security_context_t *context,
                                 uint64_t uid, uint64_t gid,
                                 uint64_t capabilities) {
    if (!context) return;
    context->uid = uid;
    context->gid = gid;
    context->capabilities = capabilities;
}

int security_has_capability(const security_context_t *context,
                            uint64_t capability) {
    return context && capability != 0 &&
           (context->capabilities & capability) == capability;
}

int security_can_access(const security_context_t *context,
                        uint64_t owner_uid, uint64_t owner_gid,
                        uint32_t mode, uint32_t requested) {
    if (!context || requested == 0 || (requested & ~7U) != 0) return 0;
    if (context->uid == 0) return 1;

    uint32_t permission;
    if (context->uid == owner_uid) permission = (mode >> 6) & 7U;
    else if (context->gid == owner_gid) permission = (mode >> 3) & 7U;
    else permission = mode & 7U;
    return (permission & requested) == requested;
}
