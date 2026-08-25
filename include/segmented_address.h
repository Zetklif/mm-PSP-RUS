#ifndef SEGMENTED_ADDRESS_H
#define SEGMENTED_ADDRESS_H

#include "ultra64.h"

#include "stdint.h"

extern uintptr_t gSegments[NUM_SEGMENTS];

#if PLATFORM_PSP
void* SegmentedToVirtualCompat(uintptr_t addr);
void* SegmentedToVirtualExplicit(uintptr_t addr);
#define SEGMENTED_TO_K0(addr) SegmentedToVirtualCompat((uintptr_t)(addr))
#define SEGMENTED_TO_VIRTUAL(addr) SegmentedToVirtualCompat((uintptr_t)(addr))
#define SEGMENTED_TO_VIRTUAL_EXPLICIT(addr) SegmentedToVirtualExplicit((uintptr_t)(addr))
#else
#define SEGMENTED_TO_K0(addr) (void*)((gSegments[SEGMENT_NUMBER(addr)] + K0BASE) + SEGMENT_OFFSET(addr))
#define SEGMENTED_TO_VIRTUAL(addr) SEGMENTED_TO_K0(addr)
#define SEGMENTED_TO_VIRTUAL_EXPLICIT(addr) SEGMENTED_TO_K0(addr)
#endif

#endif
