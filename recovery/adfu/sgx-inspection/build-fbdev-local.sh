set -eu
cc=arm-linux-gnueabihf-gcc
mkdir -p /tmp/sys
tar --ignore-zeros -xzf /w/musl-dev-1.2.5-r4.apk -C /tmp/sys usr
cc_inc=$($cc -print-file-name=include)
for d in linux asm asm-generic; do ln -s "/usr/arm-linux-gnueabihf/include/$d" "/tmp/sys/usr/include/$d"; done
$cc -D_GNU_SOURCE -O2 -mcpu=cortex-a5 -mfpu=neon -fPIC -fno-short-enums -Wall -Werror -nostdinc -isystem /tmp/sys/usr/include -isystem "$cc_inc" -include /w/ddk18/um-musl/config.h -I/w/ddk18/eurasia/eurasiacon/include -I/w/ddk18/eurasia/include4 -I/w/ddk18/eurasia/services4/include -I/w/ddk18/eurasia/hwdefs -shared -nostdlib /src/wsegl-fbdev.c -L/w/ddk18/um-musl/target -l:libsrv_um_SGX540_130.so -o /w/libpvrWSEGL_fbdev.so
mkdir -p /tmp/sys/lib
cp /w/libc.musl-armv7.so.1 /tmp/sys/lib/ld-musl-armhf.so.1
cp /w/libgcc_s-alpine.so.1 /tmp/sys/usr/lib/libgcc_s.so.1
$cc -D_GNU_SOURCE -DLINUX -O2 -mcpu=cortex-a5 -mfpu=neon -nostdinc -isystem /tmp/sys/usr/include -isystem "$cc_inc" -I/w/ddk18/eurasia/eurasiacon/include -I/glheaders -nostdlib -no-pie -Wl,--allow-shlib-undefined -Wl,--dynamic-linker,/lib/ld-musl-armhf.so.1 /tmp/sys/usr/lib/crt1.o /tmp/sys/usr/lib/crti.o /src/gles2-fbdev-probe.c -L/w/ddk18/um-musl/target -l:libEGL.so -l:libGLESv2.so -L/tmp/sys/usr/lib -lc -l:libgcc_s.so.1 /tmp/sys/usr/lib/crtn.o -o /w/gles2-fbdev-probe
