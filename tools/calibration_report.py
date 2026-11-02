# /// script
# requires-python = ">=3.10"
# dependencies = ["matplotlib"]
# ///
"""Turns the data `ofs_sim_cli calibrate --out DIR` writes into PNG charts and report.md (spec 16.1).

    uv run tools/calibration_report.py <data dir> <out dir>

Each chart gets a sentence saying what it shows and whether it meets its target from spec 7.11.
Where the spec's target is a model or the owner's feel rather than a number, the chart is marked
for review: the calibration suite (core/tests/calibration) holds the exact tolerances.
"""
from __future__ import annotations

import argparse
import json
import math
import pathlib
import sys
from dataclasses import dataclass, field

# Report order; series the report doesn't know follow in name order.
ORDER = ["deflection", "throw", "cushion_rebound", "cushion_side", "squirt", "swerve", "lag", "screw_follow",
         "break_sim_time"]
REQUIRED = ("series", "title", "params_hash", "columns", "rows")

MET = "met"
NOT_MET = "not met"
REVIEW = "for review"
NO_DATA = "no data"

# The bands this report checks against, from spec 7.11 and the sources it names.
ROLLING_DEFLECTION_DEG = (25.0, 35.0)  # C4: about 30 degrees between 1/4 and 3/4 ball (Alciatore TP A.4)
STUN_SEPARATION_DEG = (80.0, 92.0)     # C5: 90 degrees, closed a little by restitution and throw
HALF_BALL_THROW_DEG = (1.0, 6.0)       # C6: a few degrees at half-ball (Alciatore TP A.14, throw FAQ)
MAX_SIDE_SQUIRT_DEG = (1.0, 3.0)       # C9: the spec's starting range
BUDGET_US = 2000.0                     # 7.13


@dataclass
class Series:
    name: str
    title: str
    target: str
    params_hash: str
    table_hash: str
    core_version: str
    build: str
    columns: list[str]
    rows: list[list]
    summary: dict = field(default_factory=dict)
    notes: list[str] = field(default_factory=list)

    def records(self) -> list[dict]:
        return [dict(zip(self.columns, row)) for row in self.rows]

    def where(self, **match) -> list[dict]:
        return [r for r in self.records() if all(r.get(k) == v for k, v in match.items())]


@dataclass
class Verdict:
    status: str
    sentence: str


def load_series(data_dir) -> tuple[dict[str, Series], list[str]]:
    """Every *.json series in data_dir, and a problem line for each file that isn't one."""
    found: dict[str, Series] = {}
    problems: list[str] = []
    directory = pathlib.Path(data_dir)
    if not directory.is_dir():
        return found, [f"{directory} is not a folder"]
    for path in sorted(directory.glob("*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            problems.append(f"{path.name}: not readable JSON ({error})")
            continue
        if not isinstance(data, dict):
            problems.append(f"{path.name}: not a JSON object")
            continue
        missing = [key for key in REQUIRED if key not in data]
        if missing:
            problems.append(f"{path.name}: missing {', '.join(missing)}")
            continue
        columns, rows = data["columns"], data["rows"]
        if not isinstance(columns, list) or not isinstance(rows, list):
            problems.append(f"{path.name}: columns and rows must be lists")
            continue
        bad = [i for i, row in enumerate(rows) if not isinstance(row, list) or len(row) != len(columns)]
        if bad:
            problems.append(f"{path.name}: {len(bad)} rows don't have {len(columns)} values "
                            f"(first: row {bad[0]})")
            continue
        name = str(data["series"])
        if name in found:
            problems.append(f"{path.name}: a second '{name}' series, ignored")
            continue
        found[name] = Series(name=name, title=str(data["title"]), target=str(data.get("target", "")),
                             params_hash=str(data["params_hash"]), table_hash=str(data.get("table_hash", "")),
                             core_version=str(data.get("core_version", "")), build=str(data.get("build", "")),
                             columns=[str(c) for c in columns], rows=rows,
                             summary=data.get("summary") if isinstance(data.get("summary"), dict) else {},
                             notes=[str(n) for n in data.get("notes", []) if isinstance(n, str)])
    hashes = sorted({s.params_hash for s in found.values()})
    if len(hashes) > 1:
        problems.append(f"the series were made with different parameter sets: {', '.join(hashes)}")
    return found, problems


def ordered(series: dict[str, Series]) -> list[Series]:
    known = [series[name] for name in ORDER if name in series]
    return known + [series[name] for name in sorted(series) if name not in ORDER]


def number(value) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        return None
    return float(value)


def values(rows: list[dict], key: str) -> list[float]:
    return [v for v in (number(r.get(key)) for r in rows) if v is not None]


def within(value: float, band: tuple[float, float]) -> bool:
    return band[0] <= value <= band[1]


def deg(value: float) -> str:
    return f"{value:.1f} degrees"


def span(found: list[float], unit: str = "degrees") -> str:
    return f"{min(found):.1f} to {max(found):.1f} {unit}"


def combine(*statuses: str) -> str:
    if NOT_MET in statuses:
        return NOT_MET
    if all(s == NO_DATA for s in statuses):
        return NO_DATA
    if REVIEW in statuses or NO_DATA in statuses:
        return REVIEW
    return MET


def evaluate_deflection(s: Series) -> Verdict:
    mid = [r for r in s.records() if number(r.get("fraction")) is not None and 0.25 <= r["fraction"] <= 0.75]
    rolling = values([r for r in mid if r.get("mode") == "rolling"], "cue_deflection_deg")
    stun = values([r for r in mid if r.get("mode") == "stun"], "separation_deg")
    parts, statuses = [], []
    if rolling:
        ok = all(within(v, ROLLING_DEFLECTION_DEG) for v in rolling)
        statuses.append(MET if ok else NOT_MET)
        parts.append(f"A rolling cue ball cut between a quarter and three-quarter ball deflects "
                     f"{span(rolling)} from its aim line, {'inside' if ok else 'outside'} the 30 degree rule's "
                     f"{ROLLING_DEFLECTION_DEG[0]:.0f} to {ROLLING_DEFLECTION_DEG[1]:.0f} (C4).")
    else:
        statuses.append(NO_DATA)
        parts.append("There is no rolling-cut data (C4).")
    if stun:
        ok = all(within(v, STUN_SEPARATION_DEG) for v in stun)
        statuses.append(MET if ok else NOT_MET)
        parts.append(f"Stunned, the two balls part at {span(stun)}, {'inside' if ok else 'outside'} the 90 "
                     f"degree rule's {STUN_SEPARATION_DEG[0]:.0f} to {STUN_SEPARATION_DEG[1]:.0f} (C5).")
    else:
        statuses.append(NO_DATA)
        parts.append("There is no stun-cut data (C5).")
    return Verdict(combine(*statuses), " ".join(parts))


def evaluate_throw(s: Series) -> Verdict:
    half = [r for r in s.records() if number(r.get("fraction")) is not None and abs(r["fraction"] - 0.5) < 0.01
            and number(r.get("throw_deg")) is not None and number(r.get("speed_m_s")) is not None]
    if len(half) < 2:
        return Verdict(NO_DATA, "There is not enough half-ball throw data to compare speeds (C6).")
    half.sort(key=lambda r: r["speed_m_s"])
    listing = ", ".join(f"{deg(r['throw_deg'])} at {r['speed_m_s']:g} m/s" for r in half)
    slower_more = half[0]["throw_deg"] > half[-1]["throw_deg"]
    in_band = all(within(r["throw_deg"], HALF_BALL_THROW_DEG) for r in half)
    status = MET if slower_more and in_band else NOT_MET
    sentence = (f"At a half-ball hit the object ball is thrown {listing}. Slower shots should throw more "
                f"({'they do' if slower_more else 'they do not'}) and stay within the few degrees published "
                f"measurements show, {HALF_BALL_THROW_DEG[0]:.0f} to {HALF_BALL_THROW_DEG[1]:.0f} here "
                f"({'they do' if in_band else 'they do not'}) (C6).")
    return Verdict(status, sentence)


def evaluate_cushion_rebound(s: Series) -> Verdict:
    rows = [r for r in s.records() if number(r.get("rebound_deg")) is not None]
    if not rows:
        return Verdict(NO_DATA, "The ball never reached the cushion in this data (C7).")
    ratios = values(rows, "speed_ratio")
    angles = values(rows, "rebound_deg")
    faster = [r for r in ratios if r > 1.0 + 1e-9]
    if faster:
        return Verdict(NOT_MET, f"In {len(faster)} shots the ball comes off the cushion faster than it arrived "
                                f"(C7, C12).")
    kept = span([r * 100 for r in ratios], "%")
    return Verdict(REVIEW, f"Rebound angles run {span(angles)} and the ball keeps {kept} of its speed. The "
                           f"target is Mathavan, Jackson and Parkin's model; the calibration suite checks the "
                           f"numbers (C7).")


def evaluate_cushion_side(s: Series) -> Verdict:
    rows = sorted((r for r in s.records()
                   if number(r.get("rebound_deg")) is not None and number(r.get("side")) is not None),
                  key=lambda r: r["side"])
    plain = [r for r in rows if r["side"] == 0]
    if len(rows) < 3 or not plain or rows[0]["side"] >= 0 or rows[-1]["side"] <= 0:
        return Verdict(NO_DATA, "There is not enough running and check side data (C8).")
    check, middle, running = rows[0]["rebound_deg"], plain[0]["rebound_deg"], rows[-1]["rebound_deg"]
    ok = running > middle > check
    return Verdict(MET if ok else NOT_MET,
                   f"Off the cushion at 45 degrees, running side sends the ball off at {deg(running)}, no "
                   f"side at {deg(middle)} and check side at {deg(check)}. Running side should widen the "
                   f"angle and check side narrow it ({'it does' if ok else 'it does not'}) (C8).")


def evaluate_squirt(s: Series) -> Verdict:
    rows = [r for r in s.records() if r.get("miscue") is False and number(r.get("squirt_deg")) is not None
            and number(r.get("tip_x")) is not None]
    if not rows or max(r["tip_x"] for r in rows) <= 0:
        return Verdict(NO_DATA, "There is no squirt data with side (C9).")
    widest = max(rows, key=lambda r: r["tip_x"])
    squirt = abs(widest["squirt_deg"])
    if squirt == 0:
        return Verdict(NO_DATA, "The strike gives no squirt at all in this data (C9).")
    ok = within(squirt, MAX_SIDE_SQUIRT_DEG)
    return Verdict(MET if ok else NOT_MET,
                   f"With the most side before a miscue ({widest['tip_x']:.2f} R) the cue ball leaves "
                   f"{deg(squirt)} off the aim line, {'inside' if ok else 'outside'} the "
                   f"{MAX_SIDE_SQUIRT_DEG[0]:.0f} to {MAX_SIDE_SQUIRT_DEG[1]:.0f} degree range (C9).")


def path_turn(points: list[tuple[float, float]]) -> float | None:
    """Degrees the path turns between its first and last moves, anticlockwise positive."""
    steps = [(b[0] - a[0], b[1] - a[1]) for a, b in zip(points, points[1:])]
    moves = [step for step in steps if step != (0, 0)]
    if len(moves) < 2:
        return None
    (x0, y0), (x1, y1) = moves[0], moves[-1]
    return math.degrees(math.atan2(x0 * y1 - y0 * x1, x0 * x1 + y0 * y1))


def swerve_paths(s: Series) -> dict[str, dict]:
    paths: dict[str, dict] = {}
    for r in s.records():
        x, y = number(r.get("x_m")), number(r.get("y_m"))
        entry = paths.setdefault(str(r.get("variant")), {"elevation_deg": r.get("elevation_deg"),
                                                         "tip_x": r.get("tip_x"), "points": []})
        if x is not None and y is not None:
            entry["points"].append((x, y))
    return paths


def evaluate_swerve(s: Series) -> Verdict:
    checked, wrong = [], []
    for name, path in swerve_paths(s).items():
        elevation, tip = number(path["elevation_deg"]), number(path["tip_x"])
        if not elevation or not tip:
            continue
        turn = path_turn(path["points"])
        if turn is None:
            continue
        # Positive tip_x is right of centre as the player sees it: the path should bend clockwise.
        right_way = turn < -0.5 if tip > 0 else turn > 0.5
        checked.append(f"{name} turns {abs(turn):.1f} degrees {'right' if turn < 0 else 'left'}")
        if not right_way:
            wrong.append(name)
    if not checked:
        return Verdict(NO_DATA, "No swerve path with a raised cue and side moved in this data (C10).")
    status = NOT_MET if wrong else REVIEW
    tail = (f" Against the side: {', '.join(wrong)}." if wrong else
            " Each curves towards its side; whether the curve waits until the ball grips is for review on the "
            "chart.")
    return Verdict(status, f"With a raised cue, {'; '.join(checked)}.{tail} (C10)")


def evaluate_lag(s: Series) -> Verdict:
    cue, ball = number(s.summary.get("lag_cue_speed_m_s")), number(s.summary.get("lag_ball_speed_m_s"))
    if cue is None:
        return Verdict(NO_DATA, "In this data the cue ball never came back to the baulk cushion, so there is no "
                                "lag speed (C11).")
    ball_text = f" (the cue ball leaves at {ball:.2f} m/s)" if ball is not None else ""
    return Verdict(REVIEW, f"A lag, up to the top cushion and back to baulk, needs a cue speed of {cue:.2f} m/s"
                           f"{ball_text}. The target is a realistic speed for a fast tournament cloth, with its "
                           f"source recorded beside roll_friction (C11).")


def evaluate_screw_follow(s: Series) -> Verdict:
    rows = [r for r in s.records() if number(r.get("distance_m")) is not None]
    if not rows:
        return Verdict(NO_DATA, "The cue ball never reached the red in this data.")
    parts = []
    for speed in sorted({r["speed_m_s"] for r in rows}):
        at = sorted((r for r in rows if r["speed_m_s"] == speed), key=lambda r: r["tip_y"])
        parts.append(f"at {speed:g} m/s from {at[0]['distance_m']:+.2f} m (tip {at[0]['tip_y']:+.1f}) to "
                     f"{at[-1]['distance_m']:+.2f} m (tip {at[-1]['tip_y']:+.1f})")
    return Verdict(REVIEW, "After a full-ball hit the cue ball travels " + "; ".join(parts) +
                   ", negative being screw back. The owner judges screw and follow by feel (spec 7.12).")


def evaluate_break_sim_time(s: Series) -> Verdict:
    times = values(s.records(), "sim_us")
    if not times:
        return Verdict(NO_DATA, "There are no break-off timings.")
    ordered_times = sorted(times)
    p50 = ordered_times[len(ordered_times) // 2]
    worst = ordered_times[-1]
    text = (f"Over {len(times)} break-offs the simulation took a median of {p50:.0f} us and at most "
            f"{worst:.0f} us")
    if s.build != "release":
        return Verdict(REVIEW, f"{text}, in a {s.build or 'unknown'} build; only release timings count against "
                               f"the {BUDGET_US:.0f} us budget (spec 7.13).")
    ok = worst <= BUDGET_US
    return Verdict(MET if ok else NOT_MET,
                   f"{text}, {'within' if ok else 'over'} the {BUDGET_US:.0f} us budget (spec 7.13).")


EVALUATORS = {
    "deflection": evaluate_deflection,
    "throw": evaluate_throw,
    "cushion_rebound": evaluate_cushion_rebound,
    "cushion_side": evaluate_cushion_side,
    "squirt": evaluate_squirt,
    "swerve": evaluate_swerve,
    "lag": evaluate_lag,
    "screw_follow": evaluate_screw_follow,
    "break_sim_time": evaluate_break_sim_time,
}

DESCRIPTIONS = {
    "deflection": "How far the cue ball turns from its aim line after hitting a red, against how full the hit "
                  "is (1 is full-ball, 0 the thinnest), for a rolling cue ball and a stunned one.",
    "throw": "How far friction between the balls pushes the object ball off the line of centres, against how "
             "full the hit is, for a slow, a medium and a fast stun shot.",
    "cushion_rebound": "The angle a plain rolling ball comes off the side cushion at, against the angle it goes "
                       "in at, for four speeds, and how much of its speed it keeps.",
    "cushion_side": "The rebound angle of a ball played into the cushion at 45 degrees with check side (left), "
                    "none and running side (right).",
    "squirt": "How far side pushes the cue ball off the aim line, against how far from centre the tip strikes; "
              "past the miscue limit the strike miscues.",
    "swerve": "The cue ball's path seen from above with the cue raised and side applied, aiming up the table.",
    "lag": "Where a centre-ball cue ball stops for each cue speed, played from the baulk line to the top cushion "
           "and back.",
    "screw_follow": "How far the cue ball screws back or follows through after a full-ball hit on a red 1 m "
                    "away, against how high or low the tip strikes, at three speeds.",
    "break_sim_time": "How long the simulation of each break-off took on this PC.",
}


def evaluate(s: Series) -> Verdict:
    evaluator = EVALUATORS.get(s.name)
    if evaluator is None:
        return Verdict(REVIEW, "The report has no target for this series; it is here for review.")
    try:
        return evaluator(s)
    except (KeyError, TypeError, ValueError) as error:
        return Verdict(NO_DATA, f"The data could not be read: {error}.")


def assemble_report(series: dict[str, Series], charts: dict[str, str], problems: list[str],
                    source: str = "") -> str:
    """report.md: an overview table, then a section per chart; `charts` maps series names to PNG file names."""
    lines = ["# Calibration report", ""]
    every = ordered(series)
    if every:
        first = every[0]
        build = sorted({s.build for s in every if s.build})
        lines.append(f"Physics parameters `{first.params_hash}`, table outline `{first.table_hash}`, core "
                     f"{first.core_version or 'unknown'}, {' and '.join(build) or 'unknown'} build"
                     + (f", from `{source}`." if source else "."))
        lines.append("")
        lines.append("Each chart says what it shows and whether it meets its target from spec 7.11. \"For "
                     "review\" means the target is a model or the owner's feel rather than a number this report "
                     "can check; the calibration suite holds the exact tolerances.")
        lines += ["", "| Target | Chart | Verdict |", "| --- | --- | --- |"]
        verdicts = {s.name: evaluate(s) for s in every}
        for s in every:
            lines.append(f"| {s.target or '-'} | [{s.title}](#{anchor(s)}) | {verdicts[s.name].status} |")
        for s in every:
            lines += ["", f"## {heading(s)}", ""]
            if s.name in charts:
                lines += [f"![{s.title}]({charts[s.name]})", ""]
            else:
                lines += ["No chart: the data has no values to plot.", ""]
            description = DESCRIPTIONS.get(s.name, s.title + ".")
            lines.append(f"{description} {verdicts[s.name].sentence}")
            for note in s.notes:
                lines += ["", f"Setup: {note}"]
    else:
        lines.append("There is no calibration data to report.")
    if problems:
        lines += ["", "## Data problems", ""] + [f"- {p}" for p in problems]
    return "\n".join(lines) + "\n"


def heading(s: Series) -> str:
    return f"{s.target}: {s.title}" if s.target else s.title


def anchor(s: Series) -> str:
    """GitHub's anchor for heading(s): lowercase, punctuation dropped, spaces to hyphens."""
    text = "".join(c for c in heading(s).lower() if c.isalnum() or c in " -_")
    return text.replace(" ", "-")


# Rendering (matplotlib), kept apart so the functions above run without it.

SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_2 = "#52514e"
GRID = "#e4e3df"
BAND = "#ecebe7"
SERIES_COLOURS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6", "#e34948"]


def _axes(plt, panels: int = 1):
    figure, axes = plt.subplots(1, panels, figsize=(7.5 if panels == 1 else 10, 4.2), dpi=150, squeeze=False)
    figure.patch.set_facecolor(SURFACE)
    for ax in axes[0]:
        ax.set_facecolor(SURFACE)
        for side in ("top", "right"):
            ax.spines[side].set_visible(False)
        for side in ("left", "bottom"):
            ax.spines[side].set_color(INK_2)
        ax.tick_params(colors=INK_2, labelsize=8)
        ax.grid(True, color=GRID, linewidth=0.6)
        ax.set_axisbelow(True)
    return figure, list(axes[0])


def _label(ax, title: str, x: str, y: str):
    ax.set_title(title, color=INK, fontsize=10, loc="left")
    ax.set_xlabel(x, color=INK_2, fontsize=9)
    ax.set_ylabel(y, color=INK_2, fontsize=9)


def _line(ax, xs, ys, index: int, label: str):
    points = [(x, y) for x, y in zip(xs, ys) if number(x) is not None and number(y) is not None]
    if not points:
        return False
    ax.plot([p[0] for p in points], [p[1] for p in points], color=SERIES_COLOURS[index % len(SERIES_COLOURS)],
            linewidth=1.8, marker="o", markersize=3.5, label=label)
    return True


def _legend(ax):
    if len(ax.get_lines()) > 1:
        ax.legend(frameon=False, fontsize=8, labelcolor=INK_2)


def _by(s: Series, key: str) -> dict:
    groups: dict = {}
    for r in s.records():
        if number(r.get(key)) is not None:
            groups.setdefault(r[key], []).append(r)
    return groups


def _chart_deflection(plt, s: Series):
    figure, (left, right) = _axes(plt, 2)
    rolling = sorted(s.where(mode="rolling"), key=lambda r: r["fraction"])
    stun = sorted(s.where(mode="stun"), key=lambda r: r["fraction"])
    left.axhspan(*ROLLING_DEFLECTION_DEG, xmin=0, xmax=1, color=BAND, zorder=0)
    any_data = _line(left, [r["fraction"] for r in rolling], [r["cue_deflection_deg"] for r in rolling], 0,
                     "rolling")
    _label(left, "Rolling cue ball: deflection (C4)", "fraction of ball hit", "degrees from the aim line")
    right.axhspan(*STUN_SEPARATION_DEG, color=BAND, zorder=0)
    any_data |= _line(right, [r["fraction"] for r in stun], [r["separation_deg"] for r in stun], 1, "stun")
    _label(right, "Stun: angle between the balls (C5)", "fraction of ball hit", "degrees")
    return figure, any_data


def _chart_throw(plt, s: Series):
    figure, (ax,) = _axes(plt)
    any_data = False
    for i, (speed, rows) in enumerate(sorted(_by(s, "speed_m_s").items())):
        rows.sort(key=lambda r: r["fraction"])
        any_data |= _line(ax, [r["fraction"] for r in rows], [r["throw_deg"] for r in rows], i,
                          f"{speed:g} m/s")
    ax.axvline(0.5, color=INK_2, linewidth=0.8, linestyle=":")
    _label(ax, "Cut-induced throw, stun (C6)", "fraction of ball hit", "throw, degrees")
    _legend(ax)
    return figure, any_data


def _chart_cushion_rebound(plt, s: Series):
    figure, (left, right) = _axes(plt, 2)
    any_data = False
    for i, (speed, rows) in enumerate(sorted(_by(s, "speed_m_s").items())):
        rows.sort(key=lambda r: r["incidence_deg"])
        incidence = [r["incidence_deg"] for r in rows]
        any_data |= _line(left, incidence, [r["rebound_deg"] for r in rows], i, f"{speed:g} m/s")
        _line(right, [r["incidence_deg"] for r in rows], [r["speed_ratio"] for r in rows], i, f"{speed:g} m/s")
    left.plot([0, 80], [0, 80], color=INK_2, linewidth=0.8, linestyle=":")
    _label(left, "Rebound angle (C7); dotted: mirror", "incidence, degrees", "rebound, degrees")
    _label(right, "Speed kept", "incidence, degrees", "speed after / before")
    _legend(left)
    return figure, any_data


def _chart_cushion_side(plt, s: Series):
    figure, (ax,) = _axes(plt)
    rows = sorted(s.records(), key=lambda r: r["side"])
    any_data = _line(ax, [r["side"] for r in rows], [r["rebound_deg"] for r in rows], 0, "just after impact")
    _line(ax, [r["side"] for r in rows], [r.get("rebound_rolling_deg") for r in rows], 1, "once rolling again")
    ax.axvline(0.0, color=INK_2, linewidth=0.8, linestyle=":")
    _label(ax, "Check (left) and running (right) side at 45 degrees (C8)", "side, w_z R / v",
           "rebound, degrees")
    _legend(ax)
    return figure, any_data


def _chart_squirt(plt, s: Series):
    figure, (ax,) = _axes(plt)
    rows = sorted((r for r in s.records() if r.get("miscue") is False), key=lambda r: r["tip_x"])
    ax.axhspan(*MAX_SIDE_SQUIRT_DEG, color=BAND, zorder=0)
    squirt = [abs(r["squirt_deg"]) if number(r["squirt_deg"]) is not None else None for r in rows]
    any_data = _line(ax, [r["tip_x"] for r in rows], squirt, 0, "squirt")
    limit = number(s.summary.get("miscue_limit"))
    if limit is not None:
        ax.axvline(limit, color=INK_2, linewidth=0.8, linestyle=":")
    _label(ax, "Squirt; shaded: 1-3 degrees at maximum side (C9); dotted: miscue limit",
           "tip offset, fraction of R", "degrees off the aim line")
    return figure, any_data


def _chart_swerve(plt, s: Series):
    figure, (ax,) = _axes(plt)
    any_data = False
    for i, (name, path) in enumerate(swerve_paths(s).items()):
        any_data |= _line(ax, [p[0] for p in path["points"]], [p[1] for p in path["points"]], i, name)
    for line in ax.get_lines():
        line.set_marker("")
    _label(ax, "Swerve paths from above; aiming along +x, right is down (C10)", "x, m", "y, m")
    _legend(ax)
    return figure, any_data


def _chart_lag(plt, s: Series):
    figure, (ax,) = _axes(plt)
    rows = sorted(s.records(), key=lambda r: r["cue_speed_m_s"])
    any_data = _line(ax, [r["cue_speed_m_s"] for r in rows], [r["final_x_m"] for r in rows], 0,
                     "where it stops")
    lag = number(s.summary.get("lag_cue_speed_m_s"))
    if lag is not None:
        ax.axvline(lag, color=INK_2, linewidth=0.8, linestyle=":")
    _label(ax, "The lag (C11); dotted: the lag speed", "cue speed, m/s", "final x, m (baulk cushion at -1.78)")
    return figure, any_data


def _chart_screw_follow(plt, s: Series):
    figure, (ax,) = _axes(plt)
    any_data = False
    for i, (speed, rows) in enumerate(sorted(_by(s, "speed_m_s").items())):
        rows.sort(key=lambda r: r["tip_y"])
        any_data |= _line(ax, [r["tip_y"] for r in rows], [r["distance_m"] for r in rows], i, f"{speed:g} m/s")
    ax.axhline(0.0, color=INK_2, linewidth=0.8, linestyle=":")
    _label(ax, "Screw back (below 0) and follow through", "tip height, fraction of R", "metres after impact")
    _legend(ax)
    return figure, any_data


def _chart_break_sim_time(plt, s: Series):
    figure, (ax,) = _axes(plt)
    times = values(s.records(), "sim_us")
    if times:
        ax.hist(times, bins=30, color=SERIES_COLOURS[0], edgecolor=SURFACE, linewidth=1.0)
    ax.axvline(BUDGET_US, color=INK_2, linewidth=0.8, linestyle=":")
    _label(ax, f"Break-off simulation time, {s.build or 'unknown'} build; dotted: the 2 ms budget",
           "microseconds", "break-offs")
    return figure, bool(times)


CHARTS = {
    "deflection": _chart_deflection,
    "throw": _chart_throw,
    "cushion_rebound": _chart_cushion_rebound,
    "cushion_side": _chart_cushion_side,
    "squirt": _chart_squirt,
    "swerve": _chart_swerve,
    "lag": _chart_lag,
    "screw_follow": _chart_screw_follow,
    "break_sim_time": _chart_break_sim_time,
}


def render_charts(series: dict[str, Series], out_dir) -> tuple[dict[str, str], list[str]]:
    """Writes <name>.png for every series with something to plot; returns {name: file name} and problems."""
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    out = pathlib.Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    charts, problems = {}, []
    for s in ordered(series):
        draw = CHARTS.get(s.name)
        if draw is None:
            continue
        try:
            figure, any_data = draw(plt, s)
        except (KeyError, TypeError, ValueError) as error:
            problems.append(f"{s.name}: chart failed: {error}")
            plt.close("all")
            continue
        if any_data:
            figure.tight_layout()
            figure.savefig(out / f"{s.name}.png", facecolor=SURFACE)
            charts[s.name] = f"{s.name}.png"
        plt.close(figure)
    return charts, problems


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("data", type=pathlib.Path, help="the folder sim_cli calibrate --out wrote")
    parser.add_argument("out", type=pathlib.Path, help="where report.md and the charts go")
    args = parser.parse_args(argv)
    series, problems = load_series(args.data)
    if not series:
        print(f"no calibration series in {args.data}" + "".join(f"\n  {p}" for p in problems))
        return 1
    charts, chart_problems = render_charts(series, args.out)
    report = assemble_report(series, charts, problems + chart_problems, source=args.data.name)
    (args.out / "report.md").write_text(report, encoding="utf-8")
    for s in ordered(series):
        print(f"{evaluate(s).status:<10}  {s.target or '-':<8}  {s.title}")
    for problem in problems + chart_problems:
        print(f"problem: {problem}")
    print(f"wrote {args.out / 'report.md'} and {len(charts)} charts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
