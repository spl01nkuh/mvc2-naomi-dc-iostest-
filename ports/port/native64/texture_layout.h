/* Internal source-layout validation shared by native asset decoders.
 * Keep these extents consistent with the actual reads in nl_tex.c. */
#ifndef MVC2_NATIVE64_TEXTURE_LAYOUT_H
#define MVC2_NATIVE64_TEXTURE_LAYOUT_H
#include <stddef.h>
#include <stdint.h>

typedef enum MVC2TextureLayoutStatus {
    MVC2_TEXTURE_LAYOUT_OK,
    MVC2_TEXTURE_LAYOUT_INVALID,
    MVC2_TEXTURE_LAYOUT_UNSUPPORTED
} MVC2TextureLayoutStatus;

static inline int mvc2_texture_power_of_two(unsigned n) {
    return n != 0 && !(n & (n - 1));
}

/* Includes padding, mipmap prefixes and the VQ codebook. Alignment describes
 * the backend's source accesses, not the alignment of a serialized header. */
static inline MVC2TextureLayoutStatus mvc2_texture_source_extent(
    int32_t type, unsigned w, unsigned h, size_t* bytes, size_t* alignment) {
    if (!bytes || !w || !h || w > 1024 || h > 1024)
        return MVC2_TEXTURE_LAYOUT_INVALID;
    unsigned kind = (uint32_t)type >> 8 & 255u;
    unsigned format = (uint32_t)type & 255u;
    int palette = kind >= 5 && kind <= 8;
    int mip = kind == 2 || kind == 4 || kind == 6 || kind == 8;
    size_t pixels = (size_t)w * h, prefix = 1, extent;
    if ((uint32_t)type > 0xffffu || (!palette && format > 2))
        return MVC2_TEXTURE_LAYOUT_UNSUPPORTED;
    if (kind != 9 && kind != 11 &&
        (!mvc2_texture_power_of_two(w) || !mvc2_texture_power_of_two(h)))
        return MVC2_TEXTURE_LAYOUT_UNSUPPORTED;
    if (mip && w != h) return MVC2_TEXTURE_LAYOUT_UNSUPPORTED;
    for (unsigned level = 1; level < w; level <<= 1) prefix += (size_t)level * level;
    switch (kind) {
    case 1: case 9: case 11: case 13: extent = pixels * 2; break;
    case 2: extent = (prefix + pixels) * 2; break;
    case 5: extent = (pixels + 1) / 2; break;
    case 6: extent = (prefix + 2) / 2 + (pixels + 1) / 2; break;
    case 7: extent = pixels; break;
    case 8: extent = prefix + 2 + pixels; break;
    case 3: case 4: {
        if ((w == 1 || h == 1) && !(w == 1 && h == 1))
            return MVC2_TEXTURE_LAYOUT_UNSUPPORTED;
        size_t index_prefix = 0;
        if (kind == 4 && w > 1) {
            index_prefix = 1;
            for (unsigned level = 2; level < w; level <<= 1)
                index_prefix += (size_t)(level / 2) * (level / 2);
        }
        extent = 2048 + index_prefix + (pixels < 4 ? 1 : pixels / 4);
        break;
    }
    default: return MVC2_TEXTURE_LAYOUT_UNSUPPORTED;
    }
    *bytes = extent;
    if (alignment) *alignment = palette ? 1 : 2;
    return MVC2_TEXTURE_LAYOUT_OK;
}
#endif
