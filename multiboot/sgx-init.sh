#!/bin/busybox sh
# Initialize the XGB35 vendor PowerVR Services from its stock SquashFS.
set -eu

fail() {
    printf 'SGX: %s\n' "$*" >&2
    exit 1
}

[ "$#" -le 1 ] || fail 'usage: sgx-init [stock-system.img]'
image=${1:-/mnt/sd/linux/stock-system.img}
[ "$(id -u)" = 0 ] || fail 'run from the CFW root shell'
[ "$(uname -r)" = 3.10.52 ] || fail 'requires the XGB35 vendor kernel 3.10.52'

gpu=/proc/device-tree/gpu@b0350000
[ -r "$gpu/compatible" ] && tr '\000' '\n' < "$gpu/compatible" | grep -q '^actions,ats3605-gpu$' ||
    fail 'Actions ATM7051H GPU node is missing'
if [ -r "$gpu/status" ] && [ "$(tr -d '\000' < "$gpu/status")" != okay ]; then
    fail 'GPU is disabled in the running device tree'
fi
[ -r "$image" ] || fail "stock SYSTEM image unavailable at $image"
[ -r /proc/modules ] || fail '/proc/modules is unavailable'
if grep -q '^pvrsrvkm ' /proc/modules; then
    fail 'pvrsrvkm is already loaded. Use a clean CFW boot for this test'
fi

root=$(mktemp -d /tmp/xgb35-sgx.XXXXXX)
loaded=0
cleanup() {
    result=$?
    if [ "$result" -ne 0 ] && [ "$loaded" = 1 ]; then
        rmmod pvrsrvkm || printf 'SGX: could not unload pvrsrvkm\n' >&2
    fi
    for path in "$root/proc" "$root/dev" "$root"; do
        if mountpoint -q "$path"; then
            umount "$path" || result=1
        fi
    done
    rmdir "$root" || result=1
    exit "$result"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

mount -t squashfs -o loop,ro "$image" "$root"
for file in lib/modules/3.10.0/pvrsrvkm.ko usr/bin/pvrsrvctl lib/ld-uClibc.so.0 \
    usr/lib/libsrv_init.so usr/lib/libsrv_um.so; do
    [ -r "$root/$file" ] || fail "missing $file in stock SYSTEM"
done
mount --bind /dev "$root/dev"
mount --bind /proc "$root/proc"

insmod "$root/lib/modules/3.10.0/pvrsrvkm.ko" || fail 'insmod failed. See dmesg for the kernel error'
loaded=1
[ -c /dev/pvrsrvkm ] || fail 'module loaded but /dev/pvrsrvkm is missing. See dmesg'
if ! LD_LIBRARY_PATH=/lib:/usr/lib chroot "$root" /usr/bin/pvrsrvctl --start --no-module; then
    fail 'PowerVR Services initialization failed. See dmesg'
fi

printf 'SGX: PowerVR Services initialized. OpenGL ES rendering still requires a matching EGL/GLES driver.\n'
