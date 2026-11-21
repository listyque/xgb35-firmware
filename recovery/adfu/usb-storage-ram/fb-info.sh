dmesg | grep -iE 'fb|framebuffer|ion|reserve|cma|carveout' | head -40
echo "--- iomem"; cat /proc/iomem | head -40
echo "--- ion"; ls /sys/kernel/debug/ion; for f in /sys/kernel/debug/ion/*; do echo "== $f"; head -c 1500 $f; done
echo "--- video0 files"; ls -l /sys/kernel/debug/de/video0
echo "--- LCDC"; for o in 00 04 08 0c 10 14 18 1c 20 24; do echo -n "$o:$(busybox devmem 0xb02a00$o) "; done; echo
echo "--- DE"; for o in 00 04 08 0c 10 14 18 1c 20; do echo -n "$o:$(busybox devmem 0xb02f00$o) "; done; echo
cat /proc/cmdline; busybox fbset 2>&1 | head
