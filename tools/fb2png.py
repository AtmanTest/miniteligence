#!/usr/bin/env python3
"""fb2png.py - framebuffer 1 bit du robot -> PNG lisible.

    python3 tools/fb2png.py /tmp/tableau.fb /tmp/tableau.png [echelle]

Le framebuffer fait 200x200 pixels, 1 bit par pixel, 25 octets par ligne,
bit de poids fort = pixel de gauche, 1 = blanc, 0 = noir.
"""
import sys
import zlib
import struct

W, H = 200, 200


def lit(path):
    """accepte le binaire (5000 octets) et l'hexadecimal de /ecran?raw=1"""
    with open(path, "rb") as f:
        d = f.read()
    try:
        txt = d.decode("ascii").strip()
        if len(txt) >= H * 25 * 2 and all(c in "0123456789abcdefABCDEF" for c in txt[:400]):
            d = bytes.fromhex(txt[:H * 25 * 2])
    except (UnicodeDecodeError, ValueError):
        pass
    if len(d) < H * 25:
        raise SystemExit("%s : %d octets, attendu %d" % (path, len(d), H * 25))
    return d


def png(path, fb, scale=3):
    Wp, Hp = W * scale, H * scale
    raw = bytearray()
    for y in range(Hp):
        raw.append(0)
        row = fb[(y // scale) * 25:(y // scale) * 25 + 25]
        for x in range(Wp):
            bit = (row[(x // scale) >> 3] >> (7 - ((x // scale) & 7))) & 1
            raw.append(255 if bit else 0)

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", Wp, Hp, 8, 0, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else "/tmp/tableau.fb"
    dst = sys.argv[2] if len(sys.argv) > 2 else "/tmp/tableau.png"
    echelle = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    fb = lit(src)
    png(dst, fb, echelle)
    print(dst)
