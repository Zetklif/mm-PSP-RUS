#include "sys_cmpdma.h"

#include "libc64/malloc.h"
#include "color.h"
#include "yaz0.h"
#include "z64dma.h"

#if defined(TARGET_PSP) || defined(PLATFORM_PSP)
#include "oot_psp_asset_loader.h"
#endif

typedef struct {
    /* 0x0 */ union {
        u32 dmaWord[2];
        u32 dataStart;
        u32 dataSize;
        struct {
            u32 start;
            u32 end;
        } offset;
    };
} CmpDmaBuffer; // size = 0x8

CmpDmaBuffer sDmaBuffer;

static u32 CmpDma_ArchiveWordToNative(u32 value) {
#if defined(TARGET_PSP) || defined(PLATFORM_PSP)
    return ((value & 0x000000FFU) << 24) | ((value & 0x0000FF00U) << 8) |
           ((value & 0x00FF0000U) >> 8) | ((value & 0xFF000000U) >> 24);
#else
    return value;
#endif
}

void func_80178AC0(u16* src, void* dst, size_t size) {
    Color_RGBA8_u32 spC;
    Color_RGBA16_2 tc;
    Color_RGBA14 tc2;
    u32* dstCur = dst;
    u16* src16 = src;

    while (((uintptr_t)dstCur) - size < ((uintptr_t)dst)) {
        tc.rgba = *(src16++);
        if (tc.a == 1) {
            spC.r = (tc.r * 255) / 31;
            spC.g = (tc.g * 255) / 31;
            spC.b = (tc.b * 255) / 31;
            spC.a = 255;
        } else if (tc.rgba == 0) {
            spC.rgba = 0;
        } else {
            tc2.rgba = tc.rgba;
            tc.rgba = *(src16++);
            spC.r = (tc.r << 3) | tc2.r;
            spC.g = (tc.g << 3) | tc2.g;
            spC.b = (tc.b << 3) | tc2.b;
            spC.a = ((tc.rgba & 1) << 7) | ((tc2.a * 127) / 63);
        }
        *(dstCur++) = spC.rgba;
    }
}

void CmpDma_GetFileInfo(uintptr_t segmentRom, s32 id, uintptr_t* outFileRom, size_t* size, s32* flag) {
    uintptr_t dataStart;
    uintptr_t refOff;

    DmaMgr_DmaRomToRam(segmentRom, &sDmaBuffer.dataStart, sizeof(sDmaBuffer.dataStart));

    dataStart = CmpDma_ArchiveWordToNative(sDmaBuffer.dataStart);
    refOff = id * sizeof(u32);

    // if id is >= idMax
    if (refOff > (dataStart - sizeof(u32))) {
        *outFileRom = segmentRom;
        *size = 0;
    } else if (refOff == 0) {
        // get offset start of next file, i.e. size of first file
        DmaMgr_DmaRomToRam(segmentRom + sizeof(u32), &sDmaBuffer.dataSize, sizeof(sDmaBuffer.dataSize));
        *outFileRom = segmentRom + dataStart;
        *size = CmpDma_ArchiveWordToNative(sDmaBuffer.dataSize);
    } else {
        // get offset start, end from dataStart
        DmaMgr_DmaRomToRam(refOff + segmentRom, &sDmaBuffer.offset, sizeof(sDmaBuffer.offset));
        sDmaBuffer.offset.start = CmpDma_ArchiveWordToNative(sDmaBuffer.offset.start);
        sDmaBuffer.offset.end = CmpDma_ArchiveWordToNative(sDmaBuffer.offset.end);
        *outFileRom = sDmaBuffer.offset.start + segmentRom + dataStart;
        *size = sDmaBuffer.offset.end - sDmaBuffer.offset.start;
    }
    *flag = 0;
}

void CmpDma_Decompress(uintptr_t romStart, size_t size, void* dst) {
    if (size != 0) {
        Yaz0_Decompress(romStart, dst, size);
    }
}

static void CmpDma_RegisterOutput(void* dst, size_t size) {
#if defined(TARGET_PSP) || defined(PLATFORM_PSP)
    OotPsp_RegisterRuntimeAssetWrite(dst, size);
#endif
}

void CmpDma_LoadFileImpl(uintptr_t segmentRom, s32 id, void* dst, size_t size) {
    uintptr_t romStart;
    size_t compressedSize;
    s32 flag;

    CmpDma_GetFileInfo(segmentRom, id, &romStart, &compressedSize, &flag);
    if (flag & 1) {
        void* tempBuf = malloc(0x1000);

        CmpDma_Decompress(romStart, compressedSize, tempBuf);
        func_80178AC0(tempBuf, dst, size);
        CmpDma_RegisterOutput(dst, size);
        free(tempBuf);
    } else {
        CmpDma_Decompress(romStart, compressedSize, dst);
        if ((uintptr_t)gYaz0DecompressDstEnd > (uintptr_t)dst) {
            CmpDma_RegisterOutput(dst, (uintptr_t)gYaz0DecompressDstEnd - (uintptr_t)dst);
        }
    }
}

void CmpDma_LoadFile(uintptr_t segmentVrom, s32 id, void* dst, size_t size) {
    CmpDma_LoadFileImpl(DmaMgr_TranslateVromToRom(segmentVrom), id, dst, size);
}

void CmpDma_LoadAllFiles(uintptr_t segmentVrom, void* dst, size_t size) {
    uintptr_t rom = DmaMgr_TranslateVromToRom(segmentVrom);
    u32 i;
    u32 end;
    void* nextDst;
    u32 dataStart;

    DmaMgr_DmaRomToRam(rom, &sDmaBuffer.dataStart, sizeof(sDmaBuffer.dataStart));

    dataStart = CmpDma_ArchiveWordToNative(sDmaBuffer.dataStart);
    nextDst = dst;
    end = (dataStart / sizeof(u32)) - 1;

    for (i = 0; i < end; i++) {
        CmpDma_LoadFileImpl(rom, i, nextDst, 0);
        nextDst = gYaz0DecompressDstEnd;
    }

    /* Treat a fully expanded archive as one source generation as well. This
     * covers texture imports whose aligned source span reaches across two
     * adjacent archive entries. */
    if ((uintptr_t)nextDst > (uintptr_t)dst) {
        CmpDma_RegisterOutput(dst, (uintptr_t)nextDst - (uintptr_t)dst);
    }
}
