#!/usr/bin/env python3
"""Draw the front panel button maps of the calibration guide.

Each map is an inline SVG of the front panel: the 18 buttons (0-17, three
columns by six rows, numbered left to right and top to bottom) and the four
encoders. The controls a screen uses are highlighted and labelled; the rest
are greyed out. The SVGs use the page's CSS variables, so they follow its
light and dark themes.

The maps are written into Calibration_Guide.html in place, between
    <!-- buttonmap:NAME -->  and  <!-- /buttonmap:NAME -->
Run after changing a table below:
    python3 make_button_maps.py
"""

import html
import pathlib
import re
import sys

GUIDE = pathlib.Path(__file__).with_name("Calibration_Guide.html")

# Encoders, by position on the panel
ENCODERS = ["volume", "centre", "filter", "fine"]
ENCODER_NAMES = {"volume": "Volume", "centre": "Centre tune", "filter": "Filter", "fine": "Fine tune"}

# For each screen: {button number: label} and {encoder: label}.
# A label may contain "|" to break it onto two lines.
MAPS = {
    "home": {
        "title": "Home screen",
        "buttons": {0: "Select", 1: "Menu", 2: "Band +", 3: "Zoom", 4: "Reset|tuning", 5: "Band −",
                    6: "Mode", 7: "Demod.", 8: "Centre tune|step", 9: "Noise|reduction", 10: "Notch",
                    11: "Fine tune|step", 12: "Filter", 13: "Decoder", 14: "Direct|freq. entry",
                    17: "Home"},
        "encoders": {"volume": "Volume", "centre": "Tune", "filter": "Filter width|menu scroll",
                     "fine": "Fine tune"},
    },
    "frequency": {
        "title": "Frequency calibration",
        "buttons": {7: "Demod.|(pick SAM)", 15: "Step size", 17: "Save|and exit"},
        "encoders": {"filter": "Correction|factor"},
    },
    "rxiq": {
        "title": "Receive IQ calibration",
        "buttons": {2: "Band +", 5: "Band −", 15: "Step size", 16: "Automatic|(all bands)",
                    17: "Save|and exit"},
        "encoders": {"volume": "Amplitude", "filter": "Phase"},
    },
    "txiq-dual": {
        "title": "Transmit IQ calibration, dual VFO",
        "buttons": {2: "Band +", 5: "Band −", 15: "Step size", 16: "Automatic|(all bands)",
                    17: "Save|and exit"},
        "encoders": {"volume": "Amplitude", "filter": "Phase", "fine": "TX attenuation"},
    },
    "txiq-single": {
        "title": "Transmit IQ calibration, single VFO",
        "buttons": {2: "Band +", 5: "Band −", 15: "Step size", 17: "Save|and exit"},
        "encoders": {"volume": "Amplitude", "filter": "Phase", "fine": "TX attenuation"},
    },
    "power-auto": {
        "title": "Automatic power and SWR calibration",
        "buttons": {0: "Start / confirm|enter / accept", 3: "Abort", 12: "Skip load|(level 1)",
                    14: "W / dBm|retry failed", 15: "Step size", 17: "Leave"},
        "encoders": {"volume": "Level|(setup only)", "filter": "L44 turns|wattmeter reading"},
    },
    "power-manual": {
        "title": "Manual power calibration",
        "buttons": {0: "Record|the point", 1: "Automatic|calibration", 2: "Band +", 3: "Start|again",
                    5: "Band −", 12: "SSB step", 14: "W / dBm", 15: "Step size", 17: "Save|and exit"},
        "encoders": {"volume": "TX attenuation", "filter": "Measured|power"},
    },
    "smeter": {
        "title": "S-meter level",
        "buttons": {0: "Done"},
        "encoders": {"filter": "Offset (dB)"},
    },
}

PAD_X, PAD_Y = 20, 40          # top-left of the button pad
COL_W, ROW_H = 120, 74         # pitch of the button grid
R_BTN = 15
PANEL_BOTTOM = PAD_Y + 5 * ROW_H + 50
ENC_Y0 = PANEL_BOTTOM + 14      # first row of encoders
ENC_ROW_H = 100
W, H = 400, ENC_Y0 + 2 * ENC_ROW_H + 12
# Encoder positions as on the panel drawings: Volume and Centre tune above,
# Filter and Fine tune below, under the left and right button columns
ENC_POS = {"volume": (0, 0), "centre": (2, 0), "filter": (0, 1), "fine": (2, 1)}


def text_lines(x, y, label, cls, dy=13):
    lines = label.split("|")
    out = []
    for i, line in enumerate(lines):
        out.append(f'<text x="{x}" y="{y + i * dy}" class="{cls}" text-anchor="middle">'
                   f'{html.escape(line)}</text>')
    return "".join(out)


def svg(name, spec):
    parts = [f'<svg viewBox="0 0 {W} {H}" class="bmap" role="img" '
             f'aria-label="Front panel controls: {html.escape(spec["title"])}">']
    parts.append(f'<rect x="{PAD_X}" y="{PAD_Y - 26}" width="{3 * COL_W}" '
                 f'height="{PANEL_BOTTOM - (PAD_Y - 26)}" rx="10" class="bm-panel"/>')
    for n in range(18):
        col, row = n % 3, n // 3
        cx = PAD_X + 60 + col * COL_W
        cy = PAD_Y + row * ROW_H
        on = n in spec["buttons"]
        parts.append(f'<circle cx="{cx}" cy="{cy}" r="{R_BTN}" class="{"bm-on" if on else "bm-off"}"/>')
        parts.append(f'<text x="{cx}" y="{cy + 4.5}" class="{"bm-num-on" if on else "bm-num-off"}" '
                     f'text-anchor="middle">{n}</text>')
        if on:
            parts.append(text_lines(cx, cy + R_BTN + 13, spec["buttons"][n], "bm-lab"))
    # Encoders, two by two under the pad
    for enc in ENCODERS:
        col, row = ENC_POS[enc]
        cx = PAD_X + 60 + col * COL_W
        cy = ENC_Y0 + 28 + row * ENC_ROW_H
        on = enc in spec["encoders"]
        parts.append(f'<circle cx="{cx}" cy="{cy}" r="26" class="{"bm-enc-on" if on else "bm-enc-off"}"/>')
        parts.append(text_lines(cx, cy + 3.5, ENCODER_NAMES[enc].replace(" ", "|") if " " in ENCODER_NAMES[enc]
                                else ENCODER_NAMES[enc], "bm-encname-on" if on else "bm-encname-off", dy=10))
        if on:
            parts.append(text_lines(cx, cy + 26 + 14, spec["encoders"][enc], "bm-lab"))
    parts.append("</svg>")
    return "".join(parts)


def main():
    page = GUIDE.read_text(encoding="utf-8")
    count = 0
    for name, spec in MAPS.items():
        pattern = re.compile(rf"(<!-- buttonmap:{re.escape(name)} -->)(.*?)(<!-- /buttonmap:{re.escape(name)} -->)",
                             re.S)
        if not pattern.search(page):
            continue
        page = pattern.sub(lambda m: m.group(1) + svg(name, spec) + m.group(3), page)
        count += 1
    unknown = set(re.findall(r"<!-- buttonmap:([\w-]+) -->", page)) - set(MAPS)
    if unknown:
        sys.exit(f"Unknown button maps in the guide: {', '.join(sorted(unknown))}")
    GUIDE.write_text(page, encoding="utf-8")
    print(f"Wrote {count} button maps into {GUIDE.name}")


if __name__ == "__main__":
    main()
