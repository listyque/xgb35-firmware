#!/bin/sh
set -eu

# Ubuntu 24.04 build container. Put the two input archives in the working directory.
work=$PWD
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
kernel=/opt/linux-3.10.52
ddk=/opt/gfx_linux_ddk-master/eurasia_km
out=/opt/xgb35-sgx-reference-km
cross=arm-linux-gnueabihf-
kcflags='-march=armv7-a -mtune=cortex-a5 -fno-pie'

if [ ! -f "$kernel/Makefile" ]; then
    echo '21f5289a91e069d1620753af40d923d28fd905471f5ac54620cec2a5afe48711  linux-3.10.52.tar.xz' | sha256sum -c
    tar -xJf linux-3.10.52.tar.xz -C /opt
fi
if [ ! -f "$ddk/include4/pvrversion.h" ]; then
    echo 'b2bfbf646b24805ea4787f6b301bffd33464a4ebd1f0652ba864fd3af512217a  lge-sgx-ddk.zip' | sha256sum -c
    unzip -q lge-sgx-ddk.zip 'gfx_linux_ddk-master/eurasia_km/*' -d /opt
fi

# Refuse a partial or differently patched source tree.
patch_file="$source_dir/ddk18-ats3605.patch"
if patch -d "$ddk" -p1 -R --dry-run --batch --forward --fuzz=0 < "$patch_file" >/dev/null 2>&1; then
    :
else
    patch -d "$ddk" -p1 --dry-run --batch --forward --fuzz=0 < "$patch_file" >/dev/null
    patch -d "$ddk" -p1 --batch --forward --fuzz=0 < "$patch_file"
fi

# The 3.10 kernel selects a compiler header by GCC major version.
ln -sf compiler-gcc4.h "$kernel/include/linux/compiler-gcc$(${cross}gcc -dumpversion).h"
if [ ! -f "$kernel/.config" ]; then
    make -C "$kernel" ARCH=arm CROSS_COMPILE="$cross" vexpress_defconfig
    "$kernel/scripts/config" --file "$kernel/.config" \
        -e MODULES -e MODULE_UNLOAD -d MODVERSIONS -d LOCALVERSION_AUTO \
        -e SMP -d PREEMPT_VOLUNTARY -e PREEMPT -e PM_RUNTIME -e PM_OPP -e REGULATOR
fi
make -C "$kernel" ARCH=arm CROSS_COMPILE="$cross" HOSTCFLAGS=-fcommon \
    KCFLAGS="$kcflags" -j2 olddefconfig modules_prepare

mkdir -p "$out/target/kbuild"
cp "$ddk/eurasiacon/build/linux2/kbuild/Makefile.template" "$out/target/kbuild/Makefile"
cat > "$out/config_kernel.mk" <<'CONF'
SGXCORE := 540
SGX_CORE_REV := 130
PVR_SYSTEM := omap4
CONF
cat > "$out/config_kernel.h" <<'CONF'
#define LINUX
#define RELEASE
#define PVR_BUILD_DIR "xgb35_linux"
#define PVR_BUILD_TYPE "release"
#define PVRSRV_MODNAME "pvrsrvkm"
#define SGXCORE 540
#define SGX540
#define SGX_CORE_REV 130
#define SUPPORT_SGX
#define SUPPORT_SGX540
#define SGX_FEATURE_SYSTEM_CACHE
#define TRANSFER_QUEUE
#define PVR_SECURE_HANDLES
#define SUPPORT_PERCONTEXT_PB
#define SUPPORT_HW_RECOVERY
#define SUPPORT_ACTIVE_POWER_MANAGEMENT
#define SUPPORT_SGX_HWPERF
#define SUPPORT_SGX_LOW_LATENCY_SCHEDULING
#define SUPPORT_MEMINFO_IDS
#define SUPPORT_SGX_NEW_STATUS_VALS
#define SUPPORT_DBGDRV_EVENT_OBJECTS
#define SGX_DYNAMIC_TIMING_INFO
#define SYS_USING_INTERRUPTS
#define PVR_ATS3605
#define PVRSRV_NEED_PVR_DPF
#define PVRSRV_NEW_PVR_DPF
#define PVR_LINUX_USING_WORKQUEUES
#define PVR_LINUX_MISR_USING_PRIVATE_WORKQUEUE
#define PVR_LINUX_TIMERS_USING_WORKQUEUES
#define PVR_NO_OMAP_TIMER
#define PVR_NO_FULL_CACHE_OPS
#define LDM_PLATFORM
#define PVR_LDM_PLATFORM_PRE_REGISTERED
#define PVR_LDM_PLATFORM_PRE_REGISTERED_DEV "b0350000.gpu"
#define PVR_LDM_DRIVER_REGISTRATION_NAME "pvrsrvkm"
CONF

make -C "$kernel" ARCH=arm CROSS_COMPILE="$cross" HOSTCFLAGS=-fcommon -j2 W=1 \
    KCFLAGS="$kcflags" M="$out/target/kbuild" TOP="$ddk" OUT="$out" \
    INTERNAL_KBUILD_MAKEFILES="$ddk/services4/srvkm/env/linux/Kbuild.mk" \
    INTERNAL_KBUILD_OBJECTS=pvrsrvkm_sgx540_130.o modules

module="$out/target/kbuild/pvrsrvkm_sgx540_130.ko"
${cross}readelf -hSW -p .modinfo "$module" > "$work/ddk18-km-elf.txt"
${cross}nm -u "$module" > "$work/ddk18-km-imports.txt"
cp "$module" "$work/pvrsrvkm_sgx540_130-reference.ko"
