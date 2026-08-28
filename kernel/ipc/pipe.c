#include "pipe.h"
#include "../mm/heap/heap.h"

typedef struct {
    ipc_channel_t channel;
    uint32_t references;
} pipe_t;

struct pipe_endpoint {
    pipe_t *pipe;
    uint8_t write;
    uint32_t references;
};

static void pipe_release(pipe_t *pipe) {
    if (!pipe || pipe->references == 0) return;
    if (--pipe->references == 0) {
        ipc_channel_close(&pipe->channel);
        kfree(pipe);
    }
}

int pipe_create(pipe_endpoint_t **read_end, pipe_endpoint_t **write_end) {
    if (!read_end || !write_end) return 0;
    *read_end = 0; *write_end = 0;
    pipe_t *pipe = (pipe_t *)kmalloc(sizeof(*pipe));
    pipe_endpoint_t *reader = pipe ?
        (pipe_endpoint_t *)kmalloc(sizeof(*reader)) : 0;
    pipe_endpoint_t *writer = reader ?
        (pipe_endpoint_t *)kmalloc(sizeof(*writer)) : 0;
    if (!pipe || !reader || !writer) {
        if (writer) kfree(writer);
        if (reader) kfree(reader);
        if (pipe) kfree(pipe);
        return 0;
    }
    ipc_channel_initialize(&pipe->channel);
    pipe->references = 2;
    *reader = (pipe_endpoint_t){pipe, 0, 1};
    *writer = (pipe_endpoint_t){pipe, 1, 1};
    *read_end = reader;
    *write_end = writer;
    return 1;
}

void pipe_endpoint_retain(pipe_endpoint_t *endpoint) {
    if (endpoint && endpoint->references != UINT32_MAX) ++endpoint->references;
}

void pipe_endpoint_release(pipe_endpoint_t *endpoint) {
    if (!endpoint || endpoint->references == 0) return;
    if (--endpoint->references == 0) {
        pipe_t *pipe = endpoint->pipe;
        if (endpoint->write) ipc_channel_close(&pipe->channel);
        kfree(endpoint);
        pipe_release(pipe);
    }
}

int pipe_endpoint_read(pipe_endpoint_t *endpoint, void *buffer, uint32_t capacity) {
    uint32_t size = 0;
    uint64_t sender = 0;
    if (!endpoint || endpoint->write || !buffer || capacity == 0) return 0;
    return ipc_channel_receive_wait(&endpoint->pipe->channel, 0, buffer,
                                    capacity, &sender, &size) ? (int)size : 0;
}

int pipe_endpoint_write(pipe_endpoint_t *endpoint, const void *buffer,
                        uint32_t length) {
    if (!endpoint || !endpoint->write || !buffer || length == 0 ||
        length > IPC_MESSAGE_MAX) return 0;
    return ipc_channel_send_wait(&endpoint->pipe->channel, 0, buffer, length) ?
           (int)length : 0;
}
