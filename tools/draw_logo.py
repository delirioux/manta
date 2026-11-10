#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Manta's logo as SVG (assets/logo.svg): render it at 1024 x 1024 to assets/logo.png (any browser, rsvg-convert or
cairosvg).
Usage: draw_logo.py [ACCENT_COLOUR] [OUT.svg]"""
# The look (family style):
# black ground, a ring, audio bars, a line-art fish in one accent colour + cream,
# music notes, a little knob box (four knobs in one row, like the FM-1), a wide italic name.
# Manta's own touches: mango orange accent with a palm-green second colour; warm sunset wedges
# fanning out behind the ring; the audio bars beat out a 3-2 clave (the clave hits stand tall
# in green); a manta ray gliding seen from above, wings spread across the ring, cream shoulder
# patches; a hand-painted sign-style wordmark (yellow-to-orange-to-red gradient, green rim).
import math, sys

ACC = sys.argv[1] if len(sys.argv) > 1 else "#ff8a1f"   # the accent colour (mango orange)
OUT = sys.argv[2] if len(sys.argv) > 2 else str(__import__("pathlib").Path(__file__).resolve().parent.parent / "assets" / "logo.svg")
PALM = "#2fd47a"                                        # second colour: palm green
CREAM, INK, BG = "#fff6e8", "#0a0604", "#000000"
W = H = 1024
CX, CY, R = 512, 430, 362

# ---- letters on a 6 x 7 grid, chamfered like the rest of the family (A has a counter: even-odd fill)
L = {
    "M": [[(0,7),(0,0.9),(0.9,0),(2.4,0),(3.5,2.2),(4.6,0),(6.1,0),(7,0.9),(7,7),(5.3,7),(5.3,3),
           (4.2,5),(2.8,5),(1.7,3),(1.7,7)]],
    "A": [[(0,7),(0,1.8),(1.8,0),(4.2,0),(6,1.8),(6,7),(4.3,7),(4.3,4.9),(1.7,4.9),(1.7,7)],
          [(1.7,3.3),(4.3,3.3),(4.3,2.3),(3.6,1.6),(2.4,1.6),(1.7,2.3)]],
    "N": [[(0,7),(0,0),(1.7,0),(4.3,4.2),(4.3,0),(6,0),(6,7),(4.3,7),(1.7,2.8),(1.7,7)]],
    "T": [[(0,0),(6,0),(6,1.6),(3.85,1.6),(3.85,7),(2.15,7),(2.15,1.6),(0,1.6)]],
}
WID = {"M": 7, "A": 6, "N": 6, "T": 6}
GAP, WORD, SK = 0.8, "MANTA", 0.30
total = sum(WID[c] for c in WORD) + GAP * (len(WORD) - 1)
U = 22.0
TY0 = 812


def word_path(dx=0.0, dy=0.0):
    x0 = (W - total * U - 7 * U * SK) / 2
    d, x = [], 0.0
    for c in WORD:
        for poly in L[c]:
            pts = [f"{x0 + (x + px) * U + (7 - py) * U * SK + dx:.1f},{TY0 + py * U + dy:.1f}" for px, py in poly]
            d.append("M" + " L".join(pts) + " Z")
        x += WID[c] + GAP
    return " ".join(d)


# ---- warm sunset wedges fanning out from below the ring, behind everything
wedges = []
for i, a in enumerate(range(-172, -7, 15)):
    if i % 2:
        continue
    r0, r1 = math.radians(a), math.radians(a + 7.5)
    col = ACC if i % 4 == 0 else "#ff4a2a"
    wedges.append(f'<path d="M 512 800 L {512 + 1000*math.cos(r0):.0f} {800 + 1000*math.sin(r0):.0f} '
                  f'L {512 + 1000*math.cos(r1):.0f} {800 + 1000*math.sin(r1):.0f} Z" fill="{col}" opacity="0.16"/>')

# ---- audio bars clipped to the ring: sixteen steps, the 3-2 son clave hits stand tall in palm green
CLAVE = {0, 3, 6, 10, 12}
bars = []
for i in range(16):
    x = 186 + i * 41
    t = i / 15
    hit = i in CLAVE
    h = (300 if hit else 70 + 90 * abs(math.sin(t * 7.3 + 0.5))) * (0.6 + 0.4 * math.sin(t * math.pi))
    bars.append(f'<rect x="{x}" y="{CY + 40 - h * 0.6:.0f}" width="24" height="{h:.0f}" rx="3" '
                f'fill="{PALM if hit else ACC}" opacity="{0.95 if hit else 0.8}"/>')

# ---- the manta, seen from above, head up: right half as cubic segments, mirrored for the left
P0 = (0, 312)                                         # between the cephalic fins
HALF = [
    ((16, 310), (30, 302), (36, 290)),                # base of the right cephalic fin
    ((44, 266), (54, 246), (46, 226)),                # fin curls up to its tip
    ((70, 244), (78, 284), (70, 312)),                # back down its outer edge
    ((150, 270), (246, 246), (312, 254)),             # leading edge bowing out to the wing tip
    ((296, 314), (246, 382), (160, 432)),             # trailing edge sweeping back
    ((118, 456), (90, 478), (70, 500)),               # broad disc into the pelvic fins
    ((52, 522), (28, 532), (12, 534)),
    ((8, 580), (4, 620), (2, 650)),                   # the whip tail
]


def mirror_path(p0, half):
    d = f"M {p0[0]:.0f} {p0[1]:.0f} "
    for c1, c2, e in half:
        d += f"C {c1[0]} {c1[1]}, {c2[0]} {c2[1]}, {e[0]} {e[1]} "
    pts = [p0] + [seg[2] for seg in half]
    for k in range(len(half) - 1, -1, -1):           # walk back up the left side
        c1, c2, _ = half[k]
        start = pts[k]
        d += f"C {-c2[0]} {c2[1]}, {-c1[0]} {c1[1]}, {-start[0]} {start[1]} "
    return d + "Z"


body = mirror_path(P0, HALF)
# cream shoulder patches (the manta's white chevrons), one per side
patch_r = "M 100 318 C 160 300, 220 284, 270 276 C 244 320, 196 360, 136 384 C 126 360, 114 336, 100 318 Z"
patch_l = "M -100 318 C -160 300, -220 284, -270 276 C -244 320, -196 360, -136 384 C -126 360, -114 336, -100 318 Z"
spine = "M 0 340 C 0 400, 0 460, 0 520"
wing_lines = ("M 84 410 C 150 412, 200 396, 246 356 M -84 410 C -150 412, -200 396, -246 356 "
              "M 60 456 C 100 458, 130 448, 156 430 M -60 456 C -100 458, -130 448, -156 430")


def note(x, y, s, double=False):
    """An eighth (or a pair of eighths) note."""
    g = [f'<ellipse cx="{x}" cy="{y}" rx="{13*s}" ry="{9.5*s}" transform="rotate(-22 {x} {y})"/>',
         f'<rect x="{x + 9*s:.1f}" y="{y - 58*s:.1f}" width="{4.5*s:.1f}" height="{58*s:.1f}"/>']
    if double:
        x2 = x + 44 * s
        g += [f'<ellipse cx="{x2}" cy="{y - 6*s}" rx="{13*s}" ry="{9.5*s}" transform="rotate(-22 {x2} {y - 6*s})"/>',
              f'<rect x="{x2 + 9*s:.1f}" y="{y - 64*s:.1f}" width="{4.5*s:.1f}" height="{58*s:.1f}"/>',
              f'<polygon points="{x + 9*s:.1f},{y - 58*s:.1f} {x2 + 13.5*s:.1f},{y - 64*s:.1f} {x2 + 13.5*s:.1f},{y - 52*s:.1f} {x + 9*s:.1f},{y - 46*s:.1f}"/>']
    else:
        g.append(f'<path d="M {x + 13.5*s:.1f} {y - 58*s:.1f} q {22*s:.1f} {14*s:.1f} {16*s:.1f} {38*s:.1f} '
                 f'q {-2*s:.1f} {-16*s:.1f} {-16*s:.1f} {-22*s:.1f} z"/>')
    return "".join(g)


def glint(x, y, r):
    """A four-point sparkle."""
    k = r * 0.22
    return (f'<path d="M {x} {y - r} Q {x + k} {y - k} {x + r} {y} Q {x + k} {y + k} {x} {y + r} '
            f'Q {x - k} {y + k} {x - r} {y} Q {x - k} {y - k} {x} {y - r} Z"/>')


def knob(x, y):
    ticks = "".join(
        f'<line x1="{x + 30*math.cos(a):.1f}" y1="{y + 30*math.sin(a):.1f}" x2="{x + 37*math.cos(a):.1f}" y2="{y + 37*math.sin(a):.1f}"/>'
        for a in [math.radians(d) for d in range(135, 406, 45)])
    return (f'<g stroke="{INK}" stroke-width="3" stroke-linecap="round">{ticks}</g>'
            f'<circle cx="{x}" cy="{y}" r="22" fill="{ACC}" stroke="{INK}" stroke-width="5"/>'
            f'<line x1="{x}" y1="{y}" x2="{x}" y2="{y - 16}" stroke="{INK}" stroke-width="5" stroke-linecap="round"/>')


def clave(x, y, ang):
    """One clave stick."""
    return (f'<g transform="rotate({ang} {x} {y})"><rect x="{x - 70}" y="{y - 9}" width="140" height="18" rx="9" '
            f'fill="#b8692a" stroke="{INK}" stroke-width="4"/><line x1="{x - 56}" y1="{y - 2}" x2="{x + 50}" y2="{y - 2}" '
            f'stroke="#e8a060" stroke-width="3" stroke-linecap="round"/></g>')


word = word_path()
shadow = word_path(10, 10)
BX, BY, BW, BH = 512 - 170, 640, 340, 104
svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">
<defs>
<linearGradient id="sign" gradientUnits="userSpaceOnUse" x1="0" y1="{TY0}" x2="0" y2="{TY0 + 7*U}"><stop offset="0" stop-color="#fff3b0"/><stop offset="0.2" stop-color="#ffd23a"/><stop offset="0.55" stop-color="{ACC}"/><stop offset="1" stop-color="#e8321e"/></linearGradient>
<radialGradient id="sea" cx="0.5" cy="0.38" r="0.6"><stop offset="0" stop-color="#1a0e06"/><stop offset="1" stop-color="#080402"/></radialGradient>
<clipPath id="ring"><circle cx="{CX}" cy="{CY}" r="{R - 14}"/></clipPath></defs>
<rect width="{W}" height="{H}" fill="{BG}"/>
{"".join(wedges)}
<circle cx="{CX}" cy="{CY}" r="{R}" fill="url(#sea)" stroke="{ACC}" stroke-width="11"/>
<g clip-path="url(#ring)">{"".join(bars)}</g>

<!-- the box with four knobs in one row, wired to the ring, a pair of claves resting on it -->
<path d="M {BX} {BY + BH/2} C {BX - 60} {BY + BH/2}, {BX - 70} {BY - 40}, {BX - 70} {BY - 100}" fill="none" stroke="{ACC}" stroke-width="7"/>
<path d="M {BX + BW} {BY + BH/2} C {BX + BW + 60} {BY + BH/2}, {BX + BW + 70} {BY - 40}, {BX + BW + 70} {BY - 100}" fill="none" stroke="{ACC}" stroke-width="7"/>

<!-- the manta (its tail runs down behind the box) -->
<g transform="translate(512 0) rotate(-7 0 400)">
  <path d="{body}" fill="none" stroke="{BG}" stroke-width="26" stroke-linejoin="round"/>
  <path d="{body}" fill="{INK}" stroke="{ACC}" stroke-width="10" stroke-linejoin="round"/>
  <path d="{patch_r}" fill="{CREAM}"/>
  <path d="{patch_l}" fill="{CREAM}"/>
  <path d="{wing_lines}" fill="none" stroke="{ACC}" stroke-width="4" stroke-linecap="round" opacity="0.6"/>
  <path d="{spine}" fill="none" stroke="{PALM}" stroke-width="5" stroke-linecap="round" opacity="0.8"/>
  <path d="M 40 232 C 46 252, 50 276, 52 296 M -40 232 C -46 252, -50 276, -52 296" fill="none" stroke="{CREAM}" stroke-width="4" stroke-linecap="round"/>
  <circle cx="58" cy="318" r="15" fill="{CREAM}" stroke="{ACC}" stroke-width="5"/>
  <circle cx="61" cy="318" r="7" fill="{INK}"/>
  <circle cx="-58" cy="318" r="15" fill="{CREAM}" stroke="{ACC}" stroke-width="5"/>
  <circle cx="-55" cy="318" r="7" fill="{INK}"/>
  <path d="M -22 328 Q 0 340, 22 328" fill="none" stroke="{ACC}" stroke-width="5" stroke-linecap="round"/>
</g>

<rect x="{BX}" y="{BY}" width="{BW}" height="{BH}" rx="16" fill="{CREAM}" stroke="{ACC}" stroke-width="9"/>
{"".join(knob(BX + 50 + i * 80, BY + 52) for i in range(4))}
{clave(BX + BW - 10, BY - 4, -14)}{clave(BX + BW + 8, BY + 14, 8)}

<g fill="{CREAM}">{note(150, 196, 0.75)}{note(186, 150, 0.9, True)}{note(830, 172, 0.8)}</g>
<g fill="{PALM}">{glint(118, 118, 15)}{glint(888, 116, 13)}{glint(930, 210, 9)}</g>

<g stroke-linecap="round"><line x1="60" y1="{TY0 + 40}" x2="200" y2="{TY0 + 40}" stroke="{ACC}" stroke-width="7"/><line x1="36" y1="{TY0 + 80}" x2="186" y2="{TY0 + 80}" stroke="{PALM}" stroke-width="5" opacity="0.8"/><line x1="84" y1="{TY0 + 118}" x2="174" y2="{TY0 + 118}" stroke="{ACC}" stroke-width="4" opacity="0.5"/><line x1="836" y1="{TY0 + 50}" x2="976" y2="{TY0 + 50}" stroke="{PALM}" stroke-width="5" opacity="0.8"/><line x1="824" y1="{TY0 + 92}" x2="954" y2="{TY0 + 92}" stroke="{ACC}" stroke-width="4" opacity="0.5"/></g>
<path d="{shadow}" fill="#0d4a2a" fill-rule="evenodd" stroke="#0d4a2a" stroke-width="22" stroke-linejoin="round"/>
<path d="{word}" fill="none" stroke="{BG}" stroke-width="26" stroke-linejoin="round"/>
<path d="{word}" fill="none" stroke="{PALM}" stroke-width="14" stroke-linejoin="round"/>
<path d="{word}" fill="url(#sign)" fill-rule="evenodd" stroke="#3a1206" stroke-width="3" stroke-linejoin="round"/>
<g fill="#ffffff">{glint(W/2 + 250, TY0 + 6, 28)}{glint(W/2 - 262, TY0 + 150, 16)}</g>
</svg>'''
open(OUT, "w").write(svg)
print("ok", OUT)
