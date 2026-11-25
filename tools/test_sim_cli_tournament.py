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
import opponent_report
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


ESCAPE_MISSES = {"no_hit", "first_hit_not_on", "simultaneous_hit"}


def recount(lines):
    """The ladder's counts that a log shows, counted again from its lines alone, per side ("a" is
    player 0): what followed each safety, escape and break-off, and the concessions and claims.
    Snookers laid need the table's geometry, which only the engine has."""
    sides = {side: {"safeties": [0, 0, 0], "escapes": {}, "break_offs": [0, 0], "concessions": 0, "claims": 0}
             for side in ("a", "b")}
    strokes = {}
    follow = None  # (side, kind) of the last stroke
    for line in lines:
        event, d = line["event"], line["data"]
        if event == "frame.started":
            follow = None
        elif event == "opponent.decision" and d["kind"] == "shot":
            strokes[d["shot_id"]] = d
        elif event == "opponent.decision" and d["kind"] == "choice":
            side = "a" if d["player"] == 0 else "b"
            if d["choice"] in ("concede_frame", "concede_match"):
                sides[side]["concessions"] += 1
                follow = None
            elif d["choice"] == "claim_frame":
                sides[side]["claims"] += 1
                follow = None
        elif event == "rules.decision":
            decided = strokes[d["shot_id"]]
            side = "a" if decided["player"] == 0 else "b"
            why, judgement = decided["why"], d["judgement"]
            kind = why.get("kind")
            scored = judgement["points"] > 0 and not judgement["fouls"]
            if follow is not None and follow[0] != side:
                counts = sides[follow[0]]
                if follow[1] == "safety":
                    counts["safeties"][1] += kind == "pot"
                    counts["safeties"][2] += kind == "pot" and scored
                elif follow[1] == "break":
                    counts["break_offs"][1] += kind == "pot" or why.get("pots", 0) > 0
            follow = (side, "safety" if kind in ("safety", "snooker") else kind)
            if kind in ("safety", "snooker"):
                sides[side]["safeties"][0] += 1
            elif kind == "break":
                sides[side]["break_offs"][0] += 1
            elif kind == "escape":
                cushions = why.get("cushions")
                bucket = "unknown" if not isinstance(cushions, int) else "5+" if cushions >= 5 else str(cushions)
                played, hit = sides[side]["escapes"].get(bucket, (0, 0))
                missed = any(f["kind"] in ESCAPE_MISSES for f in judgement["fouls"])
                sides[side]["escapes"][bucket] = (played + 1, hit + (not missed))
    return sides


def recount_highs(lines):
    """Each frame's highest break for each player, from the log alone: [high of player 0, of player 1]
    per frame. A stroke's break so far is the striker's break before it plus what it scored."""
    highs = {}
    for line in lines:
        event, d = line["event"], line["data"]
        if event == "match.started":
            highs[d["match_id"]] = [0, 0]
        elif event == "rules.decision":
            pre, judgement = d["pre"], d["judgement"]
            points = pre["break_points"] + (0 if judgement["fouls"] else judgement["points"])
            frame = highs[d["match_id"]]
            frame[pre["striker"]] = max(frame[pre["striker"]], points)
    return list(highs.values())


def median(values):
    values, n = sorted(values), len(values)
    return values[n // 2] if n % 2 else (values[n // 2 - 1] + values[n // 2]) / 2


def as_counts(values):
    return [[v, values.count(v)] for v in sorted(set(values))]


class LadderCounts:
    """The stage 2 counts of a report, checked for their types and sums and against the log."""

    def check_ladder_counts(self, report, lines):
        again = recount(lines)
        for side in ("a", "b"):
            with self.subTest(side=side):
                stats = report[side]
                tactics = stats["tactics"]
                for group in tactics.values():
                    for key, value in group.items():
                        if key != "by_cushions":
                            self.assertIsInstance(value, int, key)
                            self.assertGreaterEqual(value, 0, key)
                safeties = tactics["safeties"]
                self.assertEqual(safeties["played"], stats["safeties"])
                self.assertEqual(safeties["then_pot_attempt"] + safeties["then_no_attempt"], safeties["played"])
                self.assertLessEqual(safeties["then_potted"], safeties["then_pot_attempt"])
                self.assertLessEqual(safeties["snooker_kind"], safeties["played"])
                self.assertEqual([safeties["played"], safeties["then_pot_attempt"], safeties["then_potted"]],
                                 again[side]["safeties"])
                snookers = tactics["snookers"]
                self.assertLessEqual(snookers["called"], snookers["laid"])
                self.assertLessEqual(snookers["called"], safeties["snooker_kind"])
                self.assertLessEqual(snookers["then_fouled"], snookers["laid"])
                self.assertGreaterEqual(snookers["then_foul_points"], 4 * snookers["then_fouled"])
                self.assertLessEqual(snookers["then_foul_points"], 7 * snookers["then_fouled"])
                escapes = tactics["escapes"]
                self.assertEqual(escapes["played"], stats["escapes"])
                self.assertLessEqual(escapes["hit"], escapes["played"])
                self.assertLessEqual(set(escapes["by_cushions"]), {"0", "1", "2", "3", "4", "5+", "unknown"})
                self.assertEqual(sum(b["played"] for b in escapes["by_cushions"].values()), escapes["played"])
                self.assertEqual(sum(b["hit"] for b in escapes["by_cushions"].values()), escapes["hit"])
                self.assertEqual({k: (b["played"], b["hit"]) for k, b in escapes["by_cushions"].items()},
                                 again[side]["escapes"])
                break_offs = tactics["break_offs"]
                self.assertEqual(break_offs["then_pot"] + break_offs["then_no_pot"], break_offs["played"])
                self.assertEqual([break_offs["played"], break_offs["then_pot"]], again[side]["break_offs"])
                highs = stats["frame_highs"]
                self.assertEqual(highs["frames"], report["frames"])
                counts = highs["counts"]
                self.assertEqual(counts, sorted(counts))
                self.assertEqual(sum(frames for _, frames in counts), highs["frames"])
                values = sorted(points for points, frames in counts for _ in range(frames))
                n = len(values)
                self.assertEqual(highs["typical"], values[n // 2] if n % 2 else (values[n // 2 - 1] + values[n // 2]) / 2)
                self.assertEqual(highs["century"], sum(v >= 100 for v in values))
                self.assertEqual(highs["fifty_plus"], sum(v >= 50 for v in values))
                self.assertEqual(max(values), stats["breaks"]["highest"])
                self.assertEqual(stats["concessions"], again[side]["concessions"])
                self.assertEqual(stats["claims"], again[side]["claims"])
        self.check_frame_best(report, lines)

    def check_frame_best(self, report, lines):
        best = report["frame_best"]
        self.assertEqual(set(best), {"frames", "typical", "fifty_plus", "century", "counts"})
        for key in ("frames", "fifty_plus", "century"):
            self.assertIsInstance(best[key], int, key)
        self.assertIsInstance(best["typical"], (int, float))
        self.assertEqual(best["frames"], report["frames"])
        highs = recount_highs(lines)
        self.assertEqual(len(highs), report["frames"])
        for side, player in (("a", 0), ("b", 1)):
            self.assertEqual(report[side]["frame_highs"]["counts"], as_counts([h[player] for h in highs]), side)
        frame_best = [max(h) for h in highs]
        self.assertEqual(best["counts"], as_counts(frame_best))
        self.assertEqual(best["typical"], median(frame_best))
        self.assertEqual(best["century"], sum(v >= 100 for v in frame_best))
        self.assertEqual(best["fifty_plus"], sum(v >= 50 for v in frame_best))
        for side in ("a", "b"):
            self.assertGreaterEqual(best["typical"], report[side]["frame_highs"]["typical"], side)


class TournamentLadderTest(LadderCounts):
    """Mixed into TournamentTest, so its shared run is not played again."""

    def test_the_ladder_counts_add_up_and_agree_with_the_log(self):
        self.check_ladder_counts(self.report, self.lines)
        for name in ("own typical break", "frame's best break", "safeties then a pot", "snookers laid / called", "escapes hit a ball on",
                     "break-offs then a pot", "concessions / claims"):
            self.assertIn(name, self.result.stdout)

    def test_the_position_reader_is_not_an_opponent(self):
        reader = json.loads(BEGINNER.read_text(encoding="utf-8"))
        reader["id"] = "person"
        path = self.tmp / "opponent_person.json"
        path.write_text(json.dumps(reader), encoding="utf-8")
        for args in (("--a", "person", "--b", "beginner"), ("--a", "beginner", "--b-file", path)):
            with self.subTest(args=args):
                result = run("tournament", *args, "--frames", 1, "--out", self.tmp / "failures")
                self.assert_exit(result, 2)
                self.assertIn("is the game's position reader, not an opponent", result.stderr)


class TournamentTest(TournamentCase, TournamentLadderTest):
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
                self.assertEqual(stats["safeties_with_pot"] + stats["no_pot"], stats["safeties"] + stats["escapes"])
                self.assertLessEqual(stats["fouls"], stats["strokes"])
                think = stats["think"]
                self.assertEqual(set(think["stopped"]), {"done", "sims", "steps", "time"})
                self.assertGreaterEqual(sum(think["stopped"].values()), stats["strokes"])
                self.assertLessEqual(think["us_mean"], think["us_max"])
        for name in ("frames won", "pots", "breaks (count)", "safe with a pot", "no pot at all",
                     "think us mean/p99/max", "failures: 0"):
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
                self.assertEqual(report["frame_best"], {"frames": 0, "typical": 0.0, "fifty_plus": 0, "century": 0,
                                                        "counts": []})

    def test_an_exhibition_stopped_by_a_failure_ends_failed_in_the_log(self):
        layouts = json.loads(EXHIBITIONS.read_text(encoding="utf-8"))["layouts"]
        tables = self.tmp / "one.json"
        tables.write_text(json.dumps({"schema_version": 1, "layouts": layouts[:1]}), encoding="utf-8")
        log = self.tmp / "x.jsonl"
        result = run("tournament", "--a", "maximum", "--frames", 0, "--exhibitions", "--exhibitions-file", tables,
                     "--max-strokes", 1, "--out", self.tmp / "failures", "--report", self.tmp / "x.json", "--log", log,
                     timeout=120)
        self.assert_exit(result, 1)
        report = json.loads((self.tmp / "x.json").read_text(encoding="utf-8"))
        self.assertEqual([f["kind"] for f in report["failures"]], ["max_strokes"])
        self.assertEqual(report["failures"][0]["where"], "exhibition " + layouts[0]["name"])
        lines = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]
        ended = [line["data"] for line in lines if line["event"] == "exhibition.ended"]
        self.assertEqual(len(ended), 1)
        self.assertEqual(ended[0]["why"], "failed")
        self.assertEqual(ended[0]["break_points"], report["exhibitions"][0]["break_points"])
        configured = next(line["data"] for line in lines if line["event"] == "rules.configured")
        self.assertEqual(configured["exhibitions"], [layouts[0]["name"]])
        self.assertEqual([level["id"] for level in configured["levels"]], ["maximum"])
        self.assertEqual(check_logs.check_file(log, check_logs.load_schema())[1], [])

    def test_the_log_passes_the_schema_the_referee_and_the_opponent(self):
        count, bad = check_logs.check_file(self.log, check_logs.load_schema())
        self.assertEqual(bad, [])
        self.assertEqual(count, len(self.lines))
        events = [line["event"] for line in self.lines]
        self.assertEqual(events[:4], ["params.loaded"] * 3 + ["rules.configured"])
        configured = self.lines[3]["data"]
        self.assertTrue(configured["ok"])
        self.assertEqual(configured["problems"], [])
        self.assertEqual(configured["levels"],
                         [dict(self.report["levels"]["a"], hash=self.report["level_hashes"]["a"])])
        self.assertEqual(configured["standin"], json.loads((DATA / "standin_params.json").read_text())["values"])
        self.assertEqual(configured["params_hash"], self.report["params_hash"])
        for event in ("match.started", "match.ended", "frame.ended"):
            self.assertEqual(events.count(event), 6, event)
        decisions = [line["data"] for line in self.lines if line["event"] == "opponent.decision"]
        strokes = sum(self.report[side]["strokes"] for side in ("a", "b"))
        self.assertEqual(sum(d["kind"] == "shot" for d in decisions), strokes)
        for d in decisions:
            self.assertEqual(d["level"], "beginner")
            self.assertRegex(d["seed"], r"^\d+$")
            self.assertRegex(d["pre_hash"], r"^[0-9a-f]+$")
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

    def test_safeties_are_split_by_whether_the_search_found_a_pot(self):
        with_pot = {"a": 0, "b": 0}
        without = {"a": 0, "b": 0}
        for line in self.lines:
            d = line["data"]
            if line["event"] != "opponent.decision" or d["kind"] != "shot":
                continue
            if d["why"].get("kind") not in ("safety", "escape"):
                continue
            side = "a" if d["player"] == 0 else "b"
            if d["why"].get("pots", 0) > 0:
                with_pot[side] += 1
            else:
                without[side] += 1
        for side in ("a", "b"):
            with self.subTest(side=side):
                self.assertEqual(self.report[side]["safeties_with_pot"], with_pot[side])
                self.assertEqual(self.report[side]["no_pot"], without[side])
        self.assertGreater(without["a"] + without["b"], 0)

    def test_nothing_printed_or_written_names_the_home_folder(self):
        home = pathlib.Path.home().name
        self.assertNotIn(home, self.result.stdout)
        self.assertNotIn(home, self.report_file.read_text(encoding="utf-8"))
        self.assertIn("report: report.json", self.result.stdout)


class TwoLevelsTest(CliTestCase, LadderCounts):
    """Two frames of two stage 2 levels, one on each side."""

    def test_two_levels_play_each_other_and_their_counts_add_up(self):
        report_file, log = self.tmp / "ladder.json", self.tmp / "ladder.jsonl"
        result = run("tournament", "--a", "club", "--b", "amateur", "--frames", 2, "--seed", 5, "--threads", 2,
                     "--out", self.tmp / "failures", "--report", report_file, "--log", log, timeout=600)
        self.assert_exit(result, 0)
        report = json.loads(report_file.read_text(encoding="utf-8"))
        self.assertEqual((report["levels"]["a"]["id"], report["levels"]["b"]["id"]), ("club", "amateur"))
        self.assertEqual(report["a"]["frames_won"] + report["b"]["frames_won"], 2)
        lines = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]
        self.check_ladder_counts(report, lines)
        self.assertEqual({d["data"]["level"] for d in lines if d["event"] == "opponent.decision"}, {"club", "amateur"})


class NoPotsTest(CliTestCase):
    def test_frames_with_no_pots_have_a_best_break_of_nothing(self):
        report_file = self.tmp / "nothing.json"
        result = run("tournament", "--a", "beginner", "--b", "beginner", "--frames", 2, "--max-strokes", 1,
                     "--out", self.tmp / "failures", "--report", report_file, timeout=120)
        # Each frame stops unfinished at its one stroke, which the run reports as a failure.
        self.assert_exit(result, 1)
        report = json.loads(report_file.read_text(encoding="utf-8"))
        self.assertEqual([f["kind"] for f in report["failures"]], ["max_strokes", "max_strokes"])
        best = report["frame_best"]
        self.assertEqual(best["frames"], 2)
        self.assertEqual(best["typical"], 0.0)
        self.assertEqual(best["counts"], [[0, 2]])
        self.assertEqual((best["fifty_plus"], best["century"]), (0, 0))
        self.assertEqual(opponent_report.median_of(opponent_report.best_counts({"frame_best": best})), 0)


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
                     "Beginner's safeties with a pot to play"):
            self.assertRegex(result.stdout, rf"(?m)^(PASS|FAIL)\s+{name}: ")
        self.assertRegex(result.stdout, r"(?m)^REPORTED\s+Beginner's strokes with no pot at all: \d+/\d+ ")
        self.assertIn("## Stage 1's marks", out.read_text(encoding="utf-8"))

    def test_the_safety_mark_counts_only_strokes_with_a_pot_to_play(self):
        def marks_for(safeties_with_pot, attempts, no_pot):
            report = copy.deepcopy(self.report)
            report["frames"] = 2000
            for side in ("a", "b"):
                stats = report[side]
                stats["safeties_with_pot"] = safeties_with_pot
                stats["pots"]["attempts"] = attempts
                stats["no_pot"] = no_pot
            path = self.tmp / "crafted.json"
            path.write_text(json.dumps(report), encoding="utf-8")
            result = self.report_tool(path, "--out", self.tmp / "crafted.md", "--check", "stage1")
            return result.stdout

        # Many strokes with no pot at all do not count against it, however many there are.
        out = marks_for(safeties_with_pot=9, attempts=91, no_pot=500)
        self.assertRegex(out, r"(?m)^PASS\s+Beginner's safeties with a pot to play: 18/200 \(9\.0%\)")
        self.assertRegex(out, r"(?m)^REPORTED\s+Beginner's strokes with no pot at all: 1000/")
        out = marks_for(safeties_with_pot=10, attempts=90, no_pot=0)
        self.assertRegex(out, r"(?m)^FAIL\s+Beginner's safeties with a pot to play: 20/200 \(10\.0%\)")
        self.assertRegex(out, r"(?m)^\d+ of 8 marks met")

    def test_a_file_that_is_not_a_report_exits_2(self):
        other = self.tmp / "other.json"
        other.write_text(json.dumps({"summary": {}}), encoding="utf-8")
        result = self.report_tool(other, "--out", self.tmp / "report.md")
        self.assertEqual(result.returncode, 2)
        self.assertIn("not a tournament report", result.stderr)


if __name__ == "__main__":
    unittest.main()
