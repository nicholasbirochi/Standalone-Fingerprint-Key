"""Parametric model of the Standalone-Fingerprint-Key enclosure.

The shell uses a side-by-side layout: the ESP32-S3 and the HLK-ZW111 sit next to
each other on the floor rather than stacked. Stacking them is the obvious
arrangement and it is what made the first revision 18 mm tall, because it adds
the two tallest parts in the build together and then needs screw bosses to hold
a deep lid on. Side by side, the height is whichever single column is tallest
and the lid becomes a flat plate.

Run `python enclosure/build.py` to regenerate the STLs and print the height
budget that shows where every millimetre goes.
"""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from manifold3d import CrossSection, JoinType, Manifold

# Cutting tools overshoot the solid by this much so subtracted faces never land
# exactly coplanar with the surface they punch through.
EPS = 0.01


# --------------------------------------------------------------------------- #
# Parameters
# --------------------------------------------------------------------------- #

def load_params(path):
    """Read params.json, dropping the underscore-prefixed documentation keys."""
    raw = json.loads(Path(path).read_text(encoding="utf-8"))
    return {
        group: {k: v for k, v in values.items() if not k.startswith("_")}
        for group, values in raw.items()
        if isinstance(values, dict)
    }


@dataclass(frozen=True)
class Layout:
    """Every derived dimension, computed once from params.json."""

    # footprint
    inner_x: float
    inner_y: float
    outer_x: float
    outer_y: float
    recess_x: float
    recess_y: float
    inner_radius: float
    recess_radius: float

    # heights
    floor: float
    cavity_height: float
    cavity_top: float
    total_height: float

    # board
    board_pocket_x: float
    board_pocket_y: float
    board_cx: float
    board_cy: float
    port_w: float
    port_h: float
    port_wall: str
    port_along: float
    port_cz: float

    # sensor
    sensor_pocket_d: float
    sensor_cx: float
    pedestal: float
    counterbore_depth: float
    seat_outer_d: float
    seat_inner_d: float
    window_d: float

    # bookkeeping for the height report
    board_column: float
    sensor_column: float
    driver: str
    width_driver: str

    @property
    def size(self):
        return (self.outer_x, self.outer_y, self.total_height)

    def port_points(self):
        """The two ends of the USB opening's width, on the outer wall face.

        In the end wall the opening is positioned relative to the case; in a
        side wall it follows the board, which is what it is cut for.
        """
        if self.port_wall == "-x":
            x = -self.outer_x / 2
            return (
                [x, self.port_along - self.port_w / 2, self.port_cz],
                [x, self.port_along + self.port_w / 2, self.port_cz],
            )
        y = -self.outer_y / 2
        cx = self.board_cx + self.port_along
        return (
            [cx - self.port_w / 2, y, self.port_cz],
            [cx + self.port_w / 2, y, self.port_cz],
        )

    def board_gaps(self):
        """Clearance between the board pocket and the cavity wall, per side."""
        bx, by = self.board_pocket_x / 2, self.board_pocket_y / 2
        return {
            "-x": (self.board_cx - bx) + self.inner_x / 2,
            "+x": self.inner_x / 2 - (self.board_cx + bx),
            "-y": (self.board_cy - by) + self.inner_y / 2,
            "+y": self.inner_y / 2 - (self.board_cy + by),
        }


def derive(p):
    """Turn the raw parameters into the dimensions the geometry actually uses."""
    s, b, f, sh, ft = p["sensor"], p["board"], p["fit"], p["shell"], p["features"]

    # The USB-C port is on the board's short edge, so the board's orientation
    # also decides which wall the port opens through.
    along_x = sh.get("board_long_axis", "x") == "x"
    long_side = b["length"] + 2 * f["part_clearance"]
    short_side = b["width"] + 2 * f["part_clearance"]
    board_pocket_x = long_side if along_x else short_side
    board_pocket_y = short_side if along_x else long_side
    port_wall = "-x" if along_x else "-y"

    sensor_pocket_d = s["body_diameter"] + 2 * f["part_clearance"]

    # The sensor always sits beside the board along X; that is the layout.
    inner_x = board_pocket_x + sh["part_gap"] + sensor_pocket_d

    # Two independent stacks compete for the cavity height; the taller one wins
    # and the other simply gets slack. board.height is the full envelope, and
    # the standoff under it is what the sensor's pigtail runs through.
    # The module does not sit entirely inside the cavity: its top 1.2 mm or so
    # lives in the lid's counterbore, which is what clamps it.
    counterbore_depth = sh["lid_thickness"] - ft["bezel_lip"]

    board_column = sh["board_standoff"] + b["height"] + f["vertical_clearance"]
    sensor_column = (
        s["body_height"] - counterbore_depth + s["cable_thickness"] + f["cable_headroom"]
    )
    cavity_height = max(board_column, sensor_column)
    driver = "board envelope" if board_column >= sensor_column else "sensor + pigtail routing"

    cavity_top = sh["floor"] + cavity_height
    total_height = cavity_top + sh["lid_thickness"]

    # Only the native USB port is opened, so the cut is off-centre. If that
    # pushes it into the rounded corner of the cavity, the shell widens rather
    # than quietly breaking out through the corner.
    port_w = b["usb_width"] + 2 * f["port_clearance"]
    port_h = b["usb_height"] + 2 * f["port_clearance"]
    port_along = b["usb_offset_from_center"]

    # In the end wall the opening is centred on the case, so an off-centre port
    # can push the cavity wider. In a side wall it is centred on the board
    # instead, which is already well clear of the corners.
    port_span = 2 * (abs(port_along) + port_w / 2 + f["port_edge_margin"])
    inner_y = max(board_pocket_y, sensor_pocket_d)
    if port_wall == "-x":
        inner_y = max(inner_y, port_span)

    if inner_y == port_span and port_span > max(board_pocket_y, sensor_pocket_d):
        width_driver = "USB opening (off-centre port)"
    elif sensor_pocket_d >= board_pocket_y:
        width_driver = "sensor diameter"
    else:
        width_driver = "board width"

    if sh.get("square"):
        side = max(inner_x, inner_y)
        if side > inner_y:
            width_driver = "squared off"
        inner_x = inner_y = side

    return Layout(
        inner_x=inner_x,
        inner_y=inner_y,
        outer_x=inner_x + 2 * sh["wall"],
        outer_y=inner_y + 2 * sh["wall"],
        recess_x=inner_x + 2 * sh["lid_ledge"],
        recess_y=inner_y + 2 * sh["lid_ledge"],
        inner_radius=max(0.5, sh["corner_radius"] - sh["wall"]),
        recess_radius=max(0.5, sh["corner_radius"] - sh["wall"] + sh["lid_ledge"]),
        floor=sh["floor"],
        cavity_height=cavity_height,
        cavity_top=cavity_top,
        total_height=total_height,
        board_pocket_x=board_pocket_x,
        board_pocket_y=board_pocket_y,
        # The board is pushed against whichever wall its port opens through,
        # otherwise the connector does not reach the hole cut for it.
        board_cx=-inner_x / 2 + board_pocket_x / 2,
        board_cy=(
            0.0 if port_wall == "-x" else -inner_y / 2 + board_pocket_y / 2
        ),
        port_w=port_w,
        port_h=port_h,
        port_wall=port_wall,
        port_along=port_along,
        port_cz=sh["floor"] + sh["board_standoff"] + b["usb_center_from_board_bottom"],
        sensor_pocket_d=sensor_pocket_d,
        sensor_cx=inner_x / 2 - sensor_pocket_d / 2,
        # Lift the module until its top face meets the counterbore ceiling. Any
        # lower and the lid closes over a module it is not actually touching,
        # which is how a sensor ends up rattling under a lid that looks right.
        pedestal=cavity_height - s["body_height"] + counterbore_depth,
        counterbore_depth=counterbore_depth,
        # The seat matches the module body, not the pocket: keeping it clear of
        # the cavity wall avoids a tangent surface, which is both a weak print
        # and a non-manifold edge in the mesh.
        seat_outer_d=s["body_diameter"],
        seat_inner_d=s["body_diameter"] - 2 * ft["sensor_seat_wall"],
        window_d=s["active_area_diameter"] + 2 * f["window_clearance"],
        board_column=board_column,
        sensor_column=sensor_column,
        driver=driver,
        width_driver=width_driver,
    )


def height_budget(p, L):
    """Rows of (label, millimetres) that add up to the external height."""
    b, f, sh = p["board"], p["fit"], p["shell"]
    return [
        ("floor", sh["floor"]),
        ("standoff under the board", sh["board_standoff"]),
        ("board envelope", b["height"]),
        ("clearance over the board", f["vertical_clearance"]),
        ("slack (cavity driven by " + L.driver + ")", L.cavity_height - L.board_column),
        ("lid", sh["lid_thickness"]),
    ]


# --------------------------------------------------------------------------- #
# 2D helpers
# --------------------------------------------------------------------------- #

def rounded_rect(w, h, r, segs):
    """A rounded rectangle, centred on the origin."""
    r = min(r, w / 2 - 1e-6, h / 2 - 1e-6)
    core = CrossSection.square([w - 2 * r, h - 2 * r], center=True)
    return core.offset(r, JoinType.Round, 2.0, segs)


def _cyl(height, diameter, segs):
    return Manifold.cylinder(height, diameter / 2, circular_segments=segs)


def _box(x0, x1, y0, y1, z0, z1):
    """An axis-aligned box from two opposite corners, in any order."""
    lo = (min(x0, x1), min(y0, y1), min(z0, z1))
    hi = (max(x0, x1), max(y0, y1), max(z0, z1))
    return Manifold.cube([hi[i] - lo[i] for i in range(3)]).translate(list(lo))


# --------------------------------------------------------------------------- #
# Parts
# --------------------------------------------------------------------------- #

def build_base(p, L):
    """Tray: floor, walls, lid recess, board posts, sensor seat, USB opening."""
    sh, ft = p["shell"], p["features"]
    segs = p["render"]["circular_segments"]

    body = rounded_rect(L.outer_x, L.outer_y, sh["corner_radius"], segs).extrude(
        L.total_height
    )

    cavity = (
        rounded_rect(L.inner_x, L.inner_y, L.inner_radius, segs)
        .extrude(L.cavity_height + EPS)
        .translate([0, 0, L.floor])
    )
    recess = (
        rounded_rect(L.recess_x, L.recess_y, L.recess_radius, segs)
        .extrude(sh["lid_thickness"] + EPS)
        .translate([0, 0, L.cavity_top])
    )
    body = body - cavity - recess

    # Four posts under the PCB corners, and L-shaped guides wherever the board
    # does not already butt against a wall. The posts carry the weight; the
    # guides stop it sliding.
    #
    # Each L is two boxes that deliberately overlap in the corner rather than
    # meeting face to face: a coplanar butt leaves a non-manifold edge, and a
    # guide flush against a wall has the same problem, which is why every arm
    # runs `gt` past the pocket edge instead of stopping on it.
    dx = L.board_pocket_x / 2 - ft["post_inset"] - ft["post_diameter"] / 2
    dy = L.board_pocket_y / 2 - ft["post_inset"] - ft["post_diameter"] / 2
    bx, by = L.board_pocket_x / 2, L.board_pocket_y / 2
    gl, gt, gh = ft["guide_length"], ft["guide_thickness"], ft["guide_height"]
    gaps = L.board_gaps()
    tol = 0.3

    for sx in (-1, 1):
        for sy in (-1, 1):
            post = _cyl(sh["board_standoff"], ft["post_diameter"], segs)
            body = body + post.translate(
                [L.board_cx + sx * dx, L.board_cy + sy * dy, L.floor]
            )

            x_in = L.board_cx + sx * bx
            y_in = L.board_cy + sy * by
            gap_x = gaps["+x" if sx > 0 else "-x"]
            gap_y = gaps["+y" if sy > 0 else "-y"]
            z0, z1 = L.floor, L.floor + gh

            if gap_y > tol:  # arm along X, standing in the Y clearance
                body = body + _box(
                    x_in - sx * gl, x_in + sx * gt, y_in, y_in + sy * gt, z0, z1
                )
            if gap_x > tol:  # arm along Y, standing in the X clearance
                body = body + _box(
                    x_in, x_in + sx * gt, y_in - sy * gl, y_in + sy * gt, z0, z1
                )

    # Ring that lifts the sensor against the underside of the lid, notched on the
    # board side so the pigtail can leave through the middle.
    seat = _cyl(L.pedestal, L.seat_outer_d, segs) - _cyl(
        L.pedestal + 2 * EPS, L.seat_inner_d, segs
    ).translate([0, 0, -EPS])
    cable_w = p["sensor"]["cable_width"] + 2 * p["fit"]["part_clearance"]
    notch = Manifold.cube(
        [L.seat_outer_d / 2 + EPS, cable_w, L.pedestal + 2 * EPS]
    ).translate([-L.seat_outer_d / 2 - EPS, -cable_w / 2, -EPS])
    body = body + (seat - notch).translate([L.sensor_cx, 0, L.floor])

    # USB-C opening. Built as a rounded slot: extrude the profile along Z, then
    # stand it up so the extrusion axis points through the wall it tunnels.
    a, b = L.port_points()
    centre = [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, L.port_cz]
    if L.port_wall == "-x":
        slot = (
            rounded_rect(L.port_h, L.port_w, ft["port_corner_radius"], segs)
            .extrude(sh["wall"] + 2 * EPS)
            .rotate([0, 90, 0])
            .translate([-L.outer_x / 2 - EPS, centre[1], L.port_cz])
        )
    else:
        slot = (
            rounded_rect(L.port_w, L.port_h, ft["port_corner_radius"], segs)
            .extrude(sh["wall"] + 2 * EPS)
            .rotate([-90, 0, 0])
            .translate([centre[0], -L.outer_y / 2 - EPS, L.port_cz])
        )
    body = body - slot

    # Scallop in the rim so a fingernail can lift the lid; the shell has no screws.
    pry_depth = (sh["wall"] - sh["lid_ledge"]) + 0.3
    pry = _cyl(sh["lid_thickness"] + EPS, ft["pry_notch_width"], segs).translate(
        [0, -L.outer_y / 2 - ft["pry_notch_width"] / 2 + pry_depth, L.cavity_top]
    )
    return body - pry


def build_lid(p, L):
    """Plate that drops into the recess, with a through window for the sensor."""
    sh, ft, f = p["shell"], p["features"], p["fit"]
    segs = p["render"]["circular_segments"]
    clr = f["lid_clearance"]

    lid = rounded_rect(
        L.recess_x - 2 * clr, L.recess_y - 2 * clr, L.recess_radius - clr, segs
    ).extrude(sh["lid_thickness"])

    # Through hole for the reading face. Nothing may cover a capacitive sensor,
    # so this is a real hole, not a thin membrane.
    window = _cyl(sh["lid_thickness"] + 2 * EPS, L.window_d, segs).translate(
        [L.sensor_cx, 0, -EPS]
    )
    # Counterbore from below; the ring left above it retains the module.
    counterbore = _cyl(
        L.counterbore_depth + EPS, L.sensor_pocket_d, segs
    ).translate([L.sensor_cx, 0, -EPS])

    return lid - window - counterbore


# --------------------------------------------------------------------------- #
# Component stand-ins
#
# These are not printed parts. They are the measured envelopes of the two
# components, positioned exactly where the shell expects them, so that a render
# shows whether things actually meet. Building the assembly view this way is
# what caught the sensor sitting 1.2 mm below the lid's counterbore.
# --------------------------------------------------------------------------- #

def build_board_mock(p, L):
    """The ESP32-S3 envelope, sitting on its posts."""
    b, sh = p["board"], p["shell"]
    segs = p["render"]["circular_segments"]
    return (
        rounded_rect(b["length"], b["width"], 1.2, segs)
        .extrude(b["height"])
        .translate([L.board_cx, L.board_cy, L.floor + sh["board_standoff"]])
    )


def build_sensor_mock(p, L):
    """The HLK-ZW111: body, plus the reading face standing proud of it."""
    s = p["sensor"]
    segs = p["render"]["circular_segments"]
    body = _cyl(s["body_height"], s["body_diameter"], segs)
    face = _cyl(s["active_area_height"], s["active_area_diameter"], segs).translate(
        [0, 0, s["body_height"]]
    )
    return (body + face).translate([L.sensor_cx, 0, L.floor + L.pedestal])


def to_arrays(part):
    """Manifold -> (triangles, face normals), for the renderer."""
    mesh = part.to_mesh()
    verts = np.asarray(mesh.vert_properties, dtype=np.float64)[:, :3]
    tris = verts[np.asarray(mesh.tri_verts, dtype=np.int64)]
    normals = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    lengths = np.linalg.norm(normals, axis=1, keepdims=True)
    normals = np.divide(normals, lengths, out=np.zeros_like(normals), where=lengths > 0)
    return tris, normals


# --------------------------------------------------------------------------- #
# STL output
# --------------------------------------------------------------------------- #

def write_stl(part, path):
    """Write a binary STL. Returns the triangle count."""
    mesh = part.to_mesh()
    verts = np.asarray(mesh.vert_properties, dtype=np.float64)[:, :3]
    tris = np.asarray(mesh.tri_verts, dtype=np.int64)

    a, b, c = verts[tris[:, 0]], verts[tris[:, 1]], verts[tris[:, 2]]
    normals = np.cross(b - a, c - a)
    lengths = np.linalg.norm(normals, axis=1, keepdims=True)
    normals = np.divide(normals, lengths, out=np.zeros_like(normals), where=lengths > 0)

    floats = np.hstack([normals, a, b, c]).astype("<f4")
    records = np.zeros((len(tris), 50), dtype=np.uint8)
    records[:, :48] = floats.view(np.uint8).reshape(len(tris), 48)

    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as fh:
        fh.write(b"Standalone-Fingerprint-Key".ljust(80, b"\0"))
        fh.write(struct.pack("<I", len(tris)))
        fh.write(records.tobytes())
    return len(tris)
