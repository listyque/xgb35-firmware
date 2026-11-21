#!/bin/sh
DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)" || exit 1
export DISPLAY="${DISPLAY:-:0}" OG_MODE=x11 OG_FULLSCREEN=1
export XDG_CONFIG_HOME="$DIR/config" XDG_DATA_HOME="$DIR/data"
export LD_LIBRARY_PATH="$DIR/lib:/mnt/sd/ports/Jellyfin_fpl/lib:/usr/lib:/lib"
unset XGB35_SGX
dialog="$DIR/bin/xmessage"
debug_lib="$DIR/lib/frame-debug/libpvrWSEGL_fbdev.so"
case "${1:-}" in
    --software|--sgx|--sgx-direct|--sgx-debug) mode=$1; explicit=1; shift ;;
    *)
        buttons='Software (CPU):10,Cancel:12'
        if [ -x /usr/local/bin/sgx-run ] && [ -f "$DIR/lib/opengorky-sgx.so" ]; then
            buttons='Software (CPU):10,PowerVR SGX540 (Direct):11,Cancel:12'
            if [ -r "$debug_lib" ]; then
                buttons='Software (CPU):10,PowerVR SGX540 (Direct):11,SGX Debug:13,Cancel:12'
            fi
        fi
        "$dialog" -fn 10x20 -center -title 'OpenGorky' -buttons "$buttons" -default 'Software (CPU)' 'Gorky 18 - choose renderer'
        case $? in 10) mode=--software ;; 11) mode=--sgx-direct ;; 13) mode=--sgx-debug ;; *) exit 0 ;; esac ;;
esac
set -- "$DIR/bin/opengorky" "$DIR/game" --play --hires 1 --upscale off "$@"
if [ "$mode" = --sgx-direct ] || [ "$mode" = --sgx-debug ]; then
    [ -r "$DIR/lib/opengorky-sgx.so" ] || exit 1
    /usr/local/bin/sgx-run --init 2>/tmp/opengorky_sgx_init.log || exit 1
    export OG_MODE=fb OG_SGX_DIRECT=1
    if [ "$mode" = --sgx-debug ]; then
        [ -r "$debug_lib" ] || exit 1
        export OG_SGX_RESIDENT=1 OG_SGX_ASYNC=1 OG_SGX_PROFILE=1
        # sgx-run prepends the system driver path. Override only the game's WSEGL.
        set -- sh -c 'export LD_LIBRARY_PATH="$1:$LD_LIBRARY_PATH"; shift; exec "$@"' sh "${debug_lib%/*}" "$@"
    fi
    xorg=$(pidof Xorg) || exit 1
    pan_file=/sys/class/graphics/fb1/pan
    restore_desktop() {
        status=$?
        # The Desktop fbdev screen uses page zero. A killed game can leave page two selected.
        if [ "$status" -ne 0 ] || [ "$(cat "$pan_file" 2>/dev/null)" != 0,0 ]; then
            printf '0,0\n' >"$pan_file" 2>/dev/null || :
        fi
        kill -CONT $xorg 2>/dev/null || return 0
        if [ -x "$DIR/bin/refresh-desktop" ]; then
            timeout 3 "$DIR/bin/refresh-desktop" >/dev/null 2>&1 || :
        fi
    }
    trap restore_desktop EXIT
    trap 'exit 1' HUP INT TERM
    kill -STOP $xorg || exit 1
    /usr/local/bin/sgx-run --fbdev "$@" >/tmp/opengorky_last.log 2>&1
    exit $?
fi
if [ "$mode" = --sgx ]; then
    if [ -f "$DIR/lib/opengorky-sgx.so" ] && /usr/local/bin/sgx-run --init 2>/tmp/opengorky_sgx_init.log; then
        exec /usr/local/bin/sgx-run "$@" >/tmp/opengorky_last.log 2>&1
    fi
    if [ "${explicit:-0}" = 1 ]; then echo 'PowerVR failed to start.' >&2; exit 1; fi
    "$dialog" -fn 10x20 -center -title 'OpenGorky' -buttons 'Software (CPU):10,Cancel:12' -default 'Software (CPU)' 'PowerVR failed to start.'
    [ "$?" = 10 ] || exit 1
fi
exec "$@" >/tmp/opengorky_last.log 2>&1
