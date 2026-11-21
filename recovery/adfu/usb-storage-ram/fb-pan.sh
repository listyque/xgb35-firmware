P=/sys/class/graphics/fb0/pan
echo 0,480 > $P; echo 0,0 > $P
cat $P
for o in 50 54 58 60 64 68 70 74 78 80 84 88 90 94 98 a0 a4 a8 b0 b4 b8 c0 c4 c8 d0 d4 d8 e0 e4 e8 f0 f4 f8; do echo -n "$o:$(busybox devmem 0xb02f02$o) "; done; echo
for o in 00 04 08 0c 10 14 18 1c 20; do echo -n "$o:$(busybox devmem 0xb02f00$o) "; done; echo
for o in 00 04 08 0c 10 14 18 1c 20 24 28 2c 30 34 38 3c 40 44 48 4c; do echo -n "$o:$(busybox devmem 0xb02f01$o) "; done; echo
