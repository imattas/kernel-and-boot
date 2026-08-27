#include "channel.h"
#include "../sched/core/scheduler.h"

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) destination[i] = source[i];
}

void ipc_channel_initialize(ipc_channel_t *channel) {
    spinlock_init(&channel->lock);
    channel->head = 0;
    channel->tail = 0;
    channel->count = 0;
    channel->open = 1;
    task_wait_queue_initialize(&channel->readers);
    task_wait_queue_initialize(&channel->writers);
}

int ipc_channel_send(ipc_channel_t *channel, uint64_t sender,
                     const void *data, uint32_t size) {
    if (!channel || !data || size == 0 || size > IPC_MESSAGE_MAX) return 0;
    uint64_t flags = spinlock_lock_irqsave(&channel->lock);
    if (!channel->open || channel->count == IPC_QUEUE_CAPACITY) {
        spinlock_unlock_irqrestore(&channel->lock, flags);
        return 0;
    }
    ipc_message_t *message = &channel->messages[channel->tail];
    message->sender = sender;
    message->size = size;
    copy_bytes(message->data, (const uint8_t *)data, size);
    channel->tail = (channel->tail + 1U) % IPC_QUEUE_CAPACITY;
    ++channel->count;
    spinlock_unlock_irqrestore(&channel->lock, flags);
    (void)scheduler_wake_one(&channel->readers);
    return 1;
}

int ipc_channel_send_wait(ipc_channel_t *channel, uint64_t sender,
                          const void *data, uint32_t size) {
    if (!channel || !data || size == 0 || size > IPC_MESSAGE_MAX) return 0;
    for (;;) {
        uint64_t flags = spinlock_lock_irqsave(&channel->lock);
        if (!channel->open) {
            spinlock_unlock_irqrestore(&channel->lock, flags);
            return 0;
        }
        if (channel->count != IPC_QUEUE_CAPACITY) {
            ipc_message_t *message = &channel->messages[channel->tail];
            message->sender = sender;
            message->size = size;
            copy_bytes(message->data, (const uint8_t *)data, size);
            channel->tail = (channel->tail + 1U) % IPC_QUEUE_CAPACITY;
            ++channel->count;
            spinlock_unlock_irqrestore(&channel->lock, flags);
            (void)scheduler_wake_one(&channel->readers);
            return 1;
        }
        if (!scheduler_block_current_with_lock(&channel->writers, &channel->lock, flags))
            return 0;
    }
}

int ipc_channel_receive(ipc_channel_t *channel, uint64_t receiver,
                        void *data, uint32_t capacity,
                        uint64_t *sender, uint32_t *size) {
    (void)receiver;
    if (!channel || !data || !sender || !size) return 0;
    uint64_t flags = spinlock_lock_irqsave(&channel->lock);
    if (channel->count == 0 || capacity < channel->messages[channel->head].size) {
        spinlock_unlock_irqrestore(&channel->lock, flags);
        return 0;
    }
    ipc_message_t *message = &channel->messages[channel->head];
    *sender = message->sender;
    *size = message->size;
    copy_bytes((uint8_t *)data, message->data, message->size);
    channel->head = (channel->head + 1U) % IPC_QUEUE_CAPACITY;
    --channel->count;
    spinlock_unlock_irqrestore(&channel->lock, flags);
    (void)scheduler_wake_one(&channel->writers);
    return 1;
}

int ipc_channel_receive_wait(ipc_channel_t *channel, uint64_t receiver,
                             void *data, uint32_t capacity,
                             uint64_t *sender, uint32_t *size) {
    (void)receiver;
    if (!channel || !data || !sender || !size) return 0;
    for (;;) {
        uint64_t flags = spinlock_lock_irqsave(&channel->lock);
        if (channel->count != 0 && capacity >= channel->messages[channel->head].size) {
            ipc_message_t *message = &channel->messages[channel->head];
            *sender = message->sender;
            *size = message->size;
            copy_bytes((uint8_t *)data, message->data, message->size);
            channel->head = (channel->head + 1U) % IPC_QUEUE_CAPACITY;
            --channel->count;
            spinlock_unlock_irqrestore(&channel->lock, flags);
            (void)scheduler_wake_one(&channel->writers);
            return 1;
        }
        if (!channel->open || (channel->count != 0 &&
                               capacity < channel->messages[channel->head].size)) {
            spinlock_unlock_irqrestore(&channel->lock, flags);
            return 0;
        }
        if (!scheduler_block_current_with_lock(&channel->readers, &channel->lock, flags))
            return 0;
    }
}

static void wake_all(task_wait_queue_t *queue) {
    while (scheduler_wake_one(queue)) { }
}

int ipc_channel_close(ipc_channel_t *channel) {
    if (!channel) return 0;
    uint64_t flags = spinlock_lock_irqsave(&channel->lock);
    if (!channel->open) {
        spinlock_unlock_irqrestore(&channel->lock, flags);
        return 0;
    }
    channel->open = 0;
    spinlock_unlock_irqrestore(&channel->lock, flags);
    wake_all(&channel->readers);
    wake_all(&channel->writers);
    return 1;
}

uint32_t ipc_channel_count(const ipc_channel_t *channel) {
    if (!channel) return 0;
    ipc_channel_t *mutable_channel = (ipc_channel_t *)channel;
    uint64_t flags = spinlock_lock_irqsave(&mutable_channel->lock);
    uint32_t count = mutable_channel->count;
    spinlock_unlock_irqrestore(&mutable_channel->lock, flags);
    return count;
}
