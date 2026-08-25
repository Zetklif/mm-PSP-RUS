#ifndef MM_PSP_AUDIO_COMPAT_H
#define MM_PSP_AUDIO_COMPAT_H

#include "z64audio.h"
#include "audio/effects.h"
#include "audio/heap.h"
#include "audio/load.h"
#include "audio/playback.h"
#include "audio/seqplayer.h"
#include "audio/synthesis.h"

#define gAudioContext gAudioCtx
#define maxAiBufferLength numSamplesPerFrameMax

Acmd* AudioSynth_BuildCommandList(Acmd* cmdStart, s32* cmdCnt, s16* aiStart, s32 aiBufLen);
Acmd* AudioSynth_BuildCommandListMe(Acmd* cmdStart, s32* cmdCnt, s16* aiStart, s32 aiBufLen);
s32 AudioSynth_CanBuildCommandsOnMe(void);
void OotPspAudioSynth_WritebackMeState(void);
void OotPspAudioSynth_MeInvalidateState(void);
void OotPspAudioSynth_MeWritebackState(void);
void OotPspAudioSynth_InvalidateMeState(void);
void OotPspAudioSynth_PublishMeAssetRange(const void* address, u32 size);
AudioTask* AudioThread_Update(void);
void Audio_Init(void);
void Audio_InitSound(void);

#endif
