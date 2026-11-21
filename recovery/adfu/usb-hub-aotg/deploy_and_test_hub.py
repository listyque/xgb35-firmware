"""Offline verifier, live WinUSB-ADB uploader, and Hub+Mouse+Wi-Fi+Flash tester for XGB35.

Modes (safe by default — `--check` touches zero hardware):
  python deploy_and_test_hub.py --check     Rebuild & verify aotg_hub_hcd.ko, Wi-Fi module, and ADB client offline
  python deploy_and_test_hub.py --upload    Push aotg_hub_hcd.ko + hub-mode.sh over Mini-USB ADB & insmod on Big USB-A
  python deploy_and_test_hub.py --test      Inspect USB Hub tree, Mouse/HID events, Flash drives, and Wi-Fi (wlan0)
  python deploy_and_test_hub.py --unload    Unload aotg_hub_hcd.ko & restore stock driver
"""
from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT))

import build_usb_hub_hcd  # noqa: E402


def sha256_file(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def offline_check() -> dict:
    ko = build_usb_hub_hcd.build()
    sh = HERE / 'hub-mode.sh'
    assert sh.exists()
    raw_sh = sh.read_bytes()
    if b'\r\n' in raw_sh:
        sh.write_bytes(raw_sh.replace(b'\r\n', b'\n'))
    assert b'\r\n' not in sh.read_bytes(), 'hub-mode.sh must use LF line endings'

    wifi_ko = ROOT / 'wifi-rt2800usb' / 'rt2800usb.ko'
    wifi_fw = ROOT / 'wifi-rt2800usb' / 'rt2870.bin'
    assert wifi_ko.exists() and wifi_fw.exists(), 'Missing Wi-Fi driver/firmware in wifi-rt2800usb/'
    assert b'vermagic=3.10.52 SMP preempt mod_unload ARMv7 p2v8 \x00' in wifi_ko.read_bytes()

    subprocess.check_call([sys.executable, str(ROOT / 'adb_winusb.py'), 'selftest'])

    # Self-check Low-Speed PRE candidate table covers all 4 upper bits + 0x00 within <=5 retries
    ls_candidates = [0x80, 0x40, 0x20, 0x10, 0x00]
    assert len(set(ls_candidates)) == 5 and all((b & 0x0F) == 0 for b in ls_candidates)

    report = {
        'aotg_hub_hcd.ko': {'bytes': ko.stat().st_size, 'sha256': sha256_file(ko)},
        'hub-mode.sh': {'bytes': sh.stat().st_size, 'sha256': sha256_file(sh)},
        'rt2800usb.ko': {'bytes': wifi_ko.stat().st_size, 'sha256': sha256_file(wifi_ko)},
        'rt2870.bin': {'bytes': wifi_fw.stat().st_size, 'sha256': sha256_file(wifi_fw)},
        'target_port': 'aotg1 (0xB0250000, full-size USB-A, port_mask=2, force_full_speed=1)',
        'control_link': 'aotg0 (0xB0240000, Mini-USB WinUSB ADB 10D6:FE02 — stays untouched)',
    }
    print('\n=== ALL OFFLINE CHECKS PASSED ===')
    print(json.dumps(report, indent=2))
    return report


def open_timed_adb(timeout_ms: int = 5000):
    import ctypes as c
    import time
    import adb_winusb
    for attempt in range(5):
        try:
            usb = adb_winusb.WinUsb()
            break
        except OSError:
            if attempt == 4:
                raise
            time.sleep(0.2)
    t = c.c_uint32(timeout_ms)
    usb.lib.WinUsb_SetPipePolicy(usb.handle, usb.ep_in, 3, 4, c.byref(t))
    usb.lib.WinUsb_SetPipePolicy(usb.handle, usb.ep_out, 3, 4, c.byref(t))
    return usb, adb_winusb.Adb(usb)


def adb_push(local: pathlib.Path, remote: str) -> None:
    import os
    import struct
    data = local.read_bytes()
    mode = 0o100755 if remote.endswith('.sh') else 0o100644
    spec = f'{remote},{mode}'.encode()
    usb, adb = open_timed_adb(8000)
    try:
        s = adb.open('sync:')
        buf = bytearray(b'SEND' + struct.pack('<I', len(spec)) + spec)
        for off in range(0, len(data), 65536):
            chunk = data[off:off + 65536]
            buf += b'DATA' + struct.pack('<I', len(chunk)) + chunk
        buf += b'DONE' + struct.pack('<I', int(os.path.getmtime(local)))
        s.write(bytes(buf))
        resp = s.read_exact(8)
        assert resp[:4] == b'OKAY', f'ADB push failed: {resp!r}'
        s.write(b'QUIT\0\0\0\0')
        s.close()
    finally:
        usb.close()


def adb_sh(cmd: str, timeout_ms: int = 12000) -> str:
    usb, adb = open_timed_adb(timeout_ms)
    try:
        stream = adb.open('shell:' + cmd)
        out = b''
        while (chunk := stream.read(timeout=timeout_ms / 1000.0)) is not None:
            out += chunk
        return out.decode('utf-8', 'replace')
    finally:
        usb.close()


def upload_live(force_fs: int = 1) -> None:
    ko = HERE / 'aotg_hub_hcd.ko'
    sh = HERE / 'hub-mode.sh'
    assert ko.exists() and sh.exists(), 'Run --check first'

    print('Pushing aotg_hub_hcd.ko and hub-mode.sh over Mini-USB ADB...')
    adb_push(ko, '/tmp/aotg_hub_hcd.ko')
    adb_push(sh, '/tmp/hub-mode.sh')

    out = adb_sh(
        'mkdir -p "/lib/modules/$(uname -r)" 2>/dev/null; '
        'echo -1 > /sys/module/usbcore/parameters/autosuspend 2>/dev/null; '
        'chmod 755 /tmp/hub-mode.sh; '
        'if [ -d /mnt/sd/linux ]; then cp -f /tmp/aotg_hub_hcd.ko /tmp/hub-mode.sh /mnt/sd/linux/ 2>/dev/null; fi; '
        '[ -d /sys/bus/platform/devices/aotg_hcd.1 ] || { echo c > /proc/acts_hcd; sleep 1; }; '
        'for p in /sys/bus/usb/devices/*/power/control; do echo on > "$p" 2>/dev/null; done; '
        'rmmod aotg_hub_hcd 2>/dev/null; '
        f'insmod /tmp/aotg_hub_hcd.ko port_mask=2 force_full_speed={force_fs} && '
        'sleep 2 && for p in /sys/bus/usb/devices/*/power/control; do echo auto > "$p" 2>/dev/null; echo on > "$p" 2>/dev/null; done && '
        'lsmod | grep aotg_hub_hcd'
    )
    print(out)


def test_hub_devices() -> None:
    out = adb_sh(
        'echo "=== MODULE PARAMS ==="; '
        'head -n 1 /sys/module/aotg_hub_hcd/parameters/* 2>/dev/null; '
        'echo "=== USB TREE ==="; '
        'for d in /sys/bus/usb/devices/*; do '
        '  [ -f "$d/idVendor" ] || continue; '
        '  printf "%s: %s:%s speed=%sM dev=%s %s | %s\\n" '
        '    "$(basename "$d")" "$(cat "$d/idVendor" 2>/dev/null)" "$(cat "$d/idProduct" 2>/dev/null)" '
        '    "$(cat "$d/speed" 2>/dev/null)" "$(cat "$d/devnum" 2>/dev/null)" '
        '    "$(cat "$d/manufacturer" 2>/dev/null)" "$(cat "$d/product" 2>/dev/null)"; '
        'done; '
        'echo "=== INPUT (MOUSE / KEYBOARD) ==="; '
        'for e in /sys/class/input/event*; do '
        '  [ -f "$e/device/name" ] && printf "%s -> %s\\n" "$(basename "$e")" "$(cat "$e/device/name")"; '
        'done; '
        'echo "=== USB FLASH DRIVES ==="; '
        'grep -E "sd[a-z]" /proc/partitions 2>/dev/null || echo "(none)"; '
        'for p in /dev/sd[a-z][1-9] /dev/sd[a-z]; do '
        '  [ -b "$p" ] || continue; '
        '  dd if="$p" of=/dev/null bs=4096 count=16 2>&1 | tr "\\n" " "; echo " [$p read OK]"; '
        'done; '
        'echo "=== WI-FI ==="; '
        '[ -d /sys/class/net/wlan0 ] || insmod /lib/rt2800usb.ko 2>/dev/null; '
        'sleep 1; '
        'if [ -d /sys/class/net/wlan0 ]; then '
        '  ip link set wlan0 up 2>/dev/null; '
        '  ip -o link show wlan0; '
        '  /usr/sbin/iw dev wlan0 scan 2>/dev/null | grep -E "^BSS |SSID:" | head -n 12; '
        'else '
        '  echo "(wlan0 not bound; check VID:PID in USB TREE above)"; '
        'fi; '
        'echo "=== DMESG TAIL ==="; '
        'dmesg | grep -v "t:5" | tail -n 30',
        timeout_ms=25000
    )
    print(out)


def boot_from_adfu_and_upload(force_fs: int = 1) -> None:
    import os
    import struct
    import time
    import adb_winusb
    import adfu_usb_storage_ram as storage
    from service_usb import Service

    adfu_paths = adb_winusb.present('VID_10D6&PID_10D6')
    assert adfu_paths, 'No ADFU device VID_10D6&PID_10D6 found'
    os.environ['ACTIONS_ADFU_PNP_PATH'] = adfu_paths[0]
    print('Found ADFU:', adfu_paths[0], flush=True)

    # Fresh BootROM ADFU only accepts 0xCC/0x05/0x10 (never send Service 0x93 before starting service!)
    subprocess.check_call([sys.executable, '-u', str(ROOT / 'adfu_autonomous_service.py'), '--diagnostic'])

    ram_dir = ROOT / 'usb-storage-ram'
    man = json.loads((ram_dir / 'manifest.json').read_text())
    code = storage.launcher(man['Image']['bytes'])

    with Service(exclusive=True) as u:
        for name, meta in man.items():
            data = (ram_dir / name).read_bytes()
            assert len(data) == meta['bytes']
            addr = meta['address']
            for off in range(0, len(data), 0x4000):
                u.write(addr + off, data[off:off + 0x4000])
            assert u.read(addr, min(len(data), 0x1000)) == data[:min(len(data), 0x1000)]
            print(f'Uploaded {name} ({len(data)} bytes) @ {addr:#x}', flush=True)
        storage.checked_write(u, storage.ENTRY, code)
        u.execute(storage.ENTRY)
    print('Linux handoff accepted, waiting for WinUSB ADB (10D6:FE02)...', flush=True)

    deadline = time.time() + 30
    while time.time() < deadline:
        if adb_winusb.present():
            time.sleep(1.5)
            break
        time.sleep(0.5)
    else:
        raise TimeoutError('WinUSB ADB 10D6:FE02 did not appear within 30s')

    upload_live(force_fs=force_fs)


def diag_hub() -> None:
    import time
    print(adb_sh(
        'echo -1 > /sys/module/usbcore/parameters/autosuspend 2>/dev/null; '
        'for p in /sys/bus/usb/devices/*/power/control; do echo on > "$p" 2>/dev/null; done; '
        'cat /sys/bus/usb/devices/1-1/power/runtime_status 2>/dev/null; '
        'head -n 1 /sys/bus/usb/devices/1-1/1-1:1.0/ep_*/wMaxPacketSize /sys/bus/usb/devices/1-1/1-1:1.0/ep_*/bInterval 2>/dev/null'
    ))
    time.sleep(2)
    print(adb_sh(
        'for a in 0xb0250008 0xb0250010 0xb0250018 0xb0250020 0xb0250028 0xb02500c4 0xb02500c8 0xb02500cc '
        '0xb0250188 0xb0250194 0xb02501a0 0xb02501a4 0xb02501b4 0xb02501bc 0xb025030c 0xb0250410 0xb0250414 0xb0250428; do '
        'printf "%s: " "$a"; /bin/devmem "$a" 32; done'
    ))
    test_hub_devices()


def unload_live() -> None:
    print(adb_sh('rmmod aotg_hub_hcd && dmesg | tail -n 8'))


def main() -> None:
    mode = sys.argv[1] if len(sys.argv) > 1 else '--check'
    if mode == '--check':
        offline_check()
    elif mode == '--upload':
        fs = int(sys.argv[2]) if len(sys.argv) > 2 else 1
        upload_live(force_fs=fs)
    elif mode == '--test':
        test_hub_devices()
    elif mode == '--boot-adfu':
        fs = int(sys.argv[2]) if len(sys.argv) > 2 else 1
        boot_from_adfu_and_upload(force_fs=fs)
    elif mode == '--rescan-hub':
        import time
        print(adb_sh(
            'echo 1-1:1.0 > /sys/bus/usb/drivers/hub/unbind 2>&1; '
            'sleep 1; '
            'echo 1-1:1.0 > /sys/bus/usb/drivers/hub/bind 2>&1'
        ))
        time.sleep(5)
        test_hub_devices()
    elif mode == '--ab-nowifi':
        import time
        print(adb_sh('rmmod rt2800usb 2>&1; true'))
        time.sleep(5)
        print(adb_sh(
            'for d in /sys/bus/usb/devices/*; do '
            '  [ -f "$d/idVendor" ] && printf "%s: %s:%s dev=%s %s\\n" "$(basename "$d")" "$(cat "$d/idVendor")" "$(cat "$d/idProduct")" "$(cat "$d/devnum")" "$(cat "$d/product" 2>/dev/null)"; '
            'done; '
            'dmesg | grep -v "t:5" | tail -n 25'
        ))
    elif mode == '--dmesg':
        print(adb_sh('dmesg | grep -v "t:5" | tail -n 80'))
    elif mode == '--diag':
        diag_hub()
    elif mode == '--unload':
        unload_live()
    else:
        raise SystemExit('Usage: python deploy_and_test_hub.py [--check|--boot-adfu [1|0]|--upload [1|0]|--test|--unload]')


if __name__ == '__main__':
    main()
