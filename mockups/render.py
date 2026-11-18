#!/usr/bin/env python3
"""POHER interface mockups (truecolor ANSI).

  render.py [V]            table fitted to this terminal, view V (default 2); resize the
                           window to see it scale, press Tab to open or close the side
                           panel, any other key to quit
  render.py deck           the whole deck
  render.py save           rewrite the .ans files next to this script
  render.py plain V W H    view V at W x H without colour (grid check)
  render.py ppm V W H F    view V at W x H as a picture file (text shown as blocks)

Views:
  2  the table (main view)
  3  the table with the side panel open: one big portrait and the chat

Avatars come from avatars.json (made by `avatars.py build` from refs/).
Without that file two placeholder pictures are used.
"""
import base64
import json
import math
import os
import shutil
import sys
from pathlib import Path

# Per view: rows of (min width, min height, card scale, avatar box at the table,
# portrait box in the side panel or 0). The first row that fits wins; a window
# smaller than the last row gets the "make the window bigger" screen.
VARIANTS = {
    2: ((216, 62, 2, 32, 0), (178, 46, 2, 24, 0), (110, 34, 1, 16, 0)),
    3: ((224, 60, 2, 24, 48), (190, 44, 2, 16, 32), (138, 34, 1, 16, 24)),
}

BG = (14, 17, 22)
TXT = (222, 224, 228)
DIM = (112, 118, 130)
YEL = (242, 200, 72)
WHITE = (250, 250, 250)
BAR = (24, 28, 37)

W = H = 0
grid = []


# ---------------------------------------------------------------- canvas
def canvas(w, h, bg=BG):
    global W, H, grid
    W, H = w, h
    grid = [[[" ", TXT, bg, False] for _ in range(w)] for _ in range(h)]


def put(x, y, s, fg=None, bg=None, bold=False):
    for i, ch in enumerate(s):
        xx = x + i
        if 0 <= xx < W and 0 <= y < H:
            c = grid[y][xx]
            c[0] = ch
            if fg is not None:
                c[1] = fg
            if bg is not None:
                c[2] = bg
            c[3] = bold


def fill(x, y, w, h, bg):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            if 0 <= xx < W and 0 <= yy < H:
                c = grid[yy][xx]
                c[0], c[2], c[3] = " ", bg, False


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def blit(x, y, bitmap, col, bg, k=1, ox=0, oy=0):
    """One-colour bitmap ('#' = ink), scaled k times, drawn with half blocks."""
    ink = set()
    for gy, line in enumerate(bitmap):
        for gx, ch in enumerate(line):
            if ch == "#":
                for dy in range(k):
                    for dx in range(k):
                        ink.add((ox + gx * k + dx, oy + gy * k + dy))
    if not ink:
        return
    for r in range(max(p[1] for p in ink) // 2 + 1):
        for cx in range(max(p[0] for p in ink) + 1):
            top = (cx, 2 * r) in ink
            bot = (cx, 2 * r + 1) in ink
            if top and bot:
                fill(x + cx, y + r, 1, 1, col)
            elif top:
                put(x + cx, y + r, "▀", col, bg)
            elif bot:
                put(x + cx, y + r, "▄", col, bg)


QUAD = " ▘▝▀▖▌▞▛"   # index = mask of quarters in the first colour: 1 TL, 2 TR, 4 BL


def blit_cells(x, y, rows, dim_to=None):
    """Picture made of quarter-cell dots: rows of (mask, first colour, second colour)."""
    for r, row in enumerate(rows):
        for c, (mask, fg, bg) in enumerate(row):
            if dim_to is not None:
                fg, bg = lerp(fg, dim_to, 0.65), lerp(bg, dim_to, 0.65)
            if mask == 0:
                fill(x + c, y + r, 1, 1, bg)
            else:
                put(x + c, y + r, QUAD[mask], fg, bg)


def frame(x, y, w, h, col):
    """One-pixel outline around a w x h block."""
    put(x - 1, y - 1, "▄" * (w + 2), col)
    put(x - 1, y + h, "▀" * (w + 2), col)
    fill(x - 1, y, 1, h, col)
    fill(x + w, y, 1, h, col)


def emit(addressed=False):
    out = []
    for y, row in enumerate(grid):
        last = None
        line = [f"\x1b[{y + 1};1H"] if addressed else []
        for ch, fg, bg, bold in row:
            key = (fg, bg, bold)
            if key != last:
                line.append(
                    "\x1b[0;%s38;2;%d;%d;%d;48;2;%d;%d;%dm"
                    % ("1;" if bold else "", *fg, *bg)
                )
                last = key
            line.append(ch)
        line.append("\x1b[0m")
        out.append("".join(line))
    return "".join(out) if addressed else "\n".join(out) + "\n"


def plain():
    return ["".join(c[0] for c in row) for row in grid]


# ---------------------------------------------------------------- cards
CARD = (246, 244, 236)
SUIT_COL = {
    "♠": (24, 25, 31),
    "♥": (208, 36, 50),
    "♦": (30, 96, 216),
    "♣": (16, 132, 62),
}
SUIT_ON_DARK = {
    "♠": (200, 204, 212),
    "♥": (232, 70, 82),
    "♦": (84, 146, 244),
    "♣": (64, 190, 110),
}
BACK = (38, 66, 146)
BACK_HI = (96, 134, 226)
SHADOW = (11, 60, 35)
SLOT = (15, 78, 46)

FONT = {
    "A": [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "K": ["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"],
    "Q": [".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"],
    "J": ["..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."],
    "10": ["#.###", "#.#.#", "#.#.#", "#.#.#", "#.#.#", "#.#.#", "#.###"],
    "9": [".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."],
    "8": [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
    "7": ["#####", "....#", "...#.", "..#..", "..#..", "..#..", "..#.."],
    "6": [".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."],
    "5": ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
    "4": ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."],
    "3": [".###.", "#...#", "....#", "..##.", "....#", "#...#", ".###."],
    "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
}
RANKS = ["A", "K", "Q", "J", "10", "9", "8", "7", "6", "5", "4", "3", "2"]
for _k, _g in FONT.items():
    assert len(_g) == 7 and all(len(r) == 5 for r in _g), _k


def card_size(k):
    return (7, 5) if k == 1 else (12, 9)


def card(x, y, rank, suit, k=1, shadow=True):
    """Rank is the 5x7 px glyph scaled k times; the suit sign sits under it."""
    cw, ch = card_size(k)
    col = SUIT_COL[suit]
    fill(x, y, cw, ch, CARD)
    blit(x, y, FONT[rank], col, CARD, k, ox=1, oy=1)
    if k == 1:
        put(x + 3, y + ch - 1, suit, col, CARD, True)
    else:
        put(x + 1, y + ch - 1, suit, col, CARD, True)
        put(x + cw - 2, y + ch - 1, suit, col, CARD, True)
    if shadow:
        put(x + cw, y, "▄", SHADOW)
        fill(x + cw, y + 1, 1, ch - 1, SHADOW)
        put(x + 1, y + ch, "▀" * cw, SHADOW)


def backs_size(k):
    return (7, 2) if k == 1 else (14, 4)


def backs(x, y, k=1):
    bw, bh, gap = (3, 2, 1) if k == 1 else (6, 4, 2)
    for dx in (0, bw + gap):
        fill(x + dx, y, bw, bh, BACK)
        for r in range(bh):
            put(x + dx, y + r, "▒" * bw, BACK_HI, BACK)


# ---------------------------------------------------------------- seats
P_BG = (31, 36, 46)
P_ACT = (64, 55, 24)
P_FOLD = (21, 24, 31)
BOT_C = (74, 86, 108)


def load_avatars():
    """avatars.json -> {picture: {box: rows of (mask, first colour, second colour)}}."""
    path = Path(__file__).resolve().parent / "avatars.json"
    if not path.exists():
        return {}
    out = {}
    for key, boxes in json.loads(path.read_text(encoding="utf-8")).items():
        out[key] = {}
        for box, b64 in boxes.items():
            a, data = int(box), base64.b64decode(b64)
            out[key][a] = [
                [
                    (data[o], tuple(data[o + 1:o + 4]), tuple(data[o + 4:o + 7]))
                    for o in range((r * a) * 7, (r * a + a) * 7, 7)
                ]
                for r in range(a // 2)
            ]
    return out


AVATARS = load_avatars()

# Placeholders for when avatars.json is missing (12 x 12 px).
AV_PAL = {
    "s": (238, 188, 140), "h": (84, 52, 30), "k": (22, 22, 28), "m": (170, 60, 66),
    "g": (156, 166, 182), "d": (40, 46, 58), "y": (250, 212, 84), "r": (232, 72, 72),
}
AV_FACE = [
    "...hhhhhh...", "..hhhhhhhh..", ".hhhssssshh.", ".hssssssssh.",
    ".kkkkkkkkkk.", ".skkksskkks.", ".ssssssssss.", ".ssssssssss.",
    "..ssmmmmss..", "..ssssssss..", "...ssssss...", "............",
]
AV_BOT = [
    ".....rr.....", ".....gg.....", "..gggggggg..", ".gggggggggg.",
    ".gyyggggyyg.", ".gyyggggyyg.", ".gggggggggg.", ".ggddddddgg.",
    ".gggggggggg.", "..gggggggg..", "...gg..gg...", "............",
]


def avatar_cells(key, a, bot, color):
    if key in AVATARS:
        return AVATARS[key][a]
    pal = dict(AV_PAL)
    pal["."] = color
    art = AV_BOT if bot else AV_FACE

    def px(c, y):
        return pal[art[y * 12 // a][c * 12 // a]]

    return [[(3, px(c, 2 * r), px(c, 2 * r + 1)) for c in range(a)] for r in range(a // 2)]


def seat(ax, ay, ix, iy, iw, a, name, stack, status, key, color, bot=False, state="normal"):
    """Avatar (a x a/2 cells) at (ax, ay); three-line info block at (ix, iy), iw wide."""
    fold = state == "fold"
    blit_cells(ax, ay, avatar_cells(key, a, bot, color), P_FOLD if fold else None)
    if state == "active":
        frame(ax, ay, a, a // 2, YEL)

    pbg = {"normal": P_BG, "active": P_ACT, "fold": P_FOLD}[state]
    name_fg = {"normal": TXT, "active": YEL, "fold": DIM}[state]
    fill(ix, iy, iw, 3, pbg)
    put(ix + 1, iy, name, name_fg, pbg, True)
    if bot:
        tag_bg = lerp(BOT_C, pbg, 0.6) if fold else BOT_C
        put(ix + 2 + len(name), iy, " BOT ", DIM if fold else WHITE, tag_bg, True)
    put(ix + 1, iy + 1, stack, DIM if fold else TXT, pbg)
    if state == "active":
        bar_w = iw - 2
        done = round(bar_w * 0.6)
        fill(ix + 1, iy + 2, done, 1, YEL)
        fill(ix + 1 + done, iy + 2, bar_w - done, 1, (92, 80, 40))
    else:
        put(ix + 1, iy + 2, status, DIM, pbg)


def bet(x, y, amount, chip=(226, 72, 72)):
    put(x, y, "●", chip)
    put(x + 2, y, amount, WHITE, None, True)


def button(x, y, w, key, text, color, focus=False):
    col = color if focus else lerp(color, BG, 0.55)
    keybg = lerp(col, (0, 0, 0), 0.35)
    put(x, y, "▄" * w, col)
    fill(x, y + 1, w, 1, col)
    put(x, y + 2, "▀" * w, col)
    put(x + 1, y + 1, f" {key} ", WHITE, keybg, True)
    put(x + 5, y + 1, text, WHITE if focus else (200, 204, 210), col, focus)


# ---------------------------------------------------------------- table scene
def pick_tier(w, h, variant):
    for tw, th, k, a, p in VARIANTS[variant]:
        if w >= tw and h >= th:
            return k, a, p
    return None


def too_small(variant):
    min_w, min_h = VARIANTS[variant][-1][:2]
    lines = [
        ("POHER", WHITE, True),
        ("", TXT, False),
        (f"Растяните окно: нужно не меньше {min_w}×{min_h}", TXT, False),
        (f"Make the window bigger: at least {min_w}×{min_h}", DIM, False),
        (f"{W}×{H}", YEL, False),
    ]
    y0 = max(0, H // 2 - 3)
    for i, (s, fg, bold) in enumerate(lines):
        put(max(0, (W - len(s)) // 2), y0 + i, s, fg, None, bold)


PANEL = (20, 23, 30)
CHAT = (
    ("Dana", (198, 84, 156), False, "think faster :)"),
    ("Karp", BOT_C, True, "nice flop"),
    ("Omar", (228, 138, 48), False, "lol"),
    (None, None, False, "Karp bets 40"),
    (None, None, False, "Dana calls 40"),
    ("Dana", (198, 84, 156), False, "who has the ace?"),
    ("You", (66, 126, 220), False, "not telling"),
)


def side_panel(x0, pw, p):
    """Big portrait of the player in focus, then the chat."""
    fill(x0, 1, pw, H - 2, PANEL)
    x = x0 + 2
    blit_cells(x, 2, avatar_cells("fb498fed", p, True, BOT_C))
    y = 2 + p // 2 + 1
    put(x, y, "Karp", WHITE, PANEL, True)
    put(x + 6, y, " BOT ", WHITE, BOT_C, True)
    put(x, y + 1, "1 480", TXT, PANEL)
    put(x, y + 2, "bets 40", YEL, PANEL, True)
    y += 4
    put(x, y, "─" * p, (52, 58, 70), PANEL)
    y += 1
    for name, col, bot, text in CHAT:
        if y >= H - 3:
            break
        if name is None:
            put(x, y, text[:p], DIM, PANEL)
        else:
            put(x, y, name, col, PANEL, True)
            nx = x + len(name)
            if bot:
                put(nx + 1, y, "BOT", DIM, PANEL)
                nx += 4
            put(nx, y, (": " + text)[:x + p - nx], TXT, PANEL)
        y += 1
    put(x, H - 3, "T  write a message", DIM, PANEL)


def table_scene(w, h, variant=2):
    canvas(w, h)
    tier = pick_tier(w, h, variant)
    if tier is None:
        too_small(variant)
        return
    k, a, portrait = tier
    panel_w = portrait + 4 if portrait else 0
    w -= panel_w                      # from here on w is the width of the table area
    cw, ch = card_size(k)
    gap = k
    aw, ah = a, a // 2                # avatar, in cells
    iw = 16                           # info block next to an avatar
    sw = aw + 4                       # side column
    below = 10 if k == 1 else 13      # rows under the table
    rim = 3 if k == 1 else 5
    bkw, bkh = backs_size(k)

    TX0, TX1 = sw + 1, w - sw - 1
    TY0, TY1 = 5, h - below
    rows = TY1 - TY0
    CX, CY = (TX0 + TX1) / 2, TY0 + TY1          # px; y px = row * 2
    HW, HH = (TX1 - TX0) / 2, rows
    R = min(round(0.76 * rows), int(HW), rows)
    EDGE = (52, 30, 16)
    WOOD = (126, 78, 42)
    WOOD_HI = (158, 104, 60)
    FELT_C = (30, 124, 74)
    FELT_E = (17, 88, 52)

    def zone(px, py):
        qx = abs(px - CX) - (HW - R)
        qy = abs(py - CY) - (HH - R)
        d = math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - R
        if d > 0:
            return "out", BG
        if d > -1:
            return "edge", EDGE
        if d > -1 - (rim - 1) / 2:
            return "wood", WOOD
        if d > -rim:
            return "hi", WOOD_HI
        t = math.hypot((px - CX) / HW, (py - CY) / HH)
        t = min(1.0, t) ** 1.6
        t = round(t * 5) / 5
        return "felt", lerp(FELT_C, FELT_E, t)

    def is_felt(x, row):
        return (
            zone(x + 0.5, row * 2 + 0.5)[0] == "felt"
            and zone(x + 0.5, row * 2 + 1.5)[0] == "felt"
        )

    def felt_left(row):
        x = TX0
        while x < CX and not is_felt(x, row):
            x += 1
        return x

    def on_felt(x_pref, row):
        """Nearest x to x_pref where a pair of card backs lies fully on the felt."""
        lo = felt_left(row) + 1
        return min(max(x_pref, lo), TX0 + TX1 - lo - bkw)

    for row in range(TY0, TY1):
        for x in range(TX0, TX1):
            top = zone(x + 0.5, row * 2 + 0.5)[1]
            bot = zone(x + 0.5, row * 2 + 1.5)[1]
            c = grid[row][x]
            if top == bot:
                c[0], c[2] = " ", top
            else:
                c[0], c[1], c[2] = "▀", top, bot

    cxi = round(CX)
    mid = (TY0 + TY1) // 2

    # title bar
    fill(0, 0, W, 1, BAR)
    for i, s in enumerate("♠♥♦♣"):
        put(1 + i, 0, s, SUIT_ON_DARK[s], BAR)
    put(6, 0, "POHER", WHITE, BAR, True)
    put(14, 0, "Kitchen  ·  No-Limit Hold'em  ·  blinds 1/2  ·  hand #42", DIM, BAR)
    friends = (("Dana", True), ("Omar", True), ("Vlad", False))
    x = W - 2 - sum(len(n) + 4 for n, _ in friends)
    for nm, on in friends:
        put(x, 0, "●", (72, 200, 120) if on else (70, 74, 84), BAR)
        put(x + 2, 0, nm, TXT if on else DIM, BAR)
        x += len(nm) + 4

    # top seats: avatar and info side by side, the avatar overlaps the table edge
    gw = aw + 1 + iw
    d = min((w - gw) // 2 - 2, max((TX1 - TX0 - gw) // 2, gw + 3))
    g_left, g_mid, g_right = cxi - d - gw // 2, cxi - gw // 2, cxi + d - gw // 2
    seat(g_left, 1, g_left + aw + 1, 1, iw, a,
         "Karp", "1 480", "bet 40", "fb498fed", BOT_C, bot=True)
    seat(g_mid, 1, g_mid + aw + 1, 1, iw, a,
         "Dana", "2 150", "call 40", "05497cd1", (198, 84, 156))
    seat(g_right + iw + 1, 1, g_right, 1, iw, a,
         "Slon", "1 020", "fold", "6d83314a", BOT_C, bot=True, state="fold")

    # side seats: info under the avatar, kept clear of the top row of avatars
    side_y = max(mid - (ah + 3) // 2, ah + 2)
    side_ax = (sw - aw) // 2
    seat(side_ax, side_y, 1, side_y + ah, sw - 2, a,
         "Omar", "960", "waiting", "a56e348a", (228, 138, 48))
    seat(w - sw + side_ax, side_y, w - sw + 1, side_y + ah, sw - 2, a,
         "Snegr", "3 300", "fold", "c9e5bb18", BOT_C, bot=True, state="fold")

    # cards of the top seats lie under their info blocks, level with the avatar bottom
    top_y = max(ah - bkh, TY0 + (2 if k == 1 else 3))
    lx = on_felt(g_left + aw + 1 + (iw - bkw) // 2, top_y)
    backs(lx, top_y, k)
    bet(lx + 1, top_y + bkh, "40")
    mx = on_felt(g_mid + aw + 1 + (iw - bkw) // 2, top_y)
    backs(mx, top_y, k)
    bet(mx + 1, top_y + bkh, "40", (72, 132, 226))
    sy = mid - bkh
    sx = felt_left(sy) + 2
    backs(sx, sy, k)
    put(sx + 1, sy + bkh + 1, " D ", (30, 26, 10), YEL, True)

    bx = cxi - (5 * cw + 4 * gap) // 2
    by = max(mid - ch // 2 - 1, top_y + bkh + 2)
    step = cw + gap
    card(bx, by, "A", "♠", k)
    card(bx + step, by, "K", "♦", k)
    card(bx + 2 * step, by, "7", "♣", k)
    fill(bx + 3 * step, by, cw, ch, SLOT)
    fill(bx + 4 * step, by, cw, ch, SLOT)

    py = by + ch + 2
    put(cxi - 6, py, "pot", (170, 214, 188))
    put(cxi - 2, py, "●", (226, 72, 72))
    put(cxi - 1, py, "●", (72, 132, 226))
    put(cxi, py, "●", YEL)
    put(cxi + 2, py, "220", WHITE, None, True)

    # hero: avatar to the left of the cards, info to the left of the avatar
    hw_total = 2 * cw + gap
    hx = cxi - hw_total // 2
    hy = TY1 - (3 if k == 1 else 4)
    hero_ax = hx - aw - 3
    info_x = hero_ax - iw - 2
    seat(hero_ax, hy + ch - ah, info_x, hy + ch - 3, iw, a,
         "You", "2 000", "", "317b84fc", (66, 126, 220), state="active")
    card(hx, hy, "Q", "♥", k, shadow=False)
    card(hx + step, hy, "10", "♥", k, shadow=False)
    tx = hx + hw_total + 3
    put(tx, hy + ch - 2, "Gutshot straight draw", TXT)
    put(tx, hy + ch - 1, "4 outs  ·  16% to hit", DIM)

    # actions
    ay = h - 6
    ax = max(info_x, 20)
    left = ax - 18
    put(left, ay + 1, "YOUR MOVE", YEL, None, True)
    put(left + 11, ay + 1, "18s", TXT)
    button(ax, ay, 13, "F", "Fold", (176, 52, 58))
    button(ax + 15, ay, 16, "C", "Call 40", (40, 164, 92), focus=True)
    button(ax + 33, ay, 14, "R", "Raise", (204, 150, 40))
    x = ax + 50
    for label in ("1/2", "2/3", "pot", "all-in"):
        put(x, ay + 1, f" {label} ", DIM, (36, 41, 52))
        x += len(label) + 3

    # chat: one line under the buttons, or the side panel
    if portrait:
        side_panel(w, panel_w, portrait)
    else:
        put(left, ay + 3, "Dana", (198, 84, 156), None, True)
        put(left + 4, ay + 3, ": think faster :)", TXT)
        put(left + 26, ay + 3, "Karp", BOT_C, None, True)
        put(left + 32, ay + 3, "BOT", DIM)
        put(left + 35, ay + 3, ": nice flop", TXT)
    fill(0, H - 1, W, 1, BAR)
    put(2, H - 1, "Tab panel    T chat    H history    S stats    Esc menu", DIM, BAR)
    size = f"{W}×{H}  ·  avatar {aw}×{ah}"
    if portrait:
        size += f"  ·  portrait {portrait}×{portrait // 2}"
    put(W - len(size) - 2, H - 1, size, DIM, BAR)


def deck_scene(k):
    cw, ch = card_size(k)
    canvas(13 * (cw + 1) + 1, 4 * (ch + 1) + 1, (22, 104, 62))
    for si, suit in enumerate("♠♥♦♣"):
        for ri, rank in enumerate(RANKS):
            card(1 + ri * (cw + 1), 1 + si * (ch + 1), rank, suit, k)


# ---------------------------------------------------------------- entry points
def live(variant):
    import select
    import termios
    import tty

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    out = sys.stdout
    try:
        tty.setcbreak(fd)
        out.write("\x1b[?1049h\x1b[?25l")
        last = None
        while True:
            ts = shutil.get_terminal_size()
            if (ts.columns, ts.lines, variant) != last:
                last = (ts.columns, ts.lines, variant)
                table_scene(ts.columns, ts.lines, variant)
                out.write("\x1b[0m\x1b[2J" + emit(addressed=True))
                out.flush()
            if select.select([fd], [], [], 0.15)[0]:
                if os.read(fd, 64) == b"\t":
                    variant = 5 - variant      # 2 <-> 3
                else:
                    break
    finally:
        out.write("\x1b[0m\x1b[?25h\x1b[?1049l")
        out.flush()
        termios.tcsetattr(fd, termios.TCSADRAIN, old)


def write(path):
    path.write_text(emit(), encoding="utf-8")


# Which quarters of a cell each block character paints: 1 TL, 2 TR, 4 BL, 8 BR.
QUARTERS = {" ": 0, "▘": 1, "▝": 2, "▀": 3, "▖": 4, "▌": 5, "▞": 6, "▛": 7,
            "▗": 8, "▚": 9, "▐": 10, "▜": 11, "▄": 12, "▙": 13, "▟": 14, "█": 15}


def ppm(path):
    """Picture of the grid: one cell = 2 x 4 px. Text is shown as a tinted cell."""
    buf = bytearray(f"P6 {W * 2} {H * 4} 255\n".encode())
    for row in grid:
        top, bot = bytearray(), bytearray()
        for ch, fg, bg, _ in row:
            if ch in QUARTERS:
                m = QUARTERS[ch]
                q = [fg if m & bit else bg for bit in (1, 2, 4, 8)]
            else:
                q = [lerp(bg, fg, 0.55)] * 4
            top += bytes(q[0]) + bytes(q[1])
            bot += bytes(q[2]) + bytes(q[3])
        buf += top + top + bot + bot
    Path(path).write_bytes(buf)


def main(argv):
    mode = argv[1] if len(argv) > 1 else "2"
    if mode == "plain":
        table_scene(int(argv[3]), int(argv[4]), int(argv[2]))
        for i, ln in enumerate(plain()):
            assert len(ln) == W, (i, len(ln))
            print(f"{i:2d}|{ln}|")
    elif mode == "ppm":
        table_scene(int(argv[3]), int(argv[4]), int(argv[2]))
        ppm(argv[5])
    elif mode == "save":
        here = Path(__file__).resolve().parent
        for variant, prefix in ((2, "table"), (3, "table-panel")):
            smallest = VARIANTS[variant][-1][:2]
            for name, size in (("s", smallest), ("m", (192, 54)), ("l", (256, 72))):
                table_scene(*size, variant)
                write(here / f"{prefix}-{name}.ans")
        deck_scene(1)
        write(here / "cards.ans")
        deck_scene(2)
        write(here / "cards-full.ans")
    elif mode == "deck":
        ts = shutil.get_terminal_size()
        deck_scene(2 if ts.columns >= 170 and ts.lines >= 42 else 1)
        sys.stdout.write(emit())
    elif mode not in ("2", "3"):
        sys.exit(__doc__)
    elif sys.stdout.isatty() and sys.stdin.isatty():
        live(int(mode))
    else:
        ts = shutil.get_terminal_size()
        table_scene(ts.columns, ts.lines - 1, int(mode))
        sys.stdout.write(emit())


if __name__ == "__main__":
    main(sys.argv)
