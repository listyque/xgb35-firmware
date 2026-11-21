busybox fbset -g 320 480 320 1440 16 -a 2>&1
echo 0 > /sys/class/graphics/fb0/blank
cat /tmp/p.raw > /dev/fb0
cat /tmp/p.raw /tmp/p.raw >> /dev/fb0 2>/dev/null
echo 0,0 > /sys/class/graphics/fb0/pan 2>&1
cd /sys/kernel/debug/de
for f in video0/* video1/*; do [ "${f##*/}" = apply ] || echo -n "$f=$(cat $f) "; done; echo
for o in 00 04 08 0c 10 14 18 1c 20; do echo -n "$o:$(busybox devmem 0xb02f00$o) "; done; echo
dmesg | tail -15
