#include <gctypes.h>
#include "dolphin_os.h"

#define __OS_INTERRUPT_DSP_ARAM 6
#define __OS_INTERRUPT_PI_DI 21

#define OS_INTERRUPTMASK(interrupt) (0x80000000u >> (interrupt))
#define OS_INTERRUPTMASK_DSP_ARAM OS_INTERRUPTMASK(__OS_INTERRUPT_DSP_ARAM)
#define OS_INTERRUPTMASK_PI_DI OS_INTERRUPTMASK(__OS_INTERRUPT_PI_DI)

typedef s16 __OSInterrupt;
typedef void (*__OSInterruptHandler)(__OSInterrupt interrupt, OSContext* context);

typedef u32 OSInterruptMask;

extern void (*OSClearContext)(OSContext* context);
extern void (*OSSetCurrentContext)(OSContext* context);

extern __OSInterruptHandler (*__OSSetInterruptHandler)(__OSInterrupt interrupt, __OSInterruptHandler handler);
extern OSInterruptMask (*__OSUnmaskInterrupts)(OSInterruptMask mask);
