#include "endpoint.h"
#include "../mm/heap/heap.h"

struct ipc_endpoint {
    ipc_channel_t channel;
    uint32_t references;
};

ipc_endpoint_t *ipc_endpoint_create(void) {
    ipc_endpoint_t *endpoint = (ipc_endpoint_t *)kmalloc(sizeof(*endpoint));
    if (!endpoint) return 0;
    ipc_channel_initialize(&endpoint->channel);
    endpoint->references = 1;
    return endpoint;
}

void ipc_endpoint_release(ipc_endpoint_t *endpoint) {
    if (!endpoint || endpoint->references == 0) return;
    if (--endpoint->references == 0) {
        ipc_channel_close(&endpoint->channel);
        kfree(endpoint);
    }
}

int ipc_endpoint_send(ipc_endpoint_t *endpoint, uint64_t sender,
                      const void *data, uint32_t size) {
    return endpoint && ipc_channel_send(&endpoint->channel, sender, data, size);
}

int ipc_endpoint_send_wait(ipc_endpoint_t *endpoint, uint64_t sender,
                           const void *data, uint32_t size) {
    return endpoint && ipc_channel_send_wait(&endpoint->channel, sender, data, size);
}

int ipc_endpoint_receive(ipc_endpoint_t *endpoint, uint64_t receiver,
                         void *data, uint32_t capacity, uint32_t *size) {
    if (!endpoint || !size) return 0;
    uint64_t sender = 0;
    return ipc_channel_receive(&endpoint->channel, receiver, data, capacity,
                               &sender, size);
}

int ipc_endpoint_receive_wait(ipc_endpoint_t *endpoint, uint64_t receiver,
                              void *data, uint32_t capacity, uint32_t *size) {
    if (!endpoint || !size) return 0;
    uint64_t sender = 0;
    return ipc_channel_receive_wait(&endpoint->channel, receiver, data, capacity,
                                    &sender, size);
}
