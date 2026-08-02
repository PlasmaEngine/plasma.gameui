"""Shared icon/banner renderer for Plasma packages.

There is no rasteriser on this machine and no Pillow, so this writes PNG bytes directly - the
format is just zlib-compressed scanlines with a filter byte each. Everything is drawn at 3x and
box-downsampled, which is where the antialiasing comes from.

A motif is a function (x, y, w, h) -> bool saying whether a point is inside the mark. Everything
else - the rounded-square icon, the gradient field, the banner - is shared, so a new package needs
only a palette and a shape.

Usage: pkgart.py <motif> <outdir> <IconName.png> <BannerName.png>
"""

import math
import os
import struct
import sys
import zlib

SS = 3
ICON = 512
BANNER_W, BANNER_H = 1100, 512


def write_png(path, w, h, rgba):
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)
        raw += rgba[y * stride:(y + 1) * stride]

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as fh:
        fh.write(png)


def mix(c0, c1, t):
    t = 0.0 if t < 0 else (1.0 if t > 1 else t)
    return tuple(c0[i] + (c1[i] - c0[i]) * t for i in range(3))


def rounded_rect(x, y, w, h, radius):
    cx = min(max(x, radius), w - radius)
    cy = min(max(y, radius), h - radius)
    dx, dy = x - cx, y - cy
    return dx * dx + dy * dy <= radius * radius if (dx or dy) else True


def downsample(buf, w, h):
    out = bytearray(w * h * 4)
    n, bw = SS * SS, w * SS
    for y in range(h):
        for x in range(w):
            r = g = b = a = 0
            for sy in range(SS):
                row = (y * SS + sy) * bw
                for sx in range(SS):
                    i = (row + x * SS + sx) * 4
                    r += buf[i]; g += buf[i + 1]; b += buf[i + 2]; a += buf[i + 3]
            o = (y * w + x) * 4
            out[o] = r // n; out[o + 1] = g // n; out[o + 2] = b // n; out[o + 3] = a // n
    return out


# ---- motifs ----------------------------------------------------------------
def terrain_mark(x, y, w, h):
    """A heightfield ridge: three overlapping peaks, filled below the skyline."""
    def ridge(px, base, amp, freq, phase):
        # Triangle wave rather than a sine, so the peaks read as terrain and not water.
        t = (px * freq + phase) % 1.0
        return base - amp * (2.0 * t if t < 0.5 else 2.0 * (1.0 - t))

    for base, amp, freq, phase in ((0.70, 0.34, 1.0 / (w * 0.66), 0.12),
                                   (0.78, 0.26, 1.0 / (w * 0.44), 0.55),
                                   (0.86, 0.18, 1.0 / (w * 0.30), 0.30)):
        if y >= ridge(x, h * base, h * amp, freq, phase):
            return True
    return False


def gamepad_mark(x, y, w, h):
    """A gamepad: a wide rounded body with a d-pad left and two buttons right."""
    cx, cy = w * 0.5, h * 0.52
    bw, bh = w * 0.34, h * 0.20

    # Body: an ellipse widened at the shoulders, with grips flaring down and out.
    ex, ey = (x - cx) / bw, (y - cy) / bh
    body = ex * ex + ey * ey <= 1.0

    for gx in (-1.0, 1.0):
        hx, hy = (x - (cx + gx * w * 0.26)) / (w * 0.12), (y - (cy + h * 0.13)) / (h * 0.17)
        if hx * hx + hy * hy <= 1.0:
            body = True

    if not body:
        return False

    # Cut-outs read as white against the body.
    dx, dy = x - (cx - w * 0.15), y - cy
    arm = h * 0.075
    if (abs(dx) <= arm and abs(dy) <= arm * 0.34) or (abs(dy) <= arm and abs(dx) <= arm * 0.34):
        return False

    for bxo, byo in ((w * 0.115, -h * 0.035), (w * 0.185, h * 0.035)):
        if math.hypot(x - (cx + bxo), y - (cy + byo)) <= h * 0.036:
            return False

    return True


def gameui_mark(x, y, w, h):
    """A UI layout: a panel with a title bar, a sidebar and two content blocks."""
    px, py = w * 0.17, h * 0.20
    pw, ph = w - px * 2.0, h - py * 2.0

    if not (px <= x <= px + pw and py <= y <= py + ph):
        return False

    bar = ph * 0.17
    if y <= py + bar:                                   # title bar
        return True

    gap = ph * 0.055
    side = pw * 0.30
    if px + gap <= x <= px + gap + side and py + bar + gap <= y <= py + ph - gap:
        return True                                     # sidebar

    cx0 = px + gap * 2.0 + side
    cw = px + pw - gap - cx0
    for y0, y1 in ((py + bar + gap, py + bar + gap + (ph - bar - gap * 3.0) * 0.42),
                   (py + bar + gap * 2.0 + (ph - bar - gap * 3.0) * 0.42, py + ph - gap)):
        if cx0 <= x <= cx0 + cw and y0 <= y <= y1:
            return True                                 # content blocks

    return False


MOTIFS = {
    "terrain": (terrain_mark, (0x7A, 0xC1, 0x5E), (0x2C, 0x5A, 0x2E), (0x14, 0x2C, 0x1A), (0xF2, 0xFB, 0xEE)),
    "gamepad": (gamepad_mark, (0x8F, 0xD6, 0x6B), (0x2E, 0x4A, 0x2C), (0x16, 0x22, 0x18), (0xF4, 0xFA, 0xF2)),
    "gameui": (gameui_mark, (0x6E, 0xB6, 0xF5), (0x1E, 0x47, 0x86), (0x0E, 0x1F, 0x3C), (0xF0, 0xF7, 0xFF)),
}


def render(motif_name, w, h, icon):
    mark, light, deep, dark, ink = MOTIFS[motif_name]
    W, H = w * SS, h * SS
    buf = bytearray(W * H * 4)
    radius = W * 0.20

    for y in range(H):
        for x in range(W):
            if icon and not rounded_rect(x + 0.5, y + 0.5, W, H, radius):
                continue

            t = (y / float(H - 1)) if icon else ((x / float(W - 1)) * 0.45 + (y / float(H - 1)) * 0.55)
            col = mix(light, dark, t * 0.95)

            if mark(x + 0.5, y + 0.5, W, H):
                # The mark itself picks up a little of the field so it does not read as flat paint.
                col = mix(ink, deep, (y / float(H - 1)) * 0.35)

            i = (y * W + x) * 4
            buf[i] = int(col[0]); buf[i + 1] = int(col[1]); buf[i + 2] = int(col[2]); buf[i + 3] = 255

    return downsample(buf, w, h)


def main():
    motif, outdir, icon_name, banner_name = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    os.makedirs(outdir, exist_ok=True)

    write_png(os.path.join(outdir, icon_name), ICON, ICON, render(motif, ICON, ICON, True))
    print("%s %dx%d" % (icon_name, ICON, ICON))

    write_png(os.path.join(outdir, banner_name), BANNER_W, BANNER_H,
              render(motif, BANNER_W, BANNER_H, False))
    print("%s %dx%d" % (banner_name, BANNER_W, BANNER_H))


if __name__ == "__main__":
    main()
