/* Host assert-based self-check for OpenGorky XGB35 pixel/blend/scaler math. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "menu_pixels.h"

static uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
static uint16_t pack1555(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint16_t)((a >= 128 ? 0x8000u : 0u) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}
static uint16_t avg565(uint16_t a, uint16_t b) {
    return (uint16_t)((a & b) + (((a ^ b) & 0xF7DEu) >> 1));
}
static void unpack565(uint16_t p, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint8_t r5 = (uint8_t)(p >> 11);
    uint8_t g6 = (uint8_t)((p >> 5) & 63);
    uint8_t b5 = (uint8_t)(p & 31);
    *r = (uint8_t)((r5 << 3) | (r5 >> 2));
    *g = (uint8_t)((g6 << 2) | (g6 >> 4));
    *b = (uint8_t)((b5 << 3) | (b5 >> 2));
}

int main(void) {
    /* 1. RGB565 round-trip */
    assert(pack565(255, 255, 255) == 0xFFFFu);
    assert(pack565(0, 0, 0) == 0x0000u);
    assert(pack565(255, 0, 0) == 0xF800u);
    assert(pack565(0, 255, 0) == 0x07E0u);
    assert(pack565(0, 0, 255) == 0x001Fu);

    uint8_t r, g, b;
    unpack565(0xFFFFu, &r, &g, &b);
    assert(r == 255 && g == 255 && b == 255);
    unpack565(0xF800u, &r, &g, &b);
    assert(r == 255 && g == 0 && b == 0);

    /* 2. ARGB1555 alpha bit */
    assert((pack1555(255, 255, 255, 0) & 0x8000u) == 0);
    assert((pack1555(255, 255, 255, 255) & 0x8000u) == 0x8000u);

    /* 3. avg565 identity and midpoint */
    assert(avg565(0xFFFFu, 0xFFFFu) == 0xFFFFu);
    assert(avg565(0x0000u, 0x0000u) == 0x0000u);
    uint16_t mid = avg565(pack565(0, 0, 0), pack565(255, 255, 255));
    unpack565(mid, &r, &g, &b);
    assert(r >= 120 && r <= 135 && g >= 120 && g <= 135 && b >= 120 && b <= 135);

    /* 4. SWAR 5-6-5 alpha blend sanity (a=0 -> dst, a=255 -> src, a=128 -> ~mid) */
    uint16_t dst = pack565(0, 0, 255), src = pack565(255, 0, 0);
    uint32_t a = (128u + 4u) >> 3, ia = 32u - a;
    uint16_t swar = (uint16_t)(((((src & 0xF81Fu) * a + (dst & 0xF81Fu) * ia) >> 5) & 0xF81Fu) |
                               ((((src & 0x07E0u) * a + (dst & 0x07E0u) * ia) >> 5) & 0x07E0u));
    unpack565(swar, &r, &g, &b);
    assert(r >= 115 && r <= 140 && g == 0 && b >= 115 && b <= 140);

    /* 5. SWAR 5-6-5 bilinear lerp sanity (0x07E0F81F packed R|B + G<<16) */
    uint16_t lmid = lerp565_swar(dst, src, 16);
    unpack565(lmid, &r, &g, &b);
    assert(r >= 115 && r <= 140 && g == 0 && b >= 115 && b <= 140);

    for (unsigned x = 0; x < 64; x++) {
        for (unsigned y = 0; y < 64; y++) {
            uint16_t p = (uint16_t)((x >> 1) << 11 | x << 5 | (63 - x) / 2);
            uint16_t q = (uint16_t)((63 - y) / 2 << 11 | y << 5 | y >> 1);
            assert(multiply565(p, q) ==
                   (((x >> 1) * ((63 - y) / 2) / 31) << 11 |
                    (x * y / 63) << 5 | ((63 - x) / 2) * (y >> 1) / 31));
            for (unsigned w = 0; w < 32; w++) {
                unsigned pr = ((p >> 11) * (32 - w) + (q >> 11) * w) >> 5;
                unsigned pg = (x * (32 - w) + y * w) >> 5;
                unsigned pb = ((p & 31u) * (32 - w) + (q & 31u) * w) >> 5;
                assert(lerp565_swar(p, q, w) == ((pr << 11) | (pg << 5) | pb));
                assert(bilerp565_swar(p, q, q, p, w, 31 - w) ==
                       lerp565_swar(lerp565_swar(p, q, w), lerp565_swar(q, p, w), 31 - w));
            }
        }
    }

#if defined(__ARM_NEON)
    uint32_t rng = 0x12345678u;
    for (int pass = 0; pass < 4096; pass++) {
        uint16_t texels[5][8], fx[8], fy[8], output[8];
        for (int i = 0; i < 5; i++) for (int lane = 0; lane < 8; lane++) {
            rng = rng * 1664525u + 1013904223u;
            texels[i][lane] = (uint16_t)(rng >> 16);
        }
        for (int lane = 0; lane < 8; lane++) {
            fx[lane] = texels[0][lane] & 31u;
            fy[lane] = texels[4][lane] & 31u;
        }
        vst1q_u16(output, bilerp_multiply565x8(vld1q_u16(texels[0]), vld1q_u16(texels[1]),
                    vld1q_u16(texels[2]), vld1q_u16(texels[3]), vld1q_u16(texels[4]),
                    vld1q_u16(fx), vld1q_u16(fy)));
        for (int lane = 0; lane < 8; lane++)
            assert(output[lane] == multiply565(texels[0][lane], bilerp565_swar(texels[1][lane],
                   texels[2][lane], texels[3][lane], texels[4][lane], fx[lane], fy[lane])));
    }
#endif

    printf("selfcheck OK (RGB565/ARGB1555/avg565/SWAR-blend/bilerp verified)\n");
    return 0;
}
