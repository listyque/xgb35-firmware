#!/bin/busybox sh
# hub-mode.sh — Manage external USB Hub driver on XGB35 (Actions ATM7051H)
# Physical ports:
#   aotg1 (0xB0250000, port_mask=2) = Full-size USB-A Host port (main hub port)
#   aotg0 (0xB0240000, port_mask=1) = Mini-USB port (charging / PC WinUSB ADB / optional 2nd OTG Host)
#
# Usage:
#   sh hub-mode.sh             # Default (usb-a-fs): 12M Full-Speed Hub on Big USB-A (Mouse+Kbd+Flash+Wi-Fi, Mini-USB stays ADB!)
#   sh hub-mode.sh usb-a-hs    # 480M High-Speed Hub on Big USB-A (Flash+Wi-Fi at full 480M; Mini-USB stays ADB)
#   sh hub-mode.sh dual-hs-fs  # 480M HS Hub on Big USB-A + 12M FS Host on Mini-USB (via Mini-USB OTG adapter)
#   sh hub-mode.sh status      # Show USB bus tree, input devices, flash drives, and wlan0
#   sh hub-mode.sh off         # Unload aotg_hub_hcd.ko and restore built-in driver

DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
KO="$DIR/aotg_hub_hcd.ko"
[ -f "$KO" ] || KO="/lib/aotg_hub_hcd.ko"

ensure_aotg1_host() {
  if [ ! -d /sys/bus/platform/devices/aotg_hcd.1 ] && [ -w /proc/acts_hcd ]; then
    echo c > /proc/acts_hcd
    sleep 1
  fi
}

case "${1:-usb-a-fs}" in
  usb-a-fs|single-fs)
    ensure_aotg1_host
    rmmod aotg_hub_hcd 2>/dev/null
    insmod "$KO" port_mask=2 force_full_speed=1
    ;;
  usb-a-hs|single-hs)
    ensure_aotg1_host
    rmmod aotg_hub_hcd 2>/dev/null
    insmod "$KO" port_mask=2 force_full_speed=0
    ;;
  dual-hs-fs)
    ensure_aotg1_host
    if [ -x /bin/usb-mode ]; then
      /bin/usb-mode host
    elif [ -w /proc/acts_udc ] && [ -w /proc/acts_hcd ]; then
      echo b > /proc/acts_udc
      sleep 1
      echo a > /proc/acts_hcd
      sleep 1
    fi
    rmmod aotg_hub_hcd 2>/dev/null
    insmod "$KO" port_mask=3 force_full_speed=2
    ;;
  status)
    echo "=== USB Devices ==="
    for d in /sys/bus/usb/devices/*; do
      [ -f "$d/idVendor" ] || continue
      printf "%s: %s:%s speed=%sM devnum=%s product=%s\n" \
        "$(basename "$d")" \
        "$(cat "$d/idVendor" 2>/dev/null)" \
        "$(cat "$d/idProduct" 2>/dev/null)" \
        "$(cat "$d/speed" 2>/dev/null)" \
        "$(cat "$d/devnum" 2>/dev/null)" \
        "$(cat "$d/product" 2>/dev/null)"
    done
    echo "=== Input (Mouse/Kbd) ==="
    for e in /sys/class/input/event*; do
      [ -f "$e/device/name" ] && printf "%s: %s\n" "$(basename "$e")" "$(cat "$e/device/name")"
    done
    echo "=== Storage (sd*) ==="
    grep -E 'sd[a-z]' /proc/partitions 2>/dev/null || echo "(no external USB flash drive)"
    echo "=== Wi-Fi (wlan0) ==="
    ip -o link show wlan0 2>/dev/null || ifconfig -a wlan0 2>/dev/null || echo "(no wlan0 yet)"
    ;;
  off)
    rmmod aotg_hub_hcd
    ;;
  *)
    echo "Usage: $0 {usb-a-fs|usb-a-hs|dual-hs-fs|status|off}"
    exit 1
    ;;
esac
