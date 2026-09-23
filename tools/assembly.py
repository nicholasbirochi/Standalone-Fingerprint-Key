"""Render the enclosure with the ESP32-S3 and the HLK-ZW111 in place.

    pip install manifold3d numpy
    python tools/assembly.py

Produces two images:

    docs/img/assembly.png   lid lifted, both components seated
    docs/img/closed.png     the finished thing, from outside

The components are the measured envelopes from params.json, positioned by the
same `Layout` the shell is built from -- so if a part does not meet the surface
it is supposed to meet, it shows up here rather than in PLA.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

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
    load_params,
    to_arrays,
)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--params", default=str(ROOT / "enclosure" / "params.json"))
    ap.add_argument("--out", default=str(ROOT / "docs" / "img"))
    args = ap.parse_args()

    p = load_params(args.params)
    L = derive(p)
    out = Path(args.out)

    base = to_arrays(build_base(p, L))
    lid = to_arrays(build_lid(p, L))
    board = to_arrays(build_board_mock(p, L))
    sensor = to_arrays(build_sensor_mock(p, L))

    # The lid's own origin is its underside; in the assembly it sits at the top
    # of the cavity. Lifting it from there keeps the parts aligned in X and Y.
    seated = L.cavity_top

    jobs = [
        (
            "assembly.png",
            [
                (*base, 0.0),
                (*board, 0.0, render.BOARD),
                (*sensor, 0.0, render.SENSOR),
                (*lid, seated + 17.0),
            ],
            900,
        ),
        (
            "closed.png",
            [
                (*base, 0.0),
                (*sensor, 0.0, render.SENSOR),
                (*lid, seated),
            ],
            760,
        ),
    ]

    for name, meshes, width in jobs:
        w, h, n = render.render(meshes, out / name, width=width)
        print(f"{name:<14} {w}x{h}  {n} facets")


if __name__ == "__main__":
    raise SystemExit(main())
