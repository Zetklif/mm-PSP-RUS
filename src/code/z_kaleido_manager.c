#include "kaleido_manager.h"

#include "global.h"
#include "fault.h"
#include "libu64/loadfragment.h"

#if defined(TARGET_PSP) || defined(PLATFORM_PSP)
/*
 * PSP links the player and pause overlays into the PRX as native code.  The
 * original overlay images in the packed asset file are big-endian N64 MIPS
 * binaries and must never be copied into memory and executed on PSP.
 */
#define KALEIDO_OVERLAY(name) \
    { NULL, ROM_FILE_UNSET, NULL, NULL, 0, #name }
#else
#define KALEIDO_OVERLAY(name) \
    { NULL, ROM_FILE(ovl_##name), SEGMENT_START(ovl_##name), SEGMENT_END(ovl_##name), 0, #name }
#endif

KaleidoMgrOverlay gKaleidoMgrOverlayTable[KALEIDO_OVL_MAX] = {
    KALEIDO_OVERLAY(kaleido_scope),
    KALEIDO_OVERLAY(player_actor),
};

void* sKaleidoAreaPtr = NULL;
KaleidoMgrOverlay* gKaleidoMgrCurOvl = NULL;

FaultAddrConvClient sKaleidoMgrFaultAddrConvClient;

uintptr_t KaleidoManager_FaultAddrConv(uintptr_t address, void* param) {
    uintptr_t addr = address;
    KaleidoMgrOverlay* kaleidoMgrOvl = gKaleidoMgrCurOvl;
    uintptr_t ramConv;
    void* ramStart;
    size_t diff;

    if (kaleidoMgrOvl != NULL) {
        diff = (uintptr_t)kaleidoMgrOvl->vramEnd - (uintptr_t)kaleidoMgrOvl->vramStart;
        ramStart = kaleidoMgrOvl->loadedRamAddr;
        ramConv = (uintptr_t)kaleidoMgrOvl->vramStart - (uintptr_t)ramStart;

        if (ramStart != NULL) {
            if ((addr >= (uintptr_t)ramStart) && (addr < (uintptr_t)ramStart + diff)) {
                return addr + ramConv;
            }
        }
    }
    return 0;
}

void KaleidoManager_LoadOvl(KaleidoMgrOverlay* ovl) {
#if defined(TARGET_PSP) || defined(PLATFORM_PSP)
    if (ovl != NULL) {
        ovl->loadedRamAddr = NULL;
        ovl->offset = 0;
    }
    gKaleidoMgrCurOvl = ovl;
#else
    ovl->loadedRamAddr = sKaleidoAreaPtr;
    Overlay_Load(ovl->file.vromStart, ovl->file.vromEnd, ovl->vramStart, ovl->vramEnd, ovl->loadedRamAddr);
    ovl->offset = (uintptr_t)ovl->loadedRamAddr - (uintptr_t)ovl->vramStart;
    gKaleidoMgrCurOvl = ovl;
#endif
}

void KaleidoManager_ClearOvl(KaleidoMgrOverlay* ovl) {
#if defined(TARGET_PSP) || defined(PLATFORM_PSP)
    if (ovl != NULL) {
        ovl->loadedRamAddr = NULL;
        ovl->offset = 0;
    }
    if (gKaleidoMgrCurOvl == ovl) {
        gKaleidoMgrCurOvl = NULL;
    }
#else
    if (ovl->loadedRamAddr != NULL) {
        ovl->offset = 0;
        bzero(ovl->loadedRamAddr, (uintptr_t)ovl->vramEnd - (uintptr_t)ovl->vramStart);
        ovl->loadedRamAddr = NULL;
        gKaleidoMgrCurOvl = NULL;
    }
#endif
}

void KaleidoManager_Init(PlayState* play) {
#if defined(TARGET_PSP) || defined(PLATFORM_PSP)
    (void)play;
    sKaleidoAreaPtr = NULL;
    gKaleidoMgrCurOvl = NULL;
#else
    s32 largestSize = 0;
    s32 size;
    u32 i;

    for (i = 0; i < ARRAY_COUNT(gKaleidoMgrOverlayTable); i++) {
        size = (uintptr_t)gKaleidoMgrOverlayTable[i].vramEnd - (uintptr_t)gKaleidoMgrOverlayTable[i].vramStart;
        if (size > largestSize) {
            largestSize = size;
        }
    }

    sKaleidoAreaPtr = THA_AllocTailAlign16(&play->state.tha, largestSize);
    gKaleidoMgrCurOvl = NULL;
    Fault_AddAddrConvClient(&sKaleidoMgrFaultAddrConvClient, KaleidoManager_FaultAddrConv, NULL);
#endif
}

void KaleidoManager_Destroy(void) {
#if defined(TARGET_PSP) || defined(PLATFORM_PSP)
    sKaleidoAreaPtr = NULL;
    gKaleidoMgrCurOvl = NULL;
#else
    Fault_RemoveAddrConvClient(&sKaleidoMgrFaultAddrConvClient);

    if (gKaleidoMgrCurOvl != NULL) {
        KaleidoManager_ClearOvl(gKaleidoMgrCurOvl);
        gKaleidoMgrCurOvl = NULL;
    }

    sKaleidoAreaPtr = NULL;
#endif
}

void* KaleidoManager_GetRamAddr(void* vram) {
#if defined(TARGET_PSP) || defined(PLATFORM_PSP)
    return vram;
#else
    if (gKaleidoMgrCurOvl == NULL) {
        s32 pad[2];
        KaleidoMgrOverlay* ovl = &gKaleidoMgrOverlayTable[0];

        do {
            if (((uintptr_t)vram >= (uintptr_t)ovl->vramStart) && ((uintptr_t)ovl->vramEnd >= (uintptr_t)vram)) {
                KaleidoManager_LoadOvl(ovl);
                return (void*)((uintptr_t)vram + ovl->offset);
            }
            ovl++;
        } while (ovl != (KaleidoMgrOverlay*)&sKaleidoAreaPtr);

        return NULL;
    } else if (((uintptr_t)vram < (uintptr_t)gKaleidoMgrCurOvl->vramStart) ||
               ((uintptr_t)vram >= (uintptr_t)gKaleidoMgrCurOvl->vramEnd)) {
        return NULL;
    }

    return (void*)((uintptr_t)vram + gKaleidoMgrCurOvl->offset);
#endif
}
