"""Compose the one-page technical sheet for the enclosure.

    pip install manifold3d numpy pillow
    python tools/spec_sheet.py

Writes docs/img/spec-sheet.png: assembled view, exploded view, orthographic
views, a section cut, and the two tables that matter before printing.

Every view is a render of the same model the STLs come from, and every dimension
line is projected through that view's own camera -- so the numbers on the sheet
cannot drift away from the geometry. Change params.json, run this, and the sheet
is correct again.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from manifold3d import Manifold
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "enclosure"))
sys.path.insert(0, str(ROOT / "tools"))

import render  # noqa: E402
from model import (  # noqa: E402
    build_base,
    build_board_mock,
    build_lid,
    build_sensor_mock,
    derive,
    height_budget,
    load_params,
    to_arrays,
)

# --------------------------------------------------------------------------- #
# Style
# --------------------------------------------------------------------------- #

CANVAS = (1600, 1040)
PAPER = (255, 255, 255)
INK = (24, 30, 42)
MUTED = (104, 116, 134)
RULE = (216, 222, 230)
PANEL = (250, 251, 253)
ZEBRA = (243, 245, 249)
ACCENT = (20, 105, 180)
WARN = (176, 88, 24)

FONT_CANDIDATES = {
    False: [
        "C:/Windows/Fonts/segoeui.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ],
    True: [
        "C:/Windows/Fonts/segoeuib.ttf",
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    ],
}
_font_cache = {}

DIA = "\u00f8"
TIMES = "\u00d7"


def font(size, bold=False):
    key = (size, bold)
    if key not in _font_cache:
        for path in FONT_CANDIDATES[bold]:
            if Path(path).exists():
                _font_cache[key] = ImageFont.truetype(path, size)
                break
        else:
            _font_cache[key] = ImageFont.load_default(size)
    return _font_cache[key]


# --------------------------------------------------------------------------- #
# Drawing helpers
# --------------------------------------------------------------------------- #

class Placed:
    """A rendered view pasted into the sheet, plus the mapping to sheet pixels."""

    def __init__(self, view, origin, scale):
        self.view = view
        self.origin = np.asarray(origin, dtype=float)
        self.scale = scale

    def at(self, points):
        """World mm -> sheet pixels."""
        return self.view.project(points) * self.scale + self.origin


def paste_view(sheet, view, box, align="center"):
    """Fit a rendered view inside `box` and paste it. Returns a Placed."""
    x0, y0, x1, y1 = box
    vw, vh = view.size
    scale = min((x1 - x0) / vw, (y1 - y0) / vh)
    w, h = max(int(vw * scale), 1), max(int(vh * scale), 1)

    ox = x0 + ((x1 - x0) - w) // 2
    oy = y0 if align == "top" else y0 + ((y1 - y0) - h) // 2

    img = Image.fromarray(view.rgba, "RGBA").resize((w, h), Image.LANCZOS)
    sheet.alpha_composite(img, (ox, oy))
    return Placed(view, (ox, oy), scale)


def panel(draw, box, number, title, subtitle=None):
    """Panel frame with a numbered heading. Returns the usable inner box."""
    x0, y0, x1, y1 = box
    draw.rounded_rectangle([x0, y0, x1, y1], 7, fill=PANEL, outline=RULE, width=1)

    draw.text((x0 + 16, y0 + 12), f"{number}", font=font(15, True), fill=ACCENT)
    draw.text((x0 + 36, y0 + 12), title, font=font(15, True), fill=INK)
    top = y0 + 34
    if subtitle:
        draw.text((x0 + 36, top), subtitle, font=font(12), fill=MUTED)
        top += 19
    return (x0 + 14, top + 6, x1 - 14, y1 - 12)


def arrowhead(draw, tip, direction, size=5, fill=ACCENT):
    d = np.asarray(direction, dtype=float)
    norm = np.linalg.norm(d)
    if norm < 1e-9:
        return
    d = d / norm
    perp = np.array([-d[1], d[0]])
    tip = np.asarray(tip, dtype=float)
    a = tip - d * size * 2.0 + perp * size * 0.7
    b = tip - d * size * 2.0 - perp * size * 0.7
    draw.polygon([tuple(tip), tuple(a), tuple(b)], fill=fill)


def dimension(draw, p0, p1, text, offset=(0, 0), text_side=None, size=11):
    """A dimension line between two sheet points, offset perpendicular."""
    p0 = np.asarray(p0, dtype=float) + np.asarray(offset, dtype=float)
    p1 = np.asarray(p1, dtype=float) + np.asarray(offset, dtype=float)
    draw.line([tuple(p0), tuple(p1)], fill=ACCENT, width=1)
    arrowhead(draw, p0, p0 - p1)
    arrowhead(draw, p1, p1 - p0)

    mid = (p0 + p1) / 2
    f = font(size, True)
    tw = draw.textlength(text, font=f)
    if text_side is None:
        direction = p1 - p0
        text_side = (-direction[1], direction[0])
    side = np.asarray(text_side, dtype=float)
    side = side / max(np.linalg.norm(side), 1e-9)
    pos = mid + side * (size + 2) - np.array([tw / 2, size / 2 + 1])
    draw.rectangle([pos[0] - 3, pos[1] - 2, pos[0] + tw + 3, pos[1] + size + 3], fill=PANEL)
    draw.text(tuple(pos), text, font=f, fill=ACCENT)


def leader(draw, anchor, text_xy, lines):
    """Dot on the part, elbow line, label text running to the right."""
    anchor = np.asarray(anchor, dtype=float)
    text_xy = np.asarray(text_xy, dtype=float)
    elbow = np.array([text_xy[0] - 12, text_xy[1] + 7])
    draw.line([tuple(anchor), tuple(elbow), (text_xy[0] - 2, elbow[1])], fill=MUTED, width=1)
    draw.ellipse(
        [anchor[0] - 2.5, anchor[1] - 2.5, anchor[0] + 2.5, anchor[1] + 2.5], fill=ACCENT
    )

    y = text_xy[1]
    for i, line in enumerate(lines):
        f = font(12, i == 0)
        draw.text((text_xy[0], y), line, font=f, fill=INK if i == 0 else MUTED)
        y += 15


def table(draw, box, rows, highlight_last=False):
    """Two-column table filling `box` from the top."""
    x0, y0, x1, y1 = box
    row_h = min(20.0, (y1 - y0) / max(len(rows), 1))
    y = float(y0)
    for i, (label, value) in enumerate(rows):
        last = highlight_last and i == len(rows) - 1
        if last:
            draw.line([x0, y - 1, x1, y - 1], fill=RULE, width=1)
        elif i % 2 == 0:
            draw.rectangle([x0, y, x1, y + row_h - 2], fill=ZEBRA)
        f = font(12, last)
        draw.text((x0 + 8, y + 2), label, font=f, fill=INK if last else MUTED)
        vw = draw.textlength(value, font=f)
        draw.text((x1 - vw - 8, y + 2), value, font=f, fill=INK)
        y += row_h
    return y


def caption(draw, xy, text, bold=False):
    draw.text(xy, text, font=font(11, bold), fill=MUTED)


def callout(draw, tip, text, dx=-46, dy=-44, size=11):
    """Dot on the geometry, elbow, short label. Negative dx runs it leftwards."""
    tip = np.asarray(tip, dtype=float)
    elbow = tip + np.array([dx, dy])
    tail = elbow + np.array([-22 if dx < 0 else 22, 0])
    draw.line([tuple(tip), tuple(elbow), tuple(tail)], fill=MUTED, width=1)
    draw.ellipse([tip[0] - 3, tip[1] - 3, tip[0] + 3, tip[1] + 3], fill=ACCENT)

    f = font(size, True)
    tw = draw.textlength(text, font=f)
    x = tail[0] - tw - 4 if dx < 0 else tail[0] + 4
    y = tail[1] - size / 2 - 1
    # A callout can land anywhere over a render, including the dark lid, so it
    # carries its own background rather than trusting what is behind it.
    draw.rectangle([x - 4, y - 3, x + tw + 4, y + size + 4], fill=PANEL)
    draw.text((x, y), text, font=f, fill=ACCENT)


# --------------------------------------------------------------------------- #
# Geometry for the views
# --------------------------------------------------------------------------- #

def cut_away(part):
    """Halve a solid at y = 0, keeping the far side, for the section view."""
    big = 400.0
    knife = Manifold.cube([big, big, big], center=True).translate([0, -big / 2, 0])
    return part - knife


# --------------------------------------------------------------------------- #
# Panels
# --------------------------------------------------------------------------- #

def panel_assembled(sheet, draw, box, p, L, parts):
    base, lid, _, sensor = parts
    inner = panel(draw, box, "1", "ASSEMBLED", "The closed shell, isometric.")

    view = render.render_view(
        [
            (*to_arrays(base), 0.0),
            (*to_arrays(sensor), 0.0, render.SENSOR),
            (*to_arrays(lid), L.cavity_top),
        ],
        width=420,
    )
    placed = paste_view(sheet, view, (inner[0] + 34, inner[1] + 10, inner[2] - 34, inner[3] - 30))

    hx, hy = L.outer_x / 2, L.outer_y / 2
    a = placed.at([[-hx, -hy, 0], [hx, -hy, 0]])
    dimension(draw, a[0], a[1], f"{L.outer_x:.1f} mm", offset=(0, 24), text_side=(0, 1))
    b = placed.at([[hx, -hy, 0], [hx, hy, 0]])
    dimension(draw, b[0], b[1], f"{L.outer_y:.1f} mm", offset=(22, 10), text_side=(1, 0))
    c = placed.at([[-hx, -hy, 0], [-hx, -hy, L.total_height]])
    dimension(draw, c[0], c[1], f"{L.total_height:.1f} mm", offset=(-24, 0), text_side=(-1, 0))


def panel_exploded(sheet, draw, box, p, L, parts):
    base, lid, board, sensor = parts
    inner = panel(draw, box, "2", "EXPLODED", "Every part, in assembly order.")

    s, b, sh = p["sensor"], p["board"], p["shell"]
    lift_lid = L.cavity_top + 54.0
    lift_sensor = 26.0

    view = render.render_view(
        [
            (*to_arrays(base), 0.0),
            (*to_arrays(board), 0.0, render.BOARD),
            (*to_arrays(sensor), lift_sensor, render.SENSOR),
            (*to_arrays(lid), lift_lid),
        ],
        width=320,
    )
    placed = paste_view(
        sheet, view, (inner[0], inner[1], inner[0] + 232, inner[3] - 104), align="top"
    )

    label_x = inner[0] + 250
    leader(
        draw,
        placed.at([[L.sensor_cx - L.window_d / 2 - 2, 0, lift_lid + sh["lid_thickness"]]])[0],
        (label_x, inner[1] + 2),
        ["Lid", f"{sh['lid_thickness']:.1f} mm plate", f"window {DIA}{L.window_d:.1f} mm"],
    )
    leader(
        draw,
        placed.at([[L.sensor_cx, 0, L.floor + L.pedestal + lift_sensor + s["body_height"]]])[0],
        (label_x, inner[1] + 76),
        [
            "HLK-ZW111",
            f"body {DIA}{s['body_diameter']:.0f} {TIMES} {s['body_height']:.0f} mm",
            f"face {DIA}{s['active_area_diameter']:.0f} mm",
        ],
    )
    leader(
        draw,
        placed.at([[L.board_cx, 0, L.floor + sh["board_standoff"] + b["height"]]])[0],
        (label_x, inner[1] + 150),
        [
            "ESP32-S3",
            f"{b['length']:.0f} {TIMES} {b['width']:.0f} {TIMES} {b['height']:.0f} mm",
            "on four posts",
        ],
    )
    leader(
        draw,
        placed.at([[-L.outer_x / 2, -L.outer_y / 5, L.total_height * 0.45]])[0],
        (label_x, inner[1] + 224),
        ["Base", "seat, posts,", "one USB-C opening"],
    )

    note = [
        "No divider, no screws.",
        "The module is clamped between the ring seat",
        "below and the lid's counterbore above, so the",
        "shell needs neither a false floor nor bosses.",
    ]
    y = inner[3] - 96
    draw.line([inner[0], y - 10, inner[2], y - 10], fill=RULE, width=1)
    for i, line in enumerate(note):
        draw.text((inner[0], y), line, font=font(12, i == 0), fill=INK if i == 0 else MUTED)
        y += 16


def panel_bom(sheet, draw, box, p, L):
    inner = panel(draw, box, "7", "BILL OF MATERIALS", "Everything the build needs.")
    s, b = p["sensor"], p["board"]
    rows = [
        ("ESP32-S3, native USB", f"{b['length']:.0f} {TIMES} {b['width']:.0f}"
                                 f" {TIMES} {b['height']:.0f} mm"),
        ("HLK-ZW111 module", f"{DIA}{s['body_diameter']:.0f} {TIMES}"
                             f" {s['body_height']:.0f} mm"),
        ("Printed base", "base.stl"),
        ("Printed lid", "lid.stl"),
        ("Silicone wire, 30 AWG", "6 cores, stranded"),
        ("USB-C cable", "data, not charge-only"),
    ]
    table(draw, (inner[0], inner[1], inner[2], inner[3] - 78), rows)

    y = inner[3] - 72
    draw.line([inner[0], y - 8, inner[2], y - 8], fill=RULE, width=1)
    note = [
        "No battery, no radio.",
        "The device is powered by the port it is plugged into and",
        "speaks only USB HID. Every radio is another way for the",
        "stored secret to leave the device, and it needs none.",
    ]
    for i, line in enumerate(note):
        draw.text((inner[0], y), line, font=font(12, i == 0), fill=INK if i == 0 else MUTED)
        y += 16


def panel_ortho(sheet, draw, box, p, L, parts):
    base, lid, _, sensor = parts
    inner = panel(draw, box, "3", "ORTHOGRAPHIC", "External dimensions, millimetres.")

    closed = [
        (*to_arrays(base), 0.0),
        (*to_arrays(sensor), 0.0, render.SENSOR),
        (*to_arrays(lid), L.cavity_top),
    ]
    hx, hy = L.outer_x / 2, L.outer_y / 2
    half = (inner[2] - inner[0]) // 2

    # Top, on the left.
    caption(draw, (inner[0], inner[1] - 2), "TOP", bold=True)
    top_view = render.render_view(closed, yaw=0.0, pitch=0.0, width=240, margin=6)
    top = paste_view(
        sheet, top_view, (inner[0] + 34, inner[1] + 40, inner[0] + half - 30, inner[3] - 60)
    )

    pts = top.at([[-hx, hy, L.total_height], [hx, hy, L.total_height]])
    dimension(draw, pts[0], pts[1], f"{L.outer_x:.1f}", offset=(0, -14), text_side=(0, -1))
    pts = top.at([[-hx, -hy, L.total_height], [-hx, hy, L.total_height]])
    dimension(draw, pts[0], pts[1], f"{L.outer_y:.1f}", offset=(-26, 0), text_side=(-1, 0))

    tip = top.at(
        [[L.sensor_cx - L.window_d / 2 * 0.71, -L.window_d / 2 * 0.71, L.total_height]]
    )[0]
    callout(draw, tip, f"{DIA}{L.window_d:.1f}", dx=-30, dy=34)

    # The USB end, on the right.
    caption(draw, (inner[0] + half + 16, inner[1] - 2), "USB-C WALL", bold=True)
    end_yaw = 90.0 if L.port_wall == "-x" else 0.0
    end_view = render.render_view(closed, yaw=end_yaw, pitch=-90.0, width=200, margin=6)
    end = paste_view(
        sheet, end_view, (inner[0] + half + 48, inner[1] + 60, inner[2] - 44, inner[3] - 76)
    )
    corner = [-hx, -hy, 0] if L.port_wall == "-x" else [-hx, -hy, 0]
    pts = end.at([corner, [corner[0], corner[1], L.total_height]])
    dimension(draw, pts[0], pts[1], f"{L.total_height:.1f}", offset=(-18, 0), text_side=(-1, 0))
    pts = end.at(list(L.port_points()))
    dimension(draw, pts[0], pts[1], f"{L.port_w:.1f}", offset=(0, 30), text_side=(0, 1))
    caption(draw, (inner[0] + half + 16, inner[3] - 38),
            "Only the native port is opened.")
    caption(draw, (inner[0] + half + 16, inner[3] - 22),
            "The UART bridge stays behind the wall.")


def panel_section(sheet, draw, box, p, L, parts):
    base, lid, board, sensor = parts
    inner = panel(draw, box, "4", "SECTION", "Cut at the centreline, components in place.")

    meshes = [
        (*to_arrays(cut_away(base)), 0.0),
        (*to_arrays(cut_away(board)), 0.0, render.BOARD),
        (*to_arrays(cut_away(sensor)), 0.0, render.SENSOR),
        (*to_arrays(cut_away(lid.translate([0, 0, L.cavity_top]))), 0.0),
    ]
    view = render.render_view(meshes, yaw=-24.0, pitch=-74.0, width=420)
    placed = paste_view(sheet, view, (inner[0], inner[1] + 6, inner[2], inner[3] - 96))

    s, sh = p["sensor"], p["shell"]
    y = inner[3] - 88
    draw.line([inner[0], y - 10, inner[2], y - 10], fill=RULE, width=1)
    rows = [
        (
            "Reading face",
            f"flush with the lid: the module's top {L.counterbore_depth:.1f} mm"
            f" sits inside the counterbore",
        ),
        (
            "Seat ring",
            f"lifts the module {L.pedestal:.1f} mm and is notched for the pigtail",
        ),
        (
            "Standoff",
            f"{sh['board_standoff']:.1f} mm under the board, which is also the"
            " pigtail's route",
        ),
    ]
    for label, text in rows:
        draw.text((inner[0], y), label, font=font(12, True), fill=INK)
        draw.text((inner[0] + 96, y), text, font=font(12), fill=MUTED)
        y += 17

    # Two pointers, both on things the section exists to show.
    callout(
        draw,
        placed.at([[L.sensor_cx, 0.4, L.cavity_top + sh["lid_thickness"]]])[0],
        f"reading face {DIA}{s['active_area_diameter']:.0f}",
        dx=-64,
        dy=-62,
    )
    callout(
        draw,
        placed.at([[L.board_cx, 0.4, L.floor + sh["board_standoff"]]])[0],
        f"standoff {sh['board_standoff']:.1f}",
        dx=-44,
        dy=44,
    )


def panel_budget(sheet, draw, box, p, L):
    inner = panel(draw, box, "5", "HEIGHT BUDGET",
                  f"Where each of the {L.total_height:.1f} mm goes.")
    rows = [(label, f"{mm:.2f}") for label, mm in height_budget(p, L) if abs(mm) > 1e-9]
    rows.append(("External height", f"{L.total_height:.2f} mm"))
    table(draw, (inner[0], inner[1], inner[2], inner[3] - 42), rows, highlight_last=True)

    stacked = (
        p["shell"]["floor"] + L.board_column + p["sensor"]["body_height"]
        + p["shell"]["lid_thickness"]
    )
    y = inner[3] - 36
    draw.line([inner[0], y - 8, inner[2], y - 8], fill=RULE, width=1)
    draw.text((inner[0] + 8, y), f"Stacked instead of side by side: {stacked:.1f} mm.",
              font=font(12, True), fill=WARN)
    draw.text((inner[0] + 8, y + 17),
              f"{p['board']['height']:.1f} of the {L.total_height:.1f} mm is the"
              " ESP32-S3 module itself.",
              font=font(12), fill=MUTED)


def panel_printing(sheet, draw, box, p, L, parts):
    base, lid = parts[0], parts[1]
    inner = panel(draw, box, "6", "PRINTING", "Both parts flat on the bed, no supports.")
    rows = [
        ("Material", "PLA or PETG"),
        ("Layer height", "0.20 mm"),
        ("Perimeters", "3"),
        ("Infill", "25%"),
        ("Supports", "none"),
        ("Orientation", "as exported"),
        ("Lid clearance", f"{p['fit']['lid_clearance']:.2f} mm per side"),
        (
            "Solid volume",
            f"base {base.volume() / 1000:.1f} + lid {lid.volume() / 1000:.1f} cm3",
        ),
    ]
    table(draw, (inner[0], inner[1], inner[2], inner[3]), rows)


# --------------------------------------------------------------------------- #
# Sheet
# --------------------------------------------------------------------------- #

def header(draw, L):
    draw.text((28, 22), "STANDALONE FINGERPRINT KEY", font=font(28, True), fill=INK)
    draw.text(
        (28, 58),
        f"HLK-ZW111 + ESP32-S3  \u00b7  two-part friction-fit enclosure  \u00b7  "
        f"{L.outer_x:.1f} {TIMES} {L.outer_y:.1f} {TIMES} {L.total_height:.1f} mm",
        font=font(14),
        fill=MUTED,
    )

    notes = [
        "Side-by-side layout: the sensor sits beside the board, not on top of it.",
        "One USB-C opening (native port only). No LED window, no divider, no screws.",
        "Generated from enclosure/params.json, so the STLs and this sheet cannot disagree.",
    ]
    y = 24
    for line in notes:
        tw = draw.textlength(line, font=font(12))
        draw.text((CANVAS[0] - 28 - tw, y), line, font=font(12), fill=MUTED)
        y += 17
    draw.line([28, 96, CANVAS[0] - 28, 96], fill=RULE, width=1)


def footer(draw):
    y = CANVAS[1] - 34
    draw.line([28, y - 12, CANVAS[0] - 28, y - 12], fill=RULE, width=1)
    draw.text(
        (28, y),
        "Component dimensions are the measured ones. Check board.usb_offset_from_center"
        " against your board before printing: it decides which of the two USB-C ports"
        " is exposed.",
        font=font(12),
        fill=MUTED,
    )
    label = "Standalone-Fingerprint-Key  \u00b7  docs/enclosure.md"
    tw = draw.textlength(label, font=font(12))
    draw.text((CANVAS[0] - 28 - tw, y), label, font=font(12), fill=MUTED)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--params", default=str(ROOT / "enclosure" / "params.json"))
    ap.add_argument("--out", default=str(ROOT / "docs" / "img" / "spec-sheet.png"))
    args = ap.parse_args()

    p = load_params(args.params)
    L = derive(p)
    parts = (
        build_base(p, L),
        build_lid(p, L),
        build_board_mock(p, L),
        build_sensor_mock(p, L),
    )

    sheet = Image.new("RGBA", CANVAS, PAPER + (255,))
    draw = ImageDraw.Draw(sheet)

    header(draw, L)
    panel_assembled(sheet, draw, (28, 112, 556, 460), p, L, parts)
    panel_section(sheet, draw, (28, 476, 556, 972), p, L, parts)
    panel_exploded(sheet, draw, (572, 112, 1024, 648), p, L, parts)
    panel_bom(sheet, draw, (572, 664, 1024, 972), p, L)
    panel_ortho(sheet, draw, (1040, 112, 1572, 510), p, L, parts)
    panel_budget(sheet, draw, (1040, 526, 1572, 748), p, L)
    panel_printing(sheet, draw, (1040, 764, 1572, 972), p, L, parts)
    footer(draw)

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.convert("RGB").save(out, optimize=True)
    print(f"{out.name}  {CANVAS[0]}x{CANVAS[1]}  {out.stat().st_size // 1024} KB")


if __name__ == "__main__":
    raise SystemExit(main())
