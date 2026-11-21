"""Reboot RAM Linux back into ADFU and boot a fresh build, without touching the ADFU button.

python ram_linux_cycle.py [fault_trace flags]   default: --headless --rom-return --fatal-only --usb-test --memory-128 --adb
python ram_linux_cycle.py --to-adfu-only

Chain: adb reboot-adfu (PMIC REBOOT_ADFU flag) -> cold BootROM ADFU -> autonomous diagnostic service
(DDR init) -> adfu_linux_fault_trace.py. If Linux crashes, --rom-return brings the CPU back to ROM ADFU,
and the next cycle starts from there (warm attach). No NAND access anywhere in this chain.
"""
import os
import struct
import subprocess
import sys
import time

import adb_winusb

HERE = os.path.dirname(os.path.abspath(__file__))
ADFU = 'VID_10D6&PID_10D6'
DEFAULT = ['--rom-return', '--fatal-only', '--usb-test', '--memory-128', '--adb']


def wait(predicate, seconds, what):
    end = time.time() + seconds
    while time.time() < end:
        result = predicate()
        if result:
            return result
        time.sleep(0.5)
    raise TimeoutError(what)


def python(*args):
    print('>', 'python', *args, flush=True)
    return subprocess.run([sys.executable, *args], cwd=HERE, env=os.environ).returncode


def to_adfu():
    if adb_winusb.present():
        print('RAM Linux is up, rebooting it into ADFU', flush=True)
        if b'ok' not in adb_winusb.run('[ -x /bin/reboot-adfu ] && echo ok'):
            # builds made before reboot-adfu was added to the initramfs
            usb = adb_winusb.WinUsb()
            try:
                adb_winusb.push(adb_winusb.Adb(usb), os.path.join(HERE, 'usb-storage-ram', 'reboot-adfu'), '/tmp/reboot-adfu')
            finally:
                usb.close()
            binary = '/tmp/reboot-adfu'
        else:
            binary = '/bin/reboot-adfu'
        try:
            print(adb_winusb.run(f'{binary}; echo reboot returned $?').decode(errors='replace'))
        except Exception as error:  # USB drops while the SoC resets
            print('adb dropped as expected:', error)
        wait(lambda: not adb_winusb.present(), 15, 'adb gadget did not go away')
    path = wait(lambda: adb_winusb.present(ADFU), 30,
                'no ADFU device: reboot-adfu did not work, enter ADFU by hand once')[0]
    os.environ['ACTIONS_ADFU_PNP_PATH'] = path
    print('ADFU device:', path, flush=True)


def ensure_service():
    from rom_usb import ROM
    from service_usb import Service
    try:
        with ROM() as usb:
            pll, = struct.unpack('<I', usb.read(0xB0150008, 4))
    except Exception:
        with Service() as usb:
            assert usb.read(0xB0150008, 4) == struct.pack('<I', 0x13C)
        print('Diagnostic service already running', flush=True)
        return
    print(f'BootROM ADFU, DDR PLL {pll:#x}', flush=True)
    assert python('adfu_autonomous_service.py', '--diagnostic') == 0, 'service start failed'


def main():
    to_adfu()
    ensure_service()
    if '--to-adfu-only' in sys.argv:
        return
    flags = sys.argv[1:] or DEFAULT
    python('adfu_linux_fault_trace.py', '--execute', *flags)  # ends with a ROM error when Linux stays up
    if '--adb' in flags:
        wait(lambda: adb_winusb.present(), 30, 'adb gadget did not appear (see trace JSON)')
        print(adb_winusb.run('uname -a; uptime').decode(errors='replace'))


if __name__ == '__main__':
    main()
