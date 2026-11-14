"""Fits the bought Studio-Lab snooker table to the game and exports it for Godot.

Run inside Blender on the bought file (see scripts/fit_table.py):

    blender -b licensed/snookertable.blend --python tools/fit_table.py -- --out game/licensed

The model is a short table (3.176 x 1.544 m between the cushion noses) with full-size pockets.
This script lengthens only the straight runs of cushion, rail and frame to the WPBSA playing
area, opens the corner mouths to the regulation gap, removes the painted markings (the game
draws its own on the regulation spots), shrinks the textures and writes glTF files plus
fit.json, the measured outline the physics table shape is built from.

Neither the bought file nor anything this writes may be committed (licensed/ and
game/licensed/ are gitignored).
"""

import argparse
import json
import math
import os
import sys
from pathlib import Path

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import licensed_imports  # noqa: E402

# The model as bought, in its own frame: metres, +y towards the black, z up.
NOSE_X = 0.7722
NOSE_Y = 1.5882
RAIL_END_Y = 1.5854  # the side rail and cushion end square to the pocket here
RAIL_END_X = 0.7694  # and the end rail and cushion here
CORNER_HOLE = (0.8198, 1.6354)

# The game's table (core/data/table_shape.json dimensions).
HALF_WIDTH = 0.889
HALF_LENGTH = 1.7845
BALL_RADIUS = 0.02625
MODEL_BALL_RADIUS = 0.026

# A 52.5 mm ball has 14 mm of play in the model's corner mouths against 31.5 mm on a
# tournament table (an 84 mm gap), and the owner's rule is that pots must not get harder. Each
# corner cushion and rail end is drawn back this far along its own length, which opens the
# mouth by sqrt(2) times as much, and the pocket's own pieces grow to stay butted against them.
# 16 mm (an 86 mm mouth) is where the pot widths measured by `ofs_sim_cli pots` stop falling
# short of the old outline's from 30 degrees off the diagonal; 13 mm matched it only head-on.
CORNER_PULL = 0.016
CORNER_PIECE = 0.13  # the corner pieces scale about a point this far beyond the two rail ends

# Stretches go where the table has no vertices, so only straight extrusions lengthen.
Y_SPANS = ((0.130, 0.425, 0.0650), (0.595, 0.725, 0.0267), (0.787, 1.297, None))
X_SPANS = ((0.472, 0.636, None),)

EMISSION_LIMIT = 4.0
CUE_LENGTH = 1.47


def stretch(values: np.ndarray, spans, total: float) -> np.ndarray:
    """Piecewise-linear lengthening of |coordinate|: each span grows by its amount."""
    given = sum(s[2] for s in spans if s[2] is not None)
    out = values.copy()
    for lo, hi, amount in spans:
        amount = total - given if amount is None else amount
        out += np.clip((values - lo) / (hi - lo), 0.0, 1.0) * amount
    return out


def islands(mesh: bpy.types.Mesh) -> np.ndarray:
    """Connected-part index of every vertex."""
    parent = np.arange(len(mesh.vertices))

    def find(i: int) -> int:
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for e in mesh.edges:
        a, b = find(e.vertices[0]), find(e.vertices[1])
        if a != b:
            parent[a] = b
    return np.array([find(i) for i in range(len(parent))])


def apply_world(o: bpy.types.Object) -> None:
    o.data.transform(o.matrix_world)
    o.matrix_world = Matrix.Identity(4)


def coords(mesh: bpy.types.Mesh) -> np.ndarray:
    co = np.empty(len(mesh.vertices) * 3)
    mesh.vertices.foreach_get("co", co)
    return co.reshape(-1, 3)


def fit(o: bpy.types.Object, is_table: bool, bed_z: float) -> tuple[dict, dict]:
    mesh = o.data
    co = coords(mesh)
    part = islands(mesh)
    sign = np.where(co[:, :2] < 0.0, -1.0, 1.0)
    a = np.abs(co[:, :2])
    out = np.column_stack((stretch(a[:, 0], X_SPANS, HALF_WIDTH - NOSE_X), stretch(a[:, 1], Y_SPANS, HALF_LENGTH - NOSE_Y)))
    shift = np.array([HALF_WIDTH - NOSE_X, HALF_LENGTH - NOSE_Y])
    centre = np.array([RAIL_END_X + CORNER_PIECE, RAIL_END_Y + CORNER_PIECE])
    scale = 1.0 + CORNER_PULL / CORNER_PIECE
    counts = {"corner_pieces": 0, "bed_hole": 0, "side_ends": 0, "end_ends": 0}
    masks = {"bed": np.zeros(len(co), bool), "side_cushion": np.zeros(len(co), bool), "end_cushion": np.zeros(len(co), bool)}
    if is_table:
        hole = np.array(CORNER_HOLE)
        for p in np.unique(part):
            m = part == p
            lo, hi = co[m].min(0), co[m].max(0)
            alo = a[m].min(0)
            one_side_x = lo[0] * hi[0] > 0.0
            one_side_y = lo[1] * hi[1] > 0.0
            if one_side_x and one_side_y and alo[0] >= 0.755 and alo[1] >= 1.570 and lo[2] >= 0.70:
                out[m] = centre + (a[m] - centre) * scale + shift
                counts["corner_pieces"] += 1
            elif hi[0] - lo[0] > 1.6 and hi[1] - lo[1] > 3.2 and 0.80 < lo[2] and hi[2] < 0.83:
                near = m & (np.hypot(a[:, 0] - hole[0], a[:, 1] - hole[1]) < 0.075)
                out[near] = centre + (a[near] - centre) * scale + shift
                counts["bed_hole"] += int(near.sum())
                masks["bed"] = m
            elif one_side_x and hi[1] - lo[1] > 1.0 and alo[0] >= 0.70 and lo[2] >= 0.75:
                ends = m & (a[:, 1] > 1.45)
                out[ends, 1] -= CORNER_PULL
                counts["side_ends"] += 1
                if abs(lo[2] - bed_z) < 1e-3 and lo[0] > 0.0 and lo[1] > 0.0:
                    masks["side_cushion"] = m
            elif one_side_y and hi[0] - lo[0] > 1.0 and alo[1] >= 1.54 and lo[2] >= 0.75:
                ends = m & (a[:, 0] > 0.65)
                out[ends, 0] -= CORNER_PULL
                counts["end_ends"] += 1
                if abs(lo[2] - bed_z) < 1e-3 and lo[1] > 0.0:
                    masks["end_cushion"] = m
    co[:, :2] = out * sign
    mesh.vertices.foreach_set("co", co.ravel())
    mesh.update()
    return counts, masks


def bvh(o: bpy.types.Object) -> tuple[BVHTree, bmesh.types.BMesh]:
    bm = bmesh.new()
    bm.from_mesh(o.data)
    return BVHTree.FromBMesh(bm), bm


def fit_circle(points: np.ndarray) -> tuple[float, float, float, float]:
    lhs = np.c_[2.0 * points, np.ones(len(points))]
    c, *_ = np.linalg.lstsq(lhs, (points ** 2).sum(1), rcond=None)
    r = math.sqrt(c[2] + c[0] ** 2 + c[1] ** 2)
    worst = float(np.abs(np.hypot(points[:, 0] - c[0], points[:, 1] - c[1]) - r).max())
    return float(c[0]), float(c[1]), r, worst


def measure(table: bpy.types.Object, bed_z: float, masks: dict) -> dict:
    """The fitted outline in the model frame, for the physics table shape."""
    tree, bm = bvh(table)
    co = coords(table.data)

    def hit(origin, direction):
        loc, _n, _i, _d = tree.ray_cast(Vector(origin), Vector(direction))
        return loc

    profile = []
    for i in range(0, 50):
        loc = hit((0.0, 0.8, bed_z + i * 0.001 + 1e-5), (1, 0, 0))
        if loc is not None and loc.x < HALF_WIDTH + 0.05:
            profile.append((i * 0.001, loc.x))
    nose_dz, nose_x = min(profile, key=lambda p: p[1])
    nose_y = hit((0.0, 0.0, bed_z + nose_dz), (0, 1, 0)).y
    res = {
        "bed_z": bed_z, "nose_height": nose_dz, "cushion_top": max(p[0] for p in profile),
        "half_width": nose_x, "half_length": nose_y,
        "rail_top": hit((HALF_WIDTH + 0.08, 0.8, 2.0), (0, 0, -1)).z - bed_z,
        "rail_outer_x": hit((2.0, 0.8, bed_z + 0.03), (-1, 0, 0)).x,
        "rail_outer_y": hit((0.0, 3.0, bed_z + 0.03), (0, -1, 0)).y,
    }
    bm.free()

    def jaw(s: np.ndarray, across: np.ndarray) -> dict:
        """A jaw as an arc tangent to the nose line: s runs along the cushion towards the pocket,
        across is the distance behind the nose line."""
        tip_s = s.max()
        tip_across = across[np.abs(s - tip_s) < 2e-4].min()
        on = (across > 2e-4) & (across <= tip_across + 1e-5)
        pts = np.unique(np.round(np.column_stack((s[on], across[on])), 6), axis=0)
        radius, start = 0.1, pts[:, 0].min() - 0.02
        for _ in range(100):
            d = np.hypot(pts[:, 0] - start, pts[:, 1] - radius)
            jac = np.column_stack((-(pts[:, 0] - start) / d, -(pts[:, 1] - radius) / d - 1.0))
            step, *_ = np.linalg.lstsq(jac, radius - d, rcond=None)
            start, radius = start + step[0], radius + step[1]
        worst = float(np.abs(np.hypot(pts[:, 0] - start, pts[:, 1] - radius) - radius).max())
        return {"radius": float(radius), "start": float(start), "tip": float(tip_s), "tip_behind_nose": float(tip_across),
                "sweep_rad": math.atan2(tip_s - start, radius - tip_across), "fit_error": worst, "points": len(pts)}

    at_nose = np.abs(co[:, 2] - (bed_z + nose_dz)) < 3e-4
    side = co[masks["side_cushion"] & at_nose]
    end = co[masks["end_cushion"] & at_nose & (co[:, 0] > 0.0)]
    far, near = side[side[:, 1] > 1.0], side[side[:, 1] < 0.3]
    res["corner_side_jaw"] = jaw(far[:, 1], far[:, 0] - nose_x)
    res["corner_end_jaw"] = jaw(end[:, 0], end[:, 1] - nose_y)
    res["middle_jaw"] = jaw(-near[:, 1], near[:, 0] - nose_x)

    def fall(cx: float, cy: float) -> dict:
        bed = co[masks["bed"]]
        ring = bed[(np.abs(bed[:, 2] - bed_z) < 2e-4) & (np.hypot(bed[:, 0] - cx, bed[:, 1] - cy) < 0.08)][:, :2]
        x, y, r, worst = fit_circle(ring)
        return {"centre": [x, y], "radius": r, "fit_error": worst, "points": len(ring)}

    grow = 1.0 + CORNER_PULL / CORNER_PIECE
    c = (RAIL_END_X + CORNER_PIECE, RAIL_END_Y + CORNER_PIECE)
    res["corner_fall"] = fall(c[0] + (CORNER_HOLE[0] - c[0]) * grow + HALF_WIDTH - NOSE_X, c[1] + (CORNER_HOLE[1] - c[1]) * grow + HALF_LENGTH - NOSE_Y)
    res["middle_fall"] = fall(0.8203 + HALF_WIDTH - NOSE_X, 0.0)
    nets = [i for i, m in enumerate(table.data.materials) if m.name == "SnookerNetShader"]
    net_verts = {v for p in table.data.polygons if p.material_index in nets for v in p.vertices}
    res["net_floor"] = float(co[list(net_verts)][:, 2].min() - bed_z)
    return res


def clear_markings(table: bpy.types.Object, bed_z: float) -> dict:
    """Paints the baulk line, D and spots out of the bed's part of every table texture."""
    mesh = table.data
    uv = mesh.uv_layers.active.data
    lo, hi = np.array([1.0, 1.0]), np.array([0.0, 0.0])
    for poly in mesh.polygons:
        if poly.normal.z > 0.99 and abs(poly.center.z - bed_z) < 5e-4 and poly.area > 0.05:
            for li in poly.loop_indices:
                p = np.array(uv[li].uv)
                lo, hi = np.minimum(lo, p), np.maximum(hi, p)
    mat = next(m for m in mesh.materials if m.name == "SnookerTableShader")
    images = {n.image for n in mat.node_tree.nodes if n.bl_idname == "ShaderNodeTexImage" and n.image}
    base = next(n.image for n in mat.node_tree.nodes if n.bl_idname == "ShaderNodeTexImage" and n.image
                and any(l.to_socket.name == "Base Color" for o in n.outputs for l in o.links))
    w, h = base.size
    x0, x1 = int(lo[0] * w) + 4, int(hi[0] * w) - 4
    y0, y1 = int(lo[1] * h) + 4, int(hi[1] * h) - 4
    px = np.empty(w * h * 4, dtype=np.float32)
    base.pixels.foreach_get(px)
    px = px.reshape(h, w, 4)
    region = px[y0:y1, x0:x1, :3]
    cloth = np.median(region.reshape(-1, 3), axis=0)
    mask = np.abs(region - cloth).max(axis=2) > 0.06
    grown = mask.copy()
    for dy in range(-3, 4):
        for dx in range(-3, 4):
            grown |= np.roll(np.roll(mask, dy, 0), dx, 1)
    marked = int(grown.sum())
    for image in images:
        iw, ih = image.size
        if (iw, ih) != (w, h):
            continue
        buf = np.empty(iw * ih * 4, dtype=np.float32)
        image.pixels.foreach_get(buf)
        buf = buf.reshape(ih, iw, 4)
        area = buf[y0:y1, x0:x1]
        left = grown.copy()
        # Borrow clean cloth from further and further along the nap.
        for step in (40, -40, 90, -90, 160, -160, 300, -300):
            for axis in (1, 0):
                src = np.roll(area, step, axis)
                src_ok = ~np.roll(grown, step, axis)
                take = left & src_ok
                area[take] = src[take]
                left &= ~take
        area[left, :3] = np.median(area[~grown][:, :3], axis=0)
        image.pixels.foreach_set(buf.ravel())
        image.update()
    return {"uv_min": lo.tolist(), "uv_max": hi.tolist(), "pixels_painted": marked}


def name_images() -> None:
    """Image names become file names on export; the model's include a colon and bare 'file3'."""
    for image in bpy.data.images:
        stem = image.filepath.replace("\\", "/").rsplit("/", 1)[-1].rsplit(".", 1)[0]
        if stem:
            image.name = "".join(ch if ch.isalnum() or ch in "_-" else "_" for ch in stem)


def shrink_textures(objects, limits: dict[str, int]) -> None:
    done = set()
    for o in objects:
        for mat in o.data.materials:
            limit = limits.get(mat.name, 1024)
            for n in mat.node_tree.nodes:
                if n.bl_idname == "ShaderNodeTexImage" and n.image and n.image.name not in done:
                    done.add(n.image.name)
                    if max(n.image.size) > limit:
                        n.image.scale(limit, limit)


def opaque_shades(mat: bpy.types.Material) -> None:
    """The lamp shades use a glass shader the glTF exporter drops; give them a plain one."""
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    glass = next(n for n in nodes if n.bl_idname == "ShaderNodeBsdfGlass")
    out = next(n for n in nodes if n.bl_idname == "ShaderNodeOutputMaterial")
    bsdf = nodes.new("ShaderNodeBsdfPrincipled")
    colour = next((n for n in nodes if n.bl_idname == "ShaderNodeTexImage" and n.image and n.image.name.endswith("_D")), None)
    if colour is not None:
        links.new(colour.outputs["Color"], bsdf.inputs["Base Color"])
    for name in ("Roughness", "Normal"):
        if glass.inputs[name].is_linked:
            links.new(glass.inputs[name].links[0].from_socket, bsdf.inputs[name])
    links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    nodes.remove(glass)


def tame_emission(mat: bpy.types.Material) -> None:
    for n in mat.node_tree.nodes:
        if n.bl_idname == "ShaderNodeBsdfPrincipled":
            strength = n.inputs["Emission Strength"]
            emits = n.inputs["Emission Color"].is_linked
            strength.default_value = min(strength.default_value, EMISSION_LIMIT) if emits else 0.0


def recentre(o: bpy.types.Object, origin: Vector, scale: float = 1.0, turn: Matrix | None = None) -> None:
    m = Matrix.Scale(scale, 4) @ (turn or Matrix.Identity(4)) @ Matrix.Translation(-origin)
    o.data.transform(m)
    o.matrix_world = Matrix.Identity(4)


def strip_colour_profiles(folder: str) -> int:
    """Godot 4.7's JPEG loader refuses a file with an embedded ICC profile, and Blender writes
    one (plain sRGB) into every colour texture. Dropping that segment changes no pixel."""
    stripped = 0
    for name in sorted(os.listdir(folder)):
        if not name.lower().endswith((".jpg", ".jpeg")):
            continue
        path = os.path.join(folder, name)
        with open(path, "rb") as f:
            data = f.read()
        out, i = bytearray(data[:2]), 2
        while i + 4 <= len(data) and data[i] == 0xFF and data[i + 1] != 0xDA:
            size = 2 + int.from_bytes(data[i + 2:i + 4], "big")
            if not (data[i + 1] == 0xE2 and data[i + 4:i + 15] == b"ICC_PROFILE"):
                out += data[i:i + size]
            i += size
        out += data[i:]
        if len(out) != len(data):
            with open(path, "wb") as f:
                f.write(out)
            stripped += 1
    return stripped


def export(path: str, objects) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.hide_set(False)
        o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLTF_SEPARATE", use_selection=True, export_apply=True,
                              export_yup=True, export_image_format="JPEG", export_image_quality=90,
                              export_cameras=False, export_lights=False, export_animations=False)


def main() -> None:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=os.path.join(root, "game", "licensed"))
    parser.add_argument("--table-texture", type=int, default=2048)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    os.makedirs(args.out, exist_ok=True)

    D = bpy.data.objects
    table, racks, lamp, chalk = D["SnookerTable"], D["SnookerProps"], D["PoolTableLights"], D["Chalk"]
    meshes = [o for o in D if o.type == "MESH"]
    for o in meshes:
        apply_world(o)

    tree, bm = bvh(table)
    bed_z = tree.ray_cast(Vector((0, 0, 2)), Vector((0, 0, -1)))[0].z
    nose = min(tree.ray_cast(Vector((0, 0.8, bed_z + i * 0.001)), Vector((1, 0, 0)))[0].x for i in range(25, 40))
    bm.free()
    if abs(nose - NOSE_X) > 5e-4:
        sys.exit(f"fit_table: the cushion nose is at x = {nose:.4f}, expected {NOSE_X}: not the model this script was written for")

    report = {"source": os.path.basename(bpy.data.filepath), "corner_pull": CORNER_PULL}
    report["markings"] = clear_markings(table, bed_z)
    report["table_parts"], masks = fit(table, True, bed_z)
    fit(racks, False, bed_z)
    report["outline"] = measure(table, bed_z, masks)

    # The game's frame: the cloth at height 0.
    drop = Matrix.Translation((0, 0, -bed_z))
    side_shift = Vector(((HALF_WIDTH - NOSE_X) * (1 if chalk.data.vertices[0].co.x > 0 else -1), 0, 0))
    chalk.data.transform(Matrix.Translation(side_shift))
    for o in (table, racks, lamp, chalk):
        o.data.transform(drop)
    table.name, racks.name, lamp.name, chalk.name = "Table", "Racks", "Lamp", "Chalk"

    name_images()
    shrink_textures([table], {"SnookerTableShader": args.table_texture, "SnookerNetShader": 1024})
    shrink_textures([racks, lamp, chalk, D["SnookerCue"], D["Rest"], D["Cue"], D["TriangleWood"], D["CueBall"]],
                    {"BallsShader": 512, "SnookerPropsShader": 1024, "snookerTableProps": 1024,
                     "PoolTableLightsShader": 1024, "PoolTableLightsGlassShader": 1024})
    for mat in bpy.data.materials:
        if mat.node_tree is None:
            continue
        if any(n.bl_idname == "ShaderNodeBsdfGlass" for n in mat.node_tree.nodes):
            opaque_shades(mat)
        tame_emission(mat)

    balls = {"CueBall": "BallCue", "SnookerBallRed01": "BallRed", "SnookerBallYellow": "BallYellow", "SnookerBallGreen": "BallGreen",
             "SnookerBallBrown": "BallBrown", "SnookerBallBlue": "BallBlue", "SnookerBallPink": "BallPink", "SnookerBallBlack": "BallBlack"}
    ball_objects = []
    for source, name in balls.items():
        o = D[source]
        co = coords(o.data)
        recentre(o, Vector(0.5 * (co.min(0) + co.max(0))), BALL_RADIUS / MODEL_BALL_RADIUS)
        o.name = name
        ball_objects.append(o)

    extras = []
    for source, name in (("Rest", "Rest"), ("Cue", "SpareCue"), ("TriangleWood", "Triangle")):
        o = D[source]
        co = coords(o.data)
        recentre(o, Vector((0.5 * (co[:, 0].min() + co[:, 0].max()), 0.5 * (co[:, 1].min() + co[:, 1].max()), co[:, 2].min())))
        o.name = name
        extras.append(o)

    # The cue lies along y; put its tip at the origin with the butt towards +y (Godot's -z).
    cue = D["SnookerCue"]
    co = coords(cue.data)
    y_lo, y_hi = co[:, 1].min(), co[:, 1].max()
    axis = Vector((0.5 * (co[:, 0].min() + co[:, 0].max()), 0.0, 0.5 * (co[:, 2].min() + co[:, 2].max())))

    def girth(y: float) -> float:
        near = np.abs(co[:, 1] - y) < 0.02
        return float(np.hypot(co[near, 0] - axis.x, co[near, 2] - axis.z).max())

    tip_at_low = girth(y_lo) < girth(y_hi)
    tip_y = y_lo if tip_at_low else y_hi
    turn = Matrix.Identity(4) if tip_at_low else Matrix.Rotation(math.pi, 4, "Z")
    recentre(cue, Vector((axis.x, tip_y, axis.z)), CUE_LENGTH / (y_hi - y_lo), turn)
    cue.name = "Cue"
    co = coords(cue.data)
    report["cue"] = {"length": float(co[:, 1].max() - co[:, 1].min()), "tip_radius": float(np.hypot(co[co[:, 1] < 0.02][:, 0], co[co[:, 1] < 0.02][:, 2]).max()),
                     "butt_radius": float(np.hypot(co[:, 0], co[:, 2]).max())}

    export(os.path.join(args.out, "table.gltf"), [table, racks, chalk])
    export(os.path.join(args.out, "lamp.gltf"), [lamp])
    export(os.path.join(args.out, "balls.gltf"), ball_objects)
    export(os.path.join(args.out, "cue.gltf"), [cue])
    export(os.path.join(args.out, "extras.gltf"), extras)
    report["colour_profiles_stripped"] = strip_colour_profiles(args.out)
    report["import_settings_written"] = licensed_imports.write(Path(args.out))

    lamp_co = coords(lamp.data)
    report["lamp"] = {"min": lamp_co.min(0).tolist(), "max": lamp_co.max(0).tolist()}
    report["triangles"] = {o.name: sum(len(p.vertices) - 2 for p in o.data.polygons) for o in [table, racks, chalk, lamp, cue] + ball_objects + extras}
    with open(os.path.join(args.out, "fit.json"), "w") as f:
        json.dump(report, f, indent=1)
    print("fit_table: wrote", args.out)
    print(json.dumps(report, indent=1))


main()
