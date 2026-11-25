"""Writes the computer opponent's report (spec 10.1, 16.4) from what `ofs_sim_cli tournament
--report` wrote, and checks a stage's marks (docs/opponent/README.md: stage 1's in "What the
tournament counts", stage 2's in "The ladder").

    python tools/opponent_report.py REPORT.json [MORE.json ...] --out docs/opponent/report.md \
        [--check stage1] [--check stage2]

One section per report file: who played whom, the frames, a table of each level's counts and the
exhibition breaks. With --check each mark is printed with its value and PASS or FAIL, a mark no
report holds is MISSING, and the exit is 1 on any FAIL or MISSING, every mark printed first; a
count the stage only reports is REPORTED and never fails. The marks also go into the written
report. When any report has a level of stage 2 the report also gets the ladder, the tactics' counts
and the thinking of each level.
"""
import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXHIBITIONS = ROOT / "core" / "data" / "exhibitions.json"
BANDS = ("short", "medium", "long", "unknown")
STAGE1_FRAMES = 2000
# Stage 2 (README, "The ladder"): each level against itself, and each over the one below.
STAGE2_FRAMES = 1000
LADDER_FRAMES = 300
LADDER_SHARE = 0.70
WORLD_CENTURY_SHARE = 0.10
WORLD_SLOWEST_S = 1.5
NAMES = {"beginner": "Beginner", "club": "Club", "amateur": "Amateur", "pro": "Pro", "world": "World class"}
# (level, medium pots, long pots, typical break: low, high or None for no ceiling)
STAGE2_LEVELS = (("club", (0.55, 0.65), (0.20, 0.30), (10, 20)),
                 ("amateur", (0.75, 0.85), (0.40, 0.50), (20, 40)),
                 ("pro", (0.86, 0.94), (0.60, 0.70), (40, 70)),
                 ("world", (0.92, 0.98), (0.70, 0.80), (60, None)))
STAGE2_IDS = tuple(level for level, *_ in STAGE2_LEVELS)
LADDER = (("club", "beginner"), ("amateur", "club"), ("pro", "amateur"), ("world", "pro"))
# The failures that are the engine refusing a level's stroke or choice.
REFUSED = ("opponent", "invalid", "choice")
PASS, FAIL, MISSING, REPORTED = "PASS", "FAIL", "MISSING", "REPORTED"


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
            ("safe with a pot to play", percent(stats["safeties_with_pot"],
                                                stats["safeties_with_pot"] + pots["attempts"])),
            ("no pot at all", percent(stats["no_pot"], stats["strokes"])),
            ("fouls", percent(stats["fouls"], stats["strokes"])),
            ("points scored", str(stats["points"])),
            ("visits with the maximum on / 147s", f"{stats['maximum']['visits_on']} / {stats['maximum']['completed']}"),
            ("simulations per stroke (mean / max)", f"{think['sims_mean']:.1f} / {think['sims_max']}"),
            ("steps per stroke (mean)", f"{think['steps_mean']:.1f}"),
            ("thinking, ms (mean / p99 / max)",
             f"{think['us_mean'] / 1000:.2f} / {think['us_p99'] / 1000:.2f} / {think['us_max'] / 1000:.2f}"),
            ("how thinking stopped", stopped),
            ("fallbacks", str(stats["fallbacks"]))]
    if "tactics" in stats:
        out += tactic_rows(stats)
    return out


def tactic_rows(stats):
    tactics, highs = stats["tactics"], stats["frame_highs"]
    safeties, snookers = tactics["safeties"], tactics["snookers"]
    escapes, break_offs = tactics["escapes"], tactics["break_offs"]
    by_cushions = ", ".join(f"{k}: {v['hit']}/{v['played']}" for k, v in escapes["by_cushions"].items()) or "none"
    return [("typical break (median of the frames' highest)", f"{highs['typical']:.1f}"),
            ("frames with a century", percent(highs["century"], highs["frames"])),
            ("safeties then a pot attempt", percent(safeties["then_pot_attempt"], safeties["played"])),
            ("- then potted", percent(safeties["then_potted"], safeties["played"])),
            ("snookers laid / called", f"{snookers['laid']} / {snookers['called']}"),
            ("- then fouled / foul points", f"{snookers['then_fouled']} / {snookers['then_foul_points']}"),
            ("escapes that hit a ball on", percent(escapes["hit"], escapes["played"])),
            ("- by cushions", by_cushions),
            ("break-offs then a pot", percent(break_offs["then_pot"], break_offs["played"])),
            ("concessions / claims", f"{stats['concessions']} / {stats['claims']}")]


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
    """The sides of `report` played by level `wanted`, their counts added: (pots by band, mean break,
    strokes, attempts, safeties with a pot, strokes with no pot)."""
    bands = {band: [0, 0] for band in BANDS}
    breaks_count, breaks_total, strokes, attempts, safe_with_pot, no_pot = 0, 0.0, 0, 0, 0, 0
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
        attempts += stats["pots"]["attempts"]
        safe_with_pot += stats["safeties_with_pot"]
        no_pot += stats["no_pot"]
    mean_break = breaks_total / breaks_count if breaks_count else None
    return bands, mean_break, strokes, attempts, safe_with_pot, no_pot


def best(reports, test):
    """The report passing `test` with the most frames, or None."""
    found = [(report["frames"], -i, name, report) for i, (name, report) in enumerate(reports) if test(report)]
    return max(found)[2:] if found else None


def mark(name, value, wanted, ok):
    return {"name": name, "value": value, "wanted": wanted, "status": PASS if ok else FAIL}


def reported(name, value, wanted):
    return {"name": name, "value": value, "wanted": wanted, "status": REPORTED}


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
             "Beginner's mean break", "Beginner's safeties with a pot to play"]
    if found is None:
        marks += [missing(name, "a report of beginner against beginner") for name in names]
    else:
        name, report = found
        bands, mean_break, strokes, attempts, safe_with_pot, no_pot = combined(report, "beginner")
        medium = share(*bands["medium"])
        long_ = share(*bands["long"])
        with_pot = attempts + safe_with_pot
        safety = share(safe_with_pot, with_pot)
        marks += [
            mark(names[0], f"{report['frames']} ({name})", f"at least {STAGE1_FRAMES}",
                 report["frames"] >= STAGE1_FRAMES),
            mark(names[1], percent(*bands["medium"]), "30 to 40%", in_range(medium, 0.30, 0.40)),
            mark(names[2], percent(*bands["long"]), "5 to 15%", in_range(long_, 0.05, 0.15)),
            mark(names[3], "none" if mean_break is None else f"{mean_break:.2f}", "under 10",
                 mean_break is not None and mean_break < 10),
            mark(names[4], percent(safe_with_pot, with_pot), "under 10% of strokes with a pot to play",
                 safety is not None and safety < 0.10),
            reported("Beginner's strokes with no pot at all", percent(no_pot, strokes), "reported, not marked"),
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


def sides_of(report, level):
    """The counts of every side of `report` that `level` played: both in a frame against itself."""
    return [report[side] for side in ("a", "b") if level_id(report, side) == level and report.get(side)]


def high_counts(sides):
    """{break: frames} of the frames' highest breaks over `sides`."""
    counts = {}
    for stats in sides:
        for points, frames in stats.get("frame_highs", {}).get("counts", []):
            counts[points] = counts.get(points, 0) + frames
    return counts


def median_of(counts):
    n = sum(counts.values())
    if not n:
        return None
    ordered = sorted(counts.items())

    def at(rank):
        seen = 0
        for points, frames in ordered:
            seen += frames
            if rank < seen:
                return points
        return ordered[-1][0]
    return at(n // 2) if n % 2 else (at(n // 2 - 1) + at(n // 2)) / 2


def band_share(sides, band):
    made = sum(s["pots"]["bands"][band]["made"] for s in sides)
    attempts = sum(s["pots"]["bands"][band]["attempts"] for s in sides)
    return made, attempts


def against_itself(level):
    return lambda report: report["frames"] > 0 and level_id(report, "a") == level_id(report, "b") == level


def pairing(upper, lower):
    return lambda report: report["frames"] > 0 and {level_id(report, "a"), level_id(report, "b")} == {upper, lower}


def stage2(reports):
    marks = []
    for level, medium_range, long_range, typical_range in STAGE2_LEVELS:
        name = NAMES[level]
        names = [f"{name}'s frames against itself", f"{name}'s medium pots", f"{name}'s long pots",
                 f"{name}'s typical break"]
        if level == "world":
            names.append(f"{name}'s frames with a century")
        found = best(reports, against_itself(level))
        if found is None:
            marks += [missing(n, f"a report of {level} against {level}") for n in names]
            continue
        file, report = found
        sides = sides_of(report, level)
        medium, long_ = band_share(sides, "medium"), band_share(sides, "long")
        counts = high_counts(sides)
        typical = median_of(counts)
        low, high = typical_range
        marks += [
            mark(names[0], f"{report['frames']} ({file})", f"at least {STAGE2_FRAMES}",
                 report["frames"] >= STAGE2_FRAMES),
            mark(names[1], percent(*medium), f"{medium_range[0]:.0%} to {medium_range[1]:.0%}",
                 in_range(share(*medium), *medium_range)),
            mark(names[2], percent(*long_), f"{long_range[0]:.0%} to {long_range[1]:.0%}",
                 in_range(share(*long_), *long_range)),
            mark(names[3], "none" if typical is None else f"{typical:g}",
                 f"{low} and over" if high is None else f"{low} to {high}",
                 typical is not None and low <= typical <= (high if high is not None else typical)),
        ]
        if level == "world":
            centuries = sum(frames for points, frames in counts.items() if points >= 100)
            frames = sum(counts.values())
            marks.append(mark(names[4], percent(centuries, frames), f"at least {WORLD_CENTURY_SHARE:.0%}",
                              frames > 0 and centuries >= WORLD_CENTURY_SHARE * frames))

    for upper, lower in LADDER:
        label = f"{NAMES[upper]} over {NAMES[lower]}"
        found = best(reports, pairing(upper, lower))
        if found is None:
            why = f"a report of {upper} against {lower}"
            marks += [missing(f"{label}: frames", why), missing(f"{label}: frames won", why)]
            continue
        file, report = found
        won = sum(s["frames_won"] for s in sides_of(report, upper))
        marks += [mark(f"{label}: frames", f"{report['frames']} ({file})", f"at least {LADDER_FRAMES}",
                       report["frames"] >= LADDER_FRAMES),
                  mark(f"{label}: frames won", percent(won, report["frames"]), f"at least {LADDER_SHARE:.0%}",
                       won >= LADDER_SHARE * report["frames"])]

    played = [(file, report) for file, report in reports
              if report["frames"] > 0 and {level_id(report, "a"), level_id(report, "b")} & set(STAGE2_IDS)]
    if not played:
        why = "a report of a stage 2 level"
        marks += [missing("No stroke refused by the engine", why), missing("No fallback", why)]
    else:
        refused = [f for _, report in played for f in report["failures"] if f["kind"] in REFUSED]
        fallbacks = sum(report[side]["fallbacks"] for _, report in played for side in ("a", "b") if report.get(side))
        marks += [mark("No stroke refused by the engine", f"{len(refused)} in {len(played)} reports", "0",
                       not refused),
                  mark("No fallback", f"{fallbacks} in {len(played)} reports", "0", fallbacks == 0)]
    world = [s for _, report in reports for s in sides_of(report, "world")]
    if not world:
        marks.append(missing("World class's slowest decision", "a report in which world plays"))
    else:
        slowest = max(s["think"]["us_max"] for s in world) / 1e6
        marks.append(mark("World class's slowest decision", f"{slowest:.2f} s", f"within {WORLD_SLOWEST_S} s",
                          slowest <= WORLD_SLOWEST_S))
    return marks


CHECKS = {"stage1": stage1, "stage2": stage2}
STAGE_TITLES = {"stage1": "Stage 1's marks", "stage2": "Stage 2's marks"}


def cell(part, whole):
    return percent(part, whole) if whole else "-"


def stage2_part(reports):
    """The ladder, the tactics and the thinking of each level, from the reports that have them."""
    levels = [level for level in ("beginner",) + STAGE2_IDS if best(reports, against_itself(level))]
    lines = []
    ladder = [(upper, lower, best(reports, pairing(upper, lower))) for upper, lower in LADDER]
    if any(found for *_, found in ladder):
        lines += ["## The ladder", "", "| Pairing | Frames | Won by the higher | |", "| --- | --- | --- | --- |"]
        for upper, lower, found in ladder:
            if found is None:
                lines.append(f"| {NAMES[upper]} over {NAMES[lower]} | no report | | |")
                continue
            file, report = found
            won = sum(s["frames_won"] for s in sides_of(report, upper))
            lines.append(f"| {NAMES[upper]} over {NAMES[lower]} | {report['frames']} | "
                         f"{percent(won, report['frames'])} | `{file}` |")
        lines.append("")
    merged = {level: sides_of(best(reports, against_itself(level))[1], level) for level in levels}
    merged = {level: sides for level, sides in merged.items() if sides and "tactics" in sides[0]}
    if merged:
        lines += ["## Tactics per level", "",
                  "Each level against itself, both sides added; README \"The ladder\" says what each is.", "",
                  "| | " + " | ".join(NAMES[level] for level in merged) + " |",
                  "| --- |" + " --- |" * len(merged)]

        def total(sides, *path):
            value = 0
            for stats in sides:
                node = stats
                for key in path:
                    node = node[key]
                value += node
            return value

        def row(label, fn):
            lines.append(f"| {label} | " + " | ".join(fn(sides) for sides in merged.values()) + " |")

        def cushions(sides):
            buckets = {}
            for stats in sides:
                for key, counts in stats["tactics"]["escapes"]["by_cushions"].items():
                    hit, played = buckets.get(key, (0, 0))
                    buckets[key] = (hit + counts["hit"], played + counts["played"])
            return ", ".join(f"{k}: {h}/{p}" for k, (h, p) in buckets.items()) or "-"

        def typical(sides):
            value = median_of(high_counts(sides))
            return "-" if value is None else f"{value:g}"

        row("strokes", lambda s: str(total(s, "strokes")))
        row("safeties", lambda s: str(total(s, "tactics", "safeties", "played")))
        row("- then a pot attempt", lambda s: cell(total(s, "tactics", "safeties", "then_pot_attempt"),
                                                   total(s, "tactics", "safeties", "played")))
        row("- then potted", lambda s: cell(total(s, "tactics", "safeties", "then_potted"),
                                            total(s, "tactics", "safeties", "played")))
        row("snookers laid", lambda s: str(total(s, "tactics", "snookers", "laid")))
        row("- called snookers", lambda s: cell(total(s, "tactics", "snookers", "called"),
                                                total(s, "tactics", "snookers", "laid")))
        row("- then a foul", lambda s: cell(total(s, "tactics", "snookers", "then_fouled"),
                                            total(s, "tactics", "snookers", "laid")))
        row("- foul points given after them", lambda s: str(total(s, "tactics", "snookers", "then_foul_points")))
        row("escapes that hit a ball on", lambda s: cell(total(s, "tactics", "escapes", "hit"),
                                                         total(s, "tactics", "escapes", "played")))
        row("- by cushions (hit/played)", cushions)
        row("break-offs then a pot", lambda s: cell(total(s, "tactics", "break_offs", "then_pot"),
                                                    total(s, "tactics", "break_offs", "played")))
        row("typical break", typical)
        row("frames with a century", lambda s: cell(total(s, "frame_highs", "century"),
                                                    total(s, "frame_highs", "frames")))
        row("concessions / claims", lambda s: f"{total(s, 'concessions')} / {total(s, 'claims')}")
        lines.append("")

        lines += ["## Thinking per level", "",
                  "Each level against itself; the mean is over both sides' strokes, p99 and max the higher side's.",
                  "", "| | " + " | ".join(NAMES[level] for level in merged) + " |",
                  "| --- |" + " --- |" * len(merged)]

        def mean(sides, key):
            strokes = total(sides, "strokes")
            return sum(s["think"][key] * s["strokes"] for s in sides) / strokes if strokes else 0.0

        def stopped(sides):
            counts = {}
            for stats in sides:
                for key, value in stats["think"]["stopped"].items():
                    counts[key] = counts.get(key, 0) + value
            return ", ".join(f"{k} {v}" for k, v in counts.items() if v) or "none"

        row("simulations (mean / max)",
            lambda s: f"{mean(s, 'sims_mean'):.1f} / {max(x['think']['sims_max'] for x in s)}")
        row("steps (mean)", lambda s: f"{mean(s, 'steps_mean'):.1f}")
        row("ms (mean / p99 / max)",
            lambda s: f"{mean(s, 'us_mean') / 1000:.2f} / {max(x['think']['us_p99'] for x in s) / 1000:.2f} / "
                      f"{max(x['think']['us_max'] for x in s) / 1000:.2f}")
        row("how thinking stopped", stopped)
        row("fallbacks", lambda s: str(total(s, "fallbacks")))
        lines.append("")
    return lines


def write(reports, marks, out):
    lines = ["# The computer opponent's report", "",
             "Written by `tools/opponent_report.py` from `ofs_sim_cli tournament --report` files; "
             "docs/opponent/README.md says what each count is.", ""]
    for stage, stage_marks in (marks or {}).items():
        lines += [f"## {STAGE_TITLES[stage]}", "", "| Mark | Value | Wanted | |", "| --- | --- | --- | --- |"]
        lines += [f"| {m['name']} | {m['value']} | {m['wanted']} | {m['status']} |" for m in stage_marks]
        lines.append("")
    if any({level_id(report, "a"), level_id(report, "b")} & set(STAGE2_IDS) for _, report in reports):
        lines += stage2_part(reports)
    for name, report in reports:
        lines += section(name, report)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("reports", nargs="+", type=pathlib.Path, help="what ofs_sim_cli tournament --report wrote")
    parser.add_argument("--out", type=pathlib.Path, required=True, help="the Markdown report to write")
    parser.add_argument("--check", choices=sorted(CHECKS), action="append",
                        help="apply a stage's marks; may be given for each stage")
    args = parser.parse_args(argv)
    try:
        reports = [(path.name, load(path)) for path in args.reports]
    except ReportError as e:
        print(f"opponent_report: {e}", file=sys.stderr)
        return 2
    by_stage = {stage: CHECKS[stage](reports) for stage in dict.fromkeys(args.check or [])}
    write(reports, by_stage, args.out)
    print(f"wrote {args.out.name}")
    if not by_stage:
        return 0
    marks = [m for stage_marks in by_stage.values() for m in stage_marks]
    for m in marks:
        wanted = "" if m["status"] == REPORTED else f" (wanted {m['wanted']})"
        print(f"{m['status']:<8} {m['name']}: {m['value']}{wanted}")
    marked = [m for m in marks if m["status"] != REPORTED]
    bad = [m for m in marked if m["status"] != PASS]
    print(f"{len(marked) - len(bad)} of {len(marked)} marks met" + (f"; not met: {len(bad)}" if bad else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
