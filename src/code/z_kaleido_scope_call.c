#include "kaleido_manager.h"

#include "z64.h"
#include "z64shrink_window.h"

#if PLATFORM_PSP
#include "oot_psp_renderer.h"
#endif

void (*sKaleidoScopeUpdateFunc)(PlayState* play);
void (*sKaleidoScopeDrawFunc)(PlayState* play);

extern void KaleidoScope_Update(PlayState* play);
extern void KaleidoScope_Draw(PlayState* play);

static void KaleidoScopeCall_RequestPauseBg(void) {
#if PLATFORM_PSP
    /* The PSP backend captures the previously displayed gameplay framebuffer
     * and restores it before every pause frame. Skip the N64 prerender path. */
    OotPspRenderer_SetPauseBackgroundActive(true);
    OotPspRenderer_RequestPauseBackground();
    R_PAUSE_BG_PRERENDER_STATE = PAUSE_BG_PRERENDER_READY;
#else
    R_PAUSE_BG_PRERENDER_STATE = PAUSE_BG_PRERENDER_SETUP;
#endif
}

void KaleidoScopeCall_LoadPlayer(void) {
    KaleidoMgrOverlay* playerActorOvl = &gKaleidoMgrOverlayTable[KALEIDO_OVL_PLAYER_ACTOR];

    if (gKaleidoMgrCurOvl != playerActorOvl) {
        if (gKaleidoMgrCurOvl != NULL) {
            KaleidoManager_ClearOvl(gKaleidoMgrCurOvl);
        }

        KaleidoManager_LoadOvl(playerActorOvl);
    }
}

void KaleidoScopeCall_Init(PlayState* play) {
    sKaleidoScopeUpdateFunc = KaleidoManager_GetRamAddr(KaleidoScope_Update);
    sKaleidoScopeDrawFunc = KaleidoManager_GetRamAddr(KaleidoScope_Draw);
    KaleidoSetup_Init(play);
}

void KaleidoScopeCall_Destroy(PlayState* play) {
#if PLATFORM_PSP
    OotPspRenderer_SetPauseBackgroundActive(false);
#endif
    KaleidoSetup_Destroy(play);
}

void KaleidoScopeCall_Update(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    KaleidoMgrOverlay* kaleidoScopeOvl = &gKaleidoMgrOverlayTable[KALEIDO_OVL_KALEIDO_SCOPE];

    if (!IS_PAUSED(&play->pauseCtx)) {
        return;
    }

    if ((pauseCtx->state == PAUSE_STATE_OPENING_0) || (pauseCtx->state == PAUSE_STATE_OWL_WARP_0)) {
        if (ShrinkWindow_Letterbox_GetSize() == 0) {
            KaleidoScopeCall_RequestPauseBg();
            pauseCtx->mainState = PAUSE_MAIN_STATE_IDLE;
            pauseCtx->savePromptState = PAUSE_SAVEPROMPT_STATE_APPEARING;
            pauseCtx->state = (pauseCtx->state & 0xFFFF) + 1;
        }
    } else if (pauseCtx->state == PAUSE_STATE_GAMEOVER_0) {
        KaleidoScopeCall_RequestPauseBg();
        pauseCtx->mainState = PAUSE_MAIN_STATE_IDLE;
        pauseCtx->savePromptState = PAUSE_SAVEPROMPT_STATE_APPEARING;
        pauseCtx->state = (pauseCtx->state & 0xFFFF) + 1;
    } else if ((pauseCtx->state == PAUSE_STATE_OPENING_1) || (pauseCtx->state == PAUSE_STATE_GAMEOVER_1) ||
               (pauseCtx->state == PAUSE_STATE_OWL_WARP_1)) {
        if (R_PAUSE_BG_PRERENDER_STATE == PAUSE_BG_PRERENDER_READY) {
            pauseCtx->state++;
        }
    } else if (pauseCtx->state != PAUSE_STATE_OFF) {
        if (gKaleidoMgrCurOvl != kaleidoScopeOvl) {
            if (gKaleidoMgrCurOvl != NULL) {
                KaleidoManager_ClearOvl(gKaleidoMgrCurOvl);
            }

            KaleidoManager_LoadOvl(kaleidoScopeOvl);
        }

        if (gKaleidoMgrCurOvl == kaleidoScopeOvl) {
            sKaleidoScopeUpdateFunc(play);

            if (!IS_PAUSED(&play->pauseCtx)) {
#if PLATFORM_PSP
                OotPspRenderer_SetPauseBackgroundActive(false);
#endif
                KaleidoManager_ClearOvl(kaleidoScopeOvl);
                KaleidoScopeCall_LoadPlayer();
            }
        }
    }
}

void KaleidoScopeCall_Draw(PlayState* play) {
    KaleidoMgrOverlay* kaleidoScopeOvl = &gKaleidoMgrOverlayTable[KALEIDO_OVL_KALEIDO_SCOPE];

    if (R_PAUSE_BG_PRERENDER_STATE == PAUSE_BG_PRERENDER_READY) {
        if (((play->pauseCtx.state >= PAUSE_STATE_OPENING_3) && (play->pauseCtx.state <= PAUSE_STATE_SAVEPROMPT)) ||
            ((play->pauseCtx.state >= PAUSE_STATE_GAMEOVER_3) && (play->pauseCtx.state <= PAUSE_STATE_UNPAUSE_SETUP))) {
            if (gKaleidoMgrCurOvl == kaleidoScopeOvl) {
                sKaleidoScopeDrawFunc(play);
            }
        }
    }
}
