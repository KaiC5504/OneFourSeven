# /// script
# requires-python = ">=3.10"
# dependencies = ["matplotlib"]
# ///
"""Writes the calibration report (spec 16.1): docs/calibration/report/report.md and its charts.

    uv run tools/calibration_report.py <data dir> <out dir> [--sim-cli EXE]

<data dir> is what `ofs_sim_cli calibrate --out DIR` wrote. The report also simulates the shots its
"In plain words" and "Results" sections quote, one at a time through `ofs_sim_cli simulate` (the
physics build's Release one unless --sim-cli names another), so every number in it comes from the
physics as it is now. "What changed" comes from docs/calibration/report/what_changed.json, whose
values are checked against core/data/table_params.json.

Each chart gets a sentence saying what it shows and whether it meets its target from spec 7.11.
Where the spec's target is the owner's feel rather than a number, the chart is marked for review;
the calibration suite (core/tests/calibration) holds the exact tolerances.
"""
from __future__ import annotations

import argparse
import json
import math
import pathlib
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = ROOT / "core" / "data"
FIXTURES = ROOT / "core" / "tests" / "fixtures"
DEFAULT_CHANGES = ROOT / "docs" / "calibration" / "report" / "what_changed.json"

# Report order; series the report doesn't know follow in name order.
ORDER = ["deflection", "throw", "cushion_rebound", "cushion_side", "squirt", "swerve", "lag", "screw_follow",
         "break_sim_time"]
REQUIRED = ("series", "title", "params_hash", "columns", "rows")

MET = "met"
NOT_MET = "not met"
REVIEW = "for review"
NO_DATA = "no data"

# The bands this report checks against, from docs/calibration/targets.md and the sources it names.
ROLLING_DEFLECTION_DEG = (24.0, 36.0)  # C4: the 30-degree rule's band between 1/4 and 3/4 ball
STUN_SEPARATION_DEG = (80.0, 92.0)     # C5: 90 degrees, closed a little by restitution and throw
HALF_BALL_THROW_DEG = (1.0, 6.0)       # C6: a few degrees at half-ball (Alciatore TP A.14, throw FAQ)
MAX_SIDE_SQUIRT_DEG = (1.0, 3.0)       # C9: the spec's starting range
CUSHION_TOLERANCE = (1.0, 0.02)        # C7: degrees and fraction of speed against the Mathavan solution
BUDGET_US = 2000.0                     # 7.13

R = 0.02625
PLAYING_LENGTH_M = 3.569
# The plain-words shots that would reach a cushion run on the real outline scaled up this much.
ENDLESS_SCALE = 20.0
STUN_TIP = -1.0 / 3.0
SCREW_TIP = -0.5
FOLLOW_TIP = 0.5


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
    anomalies: list[dict] = field(default_factory=list)

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
        anomalies = data.get("anomalies", [])
        if not isinstance(anomalies, list) or not all(isinstance(a, dict) for a in anomalies):
            problems.append(f"{path.name}: anomalies must be a list of objects; its shots' anomalies can't be listed")
            anomalies = []
        found[name] = Series(name=name, title=str(data["title"]), target=str(data.get("target", "")),
                             params_hash=str(data["params_hash"]), table_hash=str(data.get("table_hash", "")),
                             core_version=str(data.get("core_version", "")), build=str(data.get("build", "")),
                             columns=[str(c) for c in columns], rows=rows,
                             summary=data.get("summary") if isinstance(data.get("summary"), dict) else {},
                             notes=[str(n) for n in data.get("notes", []) if isinstance(n, str)],
                             anomalies=anomalies)
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
    return f"{min(found):.1f} to {max(found):.1f}{'' if unit == '%' else ' '}{unit}"


def combine(*statuses: str) -> str:
    if NOT_MET in statuses:
        return NOT_MET
    if all(s == NO_DATA for s in statuses):
        return NO_DATA
    if REVIEW in statuses or NO_DATA in statuses:
        return REVIEW
    return MET


def read_json(path) -> dict | None:
    try:
        data = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    return data if isinstance(data, dict) else None


def load_params(path) -> tuple[dict, list[str]]:
    data = read_json(path)
    if data is None or not isinstance(data.get("values"), dict):
        return {}, [f"{path}: no parameter values"]
    return data["values"], []


# The setup values each series' texts and chart labels quote from its summary, as calibrate writes
# them. A missing one is left out of the text and listed under Data problems.
SETUP = {
    "deflection": ("cue_ball_speed_m_s",),
    "throw": ("cue_ball_speeds_m_s",),
    "cushion_rebound": ("cue_ball_speeds_m_s",),
    "cushion_side": ("incidence_deg", "cue_ball_speed_m_s"),
    "squirt": ("cue_speed_m_s",),
    "swerve": ("cue_speed_m_s",),
}


def setup(s: Series | None, key: str):
    """A summary value: a number, or for a list a list of numbers; None when missing or not numbers."""
    value = s.summary.get(key) if s is not None else None
    if isinstance(value, list):
        found = [number(v) for v in value]
        return found if found and None not in found else None
    return number(value)


def speeds(values: list[float]) -> str:
    return listing([f"{v:g}" for v in values])


def series_problems(series: dict[str, Series]) -> list[str]:
    """Every anomaly calibrate recorded in a series' shots, and every setup value its texts lack."""
    problems = []
    for s in ordered(series):
        for a in s.anomalies:
            balls = a.get("balls")
            on = f" (balls {', '.join(str(b) for b in balls)})" if isinstance(balls, list) and balls else ""
            t = number(a.get("t"))
            problems.append(f"{s.name}: a {a.get('kind', 'unnamed')} anomaly in the shot '{a.get('shot', '?')}' at "
                            f"t = {'?' if t is None else f'{t:.6g}'} s{on}: {a.get('detail', '')}")
        for key in SETUP.get(s.name, ()):
            if setup(s, key) is None:
                problems.append(f"{s.name}: the summary has no {key}, so the texts leave it out")
    return problems


# Measured shapes.

def cut_band(s: Series) -> dict:
    """The C4/C5 ranges between a quarter and three-quarter ball."""
    mid = [r for r in s.records() if number(r.get("fraction")) is not None and 0.25 <= r["fraction"] <= 0.75]
    return {"rolling": values([r for r in mid if r.get("mode") == "rolling"], "cue_deflection_deg"),
            "stun": values([r for r in mid if r.get("mode") == "stun"], "separation_deg")}


def evaluate_deflection(s: Series) -> Verdict:
    band = cut_band(s)
    rolling, stun = band["rolling"], band["stun"]
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
    listing = ", ".join(f"{deg(r['throw_deg'])} with the cue ball at {r['speed_m_s']:g} m/s" for r in half)
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
    sentence = f"Rebound angles run {span(angles)} and the ball keeps {kept} of its speed."
    gentle = _by_speed(rows, max_speed=2.0)
    if len(gentle) > 1:
        apart = max(max(group) - min(group) for group in gentle.values())
        if apart < 0.5:
            sentence += (f" Up to 2 m/s the speeds give the same angles to within {apart:.2f} degrees, so their "
                         f"lines lie on top of each other; only the firm hit's restitution falls.")
    return Verdict(REVIEW, sentence + " The target is Mathavan, Jackson and Parkin's model, for review when there "
                                      "is no reference to compare with (C7).")


def _by_speed(rows: list[dict], max_speed: float) -> dict[float, list[float]]:
    """Rebound angles per incidence from the shots at or below max_speed."""
    groups: dict[float, list[float]] = {}
    for r in rows:
        if number(r.get("speed_m_s")) is not None and r["speed_m_s"] <= max_speed:
            groups.setdefault(r["incidence_deg"], []).append(r["rebound_deg"])
    return {k: v for k, v in groups.items() if len(v) > 1}


def cushion_side_angles(s: Series) -> dict | None:
    """Rebound angles with the most check side, none and the most running side."""
    rows = sorted((r for r in s.records()
                   if number(r.get("rebound_deg")) is not None and number(r.get("side")) is not None),
                  key=lambda r: r["side"])
    plain = [r for r in rows if r["side"] == 0]
    if len(rows) < 3 or not plain or rows[0]["side"] >= 0 or rows[-1]["side"] <= 0:
        return None
    return {"check": rows[0], "plain": plain[0], "running": rows[-1],
            "ordered": all(a["rebound_deg"] < b["rebound_deg"] for a, b in zip(rows, rows[1:]))}


def evaluate_cushion_side(s: Series) -> Verdict:
    found = cushion_side_angles(s)
    if found is None:
        return Verdict(NO_DATA, "There is not enough running and check side data (C8).")
    check, middle, running = (found[k]["rebound_deg"] for k in ("check", "plain", "running"))
    ok = running > middle > check
    rolled = [number(found[k].get("rebound_rolling_deg")) for k in ("running", "plain", "check")]
    tail = ""
    if None not in rolled:
        tail = (f" Once the ball grips the cloth again its path opens out to {rolled[0]:.1f}, {rolled[1]:.1f} "
                f"and {rolled[2]:.1f} degrees.")
    incidence = setup(s, "incidence_deg")
    into = f"Into the cushion at {incidence:g} degrees, running" if incidence is not None else "Running"
    return Verdict(MET if ok else NOT_MET,
                   f"{into} side sends the ball off at {deg(running)} just "
                   f"after the impact, no side at {deg(middle)} and check side at {deg(check)}.{tail} Running "
                   f"side should widen the angle and check side narrow it "
                   f"({'it does' if ok else 'it does not'}) (C8).")


def max_side_squirt(s: Series) -> dict | None:
    rows = [r for r in s.records() if r.get("miscue") is False and number(r.get("squirt_deg")) is not None
            and number(r.get("tip_x")) is not None]
    if not rows or max(r["tip_x"] for r in rows) <= 0:
        return None
    return max(rows, key=lambda r: r["tip_x"])


def evaluate_squirt(s: Series) -> Verdict:
    widest = max_side_squirt(s)
    if widest is None:
        return Verdict(NO_DATA, "There is no squirt data with side (C9).")
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
        x, y, t = number(r.get("x_m")), number(r.get("y_m")), number(r.get("t_s"))
        entry = paths.setdefault(str(r.get("variant")), {"elevation_deg": r.get("elevation_deg"),
                                                         "tip_x": r.get("tip_x"), "points": [], "times": []})
        if x is not None and y is not None:
            entry["points"].append((x, y))
            entry["times"].append(t)
    return paths


def heading(a: tuple[float, float], b: tuple[float, float]) -> float:
    return math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))


def turned(start: float, end: float) -> float:
    return (end - start + 180.0) % 360.0 - 180.0


def swerve_phases(path: dict, slide_accel: float) -> dict | None:
    """Where a sampled path slides (it accelerates at about mu_s g) and what it does before and after.

    Positions every 0.01 s: the second difference of a sliding ball's parabola is its constant
    acceleration, a rolling ball's is only the small rolling resistance.
    """
    times, points = path["times"], path["points"]
    if len(points) < 4 or any(t is None for t in times):
        return None
    accel = []
    for i in range(1, len(points) - 1):
        step, next_step = times[i] - times[i - 1], times[i + 1] - times[i]
        if step <= 0 or abs(step - next_step) > 1e-9:
            continue
        ax = (points[i + 1][0] - 2 * points[i][0] + points[i - 1][0]) / step ** 2
        ay = (points[i + 1][1] - 2 * points[i][1] + points[i - 1][1]) / step ** 2
        accel.append((i, math.hypot(ax, ay), math.degrees(math.atan2(ay, ax))))
    sliding = [a for a in accel if a[1] > slide_accel / 2]
    if not sliding or sliding[0][0] != 1:
        return None
    last = max(a[0] for a in sliding)
    if last + 2 >= len(points):
        return None
    # The last sliding sample straddles the moment it grips, so its second difference is a blend.
    pure = [a for a in sliding if a[0] < last] or sliding
    directions = [a[2] for a in pure]
    spread = max(abs(turned(directions[0], d)) for d in directions)
    start = heading(points[0], points[1])
    gripped = heading(points[last + 1], points[last + 2])
    end = heading(points[-2], points[-1])
    walked = [math.dist(a, b) for a, b in zip(points, points[1:])]
    third = max(1, last // 3)
    early = abs(turned(heading(points[0], points[1]), heading(points[third], points[third + 1])))
    late = abs(turned(heading(points[last - third], points[last - third + 1]), gripped))
    return {"squirt_deg": start, "slide_s": times[last + 1] - times[0], "slide_m": sum(walked[:last + 1]),
            "turn_slide_deg": turned(start, gripped), "turn_roll_deg": turned(gripped, end),
            "roll_m": sum(walked[last + 1:]), "accel_min": min(a[1] for a in pure),
            "accel_max": max(a[1] for a in pure), "accel_spread_deg": spread, "bends_late": late > early,
            "grip_point": points[last + 1]}


def raised_swerves(s: Series, slide_accel: float) -> list[tuple[str, dict, dict]]:
    found = []
    for name, path in swerve_paths(s).items():
        if not number(path["elevation_deg"]) or not number(path["tip_x"]):
            continue
        phases = swerve_phases(path, slide_accel)
        if phases is not None:
            found.append((name, path, phases))
    return found


def evaluate_swerve(s: Series, slide_accel: float | None = None) -> Verdict:
    if slide_accel is None:
        return evaluate_swerve_turns(s)
    checked, wrong, not_parabola = [], [], []
    for name, path, p in raised_swerves(s, slide_accel):
        right_way = p["turn_slide_deg"] < -0.5 if path["tip_x"] > 0 else p["turn_slide_deg"] > 0.5
        way = "right" if p["turn_slide_deg"] < 0 else "left"
        checked.append(f"{name} turns {abs(p['turn_slide_deg']):.1f} degrees {way} "
                       f"over its first {p['slide_m'] * 100:.0f} cm and {abs(p['turn_roll_deg']):.1f} more in the "
                       f"{p['roll_m']:.1f} m after it grips")
        if not right_way:
            wrong.append(name)
        if (abs(p["accel_max"] / slide_accel - 1) > 0.005 or abs(p["accel_min"] / slide_accel - 1) > 0.005
                or p["accel_spread_deg"] > 0.2):
            not_parabola.append(name)
    if not checked:
        return evaluate_swerve_turns(s)
    tail = []
    if wrong:
        tail.append(f"Against the side: {', '.join(wrong)}.")
    if not_parabola:
        tail.append(f"Not a parabola while sliding: {', '.join(not_parabola)}.")
    if not tail:
        late = all(p["bends_late"] for _, _, p in raised_swerves(s, slide_accel))
        tail.append(f"Each curves towards its side while it slides, on a parabola (a fixed pull of "
                    f"{slide_accel:.2f} m/s^2, the cloth's sliding friction)"
                    + (", bending most just before it grips," if late else ",") +
                    " and runs straight once it rolls apart from the nap's slight drift.")
    status = NOT_MET if wrong or not_parabola else MET
    return Verdict(status, f"With a raised cue, {'; '.join(checked)}. {' '.join(tail)} (C10)")


def evaluate_swerve_turns(s: Series) -> Verdict:
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
            " Each curves towards its side; without the cloth's friction the report cannot tell the slide from "
            "the roll.")
    return Verdict(status, f"With a raised cue, {'; '.join(checked)}.{tail} (C10)")


def evaluate_lag(s: Series) -> Verdict:
    cue, ball = number(s.summary.get("lag_cue_speed_m_s")), number(s.summary.get("lag_ball_speed_m_s"))
    if cue is None:
        return Verdict(NO_DATA, "In this data the cue ball never came back to the baulk cushion, so there is no "
                                "lag speed (C11).")
    ball_text = f", sending the cue ball off at {ball:.2f} m/s" if ball is not None else ""
    return Verdict(REVIEW, f"A lag, up to the top cushion and back to baulk, needs a cue speed of {cue:.2f} m/s"
                           f"{ball_text}. The target is a realistic speed for a fast tournament cloth, for review "
                           f"when the report has no lag timing to check it with (C11).")


def evaluate_screw_follow(s: Series) -> Verdict:
    rows = [r for r in s.records() if number(r.get("distance_m")) is not None]
    if not rows:
        return Verdict(NO_DATA, "The cue ball never reached the red in this data.")
    speed_key = "cue_speed_m_s" if "cue_speed_m_s" in s.columns else "speed_m_s"
    parts = []
    for speed in sorted({r[speed_key] for r in rows}):
        at = sorted((r for r in rows if r[speed_key] == speed), key=lambda r: r["tip_y"])
        parts.append(f"at a cue speed of {speed:g} m/s from {at[0]['distance_m']:+.2f} m (tip "
                     f"{at[0]['tip_y']:+.1f}) to {at[-1]['distance_m']:+.2f} m (tip {at[-1]['tip_y']:+.1f})")
    sentence = "After a full-ball hit the cue ball travels " + "; ".join(parts) + ", negative being screw back."
    missing = [r for r in s.records() if number(r.get("distance_m")) is None]
    for r in missing:
        stopped = number(r.get("stopped_short_m"))
        where = f", stopping {stopped:.2f} m from where it started" if stopped is not None else ""
        sentence += (f" At a cue speed of {r[speed_key]:g} m/s with the tip {r['tip_y']:+.1f} the cue ball never "
                     f"reaches the red{where}: the screw slows it too much, so the chart shows that point as "
                     f"missing.")
    return Verdict(REVIEW, sentence + " The owner judges screw and follow by feel (spec 7.12).")


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


def _at(value, template) -> str:
    """template (a format string or a function) filled with a setup value; "" without one."""
    if value is None:
        return ""
    return template(value) if callable(template) else template.format(value)


DESCRIPTIONS = {
    "deflection": lambda s: "How far the cue ball turns from its aim line after hitting a red, against how full the "
                            "hit is (1 is full-ball, 0 the thinnest), for a rolling cue ball and a stunned one"
                            + _at(setup(s, "cue_ball_speed_m_s"), ", the cue ball at {:g} m/s") + ".",
    "throw": lambda s: "How far friction between the balls pushes the object ball off the line of centres, against "
                       "how full the hit is, for a stunned cue ball"
                       + _at(setup(s, "cue_ball_speeds_m_s"), lambda v: f" arriving at {speeds(v)} m/s") + ".",
    "cushion_rebound": lambda s: "The angle a plain rolling cue ball comes off the side cushion at, against the angle "
                                 "it goes in at"
                                 + _at(setup(s, "cue_ball_speeds_m_s"), lambda v: f", at {speeds(v)} m/s")
                                 + ", and how much of its speed it keeps.",
    "cushion_side": lambda s: "The rebound angle of a ball rolling into the cushion"
                              + _at(setup(s, "incidence_deg"), " at {:g} degrees")
                              + _at(setup(s, "cue_ball_speed_m_s"), " at {:g} m/s")
                              + " with check side (left), none and running side (right). Side 1 is as much as a "
                                "tip 0.4 R off centre gives.",
    "squirt": lambda s: "How far side pushes the cue ball off the aim line, against how far from centre the tip "
                        "strikes (a level cue" + _at(setup(s, "cue_speed_m_s"), " at a cue speed of {:g} m/s")
                        + "); past the miscue limit the strike miscues.",
    "swerve": lambda s: "The cue ball's path seen from above with the cue raised and side applied, aiming up the "
                        "table" + _at(setup(s, "cue_speed_m_s"), " at a cue speed of {:g} m/s")
                        + "; the right-hand panel is the first 40 cm, with a dot where each starts to roll.",
    "lag": lambda s: "Where a centre-ball cue ball stops for each cue speed, played from the baulk line to the top "
                     "cushion and back.",
    "screw_follow": lambda s: "How far the cue ball screws back or follows through after a full-ball hit on a red "
                              "1 m away, against how high or low the tip strikes, at three cue speeds.",
    "break_sim_time": lambda s: "How long the simulation of each break-off took on this PC.",
}


def describe(s: Series) -> str:
    """What the chart shows, with the setup its series' summary records."""
    text = DESCRIPTIONS.get(s.name)
    return text(s) if text else s.title + "."


def evaluate(s: Series, context: dict | None = None) -> Verdict:
    """The chart's verdict; context may hold the cloth's sliding pull and the results rows (by target)."""
    context = context or {}
    evaluator = EVALUATORS.get(s.name)
    if evaluator is None:
        return Verdict(REVIEW, "The report has no target for this series; it is here for review.")
    try:
        verdict = evaluate_swerve(s, context.get("slide_accel")) if s.name == "swerve" else evaluator(s)
    except (KeyError, TypeError, ValueError) as error:
        return Verdict(NO_DATA, f"The data could not be read: {error}.")
    if s.name == "swerve":
        raised = []
        for name, path in swerve_paths(s).items():
            used, asked = context.get("swerve_elevation", {}).get(name), number(path["elevation_deg"])
            if used is not None and asked is not None and used > asked + 0.05:
                turn = path_turn(path["points"])
                curve = f", so it too curves, {abs(turn):.1f} degrees." if turn else "."
                raised.append(f"The {name} shot's cue has to rise to {used:.1f} degrees to clear the cushion rail "
                              f"behind the cue ball{curve}")
        if raised:
            body = verdict.sentence.removesuffix(" (C10)")
            verdict = Verdict(verdict.status, f"{body} {' '.join(raised)} (C10)")
    row = context.get("results", {}).get(s.target) if s.name in ("cushion_rebound", "lag") else None
    if row and row[4] in (MET, NOT_MET) and verdict.status != NOT_MET:
        sentence = re.sub(r" The target is .*$", "", verdict.sentence)
        verdict = Verdict(row[4], f"{sentence} Against the target ({s.target}): {row[2]}; the target is {row[3]}.")
    return verdict


# The report's own shots, through `ofs_sim_cli simulate`.

class SimError(Exception):
    pass


class Sim:
    """Runs one shot at a time through `ofs_sim_cli simulate` and returns its record (record_json)."""

    def __init__(self, exe, workdir, params):
        self.exe = pathlib.Path(exe)
        self.workdir = pathlib.Path(workdir)
        self.params = pathlib.Path(params).resolve()
        self.params_hashes: set[str] = set()
        self.anomalies: list[str] = []
        self.runs = 0

    def run(self, balls: list[dict], strike: dict | None, table) -> dict:
        """`strike` None lets the balls move as given; `table` is the outline file."""
        position = {"name": "report", "cue_ball": 0, "start": {"balls": balls}}
        if strike is not None:
            position["strike"] = {"aim_rad": 0.0, "tip_x": 0.0, "tip_y": 0.0, "elevation_rad": 0.0, **strike}
        source, out = self.workdir / "position.json", self.workdir / "record.json"
        source.write_text(json.dumps(position), encoding="utf-8")
        out.unlink(missing_ok=True)
        args = [str(self.exe), "simulate", str(source), "--out", str(out), "--params", str(self.params),
                "--table", str(pathlib.Path(table).resolve())]
        if strike is None:
            args.append("--no-strike")
        try:
            done = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace",
                                  timeout=120)
        except (OSError, subprocess.TimeoutExpired) as error:
            raise SimError(f"ofs_sim_cli simulate did not run: {error}") from error
        self.runs += 1
        if not out.exists():
            said = (done.stderr.strip() or done.stdout.strip())[-500:]
            raise SimError(f"ofs_sim_cli simulate exited {done.returncode} without a record "
                           f"({json.dumps(position)}): {said}")
        found = re.search(r"^params\s+(\S+),", done.stdout, re.MULTILINE)
        if found:
            self.params_hashes.add(found.group(1))
        record = json.loads(out.read_text(encoding="utf-8"))
        for anomaly in record.get("anomalies", []):
            self.anomalies.append(f"{anomaly.get('kind')} at {anomaly.get('t')} s in {json.dumps(position)}")
        return record


def ball(kind: str, index: int, x: float, y: float = 0.0, v=None, w=None, motion=None) -> dict:
    b = {"ball": kind, "index": index, "r": [x, y]}
    if v is not None:
        b.update({"v": list(v), "w": list(w), "motion": motion})
    return b


def moving_cue_ball(x: float, y: float, speed: float, rolling: bool) -> dict:
    """Heading +x at `speed`, rolling or stunned (sliding without spin)."""
    spin = [0.0, speed / R, 0.0] if rolling else [0.0, 0.0, 0.0]
    return ball("cue", 0, x, y, [speed, 0.0], spin, "rolling" if rolling else "sliding")


def first_event(record: dict, kind: str, index: int, after: float = -math.inf) -> dict | None:
    for e in record["events"]:
        if e["kind"] == kind and index in (e["a"], e["b"]) and e["t"] > after:
            return e
    return None


def state_from(record: dict, index: int, t: float) -> dict:
    """The ball's state at the start of its first segment from time t on (an event starts one)."""
    for segment in record["segments"][index]:
        if segment["t0"] >= t:
            return segment["start"]
    return record["final"]["balls"][index]


def rolling_from(record: dict, index: int, t: float) -> dict | None:
    for segment in record["segments"][index]:
        if segment["t0"] >= t and segment["start"]["motion"] == "rolling":
            return segment["start"]
    return None


def speed_of(vector) -> float:
    return math.hypot(vector[0], vector[1])


def signed_angle(a, b) -> float:
    """From a to b, anticlockwise positive, in degrees."""
    return math.degrees(math.atan2(a[0] * b[1] - a[1] * b[0], a[0] * b[0] + a[1] * b[1]))


def run_after_contact(record: dict) -> float | None:
    """How far the cue ball goes along the aim after its first contact with a ball; None if it never hits one."""
    hit = first_event(record, "ball_ball", 0)
    if hit is None:
        return None
    return record["final"]["balls"][0]["r"][0] - state_from(record, 0, hit["t"])["r"][0]


def endless_table(table_file, out_file) -> None:
    """The real outline scaled up ENDLESS_SCALE times: every shot here stops long before a cushion."""
    shape = json.loads(pathlib.Path(table_file).read_text(encoding="utf-8"))
    k = ENDLESS_SCALE
    for pocket in shape["pockets"]:
        pocket["drop_centre"] = [c * k for c in pocket["drop_centre"]]
        pocket["drop_radius"] *= k
    for line in shape["lines"]:
        line["p1"] = [c * k for c in line["p1"]]
        line["p2"] = [c * k for c in line["p2"]]
    for arc in shape["arcs"]:
        arc["centre"] = [c * k for c in arc["centre"]]
        arc["radius"] *= k
    pathlib.Path(out_file).write_text(json.dumps(shape), encoding="utf-8")


class Shots:
    """The shots behind "In plain words" and "Results", most on an endless table, a few on the real one."""

    def __init__(self, sim: Sim, endless, table_file, lag_summary: dict):
        self.sim = sim
        self.endless = endless
        self.table_file = table_file
        self.lag_summary = lag_summary
        self.per_cue: dict[float, float] = {}

    def ball_per_cue(self, tip_y: float) -> float:
        """Cue-ball speed per unit cue speed at this tip height (the strike is linear in cue speed)."""
        if tip_y not in self.per_cue:
            record = self.sim.run([ball("cue", 0, 0.0)], {"speed_m_s": 1.0, "tip_y": tip_y}, self.endless)
            self.per_cue[tip_y] = speed_of(record["strike"]["v"])
        return self.per_cue[tip_y]

    def struck(self, tip_y: float, ball_speed: float, red_at: float | None = None) -> dict:
        """A level strike that sends the cue ball off at ball_speed, at a red red_at metres away if given."""
        balls = [ball("cue", 0, 0.0)] + ([ball("red", 0, red_at)] if red_at is not None else [])
        cue_speed = ball_speed / self.ball_per_cue(tip_y)
        return self.sim.run(balls, {"speed_m_s": cue_speed, "tip_y": tip_y}, self.endless)

    def pace(self) -> dict:
        out = {}
        for speed in (1.0, 2.0, 3.0):
            record = self.struck(0.0, speed)
            end = record["final"]["balls"][0]["r"]
            out[speed] = math.hypot(end[0], end[1])
        return out

    def strike_ratios(self) -> dict:
        return {"centre": self.ball_per_cue(0.0), "screw": self.ball_per_cue(SCREW_TIP)}

    def slide_to_roll(self) -> dict:
        record = self.sim.run([moving_cue_ball(0.0, 0.0, 2.0, False)], None, self.endless)
        grip = next(e for e in record["events"] if e["kind"] == "transition" and e["a"] == 0 and e["to"] == "rolling")
        state = state_from(record, 0, grip["t"])
        return {"v0": 2.0, "x": state["r"][0], "v": speed_of(state["v"])}

    def full_ball(self, rolling: bool) -> dict:
        """A cue ball at 2 m/s 1 mm short of a full-ball contact with a red."""
        red = 0.3
        record = self.sim.run([moving_cue_ball(red - 2 * R - 1e-3, 0.0, 2.0, rolling), ball("red", 0, red)],
                              None, self.endless)
        hit = first_event(record, "ball_ball", 0)
        after = state_from(record, 0, hit["t"])
        out = {"v0": 2.0, "ratio": speed_of(after["v"]) / 2.0,
               "creep_m": record["final"]["balls"][0]["r"][0] - after["r"][0]}
        if rolling:
            again = rolling_from(record, 0, hit["t"])
            out["ratio"] = speed_of(again["v"]) / 2.0
        return out

    def throw(self, speed: float, rolling: bool) -> float:
        """Degrees the object ball leaves off the line of centres at a half-ball hit; positive is fuller."""
        red = (0.3, 0.0)
        centres = (math.cos(math.radians(30.0)), math.sin(math.radians(30.0)))
        start = (red[0] - centres[0] * 2 * R - 1e-3, red[1] - centres[1] * 2 * R)
        record = self.sim.run([moving_cue_ball(start[0], start[1], speed, rolling), ball("red", 0, *red)], None,
                              self.endless)
        hit = first_event(record, "ball_ball", 0)
        cue, obj = state_from(record, 0, hit["t"]), state_from(record, 1, hit["t"])
        line = (obj["r"][0] - cue["r"][0], obj["r"][1] - cue["r"][1])
        return -signed_angle(line, obj["v"])

    def throws(self, published: dict) -> dict:
        speeds = published["speeds_m_s"]
        out = {"stun": {name: (speed, self.throw(speed, False)) for name, speed in speeds.items()}}
        out["rolling"] = {"medium": (speeds["medium"], self.throw(speeds["medium"], True))}
        return out

    def stun_speed(self, red_at: float) -> dict:
        """The cue-ball speed at which a strike a third below centre arrives with no spin (a stun)."""
        lo, hi = 0.2, 8.0
        for _ in range(24):
            mid = (lo + hi) / 2
            record = self.struck(STUN_TIP, mid, red_at)
            hit = first_event(record, "ball_ball", 0)
            topspin = hit is None or state_from(record, 0, hit["t"])["w"][1] > 0
            lo, hi = (mid, hi) if topspin else (lo, mid)
        speed = hi
        return {"speed": speed, "creep_m": run_after_contact(self.struck(STUN_TIP, speed, red_at)),
                "harder_m": run_after_contact(self.struck(STUN_TIP, 1.1 * speed, red_at)),
                "softer_m": run_after_contact(self.struck(STUN_TIP, 0.9 * speed, red_at))}

    def screw(self) -> dict:
        return {(gap, speed): run_after_contact(self.struck(SCREW_TIP, speed, gap))
                for gap in (0.5, 1.0) for speed in (2.0, 3.0, 4.0)}

    def follow(self) -> dict:
        return {speed: run_after_contact(self.struck(FOLLOW_TIP, speed, 1.0)) for speed in (2.0, 3.0)}

    def run_through(self) -> dict:
        record = self.struck(0.0, 3.0, 0.5)
        hit = first_event(record, "ball_ball", 0)
        before = [s for s in record["segments"][0] if hit is not None and s["t0"] < hit["t"]]
        return {"gap": 0.5, "speed": 3.0, "distance_m": run_after_contact(record),
                "sliding_at_contact": bool(before) and before[-1]["start"]["motion"] == "sliding"}

    def screw_chart(self) -> list[list]:
        """Full-ball hits on a red 1 m away at three cue speeds and eleven tip heights."""
        rows = []
        for cue_speed in (1.0, 2.0, 3.0):
            for i in range(-5, 6):
                tip = round(0.1 * i, 10)
                record = self.sim.run([ball("cue", 0, 0.0), ball("red", 0, 1.0)],
                                      {"speed_m_s": cue_speed, "tip_y": tip}, self.endless)
                distance = run_after_contact(record)
                stopped = None if distance is not None else record["final"]["balls"][0]["r"][0]
                rows.append([cue_speed, tip, speed_of(record["strike"]["v"]), distance, stopped])
        return rows

    def swerve_elevations(self, swerve: Series | None) -> dict:
        """The elevation each swerve path's strike really used: a cue near a cushion is raised to clear its rail."""
        out = {}
        for name, path in swerve_paths(swerve).items() if swerve else []:
            asked, tip = number(path["elevation_deg"]), number(path["tip_x"])
            if asked is None or tip is None or not path["points"]:
                continue
            x, y = path["points"][0]
            strike = {"speed_m_s": 1.0, "tip_x": tip, "elevation_rad": math.radians(asked)}
            record = self.sim.run([ball("cue", 0, x, y)], strike, self.table_file)
            out[name] = math.degrees(record["strike"]["elevation_used_rad"])
        return out

    def lag_return(self) -> dict:
        """The lag on the real table at calibrate's lag speed: when it meets the top and then the baulk cushion."""
        cue_speed = number(self.lag_summary.get("lag_cue_speed_m_s"))
        start = number(self.lag_summary.get("start_x_m"))
        if cue_speed is None or start is None:
            raise SimError("the calibrate data has no lag speed")
        shape = json.loads(pathlib.Path(self.table_file).read_text(encoding="utf-8"))
        noses = [(i, line["normal"]) for i, line in enumerate(shape["lines"]) if line.get("kind") == "nose"]
        top = {i for i, n in noses if n[0] < -0.99}
        baulk = {i for i, n in noses if n[0] > 0.99}
        record = self.sim.run([ball("cue", 0, start)], {"speed_m_s": cue_speed}, self.table_file)
        hits = [e for e in record["events"] if e["kind"] == "ball_line" and e["a"] == 0]
        at_top = next((e for e in hits if e["body"] in top), None)
        at_baulk = next((e for e in hits if at_top and e["body"] in baulk and e["t"] > at_top["t"]), None)
        if at_top is None or at_baulk is None:
            raise SimError(f"the lag at {cue_speed} m/s did not reach the top cushion and then baulk")
        length = shape.get("dimensions", {}).get("playing_length_m", PLAYING_LENGTH_M)
        return {"t_top": at_top["t"], "t_baulk": at_baulk["t"], "back_m": length - 2 * R, "start_x": start,
                "playing_length_m": length}


def measure(sim: Sim, endless, table_file, series: dict[str, Series],
            published_throw: dict | None) -> tuple[dict, list[str]]:
    """Every shot group; a group that fails is left out and named in the problems."""
    lag_summary = series["lag"].summary if "lag" in series else {}
    shots = Shots(sim, endless, table_file, lag_summary)
    groups = {
        "ratios": shots.strike_ratios,
        "pace": shots.pace,
        "c1": shots.slide_to_roll,
        "c2": lambda: shots.full_ball(False),
        "c3": lambda: shots.full_ball(True),
        "stun": lambda: {gap: shots.stun_speed(gap) for gap in (0.5, 1.0)},
        "screw": shots.screw,
        "follow": shots.follow,
        "run_through": shots.run_through,
        "screw_chart": shots.screw_chart,
        "lag": shots.lag_return,
        "swerve_elevation": lambda: shots.swerve_elevations(series.get("swerve")),
    }
    if published_throw is not None:
        groups["throw"] = lambda: shots.throws(published_throw)
    m, problems = {}, []
    for name, group in groups.items():
        try:
            m[name] = group()
        except (SimError, KeyError, IndexError, TypeError, ValueError, StopIteration, OSError) as error:
            problems.append(f"shots '{name}' failed: {type(error).__name__}: {error}")
    return m, problems


# Text.

def length_of(m: dict) -> float:
    return m.get("lag", {}).get("playing_length_m", PLAYING_LENGTH_M)


def short_distance(metres: float) -> str:
    size = abs(metres)
    if size < 0.0995:
        return f"{size * 100:.1f} cm"
    if size < 0.995:
        return f"{size * 100:.0f} cm"
    return f"{size:.1f} m"


def table_share(metres: float, length: float) -> str:
    if metres <= length:
        return f"{metres / length * 100:.0f}% of the table's length"
    return f"{metres / length:.1f} table lengths"


def at_speeds(found: dict, phrase) -> str:
    """'at 2 m/s it comes back 30 cm, at 3 m/s it ...' over found {speed: metres}."""
    return listing([f"at {speed:g} m/s it {phrase(found[speed])}" for speed in sorted(found)])


def travel(metres: float, does: str, do: str, length: float) -> str:
    """'runs 2.7 m', or past the table's length 'would run 6.6 m on an endless table'."""
    if abs(metres) > length:
        return f"would {do} {abs(metres):.1f} m on an endless table"
    return f"{does} {short_distance(metres)}"


def after_contact_phrase(metres: float | None, length: float) -> str:
    if metres is None:
        return "never reaches the red"
    if metres < 0:
        return travel(metres, "comes back", "come back", length)
    return travel(metres, "runs on", "run on", length)


def listing(items: list[str]) -> str:
    return items[0] if len(items) == 1 else ", ".join(items[:-1]) + " and " + items[-1]


def percent(fraction: float) -> str:
    return f"{fraction * 100:.0f}%"


def plain_words(m: dict, series: dict[str, Series], params: dict, fixtures: dict) -> list[str]:
    """The "In plain words" bullets, every number from the shots and the calibrate data."""
    length = length_of(m)
    lines = []
    ratios = m.get("ratios")
    limit = params.get("miscue_limit")
    intro = ("Speeds are the cue ball's speed as it leaves the tip unless a figure says cue speed (how fast the "
             "cue is moving).")
    if ratios:
        intro += (f" A centre-ball hit sends the cue ball off {ratios['centre']:.2f} times as fast as the cue; "
                  f"with the tip 0.5 off centre (above, below or to one side) it leaves only "
                  f"{percent(ratios['screw'] / ratios['centre'])} as fast as that for the same cue speed.")
    intro += (" Tip positions are fractions of the ball's radius from its centre: 0.5 is half-way to the edge"
              + (f", just inside the miscue limit of {limit:g}" if limit is not None else "") +
              ". Distances between two balls are from centre to centre, and the table is "
              f"{length:.2f} m long.")
    lines.append(intro)
    bullets = []

    pace = m.get("pace")
    if pace:
        clauses = [f"at {speed:g} m/s it {travel(d, 'runs', 'run', length)} ({table_share(d, length)})"
                   for speed, d in sorted(pace.items())]
        bullets.append(f"**Pace.** A centre-ball cue ball left to run: {'; '.join(clauses)}.")

    c2, stun, rt = m.get("c2"), m.get("stun"), m.get("run_through")
    if c2 and stun:
        near, far = stun[0.5], stun[1.0]
        more = "harder" if far["speed"] > near["speed"] else "softer"
        text = (f"**Stun.** A cue ball arriving full-ball at 2 m/s with no spin stops almost dead: it keeps "
                f"{c2['ratio'] * 100:.1f}% of its speed and creeps on {short_distance(c2['creep_m'])}. Striking a "
                f"third of the way from centre to the bottom of the cue ball (tip -0.33), it stuns from 50 cm when "
                f"it leaves at {near['speed']:.2f} m/s (it then creeps on {short_distance(near['creep_m'])}) and "
                f"from 1 m at {far['speed']:.2f} m/s: from further away you hit {more}, because the cloth rubs the "
                f"backspin off on the way. From 50 cm, hit a tenth harder ({1.1 * near['speed']:.2f} m/s) and it "
                f"{after_contact_phrase(near['harder_m'], length)}; a tenth softer ({0.9 * near['speed']:.2f} m/s) "
                f"and it {after_contact_phrase(near['softer_m'], length)}.")
        if rt and rt["distance_m"] is not None and rt["distance_m"] > 0:
            how = "arrives still sliding and " if rt.get("sliding_at_contact") else ""
            text += (f" Centre ball from 50 cm at {rt['speed']:g} m/s is a stun-run-through: the cue ball {how}"
                     f"{after_contact_phrase(rt['distance_m'], length)}.")
        bullets.append(text)

    screw = m.get("screw")
    if screw:
        def at(gap):
            return at_speeds({speed: d for (g, speed), d in screw.items() if g == gap},
                             lambda d: after_contact_phrase(d, length))
        ran_on = [speed for (gap, speed), d in sorted(screw.items()) if gap == 1.0 and d is not None and d > 0]
        note = (f" At {ran_on[-1]:g} m/s from 1 m the backspin is gone before the cue ball arrives, so it runs on."
                if ran_on else "")
        bullets.append(f"**Screw needs pace.** The cloth takes backspin off quickly. Striking 0.5 below centre, "
                       f"from 50 cm {at(0.5)}; from 1 m {at(1.0)}.{note}")

    follow, c3 = m.get("follow"), m.get("c3")
    if follow:
        reached = {speed: d for speed, d in follow.items() if d is not None}
        text = (f"**Follow.** Striking 0.5 above centre, from 1 m "
                f"{at_speeds(reached, lambda d: travel(d, 'follows through', 'follow through', length))}.")
        if c3:
            text += (f" A cue ball already rolling when it hits a ball full keeps {percent(c3['ratio'])} of its "
                     f"speed, close to the textbook 2/7 ({percent(2 / 7)}).")
        bullets.append(text)

    deflection = series.get("deflection")
    if deflection:
        band = cut_band(deflection)
        if band["rolling"] and band["stun"]:
            arriving = _at(setup(deflection, "cue_ball_speed_m_s"), " arriving at {:g} m/s and")
            bullets.append(f"**Cut angles.** A rolling cue ball{arriving} cut between a quarter and "
                           f"three-quarter ball comes off {min(band['rolling']):.0f}-{max(band['rolling']):.0f} "
                           f"degrees from the "
                           f"aim line (the 30-degree rule). Stunned, the two balls part at "
                           f"{min(band['stun']):.0f}-{max(band['stun']):.0f} degrees, a little under 90 because "
                           f"the balls are not perfectly elastic and the friction between them drags the object "
                           f"ball along.")

    throw = m.get("throw")
    if throw:
        slow, med, fast = (throw["stun"][k] for k in ("slow", "medium", "fast"))
        rolled = throw["rolling"]["medium"]
        bullets.append(
            f"**Throw.** On a half-ball cut, friction between the balls drags the object ball a little the way "
            f"the cue ball is going, so it goes fuller than the line through the two centres: "
            f"{slow[1]:.1f} degrees with a stunned cue ball arriving at {slow[0]:.2f} m/s, {med[1]:.1f} at "
            f"{med[0]:.2f} m/s and {fast[1]:.1f} at {fast[0]:.2f} m/s. Over 1 m that is "
            f"{short_distance(math.tan(math.radians(slow[1])))} on the slow cut and "
            f"{short_distance(math.tan(math.radians(fast[1])))} on the firm one, so aim that much thinner. A "
            f"rolling cue ball throws less: {rolled[1]:.1f} degrees at {rolled[0]:.2f} m/s.")

    squirt = series.get("squirt")
    widest = max_side_squirt(squirt) if squirt else None
    if widest and widest["squirt_deg"]:
        angle = abs(widest["squirt_deg"])
        bullets.append(f"**Side and squirt.** Side pushes the cue ball off the aim line, away from the side used: "
                       f"with the most side before a miscue (tip {widest['tip_x']:.2f} off centre) it leaves "
                       f"{angle:.1f} degrees off, {short_distance(math.tan(math.radians(angle)))} over 1 m.")

    swerve = series.get("swerve")
    slide_accel = sliding_pull(params)
    if swerve and slide_accel:
        found = raised_swerves(swerve, slide_accel)
        pick = next(((n, p, f) for n, p, f in found if number(p["elevation_deg"]) == 30.0 and p["tip_x"] > 0),
                    found[0] if found else None)
        if pick:
            _, path, p = pick
            side = "right" if path["tip_x"] > 0 else "left"
            other = "left" if side == "right" else "right"
            late = "bending most just before it grips the cloth" if p["bends_late"] else "bending evenly"
            cue_speed = _at(setup(swerve, "cue_speed_m_s"), ", at a cue speed of {:g} m/s")
            bullets.append(
                f"**Swerve.** With the cue raised and side, the cue ball first squirts away from the side, then "
                f"curves back towards it from the moment it is struck while it slides, {late}; once it rolls it "
                f"runs straight. Raised {path['elevation_deg']:g} degrees with the tip {abs(path['tip_x']):.1f} "
                f"{side} of centre{cue_speed}, it squirts {abs(p['squirt_deg']):.1f} degrees "
                f"{other}, turns {abs(p['turn_slide_deg']):.1f} degrees {side} over the first "
                f"{short_distance(p['slide_m'])} and then holds its line: {abs(p['turn_roll_deg']):.1f} degrees "
                f"more over the next {p['roll_m']:.1f} m, the nap's drift on a slowing ball.")

    rebound, side = series.get("cushion_rebound"), series.get("cushion_side")
    if rebound and side:
        straight = {r["speed_m_s"]: r["speed_ratio"] for r in rebound.records()
                    if r.get("incidence_deg") == 0 and number(r.get("speed_ratio")) is not None}
        angles = cushion_side_angles(side)
        measured = (fixtures.get("cushion") or {}).get("published", {})
        published = measured.get("perpendicular_rolling_rebound_ratio")
        below = (measured.get("perpendicular_rolling_speed_range_m_s") or [None, None])[1]
        if 1.0 in straight and angles:
            firm = max(straight)
            text = (f"**Cushions.** A rolling ball arriving straight into a cushion at 1 m/s comes off with "
                    f"{percent(straight[1.0])} of its speed; firm hits lose more, {percent(straight[firm])} at "
                    f"{firm:g} m/s.")
            if published:
                text += (f" (The one set of measurements on a snooker table, by Mathavan and colleagues, averaged "
                         f"{percent(published)}" + (f" below {below:g} m/s" if below else "") + ", where their "
                         f"own cushion model, which the game uses, gives {percent(straight[1.0])}.)")
            p, r, c = angles["plain"], angles["running"], angles["check"]
            into = _at(setup(side, "incidence_deg"), " at {:g} degrees")
            text += (f" Rolling into a side cushion{into} with no side, the ball leaves at "
                     f"{p['rebound_deg']:.1f} degrees just after the impact, then bends out to "
                     f"{p['rebound_rolling_deg']:.1f} as it grips the cloth again: that is the line you see. "
                     f"Running side widens both ({r['rebound_deg']:.1f} and {r['rebound_rolling_deg']:.1f} "
                     f"degrees, with as much side as a tip 0.4 off centre gives) and check side narrows them "
                     f"({c['rebound_deg']:.1f} and {c['rebound_rolling_deg']:.1f}).")
            bullets.append(text)

    lag_series, lag = series.get("lag"), m.get("lag")
    if lag_series and number(lag_series.summary.get("lag_ball_speed_m_s")) is not None:
        text = (f"**Cloth speed.** A lag (from the baulk line up to the top cushion and back down to the baulk "
                f"cushion) needs the cue ball to leave at {lag_series.summary['lag_ball_speed_m_s']:.2f} m/s, a "
                f"cue speed of {lag_series.summary['lag_cue_speed_m_s']:.2f} m/s")
        if lag:
            text += (f"; coming back down from the top cushion to the baulk cushion takes "
                     f"{lag['t_baulk'] - lag['t_top']:.1f} seconds")
        bullets.append(text + ".")

    bullets.append("**Still starting guesses, for you to judge by feel in M2:** how lively the pocket jaws are "
                   "(firm balls rattling, slow ones dropping), the nap drift on slow balls with and against the "
                   "nap, what a miscue does, how much a very hard ball dies on a cushion, and the cloth's overall "
                   "pace (the one snooker measurement found a slower cloth; tournament tables are heated and "
                   "faster, so this sits between the two).")
    return lines + [""] + [f"- {b}" for b in bullets]


def sliding_pull(params: dict) -> float | None:
    mu, g = number(params.get("slide_friction")), number(params.get("gravity_m_s2"))
    return mu * g if mu is not None and g is not None else None


def tp_b6_lag_speed(mu_r: float, e_c: float, mu_s: float, g: float, d_up: float, d_back: float) -> float:
    """TP B.6's lag model, as test_c11_lag.cpp solves it: the cue-ball speed that just comes back to baulk."""
    k = (25.0 + 24.0 * mu_r / mu_s) / 49.0
    v_rebound_sq = 2.0 * mu_r * g * d_back / k
    return math.sqrt((v_rebound_sq / (e_c * e_c) + 2.0 * mu_r * g * d_up) / k)


def mark(ok: bool) -> str:
    return MET if ok else NOT_MET


def results_rows(m: dict, series: dict[str, Series], params: dict, fixtures: dict) -> list[list[str]]:
    """Rows of the results table: target, what, measured, target value, verdict."""
    rows = []
    e, mu_s, g = (number(params.get(k)) for k in ("ball_restitution", "slide_friction", "gravity_m_s2"))
    missing = "not measured (see Data problems)"

    c1 = m.get("c1")
    if c1 and mu_s and g:
        v0 = c1["v0"]
        x, v = 12 * v0 ** 2 / (49 * mu_s * g), 5 / 7 * v0
        worst = max(abs(c1["x"] / x - 1), abs(c1["v"] / v - 1))
        rows.append(["C1", "Centre ball, no spin: slides, then rolls",
                     f"at {v0:g} m/s rolls after {c1['x']:.4f} m at {c1['v']:.4f} m/s",
                     f"12 v^2 / (49 mu_s g) = {x:.4f} m at 5/7 v = {v:.4f} m/s (TP 4.1), 1e-6 relative",
                     mark(worst <= 1e-6)])
    else:
        rows.append(["C1", "Centre ball, no spin: slides, then rolls", missing, "", NO_DATA])

    c2 = m.get("c2")
    if c2 and e is not None:
        want = (1 - e) / 2
        rows.append(["C2", "Full-ball stun", f"cue ball keeps {c2['ratio'] * 100:.2f}% of its speed",
                     f"(1-e)/2 = {want * 100:.2f}%, within 0.2% of the speed", mark(abs(c2["ratio"] - want) <= 2e-3)])
    else:
        rows.append(["C2", "Full-ball stun", missing, "", NO_DATA])

    c3 = m.get("c3")
    if c3 and e is not None and all(number(params.get(k)) is not None for k in
                                    ("ball_friction_a", "ball_friction_b", "ball_friction_c")):
        want = 5 / 7 * (1 - e) / 2 + 2 / 7
        v0 = c3["v0"]
        mu = params["ball_friction_a"] + params["ball_friction_b"] * math.exp(-params["ball_friction_c"] * v0)
        slack = 1.5 * 5 / 7 * mu * (1 + e) / 2
        rows.append(["C3", "Full-ball, rolling", f"follows at {c3['ratio']:.3f} of its speed",
                     f"(5/7)(1-e)/2 + 2/7 = {want:.3f}, less up to {slack:.3f} for topspin friction (TP A.14)",
                     mark(want - slack <= c3["ratio"] <= want + 2e-3)])
    else:
        rows.append(["C3", "Full-ball, rolling", missing, "", NO_DATA])

    deflection = series.get("deflection")
    band = cut_band(deflection) if deflection else {"rolling": [], "stun": []}
    at_speed = _at(setup(deflection, "cue_ball_speed_m_s"), ", cue ball at {:g} m/s")
    if band["rolling"]:
        rows.append(["C4", f"Rolling cut, 1/4 to 3/4 ball{at_speed}",
                     f"{min(band['rolling']):.1f}-{max(band['rolling']):.1f} deg off the aim line",
                     f"30-degree rule, inside {ROLLING_DEFLECTION_DEG[0]:.0f}-{ROLLING_DEFLECTION_DEG[1]:.0f}",
                     mark(all(within(v, ROLLING_DEFLECTION_DEG) for v in band["rolling"]))])
    else:
        rows.append(["C4", "Rolling cut, 1/4 to 3/4 ball", missing, "", NO_DATA])
    if band["stun"]:
        rows.append(["C5", f"Stun cut, 1/4 to 3/4 ball{at_speed}",
                     f"{min(band['stun']):.1f}-{max(band['stun']):.1f} deg between the balls",
                     f"90-degree rule less restitution and throw: {STUN_SEPARATION_DEG[0]:.0f}-"
                     f"{STUN_SEPARATION_DEG[1]:.0f}", mark(all(within(v, STUN_SEPARATION_DEG) for v in band["stun"]))])
    else:
        rows.append(["C5", "Stun cut, 1/4 to 3/4 ball", missing, "", NO_DATA])

    throw, published = m.get("throw"), fixtures.get("throw")
    if throw and published:
        names = ("slow", "medium", "fast")
        sets = published["half_ball_stun_throw_deg"]
        tolerance = published.get("tolerance_deg", 0.75)
        ok = True
        for name in names:
            found = [s[name] for s in sets.values()]
            ok &= min(found) - tolerance <= throw["stun"][name][1] <= max(found) + tolerance
        ok &= throw["stun"]["slow"][1] - throw["stun"]["fast"][1] >= 0.25
        published_text = ", ".join(f"{key.split('_')[0].capitalize()} " + " / ".join(f"{s[n]:g}" for n in names)
                                   for key, s in sets.items())
        rows.append(["C6", "Half-ball stun throw at " + " / ".join(f"{throw['stun'][n][0]:g}" for n in names) +
                     " m/s", " / ".join(f"{throw['stun'][n][1]:.2f}" for n in names) + " deg",
                     f"{published_text} (+- {tolerance:g}); slow above fast by 0.25", mark(ok)])
    else:
        rows.append(["C6", "Half-ball stun throw", missing, "", NO_DATA])

    rows.append(cushion_reference_row(series.get("cushion_rebound"), fixtures.get("cushion"), params))

    side = series.get("cushion_side")
    angles = cushion_side_angles(side) if side else None
    if angles:
        into = (_at(setup(side, "incidence_deg"), ", into the cushion at {:g} deg")
                + _at(setup(side, "cue_ball_speed_m_s"), " at {:g} m/s"))
        rows.append(["C8", f"Running and check side{into}",
                     f"check {angles['check']['rebound_deg']:.1f} < none {angles['plain']['rebound_deg']:.1f} < "
                     f"running {angles['running']['rebound_deg']:.1f} deg",
                     "angles ordered check < plain < running (Mathavan Fig. 8)", mark(angles["ordered"])])
    else:
        rows.append(["C8", "Running and check side", missing, "", NO_DATA])

    squirt = series.get("squirt")
    widest = max_side_squirt(squirt) if squirt else None
    if widest:
        angle = abs(widest["squirt_deg"])
        rows.append(["C9", f"Squirt at maximum side (tip {widest['tip_x']:.2f})", f"{angle:.2f} deg",
                     f"{MAX_SIDE_SQUIRT_DEG[0]:.0f}-{MAX_SIDE_SQUIRT_DEG[1]:.0f} deg",
                     mark(within(angle, MAX_SIDE_SQUIRT_DEG))])
    else:
        rows.append(["C9", "Squirt at maximum side", missing, "", NO_DATA])

    swerve, pull = series.get("swerve"), sliding_pull(params)
    found = raised_swerves(swerve, pull) if swerve and pull else []
    if found:
        verdict = evaluate_swerve(swerve, pull)
        lows = min(p["accel_min"] for _, _, p in found)
        highs = max(p["accel_max"] for _, _, p in found)
        spread = max(p["accel_spread_deg"] for _, _, p in found)
        drift = max(abs(p["turn_roll_deg"]) for _, _, p in found)
        raised = sorted(number(path["elevation_deg"]) for _, path, _ in found)
        pulled = f"{lows:.3f}" if f"{lows:.3f}" == f"{highs:.3f}" else f"{lows:.3f}-{highs:.3f}"
        rows.append(["C10", f"Swerve, cue raised {raised[0]:g}-{raised[-1]:g} deg with side",
                     f"while sliding pulled at {pulled} m/s^2 in a direction fixed to {spread:.2f} deg, turning "
                     f"towards the side; once rolling at most {drift:.2f} deg more",
                     f"TP A.4 parabola: mu_s g = {pull:.3f} m/s^2 in one direction (0.5%, 0.2 deg); straight "
                     f"once rolling apart from the nap", verdict.status])
    else:
        rows.append(["C10", "Swerve", missing, "", NO_DATA])

    rows.append(lag_row(series.get("lag"), m.get("lag"), fixtures.get("table_speed"), params))
    rows.append(["C12-C14", "Energy, determinism, robustness", "not measured by this report",
                 "ofs_calibration_tests and `ofs_sim_cli fuzz` in the gate", "-"])
    return rows


def cushion_reference_row(rebound: Series | None, reference: dict | None, params: dict) -> list[str]:
    what = "Cushion rebound, rolling, no side"
    if rebound is None or reference is None:
        return ["C7", what, "not measured (see Data problems)", "", NO_DATA]
    stale = [k for k, v in reference.get("params", {}).items() if k in params and params[k] != v]
    if stale:
        return ["C7", what, f"not compared: the Mathavan reference was made with other values of "
                            f"{', '.join(stale)}; regenerate it with tools/gen_reference.py", "1 deg, 2%", NO_DATA]
    rows = {(r["speed_m_s"], r["incidence_deg"]): r for r in rebound.records()}
    worst_angle, worst_speed, compared = 0.0, 0.0, []
    for case in reference.get("cases", []):
        if case.get("group") != "rolling":
            continue
        row = rows.get((case["speed_m_s"], case["incidence_deg"]))
        if row is None or number(row.get("rebound_deg")) is None or number(row.get("speed_ratio")) is None:
            continue
        compared.append(case)
        worst_angle = max(worst_angle, abs(row["rebound_deg"] - case["rebound_angle_deg"]))
        worst_speed = max(worst_speed, abs(row["speed_ratio"] - case["rebound_speed_m_s"] / case["speed_m_s"]))
    if not compared:
        return ["C7", what, "no case in common with the Mathavan reference", "1 deg, 2%", NO_DATA]
    ok = worst_angle <= CUSHION_TOLERANCE[0] and worst_speed <= CUSHION_TOLERANCE[1]
    case_speeds = [c["speed_m_s"] for c in compared]
    angles = [c["incidence_deg"] for c in compared]
    return ["C7", what + f", {min(case_speeds):g}-{max(case_speeds):g} m/s, {min(angles):g}-{max(angles):g} deg",
            f"worst {worst_angle:.2f} deg and {worst_speed * 100:.2f}% of the speed off the fine-step Mathavan "
            f"solution over {len(compared)} cases", "1 deg, 2% (the suite also checks side, stun and screw)", mark(ok)]


def lag_row(lag_series: Series | None, lag: dict | None, published: dict | None, params: dict) -> list[str]:
    what = "Lag: baulk line to the top cushion and back to baulk"
    ball_speed = number(lag_series.summary.get("lag_ball_speed_m_s")) if lag_series else None
    mu_s, g = number(params.get("slide_friction")), number(params.get("gravity_m_s2"))
    if ball_speed is None or lag is None or published is None or not mu_s or not g:
        return ["C11", what, "not measured (see Data problems)", "", NO_DATA]
    fast, slow = published["effective_slope"]["fast"], published["effective_slope"]["slow"]
    margin, e_c = published["speed_margin_rel"], published["cushion_rebound_ratio"]
    d_up = lag["playing_length_m"] / 2 - R - lag["start_x"]
    low = tp_b6_lag_speed(fast, e_c, mu_s, g, d_up, lag["back_m"]) * (1 - margin)
    high = tp_b6_lag_speed(slow, e_c, mu_s, g, d_up, lag["back_m"]) * (1 + margin)
    back = lag["t_baulk"] - lag["t_top"]
    slope = 2 * lag["back_m"] / (g * back ** 2)
    ok = low <= ball_speed <= high and fast <= slope <= slow
    return ["C11", what, f"cue ball {ball_speed:.2f} m/s; back down from the top cushion in {back:.1f} s, "
                         f"effective slope {slope:.4f}",
            f"{low:.2f}-{high:.2f} m/s (TP B.6 for slopes {fast:g}-{slow:g}, +-{margin * 100:.0f}%); slope "
            f"{fast:g}-{slow:g}", mark(ok)]


def what_changed(changes: dict | None, params: dict) -> tuple[list[str], list[str]]:
    """The "What changed" lines from what_changed.json, and a problem for each value params no longer has."""
    if changes is None:
        return ["No what_changed.json was found."], ["what_changed.json is missing or not a JSON object"]
    lines, problems = [], []
    for entry in changes.get("changes", []):
        values_text = []
        try:
            pairs = [(name, before, after) for name, (before, after) in entry["params"].items()]
        except (KeyError, TypeError, ValueError, AttributeError):
            problems.append(f"what_changed.json: '{entry.get('what', '?')}' needs params as {{name: [from, to]}}")
            continue
        for name, before, after in pairs:
            text = f"`{name}` " + (f"new, {after}" if before is None else f"{before} to {after}")
            now = params.get(name)
            if now != after:
                text += f" (now {now})" if name in params else " (no longer in the file)"
                problems.append(f"what_changed.json gives {name} as {after}; table_params.json has {now}")
            values_text.append(text)
        lines.append(f"- {entry.get('what', '?')} ({', '.join(values_text)}): {entry.get('why', '')}")
    return lines, problems


def report_context(shots: dict, series: dict[str, Series], params: dict, fixtures: dict) -> dict:
    """What the verdicts and charts share: the cloth's sliding pull, the results rows and the swerve elevations."""
    rows = results_rows(shots, series, params, fixtures)
    return {"slide_accel": sliding_pull(params), "results": {row[0]: row for row in rows},
            "swerve_elevation": shots.get("swerve_elevation", {})}


def table(rows: list[list[str]], header: list[str]) -> list[str]:
    out = ["| " + " | ".join(header) + " |", "| " + " | ".join("---" for _ in header) + " |"]
    return out + ["| " + " | ".join(cell.replace("|", "/") for cell in row) + " |" for row in rows]


def assemble_report(series: dict[str, Series], charts: dict[str, str], problems: list[str],
                    source: str = "", shots: dict | None = None, params: dict | None = None,
                    fixtures: dict | None = None, changes: dict | None = None) -> str:
    """report.md: plain words, results and what changed, then a section per chart (`charts`: name to PNG)."""
    params, fixtures, shots = params or {}, fixtures or {}, shots or {}
    problems = list(problems) + series_problems(series)
    lines = ["# Calibration report", ""]
    every = ordered(series)
    if not every:
        lines.append("There is no calibration data to report.")
    else:
        first = every[0]
        build = sorted({s.build for s in every if s.build})
        lines.append(f"Physics parameters `{first.params_hash}`, table outline `{first.table_hash}`, core "
                     f"{first.core_version or 'unknown'}, {' and '.join(build) or 'unknown'} build"
                     + (f", from `{source}`." if source else "."))
        lines.append("")
        lines += ["## In plain words", ""]
        if shots:
            lines += plain_words(shots, series, params, fixtures)
        else:
            lines.append("Not measured: the report's own shots did not run (see Data problems).")
        context = report_context(shots, series, params, fixtures)
        lines += ["", "## Results against the targets", "",
                  "What this report measured itself, from `ofs_sim_cli calibrate` and its own shots. The "
                  "calibration suite (`ofs_calibration_tests`, run by the gate) checks the same targets with the "
                  "exact tolerances in `docs/calibration/targets.md`.", ""]
        lines += table(list(context["results"].values()), ["#", "What", "Measured", "Target", "Verdict"])
        change_lines, change_problems = what_changed(changes, params)
        problems += change_problems
        lines += ["", "## What changed", "",
                  (changes or {}).get("heading", "What changed") + ", each with its source in "
                  "`core/data/table_params.json`:", ""] + change_lines
        lines += ["", "## Charts", "",
                  "Each chart says what it shows and whether it meets its target from spec 7.11. \"For review\" "
                  "means the target is the owner's feel rather than a number this report can check."]
        lines += ["", "| Target | Chart | Verdict |", "| --- | --- | --- |"]
        verdicts = {s.name: evaluate(s, context) for s in every}
        for s in every:
            lines.append(f"| {s.target or '-'} | [{s.title}](#{anchor(s)}) | {verdicts[s.name].status} |")
        for s in every:
            lines += ["", f"### {heading_text(s)}", ""]
            if s.name in charts:
                lines += [f"![{s.title}]({charts[s.name]})", ""]
            else:
                lines += ["No chart: the data has no values to plot.", ""]
            lines.append(f"{describe(s)} {verdicts[s.name].sentence}")
            for note in s.notes:
                lines += ["", f"Setup: {note}"]
    if problems:
        lines += ["", "## Data problems", ""] + [f"- {p}" for p in problems]
    return "\n".join(lines) + "\n"


def heading_text(s: Series) -> str:
    return f"{s.target}: {s.title}" if s.target else s.title


def anchor(s: Series) -> str:
    """GitHub's anchor for heading_text(s): lowercase, punctuation dropped, spaces to hyphens."""
    text = "".join(c for c in heading_text(s).lower() if c.isalnum() or c in " -_")
    return text.replace(" ", "-")


def screw_follow_series(rows: list[list], like: Series | None, length: float) -> Series:
    """The report's own screw and follow shots as a chart series, in place of calibrate's (whose anomalies it
    keeps, so they are still listed)."""
    base = like or Series("screw_follow", "", "7.12", "", "", "", "", [], [])
    return Series(name="screw_follow", title="Screw-back and follow distance against tip height", target="7.12",
                  params_hash=base.params_hash, table_hash=base.table_hash, core_version=base.core_version,
                  build=base.build,
                  columns=["cue_speed_m_s", "tip_y", "ball_speed_m_s", "distance_m", "stopped_short_m"], rows=rows,
                  summary={"playing_length_m": length}, anomalies=base.anomalies,
                  notes=["Full-ball hit on a red 1 m away (centre to centre), simulated by this report on an endless "
                         "table (the real outline scaled up 20 times), so no cushion is in reach. Distance from the "
                         "contact to where the cue ball stops, along the aim: negative is screw back. The speeds "
                         "are cue speeds; the cue ball leaves slower the further the tip is from centre."])


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


def _colour(index: int) -> str:
    return SERIES_COLOURS[index % len(SERIES_COLOURS)]


def _line(ax, xs, ys, index: int, label: str):
    points = [(x, y) for x, y in zip(xs, ys) if number(x) is not None and number(y) is not None]
    if not points:
        return False
    ax.plot([p[0] for p in points], [p[1] for p in points], color=_colour(index), linewidth=1.8, marker="o",
            markersize=3.5, label=label)
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


def chart_titles(s: Series) -> list[str]:
    """The panel titles of the charts whose titles quote the series' setup, from its summary."""
    if s.name == "deflection":
        at = _at(setup(s, "cue_ball_speed_m_s"), ", cue ball at {:g} m/s")
        return [f"Rolling{at} (C4); shaded: {ROLLING_DEFLECTION_DEG[0]:.0f}-{ROLLING_DEFLECTION_DEG[1]:.0f}",
                f"Stun{at} (C5); shaded: {STUN_SEPARATION_DEG[0]:.0f}-{STUN_SEPARATION_DEG[1]:.0f}"]
    if s.name == "cushion_side":
        into = (_at(setup(s, "incidence_deg"), ", into the cushion at {:g} deg")
                + _at(setup(s, "cue_ball_speed_m_s"), ", cue ball {:g} m/s"))
        return [f"Check (left) and running (right) side{into} (C8)"]
    if s.name == "squirt":
        at = _at(setup(s, "cue_speed_m_s"), ", cue speed {:g} m/s")
        return [f"Squirt, level cue{at} (C9); shaded: {MAX_SIDE_SQUIRT_DEG[0]:.0f}-{MAX_SIDE_SQUIRT_DEG[1]:.0f} deg "
                f"at maximum side"]
    if s.name == "swerve":
        return [f"Swerve from above{_at(setup(s, 'cue_speed_m_s'), ', cue speed {:g} m/s')} (C10)"]
    return []


def _chart_deflection(plt, s: Series, _context):
    figure, (left, right) = _axes(plt, 2)
    rolling = sorted(s.where(mode="rolling"), key=lambda r: r["fraction"])
    stun = sorted(s.where(mode="stun"), key=lambda r: r["fraction"])
    titles = chart_titles(s)
    # The targets hold between a quarter and three-quarter ball only.
    left.fill_between([0.25, 0.75], *ROLLING_DEFLECTION_DEG, color=BAND, zorder=0)
    any_data = _line(left, [r["fraction"] for r in rolling], [r["cue_deflection_deg"] for r in rolling], 0,
                     "rolling")
    _label(left, titles[0], "fraction of ball hit", "degrees from the aim line")
    right.fill_between([0.25, 0.75], *STUN_SEPARATION_DEG, color=BAND, zorder=0)
    any_data |= _line(right, [r["fraction"] for r in stun], [r["separation_deg"] for r in stun], 1, "stun")
    _label(right, titles[1], "fraction of ball hit", "degrees between the two balls")
    return figure, any_data


def _chart_throw(plt, s: Series, _context):
    figure, (ax,) = _axes(plt)
    any_data = False
    for i, (speed, rows) in enumerate(sorted(_by(s, "speed_m_s").items())):
        rows.sort(key=lambda r: r["fraction"])
        any_data |= _line(ax, [r["fraction"] for r in rows], [r["throw_deg"] for r in rows], i,
                          f"cue ball {speed:g} m/s")
    ax.axvline(0.5, color=INK_2, linewidth=0.8, linestyle=":")
    _label(ax, "Cut-induced throw, stunned cue ball (C6); dotted: half-ball", "fraction of ball hit",
           "throw, degrees (object ball goes fuller)")
    _legend(ax)
    return figure, any_data


def _chart_cushion_rebound(plt, s: Series, _context):
    figure, (left, right) = _axes(plt, 2)
    any_data = False
    for i, (speed, rows) in enumerate(sorted(_by(s, "speed_m_s").items())):
        rows.sort(key=lambda r: r["incidence_deg"])
        incidence = [r["incidence_deg"] for r in rows]
        label = f"cue ball {speed:g} m/s"
        any_data |= _line(left, incidence, [r["rebound_deg"] for r in rows], i, label)
        _line(right, incidence, [r["speed_ratio"] for r in rows], i, label)
    left.plot([0, 80], [0, 80], color=INK_2, linewidth=0.8, linestyle=":")
    _label(left, "Rebound angle just after impact (C7); dotted: mirror", "incidence, degrees", "rebound, degrees")
    _label(right, "Speed kept", "incidence, degrees", "speed after / before")
    _legend(left)
    return figure, any_data


def _chart_cushion_side(plt, s: Series, _context):
    figure, (ax,) = _axes(plt)
    rows = sorted(s.records(), key=lambda r: r["side"])
    any_data = _line(ax, [r["side"] for r in rows], [r["rebound_deg"] for r in rows], 0, "just after impact")
    _line(ax, [r["side"] for r in rows], [r.get("rebound_rolling_deg") for r in rows], 1, "once rolling again")
    ax.axvline(0.0, color=INK_2, linewidth=0.8, linestyle=":")
    _label(ax, chart_titles(s)[0], "side, w_z R / v (1: as much as a tip 0.4 R off centre)", "rebound, degrees")
    _legend(ax)
    return figure, any_data


def _chart_squirt(plt, s: Series, _context):
    figure, (ax,) = _axes(plt)
    rows = sorted((r for r in s.records() if r.get("miscue") is False), key=lambda r: r["tip_x"])
    ax.axhspan(*MAX_SIDE_SQUIRT_DEG, color=BAND, zorder=0)
    squirt = [abs(r["squirt_deg"]) if number(r["squirt_deg"]) is not None else None for r in rows]
    any_data = _line(ax, [r["tip_x"] for r in rows], squirt, 0, "squirt")
    limit = number(s.summary.get("miscue_limit"))
    if limit is not None:
        ax.axvline(limit, color=INK_2, linewidth=0.8, linestyle=":")
        ax.text(limit, MAX_SIDE_SQUIRT_DEG[1], " miscue limit", fontsize=8, color=INK_2, ha="right", va="bottom")
    _label(ax, chart_titles(s)[0], "tip offset, fraction of R", "degrees off the aim line")
    return figure, any_data


def _chart_swerve(plt, s: Series, context):
    figure, (whole, start) = _axes(plt, 2)
    any_data = False
    pull = context.get("slide_accel")
    for i, (name, path) in enumerate(swerve_paths(s).items()):
        xs, ys = [p[0] for p in path["points"]], [p[1] for p in path["points"]]
        label = swerve_label(name, path, context.get("swerve_elevation", {}))
        any_data |= _line(whole, xs, ys, i, label)
        near = [(x, y) for x, y in zip(xs, ys) if math.dist((x, y), (xs[0], ys[0])) <= 0.4] if xs else []
        _line(start, [p[0] for p in near], [p[1] for p in near], i, label)
        phases = swerve_phases(path, pull) if pull else None
        if phases:
            start.plot(*phases["grip_point"], marker="o", markersize=6, color=_colour(i), markeredgecolor=INK)
    for ax in (whole, start):
        for line in ax.get_lines():
            if line.get_markersize() < 5:
                line.set_marker("")
    _label(whole, chart_titles(s)[0], "x, m (aiming along +x)", "y, m (right of the aim is down)")
    _label(start, "The first 40 cm; dot: starts to roll", "x, m", "y, m")
    _legend(whole)
    return figure, any_data


def swerve_label(name: str, path: dict, used: dict) -> str:
    asked, tip = number(path["elevation_deg"]), number(path["tip_x"])
    if asked is None or tip is None:
        return name
    elevation = used.get(name, asked)
    if elevation > asked + 0.05:
        cue = f"cue {elevation:.1f} deg (asked {asked:g})"
    else:
        cue = f"cue raised {asked:g} deg" if asked else "level cue"
    return f"{cue}, tip {tip:+.1f}"


def _chart_lag(plt, s: Series, _context):
    figure, (ax,) = _axes(plt)
    rows = sorted(s.records(), key=lambda r: r["cue_speed_m_s"])
    any_data = _line(ax, [r["cue_speed_m_s"] for r in rows], [r["final_x_m"] for r in rows], 0,
                     "where it stops")
    lag = number(s.summary.get("lag_cue_speed_m_s"))
    if lag is not None:
        ax.axvline(lag, color=INK_2, linewidth=0.8, linestyle=":")
    ratios = [r["ball_speed_m_s"] / r["cue_speed_m_s"] for r in rows
              if number(r.get("ball_speed_m_s")) and number(r.get("cue_speed_m_s"))]
    if ratios:
        k = sum(ratios) / len(ratios)
        top = ax.secondary_xaxis("top", functions=(lambda v: v * k, lambda v: v / k))
        top.set_xlabel("cue-ball speed, m/s", color=INK_2, fontsize=9)
        top.tick_params(colors=INK_2, labelsize=8)
    _label(ax, "The lag (C11); dotted: the lag speed", "cue speed, m/s", "final x, m (baulk cushion at -1.78)")
    return figure, any_data


def _chart_screw_follow(plt, s: Series, _context):
    figure, (ax,) = _axes(plt)
    any_data = False
    speed_key = "cue_speed_m_s" if "cue_speed_m_s" in s.columns else "speed_m_s"
    groups: dict = {}
    for r in s.records():
        groups.setdefault(r[speed_key], []).append(r)
    notes = []
    for i, (speed, rows) in enumerate(sorted(groups.items())):
        rows.sort(key=lambda r: r["tip_y"])
        any_data |= _line(ax, [r["tip_y"] for r in rows], [r["distance_m"] for r in rows], i,
                          f"cue speed {speed:g} m/s")
        for r in rows:
            if number(r.get("distance_m")) is None:
                ax.plot(r["tip_y"], 0.0, marker="x", markersize=8, color=_colour(i), linestyle="")
                notes.append(f"x: missing, cue speed {speed:g} m/s with tip {r['tip_y']:+.1f}: the screw stops the "
                             f"cue ball before it reaches the red")
    length = number(s.summary.get("playing_length_m"))
    ax.axhline(0.0, color=INK_2, linewidth=0.8, linestyle=":")
    if length is not None:
        low, high = ax.get_ylim()
        for y in (-length, length):
            if low <= y <= high:
                ax.axhline(y, color=INK_2, linewidth=0.8, linestyle="--")
    if notes:
        ax.text(0.99, 0.02, "\n".join(notes), transform=ax.transAxes, fontsize=8, color=INK_2, ha="right",
                va="bottom")
    title = "Screw back (below 0) and follow through from 1 m, endless table"
    if length is not None:
        title += "; dashed: a table length"
    _label(ax, title, "tip height, fraction of R", "metres after the contact")
    _legend(ax)
    return figure, any_data


def _chart_break_sim_time(plt, s: Series, _context):
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


def render_charts(series: dict[str, Series], out_dir, context: dict | None = None) -> tuple[dict[str, str], list[str]]:
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
            figure, any_data = draw(plt, s, context or {})
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


def find_sim_cli() -> pathlib.Path | None:
    name = "ofs_sim_cli.exe" if sys.platform == "win32" else "ofs_sim_cli"
    for build, config in (("windows-physics", "Release"), ("windows-physics", ""), ("windows", "Release"),
                          ("windows", "Debug"), ("windows", "")):
        path = ROOT / "build" / build / "tools" / "sim_cli" / config / name
        if path.is_file():
            return path
    return None


def load_fixtures() -> dict:
    return {"throw": read_json(FIXTURES / "reference" / "throw_published.json"),
            "cushion": read_json(FIXTURES / "reference" / "cushion_mathavan.json"),
            "table_speed": read_json(FIXTURES / "reference" / "table_speed_published.json")}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("data", type=pathlib.Path, help="the folder sim_cli calibrate --out wrote")
    parser.add_argument("out", type=pathlib.Path, help="where report.md and the charts go")
    parser.add_argument("--sim-cli", type=pathlib.Path, help="ofs_sim_cli for the report's own shots (default: "
                                                              "build/windows-physics' Release one)")
    parser.add_argument("--params", type=pathlib.Path, default=DATA / "table_params.json")
    parser.add_argument("--table", type=pathlib.Path, default=DATA / "table_shape.json")
    parser.add_argument("--changes", type=pathlib.Path, default=DEFAULT_CHANGES,
                        help="what_changed.json for the \"What changed\" section")
    args = parser.parse_args(argv)
    series, problems = load_series(args.data)
    if not series:
        print(f"no calibration series in {args.data}" + "".join(f"\n  {p}" for p in problems))
        return 1
    sim_cli = args.sim_cli or find_sim_cli()
    if sim_cli is None or not sim_cli.is_file():
        print(f"ofs_sim_cli not found ({sim_cli or 'nothing in build/'}): build it with `cmake --build "
              f"build/windows-physics --config Release --target ofs_sim_cli` or pass --sim-cli")
        return 2
    params, param_problems = load_params(args.params)
    problems += param_problems
    fixtures = load_fixtures()
    problems += [f"core/tests/fixtures/reference: the {name} reference is missing"
                 for name, found in fixtures.items() if found is None]
    with tempfile.TemporaryDirectory() as work:
        endless = pathlib.Path(work) / "endless_table.json"
        endless_table(args.table, endless)
        sim = Sim(sim_cli, work, args.params)
        shots, shot_problems = measure(sim, endless, args.table, series, fixtures["throw"])
    problems += shot_problems
    problems += [f"anomaly in a report shot: {a}" for a in sim.anomalies]
    data_hashes = {s.params_hash for s in series.values()}
    if sim.params_hashes and sim.params_hashes != data_hashes:
        problems.append(f"the report's shots ran with parameters {', '.join(sorted(sim.params_hashes))}, the "
                        f"calibrate data with {', '.join(sorted(data_hashes))}")
    if "screw_chart" in shots:
        series["screw_follow"] = screw_follow_series(shots["screw_chart"], series.get("screw_follow"),
                                                     length_of(shots))
    changes = read_json(args.changes)
    context = report_context(shots, series, params, fixtures)
    charts, chart_problems = render_charts(series, args.out, context)
    report = assemble_report(series, charts, problems + chart_problems, source=args.data.name, shots=shots,
                             params=params, fixtures=fixtures, changes=changes)
    (args.out / "report.md").write_text(report, encoding="utf-8", newline="\n")
    for row in context["results"].values():
        print(f"{row[4]:<10}  {row[0]:<8}  {row[1]}")
    for s in ordered(series):
        print(f"{evaluate(s, context).status:<10}  {s.target or '-':<8}  chart: {s.title}")
    for problem in problems + series_problems(series) + chart_problems + what_changed(changes, params)[1]:
        print(f"problem: {problem}")
    print(f"wrote {args.out / 'report.md'} and {len(charts)} charts from {sim.runs} shots")
    return 0


if __name__ == "__main__":
    sys.exit(main())
