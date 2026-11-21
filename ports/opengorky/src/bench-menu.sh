#!/bin/sh
set -eu
cd "$(dirname "$0")"
if pidof opengorky >/dev/null; then echo 'Close OpenGorky before benchmarking.' >&2; exit 1; fi
trace=$(mktemp /tmp/opengorky-input.XXXXXX)
trap 'rm -f "$trace"' EXIT
awk 'BEGIN { for (i=0;i<1000;i++) printf "%sm %d,%d;w 0", i?";":"",40+i*17%560,35+i*11%410 }' > "$trace"
for mode in software sgx sgx software; do
    OG_MENU_BENCH=1 OG_MENU_CHECK=1 ./OpenGorky-Desktop.sh "--$mode" --quit-after 8 --input "@$trace"
    expect=0
    if [ "$mode" = sgx ]; then expect=30; fi
    echo "$mode"
    grep 'OpenGorky menu check:' /tmp/opengorky_last.log || :
    grep "menu benchmark: frames=30 gpu_frames=$expect " /tmp/opengorky_last.log
done
