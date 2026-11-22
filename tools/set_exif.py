#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes a minimal EXIF block into JPEG files (for picsel test data).

usage: set_exif.py [--orientation N] [--date "YYYY:MM:DD HH:MM:SS"]
                   [--make MAKE] [--model MODEL] FILE...

Any existing APP1/Exif segment is replaced. Only the tags picsel reads are
written: Make, Model, Orientation and (in the Exif sub-IFD) DateTimeOriginal.
"""
import argparse
import struct
import sys


def build_exif(orientation, date, make, model):
    # Big-endian TIFF. Layout: header(8) | IFD0 | exif IFD | data area.
    ifd0 = []  # (tag, type, count, value_bytes)
    if make:
        ifd0.append((0x010F, 2, make.encode() + b"\0"))
    if model:
        ifd0.append((0x0110, 2, model.encode() + b"\0"))
    if orientation:
        ifd0.append((0x0112, 3, struct.pack(">H", orientation)))
    exif_ifd = []
    if date:
        exif_ifd.append((0x9003, 2, date.encode() + b"\0"))
    if exif_ifd:
        ifd0.append((0x8769, 4, None))  # pointer, patched below
    ifd0.sort(key=lambda e: e[0])

    def ifd_size(entries):
        return 2 + 12 * len(entries) + 4

    ifd0_off = 8
    exif_off = ifd0_off + ifd_size(ifd0)
    data_off = exif_off + (ifd_size(exif_ifd) if exif_ifd else 0)
    data = bytearray()

    def encode(entries):
        out = bytearray(struct.pack(">H", len(entries)))
        for tag, typ, val in entries:
            if val is None:
                val = struct.pack(">I", exif_off)
            count = len(val) if typ == 2 else 1
            if len(val) <= 4:
                out += struct.pack(">HHI", tag, typ, count) + val.ljust(4, b"\0")
            else:
                out += struct.pack(">HHII", tag, typ, count, data_off + len(data))
                data.extend(val)
                if len(data) % 2:
                    data.append(0)
        out += struct.pack(">I", 0)
        return out

    body = encode(ifd0) + (encode(exif_ifd) if exif_ifd else b"")
    tiff = b"MM\0\x2a" + struct.pack(">I", ifd0_off) + body + data
    return b"Exif\0\0" + tiff


def rewrite(path, payload):
    with open(path, "rb") as f:
        jpg = f.read()
    if jpg[:2] != b"\xff\xd8":
        raise SystemExit(f"{path}: not a JPEG")
    pos, segments = 2, []
    while pos + 4 <= len(jpg) and jpg[pos] == 0xFF and jpg[pos + 1] not in (0xDA, 0xD9):
        length = struct.unpack(">H", jpg[pos + 2:pos + 4])[0]
        seg = jpg[pos:pos + 2 + length]
        if not (jpg[pos + 1] == 0xE1 and seg[4:10] == b"Exif\0\0"):
            segments.append(seg)
        pos += 2 + length
    app1 = b"\xff\xe1" + struct.pack(">H", len(payload) + 2) + payload
    # APP0 (JFIF) first if present, then our APP1.
    head = [s for s in segments if s[1] == 0xE0]
    rest = [s for s in segments if s[1] != 0xE0]
    out = b"\xff\xd8" + b"".join(head) + app1 + b"".join(rest) + jpg[pos:]
    with open(path, "wb") as f:
        f.write(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--orientation", type=int, default=0)
    ap.add_argument("--date", default="")
    ap.add_argument("--make", default="")
    ap.add_argument("--model", default="")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    payload = build_exif(a.orientation, a.date, a.make, a.model)
    for f in a.files:
        rewrite(f, payload)


if __name__ == "__main__":
    sys.exit(main())
