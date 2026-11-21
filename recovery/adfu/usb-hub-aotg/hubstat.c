#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct usbdevfs_ctrltransfer {
    uint8_t bRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
    uint32_t timeout;
    void *data;
};
#define USBDEVFS_CONTROL _IOWR('U', 0, struct usbdevfs_ctrltransfer)

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) { perror("open"); return 2; }
    if (argc > 2 && argv[2][0] == 'r') {
        /* Cycle PORT_POWER (feat 8) on empty ports 2 and 4 */
        for (uint16_t p = 2; p <= 4; p += 2) {
            struct usbdevfs_ctrltransfer clr = {0x23, 0x01, 8, p, 0, 1000, 0};
            ioctl(fd, USBDEVFS_CONTROL, &clr);
        }
        usleep(400000);
        for (uint16_t p = 2; p <= 4; p += 2) {
            struct usbdevfs_ctrltransfer set = {0x23, 0x03, 8, p, 0, 1000, 0};
            ioctl(fd, USBDEVFS_CONTROL, &set);
        }
        usleep(800000);
    }
    uint8_t hub_desc[16] = {0};
    struct usbdevfs_ctrltransfer hd = {0xA0, 0x06, 0x2900, 0, sizeof(hub_desc), 1000, hub_desc};
    int hr = ioctl(fd, USBDEVFS_CONTROL, &hd);
    printf("HubDesc rc=%d nports=%u char=0x%02x%02x pwr2good=%ums\n",
           hr, hub_desc[2], hub_desc[4], hub_desc[3], hub_desc[5] * 2);

    uint16_t hst[2] = {0, 0};
    struct usbdevfs_ctrltransfer hs = {0xA0, 0x00, 0, 0, 4, 1000, hst};
    ioctl(fd, USBDEVFS_CONTROL, &hs);
    printf("HubStatus=0x%04x HubChange=0x%04x\n", hst[0], hst[1]);

    for (uint16_t p = 1; p <= 4; p++) {
        uint16_t buf[2] = {0, 0};
        struct usbdevfs_ctrltransfer c = {0xA3, 0x00, 0, p, 4, 1000, buf};
        int r = ioctl(fd, USBDEVFS_CONTROL, &c);
        printf("Port %u: rc=%d wPortStatus=0x%04x wPortChange=0x%04x\n", p, r, buf[0], buf[1]);
    }
    close(fd);
    return 0;
}
