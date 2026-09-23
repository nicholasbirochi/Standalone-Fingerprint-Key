"""Regenerate the enclosure STLs from params.json.

    pip install manifold3d numpy
    python enclosure/build.py

Writes enclosure/stl/base.stl and enclosure/stl/lid.stl, then prints the height
budget and a list of sanity checks. Both parts come out of a CSG kernel that
only produces manifold solids, so anything it writes is watertight by
construction -- the checks below are about the design being *sensible*, not
about the mesh being valid. A failed check exits non-zero.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from model import (  # noqa: E402
    build_base,
    build_lid,
    derive,
    height_budget,
    load_params,
    write_stl,
)

HERE = Path(__file__).resolve().parent


def checks(p, L):
    """(label, passed, detail) for everything worth refusing to print over."""
    s, b, f, sh, ft = p["sensor"], p["board"], p["fit"], p["shell"], p["features"]

    lip_overlap = (s["body_diameter"] - L.window_d) / 2
    seat_aspect = L.pedestal / ft["sensor_seat_wall"] if ft["sensor_seat_wall"] else 99
    # Careful not to shadow the board params: these are the opening's endpoints.
    left, right = L.port_points()
    if L.port_wall == "-x":
        port_edge = L.inner_y / 2 - max(abs(left[1]), abs(right[1]))
    else:
        port_edge = L.inner_x / 2 - max(abs(left[0]), abs(right[0]))

    return [
        (
            "lid lip clears the reading face",
            ft["bezel_lip"] <= s["active_area_height"],
            f"lip {ft['bezel_lip']:.2f} mm vs face standing {s['active_area_height']:.2f} mm proud",
        ),
        (
            "lid lip still retains the module",
            lip_overlap >= 0.8,
            f"radial overlap {lip_overlap:.2f} mm",
        ),
        (
            "sensor seat has room for the pigtail",
            L.pedestal >= s["cable_thickness"],
            f"pedestal {L.pedestal:.2f} mm vs cable {s['cable_thickness']:.2f} mm",
        ),
        (
            "sensor seat is not a flagpole",
            seat_aspect <= 4.0,
            f"{L.pedestal:.2f} mm tall on a {ft['sensor_seat_wall']:.2f} mm wall"
            f" (ratio {seat_aspect:.1f})",
        ),
        (
            "wall above the lid ledge is printable",
            sh["wall"] - sh["lid_ledge"] >= 0.8,
            f"{sh['wall'] - sh['lid_ledge']:.2f} mm",
        ),
        (
            "USB opening clears the port",
            L.port_h >= b["usb_height"] and L.port_w >= b["usb_width"],
            f"{L.port_w:.2f} x {L.port_h:.2f} mm around a"
            f" {b['usb_width']:.1f} x {b['usb_height']:.1f} mm port",
        ),
        (
            "USB opening stays clear of the corner",
            port_edge >= f["port_edge_margin"] - 1e-6,
            f"{port_edge:.2f} mm of wall to the corner",
        ),
        (
            "USB opening fits under the lid",
            L.port_cz + L.port_h / 2 <= L.cavity_top + 1e-6,
            f"top of opening at {L.port_cz + L.port_h / 2:.2f} mm,"
            f" cavity ceiling at {L.cavity_top:.2f} mm",
        ),
        (
            "guide ribs stay inside the wall",
            ft["guide_thickness"] < sh["wall"] - 0.3,
            f"rib {ft['guide_thickness']:.2f} mm vs wall {sh['wall']:.2f} mm",
        ),
        (
            "standoff clears bottom-side components",
            sh["board_standoff"] >= b["bottom_component_height"],
            f"{sh['board_standoff']:.2f} mm vs {b['bottom_component_height']:.2f} mm",
        ),
    ]


def report(p, L, parts):
    """Print the height budget and the checks that matter before printing."""
    sh = p["shell"]

    print()
    print("Standalone-Fingerprint-Key enclosure")
    print("=" * 66)
    print(f"  external         {L.outer_x:.1f} x {L.outer_y:.1f} x {L.total_height:.1f} mm")
    print(f"  cavity           {L.inner_x:.1f} x {L.inner_y:.1f} x {L.cavity_height:.1f} mm")
    print(f"  height set by    {L.driver}")
    print(f"  width set by     {L.width_driver}")
    print(f"  board runs along {p['shell'].get('board_long_axis', 'x').upper()},"
          f" USB opening in the {L.port_wall.upper()} wall")
    print(f"  bounding volume  {L.outer_x * L.outer_y * L.total_height / 1000:.1f} cm3")
    print()

    print("Height budget")
    print("-" * 66)
    for label, mm in height_budget(p, L):
        if abs(mm) < 1e-9:
            continue
        print(f"  {label:<42} {mm:>6.2f}")
    print(f"  {'':<42} {'-' * 6}")
    print(f"  {'external height':<42} {L.total_height:>6.2f} mm")
    print()

    stacked = sh["floor"] + L.board_column + p["sensor"]["body_height"] + sh["lid_thickness"]
    print(f"  Stacked instead of side by side: {stacked:.2f} mm, before any screw bosses.")
    print(f"  Side by side saves {stacked - L.total_height:.2f} mm of height and costs")
    print(f"  {p['sensor']['body_diameter'] + sh['part_gap']:.1f} mm of length.")
    print()

    print("Checks")
    print("-" * 66)
    failed = 0
    for label, ok, detail in checks(p, L):
        print(f"  [{'ok' if ok else 'XX'}] {label:<38} {detail}")
        failed += 0 if ok else 1

    print()
    print("Output")
    print("-" * 66)
    for name, path, tri, part in parts:
        print(
            f"  {name:<6} {path.name:<10} {tri:>6} triangles"
            f"   {part.volume() / 1000:>6.2f} cm3"
        )
    print()
    return failed


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--params", default=str(HERE / "params.json"))
    ap.add_argument("--out", default=str(HERE / "stl"))
    ap.add_argument("--square", action=argparse.BooleanOptionalAction, default=None,
                    help="pad the shorter axis until the footprint is square")
    ap.add_argument("--board-long-axis", choices=["x", "y"],
                    help="override which way the board's long side runs")
    args = ap.parse_args()

    p = load_params(args.params)
    if args.square is not None:
        p["shell"]["square"] = args.square
    if args.board_long_axis:
        p["shell"]["board_long_axis"] = args.board_long_axis
    L = derive(p)

    out = Path(args.out)
    parts = []
    for name, builder in (("base", build_base), ("lid", build_lid)):
        part = builder(p, L)
        path = out / f"{name}.stl"
        parts.append((name, path, write_stl(part, path), part))

    failed = report(p, L, parts)
    if failed:
        print(f"{failed} check(s) failed -- adjust params.json before printing.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
