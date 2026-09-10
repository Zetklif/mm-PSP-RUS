/* Included by gfx_fast3d.c after texture decoding helpers. Combine both
 * samples before framebuffer blending: multiplying two framebuffer passes
 * also multiplies the background, which is incorrect for transparent water.
 * Periodic tiles share the RSP's S/T coordinates, so their shifts, mirrors and
 * scrolling origins can be evaluated in a common texture domain. */
#define MATERIAL_TEXTURE_LIMIT 128
#define MATERIAL_TEXTURE_CACHE_SIZE 128

typedef struct MaterialTextureKey {
    TextureTileState tile[2];
    const uint8_t* address[2];
    uint32_t serial[2], stride[2], width[2], height[2];
    uint32_t alpha0, alpha1, colorMode;
    uint8_t nibble[2], primAlpha, envAlpha, lod, linear, mixSource;
} MaterialTextureKey;

typedef struct MaterialTextureEntry {
    MaterialTextureKey key;
    uint32_t textureId, lastUsed, lastUsedFrame;
    uint32_t width, height;
    float scaleS, scaleT;
} MaterialTextureEntry;

static MaterialTextureEntry sMaterialTextures[MATERIAL_TEXTURE_CACHE_SIZE];
static MaterialTextureEntry* sMaterialTexture;
static uint32_t sMaterialTextureClock;
static struct RGBA sMaterialPixels[MATERIAL_TEXTURE_LIMIT * MATERIAL_TEXTURE_LIMIT] __attribute__((aligned(16)));

static void gfx_material_texture_reset(void) {
    memset(sMaterialTextures, 0, sizeof(sMaterialTextures));
    sMaterialTexture = NULL;
    sMaterialTextureClock = 0;
}

static int gfx_material_wrap(int coordinate, int size, bool mirror) {
    int wrapped = coordinate & (size - 1);
    return mirror && (coordinate & size) ? size - 1 - wrapped : wrapped;
}

static struct RGBA gfx_material_read_texel(int slot, int x, int y, GfxTextureSwapState* swap,
                                           int width, int height) {
    const TextureTileState* tile = gfx_get_texture_tile(slot);
    x = gfx_material_wrap(x, tile->masks ? 1 << tile->masks : width, tile->cms & G_TX_MIRROR);
    y = gfx_material_wrap(y, tile->maskt ? 1 << tile->maskt : height, tile->cmt & G_TX_MIRROR);
    const uint8_t* row = gfx_texture_row(slot, y, gfx_texture_row_bytes(width + rdp.loaded_texture[slot].source_nibble_offset, tile->siz));
    struct RGBA result;
    if (tile->fmt == G_IM_FMT_RGBA) {
        uint16_t value = gfx_read_texture_source_be16(row, x * 2, swap);
        result = (struct RGBA){SCALE_5_8(value >> 11), SCALE_5_8((value >> 6) & 31),
                               SCALE_5_8((value >> 1) & 31), (value & 1) * 255};
    } else {
        uint8_t value = tile->siz == G_IM_SIZ_4b ? gfx_texture_read_4b(slot, x, row, swap) * 17
                                                 : gfx_read_texture_source_u8(row, x, swap);
        if (tile->fmt == G_IM_FMT_IA) {
            result = (struct RGBA){(value >> 4) * 17, (value >> 4) * 17, (value >> 4) * 17,
                                   (value & 15) * 17};
        } else {
            result = (struct RGBA){value, value, value, value};
        }
    }
    return result;
}

/* Decode each source once per rebuild. The old inner loop called dimension
 * validation and source-range lookup for every tap of every output pixel. */
#define MATERIAL_SOURCE_LIMIT 8192
static struct RGBA sMaterialSourcePixels[2][MATERIAL_SOURCE_LIMIT] __attribute__((aligned(16)));
typedef struct MaterialAxisSample {
    uint16_t lo, hi, fraction;
} MaterialAxisSample;
static MaterialAxisSample sMaterialAxis[2][2][MATERIAL_TEXTURE_LIMIT];

static void gfx_material_prepare_axis(MaterialAxisSample* samples, int count, int period,
                                      bool mirror, float step, float origin, bool linear) {
    for (int i = 0; i < count; i++) {
        float coordinate = i * step - origin;
        int base = floorf(coordinate);
        samples[i].lo = gfx_material_wrap(base, period, mirror);
        samples[i].hi = gfx_material_wrap(base + 1, period, mirror);
        samples[i].fraction = linear ? (int)((coordinate - base) * 256) : 0;
    }
}

static struct RGBA gfx_material_sample(int slot, int x, int y, int width) {
    const MaterialAxisSample* u = &sMaterialAxis[slot][0][x];
    const MaterialAxisSample* v = &sMaterialAxis[slot][1][y];
    const struct RGBA* pixels = sMaterialSourcePixels[slot];
    struct RGBA a = pixels[v->lo * width + u->lo];
    if (!(u->fraction | v->fraction)) return a;
    struct RGBA b = pixels[v->lo * width + u->hi];
    struct RGBA c = pixels[v->hi * width + u->lo];
    struct RGBA d = pixels[v->hi * width + u->hi];
    for (int i = 0; i < 4; i++) {
        int top = ((uint8_t*)&a)[i] * (256 - u->fraction) + ((uint8_t*)&b)[i] * u->fraction;
        int bottom = ((uint8_t*)&c)[i] * (256 - u->fraction) + ((uint8_t*)&d)[i] * u->fraction;
        ((uint8_t*)&a)[i] = (top * (256 - v->fraction) + bottom * v->fraction + 32768) >> 16;
    }
    return a;
}

static uint8_t gfx_material_alpha(uint32_t equation, uint8_t a0, uint8_t a1, uint8_t combined) {
    int inputs[8] = {combined, a0, a1, rdp.prim_color.a, 255, rdp.env_color.a, 255, 0};
    int a = inputs[equation & 7], b = inputs[(equation >> 3) & 7];
    int cIndex = (equation >> 6) & 7;
    int c = cIndex == 0 || cIndex == 6 ? rdp.prim_lod_frac : inputs[cIndex];
    int d = inputs[(equation >> 9) & 7];
    int product = (a - b) * c;
    int value = (product + (product < 0 ? -127 : 127)) / 255 + d;
    return value < 0 ? 0 : (value > 255 ? 255 : value);
}

static bool gfx_prepare_material_texture(void) {
    MaterialTextureKey key = {0};
    float scaleS = 1, scaleT = 1, width = 1, height = 1;
    uint32_t w0 = rdp.combine_w0, w1 = rdp.combine_w1;
    /* Reverse RGB interpolation needs an independent alpha equation. Only
     * enable it here; the two-pass fallback shares one RGB/alpha weight. */
    bool reverse = ((w0 >> 20) & 15) == G_CCMUX_TEXEL0 &&
                   ((w1 >> 28) & 15) == G_CCMUX_TEXEL1 &&
                   ((w0 >> 15) & 31) == G_CCMUX_PRIM_LOD_FRAC &&
                   ((w1 >> 15) & 7) == G_CCMUX_TEXEL1;
    if ((rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) != G_CYC_2CYCLE ||
        (!rdp.combine_two_texture_blend && !reverse) ||
        ((w0 & 31) != G_CCMUX_SHADE) || (((w0 >> 5) & 15) != G_CCMUX_COMBINED) ||
        (((w1 >> 24) & 15) != (G_CCMUX_0 & 15))) return false;
    key.colorMode = rdp.combine_two_texture_multiply ? 0 : (((w0 >> 20) & 15) == G_CCMUX_TEXEL1 ? 1 : 2);
    if (!rdp.combine_two_texture_multiply &&
        (((w1 >> 6) & 7) != (G_CCMUX_0 & 7))) return false;
    key.alpha0 = ((w0 >> 12) & 7) | (((w1 >> 12) & 7) << 3) |
                 (((w0 >> 9) & 7) << 6) | (((w1 >> 9) & 7) << 9);
    key.alpha1 = ((w1 >> 21) & 7) | (((w1 >> 3) & 7) << 3) |
                 (((w1 >> 18) & 7) << 6) | ((w1 & 7) << 9);
    /* SHADE alpha varies across vertices; it cannot be baked into a tile. */
    for (int i = 0; i < 4; i++) {
        if (((key.alpha0 >> (3 * i)) & 7) == G_ACMUX_SHADE ||
            ((key.alpha1 >> (3 * i)) & 7) == G_ACMUX_SHADE) return false;
    }
    key.mixSource = (w0 >> 15) & 31;
    key.primAlpha = rdp.prim_color.a;
    key.envAlpha = rdp.env_color.a;
    key.lod = rdp.prim_lod_frac;
    bool usesPrimAlpha = false, usesEnvAlpha = key.colorMode && key.mixSource == G_CCMUX_ENV_ALPHA;
    bool usesLod = key.colorMode && key.mixSource != G_CCMUX_ENV_ALPHA;
    for (int i = 0; i < 4; i++) {
        uint32_t a = (key.alpha0 >> (3 * i)) & 7, b = (key.alpha1 >> (3 * i)) & 7;
        usesPrimAlpha |= a == G_ACMUX_PRIMITIVE || b == G_ACMUX_PRIMITIVE;
        usesEnvAlpha |= a == G_ACMUX_ENVIRONMENT || b == G_ACMUX_ENVIRONMENT;
        if (i == 2) usesLod |= a == 0 || a == 6 || b == 0 || b == 6;
    }
    /* State changes unrelated to this equation must not rebuild its pixels. */
    if (!usesPrimAlpha) key.primAlpha = 0;
    if (!usesEnvAlpha) key.envAlpha = 0;
    if (!usesLod) key.lod = 0;
    key.linear = (rdp.other_mode_h & (3U << G_MDSFT_TEXTFILT)) != G_TF_POINT;
    for (int i = 0; i < 2; i++) {
        const TextureTileState* tile = gfx_get_texture_tile(i);
        if (!((tile->fmt == G_IM_FMT_RGBA && tile->siz == G_IM_SIZ_16b) ||
              (tile->fmt == G_IM_FMT_I && (tile->siz == G_IM_SIZ_4b || tile->siz == G_IM_SIZ_8b)) ||
              (tile->fmt == G_IM_FMT_IA && tile->siz == G_IM_SIZ_8b)) ||
            ((tile->cms | tile->cmt) & G_TX_CLAMP)) return false;
        key.width[i] = gfx_texture_import_width(i);
        key.height[i] = gfx_texture_import_height(i);
        if (!gfx_is_power_of_two(key.width[i]) || !gfx_is_power_of_two(key.height[i]) ||
            (tile->masks && (1U << tile->masks) > key.width[i]) ||
            (tile->maskt && (1U << tile->maskt) > key.height[i])) return false;
        if (key.width[i] * key.height[i] > MATERIAL_SOURCE_LIMIT) return false;
        /* Do not copy padding or irrelevant palette/line metadata into the
         * key. Reduce scrolling origins to one periodic texture domain. */
        key.tile[i].fmt = tile->fmt;
        key.tile[i].siz = tile->siz;
        key.tile[i].cms = tile->cms;
        key.tile[i].cmt = tile->cmt;
        key.tile[i].masks = tile->masks;
        key.tile[i].maskt = tile->maskt;
        key.tile[i].shifts = tile->shifts;
        key.tile[i].shiftt = tile->shiftt;
        key.tile[i].uls = tile->uls % ((tile->masks ? 1U << tile->masks : key.width[i]) *
                                      ((tile->cms & G_TX_MIRROR) ? 8 : 4));
        key.tile[i].ult = tile->ult % ((tile->maskt ? 1U << tile->maskt : key.height[i]) *
                                      ((tile->cmt & G_TX_MIRROR) ? 8 : 4));
        key.address[i] = rdp.loaded_texture[i].addr;
        key.stride[i] = rdp.loaded_texture[i].row_stride_bytes;
        key.nibble[i] = rdp.loaded_texture[i].source_nibble_offset;
        key.serial[i] = OotPsp_GetExternalAssetRangeSerial(key.address[i], gfx_texture_source_span_size(i));
        /* RAM-generated textures may change without a DMA serial. */
        if (!key.serial[i] && OotPsp_IsRuntimeByteRange(key.address[i], gfx_texture_source_span_size(i))) {
            key.serial[i] = sTextureCacheFrameSerial;
        }
        scaleS = fmaxf(scaleS, gfx_texture_shift_scale(tile->shifts));
        scaleT = fmaxf(scaleT, gfx_texture_shift_scale(tile->shiftt));
    }
    for (int i = 0; i < 2; i++) {
        const TextureTileState* tile = &key.tile[i];
        width = fmaxf(width, (tile->masks ? 1U << tile->masks : key.width[i]) *
                             ((tile->cms & G_TX_MIRROR) ? 2 : 1) * scaleS / gfx_texture_shift_scale(tile->shifts));
        height = fmaxf(height, (tile->maskt ? 1U << tile->maskt : key.height[i]) *
                               ((tile->cmt & G_TX_MIRROR) ? 2 : 1) * scaleT / gfx_texture_shift_scale(tile->shiftt));
    }
    if (width > MATERIAL_TEXTURE_LIMIT || height > MATERIAL_TEXTURE_LIMIT) return false;
    for (int i = 0; i < MATERIAL_TEXTURE_CACHE_SIZE; i++) {
        if (sMaterialTextures[i].textureId && !memcmp(&key, &sMaterialTextures[i].key, sizeof(key))) {
            sMaterialTexture = &sMaterialTextures[i];
            sMaterialTexture->lastUsed = ++sMaterialTextureClock;
            sMaterialTexture->lastUsedFrame = sTextureCacheFrameSerial;
            return true;
        }
    }
    if (!gfx_validate_texture_source(0, "material-texture0") ||
        !gfx_validate_texture_source(1, "material-texture1")) return false;
    gfx_flush();
    /* Frames are synchronized before reuse. Never rewrite an allocation
     * already referenced in this frame, even if its CPU cache entry is old. */
    MaterialTextureEntry* entry = NULL;
    for (int i = 0; i < MATERIAL_TEXTURE_CACHE_SIZE; i++) {
        MaterialTextureEntry* candidate = &sMaterialTextures[i];
        if (candidate->textureId && candidate->lastUsedFrame != sTextureCacheFrameSerial &&
            candidate->width == (uint32_t)width && candidate->height == (uint32_t)height &&
            candidate->key.address[0] == key.address[0] && candidate->key.address[1] == key.address[1] &&
            (!entry || candidate->lastUsed < entry->lastUsed)) entry = candidate;
    }
    if (!entry) {
        if (!texman_vram_space_available((uint32_t)(width * height * 4)) || !texman_texture_slot_available()) {
            gfx_texture_cache_clear();
        }
        entry = &sMaterialTextures[0];
        for (int i = 0; i < MATERIAL_TEXTURE_CACHE_SIZE; i++) {
            if (!sMaterialTextures[i].textureId) { entry = &sMaterialTextures[i]; break; }
            if (sMaterialTextures[i].lastUsed < entry->lastUsed) entry = &sMaterialTextures[i];
        }
        entry->textureId = gfx_rapi->new_texture();
    }
    GfxTextureSwapState swaps[2] = {
        gfx_texture_source_swap_state(key.address[0], gfx_texture_source_span_size(0)),
        gfx_texture_source_swap_state(key.address[1], gfx_texture_source_span_size(1))
    };
    for (int i = 0; i < 2; i++) {
        for (uint32_t y = 0; y < key.height[i]; y++) {
            for (uint32_t x = 0; x < key.width[i]; x++) {
                sMaterialSourcePixels[i][y * key.width[i] + x] =
                    gfx_material_read_texel(i, x, y, &swaps[i], key.width[i], key.height[i]);
            }
        }
        gfx_material_prepare_axis(sMaterialAxis[i][0], width,
            key.tile[i].masks ? 1U << key.tile[i].masks : key.width[i], key.tile[i].cms & G_TX_MIRROR,
            gfx_texture_shift_scale(key.tile[i].shifts) / scaleS, key.tile[i].uls * 0.25f, key.linear);
        gfx_material_prepare_axis(sMaterialAxis[i][1], height,
            key.tile[i].maskt ? 1U << key.tile[i].maskt : key.height[i], key.tile[i].cmt & G_TX_MIRROR,
            gfx_texture_shift_scale(key.tile[i].shiftt) / scaleT, key.tile[i].ult * 0.25f, key.linear);
    }
    int factor = key.mixSource == G_CCMUX_ENV_ALPHA ? key.envAlpha : key.lod;
    if (key.colorMode == 2) factor = 255 - factor;
    for (uint32_t y = 0; y < (uint32_t)height; y++) {
        for (uint32_t x = 0; x < (uint32_t)width; x++) {
            struct RGBA samples[2];
            for (int i = 0; i < 2; i++) {
                samples[i] = gfx_material_sample(i, x, y, key.width[i]);
            }
            struct RGBA* out = &sMaterialPixels[y * (uint32_t)width + x];
            for (int channel = 0; channel < 3; channel++) {
                int a = ((uint8_t*)&samples[0])[channel], b = ((uint8_t*)&samples[1])[channel];
                ((uint8_t*)out)[channel] = key.colorMode == 0 ? gfx_color_mul_channel(a, b)
                    : (a * (255 - factor) + b * factor + 127) / 255;
            }
            uint8_t alpha = gfx_material_alpha(key.alpha0, samples[0].a, samples[1].a, 0);
            /* TEXEL0/TEXEL1 swap in the second RDP cycle. */
            out->a = gfx_material_alpha(key.alpha1, samples[1].a, samples[0].a, alpha);
        }
    }
    entry->lastUsed = ++sMaterialTextureClock;
    entry->lastUsedFrame = sTextureCacheFrameSerial;
    gfx_rapi->select_texture(0, entry->textureId);
    gfx_rapi->upload_texture((const uint8_t*)sMaterialPixels, width, height, GU_PSM_8888);
    entry->key = key;
    entry->width = width;
    entry->height = height;
    entry->scaleS = scaleS;
    entry->scaleT = scaleT;
    sMaterialTexture = entry;
    rendering_state.bound_texture_id = 0;
    return true;
}
