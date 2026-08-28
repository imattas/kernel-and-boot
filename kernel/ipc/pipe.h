#ifndef OS_KERNEL_IPC_PIPE_H
#define OS_KERNEL_IPC_PIPE_H

#include <stdint.h>
#include "channel.h"

typedef struct pipe_endpoint pipe_endpoint_t;

int pipe_create(pipe_endpoint_t **read_end, pipe_endpoint_t **write_end);
void pipe_endpoint_retain(pipe_endpoint_t *endpoint);
void pipe_endpoint_release(pipe_endpoint_t *endpoint);
int pipe_endpoint_read(pipe_endpoint_t *endpoint, void *buffer, uint32_t capacity);
int pipe_endpoint_write(pipe_endpoint_t *endpoint, const void *buffer,
                        uint32_t length);

#endif
