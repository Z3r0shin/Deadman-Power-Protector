# Turns tools/out/*.pbm (P1) into 4x PNGs that look like the OLED (light-blue pixels on near-black), and one contact
# sheet of all of them. Standard library only.
import glob, os, struct, sys, zlib

SCALE = 4
ON, OFF, GAP = (170, 220, 255), (8, 10, 14), (40, 44, 52)


def read_pbm(p):
    t = open(p).read().split()
    w, h = int(t[1]), int(t[2])
    bits = "".join(t[3:])
    return w, h, [[bits[y * w + x] == "1" for x in range(w)] for y in range(h)]


def png(path, rows):
    h, w = len(rows), len(rows[0])
    raw = b"".join(b"\x00" + bytes(c for px in r for c in px) for r in rows)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def scaled(pix):
    out = []
    for r in pix:
        line = []
        for v in r:
            line += [ON if v else OFF] * (SCALE - 1) + [OFF]
        for _ in range(SCALE - 1):
            out.append(line)
        out.append([OFF] * len(line))
    return out


d = sys.argv[1] if len(sys.argv) > 1 else "out"
files = sorted(glob.glob(os.path.join(d, "*.pbm")))
imgs = []
for f in files:
    w, h, pix = read_pbm(f)
    s = scaled(pix)
    png(f[:-4] + ".png", s)
    imgs.append(s)
# Contact sheet, 3 per row.
if imgs:
    cw, ch, cols = len(imgs[0][0]), len(imgs[0]), 3
    rows_n = (len(imgs) + cols - 1) // cols
    sheet = [[GAP] * (cols * (cw + 8) + 8) for _ in range(rows_n * (ch + 8) + 8)]
    for i, im in enumerate(imgs):
        ox, oy = 8 + (i % cols) * (cw + 8), 8 + (i // cols) * (ch + 8)
        for y in range(ch):
            sheet[oy + y][ox:ox + cw] = im[y]
    png(os.path.join(d, "sheet.png"), sheet)
print(len(files), "rendered")
