#include "scheduler.h"

#include "array_count.h"
#include "oot_psp_renderer.h"

#include <pspkernel.h>
#include <string.h>

#define MM_PSP_VI_RATE_HZ      60U
#define MM_PSP_FRAME_BASE_USEC 16666U
#define MM_PSP_FRAME_REMAINDER 40U

static u32 sNextGfxCompletionUsec;
static u32 sGfxPacingRemainder;
static s32 sGfxPacingInitialized;

static inline s32 MmPspScheduler_TimeDiff(u32 a, u32 b) {
    return (s32)(a - b);
}

static u32 MmPspScheduler_GetUpdateRate(const OSScTask* task) {
    const CfbInfo* cfb;

    if ((task == NULL) || (task->framebuffer == NULL)) {
        return 1;
    }

    cfb = TASK_FRAMEBUFFER(task);
    return cfb->updateRate > 0 ? (u32)cfb->updateRate : 1U;
}

static u32 MmPspScheduler_GetFrameUsec(const OSScTask* task) {
    const u32 updateRate = MmPspScheduler_GetUpdateRate(task);
    u32 frameUsec = MM_PSP_FRAME_BASE_USEC * updateRate;

    sGfxPacingRemainder += MM_PSP_FRAME_REMAINDER * updateRate;
    if (sGfxPacingRemainder >= MM_PSP_VI_RATE_HZ) {
        const u32 extraUsec = sGfxPacingRemainder / MM_PSP_VI_RATE_HZ;

        frameUsec += extraUsec;
        sGfxPacingRemainder -= extraUsec * MM_PSP_VI_RATE_HZ;
    }

    return frameUsec;
}

static void MmPspScheduler_PaceGfxTask(const OSScTask* task) {
    const u32 now = sceKernelGetSystemTimeLow();
    const u32 frameUsec = MmPspScheduler_GetFrameUsec(task);
    s32 waitUsec;

    if (!sGfxPacingInitialized) {
        sNextGfxCompletionUsec = now;
        sGfxPacingInitialized = true;
    }

    waitUsec = MmPspScheduler_TimeDiff(sNextGfxCompletionUsec, now);
    if (waitUsec > 0) {
        sceKernelDelayThread((u32)waitUsec);
    } else if (waitUsec < 0) {
        /* A late frame starts a new timeline; do not run catch-up frames. */
        sNextGfxCompletionUsec = now;
    }

    sNextGfxCompletionUsec += frameUsec;
}

static void MmPspScheduler_ExecutePending(Scheduler* sched) {
    OSScTask* task;

    while (osRecvMesg(&sched->cmdQueue, (OSMesg*)&task, OS_MESG_NOBLOCK) == 0) {
        CfbInfo* cfb;

        if (task == NULL) {
            continue;
        }

        if (task->list.t.type == M_GFXTASK) {
            OotPspRenderer_RenderTask(&task->list);

            cfb = TASK_FRAMEBUFFER(task);
            if ((task->flags & OS_SC_SWAPBUFFER) && (cfb != NULL)) {
                osViSwapBuffer(cfb->swapBuffer);
                MmPspScheduler_PaceGfxTask(task);
            }
        }

        if (task->msgQ != NULL) {
            osSendMesg(task->msgQ, task->msg, OS_MESG_NOBLOCK);
        }
    }
}

void Sched_SendNotifyMsg(Scheduler* sched) {
    if (sched != NULL) {
        MmPspScheduler_ExecutePending(sched);
    }
}

void Sched_SendAudioCancelMsg(Scheduler* sched) {
    (void)sched;
}

void Sched_SendGfxCancelMsg(Scheduler* sched) {
    /* Tasks execute synchronously on PSP, so no RSP/RDP work remains here. */
    if (sched != NULL) {
        MmPspScheduler_ExecutePending(sched);
    }
}

void Sched_Init(Scheduler* sched, void* stack, OSPri pri, u8 viModeType, UNK_TYPE arg4, IrqMgr* irqMgr) {
    if (sched == NULL) {
        return;
    }

    memset(sched, 0, sizeof(*sched));
    osCreateMesgQueue(&sched->interruptQueue, sched->interruptMsgBuf, ARRAY_COUNT(sched->interruptMsgBuf));
    osCreateMesgQueue(&sched->cmdQueue, sched->cmdMsgBuf, ARRAY_COUNT(sched->cmdMsgBuf));

    sched->retraceCount = 1;
    sched->isFirstSwap = true;
    sNextGfxCompletionUsec = 0;
    sGfxPacingRemainder = 0;
    sGfxPacingInitialized = false;

    OotPspRenderer_Init();

    (void)stack;
    (void)pri;
    (void)viModeType;
    (void)arg4;
    (void)irqMgr;
}

void Sched_FlushTaskQueue(void) {
    MmPspScheduler_ExecutePending(&gScheduler);
}
