#!/usr/bin/env python3
"""Turn the pictures in refs/ into avatars.

An avatar is a box of A x A/2 character cells (square on screen). Every cell is
split into 2 x 2 quarters, so the picture is 2A x A dots, two colours per cell.

  avatars.py          sheet in this terminal: every picture in boxes of 16, 24, 32 and 48 cells
  avatars.py 32       only one box size
  avatars.py build    write avatars.json for render.py

Needs ImageMagick (`magick`).
"""
import base64
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REFS = ROOT / "refs"
BOXES = (16, 24, 32, 48)
QUAD = " ▘▝▀▖▌▞▛"   # index = mask of quarters drawn in the first colour: 1 TL, 2 TR, 4 BL

# Square crop around the face, as fractions of the picture: x0, y0, x1, y1.
# Keyed by the start of the file name.
CROPS = {
    "05497cd1": (0.35, 0.06, 0.72, 0.40),
    "1e9d3c9a": (0.15, 0.03, 0.72, 0.61),
    "317b84fc": (0.23, 0.00, 0.93, 0.70),
    "3ed19a80": (0.17, 0.01, 0.72, 0.37),
    "411a41e7": (0.00, 0.00, 1.00, 0.98),
    "44b55feb": (0.33, 0.00, 0.78, 0.455),
    "4e2ad6e9": (0.45, 0.00, 0.917, 0.467),
    "4faaf51c": (0.25, 0.17, 0.667, 0.536),
    "68503659": (0.286, 0.135, 0.857, 0.572),
    "6d83314a": (0.068, 0.288, 0.747, 0.84),
    "a56e348a": (0.08, 0.00, 0.68, 0.60),
    "ae94f9d2": (0.44, 0.007, 0.89, 0.478),
    "c77365de": (0.298, 0.239, 0.681, 0.527),
    "c9e5bb18": (0.279, 0.00, 0.727, 0.448),
    "d078df99": (0.00, 0.00, 0.84, 0.84),
    "d79ae0e5": (0.156, 0.047, 0.78, 0.67),
    "deba99fe": (0.36, 0.00, 0.927, 0.425),
    "f2755574": (0.00, 0.00, 0.76, 0.76),
    "f6917c8d": (0.13, 0.074, 0.864, 0.743),
    "fb498fed": (0.088, 0.027, 0.903, 0.842),
}


def pictures():
    return sorted(p for p in REFS.iterdir() if p.suffix.lower() in (".jpg", ".jpeg", ".png", ".webp"))


def crop_args(path):
    out = subprocess.run(
        ["magick", str(path), "-auto-orient", "-format", "%w %h", "info:"],
        capture_output=True, text=True, check=True,
    ).stdout.split()
    w, h = int(out[0]), int(out[1])
    box = next((b for k, b in CROPS.items() if path.name.startswith(k)), (0, 0, 1, 1))
    x0, y0, x1, y1 = box
    side = round(min((x1 - x0) * w, (y1 - y0) * h, w, h))
    x = min(max(0, round((x0 + x1) / 2 * w - side / 2)), w - side)
    y = min(max(0, round((y0 + y1) / 2 * h - side / 2)), h - side)
    return ["-auto-orient", "-crop", f"{side}x{side}+{x}+{y}", "+repage"]


def load(path, w, h):
    """Cropped picture squeezed to w x h dots. No sharpening, no colour boost."""
    raw = subprocess.run(
        ["magick", str(path), *crop_args(path), "-filter", "Lanczos", "-resize", f"{w}x{h}!",
         "-depth", "8", "rgb:-"],
        capture_output=True, check=True,
    ).stdout
    return [[tuple(raw[(r * w + c) * 3:(r * w + c) * 3 + 3]) for c in range(w)] for r in range(h)]


# How much closer to the picture a quarter split must be than a plain top/bottom
# split before it is used (sum of squared colour differences over the cell).
# Higher = softer picture, lower = sharper and harsher.
EDGE_GAIN = 3500


def _mean(cols):
    return tuple(sum(c[i] for c in cols) / len(cols) for i in range(3))


def _err(cols, m):
    return sum((c[i] - m[i]) ** 2 for c in cols for i in range(3))


def quad_cell(px):
    """px: colours of the four quarters (TL, TR, BL, BR) -> (mask, first colour, second colour).

    A cell is normally drawn as a plain top half and bottom half, which keeps
    smooth areas smooth. Only where there is a real edge that this would smear
    is the cell split into quarters instead. The bottom-right quarter always
    belongs to the second colour.
    """
    top, bot = _mean(px[:2]), _mean(px[2:])
    e_half = _err(px[:2], top) + _err(px[2:], bot)
    best = None
    for mask in (1, 2, 4, 5, 6, 7):
        a = [px[i] for i in range(4) if (mask >> i) & 1]
        b = [px[i] for i in range(4) if not (mask >> i) & 1]
        ma, mb = _mean(a), _mean(b)
        e = _err(a, ma) + _err(b, mb)
        if best is None or e < best[0]:
            best = (e, mask, ma, mb)
    if e_half - best[0] > EDGE_GAIN:
        _, mask, ma, mb = best
    else:
        mask, ma, mb = 3, top, bot
    return mask, tuple(round(v) for v in ma), tuple(round(v) for v in mb)


def cells(path, box):
    """Avatar as rows of (mask, first colour, second colour), box x box/2 cells."""
    rows = box // 2
    img = load(path, box * 2, rows * 2)
    return [
        [quad_cell([img[2 * r][2 * c], img[2 * r][2 * c + 1], img[2 * r + 1][2 * c], img[2 * r + 1][2 * c + 1]])
         for c in range(box)]
        for r in range(rows)
    ]


def sheet(boxes):
    cols = shutil.get_terminal_size().columns
    pics = pictures()
    out = []
    for box in boxes:
        arts = [cells(p, box) for p in pics]
        per_row = max(1, (cols - 2) // (box + 2))
        out.append(f"\x1b[0;1m  {box}×{box // 2}\x1b[0m\n\n")
        for start in range(0, len(arts), per_row):
            chunk = arts[start:start + per_row]
            for r in range(box // 2):
                line = ["  "]
                for art in chunk:
                    for mask, fg, bg in art[r]:
                        line.append("\x1b[38;2;%d;%d;%d;48;2;%d;%d;%dm%s" % (*fg, *bg, QUAD[mask]))
                    line.append("\x1b[0m  ")
                out.append("".join(line) + "\n")
            labels = "".join(f"{start + i + 1:<{box + 2}d}" for i in range(len(chunk)))
            out.append("  \x1b[2m" + labels + "\x1b[0m\n\n")
    sys.stdout.write("".join(out))


def build():
    """avatars.json: per picture and box size, base64 of 7 bytes per cell (mask, rgb, rgb)."""
    data = {}
    for p in pictures():
        entry = {}
        for box in BOXES:
            buf = bytearray()
            for row in cells(p, box):
                for mask, fg, bg in row:
                    buf += bytes((mask, *fg, *bg))
            entry[str(box)] = base64.b64encode(bytes(buf)).decode()
        data[p.name[:8]] = entry
    dest = Path(__file__).resolve().parent / "avatars.json"
    dest.write_text(json.dumps(data, separators=(",", ":")), encoding="utf-8")
    print(f"{dest}: {len(data)} avatars")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "build":
        build()
    elif len(sys.argv) > 1:
        sheet((int(sys.argv[1]),))
    else:
        sheet(BOXES)
