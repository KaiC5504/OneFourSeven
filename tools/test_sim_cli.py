"""Tests of ofs_sim_cli (tools/sim_cli) through its command line, against the Debug build (or the
build OFS_SIM_CLI names)."""
import json
import os
import pathlib
import re
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


def table_hash():
    """The content hash of core/data/table_shape.json, as simulate prints it."""
    result = run("simulate", STRAIGHT, "--no-strike")
    match = re.search(r"^params\s+\S+, table (\S+)$", result.stdout, re.MULTILINE)
    if result.returncode != 0 or not match:
        raise AssertionError("simulate printed no table hash:\n" + shown(result))
    return match.group(1)


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
        self.assertEqual(events.count("shot.simulated"), 1, events)
        shot = next(json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()
                    if json.loads(line)["event"] == "shot.simulated")
        self.assertEqual(shot["data"]["bundle"]["seed"], "7")

    def test_params_source_never_names_a_folder_outside_the_repo(self):
        params = self.tmp / "my_params.json"
        params.write_text((ROOT / "core" / "data" / "table_params.json").read_text(encoding="utf-8"),
                          encoding="utf-8")
        for given, source in ((None, "core/data/table_params.json"),
                              (ROOT / "core" / "data" / "table_params.json", "core/data/table_params.json"),
                              (params, "my_params.json")):
            with self.subTest(params=given):
                _, lines = self.simulate_with_log(*(("--params", given) if given else ()))
                loaded = next(line for line in lines if line["event"] == "params.loaded")
                self.assertEqual(loaded["data"]["source"], source)
                text = json.dumps(lines)
                for private in (str(self.tmp), pathlib.Path.home().name):
                    self.assertNotIn(private, text)

    def test_log_lines_name_the_core_version(self):
        version = re.search(r'core_version\(\) \{ return "([^"]+)"; \}',
                            (ROOT / "core" / "src" / "version.cpp").read_text(encoding="utf-8")).group(1)
        _, lines = self.simulate_with_log()
        for line in lines:
            self.assertRegex(line["build"], rf"^sim_cli-{re.escape(version)}(\+[0-9a-f]{{7,40}})?$")

    def test_the_bundle_names_the_table_it_ran_on(self):
        _, lines = self.simulate_with_log()
        bundle = next(line for line in lines if line["event"] == "shot.simulated")["data"]["bundle"]
        self.assertEqual(bundle["table_hash"], table_hash())

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
        self.assert_exit(result, 0)
        self.assertIn("shot.simulated   OK", result.stdout)
        self.assertNotIn("DIVERGED", result.stdout)

    def bundle_file(self, table_hash_value):
        """The straight_1m shot's {bundle, result} with its table_hash set, or removed for None."""
        _, lines = self.simulate_with_log()
        data = self.shot_line(lines)["data"]
        data["bundle"].pop("table_hash", None)
        if table_hash_value is not None:
            data["bundle"]["table_hash"] = table_hash_value
        path = self.tmp / "shot.json"
        path.write_text(json.dumps(data), encoding="utf-8")
        return path

    def test_update_rewrites_a_stale_result(self):
        path = self.bundle_file(None)
        stale = json.loads(path.read_text(encoding="utf-8"))
        stale["result"]["event_list"][0][0] += 1e-6
        path.write_text(json.dumps(stale), encoding="utf-8")
        self.assert_exit(run("replay", path), 1)
        result = run("replay", path, "--update")
        self.assert_exit(result, 0)
        self.assertIn("UPDATED", result.stdout)
        updated = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(table_hash(), updated["bundle"]["table_hash"])
        result = run("replay", path)
        self.assert_exit(result, 0)
        self.assertIn("OK", result.stdout)

    def test_update_refuses_a_log(self):
        log, _ = self.simulate_with_log()
        self.assert_exit(run("replay", log, "--update"), 2)

    def test_a_bundle_from_another_table_is_refused(self):
        path = self.bundle_file("0123456789abcdef")
        result = run("replay", path)
        self.assert_exit(result, 1)
        self.assertIn("ERROR        table mismatch: the bundle ran on table 0123456789abcdef, but "
                      f"core/data/table_shape.json is {table_hash()}", result.stdout)
        self.assertIn("--force-table", result.stdout)
        self.assertIn("1 errors", result.stdout)

        forced = run("replay", path, "--force-table")
        self.assert_exit(forced, 0)
        self.assertIn("warning: the bundle ran on table 0123456789abcdef", forced.stdout)
        self.assertIn("file     bundle           OK", forced.stdout)

    def test_a_bundle_from_this_table_replays_without_a_warning(self):
        result = run("replay", self.bundle_file(table_hash()))
        self.assert_exit(result, 0)
        self.assertIn("OK", result.stdout)
        self.assertNotIn("warning", result.stdout)

    def test_another_table_file_is_checked_against_the_bundle(self):
        table = json.loads((ROOT / "core" / "data" / "table_shape.json").read_text(encoding="utf-8"))
        table["_note"] += " A different file, so a different hash."
        other = self.tmp / "other_table.json"
        other.write_text(json.dumps(table), encoding="utf-8")
        result = run("replay", self.bundle_file(table_hash()), "--table", other)
        self.assert_exit(result, 1)
        self.assertIn("but other_table.json is", result.stdout)

    def test_a_bundle_without_a_table_hash_replays_with_a_warning(self):
        result = run("replay", self.bundle_file(None))
        self.assert_exit(result, 0)
        self.assertIn("warning: the bundle has no table_hash, so its table can't be checked; replaying on "
                      f"core/data/table_shape.json ({table_hash()})", result.stdout)
        self.assertIn("OK", result.stdout)

    def test_a_table_hash_that_is_not_a_string_is_an_error(self):
        result = run("replay", self.bundle_file(12))
        self.assert_exit(result, 1)
        self.assertIn("table_hash: want a string", result.stdout)

    def test_tampered_result_diverges_and_every_line_is_reported(self):
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


SHOTS = 17
BIG_SHOT = "break_7.0ms_rack2"


class BenchTest(CliTestCase):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        # One measured baseline for every test to edit: a bench run is the slow part.
        cls._baseline_dir = tempfile.TemporaryDirectory()
        cls.measured = pathlib.Path(cls._baseline_dir.name) / "baseline.json"
        cls.written = run("bench", "--runs", 2, "--write-baseline", "--baseline", cls.measured, timeout=300)

    @classmethod
    def tearDownClass(cls):
        cls._baseline_dir.cleanup()
        super().tearDownClass()

    def baseline(self, edit):
        data = json.loads(self.measured.read_text(encoding="utf-8"))
        edit(data)
        path = self.tmp / "baseline.json"
        path.write_text(json.dumps(data), encoding="utf-8")
        return path

    def check(self, baseline, *extra):
        return run("bench", "--runs", 2, "--check", "--budget-us", 1e12, "--baseline", baseline, *extra, timeout=300)

    def regressed(self, result):
        return [line.split()[0] for line in result.stdout.splitlines() if "REGRESSED" in line]

    def test_lists_every_shot_and_writes_a_baseline(self):
        self.assert_exit(self.written, 0)
        for name in ("break_4.5ms_rack1", "break_10.0ms_rack3", "long_multi_cushion", "cluster_split",
                     "pot_black_corner", "slow_roll_nap", "pack_break_off", "pack_slow_roll"):
            self.assertIn(name, self.written.stdout)
        self.assertNotIn("check:", self.written.stdout)
        data = json.loads(self.measured.read_text(encoding="utf-8"))
        self.assertEqual(len(data["shots"]), SHOTS)
        self.assertEqual(data["version"], 2)
        self.assertEqual(data["build"], "debug")
        self.assertRegex(data["machine"], r", \d+ logical cores$")
        self.assertGreater(data["probe_us"], 0)
        for shot in data["shots"].values():
            self.assertGreater(shot["probe_units"], 0)
        # The bench runs the recorded pack shots themselves: same events as their stored result.
        for name in ("pack_break_off", "pack_slow_roll"):
            recorded = json.loads((ROOT / "tools" / "sim_cli" / "bench_shots" / f"{name}.json").read_text(encoding="utf-8"))
            self.assertEqual(data["shots"][name]["events"], recorded["result"]["events"])

    def test_check_reports_a_regression_against_the_baseline(self):
        def edit(data):
            for shot in data["shots"].values():
                shot["probe_units"] = 1e12
            data["shots"][BIG_SHOT]["probe_units"] = 1e-9
        result = self.check(self.baseline(edit))
        self.assert_exit(result, 1)
        self.assertEqual(self.regressed(result), [BIG_SHOT], result.stdout)
        self.assertIn("check: 0 over the 1000000000000 us budget, 1 regressed more than 25% + 50 us", result.stdout)

    def test_the_absolute_slack_absorbs_differences_of_a_few_microseconds(self):
        # Every shot ten times its baseline: the slack is 50 us at the baseline machine's speed, so with
        # that machine's probe a million times faster every difference is far below it.
        def edit(data, probe_scale):
            data["probe_us"] *= probe_scale
            for shot in data["shots"].values():
                shot["probe_units"] /= 10.0
        absorbed = self.check(self.baseline(lambda data: edit(data, 1e-6)))
        self.assert_exit(absorbed, 0)
        self.assertEqual(self.regressed(absorbed), [])
        caught = self.check(self.baseline(lambda data: edit(data, 1.0)))
        self.assert_exit(caught, 1)
        self.assertIn(BIG_SHOT, self.regressed(caught))

    def test_a_shot_missing_from_the_baseline_is_new(self):
        def edit(data):
            del data["shots"]["pack_slow_roll"]
            for shot in data["shots"].values():
                shot["probe_units"] = 1e12
        result = self.check(self.baseline(edit))
        self.assert_exit(result, 0)
        line = next(line for line in result.stdout.splitlines() if line.startswith("pack_slow_roll"))
        self.assertIn("(new)", line)
        self.assertNotIn("REGRESSED", line)

    def test_a_baseline_from_another_machine_or_build_is_only_reported(self):
        def regressing(data):
            for shot in data["shots"].values():
                shot["probe_units"] = 1e-9
        cases = (({"machine": "Another CPU, 4 logical cores"}, "the baseline was recorded on Another CPU, 4 logical "
                  "cores, not this machine"),
                 ({"build": "release"}, "the baseline was recorded with a release build, this is a debug build"),
                 ({"version": 1, "machine": None}, "the baseline names no machine or speed probe (written before "
                  "baseline version 2)"))
        for change, why in cases:
            with self.subTest(change=change):
                def edit(data):
                    regressing(data)
                    for key, value in change.items():
                        if value is None:
                            del data[key]
                        else:
                            data[key] = value
                result = self.check(self.baseline(edit))
                self.assert_exit(result, 0)
                self.assertEqual(self.regressed(result), [])
                self.assertIn("note: no regression check: " + why, result.stdout)
                self.assertIn("regressions not checked: " + why, result.stdout)

    def test_the_committed_baseline_reads(self):
        result = run("bench", "--runs", 1, timeout=300)
        self.assert_exit(result, 0)
        self.assertIn("bench_baseline.json", result.stdout)

    def test_check_reports_the_budget(self):
        result = run("bench", "--runs", 2, "--check", "--budget-us", 1e-3, "--baseline", self.tmp / "none.json",
                     timeout=300)
        self.assert_exit(result, 1)
        self.assertEqual(result.stdout.count("OVER BUDGET"), SHOTS)

    def test_the_recorded_shots_replay(self):
        for name in ("pack_break_off", "pack_slow_roll"):
            with self.subTest(shot=name):
                result = run("replay", ROOT / "tools" / "sim_cli" / "bench_shots" / f"{name}.json", timeout=300)
                self.assert_exit(result, 0)
                self.assertIn("OK", result.stdout)


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

    def test_each_series_records_its_setup(self):
        out = self.tmp / "calibration"
        self.assert_exit(run("calibrate", "--out", out, "--breaks", 1, timeout=120), 0)
        summary = {path.stem: json.loads(path.read_text(encoding="utf-8"))["summary"] for path in out.glob("*.json")}
        self.assertEqual(summary["squirt"]["cue_speed_m_s"], 3.0)
        self.assertEqual(summary["swerve"]["cue_speed_m_s"], 1.5)
        self.assertEqual(summary["deflection"]["cue_ball_speed_m_s"], 1.0)
        self.assertEqual(summary["cushion_side"]["cue_ball_speed_m_s"], 1.5)
        self.assertEqual(summary["cushion_side"]["speed_m_s"], 1.5)
        self.assertEqual(summary["throw"]["cue_ball_speeds_m_s"], [0.5, 1.5, 4.0])
        self.assertEqual(summary["cushion_rebound"]["cue_ball_speeds_m_s"], [0.5, 1.0, 2.0, 4.0])
        self.assertEqual(summary["screw_follow"]["cue_speeds_m_s"], [1.0, 2.0, 3.0])
        for name in ("squirt", "swerve", "lag", "screw_follow"):
            self.assertIn("start_x_m", summary[name], name)
            self.assertIn("start_y_m", summary[name], name)

    def test_the_level_swerve_shot_is_level(self):
        out = self.tmp / "calibration"
        self.assert_exit(run("calibrate", "--out", out, "--breaks", 1, timeout=120), 0)
        used = json.loads((out / "swerve.json").read_text(encoding="utf-8"))["summary"]["elevation_used_deg"]
        self.assertEqual(used["0 deg, tip +0.40"], 0.0)

    def test_screw_and_follow_play_on_an_open_table(self):
        out = self.tmp / "calibration"
        self.assert_exit(run("calibrate", "--out", out, "--breaks", 1, timeout=120), 0)
        data = json.loads((out / "screw_follow.json").read_text(encoding="utf-8"))
        self.assertEqual(data["summary"]["table_scale"], 20.0)
        # On the real table the top cushion is 1.3 m past the red, so no follow could run half its length.
        follow = max(row[2] for row in data["rows"] if row[2] is not None)
        self.assertGreater(follow, data["summary"]["playing_length_m"] / 2.0)


if __name__ == "__main__":
    unittest.main()
