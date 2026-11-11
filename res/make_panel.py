#!/usr/bin/env python3
# Draws res/SixCopains.svg. VCV Rack can't draw SVG <text>, so every label is
# turned into outline paths with fontTools. Positions are in mm and must match
# the layout namespace in src/SixCopains.cpp.
#   pip install fonttools
#   python3 res/make_panel.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W = 213.36  # 42 HP
H = 128.5
PAPER = "#F1ECE2"
INK = "#221E1A"
SOFT = "#7A7068"
ACCENT = "#C8602A"
TEAL = "#2C8C7E"
OUTBOX = "#2A2622"
OUTINK = "#F1ECE2"
RULE = "#D3CABB"

# must match src/SixCopains.cpp
R1, R2, R3, R4, R5, R6, RIO = 24.5, 43.0, 62.0, 76.5, 92.0, 107.5, 120.5
C1, C2, C3, C4, C5 = 13.0, 36.0, 59.0, 82.0, 105.0
PAN_X = [11.0, 30.0, 49.0, 68.0, 87.0, 106.0]
IN_X = [10.0, 23.0, 36.0]
OUT_X = [50.0, 63.0, 76.0, 89.0, 102.0, 115.0]
MIX_L_X, MIX_R_X = 130.0, 144.0
B1, B2, B3 = 134.0, 151.0, 168.0
BR1, BR2, BR3, BR4, BR5 = 46.0, 60.5, 74.0, 88.0, 104.0
BOUT_V_X, BOUT_G_X = 143.0, 159.0
IT_X = 197.6
SCOPE_X, SCOPE_Y, SCOPE_W, SCOPE_H = 185.6, 49.6, 24.0, 58.8

fonts = {}


def text(s, x, y, size, font=FONT_MED, fill=INK, anchor="middle", spacing=0.04):
    """SVG path for string s; (x, y) is the baseline point in mm."""
    if font not in fonts:
        f = TTFont(font)
        fonts[font] = (f, f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    f, gs, cmap, upm = fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    x0 = x - width * scale / 2 if anchor == "middle" else (x - width * scale if anchor == "end" else x)
    pen = SVGPathPen(gs)
    cx = 0.0
    for n in names:
        gs[n].draw(TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y)))
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


parts = [f'<rect width="{W}" height="{H}" fill="{PAPER}"/>']
add = parts.append


def label(s, x, y, size=2.9, fill=INK, anchor="middle"):
    add(text(s, x, y, size, FONT_BOLD, fill, anchor=anchor, spacing=0.06))


def hint(s, x, y, size=2.2, fill=SOFT, anchor="middle"):
    add(text(s, x, y, size, FONT_MED, fill, anchor=anchor))


def rect(x, y, w, h, fill, rx=1.2):
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}"/>')


def vline(x, y1, y2):
    add(f'<line x1="{x}" y1="{y1}" x2="{x}" y2="{y2}" stroke="{RULE}" stroke-width="0.35" stroke-dasharray="0.9 0.9"/>')


def hline(y, x1, x2):
    add(f'<line x1="{x1}" y1="{y}" x2="{x2}" y2="{y}" stroke="{RULE}" stroke-width="0.35" stroke-dasharray="0.9 0.9"/>')


def friends(y):
    """Six dots, again and again: six friends."""
    x = 4.0
    while x < W - 10.0:
        for k in range(6):
            add(f'<circle cx="{x + k * 1.25:.2f}" cy="{y}" r="0.38" fill="{ACCENT}"/>')
        x += 12.0


def cv_ring(x, y):
    """A thin ring around a CV input, so inputs read as 'goes with the knob above'."""
    add(f'<circle cx="{x}" cy="{y}" r="5.1" fill="none" stroke="{RULE}" stroke-width="0.35"/>')


friends(2.4)
friends(H - 2.4)

# --- title ------------------------------------------------------------------
add(text("6 COPAINS", 8.0, 10.2, 4.4, FONT_BOLD, INK, anchor="start", spacing=0.14))
add(text("six friends that sing together", 40.5, 10.0, 2.8, FONT_MED, ACCENT, anchor="start"))
add(text("after 6Copains by Mark IJzerman", W - 8.0, 10.0, 2.4, FONT_MED, SOFT, anchor="end"))

# --- sound -----------------------------------------------------------------
LY1 = R1 - 6.8
for x, n in ((C1, "LEVEL"), (C2, "RAMP"), (C3, "CURVE"), (C4, "FM RATIO"), (C5, "FM INDEX")):
    label(n, x, LY1, 3.2)
hint("drive", C1, R1 + 7.4)
hint("rise / fall", C2, R1 + 7.4)
hint("sqr  tri  sin", C3, R1 + 7.4)
hint("x note", C4, R1 + 7.4)
hint("by voice | all", C5, R1 + 7.4)
LY2 = R2 - 5.6
label("PAN DRIFT", C1, LY2)
label("OCTAVE", C4, LY2)
for x in (C2, C3, C5):
    hint("CV", x, LY2, 2.4)
    cv_ring(x, R2)

hline(49.4, 4.0, 118.0)

# --- envelope and morph ----------------------------------------------------
LY3 = R3 - 7.2
label("ENV", C1, LY3, 3.2)
hint("CYC", C1 + 3.4, R3 - 2.9, 2.2, anchor="start")
hint("ASR", C1 + 3.4, R3 + 0.8, 2.2, anchor="start")
hint("AR", C1 + 3.4, R3 + 4.5, 2.2, anchor="start")
label("SLOPE", C2, LY3, 3.2)
label("TIME", C3, LY3, 3.2)
label("RETRIG", C4, LY3)
hint("OVERLAP", C4 + 3.4, R3 - 2.0, 2.0, anchor="start")
hint("FADE", C4 + 3.4, R3 + 3.8, 2.0, anchor="start")
label("MORPH", C5, R3 - 9.4, 3.2, ACCENT)
label("RANGE", C4, R4 - 5.6)
hint("LFO", C4 + 3.4, R4 - 2.0, 2.0, anchor="start")
hint("AUDIO", C4 + 3.4, R4 + 3.8, 2.0, anchor="start")
hint("atk / rel", C2, R4 - 6.4)
hint("2 ms - 8 s", C3, R4 - 6.4)
for x in (C2, C3, C5):
    cv_ring(x, R4)
hint("CV", C2 + 7.4, R4 + 1.0, 2.4)
hint("CV", C3 + 7.4, R4 + 1.0, 2.4)
hint("CV 0-10V", C5, R4 - 6.0)

hline(82.8, 4.0, 118.0)

# --- morph amounts ---------------------------------------------------------
add(text("MORPH MOVES", 4.5, 87.2, 2.9, FONT_BOLD, ACCENT, anchor="start", spacing=0.06))
for x, n in ((C1, "RAMP"), (C2, "CURVE"), (C3, "FM"), (C4, "SLOPE"), (C5, "TIME")):
    label(n, x, R5 + 6.8)
add(f'<line x1="{C1}" y1="{R5}" x2="{C5}" y2="{R5}" stroke="{ACCENT}" stroke-width="0.4" stroke-dasharray="0.6 1.2"/>')

# --- pans --------------------------------------------------------------------
for v, x in enumerate(PAN_X):
    label(f"PAN {v + 1}", x, R6 - 4.6)

# --- banks -------------------------------------------------------------------
vline(121.5, 14.0, 110.5)
add(text("BANKS", 124.0, 16.6, 2.9, FONT_BOLD, ACCENT, anchor="start", spacing=0.12))
hint("a note player", 176.0, 16.6, 2.2, anchor="end")
rect(124.0, 18.6, 52.0, 18.0, "#161412", 1.4)
LB1 = BR1 - 6.2
label("PLAY", B1, LB1)
label("CLOCK", B2, LB1)
label("CAPTURE", B3, LB1)
LB2 = BR2 - 7.0
label("RATE", B1, LB2, 3.2)
label("AMOUNT", B2, LB2, 3.2)
label("BANK", B3, LB2, 3.2)
LB3 = BR3 - 5.4
label("RAND", B1, LB3)
hint("CV", B2, LB3, 2.4)
hint("CV", B3, LB3, 2.4)
cv_ring(B2, BR3)
cv_ring(B3, BR3)
LB4 = BR4 - 5.0
label("VEL MIN", B1, LB4)
label("VEL MAX", B2, LB4)
label("GATE", B3, LB4)
rect(BOUT_V_X - 7.4, BR5 - 8.6, BOUT_G_X - BOUT_V_X + 14.8, 13.4, OUTBOX)
add(text("V/OCT", BOUT_V_X, BR5 - 5.4, 2.9, FONT_BOLD, OUTINK, spacing=0.06))
add(text("GATE", BOUT_G_X, BR5 - 5.4, 2.9, FONT_BOLD, OUTINK, spacing=0.06))
hint("what Banks plays, 6 channels", (BOUT_V_X + BOUT_G_X) / 2, BR5 - 10.0, 2.0)

# --- bottom row: inputs and outputs -----------------------------------------
LIO = RIO - 5.8
for x, n in zip(IN_X, ("V/OCT", "GATE", "VEL")):
    label(n, x, LIO)
rect(43.0, RIO - 8.8, 108.0, 14.4, OUTBOX)
for v, x in enumerate(OUT_X):
    add(text(str(v + 1), x, LIO, 2.9, FONT_BOLD, OUTINK, spacing=0.06))
add(text("L", MIX_L_X, LIO, 2.9, FONT_BOLD, OUTINK, spacing=0.06))
add(text("R", MIX_R_X, LIO, 2.9, FONT_BOLD, OUTINK, spacing=0.06))
add(f'<line x1="122.0" y1="{RIO - 6.8}" x2="122.0" y2="{RIO + 4.0}" stroke="#5A524A" stroke-width="0.35"/>')
add(text("MIX", 137.0, LIO, 2.4, FONT_MED, "#A89E92"))

# --- intone, scope, end ------------------------------------------------------
vline(182.0, 14.0, 110.5)
label("INTONE", IT_X, R1 - 6.8, 3.2, ACCENT)
hint("under | over", IT_X, R1 + 7.4)
hint("CV", IT_X, R2 - 5.6, 2.4)
cv_ring(IT_X, R2)
add(text("SCOPE", SCOPE_X, SCOPE_Y - 1.6, 2.6, FONT_BOLD, SOFT, anchor="start", spacing=0.1))
rect(SCOPE_X, SCOPE_Y, SCOPE_W, SCOPE_H, "#161412", 1.4)
rect(IT_X - 9.2, RIO - 8.8, 18.4, 14.4, OUTBOX)
add(text("END", IT_X, LIO, 2.9, FONT_BOLD, OUTINK, spacing=0.06))
hint("trig x6", IT_X, RIO - 8.8 - 1.0, 2.0)

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">\n'
       + "\n".join(parts) + "\n</svg>\n")
with open(os.path.join(HERE, "SixCopains.svg"), "w") as fh:
    fh.write(svg)
print("wrote SixCopains.svg")
