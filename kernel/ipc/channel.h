#ifndef OS_KERNEL_IPC_CHANNEL_H
#define OS_KERNEL_IPC_CHANNEL_H

#include <stdint.h>
#include "../core/sync/spinlock.h"
#include "../core/task/wait_queue.h"

#define IPC_MESSAGE_MAX 64U
#define IPC_QUEUE_CAPACITY 16U

typedef struct {
    uint64_t sender;
    uint32_t size;
    uint8_t data[IPC_MESSAGE_MAX];
} ipc_message_t;

typedef struct {
    spinlock_t lock;
    ipc_message_t messages[IPC_QUEUE_CAPACITY];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint8_t open;
    task_wait_queue_t readers;
    task_wait_queue_t writers;
} ipc_channel_t;

void ipc_channel_initialize(ipc_channel_t *channel);
int ipc_channel_send(ipc_channel_t *channel, uint64_t sender,
                     const void *data, uint32_t size);
int ipc_channel_send_wait(ipc_channel_t *channel, uint64_t sender,
                          const void *data, uint32_t size);
int ipc_channel_receive(ipc_channel_t *channel, uint64_t receiver,
                        void *data, uint32_t capacity,
                        uint64_t *sender, uint32_t *size);
int ipc_channel_receive_wait(ipc_channel_t *channel, uint64_t receiver,
                             void *data, uint32_t capacity,
                             uint64_t *sender, uint32_t *size);
int ipc_channel_close(ipc_channel_t *channel);
uint32_t ipc_channel_count(const ipc_channel_t *channel);

#endif
