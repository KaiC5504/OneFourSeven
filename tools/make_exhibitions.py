"""Finds the tables "Watch a 147" starts from (spec 10.1, docs/opponent/README.md, "The 147") and
proves each with the simulator: a table is kept only when the 147 makes 147 on it.

    python tools/make_exhibitions.py                      # writes core/data/exhibitions.json
    python tools/make_exhibitions.py --check              # each committed table must still make 147
    python tools/make_exhibitions.py --replace-failing    # keeps the ones that do, replaces the rest
    python tools/make_exhibitions.py --check --picture build/exhibitions

Candidates come from --seed, the same ones every time: fifteen reds opened from the pack, most of
them loosely about the pink and the black, a few out towards the middle; the colours on their
spots; the cue ball on an easy first red that leaves the black. They are played in batches by
`ofs_sim_cli tournament --a maximum --frames 0 --exhibitions` (--cli PATH, else OFS_SIM_CLI, else
the Release build in build/windows-physics) and the first --count (default 50) that make 147 are
written as "Open table 1" onwards. Every written file is played once more as written, since a
table's place in the file seeds its frame. --check prints the tables that fail and a count, a
search a count per batch; --verbose prints every table played. --picture DIR draws each table of the file, top down, as
DIR/<n>.png (needs Pillow). Exit 0 when every table makes 147, 1 when one does not, 2 when the tool
cannot run.
"""
import argparse
import collections
import json
import math
import os
import pathlib
import random
import re
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXHIBITIONS = ROOT / "core" / "data" / "exhibitions.json"
GEOMETRY = ROOT / "core" / "include" / "ofs" / "table_geometry.hpp"
SHAPE = ROOT / "core" / "data" / "table_shape.json"
DEFAULT_CLI = ROOT / "build" / "windows-physics" / "tools" / "sim_cli" / "Release" / "ofs_sim_cli.exe"

NOTE = ("Tables laid out for the 147 to play a maximum from (spec 10.1). Each ball is in the rules' form. "
        "The layouts are found by tools/make_exhibitions.py: ones the 147 completes a maximum from.")
COLOURS = ("yellow", "green", "brown", "blue", "pink", "black")
COLOUR_ORDER = (("yellow", 120), ("green", 122), ("brown", 125), ("blue", 129), ("pink", 134), ("black", 140))

# No red this near a cushion nose: the tables should look like an opened pack, and none should
# hang on the hardest pot there is, one along a cushion.
CUSHION_CLEAR_M = 0.15
GAP_M = 0.015
SPOT_CLEAR_RADII = 2.5
CORNER_RUN_CLEAR_M = 0.01


class ToolError(Exception):
    pass


def constant(text, name):
    match = re.search(rf"\b{name}\s*=\s*([0-9.]+)\s*;", text)
    if not match:
        raise ToolError(f"{GEOMETRY.name}: no {name}")
    return float(match.group(1))


class Table:
    """The measures the layouts need, read from the core's own files."""

    def __init__(self, geometry=GEOMETRY, shape=SHAPE):
        text = pathlib.Path(geometry).read_text(encoding="utf-8")
        self.length = constant(text, "kPlayingLength")
        self.width = constant(text, "kPlayingWidth")
        self.radius = constant(text, "kBallDiameter") / 2.0
        self.d_radius = constant(text, "kDRadius")
        top = self.length / 2.0
        baulk_line = -top + constant(text, "kBaulkLineFromBaulkCushion")
        self.baulk_line = baulk_line
        self.spots = {
            "yellow": (baulk_line, -self.d_radius),
            "green": (baulk_line, self.d_radius),
            "brown": (baulk_line, 0.0),
            "blue": (0.0, 0.0),
            "pink": (top / 2.0, 0.0),
            "black": (top - constant(text, "kBlackSpotFromTopCushion"), 0.0),
        }
        data = json.loads(pathlib.Path(shape).read_text(encoding="utf-8"))
        self.rail = data["rail_width_m"]
        self.pockets = [{"name": p["name"], "corner": p["corner"], "centre": tuple(p["drop_centre"]),
                         "radius": p["drop_radius"]} for p in data["pockets"]]
        self.top_corners = [p["centre"] for p in self.pockets if p["corner"] and p["centre"][0] > 0.0]

    @property
    def spacing(self):
        return 2.0 * self.radius + GAP_M

    def clear_of_cushions(self, p, clear=CUSHION_CLEAR_M):
        return abs(p[0]) <= self.length / 2.0 - clear and abs(p[1]) <= self.width / 2.0 - clear


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def to_segment(p, a, b):
    """Distance from p to the segment ab, and how far along it (0 to 1) the nearest point lies."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    length2 = dx * dx + dy * dy
    t = 0.0 if length2 == 0.0 else max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / length2))
    return math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy), t


def unit(a, b):
    d = dist(a, b)
    return ((b[0] - a[0]) / d, (b[1] - a[1]) / d)


def run_clear(a, b, others, table, extra=0.0):
    """A ball rolls from a to b without touching any of `others` (a ball's width each side)."""
    return all(to_segment(o, a, b)[0] >= 2.0 * table.radius + extra for o in others)


def cut_angle(cue, target, pocket, table):
    """The cut on `target` towards `pocket` from `cue`, and the ghost ball's place."""
    u = unit(target, pocket)
    ghost = (target[0] - 2.0 * table.radius * u[0], target[1] - 2.0 * table.radius * u[1])
    v = unit(cue, ghost)
    return math.acos(max(-1.0, min(1.0, v[0] * u[0] + v[1] * u[1]))), ghost


def red_problem(p, reds, table):
    """Why a red may not go at p; None when it may."""
    if not table.clear_of_cushions(p):
        return "cushion"
    if any(dist(p, q) < table.spacing for q in reds):
        return "spacing"
    for name, spot in table.spots.items():
        if dist(p, spot) < max(table.spacing, SPOT_CLEAR_RADII * table.radius):
            return f"{name} spot"
    black = table.spots["black"]
    for corner in table.top_corners:
        if to_segment(p, black, corner)[0] < 2.0 * table.radius + CORNER_RUN_CLEAR_M:
            return "black's run"
    return None


def scatter_reds(rng, table):
    """Fifteen reds as an opened pack lies: clumps about the pink and the black, a few strays."""
    pink, black = table.spots["pink"], table.spots["black"]
    heavy = rng.choice((-1.0, 1.0)) * rng.uniform(0.0, 0.18)
    spread = rng.uniform(0.75, 1.25)
    clumps = []
    for _ in range(rng.randint(3, 5)):
        x = rng.uniform(pink[0] - 0.25, black[0] + 0.12)
        y = rng.gauss(heavy, 0.28 * spread)
        clumps.append((x, max(-0.6, min(0.6, y))))
    strays = rng.randint(1, 4)
    reds = []
    tries = 0
    while len(reds) < 15:
        tries += 1
        if tries > 5000:
            return None
        if len(reds) < strays:
            p = (rng.uniform(-0.45, pink[0] - 0.3), rng.gauss(heavy, 0.3))
        else:
            cx, cy = rng.choice(clumps)
            p = (rng.gauss(cx, 0.13 * spread), rng.gauss(cy, 0.13 * spread))
        p = (round(p[0], 4), round(p[1], 4))
        if red_problem(p, reds, table) is None:
            reds.append(p)
    return reds


def place_cue(rng, reds, table):
    """The cue ball on an easy red, and the black open from where it stops; None when there is none."""
    colours = [table.spots[c] for c in COLOURS]
    order = list(range(len(reds)))
    rng.shuffle(order)
    for i in order:
        red = reds[i]
        others = [r for j, r in enumerate(reds) if j != i] + colours
        pockets = list(table.pockets)
        rng.shuffle(pockets)
        for pocket in pockets:
            target = pocket["centre"]
            if not run_clear(red, target, others, table):
                continue
            u = unit(target, red)
            for _ in range(6):
                cut = math.radians(rng.uniform(-22.0, 22.0))
                d = rng.uniform(0.22, 0.65)
                c, s = math.cos(cut), math.sin(cut)
                back = (u[0] * c - u[1] * s, u[0] * s + u[1] * c)
                ghost = (red[0] + 2.0 * table.radius * u[0], red[1] + 2.0 * table.radius * u[1])
                cue = (round(ghost[0] + d * back[0], 4), round(ghost[1] + d * back[1], 4))
                if not table.clear_of_cushions(cue):
                    continue
                if any(dist(cue, b) < table.spacing for b in reds + colours):
                    continue
                if not run_clear(cue, ghost, others, table):
                    continue
                if black_on_from(ghost, [r for j, r in enumerate(reds) if j != i], table):
                    return cue
    return None


def black_on_from(cue, reds, table):
    """A plain pot of the black into a top corner from `cue`."""
    black = table.spots["black"]
    in_way = reds + [table.spots[c] for c in COLOURS if c != "black"]
    for corner in table.top_corners:
        cut, ghost = cut_angle(cue, black, corner, table)
        if cut < math.radians(40.0) and dist(cue, ghost) > 0.12 and run_clear(cue, ghost, in_way, table):
            return True
    return False


def candidate(seed, number, table):
    """Candidate `number` of `seed`: a layout dict, or None when the draw could not be laid."""
    rng = random.Random(f"exhibition {seed} {number}")
    for _ in range(20):
        reds = scatter_reds(rng, table)
        if reds is None:
            continue
        cue = place_cue(rng, reds, table)
        if cue is not None:
            return layout(f"candidate {seed}-{number}", cue, reds, table)
    return None


def layout(name, cue, reds, table):
    balls = [{"ball": "cue", "index": 0, "x": cue[0], "y": cue[1]}]
    balls += [{"ball": "red", "index": i, "x": p[0], "y": p[1]} for i, p in enumerate(reds)]
    balls += [{"ball": c, "index": 0, "x": table.spots[c][0], "y": table.spots[c][1]} for c in COLOURS]
    return {"name": name, "cue_ball": 0, "balls": balls}


def problems(lay, table):
    """Every way the layout breaks the rules the tables are laid by."""
    out = []
    balls = lay["balls"]
    reds = [(b["x"], b["y"]) for b in balls if b["ball"] == "red"]
    if len(reds) != 15:
        out.append(f"{len(reds)} reds")
    for c in COLOURS:
        placed = [(b["x"], b["y"]) for b in balls if b["ball"] == c]
        if len(placed) != 1 or dist(placed[0], table.spots[c]) > 1e-9:
            out.append(f"the {c} is not on its spot")
    for i, a in enumerate(balls):
        for b in balls[i + 1:]:
            if dist((a["x"], a["y"]), (b["x"], b["y"])) < table.spacing - 1e-9:
                out.append(f"{a['ball']} {a['index']} and {b['ball']} {b['index']} are too close")
    for i, p in enumerate(reds):
        why = red_problem(p, [], table)
        if why:
            out.append(f"red {i}: {why}")
    cue = [(b["x"], b["y"]) for b in balls if b["ball"] == "cue"]
    if len(cue) != 1 or not table.clear_of_cushions(cue[0]):
        out.append("the cue ball is missing or on a cushion")
    return out


def exhibitions_file(layouts):
    return {"schema_version": 1, "_note": NOTE, "layouts": layouts}


def write_file(path, layouts):
    pathlib.Path(path).write_text(json.dumps(exhibitions_file(layouts), indent=2) + "\n", encoding="utf-8",
                                  newline="\n")


def renamed(layouts):
    return [dict(lay, name=f"Open table {i + 1}") for i, lay in enumerate(layouts)]


def cli_path(given):
    path = pathlib.Path(given or os.environ.get("OFS_SIM_CLI") or DEFAULT_CLI)
    if not path.is_file():
        raise ToolError(f"no ofs_sim_cli at {path}; build it (tools/sim_cli/README.md) or pass --cli")
    return path


def run_tournament(cli, file, threads, folder):
    """Plays every table of `file`; the report and the log's lines."""
    report = pathlib.Path(folder) / "report.json"
    log = pathlib.Path(folder) / "tournament.jsonl"
    args = [str(cli), "tournament", "--a", "maximum", "--frames", "0", "--exhibitions", "--exhibitions-file",
            str(file), "--report", str(report), "--log", str(log), "--threads", str(threads), "--out",
            str(pathlib.Path(folder) / "failures")]
    result = subprocess.run(args, capture_output=True, text=True, cwd=ROOT)
    if not report.is_file():
        raise ToolError(f"ofs_sim_cli tournament exited {result.returncode} with no report:\n"
                        f"{result.stdout[-2000:]}{result.stderr[-2000:]}")
    lines = []
    if log.is_file():
        for line in log.read_text(encoding="utf-8").splitlines():
            try:
                lines.append(json.loads(line))
            except ValueError:
                pass
    return json.loads(report.read_text(encoding="utf-8")), lines


def needed(break_points):
    """The ball a maximum needed next after a break of `break_points`."""
    if break_points < 120:
        return "red" if break_points % 8 == 0 else "black"
    for colour, before in reversed(COLOUR_ORDER):
        if break_points >= before:
            return colour
    return "?"


def last_strokes(lines):
    """Each exhibition's last decision by the 147, by its match id's table number."""
    out = {}
    for line in lines:
        if line.get("event") != "opponent.decision":
            continue
        data = line.get("data", {})
        match = re.search(r"-x(\d+)$", str(data.get("match_id", "")))
        if match:
            out[int(match.group(1))] = data.get("why", {})
    return out


def results(report, lines, layouts):
    """(layout, break, strokes, a 147 with no failure, why it ended) for each table played."""
    entries = report.get("exhibitions", [])
    if len(entries) != len(layouts):
        raise ToolError(f"the report has {len(entries)} exhibitions for {len(layouts)} tables")
    failed = collections.Counter(f.get("where", "") for f in report.get("failures", []))
    last = last_strokes(lines)
    out = []
    for i, (lay, entry) in enumerate(zip(layouts, entries)):
        points = entry["break_points"]
        failures = failed["exhibition " + entry["name"]]
        ok = bool(entry["maximum"]) and points == 147 and not failures
        why = ""
        if not ok:
            stroke = last.get(i, {})
            why = f"needed the {needed(points)}, last stroke {stroke.get('kind', '?')} {stroke.get('target', '')}".rstrip()
            if failures:
                why += f", {failures} failure(s)"
        out.append((lay, points, entry["strokes"], ok, why))
    return out


def play(cli, layouts, threads):
    with tempfile.TemporaryDirectory() as folder:
        file = pathlib.Path(folder) / "exhibitions.json"
        write_file(file, layouts)
        report, lines = run_tournament(cli, file, threads, folder)
    return results(report, lines, layouts)


def find(cli, table, seed, wanted, start, batch, limit, threads, tally, verbose=False):
    """`wanted` proven layouts from seed's candidates, starting at number `start`."""
    proven = []
    number = start
    while len(proven) < wanted:
        if number - start >= limit:
            raise ToolError(f"{limit} candidates gave {len(proven)} of {wanted} tables")
        layouts = []
        while len(layouts) < batch and number - start < limit:
            lay = candidate(seed, number, table)
            number += 1
            if lay is not None:
                layouts.append(lay)
        if not layouts:
            continue
        made = 0
        for lay, points, strokes, ok, why in play(cli, layouts, threads):
            tally["played"] += 1
            if verbose:
                print(f"  {lay['name']:<22} break {points:3d} in {strokes:2d} strokes  {'147' if ok else why}")
            if ok:
                made += 1
                tally["made"] += 1
                if len(proven) < wanted:
                    proven.append(lay)
            else:
                tally["died"][why.split(",")[0] + f" (break {points})"] += 1
        print(f"  {made} of {len(layouts)} candidates made 147; {len(proven)} of {wanted} tables")
        sys.stdout.flush()
    return proven, number


def limit_for(args, wanted):
    return args.limit if args.limit is not None else max(400, 25 * wanted)


def verify_and_fill(cli, table, layouts, args, start, tally):
    """Plays the file as it will be written; replaces any table that no longer makes 147 there."""
    number = start
    for _ in range(5):
        layouts = renamed(layouts)
        played = play(cli, layouts, args.threads)
        keep = [lay for lay, _, _, ok, _ in played if ok]
        if len(keep) == len(layouts):
            return layouts
        print(f"{len(layouts) - len(keep)} table(s) did not make 147 in their place in the file; replacing")
        more, number = find(cli, table, args.seed, len(layouts) - len(keep), number, args.batch,
                            limit_for(args, len(layouts) - len(keep)), args.threads, tally, args.verbose)
        layouts = keep + more
    raise ToolError("the file's tables would not all make 147 in place")


def draw(lay, table, path, scale=400):
    from PIL import Image, ImageDraw

    half_l, half_w, rail = table.length / 2.0, table.width / 2.0, table.rail
    w = int((table.length + 2 * rail) * scale)
    h = int((table.width + 2 * rail) * scale)

    def px(x, y):
        return ((x + half_l + rail) * scale, (half_w + rail - y) * scale)

    def disc(d, centre, r, fill, outline=None):
        cx, cy = px(*centre)
        d.ellipse((cx - r * scale, cy - r * scale, cx + r * scale, cy + r * scale), fill=fill, outline=outline)

    image = Image.new("RGB", (w, h), (74, 44, 24))
    d = ImageDraw.Draw(image)
    d.rectangle((*px(-half_l, half_w), *px(half_l, -half_w)), fill=(20, 110, 52))
    d.rectangle((*px(-half_l, half_w), *px(half_l, -half_w)), outline=(12, 80, 36), width=6)
    line = (220, 235, 220)
    d.line((*px(table.baulk_line, half_w), *px(table.baulk_line, -half_w)), fill=line, width=2)
    r = table.d_radius
    d.arc((*px(table.baulk_line - r, r), *px(table.baulk_line + r, -r)), 90, 270, fill=line, width=2)
    for pocket in table.pockets:
        disc(d, pocket["centre"], pocket["radius"], (8, 8, 8))
    for spot in table.spots.values():
        disc(d, spot, 0.006, line)
    colour = {"cue": (245, 245, 235), "red": (200, 20, 25), "yellow": (240, 210, 30), "green": (20, 140, 70),
              "brown": (120, 70, 30), "blue": (30, 70, 200), "pink": (240, 140, 170), "black": (15, 15, 15)}
    for ball in lay["balls"]:
        disc(d, (ball["x"], ball["y"]), table.radius, colour[ball["ball"]], outline=(0, 0, 0))
    d.text((10, 8), lay["name"], fill=(240, 240, 240))
    image.save(path)


def pictures(layouts, table, folder):
    folder = pathlib.Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    for i, lay in enumerate(layouts):
        draw(lay, table, folder / f"{i + 1}.png")
    print(f"pictures of {len(layouts)} tables in {folder}")


def load(path):
    try:
        data = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
        layouts = data["layouts"]
    except (OSError, ValueError, KeyError, TypeError) as e:
        raise ToolError(f"{path}: {e}") from e
    if not isinstance(layouts, list) or not layouts:
        raise ToolError(f"{path}: no layouts")
    return layouts


def check(cli, layouts, threads, verbose=False):
    """Plays the tables: a line for each that fails (each table with `verbose`), then the count."""
    started = time.monotonic()
    played = play(cli, layouts, threads)
    for lay, points, strokes, ok, why in played:
        if verbose or not ok:
            print(f"{lay['name']:<22} break {points:3d} in {strokes:2d} strokes  {'147' if ok else 'NOT 147: ' + why}")
    bad = sum(1 for p in played if not p[3])
    print(f"{len(played) - bad} of {len(played)} tables make 147 ({time.monotonic() - started:.1f} s)")
    return played


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="play the file's tables; exit 1 unless each makes 147")
    mode.add_argument("--replace-failing", action="store_true", help="keep the tables that make 147, replace the rest")
    parser.add_argument("--file", default=str(EXHIBITIONS), help="the exhibitions file (default core/data's)")
    parser.add_argument("--count", type=int, default=50, help="tables to write (default 50)")
    parser.add_argument("--seed", type=int, default=1, help="candidates come from this seed (default 1)")
    parser.add_argument("--start", type=int, default=0, help="the first candidate's number (default 0)")
    parser.add_argument("--batch", type=int, default=12, help="candidates played per run (default 12)")
    parser.add_argument("--limit", type=int, help="give up after this many candidates (default 25 per table "
                                                  "wanted, at least 400)")
    parser.add_argument("--threads", type=int, default=4, help="ofs_sim_cli threads, at most 4 (default 4)")
    parser.add_argument("--cli", help="ofs_sim_cli (default OFS_SIM_CLI, else the Release build)")
    parser.add_argument("--picture", metavar="DIR", help="draw each table of the file into DIR")
    parser.add_argument("--verbose", action="store_true", help="a line for every table played, not only the failing")
    args = parser.parse_args(argv)
    args.threads = max(1, min(4, args.threads))
    if args.count < 1 or args.batch < 1 or (args.limit is not None and args.limit < 1):
        parser.error("--count, --batch and --limit must be at least 1")

    try:
        table = Table()
        if args.check:
            layouts = load(args.file)
            played = check(cli_path(args.cli), layouts, args.threads, args.verbose)
            if args.picture:
                pictures(layouts, table, args.picture)
            return 0 if all(p[3] for p in played) else 1

        cli = cli_path(args.cli)
        tally = {"played": 0, "made": 0, "died": collections.Counter()}
        started = time.monotonic()
        if args.replace_failing:
            layouts = load(args.file)
            keep = [lay for lay, _, _, ok, _ in check(cli, layouts, args.threads, args.verbose) if ok]
            wanted = len(layouts) - len(keep)
        else:
            keep, wanted = [], args.count
        more, number = find(cli, table, args.seed, wanted, args.start, args.batch, limit_for(args, wanted),
                            args.threads, tally, args.verbose) if wanted else ([], args.start)
        layouts = verify_and_fill(cli, table, keep + more, args, number, tally)
        for lay in layouts:
            wrong = problems(lay, table)
            if wrong:
                raise ToolError(f"{lay['name']} breaks the layout rules: {'; '.join(wrong)}")
        write_file(args.file, layouts)
        print(f"wrote {len(layouts)} tables to {args.file} in {time.monotonic() - started:.0f} s; "
              f"{tally['made']} of {tally['played']} candidates made 147")
        for why, n in tally["died"].most_common():
            print(f"  {n:3d}  {why}")
        if args.picture:
            pictures(layouts, table, args.picture)
        return 0
    except ToolError as e:
        print(f"make_exhibitions: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
