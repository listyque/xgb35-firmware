/* Fast 0.33s C replacement for /bin/st7796s-init (replaces 3.5s sysfs shell bitbanging).
 * Pins on XGB35 (LLW7051-057 V2.0): SDA=GPIOA19, CLK=GPIOA18, CS=GPIOC28, PWR=GPIOB6.
 */
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

static volatile uint32_t *gpio;

static inline void spi_delay(void)
{
    for (volatile int i = 0; i < 16; i++) {}
}

static void spi_byte(int dc, uint8_t val)
{
    uint32_t w = ((uint32_t)(dc & 1) << 8) | val;
    gpio[0x20 / 4] &= ~(1u << 28); /* CS = 0 (GPIOC28) */
    spi_delay();
    for (int m = 256; m > 0; m >>= 1) {
        uint32_t a = gpio[0x08 / 4] & ~((1u << 18) | (1u << 19));
        if (w & (uint32_t)m) a |= (1u << 19);
        gpio[0x08 / 4] = a;        /* CLK = 0, SDA = bit */
        spi_delay();
        gpio[0x08 / 4] = a | (1u << 18); /* CLK = 1 */
        spi_delay();
    }
    gpio[0x20 / 4] |= (1u << 28);  /* CS = 1 */
    spi_delay();
}

/* Format: len (0x80 | ms_div_10 = sleep, 0 = end), cmd, data[len-1] */
static const uint8_t init_seq[] = {
    1, 0x11, 0x80 | 15,
    2, 0x36, 0x0C,
    2, 0x3A, 0x55,
    2, 0xF0, 0xC3,
    2, 0xF0, 0x96,
    2, 0xB4, 0x01,
    4, 0xB6, 0xA0, 0x02, 0x3B,
    2, 0xB7, 0xC6,
    3, 0xC0, 0x80, 0x75,
    2, 0xC1, 0x0B,
    2, 0xC2, 0xA7,
    2, 0xC5, 0x09,
    9, 0xE8, 0x40, 0x8A, 0x1B, 0x1B, 0x20, 0x13, 0x1D, 0x33,
    15, 0xE0, 0xF0, 0x05, 0x05, 0x02, 0x03, 0x05, 0x32, 0x43, 0x4D, 0x3B, 0x1B, 0x1B, 0x30, 0x34,
    15, 0xE1, 0x6E, 0x12, 0x18, 0x11, 0x0F, 0x16, 0x33, 0x33, 0x46, 0x35, 0x0F, 0x0E, 0x29, 0x2E,
    2, 0xF0, 0xC3,
    2, 0xF0, 0x96,
    0x80 | 12,
    1, 0x21,
    1, 0x29,
    0x80 | 6,
    1, 0x2C,
    0
};

int main(void)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) return 1;
    void *p = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0xB01C0000);
    close(fd);
    if (p == MAP_FAILED) return 2;
    gpio = (volatile uint32_t *)p;

    /* GPIOB6 power ON, GPIOA18/19 & GPIOC28 GPIO output mode */
    gpio[0x14 / 4] |= (1u << 6);
    gpio[0x0C / 4] |= (1u << 6);
    gpio[0x20 / 4] |= (1u << 28);
    gpio[0x18 / 4] |= (1u << 28);
    gpio[0x08 / 4] &= ~((1u << 18) | (1u << 19));
    gpio[0x00 / 4] |= (1u << 18) | (1u << 19);

    for (int i = 0; init_seq[i] != 0; ) {
        uint8_t b = init_seq[i++];
        if (b & 0x80) {
            usleep((useconds_t)(b & 0x7F) * 10000u);
        } else {
            spi_byte(0, init_seq[i++]);
            for (int k = 1; k < b; k++)
                spi_byte(1, init_seq[i++]);
        }
    }
    return 0;
}
