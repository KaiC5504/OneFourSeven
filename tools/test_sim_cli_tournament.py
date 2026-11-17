"""Tests of ofs_sim_cli's tournament command and tools/opponent_report.py through the command line,
against the build test_sim_cli.py finds (the Debug build, or the one OFS_SIM_CLI names)."""
import copy
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

import check_logs
from test_sim_cli import CliTestCase, ROOT, run, shown

DATA = ROOT / "core" / "data"
BEGINNER = DATA / "opponent_beginner.json"
EXHIBITIONS = DATA / "exhibitions.json"
REPORT_TOOL = ROOT / "tools" / "opponent_report.py"


def without_timings(report):
    """The report less what depends on the PC: the thinking times and the wall clock."""
    report = copy.deepcopy(report)
    report.pop("timing", None)
    for side in ("a", "b"):
        if report.get(side):
            for key in ("us_mean", "us_p99", "us_max"):
                report[side]["think"].pop(key)
    return report


class TournamentCase(CliTestCase):
    """One logged run of six frames of Beginner against itself, shared by the class's tests."""

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls._shared = tempfile.TemporaryDirectory()
        folder = pathlib.Path(cls._shared.name)
        cls.report_file = folder / "report.json"
        cls.log = folder / "tournament.jsonl"
        cls.result = run("tournament", "--a", "beginner", "--b", "beginner", "--frames", 6, "--seed", 3,
                         "--threads", 2, "--out", folder / "failures", "--report", cls.report_file, "--log", cls.log,
                         timeout=300)
        if cls.result.returncode != 0:
            raise AssertionError("the shared tournament run failed:\n" + shown(cls.result))
        cls.report = json.loads(cls.report_file.read_text(encoding="utf-8"))
        cls.lines = [json.loads(line) for line in cls.log.read_text(encoding="utf-8").splitlines()]

    @classmethod
    def tearDownClass(cls):
        cls._shared.cleanup()

    def tournament(self, *args, frames=6, seed=3, threads=2, code=0):
        report = self.tmp / "again.json"
        result = run("tournament", "--frames", frames, "--seed", seed, "--threads", threads, "--out",
                     self.tmp / "failures", "--report", report, *args, timeout=300)
        self.assert_exit(result, code)
        return json.loads(report.read_text(encoding="utf-8"))


class TournamentTest(TournamentCase):
    def test_six_frames_end_with_sane_counts(self):
        report = self.report
        self.assertEqual(report["frames"], 6)
        self.assertEqual(report["seed"], "3")
        self.assertEqual(report["failures"], [])
        self.assertEqual(report["exhibitions"], [])
        self.assertEqual(report["levels"]["a"]["id"], "beginner")
        self.assertEqual(report["levels"]["b"]["id"], "beginner")
        self.assertEqual(report["a"]["frames_won"] + report["b"]["frames_won"], 6)
        for side in ("a", "b"):
            with self.subTest(side=side):
                stats = report[side]
                self.assertGreater(stats["strokes"], 20)
                pots = stats["pots"]
                self.assertLessEqual(pots["attempts"], stats["strokes"])
                self.assertLessEqual(pots["made"], pots["attempts"])
                self.assertEqual(sum(band["attempts"] for band in pots["bands"].values()), pots["attempts"])
                self.assertEqual(sum(band["made"] for band in pots["bands"].values()), pots["made"])
                self.assertEqual(set(pots["bands"]), {"short", "medium", "long", "unknown"})
                breaks = stats["breaks"]
                self.assertGreater(breaks["count"], 0)
                self.assertLessEqual(breaks["median"], breaks["highest"])
                self.assertGreaterEqual(breaks["mean"], 1)
                self.assertLessEqual(stats["safeties"] + stats["escapes"], stats["strokes"])
                self.assertLessEqual(stats["fouls"], stats["strokes"])
                think = stats["think"]
                self.assertEqual(set(think["stopped"]), {"done", "sims", "steps", "time"})
                self.assertGreaterEqual(sum(think["stopped"].values()), stats["strokes"])
                self.assertLessEqual(think["us_mean"], think["us_max"])
        for name in ("frames won", "pots", "breaks (count)", "think us mean/p99/max", "failures: 0"):
            self.assertIn(name, self.result.stdout)
        self.assertIn("a: beginner (Beginner), core/data/opponent_beginner.json, hash ", self.result.stdout)

    def test_the_same_options_give_the_same_report_whatever_the_threads(self):
        one = self.tournament("--a", "beginner", "--b", "beginner", threads=1)
        self.assertEqual(without_timings(one), without_timings(self.report))
        self.assertEqual(one["timing"]["threads"], 1)
        two = self.tournament("--a", "beginner", "--b", "beginner", threads=2)
        self.assertEqual(without_timings(two), without_timings(self.report))
        other = self.tournament("--a", "beginner", "--b", "beginner", seed=4)
        self.assertNotEqual(without_timings(other)["a"], without_timings(self.report)["a"])

    def test_a_copy_of_the_level_file_plays_as_the_level(self):
        copied = self.tmp / "my_beginner.json"
        copied.write_text(BEGINNER.read_text(encoding="utf-8"), encoding="utf-8")
        report = self.tournament("--a-file", copied, "--b", "beginner")
        self.assertEqual(without_timings(report), without_timings(self.report))

    def test_unknown_and_malformed_levels_exit_2(self):
        broken = json.loads(BEGINNER.read_text(encoding="utf-8"))
        del broken["values"]["aim_sigma_rad"]
        missing_value = self.tmp / "missing_value.json"
        missing_value.write_text(json.dumps(broken), encoding="utf-8")
        out_of_range = copy.deepcopy(json.loads(BEGINNER.read_text(encoding="utf-8")))
        out_of_range["values"]["speeds"] = 0
        too_few = self.tmp / "too_few.json"
        too_few.write_text(json.dumps(out_of_range), encoding="utf-8")
        not_json = self.tmp / "not_json.json"
        not_json.write_text("{ not json", encoding="utf-8")
        for args, message in ((("--a", "nosuch", "--b", "beginner"), "unknown level 'nosuch'"),
                              (("--a", "../table_params", "--b", "beginner"), "is not a level id"),
                              (("--a-file", missing_value, "--b", "beginner"), "aim_sigma_rad: missing"),
                              (("--a", "beginner", "--b-file", too_few), "speeds: outside"),
                              (("--a-file", not_json, "--b", "beginner"), "--a:"),
                              (("--a-file", self.tmp / "absent.json", "--b", "beginner"), "--a:"),
                              (("--b", "beginner"), "--a ID or --a-file PATH is needed"),
                              (("--a", "beginner", "--a-file", BEGINNER, "--b", "beginner"), "both name a level"),
                              (("--a", "beginner"), "--b ID or --b-file PATH is needed"),
                              (("--a", "beginner", "--b", "beginner", "--threads", "5"), "--threads: '5'"),
                              (("--a", "beginner", "--b", "beginner", "--frames", "0"), "--frames 0"),
                              (("--a", "beginner", "--b", "beginner", "--bogus", "1"), "unknown option --bogus")):
            with self.subTest(args=args):
                result = run("tournament", *args, "--out", self.tmp / "failures")
                self.assert_exit(result, 2)
                self.assertIn(message, result.stderr)

    def test_exhibitions_play_every_table_in_the_file(self):
        layouts = json.loads(EXHIBITIONS.read_text(encoding="utf-8"))["layouts"]
        two = {"schema_version": 1, "layouts": [layouts[0], dict(layouts[0], name="the same again")]}
        tables = self.tmp / "tables.json"
        tables.write_text(json.dumps(two), encoding="utf-8")
        for file, names in ((None, [layout["name"] for layout in layouts]),
                            (tables, [layouts[0]["name"], "the same again"])):
            with self.subTest(file=file):
                args = ("--exhibitions-file", file) if file else ()
                result = run("tournament", "--a", "beginner", "--frames", 0, "--exhibitions", *args, "--out",
                             self.tmp / "failures", "--report", self.tmp / "x.json", timeout=120)
                self.assert_exit(result, 0)
                report = json.loads((self.tmp / "x.json").read_text(encoding="utf-8"))
                self.assertEqual([e["name"] for e in report["exhibitions"]], names)
                for e in report["exhibitions"]:
                    self.assertGreaterEqual(e["strokes"], 1)
                    self.assertGreaterEqual(e["break_points"], 0)
                    self.assertEqual(e["maximum"], e["break_points"] >= 147)
                    self.assertRegex(result.stdout, rf"exhibition {e['name']}\s+break\s+{e['break_points']} ")
                self.assertIsNone(report["b"])

    def test_the_log_passes_the_schema_the_referee_and_the_opponent(self):
        count, bad = check_logs.check_file(self.log, check_logs.load_schema())
        self.assertEqual(bad, [])
        self.assertEqual(count, len(self.lines))
        events = [line["event"] for line in self.lines]
        self.assertEqual(events[:3], ["params.loaded"] * 3)
        for event in ("match.started", "match.ended", "frame.ended"):
            self.assertEqual(events.count(event), 6, event)
        decisions = [line["data"] for line in self.lines if line["event"] == "opponent.decision"]
        strokes = sum(self.report[side]["strokes"] for side in ("a", "b"))
        self.assertEqual(sum(d["kind"] == "shot" for d in decisions), strokes)
        for d in decisions:
            self.assertEqual(d["level"], "beginner")
            self.assertRegex(d["seed"], r"^\d+$")
            if d["kind"] == "shot":
                self.assertIn("meant", d)
                self.assertEqual(set(d["think"]), {"candidates", "sims", "steps", "depth", "stopped", "ms"})
        # Frame 0's lines come before frame 1's, whichever thread finished first.
        frames = [int(line["data"]["match_id"].rsplit("-", 1)[1]) for line in self.lines
                  if "match_id" in line["data"]]
        self.assertEqual(frames, sorted(frames))
        for line in self.lines:
            self.assertNotIn(pathlib.Path.home().name, json.dumps(line))

        referee = run("referee", self.log, timeout=300)
        self.assert_exit(referee, 0)
        self.assertIn(f"replayed {strokes} of their shots: 0 differences, 0 unreadable, 0 not replayed",
                      referee.stdout)
        remade = run("opponent", self.log, timeout=300)
        self.assert_exit(remade, 0)
        self.assertIn(f"made again {len(decisions)} of {len(decisions)} decisions: 0 differences, 0 unreadable, "
                      "0 not replayed", remade.stdout)

    def test_nothing_printed_or_written_names_the_home_folder(self):
        home = pathlib.Path.home().name
        self.assertNotIn(home, self.result.stdout)
        self.assertNotIn(home, self.report_file.read_text(encoding="utf-8"))
        self.assertIn("report: report.json", self.result.stdout)


class ReportToolTest(TournamentCase):
    def report_tool(self, *args):
        return subprocess.run([sys.executable, str(REPORT_TOOL), *map(str, args)], capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=60)

    def test_it_writes_a_section_per_report(self):
        out = self.tmp / "report.md"
        result = self.report_tool(self.report_file, "--out", out)
        self.assertEqual(result.returncode, 0, result.stderr)
        text = out.read_text(encoding="utf-8")
        self.assertIn("## Beginner against Beginner", text)
        self.assertIn("| frames won | ", text)
        self.assertIn(f"| strokes | {self.report['a']['strokes']} | {self.report['b']['strokes']} |", text)
        self.assertNotIn("Stage 1's marks", text)
        self.assertNotIn(pathlib.Path.home().name, text)

    def test_stage1_fails_on_a_small_run_and_names_the_marks(self):
        out = self.tmp / "report.md"
        result = self.report_tool(self.report_file, "--out", out, "--check", "stage1")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertRegex(result.stdout, r"(?m)^FAIL\s+Beginner's frames against itself: 6 \(report\.json\) "
                                        r"\(wanted at least 2000\)$")
        for name in ("The 147's frames won against Beginner", "The 147 against Beginner: no failure",
                     "The 147 on every exhibition table"):
            self.assertRegex(result.stdout, rf"(?m)^MISSING\s+{name}: no report")
        for name in ("Beginner's medium pots", "Beginner's long pots", "Beginner's mean break",
                     "Beginner's safeties"):
            self.assertRegex(result.stdout, rf"(?m)^(PASS|FAIL)\s+{name}: ")
        self.assertIn("## Stage 1's marks", out.read_text(encoding="utf-8"))

    def test_a_file_that_is_not_a_report_exits_2(self):
        other = self.tmp / "other.json"
        other.write_text(json.dumps({"summary": {}}), encoding="utf-8")
        result = self.report_tool(other, "--out", self.tmp / "report.md")
        self.assertEqual(result.returncode, 2)
        self.assertIn("not a tournament report", result.stderr)


if __name__ == "__main__":
    unittest.main()
