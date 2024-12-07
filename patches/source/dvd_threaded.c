#include "dvd_threaded.h"
#include "dolphin_os.h"
#include "decomp_os.h"
#include "os.h"

#include "picolibc.h"

#include "flippy_sync.h"
#include "usbgecko.h"

// DI regs from YAGCD
#define DI_SR      0 // 0xCC006000 - DI Status Register
#define DI_SR_BRKINT     (1 << 6) // Break Complete Interrupt Status
#define DI_SR_BRKINTMASK (1 << 5) // Break Complete Interrupt Mask. 0:masked, 1:enabled
#define DI_SR_TCINT      (1 << 4) // Transfer Complete Interrupt Status
#define DI_SR_TCINTMASK  (1 << 3) // Transfer Complete Interrupt Mask. 0:masked, 1:enabled
#define DI_SR_DEINT      (1 << 2) // Device Error Interrupt Status
#define DI_SR_DEINTMASK  (1 << 1) // Device Error Interrupt Mask. 0:masked, 1:enabled
#define DI_SR_BRK        (1 << 0) // DI Break

#define DI_CVR     1 // 0xCC006004 - DI Cover Register (status2)
#define DI_CMDBUF0 2 // 0xCC006008 - DI Command Buffer 0
#define DI_CMDBUF1 3 // 0xCC00600c - DI Command Buffer 1 (offset in 32 bit words)
#define DI_CMDBUF2 4 // 0xCC006010 - DI Command Buffer 2 (source length)
#define DI_MAR     5 // 0xCC006014 - DMA Memory Address Register
#define DI_LENGTH  6 // 0xCC006018 - DI DMA Transfer Length Register
#define DI_CR      7 // 0xCC00601c - DI Control Register
#define DI_CR_RW     (1 << 2) // access mode, 0:read, 1:write
#define DI_CR_DMA    (1 << 1) // 0: immediate mode, 1: DMA mode (*1)
#define DI_CR_TSTART (1 << 0) // transfer start. write 1: start transfer, read 1: transfer pending (*2)

#define DI_IMMBUF  8 // 0xCC006020 - DI immediate data buffer (error code ?)
#define DI_CFG     9 // 0xCC006024 - DI Configuration Register

#define DVD_OEM_INQUIRY 0x12000000
#define DVD_OEM_READ 0xA8000000
#define DVD_FLIPPY_BOOTLOADER_STATUS 0xB4000000
#define DVD_FLIPPY_FILEAPI_BASE 0xB5000000

#define DI_REQUEST_FLIPPY_OPEN 0x1000
#define DI_REQUEST_READ 0x0100

static vu32* const _di_regs = (vu32*)0xCC006000;

typedef struct DIReq_FlippyOpen {
    char path[256];
    uint8_t type;
    uint8_t flags;
} DIReq_FlippyOpen;

typedef struct DIReq_Read {
    void* dst;
    unsigned int len;
    uint64_t offset;
    unsigned int fd;
} DIReq_Read;

// Define union for requests
typedef union DIRequest {
    DIReq_FlippyOpen open;
    DIReq_Read read;
} DIRequest;

// Define a structure for work items
typedef struct DI_WorkItem {
    u32 type;          // Type of the task
    DIRequest *input;  // Input data for the task
    u32 result;        // Result of the task
    OSMutex mutex;     // Mutex for synchronization
    OSCond cond;       // Condition variable
    BOOL done;         // Flag indicating the work is done
} DI_WorkItem;

static OSMutex di_mutex;
static OSCond di_interrupt_cond;
static void __DI_InterruptHandler(s16 interrupt, OSContext* context) {
	OSContext exceptionContext;

	OSClearContext(&exceptionContext);
	OSSetCurrentContext(&exceptionContext);

    // clear int
    _di_regs[DI_SR] |= (DI_SR_BRKINT | DI_SR_TCINT | DI_SR_DEINT | DI_SR_BRK);

    // handler code here
    custom_OSReport("DI Interrupt\n");
    OSSignalCond(&di_interrupt_cond);

	OSClearContext(&exceptionContext);
    OSSetCurrentContext(context);
}

int DILow_FlippyOpen(const char *path, uint8_t type, uint8_t flags) {
    GCN_ALIGNED(file_entry_t) entry;

    strncpy(entry.name, path, 256);
    entry.name[255] = 0;
    entry.type = type;
    entry.flags = flags;

    DCFlushRange(&entry, sizeof(file_entry_t));
    _di_regs[DI_SR] = (DI_SR_BRKINTMASK | DI_SR_TCINTMASK | DI_SR_DEINT | DI_SR_DEINTMASK);
    _di_regs[DI_CVR] = 0; // clear cover int

    _di_regs[DI_CMDBUF0] = DVD_FLIPPY_FILEAPI_BASE | IPC_FILE_OPEN;
    _di_regs[DI_CMDBUF1] = 0;
    _di_regs[DI_CMDBUF2] = 0;

    _di_regs[DI_MAR] = (u32)&entry & 0x1FFFFFFF;
    _di_regs[DI_LENGTH] = sizeof(file_entry_t);
    _di_regs[DI_CR] = (DI_CR_RW | DI_CR_DMA | DI_CR_TSTART); // start transfer

    return 0;
}

int DILow_Read(void* dst, unsigned int len, uint64_t offset, unsigned int fd) {
    if (offset >> 2 > 0xFFFFFFFF) return -1;

    _di_regs[DI_SR] = (DI_SR_BRKINTMASK | DI_SR_TCINTMASK | DI_SR_DEINT | DI_SR_DEINTMASK);
	_di_regs[DI_CVR] = 0; // clear cover int

    _di_regs[DI_CMDBUF0] = DVD_OEM_READ | ((fd & 0xFF) << 16);
    _di_regs[DI_CMDBUF1] = offset >> 2;
	_di_regs[DI_CMDBUF2] = len;

	_di_regs[DI_MAR] = (u32)dst & 0x1FFFFFFF;
    _di_regs[DI_LENGTH] = len;
    _di_regs[DI_CR] = (DI_CR_DMA | DI_CR_TSTART); // start transfer

    return 0;
}

// Initialize a WorkItem
static void DI_InitWorkItem(DI_WorkItem* item, u32 type, void* input) {
    item->type = type;
    item->input = input;
    item->result = 0;
    OSInitMutex(&item->mutex);
    OSInitCond(&item->cond);
    item->done = false;
}

// Define the length of the message queue
#define QUEUE_LENGTH 10

// Create a message queue and buffer
static OSMessageQueue workQueue;
static OSMessage workQueueBuffer[QUEUE_LENGTH];

const bool di_synchronous = true; // TODO: make a callback version too

// Worker thread function
void* DI_WorkerThread(void* arg) {
    OSMessage msg;
    while (true) {
        // Wait for a work item from the queue
        OSReceiveMessage(&workQueue, &msg, OS_MESSAGE_BLOCK);
        DI_WorkItem* item = (DI_WorkItem*)msg;

        // Setup the interrupt condition variable
        OSInitCond(&di_interrupt_cond);

        switch(item->type) {
            case DI_REQUEST_FLIPPY_OPEN:
                // FlippyOpen
                DIReq_FlippyOpen *cmd = &item->input->open;
                DILow_FlippyOpen(cmd->path, cmd->type, cmd->flags);
                break;
            case DI_REQUEST_READ:
                // Read
                DILow_Read(item->input->read.dst, item->input->read.len, item->input->read.offset, item->input->read.fd);
                break;
        }

        if (di_synchronous) {
            // Wait for the worker thread to complete the task
            OSLockMutex(&di_mutex);
            custom_OSReport("Waiting for DI work to complete\n");
            OSWaitCond(&di_interrupt_cond, &di_mutex);
            OSUnlockMutex(&di_mutex);

            // Handle Incoming Data cache
            if (item->type == DI_REQUEST_READ) {
                DCInvalidateRange(item->input->read.dst, item->input->read.len);
            }

            // check if ERR was asserted
            item->result = (_di_regs[DI_SR] & DI_SR_DEINT) ? 1 : 0;
            item->done = true;

            // Signal the condition variable to wake up the waiting thread
            OSLockMutex(&item->mutex);
            OSSignalCond(&item->cond);
            OSUnlockMutex(&item->mutex);
        }
    }
    return NULL;
}

#define THREAD_STACK_SIZE 8192
static OSThread workerThread;
static u8 workerThreadStack[THREAD_STACK_SIZE];

// Initialize the work queue and worker thread
void DI_InitQueue() {
    // Setup Interrupt handler
    OSInitMutex(&di_mutex);
    __OSSetInterruptHandler(__OS_INTERRUPT_PI_DI, __DI_InterruptHandler);
    __OSUnmaskInterrupts(OS_INTERRUPTMASK_PI_DI);

    // Initialize the message queue
    OSInitMessageQueue(&workQueue, workQueueBuffer, QUEUE_LENGTH);

    // Create and start the worker thread
    dolphin_OSCreateThread(&workerThread, DI_WorkerThread, NULL, workerThreadStack + THREAD_STACK_SIZE, THREAD_STACK_SIZE, 10, 0);
    dolphin_OSResumeThread(&workerThread);
}

// Function to submit work and wait for the result
u32 DI_DispatchWork(u32 type, void* input) {
    // Allocate and initialize a WorkItem on the stack
    DI_WorkItem item;
    DI_InitWorkItem(&item, type, input);

    // Send the address of the work item to the worker thread via the message queue
    if (!OSSendMessage(&workQueue, (OSMessage)&item, OS_MESSAGE_BLOCK)) {
        // Handle message send failure
        return 0; // Or appropriate error code
    }

    custom_OSReport("Work item sent\n");

    // Wait for the worker thread to complete the task
    OSLockMutex(&item.mutex);
    custom_OSReport("Waiting for work to complete\n");
    OSWaitCond(&item.cond, &item.mutex);
    u32 result = item.result;
    OSUnlockMutex(&item.mutex);

    custom_OSReport("Work completed\n");

    // No need to free item, as it is allocated on the stack
    return result;
}

int DI_Open(const char *path, uint8_t file_type, uint8_t flags) {
    u32 type = DI_REQUEST_FLIPPY_OPEN;
    DIReq_FlippyOpen input = {
        .path = "/",
        .type = file_type,
        .flags = flags,
    };
    u32 result = DI_DispatchWork(type, &input);
    custom_OSReport("Result of work for input %p is %u\n", &input, result);

    return result;
}

int DI_Read(void* dst, unsigned int len, uint64_t offset, unsigned int fd) {
    u32 type = DI_REQUEST_READ;
    DIReq_Read input = {
        .dst = dst,
        .len = len,
        .offset = offset,
        .fd = fd,
    };
    u32 result = DI_DispatchWork(type, &input);
    custom_OSReport("Result of work for input %p is %u\n", &input, result);

    return result;
}

int di_tests() {
    // Initialize the work queue system
    DI_InitQueue();


    return 0;
}
