#!/usr/bin/env python3
"""Packed MIPI RAW10 Bayer frame -> half-resolution PNG (no numpy/PIL).

usage: raw10png.py <frame.raw> <width> <height> <stride> <out.png> [pattern]
pattern: RGGB (default), BGGR, GRBG, GBRG
Each 2x2 Bayer cell becomes one RGB pixel; auto white balance and exposure,
then gamma 2.2.
"""
import struct
import sys
import zlib


def unpack_row(b, off, width):
    """RAW10 packed: 4 pixels in 5 bytes (8 MSBs each, then 4x2 LSBs)."""
    out = []
    for i in range(0, width, 4):
        p = b[off + i // 4 * 5: off + i // 4 * 5 + 5]
        lsb = p[4]
        out += [(p[0] << 2) | (lsb & 3), (p[1] << 2) | ((lsb >> 2) & 3),
                (p[2] << 2) | ((lsb >> 4) & 3), (p[3] << 2) | ((lsb >> 6) & 3)]
    return out[:width]


def main():
    path, w, h, stride, out = sys.argv[1], *map(int, sys.argv[2:5]), sys.argv[5]
    pat = sys.argv[6] if len(sys.argv) > 6 else "RGGB"
    b = open(path, "rb").read()
    idx = {c: [] for c in "RGB"}
    for k, c in enumerate(pat):
        idx[c].append(k)
    cells = []
    for y in range(0, h - 1, 2):
        r0 = unpack_row(b, y * stride, w)
        r1 = unpack_row(b, (y + 1) * stride, w)
        row = []
        for x in range(0, w - 1, 2):
            q = (r0[x], r0[x + 1], r1[x], r1[x + 1])
            row.append((sum(q[k] for k in idx["R"]) / len(idx["R"]),
                        sum(q[k] for k in idx["G"]) / len(idx["G"]),
                        sum(q[k] for k in idx["B"]) / len(idx["B"])))
        cells.append(row)
    black = 64
    n = sum(len(r) for r in cells)
    means = [max(sum(px[c] for r in cells for px in r) / n - black, 1) for c in range(3)]
    gains = [means[1] / m for m in means]
    lum = sorted(max((px[c] - black) * gains[c] for c in range(3)) for r in cells for px in r[::7])
    top = max(lum[int(len(lum) * 0.99)], 1)
    print("channel means (R,G,B minus black):", ["%.1f" % m for m in means], "p99:", top)
    raw = bytearray()
    for r in cells:
        raw.append(0)
        for px in r:
            for c in range(3):
                v = max(0.0, (px[c] - black) * gains[c] / top)
                raw.append(min(255, int(255 * v ** (1 / 2.2))))
    ow, oh = len(cells[0]), len(cells)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", ow, oh, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
    open(out, "wb").write(png)
    print("%s: %dx%d" % (out, ow, oh))


if __name__ == "__main__":
    main()
