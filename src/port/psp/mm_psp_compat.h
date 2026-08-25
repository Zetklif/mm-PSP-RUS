#ifndef MM_PSP_COMPAT_H
#define MM_PSP_COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <float.h>
#include <limits.h>

#include "math.h"
#include "ultra64.h"

#if defined(TARGET_PSP)
#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif
#ifndef M_SQRT1_2f
#define M_SQRT1_2f 0.70710678118654752440f
#endif
#ifndef M_SQRT2f
#define M_SQRT2f 1.41421356237309504880f
#endif

#ifndef OOT_PSP_COMPAT_H
#define OOT_PSP_COMPAT_H
#endif

#ifndef MM_PSP_FAST_SQRT
#define MM_PSP_FAST_SQRT 1
#endif

#if MM_PSP_FAST_SQRT
static inline __attribute__((always_inline)) float MmPsp_Sqrtf(float value) {
    float result;

    __asm__("sqrt.s %0, %1" : "=f"(result) : "f"(value));
    return result;
}
#define sqrtf MmPsp_Sqrtf
#define sqrt(value) MmPsp_Sqrtf((float)(value))
#endif

/* MM uses the N64 Expansion Pak layout, so reserve twice OoT's game heap. */
#define MM_PSP_SYSTEM_HEAP_SIZE (8U * 1024U * 1024U)
#define OOT_PSP_SYSTEM_HEAP_SIZE MM_PSP_SYSTEM_HEAP_SIZE

extern unsigned char* gMmPspSystemHeap;
#define gOotPspSystemHeap gMmPspSystemHeap

/* Field/type spellings used by the OoT libultra shim. */
#define mtqueue mtQueue
#define fullqueue fullQueue
#define OSViContext __OSViContext
#define framep buffer

#ifndef static_assert
#define static_assert(condition, message) _Static_assert(condition, message)
#endif

static inline __attribute__((always_inline)) int OotPsp_IsSystemHeapRange(const void* ptr, size_t size) {
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end;
    const uintptr_t heapStart = (uintptr_t)gMmPspSystemHeap;
    const uintptr_t heapEnd = heapStart + MM_PSP_SYSTEM_HEAP_SIZE;

    if ((gMmPspSystemHeap == NULL) || (ptr == NULL) || (size == 0) || (start > UINTPTR_MAX - size)) {
        return 0;
    }

    end = start + size;
    return (start >= heapStart) && (end <= heapEnd);
}

extern unsigned char __bss_start[];
extern unsigned char _end[];

int OotPsp_IsRuntimeByteRangeSlow(uintptr_t start, uintptr_t end) __attribute__((noinline));

static inline int OotPsp_IsRuntimeByteRange(const void* ptr, size_t size) {
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end;

    if ((ptr == NULL) || (size == 0) || (start > UINTPTR_MAX - size)) {
        return 0;
    }

    end = start + size;
    if ((start >= (uintptr_t)__bss_start) && (end <= (uintptr_t)_end)) {
        return 1;
    }

    return OotPsp_IsRuntimeByteRangeSlow(start, end);
}
#endif

#endif
