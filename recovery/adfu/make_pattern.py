"""RGB565 test pattern: python make_pattern.py WIDTH HEIGHT STRIDE_BYTES out.raw [--landscape]
Top-left red corner square, white 2px border, 8 vertical colour bars, gradient strip at the bottom,
green arrow pointing up. Lets you read off resolution, offset, R/B swap and rotation from one photo.

--landscape: draw a HEIGHTxWIDTH landscape picture into the portrait buffer. On this console the
ST7796S 320x480 panel sits rotated: buffer (0,0) is the physical bottom-left corner, buffer y runs
left->right. So landscape pixel (X, Y) goes to buffer (x = WIDTH-1-Y, y = X)."""
import struct
import sys


def rgb565(r, g, b):
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


def color(x, y, w, h):
    bars = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
            (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]
    if x < 2 or y < 2 or x >= w - 2 or y >= h - 2:
        return 255, 255, 255
    if x < 40 and y < 40:
        return 255, 0, 0
    if abs(x - w // 2) < (y - h // 4) // 2 + 1 and h // 4 <= y < h // 4 + 40:
        return 0, 255, 0   # arrow head, points up
    if y >= h - 40:
        v = x * 255 // max(w - 1, 1)
        return v, v, v
    return bars[x * 8 // w]


def pattern(w, h, stride, landscape=False):
    out = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            # landscape: X = y, Y = w-1-x on a h x w picture
            c = color(y, w - 1 - x, h, w) if landscape else color(x, y, w, h)
            struct.pack_into('<H', out, y * stride + 2 * x, rgb565(*c))
    return bytes(out)


if __name__ == '__main__':
    w, h, stride = map(int, sys.argv[1:4])
    landscape = '--landscape' in sys.argv
    data = pattern(w, h, stride, landscape)
    assert len(data) == stride * h
    if landscape:  # landscape top-left red square must land at buffer top-right
        at = 3 * stride + 2 * (w - 4)
        assert data[at:at+2] == struct.pack('<H', rgb565(255, 0, 0))
    open(sys.argv[4], 'wb').write(data)
