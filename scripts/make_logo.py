# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ronny Krueger
"""Generate an Amiga Boing-ball + cloud logo as SVG (server/logo.svg). Aufruf: python3 scripts/make_logo.py > server/logo.svg."""
import math, sys

R = 100.0          # ball radius in SVG units
CX, CY = 120.0, 108.0
LAT, LON = 8, 16   # classic Boing ball: 8 bands x 16 segments
TILT = math.radians(-18)   # rotation around view axis
SPIN = math.radians(9)     # rotation around ball axis
STEPS = 10

def sph(lat, lon):
    # lat in [-pi/2, pi/2], lon in [0, 2pi)
    x = math.cos(lat) * math.sin(lon + SPIN)
    y = -math.sin(lat)
    z = math.cos(lat) * math.cos(lon + SPIN)
    # tilt around z (view) axis
    c, s = math.cos(TILT), math.sin(TILT)
    return (x * c - y * s, x * s + y * c, z)

def clip(poly):
    """Sutherland-Hodgman against z >= 0; returns list of (x,y,z,onlimb)."""
    out = []
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        ain, bin_ = a[2] >= 0, b[2] >= 0
        if ain:
            out.append((a[0], a[1], a[2], False))
        if ain != bin_:
            t = a[2] / (a[2] - b[2])
            p = [a[k] + (b[k] - a[k]) * t for k in range(3)]
            d = math.hypot(p[0], p[1]) or 1
            out.append((p[0] / d, p[1] / d, 0.0, True))
    return out

def with_arcs(pts):
    """Replace chords between consecutive limb points with circle arcs."""
    res = []
    n = len(pts)
    for i in range(n):
        a, b = pts[i], pts[(i + 1) % n]
        res.append(a)
        if a[3] and b[3]:
            a0 = math.atan2(a[1], a[0]); a1 = math.atan2(b[1], b[0])
            d = (a1 - a0 + math.pi) % (2 * math.pi) - math.pi
            for k in range(1, 12):
                t = a0 + d * k / 12
                res.append((math.cos(t), math.sin(t), 0, True))
    return res

paths = []
for i in range(LAT):
    la0 = -math.pi / 2 + math.pi * i / LAT
    la1 = -math.pi / 2 + math.pi * (i + 1) / LAT
    for j in range(LON):
        if (i + j) % 2:
            continue
        lo0 = 2 * math.pi * j / LON
        lo1 = 2 * math.pi * (j + 1) / LON
        poly = []
        for k in range(STEPS + 1):
            poly.append(sph(la0, lo0 + (lo1 - lo0) * k / STEPS))
        for k in range(STEPS + 1):
            poly.append(sph(la0 + (la1 - la0) * k / STEPS, lo1))
        for k in range(STEPS + 1):
            poly.append(sph(la1, lo1 - (lo1 - lo0) * k / STEPS))
        for k in range(STEPS + 1):
            poly.append(sph(la1 - (la1 - la0) * k / STEPS, lo0))
        pts = clip(poly)
        if len(pts) < 3:
            continue
        pts = with_arcs(pts)
        d = "M" + " L".join("%.1f %.1f" % (CX + p[0] * R, CY + p[1] * R) for p in pts) + "Z"
        paths.append(d)

svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 260 240" role="img" aria-label="AmiCloud">
  <!-- SPDX-License-Identifier: GPL-3.0-or-later -->
  <!-- Copyright (C) 2026 Ronny Krueger -->
  <!-- Boing-Ball (Amiga) auf einer Wolke; erzeugt, nicht von Hand gezeichnet -->
  <defs>
    <radialGradient id="shade" cx="38%" cy="32%" r="75%">
      <stop offset="0" stop-color="#fff" stop-opacity=".55"/>
      <stop offset=".35" stop-color="#fff" stop-opacity="0"/>
      <stop offset=".8" stop-color="#000" stop-opacity=".18"/>
      <stop offset="1" stop-color="#000" stop-opacity=".45"/>
    </radialGradient>
    <linearGradient id="cloud" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#3fd4c4"/>
      <stop offset="1" stop-color="#0b7f74"/>
    </linearGradient>
  </defs>
  <g class="ball">
    <circle cx="{CX}" cy="{CY}" r="{R}" fill="#f4f4f4"/>
    <path fill="#e94560" d="{" ".join(paths)}"/>
    <circle cx="{CX}" cy="{CY}" r="{R}" fill="url(#shade)"/>
  </g>
  <g class="cloud">
    <path fill="url(#cloud)" stroke="#16213e" stroke-width="5" stroke-linejoin="round"
      d="M62 230 C30 230 18 206 30 188 C38 175 52 172 62 175 C64 152 84 138 106 142
         C116 120 146 112 168 126 C182 134 188 146 189 158 C210 152 236 166 236 192
         C236 214 220 230 198 230 Z"/>
    <path fill="none" stroke="#fff" stroke-opacity=".45" stroke-width="5" stroke-linecap="round"
      d="M72 186 C74 170 88 160 104 162 M118 152 C126 138 146 134 160 142"/>
  </g>
</svg>
'''
sys.stdout.write(svg)
