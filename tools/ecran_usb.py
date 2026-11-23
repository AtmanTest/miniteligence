#!/usr/bin/env python3
"""ecran_usb.py — recupere l'ecran REEL du BATMino par le cable USB.

    ./tools/ecran_usb.py sortie.png [page]

Envoie "p <page>" puis "s" sur la console USB, lit la ligne "FB <hexa>" et
ecrit un PNG 200x200. Aucun wifi necessaire : c'est la dalle qui parle.

Le port est /dev/ttyACM0 par defaut (BATMino branche en USB).
"""
import os
import subprocess
import sys
import time

import serial

PORT = os.environ.get("PORT", "/dev/ttyACM0")
HEX_LEN = 200 * 25 * 2


def lit_ecran(p, page=None, delai=1.8):
    if page is not None:
        p.write(("p %d\n" % page).encode())
        time.sleep(delai)
    p.reset_input_buffer()
    p.write(b"s\n")
    t0 = time.time()
    buf = b""
    while time.time() - t0 < 4.0:
        buf += p.read(8192)
        i = buf.find(b"FB ")
        if i >= 0 and len(buf) >= i + 3 + HEX_LEN:
            return buf[i + 3:i + 3 + HEX_LEN].decode("ascii")
    return None


def main():
    sortie = sys.argv[1] if len(sys.argv) > 1 else "ecran.png"
    page = int(sys.argv[2]) if len(sys.argv) > 2 else None
    p = serial.Serial(PORT, 115200, timeout=0.2)
    time.sleep(0.4)
    hexa = lit_ecran(p, page)
    p.close()
    if not hexa:
        print("pas de reponse de la carte", file=sys.stderr)
        return 1
    fb = os.path.join(os.path.dirname(os.path.abspath(sortie)) or ".", "ecran.fb")
    with open(fb, "w") as f:
        f.write(hexa)
    subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), "fb2png.py"),
                    fb, sortie, "3"], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
