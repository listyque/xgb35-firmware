#!/bin/sh
set -eu

if ! command -v gawk >/dev/null || [ ! -f /usr/include/stdlib.h ]; then
    apt-get install -y -qq --no-install-recommends gawk libc6-dev
fi
if [ ! -f /usr/include/gnu/stubs-32.h ]; then
    apt-get install -y -qq --no-install-recommends libc6-dev-i386 lib32gcc-13-dev
fi
if ! command -v qemu-arm >/dev/null; then
    apt-get install -y -qq --no-install-recommends qemu-user
fi
cd /work/ddk18/eurasia
base=eurasiacon/binary2_540_omap4430_android_release
out=/work/ddk18/um-cortex-a5
mkdir -p "$out"

# Modern Bison passes *yylsp as the current location.
sed -i 's/Current\./(Current)./g' tools/intern/useasm/cparser.y
tokens=tools/intern/oglcompiler/parser/tokens.h
if ! grep -q '^typedef struct ParseContextTAG' "$tokens"; then
    sed -i '/^#include "glsl_parser.tab.h"/i typedef struct ParseContextTAG ParseContext;\ntypedef struct GLSLTreeContextTAG GLSLTreeContext;' "$tokens"
fi
ctl=services4/srvinit/env/linux/pvrsrvctl.c
if grep -q 'if(!DetectPlatform(&props))' "$ctl"; then
    sed -i '/if(!DetectPlatform(\&props))/,/^[[:blank:]]*}/d' "$ctl"
    sed -i '/if(iModule \&\& (iMode == RESTART || iMode == STOP))/i\	if(iModule \&\& !DetectPlatform(\&props)) {\n\t\tDPF("Could not detect platform\\n");\n\t\tgoto err_out;\n\t}' "$ctl"
    sed -i '/^err_out:$/ { N; s/\n\tfclose(pFile);/\n\tif (pFile) fclose(pFile);/; }' "$ctl"
fi
apphint=services4/srvclient/env/linux/common/pvr_apphint.c
if ! grep -q 'Android board-specific hints' "$apphint"; then
    sed -i '/^#include <cutils\/properties.h>/i #if defined(ANDROID) /* Android board-specific hints */' "$apphint"
    sed -i '/^#include <cutils\/properties.h>/a #endif' "$apphint"
    sed -i '/^static const char \*variant_keys\[/i #if defined(ANDROID)' "$apphint"
    sed -i '/sizeof(variant_keys\[0\])/a #endif' "$apphint"
    sed -i '/^static IMG_BOOL FindAppHintVariant(/i #if defined(ANDROID)' "$apphint"
    sed -i '/^static IMG_BOOL FindAppHintVariant(/,/^}/ { /^}/a #endif
    }' "$apphint"
    sed -i '/if (FindAppHintVariant(/i #if defined(ANDROID)' "$apphint"
    sed -i '/if (FindAppHintVariant(/,/^[[:blank:]]*}/ { /^[[:blank:]]*}/a #endif
    }' "$apphint"
fi

sed -E '/^#define (ANDROID |PVR_ANDROID_|SUPPORT_ANDROID_|SUPPORT_GRAPHICS_HAL|SUPPORT_OPENCL|SUPPORT_OPENGLES1)/d;
    s/^#define SGX_CORE_REV 120$/#define SGX_CORE_REV 130/;
    s/_SGX540_120/_SGX540_130/g;
    s/^#define OGLES2_BASENAME .*/#define OGLES2_BASENAME "GLESv2"/;
    s#"/system/lib/egl/"#""#g;
    s#"/system/modules/"#"/lib/modules/"#g;
    s/"libpvrANDROID_WSEGL_SGX540_130.so"/"libpvrWSEGL.so"/;
    s/"omap4430_android"/"xgb35_linux"/' "$base/config.h" > "$out/config.h.new"
printf '%s\n' '#define GLSL_COMPILER_BASENAME "glslcompiler_SGX540_130"' >> "$out/config.h.new"
printf '%s\n' '#define PVRSRV_NEED_PVR_DPF' >> "$out/config.h.new"
printf '%s\n' '#define SGX_FEATURE_SYSTEM_CACHE' >> "$out/config.h.new"

cat > "$out/config.mk.new" <<CONF
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
: > "$out/config_kernel.h.new"
: > "$out/config_kernel.mk.new"
find "$PWD" -type f -name Linux.mk ! -path '*/android/*' > "$out/makefiles.txt"

ALL_MAKEFILES="$out/makefiles.txt" make -f eurasiacon/build/linux2/toplevel.mk -j2 \
    TOP="$PWD" OUT="$out" CROSS_COMPILE=arm-linux-gnueabihf- \
    CC=gcc CXX=g++ W=1 components

cd "$out/target"
for binary in pvrsrvctl lib*.so; do
    printf '\n%s\n' "$binary"
    arm-linux-gnueabihf-readelf -h "$binary" | grep -q 'hard-float ABI'
    if arm-linux-gnueabihf-readelf -A "$binary" | grep -q 'VFPv4'; then
        exit 1
    fi
    arm-linux-gnueabihf-readelf -h -A -d -l "$binary"
done > /work/ddk18-armv7-elf.txt

arm-linux-gnueabihf-gcc -mcpu=cortex-a5 -mfpu=neon -x c -o "$out/verify-dlopen" - -ldl <<'C'
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    void *library = argc == 3 ? dlopen(argv[1], RTLD_NOW) : NULL;
    if (library == NULL || dlsym(library, argv[2]) == NULL) {
        const char *error = dlerror();
        fprintf(stderr, "%s\n", error ? error : "expected library and symbol");
        return 1;
    }
    printf("%s exports %s\n", argv[1], argv[2]);
    return dlclose(library) != 0;
}
C
export LD_LIBRARY_PATH="$PWD"
qemu-arm -cpu cortex-a9 -L /usr/arm-linux-gnueabihf ./pvrsrvctl --help
qemu-arm -cpu cortex-a9 -L /usr/arm-linux-gnueabihf "$out/verify-dlopen" ./libEGL.so eglGetDisplay
qemu-arm -cpu cortex-a9 -L /usr/arm-linux-gnueabihf "$out/verify-dlopen" ./libGLESv2.so glClear
qemu-arm -cpu cortex-a9 -L /usr/arm-linux-gnueabihf "$out/verify-dlopen" ./libglslcompiler_SGX540_130.so GLSLCompileToUniflex

ddk=/work/ddk18/eurasia/eurasiacon
probe=/work/reference-repro
mkdir -p "$out/include/KHR"
cp "$ddk/include/drvkhrplatform.h" "$out/include/KHR/khrplatform.h"
arm-linux-gnueabihf-gcc -mcpu=cortex-a5 -mfpu=neon -DLINUX -Wall -Werror \
    -I"$ddk/include" -fPIC -shared -o libpvrWSEGL.so "$probe/wsegl-pbuffer.c"
arm-linux-gnueabihf-gcc -mcpu=cortex-a5 -mfpu=neon -DLINUX -Wall -Werror \
    -I"$ddk/include" -DWSEGL_SELF_TEST -o "$out/wsegl-self-test" "$probe/wsegl-pbuffer.c"
qemu-arm -cpu cortex-a9 -L /usr/arm-linux-gnueabihf "$out/wsegl-self-test"
echo 'WSEGL pbuffer configuration self-test passed'

arm-linux-gnueabihf-gcc -mcpu=cortex-a5 -mfpu=neon -DLINUX -Wall -Werror \
    -I"$ddk/include" -I"$ddk/unittests/include" -I"$out/include" \
    -o gles2-probe "$probe/gles2-probe.c" -L. -Wl,-rpath-link,. -lEGL -lGLESv2
exit_code=0
qemu-arm -cpu cortex-a9 -L /usr/arm-linux-gnueabihf ./gles2-probe > /work/gles2-no-gpu.log 2>&1 || exit_code=$?
test "$exit_code" -eq 1
grep 'eglInitialize failed with EGL error' /work/gles2-no-gpu.log
