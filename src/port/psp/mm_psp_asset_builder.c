#include "oot_psp_asset_builder.h"
#include "oot_psp_asset_loader.h"

#include <pspiofilemgr.h>
#include <stdio.h>

#define MM_PSP_PACKED_ASSET_PATH "data/segments/oot_psp_assets.bin"

/*
 * MM's PSP build ships the already-converted asset image beside EBOOT.PBP.
 * The OoT port can also construct that image from a ROM on first boot, but
 * its conversion manifest is game-specific and must never be applied to MM.
 */
s32 OotPspAssetBuilder_Ensure(void) {
    char pathBuffer[384];
    const char* path;
    SceUID fd;
    SceOff actualSize;
    size_t expectedSize;

    if (gOotPspExternalAssetCount == 0) {
        return false;
    }

    expectedSize = gOotPspExternalAssets[gOotPspExternalAssetCount - 1].fileOffset +
                   (gOotPspExternalAssets[gOotPspExternalAssetCount - 1].vromEnd -
                    gOotPspExternalAssets[gOotPspExternalAssetCount - 1].vromStart);
    path = OotPsp_ResolveRootPath(MM_PSP_PACKED_ASSET_PATH, pathBuffer, sizeof(pathBuffer));
    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) {
        printf("mm-psp packed assets missing path=%s err=%d\n", path, (int)fd);
        return false;
    }

    actualSize = sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoClose(fd);
    if ((actualSize < 0) || ((size_t)actualSize != expectedSize)) {
        printf("mm-psp packed assets invalid path=%s actual=%ld expected=%lu\n", path, (long)actualSize,
               (unsigned long)expectedSize);
        return false;
    }

    return true;
}
