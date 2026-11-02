"""Tests of calibration_report.py's loading, verdicts, shots and report text, without matplotlib."""
import contextlib
import io
import json
import math
import os
import pathlib
import sys
import tempfile
import unittest

import calibration_report as report

ROOT = pathlib.Path(__file__).resolve().parent.parent
PARAMS_FILE = ROOT / "core" / "data" / "table_params.json"
TABLE_FILE = ROOT / "core" / "data" / "table_shape.json"
PARAMS = json.loads(PARAMS_FILE.read_text(encoding="utf-8"))["values"]


def find_cli():
    """OFS_SIM_CLI, else the Debug build's ofs_sim_cli (as test_sim_cli.py finds it)."""
    named = os.environ.get("OFS_SIM_CLI")
    if named:
        return pathlib.Path(named)
    name = "ofs_sim_cli.exe" if sys.platform == "win32" else "ofs_sim_cli"
    build = ROOT / "build" / "windows" / "tools" / "sim_cli"
    for path in (build / "Debug" / name, build / name):
        if path.is_file():
            return path
    return None


CLI = find_cli()


def series_file(name, columns, rows, **extra):
    data = {"series": name, "title": extra.pop("title", name.replace("_", " ").capitalize()),
            "target": extra.pop("target", "C0"), "params_hash": extra.pop("params_hash", "0123456789abcdef"),
            "table_hash": "fedcba9876543210", "core_version": "0.1.0", "build": extra.pop("build", "release"),
            "columns": columns, "rows": rows, "summary": extra.pop("summary", {}),
            "notes": extra.pop("notes", [])}
    data.update(extra)
    return data


def make(name, columns, rows, **extra):
    data = series_file(name, columns, rows, **extra)
    return report.Series(name=data["series"], title=data["title"], target=data["target"],
                         params_hash=data["params_hash"], table_hash=data["table_hash"],
                         core_version=data["core_version"], build=data["build"], columns=columns, rows=rows,
                         summary=data["summary"], notes=data["notes"], anomalies=data.get("anomalies", []))


def deflection(rolling_deg, stun_deg):
    rows = []
    for cut in range(0, 90, 5):
        fraction = 1 - math.sin(math.radians(cut))
        rows.append(["rolling", cut, fraction, 88.0, rolling_deg])
        rows.append(["stun", cut, fraction, stun_deg, 60.0])
    return make("deflection", ["mode", "cut_deg", "fraction", "separation_deg", "cue_deflection_deg"], rows)


def throw(slow, fast):
    return make("throw", ["speed_m_s", "cut_deg", "fraction", "throw_deg"],
                [[0.5, 30, 0.5, slow], [4.0, 30, 0.5, fast], [0.5, 60, 1 - math.sin(math.radians(60)), 1.0]])


def swerve(turn_right_for_right_side=True):
    rows = []
    variants = (("level", 0.0, 0.4), ("raised right", 30.0, 0.4), ("raised left", 30.0, -0.4))
    for variant, elevation, tip in variants:
        bend = -1.0 if (tip > 0) == turn_right_for_right_side else 1.0
        for k in range(20):
            x = 0.1 * k
            y = bend * 0.02 * k * k * 0.1 if elevation else 0.0
            rows.append([variant, elevation, tip, 0.01 * k, x, y])
    return make("swerve", ["variant", "elevation_deg", "tip_x", "t_s", "x_m", "y_m"], rows)


class ImportTest(unittest.TestCase):
    def test_import_needs_no_matplotlib(self):
        self.assertNotIn("matplotlib", sys.modules)


class LoadTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def write(self, name, data):
        text = data if isinstance(data, str) else json.dumps(data)
        (self.dir / name).write_text(text, encoding="utf-8")

    def test_loads_good_series_and_lists_every_bad_file(self):
        self.write("throw.json", series_file("throw", ["a", "b"], [[1, 2], [3, None]]))
        self.write("lag.json", series_file("lag", ["a"], [[1]], summary={"lag_cue_speed_m_s": 1.2}))
        self.write("broken.json", "{not json")
        self.write("list.json", "[1, 2]")
        self.write("partial.json", {"series": "partial", "title": "x"})
        self.write("ragged.json", series_file("ragged", ["a", "b"], [[1, 2], [3]]))
        self.write("zz_again.json", series_file("throw", ["a"], [[1]]))
        series, problems = report.load_series(self.dir)
        self.assertEqual(set(series), {"throw", "lag"})
        self.assertEqual(series["throw"].records()[1], {"a": 3, "b": None})
        self.assertEqual(series["lag"].summary, {"lag_cue_speed_m_s": 1.2})
        text = "\n".join(problems)
        for fragment in ("broken.json: not readable JSON", "list.json: not a JSON object",
                         "partial.json: missing params_hash, columns, rows",
                         "ragged.json: 1 rows don't have 2 values",
                         "zz_again.json: a second 'throw' series"):
            self.assertIn(fragment, text)

    def test_different_parameter_sets_are_a_problem(self):
        self.write("a.json", series_file("a", ["x"], [[1]], params_hash="1111111111111111"))
        self.write("b.json", series_file("b", ["x"], [[1]], params_hash="2222222222222222"))
        _, problems = report.load_series(self.dir)
        self.assertEqual(problems, ["the series were made with different parameter sets: "
                                    "1111111111111111, 2222222222222222"])

    def test_anomalies_load_and_a_malformed_list_is_a_problem(self):
        anomaly = {"shot": "stun at 0.5 m/s, cut 30 deg", "kind": "solver_fallback", "t": 0.012, "balls": [0, 1],
                   "detail": "contact solve for 2 balls did not converge"}
        self.write("throw.json", series_file("throw", ["a"], [[1]], anomalies=[anomaly]))
        self.write("lag.json", series_file("lag", ["a"], [[1]], anomalies="none"))
        self.write("swerve.json", series_file("swerve", ["a"], [[1]]))
        series, problems = report.load_series(self.dir)
        self.assertEqual(series["throw"].anomalies, [anomaly])
        self.assertEqual(series["lag"].anomalies, [])
        self.assertEqual(series["swerve"].anomalies, [])
        self.assertEqual(problems, ["lag.json: anomalies must be a list of objects; its shots' anomalies can't be "
                                    "listed"])

    def test_missing_folder(self):
        series, problems = report.load_series(self.dir / "nowhere")
        self.assertEqual(series, {})
        self.assertIn("is not a folder", problems[0])

    def test_main_fails_without_data(self):
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(report.main([str(self.dir), str(self.dir / "out")]), 1)
        self.assertIn("no calibration series", out.getvalue())


class VerdictTest(unittest.TestCase):
    def test_thirty_and_ninety_degree_rules(self):
        self.assertEqual(report.evaluate(deflection(31.0, 88.0)).status, report.MET)
        verdict = report.evaluate(deflection(40.0, 88.0))
        self.assertEqual(verdict.status, report.NOT_MET)
        self.assertIn("outside the 30 degree rule", verdict.sentence)
        self.assertEqual(report.evaluate(deflection(31.0, 70.0)).status, report.NOT_MET)

    def test_thirty_degree_band_is_the_one_in_targets_md(self):
        targets = (ROOT / "docs" / "calibration" / "targets.md").read_text(encoding="utf-8")
        low, high = report.ROLLING_DEFLECTION_DEG
        self.assertIn(f"band {low:.0f}-{high:.0f} deg", targets)
        self.assertEqual(report.evaluate(deflection(24.5, 88.0)).status, report.MET)
        self.assertEqual(report.evaluate(deflection(35.5, 88.0)).status, report.MET)
        self.assertIn("24 to 36", report.evaluate(deflection(31.0, 88.0)).sentence)

    def test_deflection_without_numbers_is_no_data(self):
        self.assertEqual(report.evaluate(deflection(None, None)).status, report.NO_DATA)

    def test_throw_slower_throws_more(self):
        self.assertEqual(report.evaluate(throw(4.0, 2.0)).status, report.MET)
        verdict = report.evaluate(throw(2.0, 4.0))
        self.assertEqual(verdict.status, report.NOT_MET)
        self.assertIn("they do not", verdict.sentence)
        self.assertEqual(report.evaluate(throw(9.0, 2.0)).status, report.NOT_MET)
        self.assertEqual(report.evaluate(throw(None, None)).status, report.NO_DATA)

    def test_squirt_at_maximum_side(self):
        columns = ["tip_x", "squirt_deg", "ball_speed_m_s", "miscue"]
        good = make("squirt", columns, [[0.0, 0.0, 3, False], [0.5, -2.2, 3, False], [0.6, 9.0, 1, True]])
        self.assertEqual(report.evaluate(good).status, report.MET)
        self.assertIn("2.2 degrees", report.evaluate(good).sentence)
        wide = make("squirt", columns, [[0.0, 0.0, 3, False], [0.5, 4.0, 3, False]])
        self.assertEqual(report.evaluate(wide).status, report.NOT_MET)
        none = make("squirt", columns, [[0.0, 0.0, 3, False], [0.5, 0.0, 3, False]])
        self.assertEqual(report.evaluate(none).status, report.NO_DATA)

    def test_swerve_bends_towards_the_side(self):
        verdict = report.evaluate(swerve(True))
        self.assertEqual(verdict.status, report.REVIEW)
        self.assertIn("raised right turns", verdict.sentence)
        self.assertNotIn("level", verdict.sentence)
        self.assertEqual(report.evaluate(swerve(False)).status, report.NOT_MET)

    def test_path_turn(self):
        self.assertAlmostEqual(report.path_turn([(0, 0), (1, 0), (2, 1)]), 45.0)
        self.assertAlmostEqual(report.path_turn([(0, 0), (1, 0), (2, -1)]), -45.0)
        self.assertIsNone(report.path_turn([(0, 0), (0, 0)]))

    def test_running_side_widens(self):
        columns = ["side", "rebound_deg", "speed_ratio", "rebound_rolling_deg"]
        good = make("cushion_side", columns, [[-1, 30, 0.8, 30], [0, 45, 0.8, 45], [1, 55, 0.8, 55]])
        self.assertEqual(report.evaluate(good).status, report.MET)
        backwards = make("cushion_side", columns, [[-1, 55, 0.8, 30], [0, 45, 0.8, 45], [1, 30, 0.8, 55]])
        self.assertEqual(report.evaluate(backwards).status, report.NOT_MET)

    def test_cushion_rebound_flags_speed_gain(self):
        columns = ["speed_m_s", "incidence_deg", "incidence_measured_deg", "rebound_deg", "speed_ratio",
                   "rebound_rolling_deg"]
        fine = make("cushion_rebound", columns, [[1, 30, 30, 35, 0.8, 40], [1, 60, 60, 62, 0.7, None]])
        self.assertEqual(report.evaluate(fine).status, report.REVIEW)
        faster = make("cushion_rebound", columns, [[1, 30, 30, 35, 1.2, 40]])
        self.assertEqual(report.evaluate(faster).status, report.NOT_MET)
        empty = make("cushion_rebound", columns, [[1, 30, None, None, None, None]])
        self.assertEqual(report.evaluate(empty).status, report.NO_DATA)

    def test_lag(self):
        columns = ["cue_speed_m_s", "ball_speed_m_s", "cushion_contacts", "final_x_m"]
        found = make("lag", columns, [[1, 1.4, 2, -1.7]],
                     summary={"lag_cue_speed_m_s": 1.0, "lag_ball_speed_m_s": 1.4})
        self.assertEqual(report.evaluate(found).status, report.REVIEW)
        self.assertIn("1.00 m/s", report.evaluate(found).sentence)
        missing = make("lag", columns, [[1, 0, 0, -1.0]], summary={"lag_cue_speed_m_s": None})
        self.assertEqual(report.evaluate(missing).status, report.NO_DATA)

    def test_break_times_against_the_budget(self):
        columns = ["rack_seed", "cue_speed_m_s", "events", "potted", "sim_us"]
        fast = make("break_sim_time", columns, [[1, 7, 300, 1, 800.0], [2, 7, 300, 0, 1200.0]])
        self.assertEqual(report.evaluate(fast).status, report.MET)
        slow = make("break_sim_time", columns, [[1, 7, 300, 1, 800.0], [2, 7, 300, 0, 2500.0]])
        self.assertEqual(report.evaluate(slow).status, report.NOT_MET)
        debug = make("break_sim_time", columns, [[1, 7, 300, 1, 8000.0]], build="debug")
        verdict = report.evaluate(debug)
        self.assertEqual(verdict.status, report.REVIEW)
        self.assertIn("debug build", verdict.sentence)

    def test_screw_follow_lists_each_speed(self):
        columns = ["speed_m_s", "tip_y", "distance_m"]
        s = make("screw_follow", columns, [[1, -0.5, -0.3], [1, 0.5, 0.4], [2, -0.5, -0.8], [2, 0.5, 0.9]])
        sentence = report.evaluate(s).sentence
        self.assertIn("at a cue speed of 1 m/s from -0.30 m (tip -0.5) to +0.40 m (tip +0.5)", sentence)
        self.assertIn("at a cue speed of 2 m/s", sentence)

    def test_screw_follow_names_a_missing_point_and_why(self):
        rows = [[1.0, -0.5, 0.93, None, 0.74], [1.0, 0.0, 1.39, 0.3, None], [2.0, 0.0, 2.78, 1.5, None]]
        s = report.screw_follow_series(rows, None, 3.569)
        sentence = report.evaluate(s).sentence
        self.assertIn("At a cue speed of 1 m/s with the tip -0.5 the cue ball never reaches the red, stopping "
                      "0.74 m from where it started", sentence)
        self.assertIn("shows that point as missing", sentence)
        self.assertIn("endless table", " ".join(s.notes))

    def test_unknown_series_is_for_review(self):
        self.assertEqual(report.evaluate(make("mystery", ["x"], [[1]])).status, report.REVIEW)

    def test_unreadable_values_are_no_data(self):
        s = make("throw", ["speed_m_s", "cut_deg", "fraction", "throw_deg"], [["fast", 30, 0.5, 2.0]] * 2)
        self.assertEqual(report.evaluate(s).status, report.NO_DATA)


class AssembleTest(unittest.TestCase):
    def test_report_has_an_overview_and_a_section_per_series(self):
        lag = make("lag", ["cue_speed_m_s"], [[1.0]], summary={"lag_cue_speed_m_s": None},
                   notes=["From the baulk line."])
        series = {"throw": throw(4.0, 2.0), "deflection": deflection(None, None), "lag": lag}
        text = report.assemble_report(series, {"throw": "throw.png"}, ["odd.json: not readable JSON"],
                                      source="calib")
        lines = text.splitlines()
        self.assertEqual(lines[0], "# Calibration report")
        self.assertIn("Physics parameters `0123456789abcdef`, table outline `fedcba9876543210`, core 0.1.0, "
                      "release build, from `calib`.", text)
        # Report order puts deflection before throw.
        self.assertLess(text.index("## C0: Deflection"), text.index("## C0: Throw"))
        self.assertIn("| C0 | [Throw](#c0-throw) | met |", text)
        self.assertIn("| C0 | [Deflection](#c0-deflection) | no data |", text)
        self.assertIn("![Throw](throw.png)", text)
        self.assertEqual(text.count("No chart: the data has no values to plot."), 2)
        self.assertIn("Setup: From the baulk line.", text)
        self.assertIn("## Data problems\n\n- odd.json: not readable JSON", text)
        self.assertTrue(text.endswith("\n"))

    def test_every_anomaly_of_a_series_is_a_data_problem(self):
        s = throw(4.0, 2.0)
        s.summary = {"cue_ball_speeds_m_s": [0.5, 4.0]}
        s.anomalies = [{"shot": "stun at 0.5 m/s, cut 30 deg", "kind": "solver_fallback", "t": 0.0123456789,
                        "balls": [0, 1], "detail": "contact solve for 2 balls did not converge"},
                       {"shot": "stun at 4 m/s, cut 80 deg", "kind": "time_cap", "t": None, "balls": [],
                        "detail": "still moving"}]
        text = report.assemble_report({"throw": s}, {}, [])
        self.assertIn("## Data problems\n\n"
                      "- throw: a solver_fallback anomaly in the shot 'stun at 0.5 m/s, cut 30 deg' at t = 0.0123457 s "
                      "(balls 0, 1): contact solve for 2 balls did not converge\n"
                      "- throw: a time_cap anomaly in the shot 'stun at 4 m/s, cut 80 deg' at t = ? s: still moving\n",
                      text)
        # The verdict is still given: the problem sits beside the chart, not in place of it.
        self.assertIn("| C0 | [Throw](#c0-throw) | met |", text)

    def test_the_report_screw_chart_keeps_calibrates_anomalies(self):
        calibrated = make("screw_follow", ["speed_m_s", "tip_y", "distance_m"], [[1.0, 0.0, 0.3]],
                          anomalies=[{"shot": "cue speed 1 m/s, tip_y -0.5", "kind": "time_cap", "t": 120.0,
                                      "balls": [0], "detail": "still moving"}])
        replaced = report.screw_follow_series([[1.0, 0.0, 1.39, 0.3, None]], calibrated, 3.569)
        self.assertEqual(replaced.anomalies, calibrated.anomalies)
        self.assertEqual(report.series_problems({"screw_follow": replaced}),
                         ["screw_follow: a time_cap anomaly in the shot 'cue speed 1 m/s, tip_y -0.5' at t = 120 s "
                          "(balls 0): still moving"])

    def test_empty_report(self):
        text = report.assemble_report({}, {}, ["nothing.json: not readable JSON"])
        self.assertIn("There is no calibration data to report.", text)
        self.assertIn("- nothing.json: not readable JSON", text)

    def test_anchor_matches_github_headings(self):
        s = make("lag", ["x"], [[1]], target="C11", title="The lag: cue speed to reach the top cushion")
        self.assertEqual(report.anchor(s), "c11-the-lag-cue-speed-to-reach-the-top-cushion")


PULL = PARAMS["slide_friction"] * PARAMS["gravity_m_s2"]


def swerve_rows(name, elevation, tip, grip_s=0.205, end_s=3.0):
    """A path that slides on a parabola (pulled at PULL in a fixed direction) until grip_s, then runs straight."""
    side = -1.0 if tip > 0 else 1.0
    v0 = (1.4, 0.0)
    a = (-0.8 * PULL, 0.6 * PULL * side)
    at_grip = (v0[0] * grip_s + a[0] * grip_s ** 2 / 2, v0[1] * grip_s + a[1] * grip_s ** 2 / 2)
    v_grip = (v0[0] + a[0] * grip_s, v0[1] + a[1] * grip_s)
    rows = []
    for k in range(int(round(end_s / 0.01)) + 1):
        t = 0.01 * k
        if t <= grip_s:
            x, y = v0[0] * t + a[0] * t * t / 2, v0[1] * t + a[1] * t * t / 2
        else:
            x, y = at_grip[0] + v_grip[0] * (t - grip_s), at_grip[1] + v_grip[1] * (t - grip_s)
        rows.append([name, elevation, tip, t, x - 1.3, y])
    return rows


def full_series(setups=None):
    """Calibrate-like series with the values the plain-words tests quote; `setups` replaces summaries by name."""
    setups = {"squirt": {"miscue_limit": 0.55, "cue_speed_m_s": 3.0},
              "cushion_rebound": {"cue_ball_speeds_m_s": [1.0, 4.0]},
              "cushion_side": {"incidence_deg": 45.0, "cue_ball_speed_m_s": 1.5},
              "swerve": {"cue_speed_m_s": 1.5},
              "deflection": {"cue_ball_speed_m_s": 1.0}, **(setups or {})}
    squirt = make("squirt", ["tip_x", "squirt_deg", "ball_speed_m_s", "miscue"],
                  [[0.0, 0.0, 4.2, False], [0.55, 2.01, 2.6, False], [0.6, 1.5, 0.6, True]],
                  summary=setups["squirt"], target="C9")
    rebound = make("cushion_rebound", ["speed_m_s", "incidence_deg", "incidence_measured_deg", "rebound_deg",
                                       "speed_ratio", "rebound_rolling_deg"],
                   [[1.0, 0, 0, 0.0, 0.911, 0.0], [4.0, 0, 0, 0.0, 0.702, 0.0], [1.0, 50, 50, 43.46, 0.847, 56.3]],
                   summary=setups["cushion_rebound"], target="C7")
    side = make("cushion_side", ["side", "rebound_deg", "speed_ratio", "rebound_rolling_deg"],
                [[-1.0, 35.67, 0.84, 49.51], [0.0, 37.32, 0.85, 50.13], [1.0, 46.59, 0.94, 58.81]],
                summary=setups["cushion_side"], target="C8")
    swerve = make("swerve", ["variant", "elevation_deg", "tip_x", "t_s", "x_m", "y_m"],
                  swerve_rows("30 deg, tip +0.40", 30.0, 0.4) + swerve_rows("30 deg, tip -0.40", 30.0, -0.4),
                  summary=setups["swerve"], target="C10")
    lag = make("lag", ["cue_speed_m_s", "ball_speed_m_s", "cushion_contacts", "final_x_m"], [[1.5, 2.08, 2, -1.7]],
               summary={"lag_cue_speed_m_s": 1.5448, "lag_ball_speed_m_s": 2.1437, "start_x_m": -1.0475},
               target="C11")
    defl = deflection(31.0, 82.0)
    defl.target = "C4, C5"
    defl.summary = setups["deflection"]
    return {"deflection": defl, "squirt": squirt, "cushion_rebound": rebound, "cushion_side": side,
            "swerve": swerve, "lag": lag}


def fake_shots():
    g, mu_s = PARAMS["gravity_m_s2"], PARAMS["slide_friction"]
    return {
        "ratios": {"centre": 1.389, "screw": 0.928},
        "pace": {1.0: 2.72, 2.0: 10.9, 3.0: 24.5},
        "c1": {"v0": 2.0, "x": 48 / (49 * mu_s * g), "v": 10 / 7},
        "c2": {"v0": 2.0, "ratio": (1 - PARAMS["ball_restitution"]) / 2, "creep_m": 0.00998},
        "c3": {"v0": 2.0, "ratio": 0.2667, "creep_m": 1.5},
        "stun": {0.5: {"speed": 1.8304, "creep_m": 0.0036, "harder_m": -0.0049, "softer_m": 0.0533},
                 1.0: {"speed": 2.6634, "creep_m": 0.0077, "harder_m": -0.0105, "softer_m": 0.1129}},
        "screw": {(0.5, 2.0): -0.30, (0.5, 3.0): -2.4, (0.5, 4.0): -5.6, (1.0, 2.0): 0.17, (1.0, 3.0): -0.83,
                  (1.0, 4.0): -3.7},
        "follow": {2.0: 1.7, 3.0: 3.9},
        "run_through": {"gap": 0.5, "speed": 3.0, "distance_m": 0.23, "sliding_at_contact": True},
        "throw": {"stun": {"slow": (0.447, 4.86), "medium": (1.341, 4.16), "fast": (3.129, 3.39)},
                  "rolling": {"medium": (1.341, 1.7)}},
        "lag": {"t_top": 3.0, "t_baulk": 11.4, "back_m": 3.569 - 2 * report.R, "start_x": -1.0475,
                "playing_length_m": 3.569},
        "swerve_elevation": {"30 deg, tip +0.40": 30.0, "30 deg, tip -0.40": 30.0},
    }


def plain_text(shots=None, series=None):
    return "\n".join(report.plain_words(fake_shots() if shots is None else shots,
                                        full_series() if series is None else series, PARAMS,
                                        report.load_fixtures()))


class PlainWordsTest(unittest.TestCase):
    def bullet(self, title, text=None):
        found = [line for line in (text or plain_text()).splitlines() if line.startswith(f"- **{title}")]
        self.assertEqual(len(found), 1, f"one '{title}' bullet")
        return found[0]

    def test_throw_makes_the_pot_fuller_so_you_aim_thinner(self):
        throw = self.bullet("Throw.")
        self.assertIn("so it goes fuller than the line through the two centres", throw)
        self.assertIn("4.9 degrees with a stunned cue ball arriving at 0.45 m/s, 4.2 at 1.34 m/s and 3.4 at "
                      "3.13 m/s", throw)
        self.assertIn("Over 1 m that is 8.5 cm on the slow cut and 5.9 cm on the firm one, so aim that much "
                      "thinner", throw)
        self.assertIn("A rolling cue ball throws less: 1.7 degrees at 1.34 m/s.", throw)
        self.assertNotIn("thicker", throw)

    def test_distances_longer_than_the_table_say_so(self):
        pace = self.bullet("Pace.")
        self.assertIn("at 1 m/s it runs 2.7 m (76% of the table's length)", pace)
        self.assertIn("at 2 m/s it would run 10.9 m on an endless table (3.1 table lengths)", pace)
        screw = self.bullet("Screw needs pace.")
        self.assertIn("from 50 cm at 2 m/s it comes back 30 cm, at 3 m/s it comes back 2.4 m and at 4 m/s it "
                      "would come back 5.6 m on an endless table", screw)
        follow = self.bullet("Follow.")
        self.assertIn("at 3 m/s it would follow through 3.9 m on an endless table", follow)

    def test_stun_speed_and_what_a_tenth_either_way_does(self):
        stun = self.bullet("Stun.")
        self.assertIn("keeps 3.0% of its speed and creeps on 1.0 cm", stun)
        self.assertIn("(tip -0.33), it stuns from 50 cm when it leaves at 1.83 m/s (it then creeps on 0.4 cm) "
                      "and from 1 m at 2.66 m/s: from further away you hit harder", stun)
        self.assertIn("hit a tenth harder (2.01 m/s) and it comes back 0.5 cm; a tenth softer (1.65 m/s) and it "
                      "runs on 5.3 cm", stun)
        self.assertIn("arrives still sliding and runs on 23 cm", stun)

    def test_screw_that_runs_on_says_the_backspin_is_gone(self):
        screw = self.bullet("Screw needs pace.")
        self.assertIn("Striking 0.5 below centre", screw)
        self.assertIn("from 1 m at 2 m/s it runs on 17 cm", screw)
        self.assertIn("At 2 m/s from 1 m the backspin is gone before the cue ball arrives", screw)

    def test_follow_gives_one_tip_height(self):
        follow = self.bullet("Follow.")
        self.assertIn("Striking 0.5 above centre, from 1 m at 2 m/s it follows through 1.7 m", follow)
        self.assertIn("keeps 27% of its speed, close to the textbook 2/7 (29%)", follow)

    def test_swerve_curves_from_the_strike_and_runs_straight_once_rolling(self):
        swerve = self.bullet("Swerve.")
        self.assertIn("curves back towards it from the moment it is struck while it slides, bending most just "
                      "before it grips the cloth; once it rolls it runs straight", swerve)
        self.assertIn("Raised 30 degrees with the tip 0.4 right of centre, at a cue speed of 1.5 m/s", swerve)
        self.assertIn("degrees right over the first", swerve)
        self.assertIn("0.0 degrees more over the next", swerve)
        self.assertNotIn("once it grips", swerve)

    def test_cushions_give_both_angles_and_the_measured_figure(self):
        cushions = self.bullet("Cushions.")
        self.assertIn("at 1 m/s comes off with 91% of its speed; firm hits lose more, 70% at 4 m/s", cushions)
        self.assertIn("averaged 82% below 1.5 m/s, where their own cushion model, which the game uses, gives 91%",
                      cushions)
        self.assertIn("leaves at 37.3 degrees just after the impact, then bends out to 50.1 as it grips", cushions)
        self.assertIn("Running side widens both (46.6 and 58.8 degrees", cushions)
        self.assertIn("check side narrows them (35.7 and 49.5)", cushions)
        self.assertNotIn("80%", cushions)

    def test_cloth_speed_without_jargon(self):
        text = plain_text()
        self.assertNotIn("last length", text)
        cloth = self.bullet("Cloth speed.", text)
        self.assertIn("needs the cue ball to leave at 2.14 m/s, a cue speed of 1.54 m/s; coming back down from the "
                      "top cushion to the baulk cushion takes 8.4 seconds", cloth)

    def test_speeds_say_whose_they_are(self):
        intro = plain_text().splitlines()[0]
        self.assertIn("Speeds are the cue ball's speed as it leaves the tip unless a figure says cue speed", intro)
        self.assertIn("1.39 times as fast as the cue", intro)
        self.assertIn("only 67% as fast", intro)
        self.assertIn("just inside the miscue limit of 0.55", intro)

    def test_missing_shots_drop_their_bullets(self):
        text = plain_text(shots={})
        self.assertNotIn("**Stun.**", text)
        self.assertIn("**Cut angles.**", text)
        self.assertIn("**Still starting guesses", text)


class ResultsTest(unittest.TestCase):
    def rows(self, shots=None, series=None, params=None, fixtures=None):
        found = report.results_rows(fake_shots() if shots is None else shots,
                                    full_series() if series is None else series,
                                    PARAMS if params is None else params,
                                    report.load_fixtures() if fixtures is None else fixtures)
        return {row[0]: row for row in found}

    def test_every_target_has_a_row(self):
        self.assertEqual(list(self.rows()), ["C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8", "C9", "C10", "C11",
                                             "C12-C14"])

    def test_formula_targets(self):
        rows = self.rows()
        for target in ("C1", "C2", "C3", "C6", "C9", "C10", "C11"):
            self.assertEqual(rows[target][4], report.MET, rows[target])
        self.assertIn("inside 24-36", rows["C4"][3])
        off = fake_shots()
        off["c1"]["x"] *= 1.01
        off["c2"]["ratio"] += 0.01
        off["throw"]["stun"]["slow"] = (0.447, 2.0)
        rows = self.rows(shots=off)
        for target in ("C1", "C2", "C6"):
            self.assertEqual(rows[target][4], report.NOT_MET, rows[target])

    def test_lag_against_tp_b6(self):
        row = self.rows()["C11"]
        self.assertIn("cue ball 2.14 m/s; back down from the top cushion in 8.4 s, effective slope 0.0102", row[2])
        self.assertIn("1.55-2.32 m/s", row[3])
        fast_cloth = fake_shots()
        fast_cloth["lag"]["t_baulk"] = 8.0
        self.assertEqual(self.rows(shots=fast_cloth)["C11"][4], report.NOT_MET)

    def test_cushions_against_the_mathavan_reference(self):
        fixtures = report.load_fixtures()
        cases = [c for c in fixtures["cushion"]["cases"] if c["group"] == "rolling"]
        columns = ["speed_m_s", "incidence_deg", "incidence_measured_deg", "rebound_deg", "speed_ratio",
                   "rebound_rolling_deg"]

        def rebound(shift_deg):
            rows = [[c["speed_m_s"], c["incidence_deg"], c["incidence_deg"], c["rebound_angle_deg"] + shift_deg,
                     c["rebound_speed_m_s"] / c["speed_m_s"], None] for c in cases]
            return make("cushion_rebound", columns, rows, target="C7")

        series = full_series()
        series["cushion_rebound"] = rebound(0.3)
        row = self.rows(series=series)["C7"]
        self.assertEqual(row[4], report.MET)
        self.assertIn(f"worst 0.30 deg and 0.00% of the speed off the fine-step Mathavan solution over "
                      f"{len(cases)} cases", row[2])
        series["cushion_rebound"] = rebound(1.5)
        self.assertEqual(self.rows(series=series)["C7"][4], report.NOT_MET)
        stale = dict(PARAMS, cushion_friction=0.2)
        row = self.rows(series=series, params=stale)["C7"]
        self.assertEqual(row[4], report.NO_DATA)
        self.assertIn("regenerate it", row[2])

    def test_nothing_measured(self):
        rows = self.rows(shots={}, series={}, params={}, fixtures={})
        self.assertEqual({row[4] for target, row in rows.items() if target != "C12-C14"}, {report.NO_DATA})

    def test_chart_verdicts_follow_the_results(self):
        series = full_series()
        context = report.report_context(fake_shots(), series, PARAMS, report.load_fixtures())
        lag = report.evaluate(series["lag"], context)
        self.assertEqual(lag.status, report.MET)
        self.assertIn("Against the target (C11): cue ball 2.14 m/s", lag.sentence)
        swerve = report.evaluate(series["swerve"], context)
        self.assertEqual(swerve.status, report.MET)
        self.assertIn("on a parabola", swerve.sentence)


class SetupTextTest(unittest.TestCase):
    """The setups the texts and chart labels state come from the series' summaries, never assumed."""

    def series(self, setups, throw_setup):
        series = full_series(setups)
        series["throw"] = throw(4.0, 2.0)
        series["throw"].summary = throw_setup
        return series

    def texts(self, series):
        rows = {row[0]: row for row in report.results_rows(fake_shots(), series, PARAMS, report.load_fixtures())}
        return ({name: report.describe(s) for name, s in series.items()},
                {name: report.chart_titles(s) for name, s in series.items()}, rows, plain_text(series=series))

    def test_texts_and_labels_quote_the_summary(self):
        series = self.series({"deflection": {"cue_ball_speed_m_s": 2.5},
                              "squirt": {"miscue_limit": 0.55, "cue_speed_m_s": 4.5},
                              "cushion_rebound": {"cue_ball_speeds_m_s": [1.0, 4.0]},
                              "cushion_side": {"incidence_deg": 30.0, "cue_ball_speed_m_s": 2.25},
                              "swerve": {"cue_speed_m_s": 1.75}}, {"cue_ball_speeds_m_s": [0.7, 2.0]})
        self.assertEqual(report.series_problems(series), [])
        described, titles, rows, plain = self.texts(series)
        self.assertIn("for a rolling cue ball and a stunned one, the cue ball at 2.5 m/s.", described["deflection"])
        self.assertIn("for a stunned cue ball arriving at 0.7 and 2 m/s.", described["throw"])
        self.assertIn("the angle it goes in at, at 1 and 4 m/s, and how much", described["cushion_rebound"])
        self.assertIn("rolling into the cushion at 30 degrees at 2.25 m/s with check side", described["cushion_side"])
        self.assertIn("(a level cue at a cue speed of 4.5 m/s)", described["squirt"])
        self.assertIn("aiming up the table at a cue speed of 1.75 m/s;", described["swerve"])
        self.assertEqual(titles["deflection"], ["Rolling, cue ball at 2.5 m/s (C4); shaded: 24-36",
                                                "Stun, cue ball at 2.5 m/s (C5); shaded: 80-92"])
        self.assertEqual(titles["cushion_side"], ["Check (left) and running (right) side, into the cushion at 30 deg, "
                                                  "cue ball 2.25 m/s (C8)"])
        self.assertEqual(titles["squirt"], ["Squirt, level cue, cue speed 4.5 m/s (C9); shaded: 1-3 deg at maximum "
                                            "side"])
        self.assertEqual(titles["swerve"], ["Swerve from above, cue speed 1.75 m/s (C10)"])
        self.assertEqual(rows["C4"][1], "Rolling cut, 1/4 to 3/4 ball, cue ball at 2.5 m/s")
        self.assertEqual(rows["C5"][1], "Stun cut, 1/4 to 3/4 ball, cue ball at 2.5 m/s")
        self.assertEqual(rows["C8"][1], "Running and check side, into the cushion at 30 deg at 2.25 m/s")
        self.assertIn("A rolling cue ball arriving at 2.5 m/s and cut between", plain)
        self.assertIn("right of centre, at a cue speed of 1.75 m/s, it squirts", plain)
        self.assertIn("Rolling into a side cushion at 30 degrees with no side", plain)
        self.assertTrue(report.evaluate(series["cushion_side"]).sentence.startswith(
            "Into the cushion at 30 degrees, running side sends"))

    def test_a_missing_setup_value_is_a_data_problem_not_a_guess(self):
        series = self.series({"deflection": {"cue_ball_speed_m_s": "fast"}, "squirt": {"miscue_limit": 0.55},
                              "cushion_rebound": {"cue_ball_speeds_m_s": [1.0, None]}, "cushion_side": {},
                              "swerve": {}}, {})
        self.assertEqual(report.series_problems(series), [
            f"{name}: the summary has no {key}, so the texts leave it out" for name, key in (
                ("deflection", "cue_ball_speed_m_s"), ("throw", "cue_ball_speeds_m_s"),
                ("cushion_rebound", "cue_ball_speeds_m_s"), ("cushion_side", "incidence_deg"),
                ("cushion_side", "cue_ball_speed_m_s"), ("squirt", "cue_speed_m_s"), ("swerve", "cue_speed_m_s"))])
        described, titles, rows, plain = self.texts(series)
        for name in ("deflection", "throw", "cushion_rebound", "cushion_side", "squirt", "swerve"):
            self.assertNotIn("m/s", described[name], name)
            self.assertNotIn(" deg", described[name], name)
        self.assertTrue(described["deflection"].endswith("for a rolling cue ball and a stunned one."))
        self.assertEqual(titles["deflection"], ["Rolling (C4); shaded: 24-36", "Stun (C5); shaded: 80-92"])
        self.assertEqual(titles["cushion_side"], ["Check (left) and running (right) side (C8)"])
        self.assertEqual(titles["squirt"], ["Squirt, level cue (C9); shaded: 1-3 deg at maximum side"])
        self.assertEqual(titles["swerve"], ["Swerve from above (C10)"])
        self.assertEqual(rows["C4"][1], "Rolling cut, 1/4 to 3/4 ball")
        self.assertEqual(rows["C8"][1], "Running and check side")
        self.assertIn("A rolling cue ball cut between", plain)
        self.assertIn("right of centre, it squirts", plain)
        self.assertIn("Rolling into a side cushion with no side", plain)
        self.assertTrue(report.evaluate(series["cushion_side"]).sentence.startswith("Running side sends"))
        text = report.assemble_report(series, {}, [])
        self.assertIn("- cushion_side: the summary has no incidence_deg, so the texts leave it out", text)

    def test_the_c7_row_names_the_cases_it_compared(self):
        fixtures = report.load_fixtures()
        cases = [c for c in fixtures["cushion"]["cases"] if c["group"] == "rolling" and c["speed_m_s"] <= 2.0
                 and c["incidence_deg"] <= 40]
        columns = ["speed_m_s", "incidence_deg", "incidence_measured_deg", "rebound_deg", "speed_ratio",
                   "rebound_rolling_deg"]
        rows = [[c["speed_m_s"], c["incidence_deg"], c["incidence_deg"], c["rebound_angle_deg"],
                 c["rebound_speed_m_s"] / c["speed_m_s"], None] for c in cases]
        row = report.cushion_reference_row(make("cushion_rebound", columns, rows), fixtures["cushion"], PARAMS)
        speeds = [c["speed_m_s"] for c in cases]
        angles = [c["incidence_deg"] for c in cases]
        self.assertEqual(row[1], f"Cushion rebound, rolling, no side, {min(speeds):g}-{max(speeds):g} m/s, "
                                 f"{min(angles):g}-{max(angles):g} deg")


class WhatChangedTest(unittest.TestCase):
    changes = {"heading": "What changed", "changes": [
        {"what": "Rolling resistance", "params": {"roll_friction": [0.008, 0.01]}, "why": "the lag."},
        {"what": "Cushion floor", "params": {"cushion_restitution_min": [None, 0.7]}, "why": "TP B.6."}]}

    def test_lines_and_values(self):
        lines, problems = report.what_changed(self.changes, {"roll_friction": 0.01, "cushion_restitution_min": 0.7})
        self.assertEqual(lines, ["- Rolling resistance (`roll_friction` 0.008 to 0.01): the lag.",
                                 "- Cushion floor (`cushion_restitution_min` new, 0.7): TP B.6."])
        self.assertEqual(problems, [])

    def test_a_value_that_moved_on_is_flagged(self):
        lines, problems = report.what_changed(self.changes, {"roll_friction": 0.012, "cushion_restitution_min": 0.7})
        self.assertIn("(`roll_friction` 0.008 to 0.01 (now 0.012))", lines[0])
        self.assertEqual(problems, ["what_changed.json gives roll_friction as 0.01; table_params.json has 0.012"])

    def test_the_committed_file_reads(self):
        changes = report.read_json(report.DEFAULT_CHANGES)
        lines, _ = report.what_changed(changes, PARAMS)
        self.assertGreater(len(lines), 3)
        self.assertTrue(all(line.startswith("- ") and "`" in line for line in lines))

    def test_missing_file(self):
        lines, problems = report.what_changed(None, PARAMS)
        self.assertEqual(problems, ["what_changed.json is missing or not a JSON object"])


class SwerveTest(unittest.TestCase):
    def test_slide_then_roll(self):
        s = make("swerve", ["variant", "elevation_deg", "tip_x", "t_s", "x_m", "y_m"],
                 swerve_rows("raised", 30.0, 0.4))
        path = report.swerve_paths(s)["raised"]
        p = report.swerve_phases(path, PULL)
        self.assertAlmostEqual(p["slide_s"], 0.21, places=9)
        self.assertAlmostEqual(p["accel_max"], PULL, places=6)
        self.assertAlmostEqual(p["accel_min"], PULL, places=6)
        self.assertLess(p["accel_spread_deg"], 1e-6)
        self.assertLess(p["turn_slide_deg"], -10.0)
        self.assertAlmostEqual(p["turn_roll_deg"], 0.0, places=6)
        self.assertTrue(p["bends_late"])
        self.assertAlmostEqual(p["roll_m"] + p["slide_m"],
                               sum(math.dist(a, b) for a, b in zip(path["points"], path["points"][1:])))

    def test_a_path_that_never_slides(self):
        rows = [["rolling", 30.0, 0.4, 0.01 * k, 0.01 * k, 0.0] for k in range(50)]
        s = make("swerve", ["variant", "elevation_deg", "tip_x", "t_s", "x_m", "y_m"], rows)
        self.assertIsNone(report.swerve_phases(report.swerve_paths(s)["rolling"], PULL))

    def test_a_wrong_way_swerve_is_not_met(self):
        rows = swerve_rows("wrong", 30.0, -0.4)
        s = make("swerve", ["variant", "elevation_deg", "tip_x", "t_s", "x_m", "y_m"],
                 [[r[0], r[1], 0.4, *r[3:]] for r in rows])
        verdict = report.evaluate_swerve(s, PULL)
        self.assertEqual(verdict.status, report.NOT_MET)
        self.assertIn("Against the side: wrong", verdict.sentence)


class RecordTest(unittest.TestCase):
    record = {
        "events": [{"t": 0.0, "kind": "strike", "a": 0, "b": -1, "body": -1},
                   {"t": 0.5, "kind": "ball_ball", "a": 0, "b": 1, "body": -1}],
        "segments": [[{"t0": 0.0, "start": {"r": [0.0, 0.0], "v": [2.0, 0.0], "w": [0, -10, 0], "motion": "sliding"}},
                      {"t0": 0.5, "start": {"r": [0.95, 0.0], "v": [0.1, 0.0], "w": [0, -5, 0], "motion": "sliding"}},
                      {"t0": 0.7, "start": {"r": [0.9, 0.0], "v": [-0.3, 0.0], "w": [0, -11, 0], "motion": "rolling"}}],
                     [{"t0": 0.0, "start": {"r": [1.0, 0.0], "v": [0, 0], "w": [0, 0, 0], "motion": "stationary"}}]],
        "final": {"balls": [{"r": [0.45, 0.0]}, {"r": [2.0, 0.0]}]},
    }

    def test_helpers(self):
        self.assertEqual(report.first_event(self.record, "ball_ball", 1)["t"], 0.5)
        self.assertIsNone(report.first_event(self.record, "ball_ball", 0, after=0.5))
        self.assertEqual(report.state_from(self.record, 0, 0.5)["r"], [0.95, 0.0])
        self.assertEqual(report.rolling_from(self.record, 0, 0.5)["v"], [-0.3, 0.0])
        self.assertAlmostEqual(report.run_after_contact(self.record), -0.5)
        no_contact = dict(self.record, events=self.record["events"][:1])
        self.assertIsNone(report.run_after_contact(no_contact))

    def test_phrases(self):
        self.assertEqual(report.after_contact_phrase(-0.3, 3.569), "comes back 30 cm")
        self.assertEqual(report.after_contact_phrase(0.053, 3.569), "runs on 5.3 cm")
        self.assertEqual(report.after_contact_phrase(-4.6, 3.569), "would come back 4.6 m on an endless table")
        self.assertEqual(report.after_contact_phrase(None, 3.569), "never reaches the red")
        self.assertEqual(report.short_distance(1.234), "1.2 m")

    def test_endless_table_scales_the_outline(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp) / "endless.json"
            report.endless_table(TABLE_FILE, out)
            big = json.loads(out.read_text(encoding="utf-8"))
        real = json.loads(TABLE_FILE.read_text(encoding="utf-8"))
        k = report.ENDLESS_SCALE
        self.assertEqual(big["lines"][0]["p1"], [c * k for c in real["lines"][0]["p1"]])
        self.assertEqual(big["lines"][0]["normal"], real["lines"][0]["normal"])
        self.assertEqual(big["arcs"][0]["radius"], real["arcs"][0]["radius"] * k)
        self.assertEqual(big["arcs"][0]["angle_start"], real["arcs"][0]["angle_start"])
        self.assertEqual(big["pockets"][0]["drop_radius"], real["pockets"][0]["drop_radius"] * k)


class FullReportTest(unittest.TestCase):
    def test_every_section_in_order(self):
        series = full_series()
        series["throw"] = throw(4.86, 3.39)
        series["throw"].summary = {"cue_ball_speeds_m_s": [0.5, 4.0]}
        text = report.assemble_report(series, {}, [], source="calib", shots=fake_shots(), params=PARAMS,
                                      fixtures=report.load_fixtures(),
                                      changes=report.read_json(report.DEFAULT_CHANGES))
        headings = [line for line in text.splitlines() if line.startswith("## ")]
        self.assertEqual(headings, ["## In plain words", "## Results against the targets", "## What changed",
                                    "## Charts"])
        self.assertIn("| C11 | Lag: baulk line to the top cushion and back to baulk |", text)
        self.assertIn("- Rolling resistance (`roll_friction` 0.008 to 0.01)", text)
        self.assertIn("### C10: Swerve", text)

    def test_without_shots_the_sections_say_so(self):
        text = report.assemble_report(full_series(), {}, [])
        self.assertIn("Not measured: the report's own shots did not run", text)
        self.assertIn("| C1 | Centre ball, no spin: slides, then rolls | not measured", text)
        self.assertIn("- what_changed.json is missing or not a JSON object", text)


class MainTest(unittest.TestCase):
    def test_needs_ofs_sim_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = pathlib.Path(tmp)
            (data / "lag.json").write_text(json.dumps(series_file("lag", ["a"], [[1]])), encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()) as out:
                code = report.main([str(data), str(data / "out"), "--sim-cli", str(data / "nowhere.exe")])
        self.assertEqual(code, 2)
        self.assertIn("ofs_sim_cli not found", out.getvalue())


@unittest.skipIf(CLI is None, "no ofs_sim_cli build; test_sim_cli.py says how to build it")
class ShotsThroughTheCliTest(unittest.TestCase):
    def test_the_report_shots_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            endless = pathlib.Path(tmp) / "endless.json"
            report.endless_table(TABLE_FILE, endless)
            sim = report.Sim(CLI, tmp, PARAMS_FILE)
            shots = report.Shots(sim, endless, TABLE_FILE, {"lag_cue_speed_m_s": 2.0, "start_x_m": -1.0475})
            stun = shots.full_ball(False)
            self.assertAlmostEqual(stun["ratio"], (1 - PARAMS["ball_restitution"]) / 2, delta=2e-3)
            self.assertGreater(stun["creep_m"], 0.0)
            self.assertAlmostEqual(report.speed_of(shots.struck(0.0, 2.0)["strike"]["v"]), 2.0, places=9)
            self.assertLess(report.run_after_contact(shots.struck(report.SCREW_TIP, 4.0, 0.5)), -1.0)
            self.assertGreater(shots.follow()[2.0], 0.5)
            lag = shots.lag_return()
            self.assertGreater(lag["t_baulk"], lag["t_top"])
            self.assertEqual(sim.anomalies, [])
            self.assertEqual(len(sim.params_hashes), 1)


if __name__ == "__main__":
    unittest.main()
