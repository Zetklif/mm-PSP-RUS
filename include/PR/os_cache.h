#ifndef PR_OS_CACHE_H
#define PR_OS_CACHE_H

#include "ultratypes.h"
#include "stddef.h"

#if defined(TARGET_PSP) || defined(PLATFORM_PSP)
void osInvalDCache(void* vaddr, s32 nbytes);
void osInvalICache(void* vaddr, s32 nbytes);
#else
void osInvalDCache(void* vaddr, size_t nbytes);
void osInvalICache(void* vaddr, size_t nbytes);
#endif
void osWritebackDCache(void* vaddr, s32 nbytes);
void osWritebackDCacheAll(void);

#endif
