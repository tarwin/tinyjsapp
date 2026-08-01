#!/usr/bin/env python3
"""xwd dump -> PNG. Lets an X11 window (incl. XWayland) actually be looked at."""
import struct, sys
from PIL import Image

def load(path):
    b = open(path, 'rb').read()
    f = struct.unpack('>25I', b[:100])
    (hsize, ver, pfmt, depth, w, h, xoff, border, bunit, bbo, bpad, bpp,
     bpl, vclass, rmask, gmask, bmask, brgb, cmapent, ncolors,
     ww, wh, wx, wy, wbw) = f
    if ver != 7:
        raise SystemExit('not an xwd v7 file (version=%d)' % ver)
    off = hsize + ncolors * 12
    data = b[off:]
    img = Image.new('RGBA', (w, h))
    px = img.load()
    def shift(mask):
        s = 0
        while mask and not (mask & 1):
            mask >>= 1; s += 1
        return s
    rs, gs, bs = shift(rmask), shift(gmask), shift(bmask)
    for y in range(h):
        row = data[y * bpl:(y + 1) * bpl]
        for x in range(w):
            if bpp == 32:
                v = struct.unpack_from('<I', row, x * 4)[0]
                a = 255 if depth == 24 else (v >> 24) & 0xFF
            elif bpp == 24:
                c = row[x * 3:x * 3 + 3]
                v = c[0] | (c[1] << 8) | (c[2] << 16); a = 255
            else:
                raise SystemExit('unhandled bpp %d' % bpp)
            px[x, y] = (((v & rmask) >> rs) & 0xFF, ((v & gmask) >> gs) & 0xFF,
                        ((v & bmask) >> bs) & 0xFF, a)
    return img, dict(depth=depth, bpp=bpp, w=w, h=h)

if __name__ == '__main__':
    img, meta = load(sys.argv[1])
    out = sys.argv[2]
    if len(sys.argv) > 3 and sys.argv[3] == 'flatten':
        bg = Image.new('RGB', img.size, (255, 0, 255))  # magenta = "was transparent"
        bg.paste(img, mask=img.split()[3])
        bg.save(out)
    else:
        img.convert('RGB').save(out)
    print('%s %dx%d depth=%d bpp=%d' % (out, meta['w'], meta['h'], meta['depth'], meta['bpp']))
