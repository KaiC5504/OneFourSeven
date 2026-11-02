"""Tests of ofs_sim_cli (tools/sim_cli) through its command line, against the Debug build.

They pass on the M1 contracts' stubbed physics and on the real physics. Where a check needs the
real simulator (a replay that matches, a tampered result that diverges), the stubbed run asserts what
the CLI does without it and then skips the rest.
"""
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

import calibration_report
import check_logs

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "windows" / "tools" / "sim_cli"
FIXTURES = ROOT / "core" / "tests" / "fixtures" / "positions"
STRAIGHT = FIXTURES / "straight_1m.json"


def find_cli():
    """OFS_SIM_CLI, else the Debug build's ofs_sim_cli."""
    named = os.environ.get("OFS_SIM_CLI")
    if named:
        return pathlib.Path(named)
    name = "ofs_sim_cli.exe" if sys.platform == "win32" else "ofs_sim_cli"
    for path in (BUILD / "Debug" / name, BUILD / name):
        if path.is_file():
            return path
    return None


CLI = find_cli()


def run(*args, timeout=60):
    return subprocess.run([str(CLI), *map(str, args)], capture_output=True, text=True, encoding="utf-8",
                          errors="replace", timeout=timeout)


def shown(result):
    return f"exit {result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"


_stubbed = None


def physics_stubbed():
    """True while the physics is the contracts' stub: its record_json is an empty object."""
    global _stubbed
    if _stubbed is None:
        with tempfile.TemporaryDirectory() as tmp:
            record = pathlib.Path(tmp) / "record.json"
            result = run("simulate", STRAIGHT, "--out", record)
            if result.returncode != 0:
                raise AssertionError("simulate failed on straight_1m:\n" + shown(result))
            _stubbed = json.loads(record.read_text(encoding="utf-8")) == {}
    return _stubbed


class CliTestCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if CLI is None or not CLI.is_file():
            raise AssertionError(f"ofs_sim_cli not found ({CLI or 'nothing in ' + str(BUILD)}): build it with "
                                 "`cmake --build build/windows --config Debug --target ofs_sim_cli` (or run "
                                 "python scripts/gate.py), or set OFS_SIM_CLI to its path")

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = pathlib.Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def assert_exit(self, result, code):
        self.assertEqual(result.returncode, code, shown(result))

    def simulate_with_log(self, *extra):
        log = self.tmp / "shot.jsonl"
        result = run("simulate", STRAIGHT, "--seed", 7, "--log", log, *extra)
        self.assert_exit(result, 0)
        return log, [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]

    def require_real_physics(self):
        if physics_stubbed():
            self.skipTest("the physics is still the M1 contracts' stub")


class ArgumentTest(CliTestCase):
    def test_no_command_prints_usage(self):
        result = run()
        self.assert_exit(result, 2)
        self.assertIn("usage: ofs_sim_cli", result.stderr)

    def test_help_lists_every_command(self):
        result = run("help")
        self.assert_exit(result, 0)
        for command in ("simulate", "replay", "fuzz", "bench", "calibrate"):
            self.assertIn(command, result.stdout)

    def test_unknown_command(self):
        result = run("explode")
        self.assert_exit(result, 2)
        self.assertIn("unknown command 'explode'", result.stderr)

    def test_unknown_option(self):
        result = run("simulate", STRAIGHT, "--bogus", "1")
        self.assert_exit(result, 2)
        self.assertIn("unknown option --bogus", result.stderr)

    def test_option_without_value(self):
        result = run("fuzz", "--shots")
        self.assert_exit(result, 2)
        self.assertIn("--shots needs a value", result.stderr)

    def test_bad_numbers(self):
        for args in (("fuzz", "--shots", "many"), ("fuzz", "--shots", "0"), ("fuzz", "--threads", "-2"),
                     ("simulate", STRAIGHT, "--seed", "1.5"), ("replay", STRAIGHT, "--tolerance", "nan"),
                     ("bench", "--runs", "1e3")):
            with self.subTest(args=args):
                result = run(*args)
                self.assert_exit(result, 2)
                self.assertIn(f"{args[-2]}: '{args[-1]}'", result.stderr)

    def test_extra_argument(self):
        result = run("simulate", STRAIGHT, STRAIGHT)
        self.assert_exit(result, 2)
        self.assertIn("unexpected argument", result.stderr)

    def test_missing_position_file(self):
        result = run("simulate", self.tmp / "nowhere.json")
        self.assert_exit(result, 2)
        self.assertIn("cannot open", result.stderr)

    def test_position_that_is_not_a_table(self):
        path = self.tmp / "bad.json"
        path.write_text(json.dumps({"start": {"balls": [{"ball": "purple", "r": [0, 0]}]}}), encoding="utf-8")
        result = run("simulate", path)
        self.assert_exit(result, 2)
        self.assertIn("balls[0]", result.stderr)

    def test_bad_strike(self):
        for strike, fragment in (("{nope", "--strike is not JSON"), ("[1]", "want a JSON object"),
                                 ('{"spin": 1}', "unknown key 'spin'"), ('{"speed_m_s": "fast"}', "speed_m_s")):
            with self.subTest(strike=strike):
                result = run("simulate", STRAIGHT, "--strike", strike)
                self.assert_exit(result, 2)
                self.assertIn(fragment, result.stderr)

    def test_cue_ball_out_of_range(self):
        fixture = json.loads(STRAIGHT.read_text(encoding="utf-8"))
        fixture["cue_ball"] = 5
        path = self.tmp / "cue5.json"
        path.write_text(json.dumps(fixture), encoding="utf-8")
        result = run("simulate", path)
        self.assert_exit(result, 2)
        self.assertIn("cue_ball 5", result.stderr)

    def test_bad_rack_seed(self):
        result = run("simulate", "rack:abc")
        self.assert_exit(result, 2)
        self.assertIn("rack:SEED", result.stderr)

    def test_invalid_params_file_lists_every_problem(self):
        params = json.loads((ROOT / "core" / "data" / "table_params.json").read_text(encoding="utf-8"))
        params["values"]["ball_restitution"] = 2.0
        params["values"]["max_events"] = 1.5
        path = self.tmp / "params.json"
        path.write_text(json.dumps(params), encoding="utf-8")
        result = run("simulate", STRAIGHT, "--params", path)
        self.assert_exit(result, 2)
        self.assertIn("ball_restitution", result.stderr)
        self.assertIn("max_events", result.stderr)

    def test_calibrate_needs_out(self):
        result = run("calibrate")
        self.assert_exit(result, 2)
        self.assertIn("--out DIR is required", result.stderr)


class SimulateTest(CliTestCase):
    def test_writes_record_and_log(self):
        record = self.tmp / "out" / "record.json"
        log = self.tmp / "out" / "shot.jsonl"
        result = run("simulate", STRAIGHT, "--seed", 7, "--out", record, "--log", log)
        self.assert_exit(result, 0)
        for line in ("position   straight_1m: 2 balls", "events", "t_end", "potted", "anomalies"):
            self.assertIn(line, result.stdout)
        self.assertIsInstance(json.loads(record.read_text(encoding="utf-8")), dict)
        checked = subprocess.run([sys.executable, str(ROOT / "tools" / "check_logs.py"), str(log)],
                                 capture_output=True, text=True)
        self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
        events = [json.loads(line)["event"] for line in log.read_text(encoding="utf-8").splitlines()]
        self.assertEqual(events[0], "params.loaded")
        if not physics_stubbed():
            self.assertEqual(events.count("shot.simulated"), 1, events)
            shot = next(json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()
                        if json.loads(line)["event"] == "shot.simulated")
            self.assertEqual(shot["data"]["bundle"]["seed"], "7")

    def test_strike_overrides_one_key(self):
        result = run("simulate", STRAIGHT, "--strike", '{"speed_m_s": 0.5}')
        self.assert_exit(result, 0)
        self.assertIn("aim 0.000000 rad, tip (0.000, 0.000) R, elevation 0.0000 rad, speed 0.5000 m/s",
                      result.stdout)

    def test_no_strike(self):
        result = run("simulate", STRAIGHT, "--no-strike")
        self.assert_exit(result, 0)
        self.assertIn("strike     none", result.stdout)

    def test_break_off_from_a_rack(self):
        result = run("simulate", "rack:3")
        self.assert_exit(result, 0)
        self.assertIn("rack:3: 22 balls", result.stdout)


class ReplayTest(CliTestCase):
    def shot_line(self, lines):
        return next(line for line in lines if line["event"] == "shot.simulated")

    def write_jsonl(self, name, lines):
        path = self.tmp / name
        path.write_text("".join(json.dumps(line) + "\n" for line in lines), encoding="utf-8")
        return path

    def test_replays_its_own_log(self):
        log, lines = self.simulate_with_log()
        result = run("replay", log)
        if physics_stubbed():
            self.assert_exit(result, 2)
            self.assertIn("no shot.simulated or physics.anomaly lines", result.stderr)
            self.skipTest("the stubbed physics logs no shot.simulated")
        self.assert_exit(result, 0)
        self.assertIn("shot.simulated   OK", result.stdout)
        self.assertNotIn("DIVERGED", result.stdout)

    def test_tampered_result_diverges_and_every_line_is_reported(self):
        self.require_real_physics()
        _, lines = self.simulate_with_log()
        shot = self.shot_line(lines)
        tampered = json.loads(json.dumps(shot))
        tampered["data"]["result"]["final"]["balls"][0]["r"][0] += 0.01
        path = self.write_jsonl("tampered.jsonl", [shot, tampered])
        result = run("replay", path)
        self.assert_exit(result, 1)
        self.assertIn("line 1   shot.simulated   OK", result.stdout)
        self.assertIn("line 2   shot.simulated   DIVERGED", result.stdout)
        self.assertIn("1 ok, 1 diverged", result.stdout)

        only_good = run("replay", path, "--line", 1)
        self.assert_exit(only_good, 0)
        loose = run("replay", path, "--line", 2, "--tolerance", 0.1)
        self.assert_exit(loose, 0)

    def test_line_out_of_range(self):
        _, lines = self.simulate_with_log()
        path = self.write_jsonl("one.jsonl", lines)
        result = run("replay", path, "--line", 99)
        self.assert_exit(result, 2)
        self.assertIn(f"has {len(lines)} lines", result.stderr)

    def test_line_that_is_not_a_shot(self):
        _, lines = self.simulate_with_log()
        path = self.write_jsonl("one.jsonl", lines)
        result = run("replay", path, "--line", 1)
        self.assert_exit(result, 2)
        self.assertIn("line 1 is not a shot.simulated", result.stderr)

    def test_bare_bundle(self):
        self.require_real_physics()
        _, lines = self.simulate_with_log()
        path = self.tmp / "bundle.json"
        path.write_text(json.dumps(self.shot_line(lines)["data"]["bundle"]), encoding="utf-8")
        result = run("replay", path)
        self.assert_exit(result, 0)
        self.assertIn("REPLAYED", result.stdout)

    def test_malformed_bundle_is_an_error(self):
        path = self.tmp / "bundle.json"
        path.write_text(json.dumps({"v": 1, "start": "no"}), encoding="utf-8")
        result = run("replay", path)
        self.assert_exit(result, 1)
        self.assertIn("ERROR        bundle unreadable", result.stdout)

    def test_anomaly_that_does_not_reproduce(self):
        self.require_real_physics()
        _, lines = self.simulate_with_log()
        shot = self.shot_line(lines)
        anomaly = dict(shot, subsystem="physics", level="warn", event="physics.anomaly",
                       data={"kind": "time_cap", "t": 1.0, "balls": [0], "detail": "made up",
                             "bundle": shot["data"]["bundle"]})
        path = self.write_jsonl("anomaly.jsonl", [anomaly])
        result = run("replay", path)
        self.assert_exit(result, 1)
        self.assertIn("NOT REPRODUCED", result.stdout)


class FuzzTest(CliTestCase):
    def fuzz(self, *args, codes=(0,)):
        result = run("fuzz", "--out", self.tmp / "failures", *args, timeout=120)
        self.assertIn(result.returncode, codes, shown(result))
        return result.stdout

    def line(self, output, prefix):
        return next(line for line in output.splitlines() if line.startswith(prefix))

    def test_a_few_shots_pass_with_a_summary(self):
        output = self.fuzz("--shots", 30, "--seed", 1, "--threads", 2)
        for prefix in ("shots 30 of 30", "failures by kind: none", "determinism (C13): 1 shots", "events:",
                       "sim time:", "speed:", "inputs digest:"):
            self.assertIn(prefix, output)

    def test_the_shot_set_depends_on_the_seed_not_the_threads(self):
        # Failing shots don't matter here (the test above covers them), only that the results agree.
        one = self.fuzz("--shots", 24, "--seed", 5, "--threads", 1, codes=(0, 1))
        three = self.fuzz("--shots", 24, "--seed", 5, "--threads", 3, codes=(0, 1))
        other = self.fuzz("--shots", 24, "--seed", 6, "--threads", 3, codes=(0, 1))
        for prefix in ("inputs digest:", "events:", "shots 24", "failures by kind:"):
            self.assertEqual(self.line(one, prefix), self.line(three, prefix))
        self.assertNotEqual(self.line(one, "inputs digest:"), self.line(other, "inputs digest:"))


class BenchTest(CliTestCase):
    def test_lists_every_shot(self):
        result = run("bench", "--runs", 2, "--baseline", self.tmp / "none.json", timeout=120)
        self.assert_exit(result, 0)
        for name in ("break_4.5ms_rack1", "break_10.0ms_rack3", "long_multi_cushion", "cluster_split",
                     "pot_black_corner", "slow_roll_nap"):
            self.assertIn(name, result.stdout)
        self.assertNotIn("check:", result.stdout)

    def test_check_reports_a_regression_against_the_baseline(self):
        baseline = self.tmp / "baseline.json"
        written = run("bench", "--runs", 2, "--write-baseline", "--baseline", baseline, timeout=120)
        self.assert_exit(written, 0)
        data = json.loads(baseline.read_text(encoding="utf-8"))
        self.assertEqual(len(data["shots"]), 15)
        for shot in data["shots"].values():
            shot["median_us"] = 1e12
        data["shots"]["cluster_split"]["median_us"] = 1e-6
        baseline.write_text(json.dumps(data), encoding="utf-8")
        result = run("bench", "--runs", 2, "--check", "--budget-us", 1e12, "--baseline", baseline, timeout=120)
        self.assert_exit(result, 1)
        regressed = [line for line in result.stdout.splitlines() if "REGRESSED" in line]
        self.assertEqual(len(regressed), 1, result.stdout)
        self.assertTrue(regressed[0].startswith("cluster_split"))
        self.assertIn("check: 0 over the", result.stdout)

    def test_check_reports_the_budget(self):
        result = run("bench", "--runs", 2, "--check", "--budget-us", 1e-3, "--baseline", self.tmp / "none.json",
                     timeout=120)
        self.assert_exit(result, 1)
        self.assertEqual(result.stdout.count("OVER BUDGET"), 15)


class CalibrateTest(CliTestCase):
    def test_writes_series_the_report_reads(self):
        out = self.tmp / "calibration"
        result = run("calibrate", "--out", out, "--breaks", 3, timeout=120)
        self.assert_exit(result, 0)
        series, problems = calibration_report.load_series(out)
        self.assertEqual(problems, [])
        self.assertEqual(set(series), set(calibration_report.ORDER))
        params_hash = result.stdout.splitlines()[-1].split()[1].rstrip(",")
        for s in series.values():
            self.assertEqual(s.params_hash, params_hash, s.name)
        self.assertEqual(len(series["break_sim_time"].rows), 3)
        report = calibration_report.assemble_report(series, {}, problems)
        for s in series.values():
            self.assertIn(s.title, report)


if __name__ == "__main__":
    unittest.main()
