#!/bin/sh
set -eu
# Ubuntu ARM hard-float cross build. Run from the directory containing the input archives.
work=$PWD
sources=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tree=$work/ddk18/eurasia
out=$work/ddk18/um-musl
sys=/tmp/xgb35-musl-sysroot
cc=arm-linux-gnueabihf-gcc
mkdir -p "$sys/lib" "$out/target"
echo '270df32eeab9f324af29caf317208ea69b446993562b2ba2f170859b2c9d1286  musl-dev-1.2.5-r4.apk' | sha256sum -c
tar --ignore-zeros -xzf musl-dev-1.2.5-r4.apk -C "$sys" usr
echo 'a5986d1ff99b6967c7697a108711d3a60002ee9edd2f908ddba2c28ca349abf8  libc.musl-armv7.so.1' | sha256sum -c
echo '8eba8b6ce49086b8d08f759e2bcfcc4953708079cb3bf443a8f445bc0e40eb8e  libgcc_s-alpine.so.1' | sha256sum -c
cp libc.musl-armv7.so.1 "$sys/lib/ld-musl-armhf.so.1"
cp libgcc_s-alpine.so.1 "$sys/usr/lib/libgcc_s.so.1"
for dir in linux asm asm-generic; do
    ln -sfn "/usr/arm-linux-gnueabihf/include/$dir" "$sys/usr/include/$dir"
done
inc="-nostdinc -isystem $sys/usr/include -isystem $($cc -print-file-name=include)"
libs="-nostdlib -L$sys/usr/lib -Wl,--no-as-needed -lc -l:libgcc_s.so.1"
if [ ! -f "$tree/eurasiacon/build/linux2/toplevel.mk" ]; then
    echo 'b2bfbf646b24805ea4787f6b301bffd33464a4ebd1f0652ba864fd3af512217a  lge-sgx-ddk.zip' | sha256sum -c
    mkdir -p "$work/ddk18"
    unzip -q lge-sgx-ddk.zip 'gfx_linux_ddk-master/eurasia/*' -d "$work/ddk18"
    mv "$work/ddk18/gfx_linux_ddk-master/eurasia" "$tree"
fi
cd "$tree"
if ! patch -p1 --dry-run -R --batch --forward --fuzz=0 < "$sources/ddk18-musl.patch" >/dev/null 2>&1; then
    patch -p1 --dry-run --batch --forward --fuzz=0 < "$sources/ddk18-musl.patch" >/dev/null
    patch -p1 --batch --forward --fuzz=0 < "$sources/ddk18-musl.patch"
fi
base=eurasiacon/binary2_540_omap4430_android_release
sed -E '/^#define (ANDROID |PVR_ANDROID_|SUPPORT_ANDROID_|SUPPORT_GRAPHICS_HAL|SUPPORT_OPENCL|SUPPORT_OPENGLES1)/d;
    s/^#define SGX_CORE_REV 120$/#define SGX_CORE_REV 130/;
    s/_SGX540_120/_SGX540_130/g;
    s/^#define OGLES2_BASENAME .*/#define OGLES2_BASENAME "GLESv2"/;
    s#"/system/lib/egl/"#""#g;
    s#"/system/modules/"#"/lib/modules/"#g;
    s/"libpvrANDROID_WSEGL_SGX540_130.so"/"libpvrWSEGL.so"/;
    s/"omap4430_android"/"xgb35_linux"/' "$base/config.h" > "$out/config.h"
printf '%s\n' '#define GLSL_COMPILER_BASENAME "glslcompiler_SGX540_130"' >> "$out/config.h"
printf '%s\n' '#define PVRSRV_NEED_PVR_DPF' >> "$out/config.h"
printf '%s\n' '#define SGX_FEATURE_SYSTEM_CACHE' >> "$out/config.h"

cat > "$out/config.mk" <<CONF
override COMPONENTS := egl opengles2 glslcompiler pvrsrvctl
override HOST_CC := gcc -m32 -idirafter /usr/include/x86_64-linux-gnu
override HOST_CXX := g++ -m32 -idirafter /usr/include/x86_64-linux-gnu
override SYS_CFLAGS := -mcpu=cortex-a5 -marm -mfpu=neon -mfloat-abi=hard -fno-short-enums -fcommon -Wno-error=implicit-function-declaration
override SYS_CXXFLAGS := \$(SYS_CFLAGS)
override SYS_LIB_LDFLAGS := -ldl
override LIBGCC := $(arm-linux-gnueabihf-gcc -print-libgcc-file-name)
override SGXCORE := 540
override SUPPORT_SGX := 1
override SGX_CORE_REV := 130
override PVR_SYSTEM := omap4
override BUILD := release
override OPTIM := -O2
override LDM_PLATFORM := 1
CONF
: > "$out/config_kernel.h"
: > "$out/config_kernel.mk"
find "$PWD" -type f -name Linux.mk ! -path '*/android/*' > "$out/makefiles.txt"

cat >> "$out/config.mk" <<EOF
override SYS_CFLAGS += -D_GNU_SOURCE $inc
override SYS_CXXFLAGS := \$(SYS_CFLAGS)
override SYS_EXE_LDFLAGS := -no-pie $libs -Wl,--dynamic-linker,/lib/ld-musl-armhf.so.1
override SYS_EXE_CRTBEGIN := $sys/usr/lib/crt1.o $sys/usr/lib/crti.o
override SYS_EXE_CRTEND := $sys/usr/lib/crtn.o
override SYS_LIB_LDFLAGS := $libs
EOF
ALL_MAKEFILES="$out/makefiles.txt" make -f eurasiacon/build/linux2/toplevel.mk -j4 \
    TOP="$tree" OUT="$out" CROSS_COMPILE=arm-linux-gnueabihf- CC=gcc CXX=g++ W=1 components
cd "$out/target"
$cc -mcpu=cortex-a5 -mfpu=neon -DLINUX -Wall -Werror $inc \
    -I"$tree/eurasiacon/include" -fPIC -shared -o libpvrWSEGL.so "$sources/wsegl-pbuffer.c" $libs
$cc -mcpu=cortex-a5 -mfpu=neon -fno-short-enums -D_GNU_SOURCE -Wall -Werror $inc \
    -include "$out/config.h" -I"$tree/eurasiacon/include" -I"$tree/include4" \
    -I"$tree/services4/include" -I"$tree/hwdefs" -fPIC -shared \
    -o libpvrWSEGL_fbdev.so "$sources/wsegl-fbdev.c" $libs -L. -l:libsrv_um_SGX540_130.so
for f in pvrsrvctl lib*.so; do
    if arm-linux-gnueabihf-readelf -dV "$f" | grep -Eq 'GLIBC_|libc\.so\.6'; then
        echo "$f references glibc" >&2; exit 1
    fi
done
arm-linux-gnueabihf-readelf -l pvrsrvctl | grep '/lib/ld-musl-armhf.so.1'
qemu-arm -L "$sys" -E LD_LIBRARY_PATH="$PWD" ./pvrsrvctl --help
