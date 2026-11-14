#!/usr/bin/env python3
"""Generate the installer artwork for whysho SY1.

The look follows whysho.com ("signal paper": riso inks on chart-recorder
paper) and the plugin's Paper theme. Outputs are committed, so this only needs
re-running when the design changes:

    python3 packaging/art/make_art.py

Needs Pillow and two fonts (a heavy grotesque + a monospace); the defaults are
Inter Display Black and JetBrains Mono, overridable with SY1_FONT_DISPLAY /
SY1_FONT_MONO (paths to .ttf/.otf files).
"""
import math
import os
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

PAPER, PAPER_HI = (241, 236, 224), (248, 244, 234)
INK, INK_SOFT = (27, 24, 19), (90, 82, 67)
PINK, BLUE, YELLOW = (255, 72, 176), (0, 120, 191), (255, 232, 0)
NIGHT = (22, 20, 17)

ROOT = Path(__file__).resolve().parents[1]
SS = 4  # supersampling factor


def _font_path(env, family):
    if os.environ.get(env):
        return os.environ[env]
    out = subprocess.run(["fc-match", "-f", "%{file}", family], capture_output=True, text=True)
    return out.stdout.strip()


F_DISPLAY = _font_path("SY1_FONT_DISPLAY", "Inter Display:weight=black")
F_MONO = _font_path("SY1_FONT_MONO", "JetBrains Mono:weight=medium")


def font(path, px):
    return ImageFont.truetype(path, max(1, int(round(px))))


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def grid(d, w, h, bg, ink, decade, row):
    """Log-frequency grid, one decade per tile, like the site background."""
    x0 = 0.0
    while x0 < w:
        for i in range(1, 10):
            x = x0 + decade * math.log10(i)
            if x < w:
                d.line([(x, 0), (x, h)], fill=mix(bg, ink, 0.30 if i == 1 else 0.12), width=max(1, SS // 2))
        x0 += decade
    y = 0.0
    k = 0
    while y < h:
        d.line([(0, y), (w, y)], fill=mix(bg, ink, 0.30 if k % 4 == 0 else 0.12), width=max(1, SS // 2))
        y += row
        k += 1


def mark(d, x, y, px, ink, shadow, text="SY1"):
    """The product code: heavy type with a misregistered riso shadow."""
    f = font(F_DISPLAY, px)
    off = px * 0.045
    d.text((x + off, y + off), text, font=f, fill=shadow)
    d.text((x, y), text, font=f, fill=ink)
    return d.textbbox((x, y), text, font=f)


def tracked(d, x, y, text, f, fill, tracking):
    for ch in text:
        d.text((x, y), ch, font=f, fill=fill)
        x += d.textlength(ch, font=f) + tracking
    return x


def saw(d, x, y, w, h, cycles, ink, fill):
    """A sawtooth trace over an ink-wash fill: the synth's signature shape."""
    pts = []
    for c in range(cycles):
        cx = x + w * c / cycles
        pts += [(cx, y + h), (cx + w / cycles, y)]
    pts.append((x + w, y + h))
    d.polygon(pts + [(x + w, y + h), (x, y + h)], fill=fill)
    d.line(pts, fill=ink, width=max(2, int(SS * 1.2)), joint="curve")


def sidebar(w, h, dark=False):
    """Tall panel: Inno Setup wizard image / base of the macOS background."""
    W, H = w * SS, h * SS
    bg, ink, soft = (NIGHT, PAPER, mix(NIGHT, PAPER, 0.6)) if dark else (PAPER, INK, INK_SOFT)
    im = Image.new("RGB", (W, H), bg)
    d = ImageDraw.Draw(im)
    u = W / 164.0  # layout unit: 1 px of the 164-wide design
    grid(d, W, H, bg, PINK, 90 * u, 18 * u)

    m = 14 * u
    d.ellipse([W - m - 22 * u, m, W - m, m + 22 * u], fill=YELLOW)
    tracked(d, m, m + 4 * u, "whysho", font(F_MONO, 10 * u), ink, 0.6 * u)

    box = mark(d, m - 2 * u, 46 * u, 62 * u, ink, PINK)
    tracked(d, m, box[3] + 10 * u, "SYNTHESIZER", font(F_MONO, 7.5 * u), soft, 1.5 * u)
    d.line([(m, box[3] + 26 * u), (W - m, box[3] + 26 * u)], fill=ink, width=max(1, SS // 2))

    saw(d, m, H - 92 * u, W - 2 * m, 46 * u, 3, ink, mix(bg, BLUE, 0.55))
    d.line([(m, H - 46 * u), (W - m, H - 46 * u)], fill=ink, width=max(1, SS // 2))
    tracked(d, m, H - 34 * u, "FIG. 1 — SAW", font(F_MONO, 6.5 * u), soft, 1.0 * u)
    tracked(d, m, H - 22 * u, "whysho.com", font(F_MONO, 6.5 * u), ink, 0.4 * u)
    return im.resize((w, h), Image.LANCZOS)


def badge(size, with_grid=True):
    """Square mark: wizard header image and the app / installer icon."""
    S = size * SS
    im = Image.new("RGB", (S, S), PAPER)
    d = ImageDraw.Draw(im)
    if with_grid:
        grid(d, S, S, PAPER, PINK, S * 0.62, S / 6.0)
    px = S * 0.46
    f = font(F_DISPLAY, px)
    l, t, r, b = d.textbbox((0, 0), "SY1", font=f)
    mark(d, (S - (r - l)) / 2 - l, (S - (b - t)) / 2 - t - S * 0.03, px, INK, PINK)
    d.rectangle([S * 0.14, S * 0.80, S * 0.86, S * 0.80 + max(SS, S * 0.03)], fill=INK)
    return im.resize((size, size), Image.LANCZOS)


def icon_image(size):
    """Icon with rounded corners on transparency; no grid at tiny sizes."""
    base = badge(size, with_grid=size >= 64).convert("RGBA")
    mask = Image.new("L", (size * SS, size * SS), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, size * SS - 1, size * SS - 1], radius=size * SS * 0.18, fill=255)
    base.putalpha(mask.resize((size, size), Image.LANCZOS))
    return base


def mac_background(w, h, dark):
    """productbuild backdrop. Installer.app lists its steps over the upper
    left, so that area stays plain grid and the mark sits at the bottom."""
    bg, ink, soft = (NIGHT, PAPER, mix(NIGHT, PAPER, 0.6)) if dark else (PAPER, INK, INK_SOFT)
    side_w = 170
    W, H = side_w * SS, h * SS
    im = Image.new("RGB", (W, H), bg)
    d = ImageDraw.Draw(im)
    u = float(SS)
    grid(d, W, H, bg, PINK, 93 * u, 18 * u)
    m = 16 * u
    box = mark(d, m - 2 * u, H - 112 * u, 50 * u, ink, PINK)
    d.line([(m, box[3] + 12 * u), (W - m, box[3] + 12 * u)], fill=ink, width=max(1, SS // 2))
    tracked(d, m, box[3] + 20 * u, "whysho", font(F_MONO, 9 * u), ink, 0.5 * u)
    tracked(d, m, box[3] + 34 * u, "SYNTHESIZER", font(F_MONO, 6.5 * u), soft, 1.3 * u)
    d.ellipse([W - m - 14 * u, box[3] + 20 * u, W - m, box[3] + 34 * u], fill=YELLOW)
    out = Image.new("RGB", (w, h), bg)
    out.paste(im.resize((side_w, h), Image.LANCZOS), (0, 0))
    ImageDraw.Draw(out).line([(side_w, 0), (side_w, h)], fill=mix(bg, ink, 0.25), width=1)
    return out


def main():
    win, mac = ROOT / "windows", ROOT / "macos"
    # Inno Setup picks the best match for the display scale from each list.
    for scale in (1, 2):
        sidebar(164 * scale, 314 * scale).save(win / f"wizard-large-{scale}x.bmp")
        badge(55 * scale).save(win / f"wizard-small-{scale}x.bmp")
    icon_image(256).save(win / "setup.ico", sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
    icon_image(1024).save(ROOT / "icon.png")
    mac_background(620, 418, dark=False).save(mac / "background.png")
    mac_background(620, 418, dark=True).save(mac / "background-dark.png")
    print("wrote installer artwork under", ROOT)


if __name__ == "__main__":
    main()
