"""Render the footprint options side by side.

    python tools/variants.py

Writes docs/img/variants.png. The shell can be laid out three ways once you fix
the height at 13 mm, and the only honest way to choose between them is to see
them at the same scale with their numbers underneath.
"""

from __future__ import annotations

import argparse
import copy
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "enclosure"))
sys.path.insert(0, str(ROOT / "tools"))

import render  # noqa: E402
import spec_sheet as ss  # noqa: E402
from model import (  # noqa: E402
    build_base,
    build_board_mock,
    build_lid,
    build_sensor_mock,
    derive,
    load_params,
    to_arrays,
)

CANVAS = (1420, 700)

VARIANTS = [
    (
        "SQUARE  (default)",
        {"board_long_axis": "y", "square": True},
        "The smallest square these parts fit in. USB-C on a side wall.",
    ),
    (
        "RECTANGULAR",
        {"board_long_axis": "x", "square": False},
        "build.py --no-square --board-long-axis x",
    ),
]


def build(p, overrides):
    params = copy.deepcopy(p)
    params["shell"].update(overrides)
    L = derive(params)
    return params, L


def draw_variant(sheet, draw, box, name, params, L, blurb, scale_ref):
    x0, y0, x1, y1 = box
    draw.rounded_rectangle([x0, y0, x1, y1], 8, fill=ss.PANEL, outline=ss.RULE, width=1)
    draw.text((x0 + 18, y0 + 14), name, font=ss.font(17, True), fill=ss.INK)
    draw.text(
        (x0 + 18, y0 + 38),
        f"{L.outer_x:.1f} {ss.TIMES} {L.outer_y:.1f} {ss.TIMES} {L.total_height:.1f} mm",
        font=ss.font(13),
        fill=ss.ACCENT,
    )
    draw.text((x0 + 18, y0 + 58), blurb, font=ss.font(12), fill=ss.MUTED)

    base, lid = build_base(params, L), build_lid(params, L)
    board, sensor = build_board_mock(params, L), build_sensor_mock(params, L)
    view = render.render_view(
        [
            (*to_arrays(base), 0.0),
            (*to_arrays(board), 0.0, render.BOARD),
            (*to_arrays(sensor), 0.0, render.SENSOR),
            (*to_arrays(lid), L.cavity_top + 22.0),
        ],
        width=int(330 * L.outer_x / scale_ref),
    )
    ss.paste_view(sheet, view, (x0 + 14, y0 + 84, x1 - 14, y1 - 108))

    rows = [
        ("Footprint", f"{L.outer_x * L.outer_y / 100:.1f} cm2"),
        ("Bounding volume", f"{L.outer_x * L.outer_y * L.total_height / 1000:.1f} cm3"),
        ("Plastic", f"{(base.volume() + lid.volume()) / 1000:.1f} cm3"),
        ("USB-C wall", "end" if L.port_wall == "-x" else "side"),
    ]
    ss.table(draw, (x0 + 18, y1 - 98, x1 - 18, y1 - 14), rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--params", default=str(ROOT / "enclosure" / "params.json"))
    ap.add_argument("--out", default=str(ROOT / "docs" / "img" / "variants.png"))
    args = ap.parse_args()

    p = load_params(args.params)
    built = [(name, *build(p, ov), blurb) for name, ov, blurb in VARIANTS]
    # One scale for every render, so the sizes are actually comparable.
    scale_ref = max(L.outer_x for _, _, L, _ in built)

    sheet = Image.new("RGBA", CANVAS, ss.PAPER + (255,))
    draw = ImageDraw.Draw(sheet)

    draw.text((28, 22), "FOOTPRINT OPTIONS", font=ss.font(24, True), fill=ss.INK)
    draw.text(
        (28, 54),
        "Same parts, same 13.0 mm height, same scale. Only the plan changes.",
        font=ss.font(13),
        fill=ss.MUTED,
    )
    draw.line([28, 84, CANVAS[0] - 28, 84], fill=ss.RULE, width=1)

    width = (CANVAS[0] - 56 - 16) // 2
    for i, (name, params, L, blurb) in enumerate(built):
        x = 28 + i * (width + 16)
        draw_variant(sheet, draw, (x, 100, x + width, 636), name, params, L, blurb, scale_ref)

    note = (
        "40 × 40 mm is not reachable at this height: side by side the two parts need"
        " 42.8 mm of cavity (18.9 board + 2.0 for the pigtail + 21.9 sensor), which is"
        " 46.4 mm outside. A 40 mm square forces the sensor back on top of the board,"
        " and that is the 18 mm case again."
    )
    draw.line([28, 654, CANVAS[0] - 28, 654], fill=ss.RULE, width=1)
    draw.text((28, 666), note, font=ss.font(12), fill=ss.WARN)

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.convert("RGB").save(out, optimize=True)
    print(f"{out.name}  {CANVAS[0]}x{CANVAS[1]}  {out.stat().st_size // 1024} KB")


if __name__ == "__main__":
    raise SystemExit(main())
