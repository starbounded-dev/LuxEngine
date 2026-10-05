#!/usr/bin/env python3
"""Compare two golden captures (.lximg) written by the editor's GoldenCapture tool.

    golden_compare.py A.lximg B.lximg [--tolerance 2] [--float-tolerance 0.004]
                      [--max-bad-percent 0.05] [--diff diff.png]
    golden_compare.py --to-png capture.lximg out.png

A pixel is "bad" when any channel differs by more than the tolerance (8-bit units for
integer formats; absolute plus 1% relative for float formats). Exits 1 when the share of bad
pixels exceeds --max-bad-percent, or when the images differ in size or format.

Pure Python 3 (struct, zlib); no third-party packages. The .lximg layout matches
Editor/Source/Tools/GoldenCapture.cpp: a 32-byte little-endian header followed by
width * height * bytesPerPixel texel bytes.
"""

import argparse
import struct
import sys
import zlib

HEADER = struct.Struct("<4s7I")
MAGIC = b"LXIM"

UNORM8, FLOAT16, FLOAT32, UINT8, UINT16, UINT32, RAW = 0, 1, 2, 3, 4, 5, 255
COMPONENT_CODES = {UNORM8: "B", FLOAT16: "e", FLOAT32: "f", UINT8: "B", UINT16: "H", UINT32: "I"}


class Capture:
    def __init__(self, path):
        with open(path, "rb") as file:
            data = file.read()
        if len(data) < HEADER.size:
            raise ValueError(f"{path}: file too small for a header")
        magic, version, self.width, self.height, self.channels, self.component, self.format, self.bpp = HEADER.unpack_from(data)
        if magic != MAGIC or version != 1:
            raise ValueError(f"{path}: not an LXIM v1 capture")
        self.texels = data[HEADER.size:HEADER.size + self.width * self.height * self.bpp]
        if len(self.texels) != self.width * self.height * self.bpp:
            raise ValueError(f"{path}: truncated texel data")
        self.path = path

    def values(self):
        """Flat list of channel values, or None for RAW formats."""
        code = COMPONENT_CODES.get(self.component)
        if code is None:
            return None
        count = self.width * self.height * self.channels
        return struct.unpack(f"<{count}{code}", self.texels)

    def is_float(self):
        return self.component in (FLOAT16, FLOAT32)


def write_png(path, width, height, rgba_bytes):
    """Write an 8-bit RGBA PNG with zlib only."""
    stride = width * 4
    raw = b"".join(b"\x00" + rgba_bytes[y * stride:(y + 1) * stride] for y in range(height))

    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as file:
        file.write(png)


def to_rgba8(capture):
    """Preview conversion: floats clamped to [0, 1], missing channels filled, alpha forced opaque."""
    values = capture.values()
    if values is None:
        raise ValueError(f"{capture.path}: format {capture.format} has no preview conversion")
    out = bytearray(capture.width * capture.height * 4)
    channels = capture.channels
    for pixel in range(capture.width * capture.height):
        base = pixel * channels
        for c in range(3):
            v = values[base + c] if c < channels else 0
            if capture.is_float():
                v = int(max(0.0, min(1.0, v)) * 255.0 + 0.5)
            elif capture.component == UINT16 or capture.component == UINT32:
                v = min(255, v)
            out[pixel * 4 + c] = v
        out[pixel * 4 + 3] = 255
    return bytes(out)


def compare(a, b, tolerance, float_tolerance, max_bad_percent, diff_path):
    if (a.width, a.height) != (b.width, b.height):
        print(f"FAIL size {a.width}x{a.height} vs {b.width}x{b.height}")
        return False
    if (a.format, a.component, a.channels) != (b.format, b.component, b.channels):
        print(f"FAIL format {a.format} vs {b.format}")
        return False

    pixels = a.width * a.height
    bad = 0
    max_diff = 0.0
    sum_diff = 0.0
    diff_image = bytearray(pixels * 4) if diff_path else None

    va, vb = a.values(), b.values()
    if va is None:
        # RAW: byte-exact comparison per pixel.
        bpp = a.bpp
        for pixel in range(pixels):
            same = a.texels[pixel * bpp:(pixel + 1) * bpp] == b.texels[pixel * bpp:(pixel + 1) * bpp]
            if not same:
                bad += 1
                if diff_image is not None:
                    diff_image[pixel * 4:pixel * 4 + 4] = b"\xff\x00\x00\xff"
            elif diff_image is not None:
                diff_image[pixel * 4 + 3] = 255
        max_diff = 1.0 if bad else 0.0
    else:
        channels = a.channels
        is_float = a.is_float()
        for pixel in range(pixels):
            base = pixel * channels
            worst = 0.0
            exceeds = False
            for c in range(channels):
                x, y = va[base + c], vb[base + c]
                if is_float:
                    # NaN never equals itself; treat NaN on one side only as a full mismatch.
                    if x != x or y != y:
                        d = 0.0 if (x != x and y != y) else float("inf")
                    else:
                        d = abs(x - y)
                    limit = float_tolerance + 0.01 * max(abs(x) if x == x else 0.0, abs(y) if y == y else 0.0)
                else:
                    d = abs(x - y)
                    limit = tolerance
                if d > worst:
                    worst = d
                if d > limit:
                    exceeds = True
            if exceeds:
                bad += 1
            if worst != float("inf"):
                sum_diff += worst
            if worst > max_diff:
                max_diff = worst
            if diff_image is not None:
                scale = 255.0 / (16.0 * float_tolerance) if is_float else 255.0 / 16.0
                level = 255 if worst == float("inf") else int(min(255.0, worst * scale))
                diff_image[pixel * 4] = level
                diff_image[pixel * 4 + 1] = 0 if exceeds else level
                diff_image[pixel * 4 + 2] = 0 if exceeds else level
                diff_image[pixel * 4 + 3] = 255

    bad_percent = 100.0 * bad / pixels
    passed = bad_percent <= max_bad_percent
    print(f"{'PASS' if passed else 'FAIL'} bad={bad} ({bad_percent:.4f}%) max={max_diff:.5g} mean={sum_diff / pixels:.5g}")
    if diff_image is not None:
        write_png(diff_path, a.width, a.height, bytes(diff_image))
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("a")
    parser.add_argument("b", nargs="?")
    parser.add_argument("--tolerance", type=float, default=2.0, help="per-channel limit for integer formats (8-bit units)")
    parser.add_argument("--float-tolerance", type=float, default=0.004, help="absolute per-channel limit for float formats (plus 1%% relative)")
    parser.add_argument("--max-bad-percent", type=float, default=0.05)
    parser.add_argument("--diff", help="write a diff PNG here (red = over tolerance)")
    parser.add_argument("--to-png", action="store_true", help="convert A to the PNG path given as B")
    args = parser.parse_args()

    if args.to_png:
        if not args.b:
            parser.error("--to-png needs an output path")
        capture = Capture(args.a)
        write_png(args.b, capture.width, capture.height, to_rgba8(capture))
        return 0

    if not args.b:
        parser.error("two captures are required")
    try:
        a, b = Capture(args.a), Capture(args.b)
    except (OSError, ValueError) as error:
        print(f"FAIL {error}")
        return 1
    return 0 if compare(a, b, args.tolerance, args.float_tolerance, args.max_bad_percent, args.diff) else 1


if __name__ == "__main__":
    sys.exit(main())
