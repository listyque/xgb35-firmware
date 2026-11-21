"""Temporarily drive the stock PWM0 backlight from the diagnostic RAM service."""
import argparse
import json
import struct
import time

from build_autonomous_service import ROOT, branch, mov
from service_usb import Service


ENTRY = 0xB406D000
CMU_PWM0CLK = 0xB0150070
CMU_DEVCLKEN1 = 0xB01500A4
MFP_CTL1 = 0xB01C0044
PWM0_CTL = 0xB01C0050
GPIOB_OUTEN = 0xB01C000C
GPIOB_INEN = 0xB01C0010
GPIOB_DAT = 0xB01C0014
BACKLIGHT_REGISTERS = (CMU_PWM0CLK, CMU_DEVCLKEN1, MFP_CTL1, PWM0_CTL)
PANEL_REGISTERS = (GPIOB_DAT, GPIOB_INEN, GPIOB_OUTEN)
REGISTERS = PANEL_REGISTERS + BACKLIGHT_REGISTERS


def read_registers(usb, addresses):
    return {address: struct.unpack('<I', usb.read(address, 4))[0]
            for address in addresses}


def payload(writes, registers=REGISTERS, delay=()):
    words = []
    for address, value in writes:
        assert address in registers
        words.extend((*mov(0, address), *mov(1, value), 0xE5801000, 0xF57FF04F))
        words.extend(delay)
    words.extend((0xE3A00000, 0xE12FFF1E))
    return struct.pack('<' + 'I' * len(words), *words)


def run(usb, writes, registers=REGISTERS, delay=()):
    code = payload(writes, registers, delay)
    usb.write(ENTRY, code)
    assert usb.read(ENTRY, len(code)) == code
    usb.execute(ENTRY)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--panel-power', action='store_true')
    args = parser.parse_args()
    with Service() as usb:
        assert usb.read(0xB4069EFC, 4) == struct.pack('<I', branch(0xB4069EFC, 0xB406A440))
        initial = read_registers(usb, REGISTERS if args.panel_power else BACKLIGHT_REGISTERS)
        # U-Boot ATS3605 PWM config at 0x020013c4. Its clock table maps PWM0
        # to bit 55. X70 kernel pinctrl sets MFP_CTL1[31:29] to option 3.
        period_ns, steps, level = 50_000, 1024, 450
        duty_ns = period_ns * level // steps
        duty_ticks = (duty_ns * 64 + period_ns - 1) // period_ns
        divider = (24_000_000 + 20_000 * 64 - 1) // (20_000 * 64) - 1
        assert (duty_ticks, divider) == (29, 18)
        active = {
            CMU_PWM0CLK: (initial[CMU_PWM0CLK] & ~0x3FF) | 0x1000 | divider,
            CMU_DEVCLKEN1: initial[CMU_DEVCLKEN1] | (1 << 23),
            MFP_CTL1: (initial[MFP_CTL1] & ~(7 << 29)) | (3 << 29),
            PWM0_CTL: (initial[PWM0_CTL] & 0xE00001FF) | 0x8100 | (duty_ticks << 19),
        }
        panel = {}
        if args.panel_power:
            # Both DTBs specify active-high PB6. The stock backlight path
            # clears output-enable and sets input-enable on PB3.
            panel = {
                GPIOB_DAT: initial[GPIOB_DAT] | (1 << 6),
                GPIOB_INEN: (initial[GPIOB_INEN] & ~(1 << 6)) | (1 << 3),
                GPIOB_OUTEN: (initial[GPIOB_OUTEN] & ~(1 << 3)) | (1 << 6),
            }
        report = {
            'initial': {hex(a): hex(v) for a, v in initial.items()},
            'target': {hex(a): hex(v) for a, v in (panel | active).items()},
        }
        print(json.dumps(report), flush=True)
        if not args.apply:
            return
        try:
            if panel:
                run(usb, panel.items())
                time.sleep(0.1)
            run(usb, active.items())
            observed = read_registers(usb, initial)
            report['observed'] = {hex(a): hex(v) for a, v in observed.items()}
            for address, value in (panel | active).items():
                mask = 1 << 6 if address == GPIOB_DAT else 0xFFFFFFFF
                assert (observed[address] ^ value) & mask == 0, report
            print('PWM0 active for 90 seconds', flush=True)
            time.sleep(90)
        finally:
            run(usb, reversed(tuple(initial.items())))
            restored = read_registers(usb, initial)
            report['restored'] = {hex(a): hex(v) for a, v in restored.items()}
            name = 'adfu-backlight-with-panel-power.json' if panel else 'adfu-backlight-probe.json'
            (ROOT / name).write_text(json.dumps(report, indent=2))
            for address, value in initial.items():
                if address != GPIOB_DAT:
                    assert restored[address] == value, report
            print('PWM0 registers restored', flush=True)


if __name__ == '__main__':
    main()
