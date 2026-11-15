#!/usr/bin/env python3
"""Build code/docs/ATU_Tuning_Algorithm.html, the illustrated description of the ATU search.

The figures are drawn from data written by the unit test ATUTuneAlgoTest.DocFigureData:
the firmware's own search, run against the circuit model of the RF board. From code/:

    mkdir -p /tmp/atudoc
    ATU_DOC_DIR=/tmp/atudoc test/build/all_ATU_tests --gtest_filter='*DocFigureData'
    python3 tools/atu_vna/build_tuning_doc.py /tmp/atudoc docs/ATU_Tuning_Algorithm.html

Needs numpy and Pillow. The page is one self-contained HTML file (inline SVG, PNG rasters
as data URIs). The prose quotes some numbers from the data (readings, SWR); if a change
to the search moves them, re-read the captions: a few describe the example's path in words.
"""
import base64, io, json, math, os, sys
import numpy as np
from PIL import Image

if len(sys.argv) != 3:
    sys.exit('usage: build_tuning_doc.py DATA_DIR OUT.html')
D, OUT = sys.argv[1], sys.argv[2]
traces = {c['name']: c for c in json.load(open(os.path.join(D, 'traces.json')))}

def landscape(name):
    a = np.fromfile(os.path.join(D, name + '.bin'), dtype=np.float32).reshape(2, 256, 256, 2)
    return a[..., 0] + 1j * a[..., 1]          # [topo][l][c]

def swr(g):
    g = min(abs(g), 0.999)
    return (1 + g) / (1 - g)

def b64png(img):
    buf = io.BytesIO()
    img.save(buf, format='PNG', optimize=True)
    return 'data:image/png;base64,' + base64.b64encode(buf.getvalue()).decode()

PH_BYPASS, PH_WALK_LC, PH_WALK_CL, PH_REF_LC, PH_REF_CL = 0, 2, 3, 4, 5
COARSE = [0, 1, 2, 4, 8, 16, 32, 64, 128]
LNOM = [0.05, 0.1, 0.22, 0.45, 1.0, 2.2, 4.4, 8.8]
CNOM = [10, 22, 47, 100, 220, 470, 1000, 2000]
def lnom(code): return sum(v for n, v in enumerate(LNOM) if code >> n & 1)
def cnom(code): return sum(v for n, v in enumerate(CNOM) if code >> n & 1)

# SWR bands for maps, best first
BANDS = [(1.2, '#ffffcc'), (1.5, '#c7e9b4'), (2.0, '#7fcdbb'), (3.0, '#41b6c4'),
         (5.0, '#2c7fb8'), (1e9, '#253494')]
def band_colour(g):
    s = swr(g)
    for lim, col in BANDS:
        if s <= lim:
            return col
    return BANDS[-1][1]
def hex2rgb(h): return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))

# ------------------------------------------------------------------ Smith chart

class Smith:
    """An SVG Smith chart. view = radius of |Gamma| shown (1 for the whole chart)."""
    n = 0
    def __init__(self, size=360, view=1.0, cx=None, cy=None, pad=14, labels=True):
        Smith.n += 1
        self.id = 'sc%d' % Smith.n
        self.size, self.view, self.pad = size, view, pad
        self.R = (size / 2 - pad)
        self.cx = size / 2 if cx is None else cx
        self.cy = size / 2 if cy is None else cy
        self.parts = []
        self.labels = labels
        self.grid()

    def p(self, g):
        s = self.R / self.view
        return (self.cx + s * g.real, self.cy - s * g.imag)

    def circ(self, gc, rad, cls, clip=True, extra=''):
        x, y = self.p(gc)
        r = rad * self.R / self.view
        c = ' clip-path="url(#%s-clip)"' % self.id if clip else ''
        self.parts.append('<circle cx="%.2f" cy="%.2f" r="%.2f" class="%s"%s%s/>' % (x, y, r, cls, c, extra))

    def grid(self):
        s = self.R / self.view
        unit = 1.0 * s
        self.parts.append('<defs><clipPath id="%s-clip"><circle cx="%.2f" cy="%.2f" r="%.2f"/></clipPath>'
                          '<clipPath id="%s-view"><rect x="0" y="0" width="%d" height="%d"/></clipPath></defs>'
                          % (self.id, self.cx, self.cy, unit, self.id, self.size, self.size))
        self.parts.append('<g clip-path="url(#%s-view)">' % self.id)
        self.parts.append('<circle cx="%.2f" cy="%.2f" r="%.2f" class="sc-bg"/>' % (self.cx, self.cy, unit))
        for r in [0.2, 0.5, 1, 2, 5]:
            self.circ(complex(r / (1 + r), 0), 1 / (1 + r), 'sc-grid')
        for x in [0.2, 0.5, 1, 2, 5]:
            for sgn in (1, -1):
                self.circ(complex(1, sgn / x), 1 / x, 'sc-grid')
        x0, y0 = self.p(-1 + 0j)
        x1, y1 = self.p(1 + 0j)
        self.parts.append('<line x1="%.2f" y1="%.2f" x2="%.2f" y2="%.2f" class="sc-grid"/>' % (x0, y0, x1, y1))
        self.circ(0j, 1, 'sc-edge', clip=False)
        if self.labels and self.view >= 0.99:
            for r in [0.2, 0.5, 1, 2, 5]:
                g = (r - 1) / (r + 1)
                x, y = self.p(complex(g, 0))
                self.parts.append('<text x="%.1f" y="%.1f" class="sc-lab">%s</text>' % (x + 2, y - 3, ('%g' % (r * 50)) + 'Ω'))
        self.end_grid = len(self.parts)

    def swr_circle(self, s, label=True):
        g = (s - 1) / (s + 1)
        self.circ(0j, g, 'sc-swr', clip=False)
        if label:
            x, y = self.p(complex(0, g))
            self.parts.append('<text x="%.1f" y="%.1f" class="sc-swrlab">SWR %g</text>' % (x + 3, y - 3, s))

    def image(self, href, extent):
        """A raster covering |Gamma| in [-extent, extent] on both axes."""
        x0, y0 = self.p(complex(-extent, extent))
        w = 2 * extent * self.R / self.view
        self.parts.append('<image href="%s" x="%.2f" y="%.2f" width="%.2f" height="%.2f" clip-path="url(#%s-clip)" preserveAspectRatio="none"/>'
                          % (href, x0, y0, w, w, self.id))

    def path(self, pts, cls, marker_end=None):
        d = 'M' + ' L'.join('%.2f,%.2f' % self.p(g) for g in pts)
        m = ' marker-end="url(#%s)"' % marker_end if marker_end else ''
        self.parts.append('<path d="%s" class="%s"%s/>' % (d, cls, m))

    def dot(self, g, cls, r=3.2, title=None, shape='circle'):
        x, y = self.p(g)
        t = '<title>%s</title>' % title if title else ''
        if shape == 'square':
            self.parts.append('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" class="%s">%s</rect>' % (x - r, y - r, 2 * r, 2 * r, cls, t))
        else:
            self.parts.append('<circle cx="%.2f" cy="%.2f" r="%.2f" class="%s">%s</circle>' % (x, y, r, cls, t))

    def text(self, g, s, cls='sc-note', dx=0, dy=0, anchor='start'):
        x, y = self.p(g)
        self.parts.append('<text x="%.1f" y="%.1f" class="%s" text-anchor="%s">%s</text>' % (x + dx, y + dy, cls, anchor, s))

    def svg(self, w=None, title=''):
        return ('<svg viewBox="0 0 %d %d" class="smith" role="img" aria-label="%s">' % (self.size, self.size, title)
                + ''.join(self.parts) + '</g></svg>')

ARROWS = ('<svg width="0" height="0" style="position:absolute"><defs>'
          '<marker id="arr-lc" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" class="fill-lc"/></marker>'
          '<marker id="arr-cl" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" class="fill-cl"/></marker>'
          '<marker id="arr-ink" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" class="fill-ink"/></marker>'
          '<marker id="arr-hot" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="5" markerHeight="5" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="#d62728"/></marker>'
          '</defs></svg>')

def z2g(z):  # normalised
    return (z - 1) / (z + 1)

# --------------------------------------------------- Fig 2: Smith chart primer

def fig_primer():
    sc = Smith(size=440, pad=18)
    # matching circles
    sc.circ(0.5 + 0j, 0.5, 'sc-match')                  # r = 1
    sc.circ(-0.5 + 0j, 0.5, 'sc-match dashed')          # g = 1
    sc.text(0.93 + 0.36j, 'r = 1 circle', 'sc-note', anchor='end')
    sc.text(-0.97 - 0.3j, 'g = 1 circle', 'sc-note')
    # 200 ohm with LC: shunt C at the antenna, then series L
    pts = [z2g(1 / complex(0.25, b)) for b in np.linspace(0, 0.4330, 40)]
    sc.path(pts, 'arc-lc', 'arr-lc')
    pts2 = [z2g(complex(1, -1.7320 + x)) for x in np.linspace(0, 1.7320, 40)]
    sc.path(pts2, 'arc-lc dashed2', 'arr-lc')
    sc.dot(z2g(4 + 0j), 'pt-load', 5)
    sc.text(z2g(4 + 0j), '200 Ω', 'sc-note b', dx=-4, dy=-10, anchor='end')
    sc.text(z2g(complex(1, -0.9)), 'series L', 'sc-note lc', dx=-12, dy=8, anchor='end')
    sc.text(z2g(1 / complex(0.25, 0.25)), 'shunt C', 'sc-note lc', dx=10, dy=10)
    # 12.5 ohm with CL: series L at the antenna, then shunt C
    pts = [z2g(complex(0.25, x)) for x in np.linspace(0, 0.4330, 40)]
    sc.path(pts, 'arc-cl', 'arr-cl')
    pts2 = [z2g(1 / complex(1, -1.7320 + b)) for b in np.linspace(0, 1.7320, 40)]
    sc.path(pts2, 'arc-cl dashed2', 'arr-cl')
    sc.dot(z2g(0.25 + 0j), 'pt-load', 5)
    sc.text(z2g(0.25 + 0j), '12.5 Ω', 'sc-note b', dx=6, dy=16)
    sc.text(z2g(complex(0.25, 0.22)), 'series L', 'sc-note cl', dx=-10, dy=0, anchor='end')
    sc.text(z2g(1 / complex(1, -0.7)), 'shunt C', 'sc-note cl', dx=10, dy=-10)
    sc.dot(0j, 'pt-centre', 4)
    sc.text(0j, '50 Ω', 'sc-note b', dx=-8, dy=18, anchor='end')
    sc.text(0.0 + 0.82j, 'inductive (+jX)', 'sc-note muted', anchor='middle')
    sc.text(0.0 - 0.86j, 'capacitive (−jX)', 'sc-note muted', anchor='middle')
    return sc.svg(title='Smith chart: how an L network moves an impedance to 50 ohm')

# ---------------------------------------- Fig 3: everything the board can reach

def cloud_png(G, colour, extent=1.0, px=520, alpha_max=200):
    """Density image of complex points G inside [-extent, extent]^2."""
    xs = ((G.real + extent) / (2 * extent) * px).astype(int)
    ys = ((extent - G.imag) / (2 * extent) * px).astype(int)
    ok = (xs >= 0) & (xs < px) & (ys >= 0) & (ys < px)
    cnt = np.zeros((px + 1, px + 1))
    for dy in (0, 1):
        for dx in (0, 1):
            np.add.at(cnt, (ys[ok] + dy, xs[ok] + dx), 1)
    cnt = cnt[:px, :px]
    a = np.where(cnt > 0, np.clip(110 + 40 * np.log2(cnt + 1), 0, alpha_max), 0).astype(np.uint8)
    rgb = hex2rgb(colour)
    img = np.zeros((px, px, 4), np.uint8)
    img[..., 0], img[..., 1], img[..., 2], img[..., 3] = rgb[0], rgb[1], rgb[2], a
    return b64png(Image.fromarray(img, 'RGBA'))

def fig_reach(name):
    L = landscape(name)
    c = traces[name]
    zl = complex(c['r'], c['x']) / 50
    out = []
    for topo, col, label in [(0, '#3b82f6', 'LC: capacitors at the antenna side'),
                             (1, '#f97316', 'CL: capacitors at the transmitter side')]:
        sc = Smith(size=330, pad=12, labels=False)
        sc.image(cloud_png(L[topo].ravel(), col, alpha_max=230), 1.0)
        good = L[topo][np.abs(L[topo]) < 1 / 3]
        for g in good.ravel():
            sc.dot(g, 'pt-good%d' % topo, 2.4)
        for li in COARSE:
            for ci in COARSE:
                sc.dot(L[topo, li, ci], 'pt-coarse', 1.9)
        sc.swr_circle(1.5, label=False)
        sc.dot(z2g(zl), 'pt-load', 4.5)
        sc.text(z2g(zl), 'bypass', 'sc-note b', dx=-6, dy=-8, anchor='end')
        out.append('<div><div class="subcap">%s</div>%s</div>' % (label, sc.svg(title=label)))
    counts = [int((np.abs(L[t]) < 1 / 3).sum()) for t in (0, 1)]
    return '<div class="pair">' + ''.join(out) + '</div>', counts

# ---------------------------------- Fig 4: the |Gamma| landscape in code space

MAP_PX = 300
def code_to_t(code):  # 0..8, log-like axis
    return math.log2(code + 1)

def map_png(Gt):
    """Gt[l][c] complex for one topology -> SWR-band image, L across, C up."""
    px = MAP_PX
    t = (np.arange(px) + 0.5) / px * 8
    codes = np.clip(np.round(2 ** t - 1).astype(int), 0, 255)
    A = np.abs(Gt)
    img = np.zeros((px, px, 3), np.uint8)
    lut = {}
    for yi in range(px):
        cc = codes[px - 1 - yi]
        for xi in range(px):
            g = A[codes[xi], cc]
            col = band_colour(g)
            img[yi, xi] = lut.setdefault(col, hex2rgb(col))
    return b64png(Image.fromarray(img, 'RGB'))

def fig_landscape(panels):
    out = []
    W = MAP_PX
    ml, mt, mb, mr = 56, 10, 52, 10
    for name, topo, title in panels:
        L = landscape(name)
        c = traces[name]
        label = 'CL' if topo else 'LC'
        parts = ['<svg viewBox="0 0 %d %d" class="map" role="img" aria-label="%s map">' % (W + ml + mr, W + mt + mb, label)]
        parts.append('<image href="%s" x="%d" y="%d" width="%d" height="%d" preserveAspectRatio="none" style="image-rendering:pixelated"/>' % (map_png(L[topo]), ml, mt, W, W))
        parts.append('<rect x="%d" y="%d" width="%d" height="%d" class="frame"/>' % (ml, mt, W, W))
        def X(code): return ml + code_to_t(code) / 8 * W
        def Y(code): return mt + W - code_to_t(code) / 8 * W
        for k in COARSE[1:]:
            parts.append('<line x1="%.1f" y1="%d" x2="%.1f" y2="%d" class="tick"/>' % (X(k), mt + W, X(k), mt + W + 4))
            parts.append('<text x="%.1f" y="%d" class="axlab" text-anchor="middle">%d</text>' % (X(k), mt + W + 15, k))
            parts.append('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" class="tick"/>' % (ml - 4, Y(k), ml, Y(k)))
            parts.append('<text x="%d" y="%.1f" class="axlab" text-anchor="end">%d</text>' % (ml - 6, Y(k) + 3.5, k))
        parts.append('<text x="%.1f" y="%d" class="axtitle" text-anchor="middle">L code (%s)</text>' % (ml + W / 2, mt + W + 34, label))
        parts.append('<text x="14" y="%.1f" class="axtitle" text-anchor="middle" transform="rotate(-90 14 %.1f)">C code</text>' % (mt + W / 2, mt + W / 2))
        # readings of this topology
        rd = [r for r in c['readings'] if r[0] == 1 and r[1] == topo]
        for r in rd:
            x, y = X(r[2]) + 0, Y(r[3])
            # nudge code 0 points inside the frame
            x = max(x, ml + 3); y = min(y, mt + W - 3)
            cls = 'mk-walk' if r[4] in (PH_WALK_LC, PH_WALK_CL) else 'mk-ref'
            parts.append('<circle cx="%.1f" cy="%.1f" r="3.3" class="%s"><title>L %d C %d: SWR %.2f</title></circle>' % (x, y, cls, r[2], r[3], swr(r[5])))
        if (c['final'] >> 18) & 1 and ((c['final'] >> 16) & 1) == topo:
            fl, fc = c['final'] & 255, (c['final'] >> 8) & 255
            parts.append('<circle cx="%.1f" cy="%.1f" r="7" class="mk-final"/>' % (max(X(fl), ml + 3), min(Y(fc), mt + W - 3)))
        parts.append('</svg>')
        out.append('<div><div class="subcap">%s</div>' % title + ''.join(parts) + '</div>')
    return '<div class="pair">' + ''.join(out) + '</div>'

def band_legend():
    items = []
    lo = 1.0
    for lim, col in BANDS:
        txt = ('SWR ≤ %g' % lim) if lo == 1.0 else (('%g–%g' % (lo, lim)) if lim < 1e8 else '> %g' % lo)
        items.append('<span class="key"><i style="background:%s"></i>%s</span>' % (col, txt))
        lo = lim
    return '<div class="legend">' + ''.join(items) + '</div>'

# ------------------------------------------------- Fig 5: the coarse-grid walk

def fig_walk(name):
    c = traces[name]
    L = landscape(name)
    cell, ml, mt = 34, 70, 26
    out = []
    for topo, ph, label in [(0, PH_WALK_LC, 'LC walk'), (1, PH_WALK_CL, 'CL walk')]:
        W = cell * 9
        parts = ['<svg viewBox="0 0 %d %d" class="walk" role="img" aria-label="%s">' % (ml + W + 10, mt + W + 46, label)]
        order = {}
        for i, r in enumerate(c['readings']):
            if r[4] == ph:
                order[(r[2], r[3])] = (i, r[5])
        # floor per L
        for xi, lc in enumerate(COARSE):
            for yi, cc in enumerate(COARSE):
                x, y = ml + xi * cell, mt + (8 - yi) * cell
                if (lc, cc) in order:
                    i, g = order[(lc, cc)]
                    parts.append('<rect x="%d" y="%d" width="%d" height="%d" fill="%s" class="cell"><title>L %d C %d: SWR %.2f</title></rect>' % (x, y, cell, cell, band_colour(g), lc, cc, swr(g)))
                    ink = '#fff' if swr(g) > 3.0 else '#111'
                    parts.append('<text x="%d" y="%d" class="cellnum" fill="%s" text-anchor="middle">%d</text>' % (x + cell / 2, y + cell / 2 + 4, ink, i))
                else:
                    parts.append('<rect x="%d" y="%d" width="%d" height="%d" class="cell unread"/>' % (x, y, cell, cell))
        # mark the valley floor of each walked L
        floors = {}
        for (lc, cc), (i, g) in order.items():
            if lc not in floors or g < floors[lc][1]:
                floors[lc] = (cc, g)
        for lc, (cc, g) in floors.items():
            xi, yi = COARSE.index(lc), COARSE.index(cc)
            parts.append('<rect x="%d" y="%d" width="%d" height="%d" class="floor"/>' % (ml + xi * cell + 2, mt + (8 - yi) * cell + 2, cell - 4, cell - 4))
        seedL = min(floors.items(), key=lambda kv: kv[1][1])
        xi, yi = COARSE.index(seedL[0]), COARSE.index(seedL[1][0])
        parts.append('<rect x="%d" y="%d" width="%d" height="%d" class="seedbox"/>' % (ml + xi * cell - 2, mt + (8 - yi) * cell - 2, cell + 4, cell + 4))
        parts.append('<text x="%d" y="%d" class="seedlab" text-anchor="middle">seed</text>' % (ml + xi * cell + cell / 2, mt + (8 - yi) * cell + cell - 4))
        for xi, lc in enumerate(COARSE):
            parts.append('<text x="%d" y="%d" class="axlab" text-anchor="middle">%d</text>' % (ml + xi * cell + cell / 2, mt + W + 14, lc))
        for yi, cc in enumerate(COARSE):
            parts.append('<text x="%d" y="%d" class="axlab" text-anchor="end">%d</text>' % (ml - 6, mt + (8 - yi) * cell + cell / 2 + 4, cc))
        parts.append('<text x="%d" y="%d" class="axtitle" text-anchor="middle">L code — %s</text>' % (ml + W / 2, mt + W + 34, label))
        parts.append('<text x="16" y="%d" class="axtitle" text-anchor="middle" transform="rotate(-90 16 %d)">C code</text>' % (mt + W / 2, mt + W / 2))
        parts.append('</svg>')
        out.append('<div>' + ''.join(parts) + '</div>')
    return '<div class="pair">' + ''.join(out) + '</div>'

# ------------------------------------------------------- Fig 6: refine, zoomed

def fig_refine(name, topo, lrange, crange):
    c = traces[name]
    L = landscape(name)
    nl, nc = lrange[1] - lrange[0] + 1, crange[1] - crange[0] + 1
    cw, ch = 30, 46
    ml, mt = 50, 14
    W, H = nl * cw, nc * ch
    parts = ['<svg viewBox="0 0 %d %d" class="refine" role="img" aria-label="refine detail">' % (ml + W + 12, mt + H + 46)]
    walked = {(r[2], r[3]): i for i, r in enumerate(c['readings']) if r[0] == 1 and r[1] == topo and r[4] in (2, 3)}
    refined = [(i, r) for i, r in enumerate(c['readings']) if r[0] == 1 and r[1] == topo and r[4] in (4, 5)]
    def cx(l): return ml + (l - lrange[0]) * cw + cw / 2
    def cy(cc): return mt + (crange[1] - cc) * ch + ch / 2
    for l in range(lrange[0], lrange[1] + 1):
        for cc in range(crange[0], crange[1] + 1):
            g = abs(L[topo, l, cc])
            parts.append('<rect x="%d" y="%d" width="%d" height="%d" fill="%s" class="cell soft"><title>L %d C %d: SWR %.2f</title></rect>' % (cx(l) - cw / 2, cy(cc) - ch / 2, cw, ch, band_colour(g), l, cc, swr(g)))
    for (l, cc), i in walked.items():
        if lrange[0] <= l <= lrange[1] and crange[0] <= cc <= crange[1]:
            parts.append('<circle cx="%.1f" cy="%.1f" r="9" class="mk-walk big"/>' % (cx(l), cy(cc)))
            parts.append('<text x="%.1f" y="%.1f" class="cellnum small" text-anchor="middle">%d</text>' % (cx(l), cy(cc) + 3.5, i))
    # moves of the base point
    base = None
    path = []
    seed = None
    for i, r in refined:
        pass
    # reconstruct the base-point path from the readings: a reading becomes the base if it
    # is the best so far in this refine by more than SRCH_MIN_GAIN
    best_g = None
    seed_l, seed_c = None, None
    # the seed is the best walked point of this topology
    sw = min(((r[5], r[2], r[3]) for r in c['readings'] if r[0] == 1 and r[1] == topo and r[4] in (2, 3)))
    best_g, seed_l, seed_c = sw
    path = [(seed_l, seed_c)]
    for i, r in refined:
        if r[5] < best_g - 0.003:
            best_g = r[5]
            path.append((r[2], r[3]))
    d = 'M' + ' L'.join('%.1f,%.1f' % (cx(l), cy(cc)) for l, cc in path)
    parts.append('<path d="%s" class="basepath" marker-end="url(#arr-hot)"/>' % d)
    for i, r in refined:
        l, cc = r[2], r[3]
        if lrange[0] <= l <= lrange[1] and crange[0] <= cc <= crange[1]:
            parts.append('<circle cx="%.1f" cy="%.1f" r="9" class="mk-ref big"><title>reading %d: L %d C %d, SWR %.2f</title></circle>' % (cx(l), cy(cc), i, l, cc, swr(r[5])))
            parts.append('<text x="%.1f" y="%.1f" class="cellnum small light" text-anchor="middle">%d</text>' % (cx(l), cy(cc) + 3.5, i))
    parts.append('<circle cx="%.1f" cy="%.1f" r="13" class="mk-seed"/>' % (cx(seed_l), cy(seed_c)))
    for l in range(lrange[0], lrange[1] + 1):
        if l % 2 == 0:
            parts.append('<text x="%.1f" y="%d" class="axlab" text-anchor="middle">%d</text>' % (cx(l), mt + H + 14, l))
    for cc in range(crange[0], crange[1] + 1):
        parts.append('<text x="%d" y="%.1f" class="axlab" text-anchor="end">%d</text>' % (ml - 6, cy(cc) + 4, cc))
    parts.append('<text x="%.1f" y="%d" class="axtitle" text-anchor="middle">L code (linear here)</text>' % (ml + W / 2, mt + H + 34))
    parts.append('<text x="14" y="%.1f" class="axtitle" text-anchor="middle" transform="rotate(-90 14 %.1f)">C code</text>' % (mt + H / 2, mt + H / 2))
    parts.append('</svg>')
    return ''.join(parts), path

# ------------------------------------------------ Fig 7: the trace on the chart

def phase_cls(ph):
    return {PH_BYPASS: 'tr-by', PH_WALK_LC: 'tr-wlc', PH_WALK_CL: 'tr-wcl', PH_REF_LC: 'tr-rlc', PH_REF_CL: 'tr-rcl'}[ph]

def trace_chart(name, view=1.0, size=360, with_cloud=True, label_final=True, lines=True):
    c = traces[name]
    sc = Smith(size=size, view=view, pad=10, labels=(view >= 0.99))
    if with_cloud:
        Lc = landscape(name)
        sc.image(cloud_png(Lc[0].ravel(), '#93b4f5', extent=view, alpha_max=110), view)
        sc.image(cloud_png(Lc[1].ravel(), '#f5b28a', extent=view, alpha_max=110), view)
    for s in ([1.2, 1.5, 2.0, 3.0] if view < 0.99 else [1.5, 3.0]):
        sc.swr_circle(s)
    pts = [complex(r[6], r[7]) for r in c['readings']]
    if lines:
        sc.path(pts, 'tr-line')
    for i, r in enumerate(c['readings']):
        g = complex(r[6], r[7])
        sc.dot(g, phase_cls(r[4]), 3.6 if r[4] >= 4 else 3.2,
               title='reading %d: %s L %d C %d, SWR %.2f' % (i, 'bypass' if not r[0] else ('CL' if r[1] else 'LC'), r[2], r[3], swr(r[5])),
               shape='square' if r[4] == 0 else 'circle')
    f = c['readings'][-1]
    fi = min(range(len(c['readings'])), key=lambda i: c['readings'][i][5])
    fr = c['readings'][fi]
    sc.dot(complex(fr[6], fr[7]), 'tr-final', 7.5)
    return sc.svg(title='tuning trace on the Smith chart')

def phase_legend():
    return ('<div class="legend">'
            '<span class="key"><svg width="14" height="14"><rect x="2" y="2" width="10" height="10" class="tr-by"/></svg>bypass</span>'
            '<span class="key"><svg width="14" height="14"><circle cx="7" cy="7" r="4.5" class="tr-wlc"/></svg>LC walk</span>'
            '<span class="key"><svg width="14" height="14"><circle cx="7" cy="7" r="4.5" class="tr-wcl"/></svg>CL walk</span>'
            '<span class="key"><svg width="14" height="14"><circle cx="7" cy="7" r="4.5" class="tr-rlc"/></svg>LC refine</span>'
            '<span class="key"><svg width="14" height="14"><circle cx="7" cy="7" r="4.5" class="tr-rcl"/></svg>CL refine</span>'
            '<span class="key"><svg width="18" height="18"><circle cx="9" cy="9" r="7" class="tr-final"/></svg>result</span>'
            '</div>')

# ------------------------------------------------------- Fig 8: convergence plot

def fig_convergence(name):
    c = traces[name]
    rd = c['readings']
    n = len(rd)
    W, H, ml, mr, mt, mb = 640, 250, 46, 12, 12, 34
    pw, ph = W - ml - mr, H - mt - mb
    smin, smax = 1.0, 40.0
    def X(i): return ml + (i + 0.5) / n * pw
    def Y(s): return mt + ph - (math.log(min(max(s, smin), smax)) - math.log(smin)) / (math.log(smax) - math.log(smin)) * ph
    parts = ['<svg viewBox="0 0 %d %d" class="conv" role="img" aria-label="SWR of each reading">' % (W, H)]
    # phase bands
    spans = []
    for i, r in enumerate(rd):
        if not spans or spans[-1][0] != r[4]:
            spans.append([r[4], i, i])
        spans[-1][2] = i
    names = {0: 'bypass', 2: 'LC walk', 3: 'CL walk', 4: 'LC refine', 5: 'CL refine'}
    for k, (p_, a, b) in enumerate(spans):
        x0, x1 = ml + a / n * pw, ml + (b + 1) / n * pw
        parts.append('<rect x="%.1f" y="%d" width="%.1f" height="%d" class="band b%d"/>' % (x0, mt, x1 - x0, ph, k % 2))
        if b - a >= 3:
            parts.append('<text x="%.1f" y="%d" class="bandlab" text-anchor="middle">%s</text>' % ((x0 + x1) / 2, mt + 12, names[p_]))
    for s in [1.2, 1.5, 2, 3, 5, 10, 20]:
        cls = 'gridl stop' if s == 1.2 else ('gridl target' if s == 1.5 else 'gridl')
        parts.append('<line x1="%d" x2="%d" y1="%.1f" y2="%.1f" class="%s"/>' % (ml, ml + pw, Y(s), Y(s), cls))
        parts.append('<text x="%d" y="%.1f" class="axlab" text-anchor="end">%g</text>' % (ml - 5, Y(s) + 3.5, s))
    parts.append('<text x="%d" y="%.1f" class="sc-note muted">stop 1.2</text>' % (ml + 18, Y(1.2) + 12))
    parts.append('<text x="%d" y="%.1f" class="sc-note muted">target 1.5</text>' % (ml + 18, Y(1.5) - 4))
    # running best
    best = 9e9
    d = []
    for i, r in enumerate(rd):
        best = min(best, swr(r[5]))
        d.append((X(i), Y(best)))
    parts.append('<path d="M%s" class="runbest"/>' % ' L'.join('%.1f,%.1f' % p for p in d))
    for i, r in enumerate(rd):
        parts.append('<circle cx="%.1f" cy="%.1f" r="3" class="%s"><title>reading %d: SWR %.2f</title></circle>' % (X(i), Y(swr(r[5])), phase_cls(r[4]), i, swr(r[5])))
    for i in range(0, n, 10):
        parts.append('<text x="%.1f" y="%d" class="axlab" text-anchor="middle">%d</text>' % (X(i), mt + ph + 14, i))
    parts.append('<text x="%.1f" y="%d" class="axtitle" text-anchor="middle">reading</text>' % (ml + pw / 2, H - 4))
    parts.append('<text x="12" y="%.1f" class="axtitle" text-anchor="middle" transform="rotate(-90 12 %.1f)">SWR</text>' % (mt + ph / 2, mt + ph / 2))
    parts.append('</svg>')
    return ''.join(parts)

# --------------------------------------------------- Fig 1 and timeline (static)

TIMELINE = '''<svg viewBox="0 0 720 120" class="timeline" role="img" aria-label="one reading">
<g>
<rect x="20" y="40" width="110" height="36" class="seg s1"/><text x="75" y="63" class="lab" text-anchor="middle">set relays</text>
<rect x="130" y="40" width="110" height="36" class="seg s2"/><text x="185" y="63" class="lab" text-anchor="middle">relays settle</text>
<rect x="240" y="40" width="110" height="36" class="seg s3"/><text x="295" y="63" class="lab" text-anchor="middle">carrier settles</text>
<rect x="350" y="40" width="290" height="36" class="seg s4"/><text x="495" y="63" class="lab" text-anchor="middle">collect bridge readings, average</text>
<rect x="640" y="40" width="60" height="36" class="seg s5"/><text x="670" y="63" class="lab" text-anchor="middle">plan</text>
</g>
<text x="130" y="30" class="lab small" text-anchor="middle">carrier off</text>
<text x="185" y="96" class="lab small" text-anchor="middle">15 ms</text>
<text x="295" y="96" class="lab small" text-anchor="middle">15 ms</text>
<text x="495" y="96" class="lab small" text-anchor="middle">≥ 40 ms and ≥ 4 samples (gives up at 300 ms)</text>
<text x="495" y="30" class="lab small" text-anchor="middle">carrier on (ATU_TUNE_MARK), at the tune power</text>
<line x1="240" y1="34" x2="640" y2="34" class="bracket"/>
<text x="360" y="114" class="lab small muted" text-anchor="middle">about 99 ms a reading on the bench, including main-loop scheduling</text>
</svg>'''

# --------------------------------------------------------------- assemble page

def stats(name):
    c = traces[name]
    n = len(c['readings'])
    fs = swr(c['finalG'])
    best = c['best']
    return n, fs, swr(best[3]), ('CL' if best[0] else 'LC'), best[1], best[2]

ex = '20m_200'
n_ex, swr_ex, best_ex, _, _, _ = stats(ex)
refine_svg, base_path = fig_refine(ex, 0, (6, 26), (0, 5))

others = []
for nm, title in [('80m_12', '80 m, 12.5 Ω'), ('40m_25p100', '40 m, 25 + j100 Ω'), ('20m_25m100', '20 m, 25 − j100 Ω')]:
    n, fs, bs, bt, bl, bc = stats(nm)
    c = traces[nm]
    code = c['code']
    verdict = 'ATU_OK' if code == 0 else 'ATU_FAIL_NO_MATCH'
    fin = c['final']
    ftxt = '%s L %d C %d' % ('CL' if (fin >> 16) & 1 else 'LC', fin & 255, (fin >> 8) & 255)
    others.append('<figure class="mini"><div class="subcap">%s</div>%s<figcaption><b>%d readings</b>, SWR %.2f with %s (%s). '
                  'Best the simulated board allows: %.2f (%s L %d C %d).</figcaption></figure>'
                  % (title, trace_chart(nm, size=300, lines=False), n, fs, ftxt, verdict, bs, bt, bl, bc))

CSS = r'''
:root{
  --bg:#fbfaf7; --panel:#ffffff; --ink:#1d2329; --muted:#5d6670; --rule:#e3e0d8;
  --accent:#9a3412; --lc:#2563eb; --cl:#ea580c; --lc-soft:#dbe6fd; --cl-soft:#fde3d3;
  --grid:#c9ced6; --gridstrong:#8c96a3; --code:#f2f0ea; --hot:#d62728;
}
@media (prefers-color-scheme: dark){
  :root:not([data-theme="light"]){
    --bg:#15181c; --panel:#1c2026; --ink:#e7e9ec; --muted:#9aa3ad; --rule:#2d333b;
    --accent:#fb923c; --lc:#60a5fa; --cl:#fb923c; --lc-soft:#1e2c47; --cl-soft:#3d2618;
    --grid:#39414b; --gridstrong:#6b7682; --code:#232830; --hot:#ff5a5a;
  }
}
:root[data-theme="dark"]{
  --bg:#15181c; --panel:#1c2026; --ink:#e7e9ec; --muted:#9aa3ad; --rule:#2d333b;
  --accent:#fb923c; --lc:#60a5fa; --cl:#fb923c; --lc-soft:#1e2c47; --cl-soft:#3d2618;
  --grid:#39414b; --gridstrong:#6b7682; --code:#232830; --hot:#ff5a5a;
}
*{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--ink);font:16.5px/1.62 Charter,"Bitstream Charter","Iowan Old Style",Georgia,serif}
main{max-width:860px;margin:0 auto;padding:40px 16px 80px}
h1,h2,h3,.subcap,.legend,figcaption,table,.kicker,.toc,.callout b{font-family:-apple-system,"Segoe UI",Inter,Roboto,Helvetica,Arial,sans-serif}
h1{font-size:2.1rem;line-height:1.2;margin:.2em 0 .3em;letter-spacing:-.01em}
h2{font-size:1.35rem;margin:2.4em 0 .6em;padding-top:.6em;border-top:1px solid var(--rule)}
h3{font-size:1.05rem;margin:1.8em 0 .4em}
.kicker{color:var(--accent);font-weight:600;font-size:.85rem;letter-spacing:.06em;text-transform:uppercase}
.lede{font-size:1.15rem;color:var(--muted);margin-top:0}
p{margin:.7em 0}
code{font-family:ui-monospace,"SF Mono",Menlo,Consolas,monospace;font-size:.86em;background:var(--code);padding:.1em .35em;border-radius:4px}
a{color:var(--accent)}
.callout{background:var(--panel);border:1px solid var(--rule);border-left:4px solid var(--accent);border-radius:8px;padding:14px 18px;margin:1.4em 0}
.callout ul{margin:.4em 0 0;padding-left:1.2em}
.toc{font-size:.92rem;columns:2;column-gap:28px;margin:1.2em 0;padding:0;list-style:none}
.toc li{margin:.15em 0}
figure{margin:1.8em 0;background:var(--panel);border:1px solid var(--rule);border-radius:10px;padding:16px}
figure.mini{margin:0;padding:10px}
figcaption{font-size:.88rem;color:var(--muted);margin-top:10px;line-height:1.5}
figcaption b{color:var(--ink)}
.subcap{font-size:.85rem;font-weight:600;text-align:center;margin-bottom:4px}
.pair{display:grid;grid-template-columns:1fr 1fr;gap:14px;align-items:start}
.trio{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin:1.8em 0}
@media (max-width:700px){.pair,.trio{grid-template-columns:1fr}.toc{columns:1}}
svg{display:block;width:100%;height:auto}
svg.smith{max-width:440px;margin:0 auto}
.legend{display:flex;flex-wrap:wrap;gap:6px 16px;font-size:.82rem;color:var(--muted);margin-top:10px;justify-content:center}
.key{display:inline-flex;align-items:center;gap:6px}
.key i{display:inline-block;width:14px;height:14px;border-radius:3px;border:1px solid rgba(0,0,0,.15)}
.key svg{width:auto;display:inline}
table{border-collapse:collapse;width:100%;font-size:.88rem;margin:1em 0}
th,td{text-align:left;padding:6px 10px;border-bottom:1px solid var(--rule);vertical-align:top}
th{color:var(--muted);font-weight:600}
td code{white-space:nowrap}
.num{text-align:right;font-variant-numeric:tabular-nums}
.tablewrap{overflow-x:auto}
ol.steps{padding-left:1.3em} ol.steps li{margin:.35em 0}

/* SVG styling */
.sc-bg{fill:var(--panel)}
.sc-grid{fill:none;stroke:var(--grid);stroke-width:.8}
.sc-edge{fill:none;stroke:var(--gridstrong);stroke-width:1.4}
.sc-lab{font:9px sans-serif;fill:var(--muted)}
.sc-swr{fill:none;stroke:var(--gridstrong);stroke-width:1;stroke-dasharray:3 3}
.sc-swrlab{font:9.5px sans-serif;fill:var(--muted)}
.sc-match{fill:none;stroke:var(--ink);stroke-width:1.3;opacity:.55}
.dashed{stroke-dasharray:5 4}
.sc-note{font:12px sans-serif;fill:var(--ink)} .sc-note.b{font-weight:600}
.sc-note.lc{fill:var(--lc);font-weight:600} .sc-note.cl{fill:var(--cl);font-weight:600}
.sc-note.muted,.muted{fill:var(--muted)}
.arc-lc{fill:none;stroke:var(--lc);stroke-width:3} .arc-cl{fill:none;stroke:var(--cl);stroke-width:3}
.dashed2{stroke-dasharray:7 4}
.fill-lc{fill:var(--lc)} .fill-cl{fill:var(--cl)} .fill-ink{fill:var(--ink)}
.pt-load{fill:var(--ink);stroke:var(--panel);stroke-width:1.5}
.pt-centre{fill:var(--panel);stroke:var(--ink);stroke-width:2}
.pt-coarse{fill:var(--ink);opacity:.8}
.pt-good0{fill:var(--lc);stroke:var(--panel);stroke-width:.6} .pt-good1{fill:var(--cl);stroke:var(--panel);stroke-width:.6}
.frame{fill:none;stroke:var(--gridstrong)}
.tick{stroke:var(--gridstrong)}
.axlab{font:10.5px sans-serif;fill:var(--muted)}
.axtitle{font:12px sans-serif;fill:var(--ink)}
.mk-walk{fill:#fff;stroke:#111;stroke-width:1.3}
.mk-ref{fill:var(--hot);stroke:#fff;stroke-width:1.2}
.mk-final{fill:none;stroke:var(--hot);stroke-width:2.5}
.mk-seed{fill:none;stroke:var(--ink);stroke-width:2;stroke-dasharray:3 2}
.mk-walk.big{stroke-width:1.5}
.cell{stroke:var(--panel);stroke-width:1.5} .cell.soft{stroke-width:1}
.cell.unread{fill:var(--code)}
.cellnum{font:600 11px sans-serif} .cellnum.small{font-size:9.5px;fill:#111} .cellnum.light{fill:#fff}
.floor{fill:none;stroke:var(--hot);stroke-width:2}
.seedlab{font:600 8.5px sans-serif;fill:#111}
.seedbox{fill:none;stroke:var(--hot);stroke-width:3}
.basepath{fill:none;stroke:var(--hot);stroke-width:2.5;opacity:.85}
.tr-line{fill:none;stroke:var(--gridstrong);stroke-width:.6;opacity:.5}
.tr-by{fill:var(--muted);stroke:var(--panel);stroke-width:1}
.tr-wlc{fill:var(--panel);stroke:var(--lc);stroke-width:1.6}
.tr-wcl{fill:var(--panel);stroke:var(--cl);stroke-width:1.6}
.tr-rlc{fill:var(--lc);stroke:var(--panel);stroke-width:1}
.tr-rcl{fill:var(--cl);stroke:var(--panel);stroke-width:1}
.tr-final{fill:none;stroke:var(--hot);stroke-width:2.5}
.band.b0{fill:var(--code);opacity:.6} .band.b1{fill:none}
.bandlab{font:600 10.5px sans-serif;fill:var(--muted)}
.gridl{stroke:var(--grid);stroke-width:.8} .gridl.stop{stroke:var(--lc);stroke-dasharray:4 3} .gridl.target{stroke:var(--cl);stroke-dasharray:4 3}
.runbest{fill:none;stroke:var(--hot);stroke-width:2}
.schem .wire line,.schem .wire path,.wire2{stroke:var(--ink);stroke-width:2;fill:none}
.schem .bypass{stroke-dasharray:6 4;stroke:var(--muted)!important}
.schem .box{fill:var(--panel);stroke:var(--ink);stroke-width:1.6}
.schem .box.hl-cl{fill:var(--cl-soft);stroke:var(--cl)} .schem .box.hl-lc{fill:var(--lc-soft);stroke:var(--lc)}
.schem .node{fill:var(--panel);stroke:var(--ink);stroke-width:1.5}
.lab{font:12.5px sans-serif;fill:var(--ink)} .lab.small{font-size:11px;fill:var(--muted)} .lab.b{font-weight:600}
.lab.lc{fill:var(--lc);font-weight:600} .lab.cl{fill:var(--cl);font-weight:600}
.timeline .seg{stroke:var(--panel);stroke-width:2}
.seg.s1{fill:var(--code)} .seg.s2{fill:var(--grid)} .seg.s3{fill:var(--cl-soft)} .seg.s4{fill:var(--cl)} .seg.s5{fill:var(--lc-soft)}
.bracket{stroke:var(--cl);stroke-width:2}
'''

# the CL bank box sits left; the LC position is drawn as a dashed box between L and ANT
SCHEMATIC_FIXED = '''<svg viewBox="0 0 760 270" class="schem" role="img" aria-label="ATU RF board, simplified">
<g class="wire">
  <line x1="50" y1="150" x2="315" y2="150"/>
  <line x1="485" y1="150" x2="640" y2="150"/>
</g>
<path d="M80,150 V58 H610 V150" fill="none" stroke-width="2" style="stroke:var(--muted);stroke-dasharray:6 4"/>
<text x="345" y="48" class="lab" text-anchor="middle">K19 + K20 de-energised: network bypassed (the power-off state)</text>
<text x="38" y="155" class="lab b" text-anchor="end">TX</text>
<circle cx="50" cy="150" r="4" class="node"/>
<!-- series inductor chain -->
<rect x="315" y="126" width="170" height="48" rx="6" class="box"/>
<text x="400" y="146" class="lab b" text-anchor="middle">L1–L8 in series</text>
<text x="400" y="163" class="lab small" text-anchor="middle">relays K1–K8 short each coil</text>
<!-- CL position -->
<line x1="200" y1="150" x2="200" y2="190" class="wire2"/>
<rect x="140" y="190" width="120" height="44" rx="6" class="box hl-cl"/>
<text x="200" y="209" class="lab b" text-anchor="middle">C1–C8 to ground</text>
<text x="200" y="225" class="lab small" text-anchor="middle">relays K9–K16</text>
<text x="200" y="256" class="lab cl" text-anchor="middle">CL: K17 on, bank at the TX side</text>
<!-- LC position -->
<line x1="560" y1="150" x2="560" y2="190" class="wire2" style="stroke-dasharray:4 3"/>
<rect x="500" y="190" width="120" height="44" rx="6" class="box hl-lc" style="stroke-dasharray:5 4"/>
<text x="560" y="209" class="lab b" text-anchor="middle">the same bank</text>
<text x="560" y="225" class="lab small" text-anchor="middle">moved by K17</text>
<text x="560" y="256" class="lab lc" text-anchor="middle">LC: K17 off, bank at the antenna side</text>
<circle cx="200" cy="150" r="3" class="node"/><circle cx="560" cy="150" r="3" class="node"/>
<circle cx="610" cy="150" r="3" class="node"/>
<!-- K18 -->
<line x1="640" y1="150" x2="680" y2="124" class="wire2"/>
<circle cx="640" cy="150" r="3" class="node"/>
<circle cx="686" cy="120" r="4" class="node"/><circle cx="686" cy="180" r="4" class="node"/>
<text x="696" y="124" class="lab">coax out</text><text x="696" y="184" class="lab">balun out</text>
<text x="662" y="210" class="lab small" text-anchor="middle">K18</text>
</svg>'''

def tbl(rows, head, numcols=()):
    h = ''.join('<th%s>%s</th>' % (' class="num"' if i in numcols else '', x) for i, x in enumerate(head))
    b = ''.join('<tr>' + ''.join('<td%s>%s</td>' % (' class="num"' if i in numcols else '', x) for i, x in enumerate(r)) + '</tr>' for r in rows)
    return '<div class="tablewrap"><table><thead><tr>%s</tr></thead><tbody>%s</tbody></table></div>' % (h, b)

ex_c = traces[ex]
# readings per phase of the example
from collections import Counter
cnt = Counter(r[4] for r in ex_c['readings'])

html = []
A = html.append
A('<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">')
A('<title>ATU Tuning Algorithm</title>')
A('<meta name="description" content="How the Phoenix firmware tunes the K9HZ ATU: a measured search over 131,072 relay settings using only |Gamma|, illustrated on Smith charts.">')
A('<style>' + CSS + '</style></head><body>' + ARROWS + '<main>')
A('<div class="kicker">Phoenix firmware · ATU</div>')
A('<h1>How the ATU tunes</h1>')
A('<p class="lede">A measured search over 131,072 relay settings that uses nothing but the magnitude of the reflection coefficient. '
  'No model of the tuner, no element values and no calibration of the network.</p>')

A('<div class="callout"><b>In short</b><ul>'
  '<li>The tuner reads |Γ| at one relay setting at a time with a low-power carrier: about 99 ms a reading.</li>'
  '<li>It reads the bypass first. Good enough (SWR ≤ 1.3) stays bypassed; a near-total reflection is an antenna fault.</li>'
  '<li>Otherwise it <b>walks the valley</b> of each topology on a coarse, logarithmic grid of single-element codes, '
  'then <b>refines</b> the lowest point it found with a pattern search in code space.</li>'
  '<li>It stops at SWR 1.2, when refining can go no further, or after 120 readings, and stores the best setting it read for this band and antenna. '
  'A typical tune takes 35–75 readings, 3–7 s.</li></ul></div>')

A('<ul class="toc">'
  '<li><a href="#hw">1. The tuner</a></li><li><a href="#smith">2. Smith chart refresher</a></li>'
  '<li><a href="#reach">3. What the board can reach</a></li><li><a href="#problem">4. Why it needs a search</a></li>'
  '<li><a href="#algo">5. The algorithm</a></li><li><a href="#example">6. A tune, step by step</a></li>'
  '<li><a href="#others">7. Other loads</a></li><li><a href="#reading">8. Taking one reading</a></li>'
  '<li><a href="#perf">9. How well it does</a></li><li><a href="#limits">10. Limits</a></li>'
  '<li><a href="#code">11. Where it is in the code</a></li><li><a href="#method">About the figures</a></li></ul>')

# 1
A('<h2 id="hw">1. The tuner</h2>')
A('<p>The K9HZ ATU is an L network. Eight series inductors and eight shunt capacitors are switched by relays, each in binary-weighted steps, '
  'so a setting is two 8-bit codes: <b>L code</b> 0–255 and <b>C code</b> 0–255. A ninth relay, K17, moves the whole capacitor bank from one end '
  'of the inductors to the other, which gives the two <b>topologies</b>. K19 and K20 bypass the network, and K18 picks the output connector.</p>')
A('<figure>' + SCHEMATIC_FIXED + '<figcaption><b>Figure 1.</b> The ATU RF board, simplified. In <b>CL</b> the capacitors sit on the transmitter side of the coils, '
  'in <b>LC</b> on the antenna side. K18 (coax or balun) is not part of the search: it belongs to the antenna and is kept on every reading.</figcaption></figure>')
A(tbl([['L1–L8', '0.05, 0.1, 0.22, 0.45, 1.0, 2.2, 4.4, 8.8 µH', 'L code bit 0–7'],
       ['C1–C8', '10, 22, 47, 100, 220, 470, 1000, 2000 pF', 'C code bit 0–7'],
       ['K17', 'capacitor bank at the TX side (CL) or the antenna side (LC)', 'topology'],
       ['K19 + K20', 'network in line, or bypassed', '—']], ['Elements', 'Nominal values', 'Setting']))
A('<p>Each element is about 2.2 times the one before, more than double, so the code order is also the order of nominal value: '
  'code 8 (0.45 µH) is more than codes 1+2+4 together (0.37 µH). The steps are not even, though, and the board\'s strays and the coils\' self-resonance '
  'bend the real values away from nominal, most of all on 10 m. The tuner never uses the values. It ranks codes by what it measures.</p>'
  '<p>That makes 2 × 256 × 256 = <b>131,072</b> network settings, plus bypass. Reading them all at 99 ms each would take 3.6 hours.</p>')

# 2
A('<h2 id="smith">2. Smith chart refresher</h2>')
A('<p>A Smith chart plots the reflection coefficient Γ = (Z − 50)/(Z + 50) on the unit disc. The centre is a perfect 50 Ω match, the rim is total reflection, '
  'and the distance from the centre is |Γ|, so a circle around the centre is a circle of constant SWR. The upper half is inductive and the lower half capacitive.</p>'
  '<p>Each element of an L network moves the impedance along one family of circles:</p>'
  '<ul><li>a <b>series inductor</b> adds +jX, moving clockwise along a circle of constant resistance;</li>'
  '<li>a <b>shunt capacitor</b> adds +jB, moving clockwise along a circle of constant conductance.</li></ul>'
  '<p>To end at the centre, the last element (the one nearest the transmitter) must move along a circle through the centre: '
  'the r = 1 circle for a series coil, the g = 1 circle for a shunt capacitor. Working from the antenna side, the first element\'s job is to land on that circle.</p>')
A('<figure>' + fig_primer() + '<figcaption><b>Figure 2.</b> Two ideal matches. <span style="color:var(--lc)"><b>200 Ω on 20 m, LC</b></span>: a shunt 97 pF at the antenna moves the load down its conductance circle '
  'to the r = 1 circle (solid arrow), then a series 0.97 µH carries it up to the centre (dashed). <span style="color:var(--cl)"><b>12.5 Ω on 80 m, CL</b></span>: a series 0.94 µH at the antenna moves the load up its '
  'resistance circle to the g = 1 circle, then a shunt 1510 pF brings it to the centre. With ideal parts, LC can match loads with conductance below 1/50 S (resistance well above 50 Ω, or reactive) and CL loads with resistance below 50 Ω. '
  'On the real board, strays shrink and shift these regions, so the tuner tries both.</figcaption></figure>')

# 3
A('<h2 id="reach">3. What the board can reach</h2>')
A('<p>The real board is not two ideal elements. Each coil has self-capacitance and loss, the capacitors have lead inductance, and the tracks between them are short transmission lines. '
  'Figure 3 plots Γ at the transmitter for every one of the 65,536 settings of each topology, for a 200 Ω load on 20 m, using the circuit model of the board that the unit tests use as the simulated plant.</p>')
reach_svg, reach_n = fig_reach(ex)
A('<figure>' + reach_svg + '<div class="legend"><span class="key"><svg width="12" height="12"><circle cx="6" cy="6" r="2.5" class="pt-coarse"/></svg>the 81 single-element codes the walk reads from</span>'
  '<span class="key"><svg width="18" height="12"><line x1="0" y1="6" x2="18" y2="6" class="sc-swr"/></svg>SWR 1.5</span></div>'
  '<figcaption><b>Figure 3.</b> Every setting of each topology for 200 Ω on 20 m. Most of them pile up near the rim: only %d of the 65,536 LC settings and %d CL settings give an SWR below 2 (drawn larger). '
  'LC reaches SWR 1.17 for this load; CL gets no better than 1.7. '
  'The 81 neutral dots are the settings with at most one inductor and one capacitor in circuit, the grid the valley walk reads; they spread across the reachable region, which is why a walk on them finds the right neighbourhood.</figcaption></figure>' % tuple(reach_n))

# 4
A('<h2 id="problem">4. Why it needs a search</h2>')
A('<p>The SWR bridge measures forward and reflected power, so the tuner knows |Γ| but not its phase. It cannot tell from one reading which way the match lies, '
  'and it has no model to predict one. It can only read settings and compare them.</p>'
  '<p>Figure 4 shows |Γ| over the whole code space of each topology as SWR bands. The axes are logarithmic in code, so each single-element code is evenly spaced. '
  'Almost all of the space is useless (SWR above 5), and the good settings lie in a <b>narrow valley</b>, often long and diagonal: as L grows, the C that pairs with it changes too. '
  'Changing one element at a time from a point outside the valley usually makes things worse in both directions, so a search along one axis at a time stalls; '
  'a brute-force scan is far too slow. The search has to follow the valley.</p>')
A('<figure>' + fig_landscape([(ex, 0, '20 m, 200 Ω: LC'), ('40m_400', 0, '40 m, 400 Ω: LC')]) + band_legend() + '<div class="legend"><span class="key"><svg width="12" height="12"><circle cx="6" cy="6" r="4" class="mk-walk"/></svg>walk reading</span>'
  '<span class="key"><svg width="12" height="12"><circle cx="6" cy="6" r="4" class="mk-ref"/></svg>refine reading</span>'
  '<span class="key"><svg width="18" height="18"><circle cx="9" cy="9" r="7" class="mk-final"/></svg>result</span></div>'
  '<figcaption><b>Figure 4.</b> |Γ| of all 65,536 LC settings for two loads, with each tune\'s LC readings on top. <b>Left</b>, the worked example of section 6: the valley reaches below SWR 1.2 '
  'around L code 15, C code 1–2, squeezed into the bottom rows because small C codes are close together on this axis. <b>Right</b>, 400 Ω on 40 m: the valley is long and diagonal, '
  'and the walk crosses it on the coarse grid before the refine climbs along it to L 44, C 7 (SWR %.2f, %d readings).</figcaption></figure>' % (swr(traces['40m_400']['finalG']), len(traces['40m_400']['readings'])))

# 5
A('<h2 id="algo">5. The algorithm</h2>')
A('<p>The program is <code>ATU_PROG_TUNE</code> in <code>ATUTuneAlgo.cpp</code>. It is a <b>planner</b>: each time the engine has the carrier off, it asks the planner what to do next, '
  'and the planner answers "read this setting", "finish with this setting" or "fail". The planner keeps no list of what it has done. It replays its logic against the record of readings, '
  'and wherever it needs a |Γ| that is not in the record yet, it asks for that reading. So a setting is <b>never read twice</b>.</p>')
A('<h3>5.1 Bypass</h3><p>The first reading is with the network out. At SWR ≤ 1.3 the tuner stays bypassed and stores that. '
  'Above |Γ| = 0.98 (SWR 99) it stops with <code>ATU_FAIL_ANTENNA</code>: an open or a short, which no setting of the network will fix. Otherwise the search starts.</p>')
A('<h3>5.2 Walking the valley</h3>'
  '<p>For each topology in turn, LC first, the tuner walks along L on the coarse codes 0, 1, 2, 4, 8, …, 128. At each L it finds the best coarse C:</p>'
  '<ul><li><b>At the first L</b> (code 0) it sweeps C up from 0 until |Γ| has risen twice past its minimum.</li>'
  '<li><b>At later Ls</b> the valley is continuous, so it starts at the previous L\'s best C and descends: up while that gains, and if going up gained nothing at all, down while that gains.</li></ul>'
  '<p>The best C at each L is the <b>valley floor</b> there. The walk stops when the floor has risen twice (by more than 0.005 in |Γ|) since its lowest point, or when the Ls run out. '
  'The lowest floor point is that topology\'s <b>seed</b>.</p>')
A('<figure>' + fig_walk(ex) + band_legend() + '<figcaption><b>Figure 5.</b> The two walks of the 20 m / 200 Ω tune on the coarse grid. Each cell read shows its reading number; grey cells were never read. '
  'Red outlines mark the floor at each L, and the heavy outline the seed. In LC (readings 1–21) the floor falls to SWR 1.62 at L 16, then rises at L 32 and again at L 64, and the walk stops. '
  'CL (readings 22–38) never gets below 2.24. A whole walk takes about 17–20 readings.</figcaption></figure>')
A('<h3>5.3 Which valley to refine</h3>'
  '<ul><li>If the LC seed already reaches the target (SWR 1.5), LC is refined at once, and CL is walked only if that refine ends short of the target.</li>'
  '<li>Otherwise CL is walked too, and the lower of the two seeds is refined first. If that ends short of the target, the other is refined as well.</li>'
  '<li>Whatever happens, the tuner finishes with the best setting it has <i>read</i>, from either topology.</li></ul>')
A('<h3>5.4 Refining: pattern search in code space</h3>'
  '<p>From the seed, the refine is a compass search on the two codes, with steps that start at half each code\'s top set bit (at least 2):</p>'
  '<ol class="steps"><li>Read the four <b>axis</b> steps (L ± step, C ± step).</li>'
  '<li>If none gains by more than 0.003 in |Γ|, read the four <b>diagonals</b>.</li>'
  '<li>Move to the best candidate that gains, then <b>repeat the same move, doubled</b>, for as long as it keeps gaining (a pattern move: it races along the valley floor).</li>'
  '<li>When neither the axes nor the diagonals gain, <b>halve both steps</b>; when both are 1, the refine is done.</li></ol>')
fig6 = ('<figure>' + refine_svg + band_legend() +
        '<figcaption><b>Figure 6.</b> The LC refine of the 20 m / 200 Ω tune, codes on linear axes. The dashed ring is the seed (L 16, C 0, SWR 1.62). '
        'Red discs are refine readings, numbered; white discs are walk readings the refine looked up instead of reading again. '
        'The red line is the path of the base point: %s. With steps of 8 and 2 nothing gains; at 4 and 1, L 12 does, a pattern move up C reaches L 12 C 2, '
        'and steps of 2 then 1 find L 15 C 2 at SWR 1.19, below the 1.2 stop. %d readings in all.</figcaption></figure>'
        % (' → '.join('L %d C %d' % p for p in base_path), cnt[PH_REF_LC]))
A(fig6)
A('<h3>5.5 Stopping, and what is stored</h3>')
A(tbl([['<code>SRCH_BYPASS_SWR</code>', '1.3', 'stay bypassed at or below this'],
       ['<code>SRCH_GOOD_SWR</code>', '1.2', 'stop as soon as any reading is at or below this'],
       ['<code>SRCH_TARGET_SWR</code>', '1.5', 'result is <code>ATU_OK</code> at or below this, else <code>ATU_FAIL_NO_MATCH</code> (the best setting is still kept)'],
       ['<code>SRCH_FAULT_GAMMA</code>', '0.98', 'bypass above this: antenna fault'],
       ['<code>SRCH_FLOOR_RISE</code> / <code>_RISES</code>', '0.005 / 2', 'the walk stops after the valley floor rises twice'],
       ['<code>SRCH_SWEEP_RISES</code>', '2', 'the first C sweep stops after two rises past its minimum'],
       ['<code>SRCH_DESCENT_GAIN</code>', '0.002', 'a walk descent step must gain more than this'],
       ['<code>SRCH_MIN_GAIN</code>', '0.003', 'a refine move must gain more than this'],
       ['<code>SRCH_MAX_READINGS</code>', '120', 'hard limit, about 13 s'],
       ['<code>ATU_TUNE_TIMEOUT_MS</code>', '20000', 'the engine gives up on the whole program']],
      ['Constant', 'Value', 'Meaning']))
A('<p>The result is stored for the current band and antenna (<code>ED.ATU_State[band][antenna]</code>, with the frequency and the SWR). '
  'It goes back on the relays whenever that band and antenna are selected, so asking for a tune always means a <b>retune</b>: the search starts again from bypass and ignores the stored setting.</p>')

# 6 example
A('<h2 id="example">6. A tune, step by step</h2>')
A('<p>Here is the whole 20 m / 200 Ω tune on the Smith chart. Each dot is the Γ the transmitter saw at one reading. The faint clouds are every setting of the board, as in Figure 3.</p>')
A('<figure><div class="pair"><div><div class="subcap">Whole chart</div>%s</div><div><div class="subcap">Centre, |Γ| ≤ 0.4</div>%s</div></div>%s'
  '<figcaption><b>Figure 7.</b> The %d readings of the tune, joined in order on the left. The walks (hollow) scatter widely around the bypass point (grey square, SWR 4.0), and the readings at large L land out near the rim. '
  'The LC refine (filled blue) then closes in on the centre, crossing the SWR 2, 1.5 and 1.2 circles. The result (red ring) is SWR %.2f; '
  'the best the simulated board allows for this load is %.2f.</figcaption></figure>'
  % (trace_chart(ex, size=360), trace_chart(ex, view=0.4, size=360, lines=False), phase_legend(), n_ex, swr_ex, best_ex))
A('<figure>' + fig_convergence(ex) + phase_legend() + '<figcaption><b>Figure 8.</b> SWR of each reading (log scale) and the best so far (red line). '
  'The walks are cheap and noisy: they sample widely, including some very poor settings. Almost all the improvement below SWR 1.6 comes from the refine, '
  'which stops at the first reading below 1.2.</figcaption></figure>')
A(tbl([['Bypass', '1', '0'], ['LC walk', str(cnt[PH_WALK_LC]), '1–21'], ['CL walk', str(cnt[PH_WALK_CL]), '22–38'],
       ['LC refine', str(cnt[PH_REF_LC]), '39–54'], ['<b>Total</b>', '<b>%d</b>' % n_ex, '≈ %.1f s at 99 ms' % (n_ex * 0.099)]],
      ['Phase', 'Readings', 'Reading numbers'], numcols=(1,)))

# 7 others
A('<h2 id="others">7. Other loads</h2>')
A('<p>The same search on three more loads, each on a board whose coils differ from the reference by about 5 %, as another builder\'s board would. '
  'The tuner knows nothing about the difference.</p>')
A('<div class="trio">' + ''.join(others) + '</div>' + phase_legend())
A('<p><b>80 m, 12.5 Ω</b> is a CL load: the LC walk goes nowhere, the CL walk finds the valley at large C, and the refine settles at SWR 1.43 against a best of 1.41. '
  'The long tail of refine readings around L 16, C 80 is the pattern search halving its steps on a flat valley floor; it stops when the steps reach 1, because 1.43 is within the target. '
  '<b>40 m, 25 + j100 Ω</b> matches in LC. <b>20 m, 25 − j100 Ω</b> is beyond this board: the best of all 131,072 settings is SWR 2.96. '
  'The tuner returns <code>ATU_FAIL_NO_MATCH</code>, keeps the best setting it read (3.79), and the home screen says so.</p>')

# 8 reading
A('<h2 id="reading">8. Taking one reading</h2>')
A('<p>The engine (<code>ATUTune.cpp</code>) runs from the main loop and never blocks. On starting it puts the mode state machine in <code>ATU_TUNE_SPACE</code>: '
  'T/R relay in transmit, carrier off. The T/R relay then stays in transmit for the whole tune, so it switches twice per tune, not twice per reading. '
  'The ATU relays only ever switch with the carrier off.</p>')
A('<figure>' + TIMELINE + '<figcaption><b>Figure 9.</b> One reading. The bridge statistics are converted to |Γ| with the band\'s SWR bridge calibration, '
  'which is why the tuner refuses to run on a band whose bridge is uncalibrated. A forward power below 0.1 W fails the tune with <code>ATU_FAIL_NO_RF</code>.</figcaption></figure>')
A('<p>A tune starts from SSB or CW receive only, on a band where transmit is allowed, at the tune power (2 W by default, 0.5–10 W in the ATU menu). '
  'Any front-panel action except volume stops it, as do <code>TX;</code>, <code>RX;</code> and <code>AX0;</code> over CAT, and a band or antenna change made over CAT. '
  'A stopped or failed tune puts back the setting the relays had before it.</p>')

# 9 perf
A('<h2 id="perf">9. How well it does</h2>')
A('<p><b>In simulation</b> (<code>TunesOtherBuildsCloseToTheirBestOnEveryBand</code> in <code>ATUTune_test.cpp</code>): 15 loads, from 12.5 to 400 Ω with −100, 0 and +100 Ω of reactance, on 80, 40, 20, 15 and 10 m, '
  'each on a board with randomly perturbed coils.</p>')
A(tbl([['5 % build, ideal bridge', '50 of 51', '0.18', '55'],
       ['12 % build, leaky bridge', '46 of 51', '0.56', '56']],
      ['Board', 'Reachable loads tuned to SWR ≤ 1.5 (80–15 m)', '90th percentile shortfall', 'Median readings'], numcols=(1, 2, 3)))
A('<p>"Reachable" means the best setting of all is SWR 1.4 or better; "shortfall" is how far the tuned SWR is above the better of that best and 1.2, where the search may stop. '
  'The test fails if a change makes the search worse than these figures.</p>')
A('<p><b>On the bench</b> (KI3P prototype, 200 Ω dummy load on the coax output, 2 W):</p>')
A(tbl([['40 m', '37–40', '4.2 s', '1.17', 'LC'], ['20 m', '68–73', '6.0–6.9 s', '1.25', 'CL'], ['10 m', '42', '3.7 s', '1.30', 'CL']],
      ['Band', 'Readings', 'Time', 'SWR', 'Topology'], numcols=(1, 2, 3)))
A('<p>On the real board, 20 m into 200 Ω ended in CL rather than the LC the model prefers: the prototype\'s strays differ from the model, and the search, which only ranks measurements, simply followed what it read.</p>')

# 10 limits
A('<h2 id="limits">10. Limits</h2>'
  '<ul><li><b>Local search.</b> The refine only improves on the seed\'s neighbourhood. If the coarse grid misses a valley entirely, the refine will not find it. Walking both topologies, and refining the second when the first falls short, reduces this.</li>'
  '<li><b>|Γ| only.</b> With no phase, every decision is a comparison of two readings, so bridge noise near a flat valley floor can stop a refine a little early. The 0.002–0.005 gain thresholds keep it from chasing noise.</li>'
  '<li><b>Elements past resonance.</b> On 10 m the largest coils and capacitors are past their self-resonance and read badly. The search does not know, it simply passes over them.</li>'
  '<li><b>The frequency.</b> A setting is stored with the frequency it was tuned on. Moving far within the band does not trigger a retune; the operator presses Tune.</li>'
  '<li><b>Tune power.</b> The search keys a carrier for every reading: about 4–7 s of carrier at the tune power, into whatever the antenna presents.</li></ul>')

# 11 code
A('<h2 id="code">11. Where it is in the code</h2>')
A(tbl([['<code>ATUTuneAlgo.cpp</code>', '<code>TuneBegin</code>, <code>TuneNext</code>', 'the planner: one call per decision'],
       ['', '<code>BypassStep</code>, <code>WalkStep</code>, <code>RefineStep</code>', 'one function per phase'],
       ['', '<code>SweepC</code>, <code>DescendC</code>', 'the best coarse C at one L'],
       ['', '<code>AfterWalk</code>, <code>AfterRefine</code>', 'which topology to walk or refine next'],
       ['<code>ATUTune.cpp</code>', '<code>ATUTuneStart</code>, <code>ATUTuneService</code>, <code>Finish</code>', 'the engine: refusals, relays, carrier, readings, timeouts'],
       ['', '<code>ATURetune</code>, <code>ATUApplyStoredIfChanged</code>', 'button 16 / menu / CAT, and the stored setting on band or antenna changes'],
       ['<code>ATUBoard.cpp</code>', '<code>SetATUPacked</code>', 'every relay in one I²C update'],
       ['<code>ModeSm.drawio</code>', '<code>ATU_TUNE_SPACE</code>, <code>ATU_TUNE_MARK</code>', 'carrier off / on with the T/R relay held in transmit'],
       ['<code>test/ATUTune_test.cpp</code>', '<code>ATUTuneAlgoTest</code>', 'the search against the simulated board'],
       ['<code>test/atu_plant/</code>', '<code>ATUModel</code>', 'circuit model of the RF board (the simulated plant)']],
      ['File', 'Functions', 'Role']))
A('<p>More: <code>wiki/firmware/atu-control.md</code> (menu, CAT commands, per-antenna settings), <code>wiki/roadmap/atu-auto-tuning.md</code> (how the design got here), '
  '<code>code/docs/ATU_RF_Board_Characterisation.md</code> (the VNA measurements behind the model).</p>')

A('<h2 id="method">About the figures</h2>'
  '<p>Every trace here is the firmware\'s own search code, compiled for the host and run in the unit-test harness against the circuit model of the board '
  '(<code>test/atu_plant/ATUModel.cpp</code>, fitted to VNA measurements of the RF board), with the coil values perturbed by 5 % and an ideal bridge, so the readings equal the true |Γ|. '
  'The model is least trustworthy on 10 m and near series resonances. The bench results in section 9 are from the real radio.</p>'
  '<p>To regenerate the page, run the test <code>ATUTuneAlgoTest.DocFigureData</code> with <code>ATU_DOC_DIR</code> set to a directory, '
  'then <code>code/tools/atu_vna/build_tuning_doc.py</code> on that directory; the script\'s header has the commands.</p>')
A('</main></body></html>')

open(OUT, 'w').write(''.join(html))
print('wrote', OUT, os.path.getsize(OUT) // 1024, 'KB')
