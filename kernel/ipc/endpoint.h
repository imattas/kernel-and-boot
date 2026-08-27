#ifndef OS_KERNEL_IPC_ENDPOINT_H
#define OS_KERNEL_IPC_ENDPOINT_H

#include <stdint.h>
#include "channel.h"

typedef struct ipc_endpoint ipc_endpoint_t;

ipc_endpoint_t *ipc_endpoint_create(void);
void ipc_endpoint_release(ipc_endpoint_t *endpoint);
int ipc_endpoint_send(ipc_endpoint_t *endpoint, uint64_t sender,
                      const void *data, uint32_t size);
int ipc_endpoint_receive(ipc_endpoint_t *endpoint, uint64_t receiver,
                         void *data, uint32_t capacity, uint32_t *size);

#endif
