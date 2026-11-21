cd /sys/kernel/debug/de
for d in path0 path1 video0 video1; do for f in $d/*; do echo "$f=$(cat $f 2>&1)"; done; done
echo "--- DE regs"
for o in 0 4 8 c 10 14 18 1c 20 24 28 2c 30 34 38 3c 40 44 48 4c; do echo -n "$o:$(busybox devmem 0xb02f00$o) "; done; echo
echo "--- DE path0 / video0 (0x1000/0x400 strides guessed)"
for b in 0xb02f0100 0xb02f0200 0xb02f0400 0xb02f1000 0xb02f2000; do echo -n "$b: "; for o in 0 4 8 c 10 14 18 1c; do echo -n "$(busybox devmem $((b + 0x$o))) "; done; echo; done
echo "--- LCDC"
for o in 0 4 8 c 10 14 18 1c 20 24 28 2c 30 34 38 3c 40 44 48 4c 50 54 58 5c; do echo -n "$o:$(busybox devmem 0xb02a00$o) "; done; echo
echo "--- MFP / pad (0xb01c0040..)"
for o in 40 44 48 4c 50 54 58 5c 60 64 68; do echo -n "$o:$(busybox devmem 0xb01c00$o) "; done; echo
echo "--- CMU LCD/DE clocks"
for o in 00 04 08 0c 10 14 18 1c 20 24 28 2c 30 34 38 3c 40 44 48 4c 50 54 58 5c 60 64 68 6c 70 74 78 7c 80 84 88 8c 90 94 98 9c a0 a4 a8 ac; do echo -n "$o:$(busybox devmem 0xb01500$o) "; done; echo
