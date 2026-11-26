#!/usr/bin/env python3
"""Convert raw RGB565 dumps (466x466) to PNG, and optionally tile several into a contact sheet.
usage: view.py out.png in1.rgb565 [in2.rgb565 ...] [--scale 0.5] [--cols 4]"""
import sys
import numpy as np
from PIL import Image, ImageDraw

args = sys.argv[1:]
scale, cols = 1.0, 4
out = args.pop(0)
files = []
while args:
    a = args.pop(0)
    if a == "--scale": scale = float(args.pop(0))
    elif a == "--cols": cols = int(args.pop(0))
    else: files.append(a)

def load(p):
    d = np.fromfile(p, dtype="<u2").reshape(466, 466).astype(np.uint32)
    r = ((d >> 11) & 31) * 255 // 31
    g = ((d >> 5) & 63) * 255 // 63
    b = (d & 31) * 255 // 31
    img = np.stack([r, g, b], -1).astype(np.uint8)
    im = Image.fromarray(img)
    if scale != 1.0:
        im = im.resize((int(466 * scale), int(466 * scale)), Image.LANCZOS)
    return im

ims = [load(f) for f in files]
w, h = ims[0].size
rows = (len(ims) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (w + 6), rows * (h + 22)), (30, 30, 34))
d = ImageDraw.Draw(sheet)
for i, (im, f) in enumerate(zip(ims, files)):
    x, y = (i % cols) * (w + 6), (i // cols) * (h + 22)
    sheet.paste(im, (x, y + 18))
    d.text((x + 4, y + 3), f.split("/")[-1].replace(".rgb565", "")[-30:], fill=(200, 200, 210))
sheet.save(out)
print(out, sheet.size)
