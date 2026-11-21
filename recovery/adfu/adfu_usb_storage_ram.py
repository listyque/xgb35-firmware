"""Boot an isolated USB Mass Storage smoke test. The backing volume lives in RAM."""
import gzip
import json
import lzma
from pathlib import Path
import re
import stat
import struct
import subprocess
import sys
import time

from build_autonomous_service import Arm, ROOT, branch, mov
from service_usb import Service


sys.path.insert(0, str(ROOT.parents[1] / 'firmware/diagnostic'))
from prepare_diagnostic import check_static_arm_elf, make_cpio, read_cpio, read_uimage, sha256


ENTRY, STAGING = 0xB406D000, 0x01000000
RESUME, REPORT = 0xB406D400, 0xB406D800
KERNEL, INITRD, DTB, LIMIT = 0x8000, 0x01800000, 0x01E00000, 0x02000000
IDMAP, CPU_RESET = 0x004E3F00, 0x004E3F24
CPU_REGS = ((2, 0, 0), (2, 0, 1), (2, 0, 2), (3, 0, 0), (12, 0, 0), (13, 0, 4))
BANKS = (0x11, 0x12, 0x17, 0x1B)
CHECKPOINTS = (0x006A5778, 0x006A7B1C, 0x006A7B20, 0x006A7C00)
LATE_CHECKPOINTS = (0x006A7C04, 0x006A5800, 0x006A5944, 0x006A595C)
IRQ_CHECKPOINTS = (0x006A5948, 0x006A595C, 0x006A5960, 0x006A5998)
IRQ_ENABLE = 0x006A5994
IRQ_HANDLER = 0x0000DA40
TIMER_CHECKPOINTS = (0x002F3204, 0x002F3248)
IRQ_DISPATCH = 0x000085BC
GIC_DOMAIN_POINTER = 0xC06ED510
DOMAIN_FIELDS = ('address', 'type', 'size', 'first_irq_or_table', 'first_hwirq')
DOMAIN_REGS = (8, 6, 7, 10, 11)
IRQ_OBSERVATIONS = (0xB0020118, 0xB0021014, 0xB0021104, 0xB0021108,
                    0xB0021204, 0xB0021208, 0xB0158000, 0xB015800C)
OUTPUT = ROOT / 'usb-storage-ram'
RAM_SECTORS = 32768
INIT = b"""#!/bin/busybox sh
BB=/bin/busybox
$BB mount -t proc proc /proc || exit 71
$BB mount -t sysfs sysfs /sys || exit 72
$BB insmod /lib/libcomposite.ko || exit 73
$BB insmod /lib/g_android.ko nluns=1 || exit 74
usb=/sys/class/android_usb/android0
echo 0 > "$usb/enable" || exit 75
echo 10d6 > "$usb/idVendor" || exit 76
echo 0c02 > "$usb/idProduct" || exit 76
echo 'ATM7051 RAM test' > "$usb/iProduct" || exit 76
echo mass_storage > "$usb/functions" || exit 77
for lun in "$usb"/f_mass_storage/lun*; do
    [ -w "$lun/ro" ] && [ -w "$lun/file" ] || continue
    echo 1 > "$lun/ro" || exit 79
    [ "$($BB cat "$lun/ro")" = 1 ] || exit 79
    echo /ramusb.img > "$lun/file" || exit 80
    echo 1 > "$usb/enable" || exit 81
    while :; do $BB sleep 60; done
done
exit 78
"""
# adb-only gadget: PID FE02 binds the already installed adfuwinusb.inf (WinUSB), host side is adb_winusb.py.
ADB_INIT = b"""#!/bin/busybox sh
BB=/bin/busybox
$BB mount -t proc proc /proc || exit 71
$BB mount -t sysfs sysfs /sys || exit 72
$BB mount -t devtmpfs devtmpfs /dev || exit 82
$BB mkdir -p /dev/pts /dev/shm && $BB mount -t devpts devpts /dev/pts || exit 83
$BB mount -t tmpfs tmpfs /dev/shm
$BB mount -t tmpfs tmpfs /tmp
$BB --install -s
if [ -x /bin/xgbpowerd ]; then
    rm -f /sbin/poweroff /sbin/reboot
    printf '#!/bin/busybox sh\nexec /bin/xgbpowerd --off\n' > /sbin/poweroff
    printf '#!/bin/busybox sh\n/bin/busybox sync\nexec /bin/busybox reboot -f\n' > /sbin/reboot
    chmod 755 /sbin/poweroff /sbin/reboot
fi
export PATH=/bin:/sbin:/usr/bin:/usr/sbin LD_LIBRARY_PATH=/lib
echo 4 > /proc/sys/kernel/printk
# Keep both physical ATS3605 Cortex-A5 cores (CPU0+CPU1) online at full speed with owl_pm_hotplug locked
echo 1 > /sys/devices/system/cpu/autoplug/usr_lock 2>/dev/null
echo 1 > /sys/devices/system/cpu/cpu1/online 2>/dev/null
# Unlock fast, rock-solid peripheral/storage clocks (SD0=50MHz High-Speed, NAND=120MHz 19.2MB/s, AHB=200MHz, DEVPLL=600MHz).
# Keep DISPLAYPLL (0xb0150010=0x108) & DECLK (0xb0150030=0) at factory 48MHz/12MHz for flicker-free 3.5" ST7796S LCD,
# and NEVER write 0 to hdmi/enable at cold boot so HDMI/DE VSYNC (DE_FCR=0) always latches cleanly!
if [ -x /bin/devmem ]; then
    /bin/devmem 0xb015000c 32 0x00000128 2>/dev/null
    /bin/devmem 0xb015004c 32 0x00000001 2>/dev/null
    /bin/devmem 0xb015001c 32 0x00000022 2>/dev/null
    /bin/devmem 0xb015006c 32 0x00000001 2>/dev/null
    /bin/devmem 0xb0150004 32 0x00001150 2>/dev/null
    /bin/devmem 0xb0150010 32 0x00000108 2>/dev/null
    /bin/devmem 0xb0150030 32 0x00001111 2>/dev/null
fi
[ -x /bin/perf-mode ] && /bin/perf-mode init >/dev/null 2>&1
log() { echo "init: $*" > /dev/kmsg; }
# Board + buttons/joysticks BEFORE display unblank, with tiny_mode=0 (tiny_mode=1 mutes matrix_keypad_scan!)
[ -f /lib/atm7051_board.ko ] && $BB insmod /lib/atm7051_board.ko
echo 0 > /sys/class/custom/led_control 2>/dev/null
echo 0 > /sys/class/custom/led2_control 2>/dev/null
[ -f /lib/owl_gpio_matrix_adcjoystick.ko ] && $BB insmod /lib/owl_gpio_matrix_adcjoystick.ko tiny_mode=0
# Display (non-headless builds): cycle blank so owl_lcdc resets 320x480@12MHz, fast C SPI init (0.34s), restore keypad MFP1
if [ -e /dev/fb0 ] && [ -x /bin/st7796s-init ]; then
    echo 4 > /sys/class/graphics/fb0/blank 2>/dev/null
    echo 0 > /sys/class/graphics/fb0/blank 2>/dev/null
    /bin/st7796s-init
    /bin/devmem 0xb01c0044 32 0x60600000 2>/dev/null
    /bin/devmem 0xb01c0050 32 0x01788100 2>/dev/null
    echo 0 > /sys/class/backlight/owl_backlight/bl_power
fi
# Auto-mirror 3.5" LCD onto HDMI when plugged in (or power off HDMI cleanly if unplugged -- zero green stripes!)
[ -x /bin/display-mode ] && /bin/display-mode mirror-lcd
# Arm ATC2603C PMIC ONOFF wakeup (both short press & hold) + start Power button daemon
echo 1 > /sys/power/wakeups/wken_onoff_short 2>/dev/null
echo 1 > /sys/power/wakeups/wken_onoff_long 2>/dev/null
[ -x /bin/xgbpowerd ] && /bin/xgbpowerd &
# Launch Multi-Boot UI IMMEDIATELY while audio + USB ADB initialize in background (~4.5s faster!)
if [ -e /dev/fb0 ] && [ -x /bin/xgbshell ]; then
    (
        while :; do
            /bin/xgbshell --bootmenu
            case "$?" in
            1) exec /bin/boot-stock ;;
            2) /bin/usb-mode pc-disks ;;
            3) exec /bin/reboot-adfu ;;
            4) exec /bin/boot-superx ;;
            5) /bin/boot-onion ;;
            7) /bin/boot-desktop 720p ;;
            *)
                /bin/mount-games >/dev/null 2>&1
                while :; do /bin/xgbshell; $BB sleep 1; done ;;
            esac
        done
    ) < /dev/null > /dev/null 2>&1 &
elif [ -e /dev/fb0 ] && [ -x /bin/fbterm ]; then
    $BB setsid /bin/fbterm -x 't:' -x 'umon->state' -x 'no blk dev' -x 'read MiscInfo' -x 'read Head' -x 'hdmi_hdcp:' < /dev/null > /dev/null 2>&1 &
fi
(
    [ -x /bin/audio-init ] && $BB sh /bin/audio-init > /dev/null 2>&1
    $BB insmod /lib/libcomposite.ko && $BB insmod /lib/g_android.ko nluns=3 || exit 0
    usb=/sys/class/android_usb/android0
    echo 0 > "$usb/enable"
    echo 10d6 > "$usb/idVendor"
    echo fe02 > "$usb/idProduct"
    echo 'ATM7051 RAM adb' > "$usb/iProduct"
    echo adb > "$usb/functions"
    [ -c /dev/android_adb ] && /bin/adbd &
    $BB sleep 1
    echo 1 > "$usb/enable"
    echo 1 > /proc/sys/kernel/printk 2>/dev/null
    echo -1 > /sys/module/usbcore/parameters/autosuspend 2>/dev/null
    if [ -w /proc/acts_hcd ]; then
        echo c > /proc/acts_hcd
        for _ in $($BB seq 1 25); do
            [ -d /sys/bus/platform/devices/aotg_hcd.1/usb1 ] && break
            $BB usleep 100000
        done
    fi
    [ -w /proc/irq/68/smp_affinity ] && echo 1 > /proc/irq/68/smp_affinity 2>/dev/null
    for d in /sys/bus/usb/devices/*; do
        [ -w "$d/power/autosuspend_delay_ms" ] && echo -1000 > "$d/power/autosuspend_delay_ms" 2>/dev/null
        [ -w "$d/power/control" ] && echo on > "$d/power/control" 2>/dev/null
    done
    # Only load aotg_hub_hcd.ko on demand in wifi-init when an external USB hub (bDeviceClass=09) is detected.
    [ -x /bin/wifi-init ] && /bin/wifi-init &
) < /dev/null > /dev/null 2>&1 &
while :; do $BB sleep 60; done
"""
ADB_SHELL = b"""#!/bin/busybox sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin LD_LIBRARY_PATH=/lib HOME=/ TERM=dumb
if [ "$1" = -c ]; then exec /bin/busybox sh -c "$2"; fi
exec /bin/busybox sh
"""
AUDIO_INIT = b"""#!/bin/busybox sh
export LD_LIBRARY_PATH=/lib
T=/bin/tinymix
$T 28 i2s
$T 20 1 1
$T 30 1
$T 34 0
$T 36 0
$T 37 0
$T 38 0
$T 39 0
$T 15 28
# Keep external speaker PA (GPIOC9) muted in idle menus to prevent digital rail whine; game launchers enable ctl 35
$T 35 "${1:-0}"
"""
USB_MODE = b"""#!/bin/busybox sh
# Switch primary OTG port (aotg0) between 'adb', 'storage'/'pc-disks' (Windows SD + NAND drives), and 'host'.
usb=/sys/class/android_usb/android0
find_nand_part() {
    for u in /sys/block/nand0/nand0p*/uevent; do
        [ -f "$u" ] || continue
        if grep -q "^PARTNAME=$1$" "$u" 2>/dev/null; then
            echo "/dev/$(sed -n 's/^DEVNAME=//p' "$u")"
            return 0
        fi
    done
}
start_storage() {
    /bin/nand-load >/dev/null 2>&1
    sync
    swapoff /mnt/sd/linux/swapfile 2>/dev/null
    for mp in /mnt/SDCARD/Roms /tmp/onion_roms /mnt/SDCARD /desktop/mnt/usb /desktop/mnt/sd /desktop/mnt/udisk /desktop/mnt/media /desktop/proc /desktop/sys /desktop/dev/pts /desktop/dev/shm /desktop/dev /desktop/tmp /desktop /alpine/mnt/usb /alpine/mnt/sd /alpine/mnt/udisk /alpine/mnt/media /alpine /superx/mnt/card /superx /mnt/usb /mnt/sd /mnt/udisk /mnt/media; do
        umount "$mp" 2>/dev/null
    done
    for lp in /dev/loop0 /dev/loop1 /dev/loop2 /dev/loop3; do losetup -d "$lp" 2>/dev/null; done
    echo b > /proc/acts_hcd 2>/dev/null
    echo a > /proc/acts_udc 2>/dev/null
    insmod /lib/libcomposite.ko 2>/dev/null
    insmod /lib/g_android.ko nluns=3 2>/dev/null
    echo 0 > "$usb/enable"
    for l in "$usb"/f_mass_storage/lun*; do [ -w "$l/file" ] && echo "" > "$l/file" 2>/dev/null; done
    SD_DEV=""; [ -b /dev/mmcblk0p1 ] && SD_DEV=/dev/mmcblk0p1 || { [ -b /dev/mmcblk0 ] && SD_DEV=/dev/mmcblk0; }
    UD_DEV=$(find_nand_part UDISK)
    MD_DEV=$(find_nand_part MEDIA)
    idx=0
    for dev in $SD_DEV $UD_DEV $MD_DEV; do
        lun="$usb/f_mass_storage/lun$idx"
        [ -b "$dev" ] && [ -w "$lun/file" ] || continue
        echo 0 > "$lun/ro" 2>/dev/null
        echo "$dev" > "$lun/file" 2>/dev/null && idx=$((idx + 1))
    done
    echo 10d6 > "$usb/idVendor"
    echo 0c02 > "$usb/idProduct"
    echo 'XGB35 SD+NAND' > "$usb/iProduct"
    echo mass_storage > "$usb/functions"
    echo 1 > "$usb/enable"
    [ -w /proc/acts_hcd ] && echo c > /proc/acts_hcd
}
stop_storage_to_adb() {
    echo 0 > "$usb/enable" 2>/dev/null
    for l in "$usb"/f_mass_storage/lun*; do [ -w "$l/file" ] && echo "" > "$l/file" 2>/dev/null; done
    echo 10d6 > "$usb/idVendor"
    echo fe02 > "$usb/idProduct"
    echo 'ATM7051 RAM adb' > "$usb/iProduct"
    echo adb > "$usb/functions"
    pidof adbd >/dev/null 2>&1 || /bin/adbd &
    usleep 300000
    echo 1 > "$usb/enable"
    [ -w /proc/acts_hcd ] && echo c > /proc/acts_hcd
    /bin/mount-games >/dev/null 2>&1
}
case "$1" in
storage)
    start_storage
    echo "SD + NAND exported to Windows PC (USB Mass Storage 10d6:0c02)" ;;
pc-disks)
    start_storage
    /bin/fbterm -1 -x 't:' -- /bin/sh -c '
        clear
        echo "======================================================"
        echo "   USB MASS STORAGE ACTIVE (SD + NAND -> WINDOWS PC)  "
        echo "======================================================"
        echo ""
        for l in /sys/class/android_usb/android0/f_mass_storage/lun*; do
            f=$(cat "$l/file" 2>/dev/null)
            [ -n "$f" ] && echo "  Exported $(basename "$l"): $f"
        done
        echo ""
        echo "  Drives are now visible in Windows Explorer!"
        echo "  Drop games into:  game/ps1  game/gba  game/nes"
        echo "                    game/gb   game/md   game/sfc"
        echo "  or Onion folders: onion/Roms/{PS,GBA,FC,GB,MD,SFC}"
        echo ""
        echo "  1) Safely Eject drives in Windows when finished."
        echo "  2) Press [B], [A], or [START] here to return!"
        /bin/xgbpowerd --wait-key
    '
    stop_storage_to_adb ;;
host)
    [ -f "$usb/enable" ] && echo 0 > "$usb/enable"
    killall adbd 2>/dev/null
    rmmod g_android libcomposite 2>/dev/null
    echo b > /proc/acts_udc 2>/dev/null
    echo a > /proc/acts_hcd
    echo "aotg0 switched to USB Host mode" ;;
adb)
    stop_storage_to_adb
    echo "aotg0 switched to ADB gadget mode" ;;
*)
    echo "Usage: usb-mode [storage|pc-disks|adb|host]"
    ls /sys/bus/usb/devices ;;
esac
"""
NAND_LOAD = b"""#!/bin/busybox sh
# Load Block-0-write-protected Actions NAND FTL modules on demand with merge_disable=1.
[ -b /dev/nand0 ] && exit 0
for m in nandid nandcore virnand; do insmod /lib/nand/$m.ko || exit 1; done
insmod /lib/nand/nandunit.ko merge_disable=1 || exit 1
for m in nandreq nanddisk; do insmod /lib/nand/$m.ko || exit 1; done
[ -x /bin/devmem ] && { /bin/devmem 0xb015000c 32 0x00000128 2>/dev/null; /bin/devmem 0xb015004c 32 0x00000001 2>/dev/null; }
mdev -s 2>/dev/null || true
"""
MOUNT_GAMES = b"""#!/bin/busybox sh
# Mount MicroSD (/mnt/sd) AND internal 8GB NAND game partitions (/mnt/udisk + /mnt/media) safely!
mkdir -p /mnt/media /mnt/udisk /mnt/sd /alpine
# Set CMU_SD0CLK=0x02 after owl_mmc probes (~22.8 MB/s read / 10.0 MB/s write, 0 CRC errors; 1 divider step margin above 0x00 cliff)
[ -x /bin/devmem ] && /bin/devmem 0xb0150050 32 0x00000002 2>/dev/null
if [ -b /dev/mmcblk0p1 ]; then
    mountpoint -q /mnt/sd 2>/dev/null || mount -t vfat -o rw,noatime,iocharset=utf8 /dev/mmcblk0p1 /mnt/sd 2>/dev/null
elif [ -b /dev/mmcblk0 ]; then
    mountpoint -q /mnt/sd 2>/dev/null || mount -t vfat -o rw,noatime,iocharset=utf8 /dev/mmcblk0 /mnt/sd 2>/dev/null
fi
/bin/nand-load >/dev/null 2>&1
for u in /sys/block/nand0/nand0p*/uevent; do
    [ -f "$u" ] || continue
    dev="/dev/$(sed -n 's/^DEVNAME=//p' "$u")"
    [ -b "$dev" ] || continue
    if grep -q '^PARTNAME=UDISK$' "$u" 2>/dev/null; then
        mountpoint -q /mnt/udisk 2>/dev/null || mount -t vfat -o rw,noatime,iocharset=utf8 "$dev" /mnt/udisk 2>/dev/null
    elif grep -q '^PARTNAME=MEDIA$' "$u" 2>/dev/null; then
        mountpoint -q /mnt/media 2>/dev/null || mount -t vfat -o rw,noatime,iocharset=utf8 "$dev" /mnt/media 2>/dev/null
    fi
done
if [ ! -x /alpine/bin/ash ] && [ -f /mnt/sd/linux/alpine.ext4 ]; then
    mount -t ext2 -o loop /mnt/sd/linux/alpine.ext4 /alpine 2>/dev/null || mount -t ext2 -o loop,ro /mnt/sd/linux/alpine.ext4 /alpine 2>/dev/null
fi
if [ -x /alpine/bin/ash ]; then
    mkdir -p /alpine/proc /alpine/sys /alpine/dev /alpine/tmp /alpine/mnt/sd /alpine/mnt/udisk /alpine/mnt/media
    [ -L /alpine/alpine ] || ln -sf . /alpine/alpine 2>/dev/null
    mountpoint -q /alpine/proc 2>/dev/null || mount --bind /proc /alpine/proc
    mountpoint -q /alpine/sys 2>/dev/null  || mount --bind /sys /alpine/sys
    mountpoint -q /alpine/dev 2>/dev/null  || mount --bind /dev /alpine/dev
    mountpoint -q /alpine/dev/pts 2>/dev/null || mount --bind /dev/pts /alpine/dev/pts
    mountpoint -q /alpine/tmp 2>/dev/null  || mount --bind /tmp /alpine/tmp
    for sub in sd udisk media; do
        mountpoint -q /mnt/$sub 2>/dev/null && (mountpoint -q /alpine/mnt/$sub 2>/dev/null || mount --bind /mnt/$sub /alpine/mnt/$sub 2>/dev/null)
    done
fi
# Full 256 MiB DDR3 active: do not swap onto FAT32 MicroSD (prevents mmc0 -110 I/O stalls)
if [ ! -f /tmp/.perf_loaded ] && [ -f /mnt/sd/linux/cpu_profile ]; then
    touch /tmp/.perf_loaded
    [ -x /bin/perf-mode ] && /bin/perf-mode init >/dev/null 2>&1
fi
"""
BOOT_STOCK = b"""#!/bin/busybox sh
# Boot native 480x320 Stock XGB35 OS with real NAND games (/mnt/udisk + /mnt/diska) + MicroSD (/mnt/card) and Block-0-protected NAND!
/bin/mount-games >/dev/null 2>&1
mkdir -p /mnt/sd /stock
if [ ! -f /mnt/sd/linux/stock-system.img ]; then
    /bin/fbterm -1 -x 't:' -- /bin/sh -c 'echo "Insert MicroSD card with /linux/stock-system.img first!"; sleep 4'
    exec /bin/xgbshell
fi
mkdir -p /mnt/sd/linux /mnt/sd/stock_upgrade /mnt/sd/stock_diska
[ -s /mnt/sd/linux/stock-config.bin ] || zcat /lib/stock-config.gz > /mnt/sd/linux/stock-config.bin
killall xgbpowerd fbterm 2>/dev/null
devmem 0xb01c0044 32 0x60600000 2>/dev/null
devmem 0xb01c0050 32 0x01F88100 2>/dev/null
echo 0 > /sys/class/backlight/owl_backlight/bl_power 2>/dev/null
mountpoint -q /stock 2>/dev/null || mount -t squashfs -o loop,ro /mnt/sd/linux/stock-system.img /stock || exit 91
mount --bind /dev /stock/dev
mount --bind /proc /stock/proc
mount --bind /sys /stock/sys
mount -t debugfs none /stock/sys/kernel/debug 2>/dev/null
mount -t tmpfs tmpfs /stock/tmp
mount -t tmpfs tmpfs /stock/var
mkdir -p /stock/dev/shm /stock/dev/pts /stock/dev/mqueue /stock/tmp/net /stock/tmp/block/mtdblockc/mtdblockc1 /stock/var/run /stock/var/upgrade
mount -t tmpfs tmpfs /stock/dev/shm
mount --bind /dev/pts /stock/dev/pts
mount -t mqueue none /stock/dev/mqueue 2>/dev/null
mount --bind /mnt/sd/stock_upgrade /stock/var/upgrade
mkdir -p /stock/var/upgrade/download
if mountpoint -q /mnt/media 2>/dev/null; then
    mount --bind /mnt/media /stock/mnt/diska
else
    mount --bind /mnt/sd/stock_diska /stock/mnt/diska
fi
[ -f /mnt/sd/linux/stock-res.img ] && (mountpoint -q /stock/usr/local 2>/dev/null || mount -t squashfs -o loop,ro /mnt/sd/linux/stock-res.img /stock/usr/local)
if mountpoint -q /mnt/udisk 2>/dev/null; then
    mount --bind /mnt/udisk /stock/mnt/udisk
else
    mount --bind /mnt/sd /stock/mnt/udisk
fi
mount --bind /mnt/sd /stock/mnt/card
cp -f /lib/stock-libconfigpart.so /stock/tmp/libconfigpart.so
printf 'PARTNAME=CONFIG\nDEVNAME=stockcfg\n' > /stock/tmp/block/mtdblockc/mtdblockc1/uevent
printf '256\n' > /stock/tmp/block/mtdblockc/mtdblockc1/size
ln -sf /mnt/card/linux/stock-config.bin /stock/dev/stockcfg
printf '#!/bin/sh\nexit 0\n' > /stock/tmp/usb.sh
printf '#!/bin/sh\nsync\nexec /bin/busybox reboot -f\n' > /stock/tmp/reboot
printf '#!/bin/sh\nexec /bin/xgbpowerd --off\n' > /stock/tmp/poweroff
chmod 755 /stock/tmp/usb.sh /stock/tmp/reboot /stock/tmp/poweroff
printf '0\n' > /stock/tmp/zero
[ -e /stock/sys/monitor/usb_port/status/pc_connected ] && mount --bind /stock/tmp/zero /stock/sys/monitor/usb_port/status/pc_connected 2>/dev/null
echo 1 > /proc/sys/vm/overcommit_memory
[ -f /stock/etc/wpa_supplicant.conf ] && cp /stock/etc/wpa_supplicant.conf /stock/tmp/wpa.cfg
insmod /stock/lib/modules/3.10.0/pvrsrvkm.ko 2>/dev/null
/bin/devmem 0xb0150048 32 0xBA320032 2>/dev/null
[ -f /stock/lib/modules/3.10.0/dc_owlfb.ko ] && insmod /stock/lib/modules/3.10.0/dc_owlfb.ko 2>/dev/null
/bin/audio-init 1 >/dev/null 2>&1
exec chroot /stock /bin/sh -c 'export LD_LIBRARY_PATH=/tmp:/var/upgrade:/mnt/udisk:/lib:/usr/lib:/usr/local/lib PATH=/tmp:/var/upgrade:/mnt/udisk:/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin; pvrsrvctl --start --no-module 2>/dev/null; ulimit -s 1024; exec /usr/bin/manager'
"""
BOOT_SUPERX = b"""#!/bin/busybox sh
# Boot adapted SuperX V1.1 CFW (RetroArch + SimplerMenu+ + HW DE upscaler) from /mnt/sd/rootfs.ext3.
mkdir -p /mnt/sd /superx
if [ -b /dev/mmcblk0p1 ]; then
    mountpoint -q /mnt/sd 2>/dev/null || mount -t vfat -o rw,noatime,iocharset=utf8 /dev/mmcblk0p1 /mnt/sd 2>/dev/null
elif [ -b /dev/mmcblk0 ]; then
    mountpoint -q /mnt/sd 2>/dev/null || mount -t vfat -o rw,noatime,iocharset=utf8 /dev/mmcblk0 /mnt/sd 2>/dev/null
fi
if [ ! -f /mnt/sd/rootfs.ext3 ]; then
    /bin/fbterm -1 -x 't:' -- /bin/sh -c 'echo "SuperX CFW (/mnt/sd/rootfs.ext3) not found on SD card yet!"; sleep 4'
    exec /bin/xgbshell
fi
devmem 0xb01c0044 32 0x60600000 2>/dev/null
devmem 0xb01c0050 32 0x01F88100 2>/dev/null
echo 0 > /sys/class/backlight/owl_backlight/bl_power 2>/dev/null
mountpoint -q /superx 2>/dev/null || mount -t ext3 -o loop /mnt/sd/rootfs.ext3 /superx || mount -o loop /mnt/sd/rootfs.ext3 /superx || exit 92
mkdir -p /superx/mnt/card /superx/proc /superx/sys /superx/tmp /superx/dev/pts /superx/dev/shm
mountpoint -q /superx/proc 2>/dev/null || mount --bind /proc /superx/proc
mountpoint -q /superx/sys 2>/dev/null  || mount --bind /sys /superx/sys
mountpoint -q /superx/dev 2>/dev/null  || mount --bind /dev /superx/dev
mountpoint -q /superx/dev/pts 2>/dev/null || mount --bind /dev/pts /superx/dev/pts
mountpoint -q /superx/tmp 2>/dev/null  || mount -t tmpfs tmpfs /superx/tmp
mountpoint -q /superx/dev/shm 2>/dev/null || mount -t tmpfs tmpfs /superx/dev/shm
mount -t debugfs debugfs /superx/sys/kernel/debug 2>/dev/null
mountpoint -q /superx/mnt/card 2>/dev/null || mount --bind /mnt/sd /superx/mnt/card
sed -i 's/^poweroff$/poweroff -f/' /superx/usr/bin/shutdown.sh 2>/dev/null
/bin/audio-init 1 >/dev/null 2>&1
for c in 0 1 2 3; do
    [ -w /sys/devices/system/cpu/cpu$c/online ] && echo 1 > /sys/devices/system/cpu/cpu$c/online 2>/dev/null
done
'
# Respect saved CPU profile (Eco/Balanced/Stock/Turbo) instead of forcing 900MHz inside chroot
/bin/perf-mode apply >/dev/null 2>&1
exec chroot /superx /bin/sh -c '
export PATH=/usr/local/bin:/usr/bin:/bin:/sbin:/usr/sbin:/mnt/card/cfw/java/bin
export LD_LIBRARY_PATH=/lib:/usr/lib:/usr/local/lib
export HOME=/mnt/card SDL_VIDEODRIVER=fbcon SDL_NOMOUSE=1 SDL_MOUSEDEV=/dev/null
export SDL_VIDEO_FBCON_ROTATION=CW SCREEN_WIDTH=480 SCREEN_HEIGHT=320
export SDL_AUDIODRIVER=alsa SDL_AUDIO_ALSA_DEBUG=0 SDL_AUDIO_ALLOW_FREQUENCY_CHANGE=0
export SMP_BASEPATH=/mnt/card/cfw/simplermenu_plus SMP_STATEFILE=/tmp/.state
export RA_CONFIG=/mnt/card/cfw/retroarch/retroarch.cfg SOUND_PROCESS_LIST=retroarch
watchdog_feeder 5 30 &
if [ -w /sys/kernel/debug/asoc/s900_link/atc260x-audio/codec_reg ]; then
    for r in "2 0x0223" "0 0x0022" "3 0xbebe" "5 0x0468" "7 0x26BF"; do
        echo "$r" > /sys/kernel/debug/asoc/s900_link/atc260x-audio/codec_reg 2>/dev/null
    done
fi
echo 0,480 > /sys/class/graphics/fb0/pan 2>/dev/null
echo 0,0 > /sys/class/graphics/fb0/pan 2>/dev/null
sed -i "s/video_rotation[[:space:]]*=[[:space:]]*\"[0-3]\"/video_rotation = \"1\"/" "$RA_CONFIG" 2>/dev/null
[ -f /mnt/card/cfw/resources/welcome.wav ] && tinyplay /mnt/card/cfw/resources/welcome.wav &
simplermenu_plus &
sleep 3
power_volume_handler &
wait
'
"""
BOOT_ONION = b"""#!/bin/busybox sh
# Boot Open MainUI (Onion OS v1.1 for XGB35) from /mnt/sd/onion with merged SD + NAND games & USB PC Disks app.
while :; do
    /bin/mount-games >/dev/null 2>&1
    if [ ! -f /mnt/sd/onion/bin/MainUI ]; then
        while :; do /bin/xgbshell; sleep 1; done
    fi
    devmem 0xb01c0044 32 0x60600000 2>/dev/null
    devmem 0xb01c0050 32 0x01F88100 2>/dev/null
    echo 0 > /sys/class/backlight/owl_backlight/bl_power 2>/dev/null
    /bin/audio-init >/dev/null 2>&1
    mkdir -p /mnt/SDCARD /superx /mnt/sd/onion/App/USBDisks /mnt/sd/onion/App/HDMIDesktop720p /mnt/sd/onion/App/HDMIDesktop1080p /mnt/sd/onion/App/CPUProfile
    if [ ! -f /mnt/sd/onion/App/USBDisks/launch.sh ]; then
        printf '{\n  "label": "USB Disks (SD+NAND)",\n  "icon": "/mnt/SDCARD/miyoo/app/skin/ic-retroarch.png",\n  "launch": "launch.sh"\n}\n' > /mnt/sd/onion/App/USBDisks/config.json
        printf '#!/bin/sh\ntouch /tmp/.usb_onion /tmp/.exit_onion\n' > /mnt/sd/onion/App/USBDisks/launch.sh
    fi
    if [ ! -f /mnt/sd/onion/App/HDMIDesktop720p/launch.sh ]; then
        printf '{\n  "label": "HDMI Desktop 720p",\n  "icon": "/mnt/SDCARD/miyoo/app/skin/ic-retroarch.png",\n  "launch": "launch.sh"\n}\n' > /mnt/sd/onion/App/HDMIDesktop720p/config.json
        printf '#!/bin/sh\ntouch /tmp/.hdmi720_onion /tmp/.exit_onion\n' > /mnt/sd/onion/App/HDMIDesktop720p/launch.sh
    fi
    if [ ! -f /mnt/sd/onion/App/HDMIDesktop1080p/launch.sh ]; then
        printf '{\n  "label": "HDMI Desktop 1080p",\n  "icon": "/mnt/SDCARD/miyoo/app/skin/ic-retroarch.png",\n  "launch": "launch.sh"\n}\n' > /mnt/sd/onion/App/HDMIDesktop1080p/config.json
        printf '#!/bin/sh\ntouch /tmp/.hdmi1080_onion /tmp/.exit_onion\n' > /mnt/sd/onion/App/HDMIDesktop1080p/launch.sh
    fi
    if [ ! -f /mnt/sd/onion/App/CPUProfile/launch.sh ]; then
        printf '{\n  "label": "CPU Mode (Eco/Turbo)",\n  "icon": "/mnt/SDCARD/miyoo/app/skin/ic-retroarch.png",\n  "launch": "launch.sh"\n}\n' > /mnt/sd/onion/App/CPUProfile/config.json
        printf '#!/bin/sh\n/bin/perf-mode cycle\n/bin/fbterm -1 -x "t:" -- /bin/sh -c "echo === CPU PROFILE UPDATED ===; echo; /bin/perf-mode status; sleep 2"\n' > /mnt/sd/onion/App/CPUProfile/launch.sh
    fi
    mountpoint -q /mnt/SDCARD 2>/dev/null || mount --bind /mnt/sd/onion /mnt/SDCARD
    if [ -f /mnt/sd/rootfs.ext3 ]; then
        mountpoint -q /superx 2>/dev/null || mount -o loop,ro /mnt/sd/rootfs.ext3 /superx 2>/dev/null
        if [ -x /superx/usr/bin/retroarch ]; then
            mkdir -p /superx/mnt/card /superx/proc /superx/sys /superx/dev /superx/tmp
            mountpoint -q /superx/proc 2>/dev/null || mount --bind /proc /superx/proc
            mountpoint -q /superx/sys 2>/dev/null  || mount --bind /sys /superx/sys
            mkdir -p /superx/mnt/media /superx/mnt/SDCARD
            mountpoint -q /superx/dev 2>/dev/null  || mount --bind /dev /superx/dev
            mountpoint -q /superx/mnt/card 2>/dev/null || mount --bind /mnt/sd /superx/mnt/card
            mountpoint -q /superx/mnt/media 2>/dev/null || mount --bind /mnt/media /superx/mnt/media
            mountpoint -q /superx/mnt/SDCARD 2>/dev/null || mount --bind /mnt/SDCARD /superx/mnt/SDCARD
        fi
    fi
    [ -d /alpine/usr/lib/libretro ] && ln -sf /alpine/usr/lib/libretro/* /usr/lib/libretro/ 2>/dev/null
    [ -d /mnt/sd/onion/cores ] && ln -sf /mnt/sd/onion/cores/* /usr/lib/libretro/ 2>/dev/null
    [ -x /usr/bin/xgbretro ] || ln -sf /bin/xgbretro /usr/bin/xgbretro 2>/dev/null
    # All games live directly on MicroSD in /mnt/sd/onion/Roms/* (no bind-mounts or symlinks needed!)
    mkdir -p /mnt/SDCARD/miyoo/app /usr/share
    if ! grep -q '"brightness"' /mnt/SDCARD/system.json 2>/dev/null; then
        printf '{\n\t"vol":\t14,\n\t"keymap":\t"",\n\t"mute":\t0,\n\t"bgmvol":\t8,\n\t"brightness":\t7,\n\t"language":\t"ru.lang",\n\t"hibernate":\t0,\n\t"lumination":\t7,\n\t"hue":\t10,\n\t"saturation":\t10,\n\t"contrast":\t10,\n\t"theme":\t"./"\n}\n' > /mnt/SDCARD/system.json
    fi
    [ -d /alpine/usr/share/alsa ] && ln -sfn /alpine/usr/share/alsa /usr/share/alsa 2>/dev/null
    [ -f /mnt/SDCARD/miyoo/app/wpa_supplicant ] || printf '#!/bin/sh\nexec /usr/sbin/wpa_supplicant "$@"\n' > /mnt/SDCARD/miyoo/app/wpa_supplicant
    [ -f /mnt/sd/wpa_supplicant.conf ] && cp -f /mnt/sd/wpa_supplicant.conf /appconfigs/wpa_supplicant.conf
    cp -f /mnt/SDCARD/bin/MainUI /tmp/MainUI && chmod 755 /tmp/MainUI
    echo 354 > /tmp/deviceModel
    rm -f /tmp/.exit_onion /tmp/.usb_onion /tmp/cmd_to_run.sh
    export LD_LIBRARY_PATH=/mnt/SDCARD/lib:/alpine/usr/lib:/alpine/lib:/usr/lib:/lib
    export PATH=/bin:/sbin:/usr/bin:/usr/sbin:/alpine/usr/bin:/alpine/bin
    while [ ! -f /tmp/.exit_onion ]; do
        cd /mnt/SDCARD/miyoo/app || cd /mnt/SDCARD
        /tmp/MainUI --sd-root /mnt/SDCARD --device real --handoff-dir /tmp >/tmp/mainui.log 2>&1
        if [ -f /tmp/cmd_to_run.sh ]; then
            sed -i 's|LD_PRELOAD=[^ ]* ||g; s|chmod a+x |/bin/true |g' /tmp/cmd_to_run.sh
            LD_LIBRARY_PATH=/lib /bin/tinymix 35 1 >/dev/null 2>&1
            /bin/sh /tmp/cmd_to_run.sh
            LD_LIBRARY_PATH=/lib /bin/tinymix 35 0 >/dev/null 2>&1
            rm -f /tmp/cmd_to_run.sh
            devmem 0xb01c0044 32 0x60600000 2>/dev/null
            devmem 0xb01c0050 32 0x01F88100 2>/dev/null
        else
            sleep 1
        fi
    done
    umount /mnt/SDCARD 2>/dev/null
    if [ -f /tmp/.usb_onion ]; then
        rm -f /tmp/.usb_onion /tmp/.exit_onion
        /bin/usb-mode pc-disks
        continue
    fi
    if [ -f /tmp/.hdmi720_onion ]; then
        rm -f /tmp/.hdmi720_onion /tmp/.exit_onion
        /bin/boot-desktop 720p
        continue
    fi
    if [ -f /tmp/.hdmi1080_onion ]; then
        rm -f /tmp/.hdmi1080_onion /tmp/.exit_onion
        /bin/boot-desktop 1080p
        continue
    fi
    rm -f /tmp/.exit_onion
    exit 0
done
"""
# installed name in initramfs -> file in system.img
ADB_FILES = {
    'bin/adbd': 'bin/adbd',
    'bin/tinymix': 'bin/tinymix',
    'bin/tinyplay': 'bin/tinyplay',
    'usr/bin/dosfsck': 'usr/bin/dosfsck',
    'lib/ld-uClibc.so.0': 'lib/ld-uClibc-0.9.33.2.so',
    'lib/libc.so.0': 'lib/libuClibc-0.9.33.2.so',
    'lib/libpthread.so.0': 'lib/libpthread-0.9.33.2.so',
    'lib/libdl.so.0': 'lib/libdl-0.9.33.2.so',
    'lib/libgcc_s.so.1': 'lib/libgcc_s.so.1',
    'lib/libm.so.0': 'lib/libm-0.9.33.2.so',
    'lib/libtinyalsa.so': 'usr/lib/libtinyalsa.so',
    'usr/lib/libnl-tiny.so': 'usr/lib/libnl-tiny.so',
    'usr/sbin/iw.real': 'usr/sbin/iw',
    'usr/sbin/iwconfig.real': 'usr/sbin/iwconfig',
    'usr/sbin/wpa_supplicant.real': 'usr/sbin/wpa_supplicant',
    'usr/sbin/wpa_cli.real': 'usr/sbin/wpa_cli',
    'lib/atm7051_board.ko': 'lib/modules/atm7051_board.ko',
    'lib/owl_gpio_matrix_adcjoystick.ko': 'lib/modules/3.10.0/owl_gpio_matrix_adcjoystick.ko',
}


REBOOT_ADFU_SH = b"""#!/bin/busybox sh
sync
/bin/devmem 0xb406f7f0 32 0x41444655
/bin/devmem 0xb406f7f4 32 0xa55a1234
/bin/devmem 0xb0158000 32 0x11
exec /bin/busybox reboot -f
"""

PERF_MODE_SH = b"""#!/bin/busybox sh
# Switch ATM7051H dual Cortex-A5 profiles: stock (900M@1.15V) | smart (dynamic 240M-1260M@1.00-1.20V UV+OC) | eco (240M) | balanced (600M) | turbo (1200M) | max (1272M)
CF=/sys/devices/system/cpu/cpu0/cpufreq
CUR=$(cat /tmp/cpu_profile 2>/dev/null || cat /mnt/sd/linux/cpu_profile 2>/dev/null || echo stock)
MODE="${1:-status}"
case "$MODE" in
init|apply)
    MODE="$CUR" ;;
cycle)
    case "$CUR" in
    stock)    MODE=smart ;;
    smart)    MODE=eco ;;
    eco)      MODE=balanced ;;
    balanced) MODE=turbo ;;
    turbo)    MODE=max ;;
    *)        MODE=stock ;;
    esac ;;
status)
    if [ "$CUR" = "smart" ] && [ -f /tmp/cpugov_status ]; then
        echo "Profile: smart ($(cat /tmp/cpugov_status))"
    else
        KHZ=$(cat "$CF/scaling_cur_freq" 2>/dev/null || echo 900000)
        PLL=$(/bin/devmem 0xb0150000 2>/dev/null)
        echo "Profile: $CUR ($((KHZ / 1000)) MHz, COREPLL=$PLL)"
    fi
    grep -E 'SwapTotal|SwapFree' /proc/meminfo 2>/dev/null
    exit 0 ;;
esac

if [ "$MODE" != "smart" ] && pidof xgbcpud >/dev/null 2>&1; then
    killall xgbcpud 2>/dev/null
    usleep 50000
fi

case "$MODE" in
smart)
    echo userspace > "$CF/scaling_governor" 2>/dev/null
    echo 900000 > "$CF/scaling_setspeed" 2>/dev/null
    pidof xgbcpud >/dev/null 2>&1 || setsid /bin/xgbcpud </dev/null >/dev/null 2>&1 & ;;
eco)
    echo interactive > "$CF/scaling_governor" 2>/dev/null
    echo 240000 > "$CF/scaling_min_freq" 2>/dev/null
    echo 240000 > "$CF/scaling_max_freq" 2>/dev/null ;;
balanced)
    echo interactive > "$CF/scaling_governor" 2>/dev/null
    echo 240000 > "$CF/scaling_min_freq" 2>/dev/null
    echo 600000 > "$CF/scaling_max_freq" 2>/dev/null ;;
turbo|max)
    # "max" used to poke COREPLL to 1272 MHz below its voltage and hung the SoC; it is now turbo.
    MODE=turbo
    echo performance > "$CF/scaling_governor" 2>/dev/null
    echo 1200000 > "$CF/scaling_max_freq" 2>/dev/null
    echo 1200000 > "$CF/scaling_min_freq" 2>/dev/null ;;
*)
    MODE=stock
    echo interactive > "$CF/scaling_governor" 2>/dev/null
    echo 240000 > "$CF/scaling_min_freq" 2>/dev/null
    echo 900000 > "$CF/scaling_max_freq" 2>/dev/null ;;
esac

echo "$MODE" > /tmp/cpu_profile
if [ -d /mnt/sd/linux ] && mountpoint -q /mnt/sd 2>/dev/null; then
    echo "$MODE" > /mnt/sd/linux/cpu_profile 2>/dev/null
fi
"""

BOOT_DESKTOP_SH = """#!/bin/busybox sh
# Launch Alpine LXDE / Openbox / JWM Desktop on external HDMI (/dev/fb1) at 720p@60 or 1080p@60 (+ Dual-Screen modes).
REQ_MODE="${1:-$(cat /tmp/desktop_res 2>/dev/null || echo 1080p)}"
if [ "$2" = "--" ]; then
    shift 2
    printf '%s\n' "$*" > /tmp/gamemode_cmd
    rm -f /tmp/gamemode_return_jwm
elif [ "$1" = "--" ]; then
    REQ_MODE="$(cat /tmp/desktop_res 2>/dev/null || echo 1080p)"
    shift 1
    printf '%s\n' "$*" > /tmp/gamemode_cmd
    rm -f /tmp/gamemode_return_jwm
fi
HDMI=/sys/devices/b02c0000.hdmi/panel.1/owl_panel/hdmi
HWT=/bin/hwtest

/bin/mount-games >/dev/null 2>&1
mkdir -p /desktop
if [ ! -x /desktop/usr/libexec/Xorg ] && [ -f /mnt/sd/linux/desktop.ext2 ]; then
    [ -x /alpine/sbin/e2fsck ] && chroot /alpine /sbin/e2fsck -p /mnt/sd/linux/desktop.ext2 >/dev/null 2>&1
    mount -t ext2 -o loop,noatime /mnt/sd/linux/desktop.ext2 /desktop 2>/dev/null || mount -t ext4 -o loop,noatime /mnt/sd/linux/desktop.ext2 /desktop
fi
if [ ! -x /desktop/usr/libexec/Xorg ]; then
    /bin/fbterm -1 -x 't:' -- /bin/sh -c 'echo "Desktop image /mnt/sd/linux/desktop.ext2 not found on SD!"; sleep 4'
    exit 1
fi

echo 1 > /proc/sys/kernel/printk 2>/dev/null
CF=/sys/devices/system/cpu/cpu0/cpufreq
CUR_PROF=$(cat /tmp/cpu_profile 2>/dev/null || cat /mnt/sd/linux/cpu_profile 2>/dev/null || echo stock)
if [ "$CUR_PROF" != "turbo" ] && [ "$CUR_PROF" != "max" ] && [ "$CUR_PROF" != "smart" ]; then
    echo performance > "$CF/scaling_governor" 2>/dev/null
    echo 900000 > "$CF/scaling_max_freq" 2>/dev/null
    echo 900000 > "$CF/scaling_min_freq" 2>/dev/null
fi

for km in /sys/devices/b0188000.i2c/i2c-2/2-0065/atc2603c-adckeypad*/input/input*/key_mode; do
    [ -w "$km" ] && echo 1 > "$km" 2>/dev/null
done
if [ -x /bin/xgbjoyx11 ]; then
    cp -f /bin/xgbjoyx11 /desktop/usr/bin/xgbjoyx11 2>/dev/null
    cp -f /bin/xgbjoyx11 /desktop/usr/local/bin/xgbjoyx11 2>/dev/null
fi
if [ -x /bin/xgb-keytest ]; then
    mkdir -p /desktop/usr/local/bin /desktop/root/Desktop
    cp -f /bin/xgb-keytest /desktop/usr/local/bin/xgb-keytest 2>/dev/null
    chmod 755 /desktop/usr/local/bin/xgb-keytest 2>/dev/null
    if [ ! -f /desktop/root/Desktop/10-keytest.desktop ]; then
        printf '[Desktop Entry]\nVersion=1.0\nType=Application\nName=Тестер кнопок приставки\nIcon=input-gaming\nExec=/usr/local/bin/xgb-keytest\nTerminal=false\nCategories=System;Utility;\n' > /desktop/root/Desktop/10-keytest.desktop
        chmod 755 /desktop/root/Desktop/10-keytest.desktop
    fi
fi

cleanup() {
    [ -n "$WATCH_PID" ] && kill "$WATCH_PID" 2>/dev/null
    killall xgbdisplay display-mode xgbjoyx11 lxpanel pcmanfm openbox jwm Xorg 2>/dev/null
    rm -f /tmp/desktop_dualscreen /tmp/desktop_relaunch
    [ -x /bin/perf-mode ] && /bin/perf-mode init >/dev/null 2>&1
    echo 20 > /proc/sys/vm/swappiness 2>/dev/null
    dd if=/dev/zero of=/dev/fb1 bs=65536 count=64 2>/dev/null
    /bin/devmem 0xb02f0100 32 0x00000000 2>/dev/null
    /bin/devmem 0xb02f0104 32 1 2>/dev/null
    usleep 30000
    /bin/devmem 0xb02f0148 32 1 2>/dev/null
    echo 4 > /sys/class/graphics/fb0/blank 2>/dev/null
    echo 0 > /sys/class/graphics/fb0/blank 2>/dev/null
    [ -x /bin/st7796s-init ] && /bin/st7796s-init
    /bin/devmem 0xb01c0044 32 0x60600000 2>/dev/null
    /bin/devmem 0xb01c0050 32 0x01F88100 2>/dev/null
    echo 0 > /sys/class/backlight/owl_backlight/bl_power 2>/dev/null
    [ -x /bin/display-mode ] && /bin/display-mode mirror-lcd
}
trap cleanup EXIT INT TERM

echo "$REQ_MODE" > /tmp/desktop_res
while :; do
    MODE=$(cat /tmp/desktop_res 2>/dev/null || echo "$REQ_MODE")
    rm -f /tmp/desktop_relaunch
    case "$MODE" in
    1080|1080p|fhd)
        W=1920; H=1080
        TIMING="1920x1080p-60Hz,6734,88/148/44,4/36/5"
        MODE_NAME="1920x1080p-60Hz"
        AVI_W0="0x006840b7"; AVI_W1="0x00000010" ;;
    *)
        W=1280; H=720
        TIMING="1280x720p-60Hz,13468,110/220/40,5/20/5"
        MODE_NAME="1280x720p-60Hz"
        AVI_W0="0x006840c3"; AVI_W1="0x00000004" ;;
    esac

    DUAL=$(cat /tmp/desktop_dualscreen 2>/dev/null || echo single)
    FB_W=$W
    [ "$DUAL" = "extend" ] && FB_W=$((W + 480))

    killall xgbdisplay display-mode xgbjoyx11 lxpanel pcmanfm openbox jwm Xorg 2>/dev/null
    rm -f /tmp/MainUI 2>/dev/null
    [ -n "$WATCH_PID" ] && kill "$WATCH_PID" 2>/dev/null
    echo 4 > /sys/class/backlight/owl_backlight/bl_power 2>/dev/null
    /bin/devmem 0xb02f0148 32 0 2>/dev/null
    /bin/devmem 0xb0150030 32 0x00001111 2>/dev/null
    /bin/devmem 0xb0150010 32 0x00000108 2>/dev/null
    sync
    echo 3 > /proc/sys/vm/drop_caches 2>/dev/null

    apply_hdmi_color() {
        /bin/devmem 0xb02c00f8 32 0x000d0282 2>/dev/null
        /bin/devmem 0xb02c00fc 32 "$AVI_W0" 2>/dev/null
        /bin/devmem 0xb02c0100 32 "$AVI_W1" 2>/dev/null
        /bin/devmem 0xb02c00f4 32 0x00001107 2>/dev/null
        /bin/devmem 0xb02c00e8 32 0x80010105 2>/dev/null
        /bin/devmem 0xb02c0058 32 0x08000021 2>/dev/null
    }

    echo 1 > "$HDMI/need_edid" 2>/dev/null
    CUR_MODE=$(cat "$HDMI/mode" 2>/dev/null)
    if [ "$CUR_MODE" != "$MODE_NAME" ]; then
        echo 0 > "$HDMI/enable" 2>/dev/null
        /bin/devmem 0x0075780c 32 0 2>/dev/null
        echo "$TIMING" > "$HDMI/default_mode" 2>/dev/null
        echo 0 > "$HDMI/hpd_enable" 2>/dev/null
        usleep 400000
        echo 1 > "$HDMI/hpd_enable" 2>/dev/null
        sleep 1
    fi
    [ "$(cat "$HDMI/enable" 2>/dev/null)" = "1" ] || echo 1 > "$HDMI/enable" 2>/dev/null
    "$HWT" hdmi-set "$FB_W" "$H"
    apply_hdmi_color

    # Enable 128 MiB swapfile AFTER hdmi-set allocates contiguous 1080p fb1 DMA memory
    if [ -f /mnt/sd/linux/swapfile ]; then
        grep -q '/mnt/sd/linux/swapfile' /proc/swaps 2>/dev/null || swapon /mnt/sd/linux/swapfile 2>/dev/null
        echo 15 > /proc/sys/vm/swappiness 2>/dev/null
        echo 50 > /proc/sys/vm/vfs_cache_pressure 2>/dev/null
    fi

    mkdir -p /desktop/proc /desktop/sys /desktop/dev /desktop/dev/pts /desktop/dev/shm /desktop/tmp /desktop/host /desktop/etc/X11 /desktop/var/log /desktop/mnt/sd /desktop/mnt/udisk /desktop/mnt/media /desktop/mnt/usb
    mountpoint -q /desktop/proc 2>/dev/null || mount --bind /proc /desktop/proc
    mountpoint -q /desktop/sys 2>/dev/null  || mount --bind /sys /desktop/sys
    mountpoint -q /desktop/dev 2>/dev/null  || mount --bind /dev /desktop/dev
    mountpoint -q /desktop/dev/pts 2>/dev/null || mount --bind /dev/pts /desktop/dev/pts
    mountpoint -q /desktop/dev/shm 2>/dev/null || mount --bind /dev/shm /desktop/dev/shm
    mountpoint -q /desktop/tmp 2>/dev/null  || mount --bind /tmp /desktop/tmp
    mountpoint -q /desktop/host 2>/dev/null || mount --bind / /desktop/host
    for sub in sd udisk media usb; do
        mountpoint -q /mnt/$sub 2>/dev/null && (mountpoint -q /desktop/mnt/$sub 2>/dev/null || mount --bind /mnt/$sub /desktop/mnt/$sub 2>/dev/null)
    done
    [ -s /etc/resolv.conf ] && cp -f /etc/resolv.conf /desktop/etc/resolv.conf 2>/dev/null

    cat > /desktop/etc/X11/xorg.conf <<EOF
Section "ServerFlags"
    Option "AutoAddDevices" "false"
    Option "AutoEnableDevices" "false"
    Option "AllowEmptyInput" "true"
    Option "DontVTSwitch" "true"
    Option "BlankTime" "0"
    Option "StandbyTime" "0"
    Option "SuspendTime" "0"
    Option "OffTime" "0"
EndSection

Section "Device"
    Identifier "OWL_HDMI"
    Driver "fbdev"
    Option "fbdev" "/dev/fb1"
    Option "ShadowFB" "true"
EndSection

Section "Monitor"
    Identifier "HDMI_Monitor"
EndSection

Section "Screen"
    Identifier "DefaultScreen"
    Device "OWL_HDMI"
    Monitor "HDMI_Monitor"
    DefaultDepth 16
EndSection

Section "ServerLayout"
    Identifier "MainLayout"
    Screen "DefaultScreen"
EndSection
EOF

    rm -f /tmp/.X0-lock /tmp/.X11-unix/X0 /desktop/var/log/Xorg.0.log /desktop/var/log/jwm.log
    mkdir -p /tmp/.X11-unix && chmod 1777 /tmp/.X11-unix

    chroot /desktop /usr/libexec/Xorg :0 -config /etc/X11/xorg.conf -nolisten tcp -noreset -keeptty vt1 >/desktop/var/log/xorg.out 2>&1 &
    X_PID=$!

    for _ in $(seq 1 60); do
        if [ -S /tmp/.X11-unix/X0 ]; then
            chroot /desktop /usr/bin/xsetroot -d :0 -solid '#0d1117' >/dev/null 2>&1 && break
        fi
        kill -0 "$X_PID" 2>/dev/null || break
        usleep 100000
    done

    if ! kill -0 "$X_PID" 2>/dev/null; then
        exit 2
    fi

    if [ "$DUAL" = "extend" ]; then
        /bin/display-mode extend "$W"
    else
        echo 4 > /sys/class/backlight/owl_backlight/bl_power 2>/dev/null
        /bin/devmem 0xb02f0148 32 0 2>/dev/null
        "$HWT" hdmi-kick
        echo single > /tmp/display_mode
    fi
    apply_hdmi_color
    (sleep 1; [ "$(cat /tmp/display_mode 2>/dev/null)" = "single" ] && "$HWT" hdmi-kick; apply_hdmi_color) </dev/null >/dev/null 2>&1 &

    (
        while kill -0 "$X_PID" 2>/dev/null; do
            if [ "$(cat "$HDMI/connected" 2>/dev/null)" = "0" ]; then
                sleep 3
                if [ "$(cat "$HDMI/connected" 2>/dev/null)" = "0" ]; then
                    rm -f /tmp/desktop_relaunch /tmp/desktop_dualscreen
                    killall lxpanel pcmanfm openbox jwm Xorg 2>/dev/null
                    exit 0
                fi
            fi
            sleep 2
        done
    ) </dev/null >/dev/null 2>&1 &
    WATCH_PID=$!

    if [ -s /tmp/gamemode_cmd ]; then
        GM_CMD=$(cat /tmp/gamemode_cmd)
        rm -f /tmp/gamemode_cmd
        sync; echo 3 > /proc/sys/vm/drop_caches 2>/dev/null
        [ -x /desktop/usr/bin/xgbjoyx11 ] && chroot /desktop /bin/sh -c 'DISPLAY=:0 /usr/bin/xgbjoyx11 >/dev/null 2>&1 &'
        chroot /desktop /bin/sh -c "export DISPLAY=:0 HOME=/root USER=root PATH=/usr/local/bin:/usr/bin:/bin:/sbin:/usr/sbin LANG=ru_RU.UTF-8 LC_ALL=ru_RU.UTF-8 NO_AT_BRIDGE=1; xset -dpms s off 2>/dev/null; exec $GM_CMD" >/desktop/var/log/gamemode.log 2>&1
        killall xgbjoyx11 2>/dev/null
        if [ -f /tmp/gamemode_return_jwm ]; then
            rm -f /tmp/gamemode_return_jwm
            touch /tmp/desktop_relaunch
        fi
    elif [ -x /desktop/usr/local/bin/start-xgb-lxde ]; then
        chroot /desktop /usr/local/bin/start-xgb-lxde >/desktop/var/log/jwm.log 2>&1
    else
        chroot /desktop /bin/sh -c 'export DISPLAY=:0 HOME=/root PATH=/usr/local/bin:/usr/bin:/bin:/sbin:/usr/sbin; exec /usr/bin/jwm' >/desktop/var/log/jwm.log 2>&1
    fi

    if [ -f /tmp/desktop_relaunch ] || [ -f /tmp/desktop_dualscreen ]; then
        continue
    fi
    break
done
""".encode('utf-8')

UDHCPC_SCRIPT = b"""#!/bin/busybox sh
[ -z "$1" ] && exit 1
case "$1" in
deconfig)
    ifconfig "$interface" 0.0.0.0 ;;
renew|bound)
    ifconfig "$interface" "$ip" ${broadcast:+broadcast $broadcast} ${subnet:+netmask $subnet}
    if [ -n "$router" ]; then
        while route del default gw 0.0.0.0 dev "$interface" 2>/dev/null; do :; done
        for r in $router; do route add default gw "$r" dev "$interface" 2>/dev/null; done
    fi
    : > /etc/resolv.conf
    for d in $dns; do echo "nameserver $d" >> /etc/resolv.conf; done
    [ -s /etc/resolv.conf ] || echo "nameserver 8.8.8.8" >> /etc/resolv.conf
    [ -f /appconfigs/wpa_supplicant.conf ] && [ -d /mnt/sd ] && mountpoint -q /mnt/sd 2>/dev/null && cp -f /appconfigs/wpa_supplicant.conf /mnt/sd/wpa_supplicant.conf 2>/dev/null ;;
esac
exit 0
"""

AXP_TEST_SH = b"""#!/bin/busybox sh
case "$1" in
wifion)
    [ -d /sys/class/net/wlan0 ] || insmod /lib/rt2800usb.ko 2>/dev/null
    for _ in 1 2 3 4 5 6 7 8 9 10; do [ -d /sys/class/net/wlan0 ] && break; usleep 200000; done
    ip link set wlan0 up 2>/dev/null ;;
wifioff)
    ip link set wlan0 down 2>/dev/null ;;
esac
exit 0
"""

WIFI_INIT_SH = """#!/bin/busybox sh
mkdir -p /var/run/wpa_supplicant /appconfigs /etc/init.d /mnt/usb "/lib/modules/$(uname -r)"
[ -f /appconfigs/wpa_supplicant.conf ] || printf 'ctrl_interface=/var/run/wpa_supplicant\nupdate_config=1\n' > /appconfigs/wpa_supplicant.conf
# Load rt2800usb.ko on demand only when a Ralink/MTK Wi-Fi dongle is attached (saves RAM when unused)
echo 1 > /proc/sys/kernel/printk 2>/dev/null
echo -1 > /sys/module/usbcore/parameters/autosuspend 2>/dev/null
# Initramfs BusyBox rmmod is modprobe-small and fails ("module not found"); Alpine BusyBox rmmod works.
rmmod() { if [ -x /alpine/bin/busybox ]; then /alpine/bin/busybox rmmod "$@"; else /bin/busybox rmmod "$@"; fi; }
BOUND_USB1=""
if grep -q '^aotg_hub_hcd ' /proc/modules 2>/dev/null && [ -d /sys/bus/platform/devices/aotg_hcd.1/usb1 ]; then
    BOUND_USB1=$(ls -di /sys/bus/platform/devices/aotg_hcd.1/usb1 2>/dev/null)
fi
LAST_SIG=""

while :; do
    # Zero-fork fast path: skip all USB/input/mount/Wi-Fi probing when hardware topology is unchanged
    set -- /sys/bus/usb/devices/* /sys/class/input/event* /dev/sd* /sys/class/net/wlan0 /desktop/usr/bin
    CUR_SIG="$*"
    if [ "$CUR_SIG" = "$LAST_SIG" ] && [ -d /sys/bus/platform/devices/aotg_hcd.1/usb1 ]; then
        sleep 3
        continue
    fi
    LAST_SIG="$CUR_SIG"

    # 1. Keep USB-A Host (aotg_hcd.1) & multi-device Hub TDM driver (aotg_hub_hcd.ko) alive across direct/hub hotplug
    if [ ! -d /sys/bus/platform/devices/aotg_hcd.1/usb1 ]; then
        grep -q '^aotg_hub_hcd ' /proc/modules 2>/dev/null && rmmod aotg_hub_hcd 2>/dev/null
        BOUND_USB1=""
        if [ -w /proc/acts_hcd ]; then
            echo c > /proc/acts_hcd
            for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
                [ -d /sys/bus/platform/devices/aotg_hcd.1/usb1 ] && break
                usleep 100000
            done
        fi
    fi
    if [ -d /sys/bus/platform/devices/aotg_hcd.1/usb1 ]; then
        CUR_USB1=$(ls -di /sys/bus/platform/devices/aotg_hcd.1/usb1 2>/dev/null)
        DEV_CLS=$(cat /sys/bus/usb/devices/1-1/bDeviceClass 2>/dev/null)
        if grep -q '^aotg_hub_hcd ' /proc/modules 2>/dev/null; then
            # Unload hub TDM driver when hub is unplugged or aotg_hcd.1 was recreated, so direct USB devices use stock HCD
            if [ ! -d /sys/bus/usb/devices/1-1 ] || { [ -n "$BOUND_USB1" ] && [ "$CUR_USB1" != "$BOUND_USB1" ]; }; then
                rmmod aotg_hub_hcd 2>/dev/null
                BOUND_USB1=""
            fi
        fi
        # Load hub TDM driver ONLY when an external USB Hub (class 09) is plugged into USB-A (1-1)
        if [ "$DEV_CLS" = "09" ] && ! grep -q '^aotg_hub_hcd ' /proc/modules 2>/dev/null; then
            KO=/lib/aotg_hub_hcd.ko
            [ -f /mnt/sd/linux/aotg_hub_hcd.ko ] && KO=/mnt/sd/linux/aotg_hub_hcd.ko
            if [ -f "$KO" ] && insmod "$KO" port_mask=2 force_full_speed=1 2>/dev/null; then
                BOUND_USB1=$(ls -di /sys/bus/platform/devices/aotg_hcd.1/usb1 2>/dev/null)
            fi
        fi
    fi

    # 2. Prevent autosuspend on root usb1, hubs, and direct/hub mice & ensure /dev/input/event* nodes exist
    NEW_USB=0
    for d in /sys/bus/usb/devices/*; do
        [ -w "$d/power/control" ] || continue
        [ -e "$d:1.0" ] && [ ! -e "$d:1.0/driver" ] && continue
        tag="/tmp/.usb_on_$(basename "$d")_$(cat "$d/devnum" 2>/dev/null)"
        if [ ! -f "$tag" ] || [ "$(cat "$d/power/control" 2>/dev/null)" != "on" ] || [ "$(cat "$d/power/runtime_status" 2>/dev/null)" = "suspended" ]; then
            rm -f "/tmp/.usb_on_$(basename "$d")_"* 2>/dev/null
            touch "$tag"
            [ -w "$d/power/autosuspend_delay_ms" ] && echo -1000 > "$d/power/autosuspend_delay_ms" 2>/dev/null
            echo auto > "$d/power/control" 2>/dev/null
            echo on > "$d/power/control" 2>/dev/null
            NEW_USB=1
        fi
    done
    mkdir -p /dev/input
    for ev in /sys/class/input/event*; do
        [ -f "$ev/dev" ] || continue
        evn=$(basename "$ev")
        if [ ! -c "/dev/input/$evn" ]; then
            IFS=: read maj min < "$ev/dev"
            mknod "/dev/input/$evn" c "$maj" "$min" 2>/dev/null
            NEW_USB=1
        fi
    done
    HAS_WIFI=0
    for vf in /sys/bus/usb/devices/*/idVendor; do
        [ -f "$vf" ] || continue
        case "$(cat "$vf" 2>/dev/null)" in
            148f|0e8d|0b05|07d1|13b1|1737|0411|2001|7392|050d|0db0) HAS_WIFI=1; break ;;
        esac
    done
    if [ "$NEW_USB" = "1" ]; then
        mdev -s 2>/dev/null || true
    fi
    if [ "$HAS_WIFI" = "1" ]; then
        [ -f /lib/rt2800usb.ko ] && ! grep -q '^rt2800usb ' /proc/modules 2>/dev/null && insmod /lib/rt2800usb.ko 2>/dev/null
    elif [ ! -d /sys/class/net/wlan0 ] && grep -q '^rt2800usb ' /proc/modules 2>/dev/null; then
        rmmod rt2800usb 2>/dev/null
    fi

    # 3. USB Flash Drive hotplug auto-mount (/mnt/usb + /desktop/mnt/usb + Desktop shortcut) & clean unplug
    if [ -f /tmp/.usb_mounted_dev ]; then
        MDEV=$(cat /tmp/.usb_mounted_dev 2>/dev/null)
        BNAME="${MDEV#/dev/}"
        if [ -z "$BNAME" ] || ! grep -q " ${BNAME}$" /proc/partitions 2>/dev/null; then
            rm -f /tmp/.usb_mounted_dev /desktop/root/Desktop/11-usb-drive.desktop 2>/dev/null
            umount -l /desktop/mnt/usb 2>/dev/null
            umount -l /alpine/mnt/usb 2>/dev/null
            umount -l /mnt/usb 2>/dev/null
        fi
    fi
    if [ ! -f /tmp/.usb_mounted_dev ]; then
        CAND=""
        for p in /dev/sd[a-d][1-9] /dev/sd[a-d]; do
            [ -b "$p" ] || continue
            bn="${p#/dev/}"
            if grep -q " ${bn}$" /proc/partitions 2>/dev/null; then
                CAND="$p"
                break
            fi
        done
        if [ -n "$CAND" ]; then
            mkdir -p /mnt/usb
            if mount -t vfat -o rw,noatime,iocharset=utf8 "$CAND" /mnt/usb 2>/dev/null || \
               mount -t exfat -o rw,noatime "$CAND" /mnt/usb 2>/dev/null || \
               mount -o rw,noatime "$CAND" /mnt/usb 2>/dev/null; then
                echo "$CAND" > /tmp/.usb_mounted_dev
                if [ -d /desktop/usr/bin ]; then
                    mkdir -p /desktop/mnt/usb /desktop/root/Desktop
                    mountpoint -q /desktop/mnt/usb 2>/dev/null || mount --bind /mnt/usb /desktop/mnt/usb 2>/dev/null
                    printf '[Desktop Entry]\nVersion=1.0\nType=Application\nName=USB Флешка (/mnt/usb)\nIcon=drive-removable-media\nExec=/usr/bin/pcmanfm /mnt/usb\nTerminal=false\n' > /desktop/root/Desktop/11-usb-drive.desktop
                    chmod 755 /desktop/root/Desktop/11-usb-drive.desktop 2>/dev/null
                fi
                if [ -d /alpine/bin ]; then
                    mkdir -p /alpine/mnt/usb
                    mountpoint -q /alpine/mnt/usb 2>/dev/null || mount --bind /mnt/usb /alpine/mnt/usb 2>/dev/null
                fi
            fi
        fi
    elif [ -d /desktop/usr/bin ] && ! mountpoint -q /desktop/mnt/usb 2>/dev/null; then
        mkdir -p /desktop/mnt/usb
        mount --bind /mnt/usb /desktop/mnt/usb 2>/dev/null
    fi

    # 4. USB Wi-Fi (wlan0) hotplug & automatic WPA2/DHCP reconnect
    if [ -d /sys/class/net/wlan0 ]; then
        if [ ! -f /tmp/.wifi_up ]; then
            touch /tmp/.wifi_up
            ip link set wlan0 up 2>/dev/null
            /bin/mount-games >/dev/null 2>&1
            if [ -f /mnt/sd/wifi.txt ]; then
                SSID=$(sed -n '1p' /mnt/sd/wifi.txt | tr -d '\r')
                PASS=$(sed -n '2p' /mnt/sd/wifi.txt | tr -d '\r')
                [ -n "$SSID" ] && /bin/wifi-connect "$SSID" "$PASS" >/dev/null 2>&1 &
            elif [ -f /mnt/sd/wpa_supplicant.conf ]; then
                cp -f /mnt/sd/wpa_supplicant.conf /appconfigs/wpa_supplicant.conf
            fi
            if [ ! -f /mnt/sd/wifi.txt ] && grep -q 'network=' /appconfigs/wpa_supplicant.conf 2>/dev/null; then
                killall wpa_supplicant.real udhcpc 2>/dev/null
                usleep 200000
                rm -f /var/run/wpa_supplicant/wlan0
                /usr/sbin/wpa_supplicant -B -D nl80211 -i wlan0 -c /appconfigs/wpa_supplicant.conf 2>/dev/null
                udhcpc -i wlan0 -s /etc/init.d/udhcpc.script -T 1 -t 10 -A 3 -b -q 2>/dev/null
            fi
        fi
        [ -s /etc/resolv.conf ] && [ -d /desktop/etc ] && cp -f /etc/resolv.conf /desktop/etc/resolv.conf 2>/dev/null
    elif [ -f /tmp/.wifi_up ]; then
        rm -f /tmp/.wifi_up /var/run/wpa_supplicant/wlan0
        killall wpa_supplicant.real udhcpc 2>/dev/null
    fi
    sleep 3
done
""".encode('utf-8')

WIFI_CONNECT_SH = b"""#!/bin/busybox sh
if [ -z "$1" ]; then
    echo "Usage: wifi-connect <SSID> [PASSWORD]"
    exit 1
fi
SSID="$1"
PASS="$2"
/customer/app/axp_test wifion
killall wpa_supplicant.real udhcpc 2>/dev/null
usleep 300000
rm -f /var/run/wpa_supplicant/wlan0
if [ -n "$PASS" ]; then
    printf 'ctrl_interface=/var/run/wpa_supplicant\nupdate_config=1\n\nnetwork={\n\tssid="%s"\n\tpsk="%s"\n}\n' "$SSID" "$PASS" > /appconfigs/wpa_supplicant.conf
else
    printf 'ctrl_interface=/var/run/wpa_supplicant\nupdate_config=1\n\nnetwork={\n\tssid="%s"\n\tkey_mgmt=NONE\n}\n' "$SSID" > /appconfigs/wpa_supplicant.conf
fi
[ -d /mnt/sd ] && mountpoint -q /mnt/sd 2>/dev/null && cp -f /appconfigs/wpa_supplicant.conf /mnt/sd/wpa_supplicant.conf 2>/dev/null
/usr/sbin/wpa_supplicant -B -D nl80211 -i wlan0 -c /appconfigs/wpa_supplicant.conf || exit 2
exec udhcpc -i wlan0 -s /etc/init.d/udhcpc.script -T 1 -t 15 -A 3 -b
"""


def fat16():
    image = bytearray(RAM_SECTORS * 512)
    fat_sectors, root_entries = 32, 512
    image[:11] = b'\xeb\x3c\x90MSDOS5.0'
    struct.pack_into('<HBHBHHBHHHII', image, 11,
                     512, 4, 1, 2, root_entries, RAM_SECTORS, 0xF8, fat_sectors, 32, 64, 0, 0)
    struct.pack_into('<BBBI11s8s', image, 36, 0x80, 0, 0x29, 0x70510001,
                     b'RAMUSB     ', b'FAT16   ')
    image[510:512] = b'\x55\xaa'
    for sector in (1, 1 + fat_sectors):
        struct.pack_into('<3H', image, sector*512, 0xFFF8, 0xFFFF, 0xFFFF)
    root = (1 + 2 * fat_sectors) * 512
    image[root:root+12] = b'RAMUSB     \x08'
    image[root+32:root+44] = b'RAMTEST TXT\x20'
    proof = b'ATM7051 USB Mass Storage is running from RAM.\r\n'
    struct.pack_into('<HI', image, root+32+26, 2, len(proof))
    data = root + root_entries * 32
    image[data:data+len(proof)] = proof
    return bytes(image)


DISPLAY_NODES = ('de@', 'lcd@', 'hdmi@', 'gpu@')


def device_tree(source, initrd_size, headless=False, memory_limit=LIMIT, full_hw=False):
    header = list(struct.unpack_from('>10I', source))
    magic, total, offset, strings, _, version, _, _, names_size, tree_size = header
    assert (magic, total, version) == (0xD00DFEED, len(source), 17)
    names = bytearray(source[strings:strings+names_size])
    tree, path, edits, seen = bytearray(), [], {}, set()
    bootargs = (b'console=ttyS5,115200 earlyprintk loglevel=4 vmalloc=880M '
                b'rdinit=/init ro nr_cpus=2 maxcpus=2 panic=2 clk_ignore_unused\0' if full_hw else
                b'console=ttyS5,115200 earlyprintk loglevel=8 '
                b'rdinit=/init ro maxcpus=1 panic=2 clk_ignore_unused\0')
    edits.update({
        ('/memory', 'reg'): struct.pack('>II', 0, memory_limit),
        ('/chosen', 'bootargs'): bootargs,
        ('/chosen', 'linux,initrd-start'): struct.pack('>I', INITRD),
        ('/chosen', 'linux,initrd-end'): struct.pack('>I', INITRD+initrd_size),
    })
    if full_hw:
        # Drop mfp1_9 (P_LCD0_D18 + P_LCD0_D2, unused on 16-bit RGB565) so all 20 keypad buttons work.
        edits[('/pinctrl@b01c0000/lcd0_default/lcd0_mux', 'actions,groups')] = (
            b'mfp0_31_29\0mfp0_28_26\0mfp0_25_23\0mfp0_22_20_sirq0\0'
            b'mfp0_19_17\0mfp0_16_14\0mfp1_22\0mfp1_21\0')
        # Push PWM0 (backlight) from 20 kHz (audible squeal) to 33.3 kHz ultrasound.
        # Keep PWM1 (/vcom) at 100% factory 50000 ns (20 kHz, dft_vcom=20) so ST7796S VCOM DC bias has zero 30Hz flicker or ghosting!
        edits[('/backlight', 'pwms')] = struct.pack('>4I', 0x21, 0, 30000, 0)
        # Set HDMI panel native draw_width=1280, draw_height=720 (instead of clone 320x480) so DE boots in 1920px HD mode!
        edits[('/hdmi@b02c0000/panel@ghp', 'draw_width')] = struct.pack('>I', 1280)
        edits[('/hdmi@b02c0000/panel@ghp', 'draw_height')] = struct.pack('>I', 720)
        # Keep SD0 within XGB35's factory 30 MHz PCB limit when DEVPLL=480 MHz (2x stock clk cache -> 15MHz DTB = 30MHz real!)
        edits[('/mmc@b0340000', 'max_clk')] = struct.pack('>I', 15000000)
        if memory_limit >= 0x08000000:
            # Put 24 MiB ION pmem at top of RAM (contiguous mem_map!) while CMA stays <128 MiB via 0xC06BA040!
            edits[('/ion_config', 'actions,ion_heap_pmem')] = struct.pack('>II', memory_limit - 0x01800000, 0x01800000)

    def prop(key, value):
        name = key[1].encode() + b'\0'
        name_at = names.find(name)
        if name_at < 0:
            name_at = len(names)
            names.extend(name)
        tree.extend(struct.pack('>III', 3, len(value), name_at))
        tree.extend(value + bytes(-len(value) % 4))
        seen.add(key)

    def finish_properties():
        for key, value in edits.items():
            if key[0] == '/'.join(path) and key not in seen:
                prop(key, value)

    pos = offset
    while pos < offset + tree_size:
        start = pos
        token, = struct.unpack_from('>I', source, pos)
        pos += 4
        if token == 1:
            if path:
                finish_properties()
            end = source.index(0, pos)
            name = source[pos:end].decode()
            path.append(name)
            display = headless and (
                len(path) == 2 and (
                    name.startswith(DISPLAY_NODES) or name in ('backlight', 'vcom', 'framebuffer'))
                or len(path) == 3 and path[-2].startswith(('lcd@', 'hdmi@')) and name.startswith('panel@'))
            if (not full_hw and name.startswith(('nand@', 'mmc@'))) or display:
                edits[('/'.join(path), 'status')] = b'disabled\0'
            pos = (end + 4) & ~3
        elif token == 2:
            finish_properties()
            path.pop()
        elif token == 3:
            size, name_at = struct.unpack_from('>II', source, pos)
            name = names[name_at:names.index(0, name_at)].decode()
            pos = (pos + 8 + size + 3) & ~3
            key = '/'.join(path), name
            if key in edits:
                prop(key, edits[key])
                continue
        else:
            assert token in (4, 9)
        if token == 9 and full_hw:
            # 512 bytes of FDT_NOP padding so U-Boot bootm fixups (bootargs.add, initrd, memory) fit in-place.
            tree.extend(struct.pack('>I', 4) * 128)
        tree.extend(source[start:pos])
        if token == 9:
            break
    expected_status = 13 if headless else (0 if full_hw else 4)
    assert seen == set(edits) and sum(key[1] == 'status' for key in seen) == expected_status
    header[1], header[3] = offset+len(tree)+len(names), offset+len(tree)
    header[8:10] = len(names), len(tree)
    return struct.pack('>10I', *header) + source[40:offset] + tree + names


def system_file(name):
    return subprocess.check_output([
        'C:/Program Files/7-Zip/7z.exe', 'e', '-so', str(ROOT.parent / 'system.img'), name])


def build(compressed_entry=False, headless=False, memory128=False, memory256=False, adb=False):
    update = ROOT.parent / 'misc'
    _, zimage = read_uimage((update / 'uImage').read_bytes())
    _, compressed = read_uimage((update / 'ramdisk.img').read_bytes())
    stock_ramdisk = read_cpio(gzip.decompress(compressed))
    busybox = stock_ramdisk['bin/busybox'][1]
    check_static_arm_elf(busybox)
    for entry in re.finditer(bytes.fromhex('fd377a585a00'), zimage):
        try:
            decompressor = lzma.LZMADecompressor()
            kernel = decompressor.decompress(zimage[entry.start():], max_length=0x800000)
            if decompressor.eof and b'Linux version 3.10.52' in kernel:
                break
        except lzma.LZMAError:
            continue
    else:
        raise ValueError('Could not find the ARM kernel image in the update')
    assert len(kernel) == 7454656 and len(kernel) % 4 == 0

    dirs = ('bin', 'sys', 'proc', 'dev', 'lib') + ((
        'lib/nand', 'lib/firmware', 'etc', 'etc/init.d', 'sbin', 'usr', 'usr/bin', 'usr/sbin',
        'usr/lib', 'usr/lib/libretro', 'tmp', 'var', 'var/run', 'var/run/wpa_supplicant',
        'appconfigs', 'customer', 'customer/app',
        'mnt', 'mnt/udisk', 'mnt/media', 'mnt/sd', 'mnt/usb', 'alpine', 'desktop', 'stock', 'superx') if adb else ())
    entries = [(n, stat.S_IFDIR | 0o755, b'', 0, 0) for n in dirs]
    entries.extend([
        ('dev/console', stat.S_IFCHR | 0o600, b'', 5, 1),
        ('dev/null', stat.S_IFCHR | 0o666, b'', 1, 3),
        ('bin/busybox', stat.S_IFREG | 0o755, busybox, 0, 0),
        ('init', stat.S_IFREG | 0o755, ADB_INIT if adb else INIT, 0, 0),
    ])
    if adb:
        entries.extend([
            ('dev/random', stat.S_IFCHR | 0o666, b'', 1, 8),
            ('dev/urandom', stat.S_IFCHR | 0o666, b'', 1, 9),
            ('bin/adb_shell', stat.S_IFREG | 0o755, ADB_SHELL, 0, 0),
            ('bin/audio-init', stat.S_IFREG | 0o755, AUDIO_INIT, 0, 0),
            ('bin/usb-mode', stat.S_IFREG | 0o755, USB_MODE, 0, 0),
            ('bin/nand-load', stat.S_IFREG | 0o755, NAND_LOAD, 0, 0),
            ('bin/mount-games', stat.S_IFREG | 0o755, MOUNT_GAMES, 0, 0),
            ('bin/boot-stock', stat.S_IFREG | 0o755, BOOT_STOCK, 0, 0),
            ('bin/sgx-init', stat.S_IFREG | 0o755,
             (ROOT.parents[1] / 'multiboot/sgx-init.sh').read_bytes().replace(b'\r\n', b'\n'), 0, 0),
            ('bin/boot-superx', stat.S_IFREG | 0o755, BOOT_SUPERX, 0, 0),
            ('bin/boot-onion', stat.S_IFREG | 0o755, BOOT_ONION, 0, 0),
            ('bin/boot-desktop', stat.S_IFREG | 0o755, BOOT_DESKTOP_SH, 0, 0),
            ('bin/perf-mode', stat.S_IFREG | 0o755, PERF_MODE_SH, 0, 0),
            ('bin/reboot-adfu', stat.S_IFREG | 0o755, REBOOT_ADFU_SH, 0, 0),
            ('bin/wifi-init', stat.S_IFREG | 0o755, WIFI_INIT_SH, 0, 0),
            ('bin/wifi-connect', stat.S_IFREG | 0o755, WIFI_CONNECT_SH, 0, 0),
            ('etc/init.d/udhcpc.script', stat.S_IFREG | 0o755, UDHCPC_SCRIPT, 0, 0),
            ('customer/app/axp_test', stat.S_IFREG | 0o755, AXP_TEST_SH, 0, 0),
        ])
        for wrap in ('iw', 'iwconfig', 'wpa_supplicant', 'wpa_cli'):
            wsh = f'#!/bin/busybox sh\nexport LD_LIBRARY_PATH=/lib:/usr/lib\nexec /usr/sbin/{wrap}.real "$@"\n'.encode()
            entries.append((f'usr/sbin/{wrap}', stat.S_IFREG | 0o755, wsh, 0, 0))
        wifi_dir = ROOT / 'wifi-rt2800usb'
        if (wifi_dir / 'rt2800usb.ko').exists() and (wifi_dir / 'rt2870.bin').exists():
            entries.append(('lib/rt2800usb.ko', stat.S_IFREG | 0o644, (wifi_dir / 'rt2800usb.ko').read_bytes(), 0, 0))
            entries.append(('lib/firmware/rt2870.bin', stat.S_IFREG | 0o644, (wifi_dir / 'rt2870.bin').read_bytes(), 0, 0))
        hub_dir = ROOT / 'usb-hub-aotg'
        if (hub_dir / 'aotg_hub_hcd.ko').exists():
            entries.append(('lib/aotg_hub_hcd.ko', stat.S_IFREG | 0o644, (hub_dir / 'aotg_hub_hcd.ko').read_bytes(), 0, 0))
        if (hub_dir / 'hub-mode.sh').exists():
            entries.append(('bin/hub-mode', stat.S_IFREG | 0o755, (hub_dir / 'hub-mode.sh').read_bytes().replace(b'\r\n', b'\n'), 0, 0))
        nand_guards = {
            'nandcore.ko': ((0x10258, 0xE59FC104), (0x147EC, 0xE92D4FF0)),
            'virnand.ko':  ((0x29C0,  0xE92D4FF0),),
            'nandunit.ko': ((0x5868,  0xE92D4070),),
            'nanddisk.ko': ((0x10F8,  0xE92D47F0),),
        }
        for mod_path, (_, mod_data) in sorted(stock_ramdisk.items()):
            if mod_path.startswith('lib/modules/') and mod_path.endswith('.ko'):
                mname = Path(mod_path).name
                if mname in nand_guards:
                    buf = bytearray(mod_data)
                    for foff, exp_w0 in nand_guards[mname]:
                        assert struct.unpack_from('<I', buf, foff)[0] == exp_w0, (mname, hex(foff))
                        struct.pack_into('<2I', buf, foff, 0xE3A00000, 0xE12FFF1E)
                    mod_data = bytes(buf)
                entries.append((f'lib/nand/{mname}', stat.S_IFREG | 0o644, mod_data, 0, 0))
        if not headless:
            for tool in ('st7796s-init', 'fbterm', 'hwtest', 'xgbshell', 'xgbretro', 'xgbpowerd', 'xgbdisplay', 'xgbjoyx11', 'xgb-keytest'):
                blob = (ROOT / 'fbterm' / tool).read_bytes()
                assert blob[:6] == b'\x7fELF\x01\x01' and struct.unpack_from('<H', blob, 18)[0] == 40
                entries.append((f'bin/{tool}', stat.S_IFREG | 0o755, blob, 0, 0))
            entries.append(('bin/display-mode', stat.S_IFLNK | 0o777, b'xgbdisplay', 0, 0))
            if (ROOT / 'fbterm' / 'libgcompat.so').exists():
                gc = (ROOT / 'fbterm' / 'libgcompat.so').read_bytes()
                assert gc[:6] == b'\x7fELF\x01\x01' and struct.unpack_from('<H', gc, 18)[0] == 40
                entries.append(('usr/lib/libgcompat.so', stat.S_IFREG | 0o755, gc, 0, 0))
            staging = ROOT / 'alpine-bundle' / 'staging'
            for dst, src in (
                ('lib/ld-musl-armhf.so.1', 'ld-musl-armhf.so.1'),
                ('usr/lib/libgcc_s.so.1', 'libgcc_s.so.1'),
                ('usr/lib/libretro/2048_libretro.so', '2048_libretro.so'),
            ):
                blob = (staging / src).read_bytes()
                assert blob[:6] == b'\x7fELF\x01\x01' and struct.unpack_from('<H', blob, 18)[0] == 40, src
                entries.append((dst, stat.S_IFREG | 0o755, blob, 0, 0))
            # Point libstdc++.so.6 to Alpine ext4 on SD instead of pinning 1.9 MiB in initramfs RAM
            entries.append(('usr/lib/libstdc++.so.6', stat.S_IFLNK | 0o777, b'/alpine/usr/lib/libstdc++.so.6', 0, 0))
            entries.append(('lib/libc.musl-armv7.so.1', stat.S_IFLNK | 0o777, b'ld-musl-armhf.so.1', 0, 0))
            # zig-linked dynamic musl tools (xgbretro) record NEEDED "libc.so"
            entries.append(('lib/libc.so', stat.S_IFLNK | 0o777, b'ld-musl-armhf.so.1', 0, 0))
            entries.append(('bin/devmem', stat.S_IFLNK | 0o777, b'xgbpowerd', 0, 0))
            entries.append(('bin/xgbcpud', stat.S_IFLNK | 0o777, b'xgbpowerd', 0, 0))
            entries.append(('bin/screenshot', stat.S_IFLNK | 0o777, b'xgbpowerd', 0, 0))
        for name, source in ADB_FILES.items():
            data = system_file(source)
            assert data[:6] == b'\x7fELF\x01\x01' and struct.unpack_from('<H', data, 18)[0] == 40, source
            if name == 'lib/owl_gpio_matrix_adcjoystick.ko':
                buf = bytearray(data)
                assert buf[0x11dc] == 0x0A and buf[0x1210] == 0x0A
                buf[0x11dc] = 0x01
                buf[0x1210] = 0x01
                data = bytes(buf)
            entries.append((name, stat.S_IFREG | 0o755, data, 0, 0))
        # SD-backed CONFIG partition shim for Stock XGB35 OS (avoids loading nandunit.ko over Block 0):
        cfg_so = system_file('usr/lib/libconfigpart.so')
        assert cfg_so.count(b'/sys/block/') == 6
        entries.append(('lib/stock-libconfigpart.so', stat.S_IFREG | 0o755, cfg_so.replace(b'/sys/block/', b'/tmp/block/'), 0, 0))
        cfg_raw = (ROOT.parent / 'nand-partitions-backup/nand0p3-CONFIG.img').read_bytes()
        assert len(cfg_raw) == 131072
        entries.append(('lib/stock-config.gz', stat.S_IFREG | 0o644, gzip.compress(cfg_raw, 9), 0, 0))
    else:
        entries.append(('ramusb.img', stat.S_IFREG | 0o400, fat16(), 0, 0))
    for module in ('libcomposite.ko', 'g_android.ko'):
        data = system_file(f'lib/modules/3.10.0/{module}')
        assert data[:6] == b'\x7fELF\x01\x01' and b'vermagic=3.10.52 ' in data
        if module == 'g_android.ko':
            assert all(marker in data for marker in (b'fsg_store_ro\0', b'mass_storage\0', b'parmtype=nluns:int\0'))
        entries.append((f'lib/{module}', stat.S_IFREG | 0o400, data, 0, 0))
    cpio = make_cpio(entries)
    parsed = read_cpio(cpio)
    assert all(parsed[n][1] == data for n, _, data, _, _ in entries)
    assert set(parsed) == {n for n, *_ in entries}
    assert adb or len(parsed['ramusb.img'][1]) == RAM_SECTORS * 512
    initrd = gzip.compress(cpio, mtime=0)
    memory_limit = LIMIT
    if memory256:
        memory_limit = 0x10000000
    elif memory128:
        from adfu_ram_geometry_probe import ADDRESSES, LIMIT as RAM128, PATTERNS, PROOF
        proof = json.loads(PROOF.read_text())
        assert (proof['memory_limit'], proof['ddr_pll'], proof['originals_restored']) == (RAM128, 0x13C, True)
        assert tuple(proof['addresses']) == ADDRESSES and tuple(proof['patterns']) == PATTERNS
        memory_limit = RAM128
    dtb = device_tree((update / 'kernel.dtb').read_bytes(), len(initrd), headless=headless,
                      memory_limit=memory_limit, full_hw=adb and not headless)
    assert STAGING + len(kernel) < INITRD and INITRD + len(initrd) < DTB and DTB + len(dtb) < LIMIT
    entry = {'zImage': (STAGING, zimage)} if compressed_entry else {'Image': (STAGING, kernel)}
    return {**entry, 'initramfs.cpio.gz': (INITRD, initrd), 'kernel.dtb': (DTB, dtb)}


def patch_kernel_hw(image):
    """Apply non-headless XGB35 kernel fixes: quiet HDCP spam + live I2C read in powerkey_status_show."""
    hdcp_off = 0x1EEA18 - KERNEL
    assert struct.unpack_from('<2I', image, hdcp_off) == (0xE92D43F0, 0xE3000A30)
    struct.pack_into('<2I', image, hdcp_off, 0xE3A00001, 0xE12FFF1E)

    # Make sysfs powerkey_status_show (0xC02ABF78) read ATC2603C reg 0x02 bit 15 (ONOFF_PRESS) on demand
    # in process context (uses unused ATC2603A 0xC02ABE8C..0xC02ABEB0 & ATC2609A 0xC02ABE64..0xC02ABE70,
    # keeping active ATC2603C _atc2603c_onoff_init @ 0xC02ABE78..0xC02ABE88 100% untouched!).
    voff = lambda va: va - 0xC0000000 - KERNEL
    assert struct.unpack_from('<3I', image, voff(0xC02ABE64)) == (0xE5900008, 0xE3A01002, 0xE3072C06)
    assert struct.unpack_from('<5I', image, voff(0xC02ABE78)) == (0xE5900008, 0xE3A01002, 0xE3072C06, 0xE3073806, 0xEAFE68B7)
    assert struct.unpack_from('<2I', image, voff(0xC02ABE8C)) == (0xE5900008, 0xE3A01002)
    assert struct.unpack_from('<2I', image, voff(0xC02ABF78)) == (0xE30D39CC, 0xE1A00002)

    struct.pack_into('<10I', image, voff(0xC02ABE8C),
                     0xE92D4010, 0xE1A04002, 0xE30A3D08, 0xE34C3075,
                     0xE5933000, 0xE5930004, 0xE3A01002,
                     branch(0xC02ABEA8, 0xC02462C4, link=True),
                     0xE1C00FC0, branch(0xC02ABEB0, 0xC02ABF7C))
    struct.pack_into('<6I', image, voff(0xC02ABF78),
                     branch(0xC02ABF78, 0xC02ABE8C),
                     0xE1A007A0, 0xE2000001, 0xE2800030, 0xE3800C0A,
                     branch(0xC02ABF8C, 0xC02ABE64))
    struct.pack_into('<3I', image, voff(0xC02ABE64),
                     0xE5840000, 0xE3A00002, 0xE8BD8010)

    # Fix Actions aotg_hcd calling sleeping disable_irq (0xC00766BC) inside softirq tasklet (0xC0295C7C!),
    # atomic urb_enqueue/dequeue, & timers (deadlocks on dual-core SMP when CPU0 is in uhc_irq and CPU1 runs tasklet!),
    # and silence aotg_uhost_mon_timer (0xC029B940) so transient VBUS dips on RT3070 TX never re-init aotg1 mid-transfer.
    for site in (0xC0294BC4, 0xC02953E0, 0xC0295C7C, 0xC0296C98, 0xC0298D68, 0xC029912C, 0xC029A294):
        assert struct.unpack_from('<I', image, voff(site))[0] == branch(site, 0xC00766BC, link=True)
        struct.pack_into('<I', image, voff(site), branch(site, 0xC00766B8, link=True))
    assert struct.unpack_from('<I', image, voff(0xC029B940))[0] == 0xE92D4070
    struct.pack_into('<I', image, voff(0xC029B940), 0xE12FFF1E)

    # Fix ATM7051H quad-core SMP wakeup: BROM (0xFFFF0224/0xFFFF0280) parks CPU2/CPU3 on 0xB0158080/0xB0158088,
    # whereas kernel cpux_flag_regs (0xC04E5AE4) still had legacy ATM7029 0xB0158058/0xB0158060.
    # Also silence owl_pm_hotplug worker (0xC04DB884 -> bx lr) so all 4 cores stay online without CPU1 shutdown spam.
    assert struct.unpack_from('<3I', image, voff(0xC04E5AE4)) == (0xB0158050, 0xB0158058, 0xB0158060)
    struct.pack_into('<2I', image, voff(0xC04E5AE8), 0xB0158080, 0xB0158088)
    assert struct.unpack_from('<I', image, voff(0xC04DB884))[0] == 0xE3053DAC
    struct.pack_into('<I', image, voff(0xC04DB884), 0xE12FFF1E)

    # Fix owl_pm_power_off (0xC01FC760) full S4 shutdown: always enter deep S4 (not S3) even if USB 5V is present,
    # and wake only on ONOFF_LONG | ONOFF_SHORT | RESET (0x0182), dropping VBUS/WALL auto-wake (0x0703) so all LEDs stay off.
    assert struct.unpack_from('<I', image, voff(0xC01FC764))[0] == 0xE3003103
    assert struct.unpack_from('<I', image, voff(0xC01FC76C))[0] == 0xE3006703
    assert struct.unpack_from('<I', image, voff(0xC01FC798))[0] == 0x13A05000
    struct.pack_into('<I', image, voff(0xC01FC764), 0xE3003182)
    struct.pack_into('<I', image, voff(0xC01FC76C), 0xE3006182)
    struct.pack_into('<I', image, voff(0xC01FC798), 0x13A05001)

    # Unlock full DVFS/OPP range in cpu0_table (0xC070F96C; board_opt=1 adds +25mV) at 100% stock voltages
    # (no static kernel undervolt; dynamic load-based undervolt+OC is handled in userspace by /bin/xgbcpud 'smart' profile):
    assert struct.unpack_from('<10I', image, voff(0xC070F96C)) == (
        900000, 1125000, 876000, 1100000, 768000, 1025000, 600000, 1000000, 240000, 975000
    )
    # 1200 MHz entry uses 1175 mV so board_opt's +25 mV lands exactly on the DCDC1 1200 mV
    # constraint; at 1200 mV the kernel refused the step ("Restricting voltage, 1225000-1200000uV").
    struct.pack_into('<10I', image, voff(0xC070F96C),
                     1200000, 1175000, 900000, 1125000, 600000, 1000000, 360000, 975000, 240000, 975000)

    # Allow runtime HDMI 720p <-> 1080p default_mode switching on hpd_enable toggle (hdmic_hotplug_handle @ 0xC01ECE3C).
    assert struct.unpack_from('<I', image, voff(0xC01ECE3C))[0] == 0x18BD8070
    struct.pack_into('<I', image, voff(0xC01ECE3C), 0xE1A00000)

    # Keep HDMI transmitter at stock pixel_encoding=2 (YCbCr 4:4:4) matching DE_PATH0's hardwired YUV bus (monitor decodes to true sRGB).
    assert struct.unpack_from('<I', image, voff(0xC01ECA64))[0] == 0xE3A02002
    assert struct.unpack_from('<I', image, voff(0xC01EC818))[0] == 0xE3A03002

    # Fix owl_fb_blank (0xC01F38FC): allow FB_BLANK_UNBLANK (0) to re-enable panel & DE path after Xorg ScreenInit blank.
    assert struct.unpack_from('<I', image, voff(0xC01F38FC))[0] == 0x0A000018
    struct.pack_into('<I', image, voff(0xC01F38FC), 0xE1A00000)

    # Prevent de_ats3605 (0xC01E9D50..0xC01E9D98) from re-parenting DE or raising DISPLAYPLL (48MHz -> 480MHz) on HDMI enable/hotplug,
    # which otherwise overclocks 3.5" ST7796S LCD0 10x (12MHz -> 120MHz) and causes refresh beat + ghosting!
    for va, exp in ((0xC01E9D50, 0xEB050F49), (0xC01E9D5C, 0xEB050F46), (0xC01E9D68, 0xEB050F43),
                    (0xC01E9D78, 0xEB050D39), (0xC01E9D88, 0xEB050D35), (0xC01E9D98, 0xEB050D31)):
        assert struct.unpack_from('<I', image, voff(va))[0] == exp, hex(va)
        struct.pack_into('<I', image, voff(va), 0xE3A00000)


def cp15(read, rd, reg):
    crn, crm, op2 = reg
    return (0xEE100F10 if read else 0xEE000F10) | (crn << 16) | (rd << 12) | (op2 << 5) | crm


def launcher(kernel_size, checkpoint=False, copy_only=False, compressed_entry=False):
    arm = Arm(ENTRY)
    emit = arm.emit
    if copy_only:
        emit(0xE92D4010)
    if checkpoint:
        emit(0xE92D5FF0, *mov(4, REPORT), 0xE584D008,
             0xE10F0000, 0xE584000C, 0xEE110F10, 0xE5840010, 0xF10C00C0)
        for index, mode in enumerate(BANKS):
            emit(0xF1020000 | mode, 0xE584D060 + 4*index)
        emit(0xF1020013)
        for index, reg in enumerate(CPU_REGS):
            emit(cp15(True, 0, reg), 0xE5840040 + 4*index)
        emit(0xF57FF04F)
    emit(*mov(0, STAGING), *mov(1, LIMIT))
    arm.label('clean')
    emit(0xEE070F3A, 0xE2800020, 0xE1500001)
    arm.jump('clean', cond=3)
    emit(0xF57FF04F)
    # Retain the running service's DDR bus clock. CMU+0x1c = 2 stalls RAM access.
    if not copy_only:
        emit(0xF10C00C0, 0xF1020013, 0xEE110F10, 0xE3C00005, 0xE3C00A01,
             0xEE010F10, 0xF57FF06F)
        emit(0xE3A00000, 0xEE070F15, 0xEE070FD5, 0xF57FF04F, 0xF57FF06F)
    if not compressed_entry:
        emit(*mov(0, STAGING), *mov(1, KERNEL), *mov(2, kernel_size // 4))
        arm.label('copy')
        emit(0xE4903004, 0xE4813004, 0xE2522001)
        arm.jump('copy', cond=1)
    if copy_only:
        emit(0xF57FF04F, 0xE3A00000, 0xE8BD8010)
        return arm.finish()
    emit(0xF57FF04F, 0xF57FF06F, 0xE3A00000, 0xE3E01000, *mov(2, DTB),
         *mov(3, STAGING if compressed_entry else KERNEL), 0xE12FFF13)
    return arm.finish()


def restore_stub(irq_state=(), capture_vector=False, finish_timer_irq=False, capture_domain=False,
                 return_tail=None, extra_regs=()):
    arm = Arm(RESUME)
    emit = arm.emit
    emit(0xF10C00C0, *mov(4, REPORT), 0xE5849020, 0xE5842024, 0xE10F0000, 0xE5840028)
    if irq_state:
        for index, address in enumerate(IRQ_OBSERVATIONS):
            emit(*mov(0, address), 0xE5901000, 0xE5841080 + 4*index)
    if capture_vector:
        emit(0xE58460A0, 0xE58470A4)
    if capture_domain:
        for index, reg in enumerate(DOMAIN_REGS):
            emit(0xE5840000 | reg << 12 | (0xA0 + 4*index))
    for index, reg in enumerate(extra_regs):
        emit(0xE5840000 | reg << 12 | (0xB4 + 4*index))
    emit(0xE3A00002, 0xE5840004, 0xF57FF04F)
    if finish_timer_irq:
        # GIC IRQ 44 was acknowledged before entering the timer handler.
        emit(*mov(0, 0xB0021304), 0xE5901000, *mov(2, 0x1000), 0xE1110002,
             *mov(0, 0xB0020110), 0xE3A0102C, 0x15801000, 0xF57FF04F)
    for address, value in irq_state:
        emit(*mov(0, address), *mov(1, value), 0xE5801000)
    emit(0xF57FF04F)
    if return_tail is not None:
        return_tail(arm)
        return arm.finish()
    for index, reg in enumerate(CPU_REGS):
        emit(0xE5940040 + 4*index, cp15(False, 0, reg))
    for index, mode in enumerate(BANKS):
        emit(0xF1020000 | mode, 0xE594D060 + 4*index)
    emit(0xF1020013, 0xE3A00000, 0xEE080F17, 0xF57FF04F, 0xF57FF06F,
         0xE5940010, 0xEE010F10, 0xF57FF06F, 0xE594D008, 0xE594000C,
         0xE121F000, 0xE3A00000, 0xE8BD9FF0)
    return arm.finish()


def checked_write(usb, address, data):
    usb.write(address, data)
    assert usb.read(address, len(data)) == data, hex(address)


def gic_restore(usb):
    dist, cpu = 0xB0021000, 0xB0020100
    read = lambda address: struct.unpack('<I', usb.read(address, 4))[0]
    groups = (read(dist+4) & 31) + 1
    assert 2 <= groups <= 8
    ctl = read(dist), read(cpu)
    enables = [read(dist+0x100+4*i) for i in range(groups)]
    # USB0/USB1 are IRQ IDs 67/68 in the stock DTB. Restore priority and target.
    routed = [dist+offset for offset in (0x440, 0x444, 0x840, 0x844, 0xC10)]
    state = [(cpu, 0), (dist, 0)]
    state.extend((a, read(a)) for a in (*routed, cpu+4, cpu+8))
    for i, mask in enumerate(enables):
        state.append((dist+0x180+4*i, 0xFFFFFFFF))
        if mask:
            state.append((dist+0x100+4*i, mask))
    return (*state, (dist, ctl[0]), (cpu, ctl[1]))


def identity_stub(register_setup=()):
    # Copy the high kernel mapping for section 0x004 into its identity slot.
    identity = (
        0xE1A09000, *register_setup, cp15(True, 1, CPU_REGS[0]), 0xE1A01721, 0xE1A01701,
        0xE3811103, 0xE2813A03, 0xE5933010, 0xE5813010, 0xE2813010,
        0xEE073F3A, 0xF57FF04F, 0xE3A03000, 0xEE083F17, 0xF57FF04F,
        0xF57FF06F, 0xE51FF004, IDMAP+4*(17+len(register_setup)),
    )
    physical = (0xEE110F10, 0xE3C00005, 0xEE010F10, 0xF57FF06F, 0xE51FF004, RESUME)
    words = (*identity, *physical)
    return struct.pack('<'+'I'*len(words), *words)


def probe(usb, image, code, late=False, irq=False, unmask=False, handler=False, timer=False,
          dispatch=False, domain=False):
    assert sha256(image) == '81b0afc324d6396aad30fa3a2393dd1298679fa6a579bfbf94c07bf3c932d114'
    checked_write(usb, ENTRY, code)
    restoring = restore_stub(gic_restore(usb) if irq else (), capture_vector=handler,
                             finish_timer_irq=timer or dispatch, capture_domain=domain)
    assert RESUME + len(restoring) <= REPORT
    checked_write(usb, RESUME, restoring)
    at = lambda physical: STAGING + physical - KERNEL
    assert image[CPU_RESET-KERNEL+4:CPU_RESET-KERNEL+8] == bytes.fromhex('0110c1e3')
    if domain:
        checkpoints = (IRQ_ENABLE,)
    elif dispatch:
        checkpoints = (IRQ_DISPATCH,)
    elif timer:
        checkpoints = TIMER_CHECKPOINTS
    elif handler:
        assert image[IRQ_HANDLER-KERNEL:IRQ_HANDLER-KERNEL+4] == bytes.fromhex('44d04de2')
        checkpoints = (IRQ_HANDLER,)
    elif unmask:
        checkpoints = (IRQ_ENABLE, IRQ_ENABLE+4)
    else:
        checkpoints = IRQ_CHECKPOINTS if irq else LATE_CHECKPOINTS if late else CHECKPOINTS
    if late:
        # A late kernel may have unmapped the boot identity section.
        # Copy the high kernel mapping for section 0x004 into its identity slot.
        vector = (*mov(6, 0xFFFF0018), 0xE5966000, cp15(True, 7, CPU_REGS[4])) if handler else ()
        if domain:
            vector = (
                *mov(8, GIC_DOMAIN_POINTER), 0xE5988000,
                *[0xE3A00000 | reg << 12 for reg in DOMAIN_REGS[1:]],
                *mov(1, 0xC0000000), 0xE0481001, *mov(3, LIMIT-20),
                0xE1510003,
                *[0x35980000 | reg << 12 | offset
                  for reg, offset in zip(DOMAIN_REGS[1:], (8, 12, 16, 20))],
            )
        checked_write(usb, at(IDMAP), identity_stub(vector))
    else:
        checked_write(usb, at(CPU_RESET+4), bytes.fromhex('0510c1e3'))
        checked_write(usb, at(IDMAP), struct.pack(
            '<4I', 0xE1A09000, 0xE59F0000, branch(IDMAP+8, CPU_RESET), RESUME,
        ))
    observations = []
    for point in checkpoints:
        report_words = 45 if domain else 42
        checked_write(usb, REPORT, bytes(report_words * 4))
        if unmask and point == IRQ_ENABLE+4:
            assert image[IRQ_ENABLE-KERNEL:IRQ_ENABLE-KERNEL+4] == bytes.fromhex('800008f1')
            checked_write(usb, at(IRQ_ENABLE), struct.pack('<I', 0xE1A00000))
        checked_write(usb, at(point), struct.pack('<II', 0xE51FF004, IDMAP + (0xC0000000 if late else 0)))
        print(f'Starting Linux checkpoint {point:08x}', flush=True)
        usb.execute(ENTRY)
        time.sleep(0.3)
        observation = struct.unpack(f'<{report_words}I', usb.read(REPORT, report_words * 4))
        assert observation[1] == 2, observation
        observations.append({'checkpoint': hex(point), 'r0': hex(observation[8]),
                             'r2': hex(observation[9]), 'masked_control': unmask and point > IRQ_ENABLE})
        if irq:
            observations[-1]['irq'] = {hex(a): hex(v) for a, v in zip(IRQ_OBSERVATIONS, observation[32:])}
        if handler:
            observations[-1].update(vector=hex(observation[40]), vbar=hex(observation[41]))
        if domain:
            observations[-1]['domain'] = dict(zip(DOMAIN_FIELDS, observation[40:45]))
        name = ('irq-domain.json' if domain else 'irq-dispatch.json' if dispatch
                else 'timer-checkpoints.json' if timer else 'irq-handler.json' if handler
                else 'unmask-checkpoints.json' if unmask else 'irq-checkpoints.json' if irq
                else 'late-checkpoints.json' if late else 'checkpoints.json')
        (OUTPUT / name).write_text(
            json.dumps(observations, indent=2))
        print(observations[-1], flush=True)
        checked_write(usb, at(point), image[point-KERNEL:point-KERNEL+8])
        if unmask and point == IRQ_ENABLE+4:
            checked_write(usb, at(IRQ_ENABLE), image[IRQ_ENABLE-KERNEL:IRQ_ENABLE-KERNEL+4])


def main():
    domain = '--domain-probe' in sys.argv
    dispatch = '--dispatch-probe' in sys.argv
    timer = '--timer-probe' in sys.argv
    handler = '--handler-probe' in sys.argv
    unmask = '--unmask-probe' in sys.argv
    irq = '--irq-probe' in sys.argv or unmask or handler or timer or dispatch or domain
    late = '--late-probe' in sys.argv or irq
    checkpoint = '--probe' in sys.argv or late
    copy_only = '--copy-test' in sys.argv
    compressed_entry = '--zimage' in sys.argv
    assert sum((checkpoint, copy_only, compressed_entry)) <= 1
    files = build(compressed_entry, headless='--headless' in sys.argv,
                  memory128='--memory-128' in sys.argv, memory256='--memory-256' in sys.argv,
                  adb='--adb' in sys.argv)
    if '--hw-patch' in sys.argv and 'Image' in files:
        k_addr, k_raw = files['Image']
        k_buf = bytearray(k_raw)
        patch_kernel_hw(k_buf)
        files['Image'] = (k_addr, bytes(k_buf))
    code = launcher(len(next(iter(files.values()))[1]), checkpoint, copy_only, compressed_entry)
    manifest = {n: {'address': a, 'bytes': len(d), 'sha256': sha256(d)}
                for n, (a, d) in files.items()}
    OUTPUT.mkdir(exist_ok=True)
    for name, (_, data) in files.items():
        (OUTPUT / name).write_bytes(data)
    (OUTPUT / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    print(manifest, flush=True)
    if '--boot' not in sys.argv and not checkpoint and not copy_only:
        return
    with Service(exclusive=True) as usb:
        assert usb.read(0xB4069EFC, 4) == struct.pack('<I', branch(0xB4069EFC, 0xB406A440))
        assert usb.read(0xB0150008, 4) == struct.pack('<I', 0x13C)
        for name, (address, data) in files.items():
            for offset in range(0, len(data), 0x4000):
                chunk = data[offset:offset+0x4000]
                checked_write(usb, address+offset, chunk)
            print(f'Verified {name} in DDR', flush=True)
        if checkpoint:
            probe(usb, files['Image'][1], code, late, irq, unmask, handler, timer, dispatch, domain)
            return
        checked_write(usb, ENTRY, code)
        usb.execute(ENTRY)
        if copy_only:
            _, kernel = files['Image']
            for offset in range(0, len(kernel), 0x4000):
                expected = kernel[offset:offset+0x4000]
                assert usb.read(KERNEL+offset, len(expected)) == expected, hex(KERNEL+offset)
            print('Verified whole kernel at physical 0x8000', flush=True)
            return
    print('Linux handoff accepted', flush=True)


if __name__ == '__main__':
    main()
