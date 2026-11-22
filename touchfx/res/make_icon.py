"""Generates the TouchFX app icon (touchfx.ico + touchfx.png). Run: python3 make_icon.py"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

S = 1024  # supersampled canvas
HERE = os.path.dirname(os.path.abspath(__file__))


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def glow_layer(draw_fn, blur):
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    draw_fn(ImageDraw.Draw(layer))
    return layer.filter(ImageFilter.GaussianBlur(blur))


def add(base, layer):
    """Additive blend of an RGBA layer onto an RGB base."""
    b = np.asarray(base).astype(np.float32)
    l = np.asarray(layer).astype(np.float32)
    a = l[..., 3:4] / 255.0
    out = np.clip(b + l[..., :3] * a, 0, 255).astype(np.uint8)
    return Image.fromarray(out, "RGB")


# ---- background: dark tile with a warm radial glow near the touch point ----
yy, xx = np.mgrid[0:S, 0:S].astype(np.float32)
cx, cy = S * 0.5, S * 0.70
d = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2) / S
bg = np.zeros((S, S, 3), np.float32)
bg[...] = (16, 15, 24)
g = np.exp(-(d / 0.32) ** 2)[..., None]
bg += g * np.array([90, 32, 10], np.float32)
img = Image.fromarray(np.clip(bg, 0, 255).astype(np.uint8), "RGB")

# ---- laser fan from the touch point ----
beams = 7
cols = [(255, 120, 20), (255, 60, 140), (220, 60, 255), (60, 200, 255), (220, 60, 255), (255, 60, 140), (255, 120, 20)]
spread = math.radians(110)
L = S * 0.85


def beam_lines(width, alpha_scale):
    def fn(dr):
        for i in range(beams):
            a = -math.pi / 2 + (i / (beams - 1) - 0.5) * spread
            x1, y1 = cx + math.cos(a) * L, cy + math.sin(a) * L
            c = cols[i]
            dr.line([(cx, cy), (x1, y1)], fill=c + (int(255 * alpha_scale),), width=width)
    return fn


img = add(img, glow_layer(beam_lines(46, 0.55), 30))  # haze
img = add(img, glow_layer(beam_lines(16, 0.9), 6))    # beam
img = add(img, glow_layer(lambda dr: [dr.line([(cx, cy), (cx + math.cos(-math.pi / 2 + (i / (beams - 1) - 0.5) * spread) * L,
                                                         cy + math.sin(-math.pi / 2 + (i / (beams - 1) - 0.5) * spread) * L)],
                                                fill=(255, 245, 235, 230), width=5) for i in range(beams)], 1))  # hot core

# ---- touch ripples ----
def ripples(dr):
    for r, a in ((120, 200), (190, 120)):
        dr.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(255, 170, 80, a), width=14)


img = add(img, glow_layer(ripples, 8))
img = add(img, glow_layer(ripples, 1))

# ---- hot touch point ----
img = add(img, glow_layer(lambda dr: dr.ellipse([cx - 110, cy - 110, cx + 110, cy + 110], fill=(255, 130, 40, 255)), 50))
img = add(img, glow_layer(lambda dr: dr.ellipse([cx - 62, cy - 62, cx + 62, cy + 62], fill=(255, 235, 210, 255)), 10))

# ---- sparkle (4-point star) top right ----
def star(dr, x, y, r, w, col):
    dr.polygon([(x, y - r), (x + w, y - w), (x + r, y), (x + w, y + w), (x, y + r), (x - w, y + w), (x - r, y), (x - w, y - w)], fill=col)


img = add(img, glow_layer(lambda dr: star(dr, S * 0.80, S * 0.20, 120, 22, (120, 220, 255, 255)), 18))
img = add(img, glow_layer(lambda dr: star(dr, S * 0.80, S * 0.20, 105, 14, (255, 255, 255, 255)), 1))

# ---- rounded tile mask ----
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * 0.2), fill=255)
tile = img.convert("RGBA")
tile.putalpha(mask)
# thin border
border = Image.new("RGBA", (S, S), (0, 0, 0, 0))
ImageDraw.Draw(border).rounded_rectangle([6, 6, S - 7, S - 7], radius=int(S * 0.2) - 6, outline=(255, 140, 60, 90), width=10)
tile = Image.alpha_composite(tile, border)

big = tile.resize((256, 256), Image.LANCZOS)
big.save(os.path.join(HERE, "touchfx.png"))
big.save(os.path.join(HERE, "touchfx.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("wrote touchfx.ico / touchfx.png")
