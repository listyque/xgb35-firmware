"""Read the panel SPI response through the update's SDA, SCL and CS pins."""
import json
import struct
import time

from adfu_backlight_probe import GPIOB_DAT, GPIOB_INEN, GPIOB_OUTEN, read_registers, run
from adfu_lcd_pattern import GPIO, spi_edges
from build_autonomous_service import ROOT, branch, mov
from service_usb import Service


A_OUT, A_IN, A_DAT = GPIO, GPIO + 4, GPIO + 8
C_OUT, C_IN, C_DAT = GPIO + 0x18, GPIO + 0x1C, GPIO + 0x20
SCL, SDA, CS = 1 << 18, 1 << 19, 1 << 28
REGISTERS = (A_DAT, C_DAT, GPIOB_DAT, A_IN, C_IN, GPIOB_INEN, A_OUT, C_OUT, GPIOB_OUTEN)
DELAY = (*mov(2, 1024), 0xE2522001, 0x1AFFFFFD)


def main():
    with Service(exclusive=True) as usb:
        assert usb.read(0xB4069EFC, 4) == struct.pack('<I', branch(0xB4069EFC, 0xB406A440))
        initial = read_registers(usb, REGISTERS)
        a, c = initial[A_DAT] & ~(SCL | SDA), initial[C_DAT] & ~CS
        pins = {
            A_DAT: a, C_DAT: c | CS, GPIOB_DAT: initial[GPIOB_DAT] | 0x40,
            A_IN: initial[A_IN] & ~(SCL | SDA), C_IN: initial[C_IN] & ~CS,
            GPIOB_INEN: initial[GPIOB_INEN] & ~0x40, A_OUT: initial[A_OUT] | SCL | SDA,
            C_OUT: initial[C_OUT] | CS, GPIOB_OUTEN: initial[GPIOB_OUTEN] | 0x40,
        }
        responses = {}
        try:
            run(usb, pins.items(), REGISTERS)
            time.sleep(0.15)
            commands = (
                (0x04, 25), (0xDA, 8), (0xDB, 8), (0xDC, 8), (0x0A, 8), (0x0C, 8),
            )
            for command, count in commands:
                writes = spi_edges(a, c, command, False)
                assert writes[-1] == (C_DAT, c | CS)
                writes[-1:] = [(A_OUT, pins[A_OUT] & ~SDA), (A_IN, pins[A_IN] | SDA)]
                run(usb, writes, REGISTERS, DELAY)
                bits = []
                for _ in range(count):
                    run(usb, [(A_DAT, a), (A_DAT, a | SCL)], REGISTERS, DELAY)
                    levels = read_registers(usb, (A_DAT,))[A_DAT]
                    bits.append('1' if levels & SDA else '0')
                responses[f'{command:02x}'] = ''.join(bits)
                run(usb, [(C_DAT, c | CS), (A_IN, pins[A_IN]), (A_OUT, pins[A_OUT])], REGISTERS)
            print(responses, flush=True)
        finally:
            run(usb, reversed(tuple(initial.items())), REGISTERS)
            restored = read_registers(usb, initial)
            assert all(restored[a] == initial[a] for a in initial
                       if a not in (A_DAT, GPIOB_DAT, C_DAT))
            (ROOT / 'adfu-lcd-spi-read.json').write_text(json.dumps(responses, indent=2))


if __name__ == '__main__':
    main()
