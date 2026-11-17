"""Writes the computer opponent's report (spec 10.1, 16.4) from what `ofs_sim_cli tournament
--report` wrote, and checks stage 1's marks (docs/opponent/README.md, "What the tournament counts").

    python tools/opponent_report.py REPORT.json [MORE.json ...] --out docs/opponent/report.md [--check stage1]

One section per report file: who played whom, the frames, a table of each level's counts and the
exhibition breaks. With --check stage1 each mark is printed with its value and PASS or FAIL, a
mark no report holds is MISSING, and the exit is 1 on any FAIL or MISSING; the marks also go into
the written report.
"""
import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXHIBITIONS = ROOT / "core" / "data" / "exhibitions.json"
BANDS = ("short", "medium", "long", "unknown")
STAGE1_FRAMES = 2000
PASS, FAIL, MISSING = "PASS", "FAIL", "MISSING"


class ReportError(Exception):
    pass


def load(path):
    try:
        report = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        raise ReportError(f"{pathlib.Path(path).name}: {e}") from e
    for key in ("frames", "seed", "levels", "a", "exhibitions", "failures"):
        if key not in report:
            raise ReportError(f"{pathlib.Path(path).name}: no '{key}'; not a tournament report")
    return report


def level_id(report, side):
    level = report["levels"].get(side)
    return level.get("id") if isinstance(level, dict) else None


def level_name(report, side):
    level = report["levels"].get(side)
    return level.get("name", level.get("id")) if isinstance(level, dict) else None


def share(part, whole):
    return part / whole if whole else None


def percent(part, whole):
    value = share(part, whole)
    return f"{part}/{whole} ({value * 100:.1f}%)" if value is not None else f"{part}/{whole}"


def rows(stats):
    """(label, cell) for each line of a level's table."""
    pots = stats["pots"]
    breaks = stats["breaks"]
    think = stats["think"]
    stopped = ", ".join(f"{k} {v}" for k, v in think["stopped"].items() if v) or "none"
    out = [("frames won", str(stats["frames_won"])),
           ("strokes", str(stats["strokes"])),
           ("pots made", percent(pots["made"], pots["attempts"]))]
    out += [(f"- {band}", percent(pots["bands"][band]["made"], pots["bands"][band]["attempts"])) for band in BANDS]
    out += [("breaks", str(breaks["count"])),
            ("- mean / median", f"{breaks['mean']:.1f} / {breaks['median']:.1f}"),
            ("- highest", str(breaks["highest"])),
            ("- 50+ / 100+ / 147", f"{breaks['fifty_plus']} / {breaks['hundred_plus']} / {breaks['maximums']}"),
            ("safeties", percent(stats["safeties"], stats["strokes"])),
            ("escapes", str(stats["escapes"])),
            ("fouls", percent(stats["fouls"], stats["strokes"])),
            ("points scored", str(stats["points"])),
            ("visits with the maximum on / 147s", f"{stats['maximum']['visits_on']} / {stats['maximum']['completed']}"),
            ("simulations per stroke (mean / max)", f"{think['sims_mean']:.1f} / {think['sims_max']}"),
            ("steps per stroke (mean)", f"{think['steps_mean']:.1f}"),
            ("thinking, ms (mean / p99 / max)",
             f"{think['us_mean'] / 1000:.2f} / {think['us_p99'] / 1000:.2f} / {think['us_max'] / 1000:.2f}"),
            ("how thinking stopped", stopped),
            ("fallbacks", str(stats["fallbacks"]))]
    return out


def section(name, report):
    a, b = level_name(report, "a"), level_name(report, "b")
    frames = report["frames"]
    title = f"{a} against {b}" if frames and b else f"{a}: exhibitions"
    lines = [f"## {title}", "",
             f"`{name}`: {frames} frames, seed {report['seed']}, physics `{report.get('params_hash', '?')}`, "
             f"table `{report.get('table_hash', '?')}`."]
    hashes = report.get("level_hashes") or {}
    levels = [f"A is `{level_id(report, 'a')}` (`{hashes.get('a', '?')}`)"]
    if frames and b:
        levels.append(f"B is `{level_id(report, 'b')}` (`{hashes.get('b', '?')}`)")
    lines += [", ".join(levels) + ". A breaks in the even frames.", ""]
    if frames and b:
        a_rows, b_rows = rows(report["a"]), rows(report["b"])
        lines += [f"| | A: {a} | B: {b} |", "| --- | --- | --- |"]
        lines += [f"| {label} | {left} | {right} |" for (label, left), (_, right) in zip(a_rows, b_rows)]
        lines.append("")
    if report["exhibitions"]:
        lines += [f"Exhibitions, {a} alone:", "", "| Table | Break | Strokes | 147 |", "| --- | --- | --- | --- |"]
        lines += [f"| {e['name']} | {e['break_points']} | {e['strokes']} | {'yes' if e['maximum'] else 'no'} |"
                  for e in report["exhibitions"]]
        lines.append("")
    failures = report["failures"]
    lines.append(f"Failures: {len(failures)}.")
    for f in failures[:10]:
        lines.append(f"- {f['where']}, stroke {f['stroke']} ({f['level']}), {f['kind']}: {f['detail']}")
    if len(failures) > 10:
        lines.append(f"- and {len(failures) - 10} more")
    lines.append("")
    return lines


def combined(report, wanted):
    """The sides of `report` played by level `wanted`, their counts added: (pots by band, breaks, strokes, safeties)."""
    bands = {band: [0, 0] for band in BANDS}
    breaks_count, breaks_total, strokes, safeties = 0, 0.0, 0, 0
    for side in ("a", "b"):
        if level_id(report, side) != wanted or not report.get(side):
            continue
        stats = report[side]
        for band in BANDS:
            bands[band][0] += stats["pots"]["bands"][band]["made"]
            bands[band][1] += stats["pots"]["bands"][band]["attempts"]
        breaks_count += stats["breaks"]["count"]
        breaks_total += stats["breaks"]["mean"] * stats["breaks"]["count"]
        strokes += stats["strokes"]
        safeties += stats["safeties"]
    return bands, (breaks_total / breaks_count if breaks_count else None), strokes, safeties


def best(reports, test):
    """The report passing `test` with the most frames, or None."""
    found = [(report["frames"], -i, name, report) for i, (name, report) in enumerate(reports) if test(report)]
    return max(found)[2:] if found else None


def mark(name, value, wanted, ok):
    return {"name": name, "value": value, "wanted": wanted, "status": PASS if ok else FAIL}


def missing(name, why):
    return {"name": name, "value": "no report", "wanted": why, "status": MISSING}


def in_range(value, low, high):
    return value is not None and low <= value <= high


def stage1(reports, exhibitions_file=EXHIBITIONS):
    marks = []

    def against_itself(report):
        return report["frames"] > 0 and level_id(report, "a") == "beginner" and level_id(report, "b") == "beginner"

    found = best(reports, against_itself)
    names = ["Beginner's frames against itself", "Beginner's medium pots", "Beginner's long pots",
             "Beginner's mean break", "Beginner's safeties"]
    if found is None:
        marks += [missing(name, "a report of beginner against beginner") for name in names]
    else:
        name, report = found
        bands, mean_break, strokes, safeties = combined(report, "beginner")
        medium = share(*bands["medium"])
        long_ = share(*bands["long"])
        safety = share(safeties, strokes)
        marks += [
            mark(names[0], f"{report['frames']} ({name})", f"at least {STAGE1_FRAMES}",
                 report["frames"] >= STAGE1_FRAMES),
            mark(names[1], percent(*bands["medium"]), "30 to 40%", in_range(medium, 0.30, 0.40)),
            mark(names[2], percent(*bands["long"]), "5 to 15%", in_range(long_, 0.05, 0.15)),
            mark(names[3], "none" if mean_break is None else f"{mean_break:.2f}", "under 10",
                 mean_break is not None and mean_break < 10),
            mark(names[4], percent(safeties, strokes), "under 10% of strokes", safety is not None and safety < 0.10),
        ]

    def maximum_against_beginner(report):
        ids = {level_id(report, "a"), level_id(report, "b")}
        return report["frames"] > 0 and ids == {"maximum", "beginner"}

    found = best(reports, maximum_against_beginner)
    if found is None:
        marks += [missing("The 147's frames won against Beginner", "a report of maximum against beginner"),
                  missing("The 147 against Beginner: no failure", "a report of maximum against beginner")]
    else:
        name, report = found
        side = "a" if level_id(report, "a") == "maximum" else "b"
        won = report[side]["frames_won"]
        marks += [
            mark("The 147's frames won against Beginner", f"{percent(won, report['frames'])} ({name})",
                 "at least 95%", won >= 0.95 * report["frames"]),
            mark("The 147 against Beginner: no failure", str(len(report["failures"])), "0",
                 not report["failures"]),
        ]

    layouts = [layout["name"] for layout in json.loads(exhibitions_file.read_text(encoding="utf-8"))["layouts"]]

    def maximum_exhibitions(report):
        return level_id(report, "a") == "maximum" and report["exhibitions"]

    found = best(reports, maximum_exhibitions)
    if found is None:
        marks.append(missing("The 147 on every exhibition table", "a report of maximum with --exhibitions"))
    else:
        name, report = found
        played = {e["name"]: e for e in report["exhibitions"]}
        made = [n for n in layouts if n in played and played[n]["maximum"]]
        unplayed = [n for n in layouts if n not in played]
        value = f"147 on {len(made)} of {len(layouts)} tables ({name})"
        if unplayed:
            value += "; not played: " + ", ".join(unplayed)
        marks.append(mark("The 147 on every exhibition table", value, "147 on each", len(made) == len(layouts)))
    return marks


CHECKS = {"stage1": stage1}


def write(reports, marks, out):
    lines = ["# The computer opponent's report", "",
             "Written by `tools/opponent_report.py` from `ofs_sim_cli tournament --report` files; "
             "docs/opponent/README.md says what each count is.", ""]
    if marks is not None:
        lines += ["## Stage 1's marks", "", "| Mark | Value | Wanted | |", "| --- | --- | --- | --- |"]
        lines += [f"| {m['name']} | {m['value']} | {m['wanted']} | {m['status']} |" for m in marks]
        lines.append("")
    for name, report in reports:
        lines += section(name, report)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("reports", nargs="+", type=pathlib.Path, help="what ofs_sim_cli tournament --report wrote")
    parser.add_argument("--out", type=pathlib.Path, required=True, help="the Markdown report to write")
    parser.add_argument("--check", choices=sorted(CHECKS), help="apply a stage's marks")
    args = parser.parse_args(argv)
    try:
        reports = [(path.name, load(path)) for path in args.reports]
    except ReportError as e:
        print(f"opponent_report: {e}", file=sys.stderr)
        return 2
    marks = CHECKS[args.check](reports) if args.check else None
    write(reports, marks, args.out)
    print(f"wrote {args.out.name}")
    if marks is None:
        return 0
    for m in marks:
        print(f"{m['status']:<7} {m['name']}: {m['value']} (wanted {m['wanted']})")
    bad = [m for m in marks if m["status"] != PASS]
    print(f"{len(marks) - len(bad)} of {len(marks)} marks met" + (f"; not met: {len(bad)}" if bad else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
