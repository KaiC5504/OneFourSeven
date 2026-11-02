"""Tests of calibration_report.py's loading, verdicts and report text, without matplotlib."""
import contextlib
import io
import json
import math
import pathlib
import sys
import tempfile
import unittest

import calibration_report as report


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
                         summary=data["summary"], notes=data["notes"])


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
        self.assertIn("at 1 m/s from -0.30 m (tip -0.5) to +0.40 m (tip +0.5)", sentence)
        self.assertIn("at 2 m/s", sentence)

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

    def test_empty_report(self):
        text = report.assemble_report({}, {}, ["nothing.json: not readable JSON"])
        self.assertIn("There is no calibration data to report.", text)
        self.assertIn("- nothing.json: not readable JSON", text)

    def test_anchor_matches_github_headings(self):
        s = make("lag", ["x"], [[1]], target="C11", title="The lag: cue speed to reach the top cushion")
        self.assertEqual(report.anchor(s), "c11-the-lag-cue-speed-to-reach-the-top-cushion")


if __name__ == "__main__":
    unittest.main()
