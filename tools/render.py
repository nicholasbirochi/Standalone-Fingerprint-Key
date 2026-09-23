"""Render the enclosure STLs to PNG for the README and the docs.

    python tools/render.py

Dependencies are numpy and the standard library -- no Pillow, no headless GL.
The meshes are projected isometrically, back faces are culled, and the survivors
are rasterised with a z-buffer at 3x then box-filtered down, which is enough
anti-aliasing for a part this simple. The background stays transparent so the
images read correctly in both GitHub themes.
"""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
SS = 3  # supersampling factor

# Light direction in view space, and the two tones the shading interpolates
# between. Mid-tones, so the part stays legible on a white or a dark page.
LIGHT = np.array([-0.35, -0.40, 0.85])
LIGHT /= np.linalg.norm(LIGHT)
SHADOW = np.array([54, 67, 86], dtype=float)
HIGHLIGHT = np.array([198, 210, 224], dtype=float)
SHELL = (SHADOW, HIGHLIGHT)

# Stand-in components get their own palettes so an assembly view reads at a
# glance: green for the board, near-black for the sensor module.
BOARD = (np.array([24, 58, 42], dtype=float), np.array([126, 190, 148], dtype=float))
SENSOR = (np.array([26, 28, 33], dtype=float), np.array([132, 138, 150], dtype=float))


# --------------------------------------------------------------------------- #
# I/O
# --------------------------------------------------------------------------- #

def read_stl(path):
    """Read a binary STL into (triangles, normals)."""
    data = Path(path).read_bytes()
    count = struct.unpack("<I", data[80:84])[0]
    rec = np.frombuffer(data[84 : 84 + 50 * count], dtype=np.uint8).reshape(count, 50)
    flat = rec[:, :48].copy().view("<f4").reshape(count, 12).astype(np.float64)
    return flat[:, 3:].reshape(count, 3, 3), flat[:, 0:3]


def write_png(rgba, path):
    """Write an RGBA image (H, W, 4) uint8 as a PNG."""
    h, w, _ = rgba.shape
    raw = np.hstack([np.zeros((h, 1), np.uint8), rgba.reshape(h, w * 4)])

    def chunk(tag, payload):
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))

    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw.tobytes(), 9))
        + chunk(b"IEND", b"")
    )
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


# --------------------------------------------------------------------------- #
# Projection and rasterisation
# --------------------------------------------------------------------------- #

def view_matrix(yaw_deg, pitch_deg):
    """Rotate world -> view: yaw about Z, then pitch about X."""
    yaw, pitch = np.radians(yaw_deg), np.radians(pitch_deg)
    rz = np.array(
        [[np.cos(yaw), -np.sin(yaw), 0], [np.sin(yaw), np.cos(yaw), 0], [0, 0, 1]]
    )
    rx = np.array(
        [[1, 0, 0], [0, np.cos(pitch), -np.sin(pitch)], [0, np.sin(pitch), np.cos(pitch)]]
    )
    return rx @ rz


def rasterise(tris, colors, width, height):
    """Z-buffer fill. tris is (n, 3, 3) in screen space, +z toward the camera."""
    color_buf = np.zeros((height, width, 3), dtype=np.float64)
    alpha = np.zeros((height, width), dtype=bool)
    depth = np.full((height, width), -np.inf)

    for tri, rgb in zip(tris, colors):
        x0 = max(int(np.floor(tri[:, 0].min())), 0)
        x1 = min(int(np.ceil(tri[:, 0].max())) + 1, width)
        y0 = max(int(np.floor(tri[:, 1].min())), 0)
        y1 = min(int(np.ceil(tri[:, 1].max())) + 1, height)
        if x1 <= x0 or y1 <= y0:
            continue

        ax, ay, az = tri[0]
        bx, by, bz = tri[1]
        cx, cy, cz = tri[2]
        det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(det) < 1e-12:
            continue

        ys, xs = np.mgrid[y0:y1, x0:x1]
        px, py = xs + 0.5, ys + 0.5
        w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / det
        w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / det
        w2 = 1.0 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            continue

        z = w0 * az + w1 * bz + w2 * cz
        win = inside & (z > depth[y0:y1, x0:x1])
        if not win.any():
            continue
        depth[y0:y1, x0:x1][win] = z[win]
        color_buf[y0:y1, x0:x1][win] = rgb
        alpha[y0:y1, x0:x1][win] = True

    return color_buf, alpha


class View:
    """A rendered image plus the mapping that produced it.

    Keeping the projection around is what lets the spec sheet put a dimension
    line exactly on an edge of the part rather than somewhere near it.
    """

    def __init__(self, rgba, matrix, lo, hi, scale, margin, facets=0):
        self.rgba = rgba
        self.matrix = matrix
        self.lo = lo
        self.hi = hi
        self.scale = scale
        self.margin = margin
        self.facets = facets

    @property
    def size(self):
        return self.rgba.shape[1], self.rgba.shape[0]

    def project(self, points):
        """World coordinates (n, 3) in mm -> pixel coordinates in this image."""
        view = np.atleast_2d(np.asarray(points, dtype=float)) @ self.matrix.T
        x = ((view[:, 0] - self.lo[0]) * self.scale + self.margin * SS) / SS
        y = ((self.hi[1] - view[:, 1]) * self.scale + self.margin * SS) / SS
        return np.stack([x, y], axis=1)


def render_view(meshes, yaw=-35.0, pitch=-62.0, width=880, margin=18):
    """Render groups and return a View.

    Each group is (triangles, normals, z_offset) and may carry a fourth entry,
    a (shadow, highlight) palette; without one it is drawn as shell plastic.
    """
    m = view_matrix(yaw, pitch)

    tri_list, norm_list, pal_list = [], [], []
    for group in meshes:
        triangles, normals, z_offset = group[0], group[1], group[2]
        shadow, highlight = group[3] if len(group) > 3 else SHELL
        tri_list.append((triangles + np.array([0.0, 0.0, z_offset])) @ m.T)
        norm_list.append(normals @ m.T)
        pal_list.append(
            np.tile(np.concatenate([shadow, highlight]), (len(normals), 1))
        )
    tris = np.vstack(tri_list)
    norms = np.vstack(norm_list)
    palettes = np.vstack(pal_list)

    # Back-face culling: in view space the camera looks along +Z toward us.
    facing = norms[:, 2] > 1e-9
    tris, norms, palettes = tris[facing], norms[facing], palettes[facing]

    lit = np.clip(norms @ LIGHT, 0.0, 1.0)[:, None]
    shadows, highlights = palettes[:, :3], palettes[:, 3:]
    colors = shadows + (highlights - shadows) * (0.16 + 0.84 * lit)

    pts = tris[:, :, :2]
    lo = pts.reshape(-1, 2).min(axis=0)
    hi = pts.reshape(-1, 2).max(axis=0)
    span = hi - lo
    big_w = width * SS
    scale = (big_w - 2 * margin * SS) / span[0]
    big_h = int(round(span[1] * scale + 2 * margin * SS))

    screen = np.empty_like(tris)
    screen[:, :, 0] = (tris[:, :, 0] - lo[0]) * scale + margin * SS
    screen[:, :, 1] = (hi[1] - tris[:, :, 1]) * scale + margin * SS  # PNG y grows down
    screen[:, :, 2] = tris[:, :, 2]

    color_buf, alpha = rasterise(screen, colors, big_w, big_h)

    # Box-filter down. Averaging colour and coverage together gives antialiased
    # edges that composite correctly over any background.
    h, w = big_h // SS, big_w // SS
    color_buf = color_buf[: h * SS, : w * SS].reshape(h, SS, w, SS, 3).mean(axis=(1, 3))
    cov = alpha[: h * SS, : w * SS].reshape(h, SS, w, SS).mean(axis=(1, 3))

    # Un-premultiply so the visible pixels keep their true colour at the edges.
    safe = np.maximum(cov, 1e-6)[:, :, None]
    rgb = np.clip(color_buf / safe, 0, 255)

    out = np.zeros((h, w, 4), dtype=np.uint8)
    out[:, :, :3] = rgb.astype(np.uint8)
    out[:, :, 3] = np.clip(cov * 255, 0, 255).astype(np.uint8)
    return View(out, m, lo, hi, scale, margin, facets=len(tris))


def render(meshes, out_path, **kwargs):
    """Render groups straight to a PNG. Returns (width, height, facet count)."""
    view = render_view(meshes, **kwargs)
    write_png(view.rgba, out_path)
    w, h = view.size
    return w, h, view.facets


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--stl", default=str(ROOT / "enclosure" / "stl"))
    ap.add_argument("--out", default=str(ROOT / "docs" / "img"))
    args = ap.parse_args()

    stl, out = Path(args.stl), Path(args.out)
    base_tris, base_norms = read_stl(stl / "base.stl")
    lid_tris, lid_norms = read_stl(stl / "lid.stl")

    jobs = [
        ("base.png", [(base_tris, base_norms, 0.0)], 620),
        ("lid.png", [(lid_tris, lid_norms, 0.0)], 620),
        # The lid has to clear the base by more than the assembly gap: at this
        # camera angle a smaller offset lets the near rim cross in front of it.
        (
            "exploded.png",
            [(base_tris, base_norms, 0.0), (lid_tris, lid_norms, 30.0)],
            900,
        ),
    ]
    for name, meshes, width in jobs:
        w, h, n = render(meshes, out / name, width=width)
        print(f"{name:<14} {w}x{h}  {n} facets")


if __name__ == "__main__":
    raise SystemExit(main())
