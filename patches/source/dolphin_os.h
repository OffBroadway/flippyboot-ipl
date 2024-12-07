#pragma once

#include <gctypes.h>
#include "config.h"

#include "../../cubeboot/include/gcm.h"

#define DEFAULT_THREAD_PRIO 0x10

// gcbool
typedef unsigned int BOOL;

typedef s16 __OSInterrupt;
typedef u32  OSInterruptMask;

typedef u16 OSThreadState;
typedef s32 OSPriority;  //  0 highest, 31 lowest

typedef void* (*OSThreadStartFunction)(void*);

typedef struct OSThread OSThread;
typedef struct OSThreadQueue OSThreadQueue;
typedef struct OSThreadLink OSThreadLink;

typedef struct OSMutex OSMutex;
typedef struct OSMutexQueue OSMutexQueue;
typedef struct OSMutexLink OSMutexLink;
typedef struct OSCond OSCond;

struct OSThreadLink {
    OSThread* next;
    OSThread* prev;
};

struct OSThreadQueue {
    /* 0x0 */ OSThread* head;
    /* 0x4 */ OSThread* tail;
};

struct OSMutexLink {
    OSMutex* next;
    OSMutex* prev;
};

struct OSMutexQueue {
    OSMutex* head;
    OSMutex* tail;
};

typedef struct OSMutex {
    /* 0x00 */ OSThreadQueue queue;
    /* 0x08 */ OSThread* thread;
    /* 0x0C */ s32 count;
    /* 0x10 */ OSMutexLink link;
} OSMutex;  // Size: 0x18

typedef struct OSCond {
    OSThreadQueue queue;
} OSCond;

typedef struct OSContext {
    /* 0x000 */ u32 gpr[32];
    /* 0x080 */ u32 cr;
    /* 0x084 */ u32 lr;
    /* 0x088 */ u32 ctr;
    /* 0x08C */ u32 xer;
    /* 0x090 */ f64 fpr[32];
    /* 0x190 */ u32 field_0x190;
    /* 0x194 */ u32 fpscr;
    /* 0x198 */ u32 srr0;
    /* 0x19C */ u32 srr1;
    /* 0x1A0 */ u16 mode;
    /* 0x1A2 */ u16 state;
    /* 0x1A4 */ u32 gqr[8];
    /* 0x1C4 */ f64 ps[32];
} OSContext;

struct OSThread {
    OSContext context;
    OSThreadState state;
    u16 attributes;
    s32 suspend_count;
    s32 effective_priority;
    u32 base_priority;
    void* exit_value;
    OSThreadQueue* queue;
    OSThreadLink link;
    OSThreadQueue join_queue;
    OSMutex* mutex;
    OSMutexQueue owned_mutexes;
    OSThreadLink active_threads_link;
    u8* stack_base;
    u32* stack_end;
    u8* error_code;
    void* data[2];
};

typedef void* OSMessage;

typedef struct OSMessageQueue {
    /* 0x00 */ OSThreadQueue sending_queue;
    /* 0x08 */ OSThreadQueue receiving_queue;
    /* 0x10 */ OSMessage* message_array;
    /* 0x14 */ s32 num_messages;
    /* 0x18 */ s32 first_index;
    /* 0x1C */ s32 num_used;
} OSMessageQueue;

// Flags to turn blocking on/off when sending/receiving message
#define OS_MESSAGE_NOBLOCK 0
#define OS_MESSAGE_BLOCK 1

typedef enum {
	OS_MSG_PERSISTENT = (1 << 0),
} OSMessageFlags;

struct dolphin_debugger_info {
	uint32_t		present;
	uint32_t		exception_mask;
	uint32_t		exception_hook_address;
	uint32_t		saved_lr;
	unsigned char		__pad1[0x10];
} __attribute__ ((__packed__));

struct dolphin_lowmem {
	struct gcm_disk_info	b_disk_info;

	uint32_t		a_boot_magic;
	uint32_t		a_version;

	uint32_t		b_physical_memory_size;
	uint32_t		b_console_type;

	uint32_t		a_arena_lo;
	uint32_t		a_arena_hi;
	void			*a_fst;
	uint32_t		a_fst_max_size;

	struct dolphin_debugger_info a_debugger_info;
	unsigned char		hook_code[0x60];

	uint32_t		o_current_os_context_phys;
	uint32_t		o_previous_os_interrupt_mask;
	uint32_t		o_current_os_interrupt_mask;

	uint32_t		tv_mode;
	uint32_t		b_aram_size;

	void			*o_current_os_context;
	void			*o_default_os_thread;
	void			*o_thread_queue_head;
	void			*o_thread_queue_tail;
	void			*o_current_os_thread;

	uint32_t		a_debug_monitor_size;
	void			*a_debug_monitor;

	uint32_t		a_simulated_memory_size;

	void			*a_bi2;

	uint32_t		b_bus_clock_speed;
	uint32_t		b_cpu_clock_speed;
} __attribute__ ((__packed__));

extern OSThread* __OSCurrentThread;
extern OSThreadQueue __OSActiveThreadQueue;

void OSYieldThread();
BOOL OSJoinThread(OSThread *thread, void * val);
void __OSPromoteThread(OSThread *thread, s32 priority);
BOOL dolphin_OSCreateThread(OSThread *thread, OSThreadStartFunction func, void* param, void* stack, u32 stackSize, s32 priority, u16 attr);
s32 dolphin_OSResumeThread(OSThread *thread);

void OSInitMutex(OSMutex* mutex);
void OSLockMutex(OSMutex* mutex);
void OSUnlockMutex(OSMutex* mutex);
void __OSUnlockAllMutex(OSThread* thread);
BOOL OSTryLockMutex(OSMutex* mutex);

void OSInitCond(OSCond* cond);
void OSWaitCond(OSCond* cond, OSMutex* mutex);
void OSSignalCond(OSCond* cond);

void OSInitMessageQueue(OSMessageQueue* mq, OSMessage* msgArray, s32 msgCount);
BOOL OSSendMessage(OSMessageQueue* mq, OSMessage msg, s32 flags);
BOOL OSReceiveMessage(OSMessageQueue* mq, OSMessage* msg, s32 flags);
