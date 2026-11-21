/* Freestanding cold-boot splash + live progress bar for ATM7051H / XGB35.
 * Stored in MicroSD LBA 28..31 (2 KiB) and loaded to DDR 0x01F30000 by Block-0:
 *   0x01F30000: .word 0x53504C31 ('1LPS' magic)
 *   0x01F30004: b splash_init
 *   0x01F30008: b splash_step
 *   0x01F3000C: b splash_stop
 */
#include <stdint.h>

#define FB_ADDR 0x01F40000u

void __attribute__((naked, used, section(".text.entry"))) _start(void)
{
    __asm__ volatile(
        ".word 0x53504C31\n"
        "b splash_init\n"
        "b splash_step\n"
        "b splash_stop\n"
    );
}

static inline void spi_delay(void)
{
    volatile uint32_t *t0 = (volatile uint32_t *)0xB0158000u;
    (void)t0[0x10 / 4];
}

static void delay_us(uint32_t us)
{
    volatile uint32_t *t0 = (volatile uint32_t *)0xB0158000u;
    uint32_t start = t0[0x10 / 4], ticks = us * 24u;
    while ((uint32_t)(t0[0x10 / 4] - start) < ticks) {}
}

static void spi_send(volatile uint32_t *gpio, int dc, uint8_t val)
{
    uint32_t w = ((uint32_t)dc << 8) | val;
    gpio[0x20 / 4] &= ~(1u << 28);
    spi_delay();
    for (uint32_t m = 256; m; m >>= 1) {
        uint32_t a = gpio[0x08 / 4] & ~((1u << 18) | (1u << 19));
        if (w & m) a |= (1u << 19);
        gpio[0x08 / 4] = a;
        spi_delay();
        gpio[0x08 / 4] = a | (1u << 18);
        spi_delay();
    }
    gpio[0x20 / 4] |= (1u << 28);
    spi_delay();
}

static void fill_land(int x0, int y0, int w, int h, uint16_t col)
{
    volatile uint16_t *fb = (volatile uint16_t *)FB_ADDR;
    for (int x = x0; x < x0 + w; x++) {
        volatile uint16_t *row = fb + x * 320;
        for (int y = y0; y < y0 + h; y++)
            row[319 - y] = col;
    }
}

void __attribute__((used)) splash_init(void)
{
    static const uint8_t seq[] = {
        1, 0x11, 0x80 | 12,
        2, 0x36, 0x0C, 2, 0x3A, 0x55, 2, 0xF0, 0xC3, 2, 0xF0, 0x96,
        2, 0xB4, 0x01, 4, 0xB6, 0xA0, 0x02, 0x3B, 2, 0xB7, 0xC6,
        3, 0xC0, 0x80, 0x75, 2, 0xC1, 0x0B, 2, 0xC2, 0xA7, 2, 0xC5, 0x09,
        9, 0xE8, 0x40, 0x8A, 0x1B, 0x1B, 0x20, 0x13, 0x1D, 0x33,
        15, 0xE0, 0xF0, 0x05, 0x05, 0x02, 0x03, 0x05, 0x32, 0x43, 0x4D, 0x3B, 0x1B, 0x1B, 0x30, 0x34,
        15, 0xE1, 0x6E, 0x12, 0x18, 0x11, 0x0F, 0x16, 0x33, 0x33, 0x46, 0x35, 0x0F, 0x0E, 0x29, 0x2E,
        2, 0xF0, 0xC3, 2, 0xF0, 0x96, 0x80 | 2,
        1, 0x21, 1, 0x29, 0x80 | 2, 1, 0x2C, 0
    };
    static const uint8_t logo[5][5] = {
        { 0x63, 0x14, 0x08, 0x14, 0x63 }, /* X */
        { 0x3E, 0x41, 0x49, 0x49, 0x3A }, /* G */
        { 0x7F, 0x49, 0x49, 0x49, 0x36 }, /* B */
        { 0x22, 0x41, 0x49, 0x49, 0x36 }, /* 3 */
        { 0x27, 0x45, 0x45, 0x45, 0x39 }, /* 5 */
    };
    static const uint16_t de_offs[] = {
        0x000, 0x004, 0x100, 0x140, 0x150, 0x220, 0x2A0, 0x2A4, 0x2A8, 0x2AC,
        0x2B0, 0x2B4, 0x2B8, 0x2BC, 0x2C0, 0x2C4, 0x2C8, 0x2CC, 0x2D0, 0x2E8, 0x108, 0x148
    };
    static const uint32_t de_vals[] = {
        0x12, 0x12, 0, 0x2021, 0x01DF013F, 0x04080770, 0x04080770, 0x01DF013F, 0x01DF013F, 0x20002000,
        0x4000, 0xFF073EFC, 0xFE1038FA, 0xFC1B30F9, 0xFA2626FA, 0xF9301BFC, 0xFA3810FE, 0xFC3E07FF, FB_ADDR, 0x50, 0, 1
    };

    volatile uint32_t *cmu  = (volatile uint32_t *)0xB0150000u;
    volatile uint32_t *t0   = (volatile uint32_t *)0xB0158000u;
    volatile uint32_t *gpio = (volatile uint32_t *)0xB01C0000u;
    volatile uint32_t *lcd  = (volatile uint32_t *)0xB02A0000u;
    volatile uint32_t *de   = (volatile uint32_t *)0xB02F0000u;

    cmu[0x24 / 4] = 3;
    cmu[0x30 / 4] = 0;
    cmu[0x70 / 4] = 0x1012;
    cmu[0xA0 / 4] |= 0x00040180u;
    cmu[0xA4 / 4] |= 0x08800000u;
    cmu[0xA8 / 4] |= 0x00000180u;
    cmu[0xAC / 4] |= 0x08800000u;
    t0[0x08 / 4] = 5u;

    volatile uint32_t *fb32 = (volatile uint32_t *)FB_ADDR;
    for (int i = 0; i < (320 * 480) / 2; i++) fb32[i] = 0x10841084u;

    for (int g = 0; g < 5; g++) {
        int gx = 152 + g * 38;
        for (int c = 0; c < 5; c++)
            for (int r = 0; r < 7; r++)
                if (logo[g][c] & (1u << r))
                    fill_land(gx + c * 5, 112 + r * 5, 4, 4, 0xDFDFu);
    }
    fill_land(78, 178, 328, 18, 0x39E9u);
    fill_land(80, 180, 324, 14, 0x0842u);

    gpio[0x40 / 4] = 0x24924000u;
    gpio[0x14 / 4] |= (1u << 6);
    gpio[0x0C / 4] |= (1u << 6);
    gpio[0x20 / 4] |= (1u << 28);
    gpio[0x18 / 4] |= (1u << 28);
    gpio[0x08 / 4] &= ~((1u << 18) | (1u << 19));
    gpio[0x00 / 4] |= (1u << 18) | (1u << 19);

    for (int i = 0; seq[i]; ) {
        uint8_t b = seq[i++];
        if (b & 0x80) delay_us((uint32_t)(b & 0x7F) * 10000u);
        else {
            spi_send(gpio, 0, seq[i++]);
            for (int k = 1; k < b; k++) spi_send(gpio, 1, seq[i++]);
        }
    }

    lcd[0x04 / 4] = 0x01DF013Fu;
    lcd[0x08 / 4] = 0xAA028033u;
    lcd[0x0C / 4] = 0x00002000u;
    lcd[0x10 / 4] = 0x00502403u;
    lcd[0x14 / 4] = 0x00004800u;
    lcd[0x2C / 4] = 0x013F0000u;
    lcd[0x30 / 4] = 0x01DF0000u;
    lcd[0x00 / 4] = 0x00020081u;

    for (unsigned i = 0; i < sizeof(de_offs) / sizeof(de_offs[0]); i++)
        de[de_offs[i] / 4] = de_vals[i];

    gpio[0x44 / 4] = 0x60600000u;
    gpio[0x50 / 4] = 0x01F88100u;
}

void __attribute__((used)) splash_step(uint32_t chunk)
{
    int x0 = 80 + (int)(((chunk - 1u) * 231u) >> 5);
    int x1 = 80 + (int)((chunk * 231u) >> 5);
    if (x1 > 404) x1 = 404;
    if (x0 < x1) fill_land(x0, 180, x1 - x0, 14, 0x2E73u);
}

void __attribute__((used)) splash_stop(void)
{
    volatile uint32_t *de = (volatile uint32_t *)0xB02F0000u;
    de[0x108 / 4] = 0;
    de[0x148 / 4] = 1;
}
