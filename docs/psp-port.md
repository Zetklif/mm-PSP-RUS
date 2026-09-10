# Majora's Mask PSP port

This target runs Majora's Mask's native game code on PSP. Its Allegrex/libultra
layer, F3DEX2 renderer, audio mixer, controller mapping, video output, and home
menu were derived from the OoT PSP port and are vendored under
`src/port/psp/backend`. MM-specific overlays, message data, archives, flash
saves, and its 32 kHz audio tables are built from this tree. No OoT checkout is
required.

MM needs the N64 Expansion Pak memory layout. The PSP target therefore allocates
an 8 MiB system/game arena (`MM_PSP_SYSTEM_HEAP_SIZE`), exactly twice the OoT
port's 4 MiB arena. It is allocated from the user partition at runtime instead
of being embedded in the PRX's static image, which keeps raw PSPLink module
loads below the default-partition limit. The EBOOT requests expanded memory with
`MEMSIZE=1`, so it is intended for PSP-2000/3000/Go hardware or an emulator with
the Slim memory model. A PSP-1000 does not provide that memory configuration.

## Material rendering validation

The PSP combiner folds primitive tint and vertex lighting across both RDP
cycles. For supported periodic two-texture materials it combines RGB and both
alpha equations into a cached texture before framebuffer blending. This covers
Clock Town's detail-texture products and crossfades, including independently
scaled and scrolling water tiles. Water's additive primitive color is drawn
before fog. Single-cycle materials use the second mux bank.

Material rebuilds decode each source once and precompute sampling coordinates.
Scrolling variants reuse allocations after the previous frame has completed;
variants already queued in the current frame keep separate allocations. Texture
uploads explicitly refresh the GU format, dimensions, and texture cache, including
when an ID was selected before its first upload.

The material cache supports RGBA16, I4, I8, and IA8 with a common repeating or
mirrored domain up to 128 by 128 texels. Clamped tiles, larger domains, other
formats, and equations requiring per-vertex shade alpha retain the existing
renderer fallback. This is not a complete, bit-exact RDP implementation:
filtering a precombined texture, 8-bit arithmetic, the additive pass's saturation,
and the existing approximation of texture LOD can still differ from N64 output.

Run the host regression checks with:

```sh
python3 -m unittest discover -s tools/tests -p 'test_psp_*.py'
```

These compile the actual combiner decoder and material sampler with a mock
texture backend. They check pixel values, alpha endpoints, texture formats,
scrolling, mirroring, cache invalidation, and cycle selection. A 120-frame test
checks allocation reuse for three animated materials. A separate texture-manager
test checks GU binding commands for initial uploads and allocation reuse, with
PSP memory checks and VFPU copies mocked for the host. Extracted South
and East Clock Town display-list equations are also checked when available.
They do not execute the PSP GU. Hardware validation should inspect both towns'
floors and water while moving the camera, changing areas, and crossing dusk or
dawn; also check HUD sun/moon visibility and the Termina Field to swamp route.
Check frame rate during water scrolling, which still requires CPU pixel updates.

## Build

The build requires:

- a working PSPSDK (`psp-config`, `psp-gcc`, `psp-prxgen`, and `pack-pbp` on
  `PATH`);
- the PSP intraFont library and headers (`libintrafont`);
- the matching US Majora's Mask baserom required by this decompilation project.

The custom HOME menu loads `flash0:/font/ltn0.pgf` through intraFont during
renderer initialization. The font comes from PSP firmware; no font file needs
to be copied to the game folder. The bitmap fallback is retained for runtime
initialization or font-load failures.

Prepare the decompilation assets once, then build the PSP package:

```sh
make setup
make -j"$(nproc)" assets
./psp.sh
```

The default `n64-us` build produces:

```text
build/psp-port/n64-us/
├── EBOOT.PBP
├── mm-psp-port.prx
├── Plugins/dvemgr.prx
└── data/segments/oot_psp_assets.bin
```

The external asset filename is retained for compatibility with the derived
loader; its contents are generated entirely from MM assets.

To use a specific job count:

```sh
JOBS=8 ./psp.sh
```

Use `make psp-port-clean` to remove only the PSP target's generated files.

The audio-command mixer uses the PSP Media Engine by default and falls back to
the Allegrex CPU if ME startup or a submitted job fails. MM synthesizes at
32 kHz; the final output uses the backend's software resampler and a normal PSP
audio channel instead of the hardware SRC channel. Either stage can be
overridden for diagnosis:

```sh
PSP_AUDIO_MEDIA_ENGINE=0 ./psp.sh
PSP_AUDIO_HARDWARE_SRC=1 ./psp.sh
```

The main thread uses a 256 KiB stack rather than OoT's 1 MiB stack. MM's larger
module image plus a 1 MiB stack can exhaust PSPLink's default user partition
before `main()` is entered, even on hardware where an XMB launch honors the
EBOOT's expanded-memory request.

The newlib bootstrap heap is fixed at 64 KiB. Game allocations do not use that
heap: they are served by MM's dedicated 8 MiB `gMmPspSystemHeap`. This avoids
newlib's negative “all remaining memory” policy during raw PRX launches.

The renderer's permanent pause/home-menu framebuffer captures and optional TV
texture cache use separate user-partition blocks. They therefore do not depend
on the 64 KiB newlib heap and do not reduce the 8 MiB game arena. The misleading
`OUT OF FRAMEBUFFER MEMORY!` path in the shared renderer is a system-RAM
allocation, not a failure to fit the LCD buffers in EDRAM.

Pinned and sliding external-asset caches also use reversible user-partition
blocks. This lets MM pin its roughly 5.3 MiB Audiotable instead of failing over
to the PSP's 4 MiB volatile partition, while the game arena remains exactly
8 MiB.

MM's original libc64 allocator exports `malloc`, `free`, `calloc`, and
`realloc`. For PSP these are source-mapped to private `MmPspGame_*` symbols.
This keeps them separate from newlib's allocator, which must be operational
before `main()` while newlib creates its recursive lock work areas.

## Install

Copy the package while preserving the `data/segments` subdirectory:

```text
ms0:/PSP/GAME/MMPSP/
├── EBOOT.PBP
├── Plugins/dvemgr.prx
└── data/segments/oot_psp_assets.bin
```

The port creates `mm-psp-save.bin` beside the EBOOT for MM's 1-Mbit flash save.
The generated asset pack is build output and does not contain or require a ROM
on the PSP.
