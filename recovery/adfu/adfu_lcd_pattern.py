"""Display RGB565 test patterns from RAM, with register rollback."""
import hashlib
import json
import struct
import sys
import time

from adfu_backlight_probe import (
    BACKLIGHT_REGISTERS, CMU_DEVCLKEN1, CMU_PWM0CLK, ENTRY, GPIOB_DAT, GPIOB_INEN,
    GPIOB_OUTEN, MFP_CTL1, PANEL_REGISTERS, PWM0_CTL, read_registers, run,
)
from build_autonomous_service import ROOT, branch, mov
from service_usb import Service


FB = 0x01200000
WIDTH, HEIGHT = 320, 480
CMU = 0xB0150000
GPIO = 0xB01C0000
LCD = 0xB02A0000
DE = 0xB02F0000
PATH = DE + 0x140  # The stock path with output capability 0x41, including LCD.
VIDEO = DE + 0x220
CLOCKS = (CMU + 0x24, CMU + 0x30, CMU + 0xA0)
PINS = (GPIO, GPIO + 4, GPIO + 8, GPIO + 0x18, GPIO + 0x1C, GPIO + 0x20, GPIO + 0x40)
SCANOUT = (
    LCD, LCD + 4, LCD + 0xC, LCD + 0x10, LCD + 0x14, LCD + 0x18,
    PATH, PATH + 4, PATH + 8, PATH + 0xC, PATH + 0x10, PATH + 0x14,
    VIDEO, VIDEO + 4, VIDEO + 8, VIDEO + 0xC,
    *(VIDEO + n for n in range(0x10, 0x30, 4)), VIDEO + 0x30, VIDEO + 0x48,
)
REGISTERS = CLOCKS + PINS + PANEL_REGISTERS + BACKLIGHT_REGISTERS + SCANOUT


def spi_words(swap_axes=False):
    # Decode the immediate calls of panel_glp_enable in this exact U-Boot.
    image = (ROOT.parent / 'uboot.bin').read_bytes()[64:]
    assert hashlib.sha256(image).hexdigest() == (
        '38abcaa2bc8ee41505230c326365be208fdaebf74ed87d2b6721cd55dfb1d995'
    )
    calls = {0x1E10C: 'data', 0x1E1AC: 'command', 0x321C4: 'wait'}
    result = []
    for address in range(0x1E25C, 0x1E5D4, 4):
        word, = struct.unpack_from('<I', image, address)
        if word & 0xFFFFF000 in (0xE3A00000, 0xE3A01000):
            value = word & 0xFF
            assert word & 0xF00 == 0
        if word & 0xFF000000 == 0xEB000000:
            target = address + 8 + 4 * ((word & 0x7FFFFF) - (word & 0x800000))
            assert target in calls
            result.append((calls[target], value))
    assert len(result) == 75
    assert result[:2] == [('command', 0x11), ('wait', 150)]
    assert result[2:4] == [('command', 0x36), ('data', 0x0C)]
    assert result[-2:] == [('wait', 60), ('command', 0x2C)]
    if swap_axes:
        result[3] = ('data', 0x2C)
    return result


def spi_edges(a, c, value, is_data):
    data, clock, select = 1 << 19, 1 << 18, 1 << 28
    writes = [(GPIO + 0x20, c), (GPIO + 8, a)]
    for bit in (int(is_data), *((value >> i) & 1 for i in range(7, -1, -1))):
        line = a | (data if bit else 0)
        writes.extend(((GPIO + 8, line), (GPIO + 8, line | clock)))
    writes.append((GPIO + 0x20, c | select))
    return writes


def clean_frame(usb, start, length):
    assert Service.writable(start, length) and start % 32 == length % 32 == 0
    # Clean cache lines to the point of coherency before enabling display DMA.
    words = (*mov(0, start), *mov(1, start + length), 0xEE070F3A,
             0xE2800020, 0xE1500001, 0x3AFFFFFB, 0xF57FF04F, 0xE3A00000, 0xE12FFF1E)
    code = struct.pack('<' + 'I' * len(words), *words)
    usb.write(ENTRY, code)
    assert usb.read(ENTRY, len(code)) == code
    usb.execute(ENTRY)


def main():
    vga = '--vga' in sys.argv
    wqvga = '--480x272' in sys.argv
    portrait = '--272x480' in sys.argv
    swap_axes = '--swap-axes' in sys.argv
    assert sum((vga, wqvga, portrait)) <= 1
    assert not (vga and swap_axes)
    words = spi_words(swap_axes)
    width, height = (640, 480) if vga else (WIDTH, HEIGHT)
    hsw, hbp, hfp = (96, 48, 16) if vga else (6, 4, 10)
    vsw, vbp, vfp = (2, 33, 10) if vga else (1, 1, 19)
    lcd_clock = 0x102 if vga else 0x105
    if wqvga or portrait:
        width, height = (272, 480) if portrait else (480, 272)
        hsw, hbp, hfp = 41, 2, 2
        vsw, vbp, vfp = 10, 2, 2
        lcd_clock = 0x107  # The vendor factor table maps this to 480 MHz / 56.
    mode = f'{width}x{height}'
    row = b''.join(struct.pack('<H', color) * (width // 8)
                   for color in (0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0))
    pattern = row * height
    assert len(pattern) == width * height * 2 and row[:2] == b'\xff\xff' and row[-2:] == bytes(2)
    if '--apply' not in sys.argv:
        assert len(spi_edges(0, 0, 0x11, False)) == 21
        print(f'{mode} RGB565 framebuffer and stock panel commands verified')
        return

    frames = {FB: pattern}
    cycle = '--cycle' in sys.argv
    background = '--background' in sys.argv
    if cycle:
        frames = {FB + 0x100000 * i: struct.pack('<H', color) * (width * height)
                  for i, color in enumerate((0xF800, 0x07E0, 0x001F))}
    with Service(exclusive=True) as usb:
        assert usb.read(0xB4069EFC, 4) == struct.pack('<I', branch(0xB4069EFC, 0xB406A440))
        for address, frame in frames.items():
            for offset in range(0, len(frame), 0x4000):
                data = frame[offset:offset + 0x4000]
                usb.write(address + offset, data)
                assert usb.read(address + offset, len(data)) == data
            clean_frame(usb, address, len(frame))
        initial = read_registers(usb, CLOCKS + PINS + PANEL_REGISTERS + BACKLIGHT_REGISTERS)
        dev_pll, _, _, disp_pll = struct.unpack('<4I', usb.read(CMU + 4, 16))
        reset, = struct.unpack('<I', usb.read(CMU + 0xA8, 4))
        assert reset & 0x180 == 0x180 and disp_pll & 0x100
        pixel_mhz = (disp_pll & 0xFF) * 6
        assert pixel_mhz == 480
        de_div = ((dev_pll & 0x7F) * 6 + 125) // 330
        assert de_div == 2
        display_clock = {
            CMU + 0x24: lcd_clock,
            CMU + 0x30: initial[CMU + 0x30] | 0x1311,
            CMU + 0xA0: initial[CMU + 0xA0] | 0x300,
        }
        initial_scanout = {}
        report = {'mode': mode, 'panel_spi': not vga, 'lcd_clock': hex(lcd_clock)}
        if not vga:
            report['madctl'] = hex(words[3][1])
        spi_ready = False
        try:
            run(usb, display_clock.items(), REGISTERS)
            time.sleep(0.005)
            assert read_registers(usb, display_clock) == display_clock
            initial_scanout = read_registers(usb, SCANOUT)
            assert not (initial_scanout[LCD] & 1 or initial_scanout[PATH] & 0x40)
            size = (height - 1) << 16 | width - 1
            scanout = {
                LCD + 4: size, LCD + 0xC: 0x20C0 if vga else 0x2000,
                LCD + 0x10: (hbp - 1) << 20 | (vsw - 1) << 10 | (hfp - 1),
                LCD + 0x14: (vbp - 1) << 20 | (vfp - 1) << 10 | (hsw - 1),
                LCD + 0x18: 0xFF,
                PATH + 8: 0 if background else 1, PATH + 0xC: 0,
                PATH + 0x10: size, PATH + 0x14: 0,
                VIDEO: 0x04000000, VIDEO + 4: size, VIDEO + 8: size,
                VIDEO + 0xC: 0x20002000, VIDEO + 0x30: FB,
                VIDEO + 0x48: width * 2 // 8,
            }
            scanout.update(zip(range(VIDEO + 0x10, VIDEO + 0x30, 4),
                               (0x4000, 0xFF073EFC, 0xFE1038FA, 0xFC1B30F9,
                                0xFA2626FA, 0xF9301BFC, 0xFA3810FE, 0xFC3E07FF)))
            scanout.update({PATH: (initial_scanout[PATH] & ~0x200) | 0x60,
                            LCD: (initial_scanout[LCD] & ~0x800700C3) | (0x81 if vga else 0x20081),
                            PATH + 4: 1})
            run(usb, scanout.items(), REGISTERS)
            for address in (LCD, LCD + 4, PATH, VIDEO + 0x30):
                assert read_registers(usb, (address,))[address] == scanout[address]

            spi_a, spi_c = 3 << 18, 1 << 28
            a, c = initial[GPIO + 8] & ~spi_a, initial[GPIO + 0x20] & ~spi_c
            panel = {
                GPIO + 0x40: (initial[GPIO + 0x40] & ~0xFFFFC000) | 0x24924000,
                MFP_CTL1: (initial[MFP_CTL1] & ~0x600000) | 0x600000,
                GPIOB_DAT: initial[GPIOB_DAT] | 0x40,
                GPIOB_INEN: (initial[GPIOB_INEN] & ~0x40) | 8,
                GPIOB_OUTEN: (initial[GPIOB_OUTEN] & ~8) | 0x40,
            }
            if not vga:
                panel.update({
                    GPIO + 8: a, GPIO + 0x20: c | spi_c,
                    GPIO + 4: initial[GPIO + 4] & ~spi_a,
                    GPIO + 0x1C: initial[GPIO + 0x1C] & ~spi_c,
                    GPIO: initial[GPIO] | spi_a, GPIO + 0x18: initial[GPIO + 0x18] | spi_c,
                })
            run(usb, panel.items(), REGISTERS)
            time.sleep(0.1)
            spi_delay = (*mov(2, 1024), 0xE2522001, 0x1AFFFFFD)
            spi_ready = not vga
            for kind, value in (() if vga else words):
                if kind == 'wait':
                    time.sleep(value / 1000)
                else:
                    run(usb, spi_edges(a, c, value, kind == 'data'), REGISTERS, spi_delay)

            pwm = {
                CMU_PWM0CLK: (initial[CMU_PWM0CLK] & ~0x3FF) | 0x1012,
                CMU_DEVCLKEN1: initial[CMU_DEVCLKEN1] | (1 << 23),
                MFP_CTL1: (initial[MFP_CTL1] & ~0xE0600000) | 0x60600000,
                PWM0_CTL: (initial[PWM0_CTL] & 0xE00001FF) | 0xE88100,
            }
            run(usb, pwm.items(), REGISTERS)
            assert read_registers(usb, pwm) == pwm
            status_regs = (LCD, LCD + 4, LCD + 8, DE + 4, DE + 0x402C, PATH,
                           PATH + 4, PATH + 8, VIDEO + 0x30, PWM0_CTL)
            report['active'] = read_registers(usb, status_regs)
            if background:
                report['background'] = []
                for color in (0xFF0000, 0x00FF00, 0x0000FF) * 4:
                    run(usb, [(PATH + 0xC, color), (PATH + 4, 1)], REGISTERS)
                    time.sleep(0.1)
                    report['background'].append(read_registers(usb, (*status_regs, PATH + 0xC)))
                    print(f'{mode} DE background {color:06x}', flush=True)
                    time.sleep(6)
            elif cycle:
                report['frames'] = []
                for address, color in zip(tuple(frames) * 4, ('red', 'green', 'blue') * 4):
                    run(usb, [(VIDEO + 0x30, address), (PATH + 4, 1)], REGISTERS)
                    time.sleep(0.1)
                    report['frames'].append(read_registers(usb, status_regs))
                    print(f'{mode} {color} frame', flush=True)
                    time.sleep(7)
            else:
                print(f'{mode} RGB565 pattern active for 90 seconds', flush=True)
                time.sleep(90)
        finally:
            run(usb, [(PWM0_CTL, initial[PWM0_CTL])], REGISTERS)
            if swap_axes and spi_ready:
                run(usb, spi_edges(a, c, 0x36, False), REGISTERS, spi_delay)
                run(usb, spi_edges(a, c, 0x0C, True), REGISTERS, spi_delay)
            if initial_scanout:
                run(usb, [(LCD, initial_scanout[LCD]), (PATH, initial_scanout[PATH])], REGISTERS)
                run(usb, initial_scanout.items(), REGISTERS)
            run(usb, reversed(tuple(initial.items())), REGISTERS)
            restored = read_registers(usb, initial)
            report['restored'] = restored
            (ROOT / 'adfu-lcd-pattern.json').write_text(json.dumps(report, indent=2))
            assert all(restored[a] == initial[a] for a in initial
                       if a not in (GPIO + 8, GPIOB_DAT, GPIO + 0x20))
            print('Display registers restored', flush=True)


if __name__ == '__main__':
    main()
