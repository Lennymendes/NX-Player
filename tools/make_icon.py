#!/usr/bin/env python3
"""NX Player icon: dark tile, emerald -> cyan ring and a stylised 1-bit / PCM waveform. Requires Pillow."""
import math, sys, os
from PIL import Image, ImageDraw, ImageFilter

def lerp(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

def make(size):
    S = size * 4
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = int(S * 0.22)
    d.rounded_rectangle([0, 0, S - 1, S - 1], radius=r, fill=(10, 12, 14, 255))
    em, cy = (0, 245, 160), (0, 217, 245)
    # glow
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    g = ImageDraw.Draw(glow)
    c, R, w = S / 2, S * 0.33, S * 0.055
    for i in range(360):
        t = i / 360
        col = lerp(em, cy, 0.5 - 0.5 * math.cos(2 * math.pi * t))
        a0 = math.radians(i - 90); a1 = math.radians(i - 88.5)
        g.arc([c - R, c - R, c + R, c + R], math.degrees(a0), math.degrees(a1), fill=col + (255,), width=int(w))
    blurred = glow.filter(ImageFilter.GaussianBlur(S * 0.03))
    img = Image.alpha_composite(img, blurred)
    img = Image.alpha_composite(img, glow)
    d = ImageDraw.Draw(img)
    # waveform: left half PCM steps, right half smooth sine
    pts = []
    for k in range(200):
        x = c - R * 0.62 + (2 * R * 0.62) * k / 199
        u = (k / 199) * 2 * math.pi * 1.5
        y = c - R * 0.36 * math.sin(u)
        if k < 100:
            y = c - R * 0.36 * math.sin(round(u * 3) / 3)
        pts.append((x, y))
    for k in range(len(pts) - 1):
        t = k / (len(pts) - 1)
        d.line([pts[k], pts[k + 1]], fill=lerp(em, cy, t) + (255,), width=int(S * 0.028))
    return img.resize((size, size), Image.LANCZOS)

out = sys.argv[1] if len(sys.argv) > 1 else "Resources"
os.makedirs(out, exist_ok=True)
for s in (1024, 256):
    make(s).save(os.path.join(out, f"AppIcon_{s}.png"))
print("icons written to", out)
