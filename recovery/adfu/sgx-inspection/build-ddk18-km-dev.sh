#!/bin/sh
set -eu

kernel=/opt/linux-3.10.52
ddk=/work/ddk18/eurasia_km
out=/opt/xgb35-sgx-km
cross=arm-linux-gnueabihf-

if ! command -v gcc >/dev/null || [ ! -f /usr/include/stdlib.h ]; then
    apt-get install -y -qq --no-install-recommends gcc libc6-dev patch
fi

if [ ! -f "$kernel/Makefile" ]; then
    cd /work
    echo '21f5289a91e069d1620753af40d923d28fd905471f5ac54620cec2a5afe48711  linux-3.10.52.tar.xz' | sha256sum -c
    tar -xJf linux-3.10.52.tar.xz -C /opt
fi

# This kernel checks GCC's major version in the header filename.
ln -sf compiler-gcc4.h "$kernel/include/linux/compiler-gcc$(${cross}gcc -dumpversion).h"
if [ ! -f "$kernel/.config" ]; then
    make -C "$kernel" ARCH=arm CROSS_COMPILE="$cross" vexpress_defconfig
    "$kernel/scripts/config" --file "$kernel/.config" \
        -e MODULES -e MODULE_UNLOAD -d MODVERSIONS -d LOCALVERSION_AUTO \
        -e SMP -d PREEMPT_VOLUNTARY -e PREEMPT -e PM_RUNTIME -e PM_OPP -e REGULATOR
fi
make -C "$kernel" ARCH=arm CROSS_COMPILE="$cross" HOSTCFLAGS=-fcommon \
    KCFLAGS='-march=armv7-a -mtune=cortex-a5 -fno-pie' -j2 olddefconfig modules_prepare

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
    KCFLAGS='-march=armv7-a -mtune=cortex-a5 -fno-pie' \
    M="$out/target/kbuild" TOP="$ddk" OUT="$out" \
    INTERNAL_KBUILD_MAKEFILES="$ddk/services4/srvkm/env/linux/Kbuild.mk" \
    INTERNAL_KBUILD_OBJECTS=pvrsrvkm_sgx540_130.o modules

module="$out/target/kbuild/pvrsrvkm_sgx540_130.ko"
${cross}readelf -hSW -p .modinfo "$module" > /work/ddk18-km-elf.txt
${cross}nm -u "$module" > /work/ddk18-km-imports.txt
cp "$module" /work/pvrsrvkm_sgx540_130-reference.ko
