#!/usr/bin/env python3
"""Writes artwork/Galleria.hvif, Galleria's vector icon.

HVIF is Haiku's compact vector icon format (what Icon-O-Matic exports). The
encoding follows src/libs/icon/flat_icon/FlatIconImporter.cpp in the Haiku
source tree: a magic number, then styles, paths and shapes. Coordinates are
in a 64 x 64 space.

The icon: a photo lying behind a tilted print showing a landscape (sky, sun,
two hills), in Haiku's usual style with dark outlines and soft gradients.
"""

import math
import os
import struct
import sys

STYLE_SOLID_COLOR = 1
STYLE_GRADIENT = 2
STYLE_SOLID_COLOR_NO_ALPHA = 3
SHAPE_PATH_SOURCE = 10

GRADIENT_LINEAR = 0
GRADIENT_CIRCULAR = 1
GRADIENT_FLAG_TRANSFORM = 1 << 1
GRADIENT_FLAG_NO_ALPHA = 1 << 2

PATH_FLAG_CLOSED = 1 << 1
PATH_FLAG_NO_CURVES = 1 << 3

SHAPE_FLAG_TRANSFORM = 1 << 1


def coord(value):
    """One byte for small integers, otherwise 15 bit fixed point."""
    if float(value).is_integer() and -32 <= value <= 95:
        return bytes([int(value) + 32])
    raw = int((value + 128.0) * 102.0) | 0x8000
    return bytes([raw >> 8, raw & 0xFF])


def float24(value):
    """1 sign bit, 6 bit exponent, 17 bit mantissa."""
    if value == 0.0:
        return b"\0\0\0"
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    sign = bits >> 31
    exponent = ((bits >> 23) & 0xFF) - 127
    mantissa = bits & 0x7FFFFF
    if exponent >= 32 or exponent < -32:
        return b"\0\0\0"
    short = (sign << 23) | ((exponent + 32) << 17) | (mantissa >> 6)
    return bytes([(short >> 16) & 0xFF, (short >> 8) & 0xFF, short & 0xFF])


def matrix(m):
    return b"".join(float24(v) for v in m)


def rgb(hex_color):
    hex_color = hex_color.lstrip("#")
    return bytes(int(hex_color[i:i + 2], 16) for i in (0, 2, 4))


# --- styles -----------------------------------------------------------------

def solid(hex_color, alpha=None):
    if alpha is None:
        return bytes([STYLE_SOLID_COLOR_NO_ALPHA]) + rgb(hex_color)
    return bytes([STYLE_SOLID_COLOR]) + rgb(hex_color) + bytes([alpha])


def vertical_gradient(top, bottom, y0, y1, cx=32.0, stops=None):
    """Linear gradient from y0 (top color) to y1 (bottom color).

    A linear gradient runs from -64 to 64 along x in gradient space; the
    matrix rotates that onto the y axis and scales it to y0..y1.
    """
    s = (y1 - y0) / 128.0
    m = (0.0, s, -s, 0.0, cx, (y0 + y1) / 2.0)
    stops = stops or [(0, top), (255, bottom)]
    data = bytes([STYLE_GRADIENT, GRADIENT_LINEAR,
                  GRADIENT_FLAG_TRANSFORM | GRADIENT_FLAG_NO_ALPHA,
                  len(stops)])
    data += matrix(m)
    for offset, color in stops:
        data += bytes([offset]) + rgb(color)
    return data


def radial_gradient(center, edge, cx, cy, radius):
    s = radius / 64.0
    m = (s, 0.0, 0.0, s, cx, cy)
    data = bytes([STYLE_GRADIENT, GRADIENT_CIRCULAR,
                  GRADIENT_FLAG_TRANSFORM | GRADIENT_FLAG_NO_ALPHA, 2])
    data += matrix(m)
    data += bytes([0]) + rgb(center) + bytes([255]) + rgb(edge)
    return data


# --- paths ------------------------------------------------------------------

def polygon(points):
    data = bytes([PATH_FLAG_CLOSED | PATH_FLAG_NO_CURVES, len(points)])
    for x, y in points:
        data += coord(x) + coord(y)
    return data


def rect(left, top, right, bottom):
    return polygon([(left, top), (right, top), (right, bottom),
                    (left, bottom)])


def curves(points):
    """Closed path of (point, in-handle, out-handle) triples."""
    data = bytes([PATH_FLAG_CLOSED, len(points)])
    for point, handle_in, handle_out in points:
        for x, y in (point, handle_in, handle_out):
            data += coord(x) + coord(y)
    return data


def corner(x, y):
    return ((x, y), (x, y), (x, y))


def circle(cx, cy, r):
    k = r * 0.5523
    return curves([
        ((cx, cy - r), (cx - k, cy - r), (cx + k, cy - r)),
        ((cx + r, cy), (cx + r, cy - k), (cx + r, cy + k)),
        ((cx, cy + r), (cx + k, cy + r), (cx - k, cy + r)),
        ((cx - r, cy), (cx - r, cy + k), (cx - r, cy - k)),
    ])


# --- shapes -----------------------------------------------------------------

def rotation(degrees, cx, cy, dx=0.0, dy=0.0):
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    return (c, s, -s, c, cx - c * cx + s * cy + dx, cy - s * cx - c * cy + dy)


def shape(style, paths, transform=None):
    data = bytes([SHAPE_PATH_SOURCE, style, len(paths)]) + bytes(paths)
    if transform is None:
        return data + bytes([0])
    return data + bytes([SHAPE_FLAG_TRANSFORM]) + matrix(transform)


def build():
    styles = [
        solid("#1E252E"),                                         # 0 outline
        vertical_gradient("#F4F4F4", "#B9BEC4", 8, 44, 36),       # 1 back card
        vertical_gradient("#FFC259", "#C8457E", 11, 37, 36),      # 2 back photo
        vertical_gradient("#FFFFFF", "#D3D6DA", 20, 60, 28),      # 3 front card
        vertical_gradient("#2C7FE6", "#B4E1FF", 24, 46, 28),      # 4 sky
        radial_gradient("#FFFBD0", "#FFB000", 35, 31, 5),         # 5 sun
        vertical_gradient("#46A653", "#25652F", 35, 50, 28),      # 6 far hill
        vertical_gradient("#A6E07A", "#3F9A33", 40, 50, 28),      # 7 near hill
        solid("#000000", 60),                                     # 8 shadow
    ]

    paths = [
        rect(16.5, 6.5, 55.5, 45.5),        # 0 back card outline
        rect(18, 8, 54, 44),                # 1 back card
        rect(21, 11, 51, 37),               # 2 back photo
        rect(9, 22, 50, 62),                # 3 front card shadow
        rect(6.5, 18.5, 49.5, 61.5),        # 4 front card outline
        rect(8, 20, 48, 60),                # 5 front card
        rect(12, 24, 44, 50),               # 6 sky
        circle(35, 31, 4),                  # 7 sun
        curves([                            # 8 far hill
            corner(12, 50),
            ((12, 42), (12, 42), (15, 38)),
            ((21, 35), (17, 35), (26, 35)),
            ((40, 50), (32, 40), (40, 50)),
        ]),
        curves([                            # 9 near hill
            corner(12, 50),
            ((12, 47), (12, 47), (20, 42)),
            ((36, 40), (29, 40), (40, 40)),
            ((44, 41), (42, 40), (44, 41)),
            corner(44, 50),
        ]),
    ]

    back = rotation(10, 36, 26)
    front = rotation(-7, 28, 40)

    shapes = [
        shape(0, [0], back),
        shape(1, [1], back),
        shape(2, [2], back),
        shape(8, [3], front),
        shape(0, [4], front),
        shape(3, [5], front),
        shape(4, [6], front),
        shape(5, [7], front),
        shape(6, [8], front),
        shape(7, [9], front),
    ]

    data = b"ncif"
    data += bytes([len(styles)]) + b"".join(styles)
    data += bytes([len(paths)]) + b"".join(paths)
    data += bytes([len(shapes)]) + b"".join(shapes)
    return data


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        root, "artwork", "Galleria.hvif")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    data = build()
    with open(out, "wb") as f:
        f.write(data)
    print(f"wrote {out} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
