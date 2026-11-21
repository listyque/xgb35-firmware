#ifndef OG_MENU_PIXELS_H
#define OG_MENU_PIXELS_H

#include <stdint.h>

static inline uint32_t expand565(uint16_t p) {
    return ((uint32_t)p | ((uint32_t)p << 16)) & 0x07E0F81Fu;
}

static inline uint32_t lerp565_expanded(uint32_t a, uint32_t b, uint32_t weight) {
    return (((a << 5) + (b - a) * weight) >> 5) & 0x07E0F81Fu;
}

static inline uint16_t collapse565(uint32_t p) {
    return (uint16_t)(p | (p >> 16));
}

static inline uint16_t lerp565_swar(uint16_t a, uint16_t b, uint32_t weight) {
    if (a == b) return a;
    return collapse565(lerp565_expanded(expand565(a), expand565(b), weight));
}

static inline uint16_t bilerp565_swar(uint16_t a, uint16_t b, uint16_t c, uint16_t d,
                                     uint32_t fx, uint32_t fy) {
    uint32_t top = lerp565_expanded(expand565(a), expand565(b), fx);
    uint32_t bottom = lerp565_expanded(expand565(c), expand565(d), fx);
    return collapse565(lerp565_expanded(top, bottom, fy));
}

static inline uint16_t multiply565(uint16_t d, uint16_t s) {
    uint32_t r = (d >> 11) * (s >> 11);
    uint32_t g = ((d >> 5) & 63u) * ((s >> 5) & 63u);
    uint32_t b = (d & 31u) * (s & 31u);
    return (uint16_t)((((r + 1u + (r >> 5)) >> 5) << 11) |
                      (((g + 1u + (g >> 6)) >> 6) << 5) |
                      ((b + 1u + (b >> 5)) >> 5));
}

#if defined(__ARM_NEON)
#include <arm_neon.h>

static inline uint16x8_t red565x8(uint16x8_t p) { return vshrq_n_u16(p, 11); }
static inline uint16x8_t green565x8(uint16x8_t p) { return vandq_u16(vshrq_n_u16(p, 5), vdupq_n_u16(63)); }
static inline uint16x8_t blue565x8(uint16x8_t p) { return vandq_u16(p, vdupq_n_u16(31)); }
static inline uint16x8_t pack565x8(uint16x8_t r, uint16x8_t g, uint16x8_t b) {
    return vorrq_u16(vorrq_u16(vshlq_n_u16(r, 11), vshlq_n_u16(g, 5)), b);
}

static inline uint16x8_t lerp_channel_x8(uint16x8_t a, uint16x8_t b, uint16x8_t w) {
    return vshrq_n_u16(vmlaq_u16(vshlq_n_u16(a, 5), vsubq_u16(b, a), w), 5);
}

static inline uint16x8_t lerp565x8(uint16x8_t a, uint16x8_t b, uint16x8_t w) {
    return pack565x8(lerp_channel_x8(red565x8(a), red565x8(b), w),
                     lerp_channel_x8(green565x8(a), green565x8(b), w),
                     lerp_channel_x8(blue565x8(a), blue565x8(b), w));
}

static inline uint16x8_t bilerp_channel_x8(uint16x8_t a, uint16x8_t b, uint16x8_t c, uint16x8_t d,
                                          uint16x8_t wx, uint16x8_t wy) {
    return lerp_channel_x8(lerp_channel_x8(a, b, wx), lerp_channel_x8(c, d, wx), wy);
}

static inline uint16x8_t multiply_channels565x8(uint16x8_t dst, uint16x8_t red,
                                                 uint16x8_t green, uint16x8_t blue) {
    uint16x8_t r = vmulq_u16(red565x8(dst), red);
    uint16x8_t g = vmulq_u16(green565x8(dst), green);
    uint16x8_t bl = vmulq_u16(blue565x8(dst), blue);
    uint16x8_t one = vdupq_n_u16(1);
    return pack565x8(vshrq_n_u16(vaddq_u16(vaddq_u16(r, one), vshrq_n_u16(r, 5)), 5),
                     vshrq_n_u16(vaddq_u16(vaddq_u16(g, one), vshrq_n_u16(g, 6)), 6),
                     vshrq_n_u16(vaddq_u16(vaddq_u16(bl, one), vshrq_n_u16(bl, 5)), 5));
}

static inline uint16x8_t bilerp_multiply565x8(uint16x8_t dst, uint16x8_t a, uint16x8_t b,
                                            uint16x8_t c, uint16x8_t d, uint16x8_t wx, uint16x8_t wy) {
    return multiply_channels565x8(dst,
        bilerp_channel_x8(red565x8(a), red565x8(b), red565x8(c), red565x8(d), wx, wy),
        bilerp_channel_x8(green565x8(a), green565x8(b), green565x8(c), green565x8(d), wx, wy),
        bilerp_channel_x8(blue565x8(a), blue565x8(b), blue565x8(c), blue565x8(d), wx, wy));
}
#endif

#endif
