#ifndef LIBC64_MALLOC_H
#define LIBC64_MALLOC_H

#include "ultra64.h"
#include "libc64/os_malloc.h"

/*
 * MM's original libc64 allocator exports the standard C allocator names. On
 * PSP that would interpose on newlib before main(): newlib's lock bootstrap
 * calls malloc() before MM's arena exists, gets NULL, and passes it to
 * sceKernelCreateLwMutex. Keep the game API source-compatible while giving it
 * PSP-private linker names, leaving malloc/free/calloc/realloc to newlib.
 */
#if defined(TARGET_PSP)
#define malloc MmPspGame_Malloc
#define malloc_r MmPspGame_MallocR
#define realloc MmPspGame_Realloc
#define free MmPspGame_Free
#define calloc MmPspGame_Calloc
#endif

void* malloc(size_t size);
void* malloc_r(size_t size);
void* realloc(void* oldPtr, size_t newSize);
void free(void* ptr);
void* calloc(size_t num, size_t size);
void GetFreeArena(size_t* maxFreeBlock, size_t* bytesFree, size_t* bytesAllocated);
s32 CheckArena(void);
void MallocInit(void* start, size_t size);
void MallocCleanup(void);
s32 MallocIsInitialized(void);

extern Arena malloc_arena;

#endif
