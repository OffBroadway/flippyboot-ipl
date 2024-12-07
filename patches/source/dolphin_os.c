#include "dolphin_os.h"
#include "reloc.h"
#include "attr.h"

__attribute_reloc__ OSThread* (*SelectThread)(BOOL);
__attribute_reloc__ vs32 *Reschedule;

__attribute_reloc__ BOOL (*OSCreateThread)(OSThread *thread, OSThreadStartFunction func, void* param, void* stack, u32 stackSize, s32 priority, u16 attr);
__attribute_reloc__ s32 (*OSResumeThread)(OSThread *thread);

__attribute_reloc__ OSThread* (*SetEffectivePriority)(OSThread* thread, s32 priority);
__attribute_reloc__ s32 (*__OSGetEffectivePriority)(OSThread* thread);

__attribute_reloc__ void (*OSSleepThread)(OSThreadQueue* queue);
__attribute_reloc__ void (*OSWakeupThread)(OSThreadQueue* queue);

extern OSThread* __OSCurrentThread;
extern OSThreadQueue __OSActiveThreadQueue;

// from https://github.com/projectPiki/pikmin2/blob/de4b757bcb49e10b6d6822e6198be93c01890dcf/src/Dolphin/os/OSThread.c#L336-L343
void OSYieldThread() {
    BOOL enabled = OSDisableInterrupts();
    SelectThread(TRUE);
    OSRestoreInterrupts(enabled);
}

BOOL dolphin_OSCreateThread(OSThread *thread, OSThreadStartFunction func, void* param, void* stack, u32 stackSize, s32 priority, u16 attr) {
    return OSCreateThread(thread, func, param, stack, stackSize, priority, attr);
}

s32 dolphin_OSResumeThread(OSThread *thread) {
    return OSResumeThread(thread);
}

// void dolphin_OSWakeupThread(OSThreadQueue* queue) {
//     OSWakeupThread(queue);
// }

// from https://github.com/zeldaret/tp/blob/a61e3491f7c46b698514af50464cf71ba76bd3a3/libs/dolphin/os/OSThread.c#L196
OSThread* OSGetCurrentThread(void) {
    return __OSCurrentThread;
}

void OSInitThreadQueue(OSThreadQueue* queue) {
    queue->tail = NULL;
    queue->head = NULL;
}

// from https://github.com/zeldaret/tww/blob/4d5aeeefc6329a307613c2be0128084c6ed8b88f/src/dolphin/os/OSThread.c#L538-L559
#define RemoveItem(queue, thread, link)                                                            \
    do {                                                                                           \
        OSThread *next, *prev;                                                                     \
        next = (thread)->link.next;                                                                \
        prev = (thread)->link.prev;                                                                \
        if (next == NULL)                                                                          \
            (queue)->tail = prev;                                                                  \
        else                                                                                       \
            next->link.prev = prev;                                                                \
        if (prev == NULL)                                                                          \
            (queue)->head = next;                                                                  \
        else                                                                                       \
            prev->link.next = next;                                                                \
    } while (0)

static BOOL __OSIsThreadActive(OSThread* thread) {
    OSThread* active;

    if (thread->state == 0) {
        return FALSE;
    }

    for (active = __OSActiveThreadQueue.head; active; active = active->active_threads_link.next) {
        if (thread == active) {
            return TRUE;
        }
    }

    return FALSE;
}

BOOL OSJoinThread(OSThread *thread, void *val) {
    int enabled = OSDisableInterrupts();

    if (!(thread->attributes & 1) && (thread->state != 8) && (thread->join_queue.head == NULL)) {
        OSSleepThread(&thread->join_queue);
        if (__OSIsThreadActive(thread) == 0) {
            OSRestoreInterrupts(enabled);
            return 0;
        }
    }
    if (thread->state == 8) {
        if (val) {
            *(s32*)val = (s32)thread->exit_value;
        }
        RemoveItem(&__OSActiveThreadQueue, thread, active_threads_link);
        thread->state = 0;
        OSRestoreInterrupts(enabled);
        return 1;
    }
    OSRestoreInterrupts(enabled);
    return 0;
}

void __OSPromoteThread(OSThread* thread, s32 priority) {
    do {
        if (thread->suspend_count > 0 || thread->effective_priority <= priority) {
            break;
        }

        thread = SetEffectivePriority(thread, priority);
    } while (thread != NULL);
}

// from https://github.com/zeldaret/tp/blob/fcf137a90218064c0d4218abadf934538ed43671/libs/dolphin/os/OSMutex.c
#define PushTail(queue, mutex, link)                                                               \
    do {                                                                                           \
        OSMutex* __prev;                                                                           \
                                                                                                   \
        __prev = (queue)->tail;                                                                    \
        if (__prev == NULL)                                                                        \
            (queue)->head = (mutex);                                                               \
        else                                                                                       \
            __prev->link.next = (mutex);                                                           \
        (mutex)->link.prev = __prev;                                                               \
        (mutex)->link.next = NULL;                                                                 \
        (queue)->tail = (mutex);                                                                   \
    } while (0)

#define PopHead(queue, mutex, link)                                                                \
    do {                                                                                           \
        OSMutex* __next;                                                                           \
                                                                                                   \
        (mutex) = (queue)->head;                                                                   \
        __next = (mutex)->link.next;                                                               \
        if (__next == NULL)                                                                        \
            (queue)->tail = NULL;                                                                  \
        else                                                                                       \
            __next->link.prev = NULL;                                                              \
        (queue)->head = __next;                                                                    \
    } while (0)

#define PopItem(queue, mutex, link)                                                                \
    do {                                                                                           \
        OSMutex* __next;                                                                           \
        OSMutex* __prev;                                                                           \
                                                                                                   \
        __next = (mutex)->link.next;                                                               \
        __prev = (mutex)->link.prev;                                                               \
                                                                                                   \
        if (__next == NULL)                                                                        \
            (queue)->tail = __prev;                                                                \
        else                                                                                       \
            __next->link.prev = __prev;                                                            \
                                                                                                   \
        if (__prev == NULL)                                                                        \
            (queue)->head = __next;                                                                \
        else                                                                                       \
            __prev->link.next = __next;                                                            \
    } while (0)

//
// Declarations:
//

void OSInitMutex(OSMutex* mutex) {
    OSInitThreadQueue(&mutex->queue);
    mutex->thread = 0;
    mutex->count = 0;
}

void OSLockMutex(OSMutex* mutex) {
    BOOL enabled = OSDisableInterrupts();
    OSThread* currentThread = OSGetCurrentThread();
    OSThread* ownerThread;

    while (TRUE) {
        ownerThread = ((OSMutex*)mutex)->thread;
        if (ownerThread == 0) {
            mutex->thread = currentThread;
            mutex->count++;
            PushTail(&currentThread->owned_mutexes, mutex, link);
            break;
        } else if (ownerThread == currentThread) {
            mutex->count++;
            break;
        } else {
            currentThread->mutex = mutex;
            __OSPromoteThread(mutex->thread, currentThread->effective_priority);
            OSSleepThread(&mutex->queue);
            currentThread->mutex = 0;
        }
    }
    OSRestoreInterrupts(enabled);
}

void OSUnlockMutex(OSMutex* mutex) {
    BOOL enabled = OSDisableInterrupts();
    OSThread* currentThread = OSGetCurrentThread();

    if (mutex->thread == currentThread && --mutex->count == 0) {
        PopItem(&currentThread->owned_mutexes, mutex, link);
        mutex->thread = NULL;
        if ((s32)currentThread->effective_priority < (s32)currentThread->base_priority) {
            currentThread->effective_priority = __OSGetEffectivePriority(currentThread);
        }

        OSWakeupThread(&mutex->queue);
    }
    OSRestoreInterrupts(enabled);
}

void __OSUnlockAllMutex(OSThread* thread) {
    OSMutex* mutex;

    while (thread->owned_mutexes.head) {
        PopHead(&thread->owned_mutexes, mutex, link);
        mutex->count = 0;
        mutex->thread = NULL;
        OSWakeupThread(&mutex->queue);
    }
}

BOOL OSTryLockMutex(OSMutex* mutex) {
    BOOL enabled = OSDisableInterrupts();
    OSThread* currentThread = OSGetCurrentThread();
    BOOL locked;
    if (mutex->thread == 0) {
        mutex->thread = currentThread;
        mutex->count++;
        PushTail(&currentThread->owned_mutexes, mutex, link);
        locked = TRUE;
    } else if (mutex->thread == currentThread) {
        mutex->count++;
        locked = TRUE;
    } else {
        locked = FALSE;
    }
    OSRestoreInterrupts(enabled);
    return locked;
}

// from https://github.com/doldecomp/melee/blob/e88fcc1b140dbd04ca4341376139300d0cb698fa/src/dolphin/os/OSThread.c#L28
s32 OSDisableScheduler(void) {
    bool enabled;
    s32 count;

    enabled = OSDisableInterrupts();
    count = *Reschedule;
    *Reschedule = count + 1;
    OSRestoreInterrupts(enabled);
    return count;
}

s32 OSEnableScheduler(void) {
    bool enabled;
    s32 count;

    enabled = OSDisableInterrupts();
    count = *Reschedule;
    *Reschedule = count - 1;
    OSRestoreInterrupts(enabled);
    return count;
}

void OSInitCond(OSCond* cond) { OSInitThreadQueue(&cond->queue); }

void OSWaitCond(OSCond* cond, OSMutex* mutex) {
    BOOL enabled = OSDisableInterrupts();
    OSThread* currentThread = OSGetCurrentThread();
    s32 count;

    if (mutex->thread == currentThread) {
        count = mutex->count;
        mutex->count = 0;
        PopItem(&currentThread->owned_mutexes, mutex, link);
        mutex->thread = NULL;

        if (currentThread->effective_priority < (s32)currentThread->base_priority) {
            currentThread->effective_priority = __OSGetEffectivePriority(currentThread);
        }

        OSDisableScheduler();
        OSWakeupThread(&mutex->queue);
        OSEnableScheduler();
        OSSleepThread(&cond->queue);
        OSLockMutex(mutex);
        mutex->count = count;
    }

    OSRestoreInterrupts(enabled);
}

void OSSignalCond(OSCond* cond) {
    OSWakeupThread(&cond->queue);
}

// from https://github.com/zeldaret/tp/blob/a61e3491f7c46b698514af50464cf71ba76bd3a3/libs/dolphin/os/OSMessage.c
void OSInitMessageQueue(OSMessageQueue* mq, OSMessage* msgArray, s32 msgCount) {
    OSInitThreadQueue(&mq->sending_queue);
    OSInitThreadQueue(&mq->receiving_queue);
    mq->message_array = msgArray;
    mq->num_messages = msgCount;
    mq->first_index = 0;
    mq->num_used = 0;
}

BOOL OSSendMessage(OSMessageQueue* mq, OSMessage msg, s32 flags) {
    BOOL enabled;
    s32 lastIndex;

    enabled = OSDisableInterrupts();

    while (mq->num_messages <= mq->num_used) {
        if (!(flags & OS_MESSAGE_BLOCK)) {
            OSRestoreInterrupts(enabled);
            return FALSE;
        } else {
            OSSleepThread(&mq->sending_queue);
        }
    }

    lastIndex = (mq->first_index + mq->num_used) % mq->num_messages;
    mq->message_array[lastIndex] = msg;
    mq->num_used++;

    OSWakeupThread(&mq->receiving_queue);

    OSRestoreInterrupts(enabled);
    return TRUE;
}

BOOL OSReceiveMessage(OSMessageQueue* mq, OSMessage* msg, s32 flags) {
    BOOL enabled;

    enabled = OSDisableInterrupts();

    while (mq->num_used == 0) {
        if (!(flags & OS_MESSAGE_BLOCK)) {
            OSRestoreInterrupts(enabled);
            return FALSE;
        } else {
            OSSleepThread(&mq->receiving_queue);
        }
    }

    if (msg != NULL) {
        *msg = mq->message_array[mq->first_index];
    }
    mq->first_index = (mq->first_index + 1) % mq->num_messages;
    mq->num_used--;

    OSWakeupThread(&mq->sending_queue);

    OSRestoreInterrupts(enabled);
    return TRUE;
}
