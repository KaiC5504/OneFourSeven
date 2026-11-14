#!/usr/bin/env python3
"""Rebuilds the outline in core/data/table_shape.json from its own "dimensions".

    python tools/make_table_shape.py            # rewrite pockets, lines and arcs
    python tools/make_table_shape.py --check    # fail when the file doesn't match its dimensions

Change a dimension (with its source in "docs"), then run this; nothing else in the file is
written by hand. The construction is the one the file's "construction" text describes.
"""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path

SHAPE = Path(__file__).resolve().parents[1] / "core" / "data" / "table_shape.json"

CONSTRUCTION = (
    "Cushion noses ('kind': 'nose') run along the playing area's edges. At each pocket each nose ends in a jaw: "
    "a circular arc of jaw_radius_m, tangent to the nose, that turns away from the table through jaw_sweep_rad to "
    "the cushion's tip. A corner jaw starts corner_jaw_start_m short of the corner point (where the two nose lines "
    "cross); a middle jaw starts middle_jaw_start_m from the pocket's centre line. From each tip a straight facing "
    "('kind': 'facing') runs on along the jaw's tangent until it is level with the far edge of the drop circle, "
    "measured along the pocket's axis (the diagonal through the corner point; square to the cushion for a middle "
    "pocket). Each drop circle is the hole in the bed: its centre sits on the pocket's axis, corner_drop_centre_m "
    "beyond the corner point or middle_drop_centre_m behind the nose line, with the matching drop radius."
)

CORNERS = (("top_left", 1, 1), ("top_right", 1, -1), ("bottom_left", -1, 1), ("bottom_right", -1, -1))
POCKET_ORDER = ("top_left", "top_right", "middle_left", "middle_right", "bottom_left", "bottom_right")


def add(a, b, k=1.0):
    return (a[0] + k * b[0], a[1] + k * b[1])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1]


def jaw(name: str, pocket: str, nose_end, normal, towards, radius: float, sweep: float, level):
    """One jaw's arc and facing. `normal` points into the playing area, `towards` along the nose
    to the pocket; level(point) is how far a point lies along the pocket's axis, short of the
    far edge of its drop circle."""
    centre = add(nose_end, normal, -radius)
    tip_radial = add((normal[0] * math.cos(sweep), normal[1] * math.cos(sweep)), towards, math.sin(sweep))
    tip = add(centre, tip_radial, radius)
    run = add((towards[0] * math.cos(sweep), towards[1] * math.cos(sweep)), normal, -math.sin(sweep))
    start = math.atan2(normal[1], normal[0])
    anticlockwise = normal[0] * towards[1] - normal[1] * towards[0] > 0.0
    if not anticlockwise:
        start -= sweep
    start = (start + math.pi) % (2.0 * math.pi) - math.pi
    probe = 1e-3
    length = level(tip) / ((level(tip) - level(add(tip, run, probe))) / probe)
    arc = {"name": f"{pocket}_jaw_{name}", "pocket": pocket, "centre": list(centre), "radius": radius,
           "angle_start": start, "angle_end": start + sweep}
    facing = {"name": f"{pocket}_facing_{name}", "kind": "facing", "p1": list(tip), "p2": list(add(tip, run, length)),
              "normal": list(tip_radial)}
    return arc, facing


def build(dims: dict) -> dict:
    half_l, half_w = 0.5 * dims["playing_length_m"], 0.5 * dims["playing_width_m"]
    radius, sweep = dims["jaw_radius_m"], dims["jaw_sweep_rad"]
    corner_start, middle_start = dims["corner_jaw_start_m"], dims["middle_jaw_start_m"]
    run_x, run_y = half_l - corner_start, half_w - corner_start

    def nose(name, p1, p2, normal):
        return {"name": name, "kind": "nose", "p1": list(p1), "p2": list(p2), "normal": list(normal)}

    lines = [
        nose("top", (half_l, -run_y), (half_l, run_y), (-1.0, 0.0)),
        nose("baulk", (-half_l, run_y), (-half_l, -run_y), (1.0, 0.0)),
        nose("left_top", (middle_start, half_w), (run_x, half_w), (0.0, -1.0)),
        nose("left_baulk", (-run_x, half_w), (-middle_start, half_w), (0.0, -1.0)),
        nose("right_top", (run_x, -half_w), (middle_start, -half_w), (0.0, 1.0)),
        nose("right_baulk", (-middle_start, -half_w), (-run_x, -half_w), (0.0, 1.0)),
    ]
    pockets, arcs, facings = {}, {}, {}
    for pocket, sx, sy in CORNERS:
        corner = (sx * half_l, sy * half_w)
        axis = (sx * math.sqrt(0.5), sy * math.sqrt(0.5))
        far = dims["corner_drop_centre_m"] + dims["corner_drop_radius_m"]
        level = lambda p, corner=corner, axis=axis, far=far: far - dot(add(p, corner, -1.0), axis)
        side = jaw("left" if sy > 0 else "right", pocket, (sx * run_x, sy * half_w), (0.0, -float(sy)), (float(sx), 0.0),
                   radius, sweep, level)
        end = jaw("top" if sx > 0 else "baulk", pocket, (sx * half_l, sy * run_y), (-float(sx), 0.0), (0.0, float(sy)),
                  radius, sweep, level)
        arcs[pocket], facings[pocket] = [side[0], end[0]], [side[1], end[1]]
        pockets[pocket] = {"name": pocket, "corner": True, "drop_centre": list(add(corner, axis, dims["corner_drop_centre_m"])),
                           "drop_radius": dims["corner_drop_radius_m"]}
    for pocket, sy in (("middle_left", 1), ("middle_right", -1)):
        far = dims["middle_drop_centre_m"] + dims["middle_drop_radius_m"]
        level = lambda p, sy=sy, far=far: far - (sy * p[1] - half_w)
        jaws = [jaw(name, pocket, (sx * middle_start, sy * half_w), (0.0, -float(sy)), (-float(sx), 0.0), radius, sweep, level)
                for name, sx in (("top", 1), ("baulk", -1))]
        arcs[pocket], facings[pocket] = [j[0] for j in jaws], [j[1] for j in jaws]
        pockets[pocket] = {"name": pocket, "corner": False, "drop_centre": [0.0, sy * (half_w + dims["middle_drop_centre_m"])],
                           "drop_radius": dims["middle_drop_radius_m"]}
    return {
        "construction": CONSTRUCTION,
        "rail_height_m": dims["rail_height_m"],
        "rail_width_m": dims["rail_width_m"],
        "pockets": [pockets[p] for p in POCKET_ORDER],
        "lines": lines + [f for p in POCKET_ORDER for f in facings[p]],
        "arcs": [a for p in POCKET_ORDER for a in arcs[p]],
    }


def main(argv: list[str]) -> int:
    shape = json.loads(SHAPE.read_text(encoding="utf-8"))
    built = build(shape["dimensions"])
    if argv == ["--check"]:
        stale = [key for key, value in built.items() if shape.get(key) != value]
        if stale:
            print(f"{SHAPE.name}: {', '.join(stale)} do not match the dimensions; run tools/make_table_shape.py", file=sys.stderr)
            return 1
        return 0
    if argv:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    shape.update(built)
    SHAPE.write_text(json.dumps(shape, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {SHAPE}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
