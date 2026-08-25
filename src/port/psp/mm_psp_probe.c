#include "global.h"

#include "buffers.h"
#include "fault.h"
#include "libc64/malloc.h"
#include "oot_psp_asset_loader.h"
#include "oot_psp_audio_backend.h"
#include "oot_psp_controls.h"
#include "oot_psp_dve.h"
#include "oot_psp_home_menu.h"
#include "oot_psp_renderer.h"
#include "oot_psp_video.h"
#include "sys_cfb.h"
#include "z64DLF.h"

#include <pspctrl.h>
#include <pspdebug.h>
#include <pspfpu.h>
#include <pspkernel.h>
#include <psppower.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("MM PSP Port", 0, 1, 0);
PSP_MAIN_THREAD_PRIORITY(0x20);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
/* Leave headroom for raw PSPLink launches while retaining ample room for MM's
 * deepest game call paths. */
PSP_MAIN_THREAD_STACK_SIZE_KB(256);
/* MM's game arena is a separate 8 MiB user-partition allocation. */
PSP_HEAP_SIZE_KB(64);

s32 MmPspGame_Init(void);
s32 MmPspGame_ReserveHeap(void);
void Graph_Init(GraphicsContext* gfxCtx);
void Graph_Destroy(GraphicsContext* gfxCtx);
void Graph_Update(GraphicsContext* gfxCtx, GameState* gameState);
GameStateOverlay* Graph_GetNextGameState(GameState* gameState);

static int MmPspExitCallback(UNUSED int arg1, UNUSED int arg2, UNUSED void* common) {
    return 0;
}

static int MmPspPowerCallback(UNUSED int arg1, int powerInfo, UNUSED void* common) {
    if (powerInfo & PSP_POWER_CB_RESUMING) {
        OotPsp_AssetNotifyResume();
    }
    return 0;
}

static int MmPspCallbackThread(UNUSED SceSize args, UNUSED void* argp) {
    int exitCallbackId = sceKernelCreateCallback("MM PSP Exit Callback", MmPspExitCallback, NULL);
    int powerCallbackId = sceKernelCreateCallback("MM PSP Power Callback", MmPspPowerCallback, NULL);

    if (exitCallbackId >= 0) {
        sceKernelRegisterExitCallback(exitCallbackId);
    }
    if (powerCallbackId >= 0) {
        scePowerRegisterCallback(0, powerCallbackId);
    }
    sceKernelSleepThreadCB();
    return 0;
}

static void MmPspSetupCallbacks(void) {
    int threadId = sceKernelCreateThread("MM PSP Callback Thread", MmPspCallbackThread, 0x11, 0x1000, 0, NULL);

    if (threadId >= 0) {
        sceKernelStartThread(threadId, 0, NULL);
    }
}

static s32 MmPsp_InitGraphBuffers(void) {
    uintptr_t allocation;

    allocation = (uintptr_t)malloc(sizeof(*gZBufferLoRes) + sizeof(*gWorkBufferLoRes) + 63);
    if (allocation == 0) {
        return false;
    }
    gZBufferLoRes = (void*)ALIGN64(allocation);
    gWorkBufferLoRes = (void*)((u8*)gZBufferLoRes + sizeof(*gZBufferLoRes));

    gGfxSPTaskOutputBufferLoRes = malloc(sizeof(*gGfxSPTaskOutputBufferLoRes));
    if (gGfxSPTaskOutputBufferLoRes == NULL) {
        return false;
    }
    gGfxSPTaskOutputBufferHiRes = gGfxSPTaskOutputBufferLoRes;
    gGfxSPTaskOutputBufferEndLoRes =
        (u8*)gGfxSPTaskOutputBufferLoRes + sizeof(*gGfxSPTaskOutputBufferLoRes);
    gGfxSPTaskOutputBufferEndHiRes = gGfxSPTaskOutputBufferEndLoRes;
    return true;
}

static void MmPsp_RunGame(void) {
    GraphicsContext gfxCtx;
    GameStateOverlay* nextOverlay = &gGameStateOverlayTable[0];

    if (!MmPsp_InitGraphBuffers()) {
        Fault_AddHungupAndCrash(__FILE__, __LINE__);
    }
    SysCfb_Init();
    Fault_SetFrameBuffer(gWorkBuffer, SCREEN_WIDTH, SCREEN_HEIGHT);
    Graph_Init(&gfxCtx);

    while (nextOverlay != NULL) {
        GameStateOverlay* overlay = nextOverlay;
        GameState* gameState;
        size_t size = overlay->instanceSize;

        Overlay_LoadGameState(overlay);
        gameState = malloc(size);
        if (gameState == NULL) {
            Fault_AddHungupAndCrash(__FILE__, __LINE__);
        }
        memset(gameState, 0, size);
        GameState_Init(gameState, overlay->init, &gfxCtx);

        while (GameState_IsRunning(gameState)) {
            OotPspHomeMenu_PollHomeButton();
            if (OotPspHomeMenu_IsOpen()) {
                if (OotPspHomeMenu_RunFrame(NULL) == OOT_PSP_HOME_MENU_RESULT_EXIT_GAME) {
                    gameState->running = false;
                    nextOverlay = NULL;
                }
            } else {
                Graph_Update(&gfxCtx, gameState);
            }
        }

        if (nextOverlay != NULL) {
            nextOverlay = Graph_GetNextGameState(gameState);
        }
        GameState_Destroy(gameState);
        free(gameState);
        Overlay_FreeGameState(overlay);
    }
    Graph_Destroy(&gfxCtx);
}

int main(int argc, char** argv) {
    const char* executablePath = ((argc > 0) && (argv != NULL)) ? argv[0] : NULL;
    s32 audioMeBootResult = 0;

    scePowerSetClockFrequency(333, 333, 166);
    pspFpuSetEnable(0);
#if OOT_PSP_AUDIO_MEDIA_ENGINE
    audioMeBootResult = OotPspAudioBackend_BootMe();
#endif
    pspDebugScreenInit();
    printf("mm-psp startup: main entered, audio=%s me-boot=%ld\n",
#if OOT_PSP_AUDIO_MEDIA_ENGINE
           "media-engine",
#else
           "cpu",
#endif
           (long)audioMeBootResult
    );
    if (!MmPspGame_ReserveHeap()) {
        pspDebugScreenPrintf("MM PSP could not allocate the 8 MiB game arena\n");
        sceKernelDelayThread(3000000);
        sceKernelExitGame();
        return 1;
    }
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    (void)OotPspDve_Init(executablePath);
    OotPspVideo_Init(executablePath);
    OotPspHomeMenu_Init();
    MmPspSetupCallbacks();

    osInitialize();
    OotPspRenderer_Init();
    if (!OotPsp_AssetInit(executablePath)) {
        pspDebugScreenPrintf("MM PSP asset initialization failed\n");
        sceKernelDelayThread(3000000);
        OotPspDve_Shutdown();
        sceKernelExitGame();
        return 1;
    }
    printf("mm-psp startup: assets ready\n");

    OotPspControls_Load();
    if (!MmPspGame_Init()) {
        pspDebugScreenPrintf("MM PSP could not allocate the 8 MiB game arena\n");
        sceKernelDelayThread(3000000);
        OotPspDve_Shutdown();
        sceKernelExitGame();
        return 1;
    }
    printf("mm-psp startup: game initialized\n");
    MmPsp_RunGame();
    OotPspDve_Shutdown();
    sceKernelExitGame();
    return 0;
}
