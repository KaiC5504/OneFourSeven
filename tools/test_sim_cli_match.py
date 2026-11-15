"""Tests of ofs_sim_cli's match and referee commands through the command line, against the build
test_sim_cli.py finds (the Debug build, or the one OFS_SIM_CLI names)."""
import copy
import json
import pathlib
import tempfile
import unittest

import check_logs
from test_sim_cli import CliTestCase, PARAMS_FILE, ROOT, STRAIGHT, TABLE_FILE, run, shown


def summary_line(result):
    """The JSON line a match run ends with."""
    return json.loads(result.stdout.strip().splitlines()[-1])


class MatchCase(CliTestCase):
    """One short logged run, two one-frame matches of seed 3, shared by the class's tests."""

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls._shared = tempfile.TemporaryDirectory()
        cls.failures = pathlib.Path(cls._shared.name) / "failures"
        cls.log = pathlib.Path(cls._shared.name) / "match.jsonl"
        cls.result = run("match", "--frames", 2, "--best-of", 1, "--seed", 3, "--threads", 2, "--out", cls.failures,
                         "--log", cls.log, timeout=300)
        if cls.result.returncode != 0:
            raise AssertionError("the shared match run failed:\n" + shown(cls.result))
        cls.lines = [json.loads(line) for line in cls.log.read_text(encoding="utf-8").splitlines()]

    @classmethod
    def tearDownClass(cls):
        cls._shared.cleanup()

    def match(self, *args, frames=2, seed=3, threads=2):
        result = run("match", "--frames", frames, "--best-of", 1, "--seed", seed, "--threads", threads,
                     "--out", self.tmp / "failures", *args, timeout=300)
        self.assert_exit(result, 0)
        return result

    def logged_match(self):
        return self.result, self.log, self.lines

    def write_jsonl(self, name, lines):
        path = self.tmp / name
        path.write_text("".join(json.dumps(line) + "\n" for line in lines), encoding="utf-8")
        return path


class MatchTest(MatchCase):
    def test_a_short_run_plays_its_frames_and_its_log_passes_the_referee(self):
        result, log, lines = self.logged_match()
        self.assertIn("frames 2 of 2 in 2 matches", result.stdout)
        self.assertNotIn("BREACH", result.stdout)
        summary = summary_line(result)["summary"]
        self.assertEqual(summary["frames"], 2)
        self.assertFalse(summary["breached"])
        self.assertEqual(sum(summary["ended"].values()), 2)
        self.assertEqual(sum(summary["stroke_kinds"].values()), summary["strokes"])
        self.assertGreater(summary["strokes"], 20)
        self.assertEqual(list(self.failures.glob("*.json")), [])

        decisions = [line for line in lines if line["event"] == "rules.decision"]
        self.assertEqual(len(decisions), summary["strokes"])
        checked = run("referee", log)
        self.assert_exit(checked, 0)
        self.assertIn(f"re-judged {len(decisions)} decisions, replayed {len(decisions)} of their shots: "
                      "0 differences, 0 unreadable, 0 not replayed", checked.stdout)
        self.assertNotIn("DIFFERS", checked.stdout)
        self.assertNotIn("NOT REPLAYED", checked.stdout)

    def test_each_choice_of_the_standin_logs_its_seed_and_why(self):
        _, _, lines = self.logged_match()
        chosen = [line["data"] for line in lines if line["event"] == "opponent.decision"]
        self.assertTrue(chosen)
        for data in chosen:
            self.assertRegex(data["seed"], r"^\d+$")
            self.assertIsInstance(data["why"], dict)
            if data["kind"] == "choice":
                self.assertNotIn("shot_id", data)
                self.assertIn(data["choice"], data["options"])
                # After a foul, a re-rack or the toss it names its rule; a claim or concession its kind.
                self.assertTrue("rule" in data["why"] or data["why"].get("kind") in ("claim", "concede"), data)
        self.assertEqual(len({data["seed"] for data in chosen}), len(chosen), "a fresh seed for each")
        loaded = next(line["data"]["hash"] for line in lines if line["event"] == "params.loaded")
        for line in lines:
            if line["event"] == "frame.started":
                self.assertEqual(line["data"]["params_hash"], loaded)

    def test_the_log_passes_the_schema_and_ties_each_stroke_to_its_shot(self):
        _, log, lines = self.logged_match()
        count, bad = check_logs.check_file(log, check_logs.load_schema())
        self.assertEqual(bad, [])
        self.assertEqual(count, len(lines))
        events = [line["event"] for line in lines]
        self.assertEqual(events[0], "params.loaded")
        for event, wanted in (("match.started", 2), ("match.ended", 2), ("frame.ended", 2)):
            self.assertEqual(events.count(event), wanted, event)
        self.assertGreaterEqual(events.count("frame.started"), 2)
        shots = {line["data"]["context"]["shot_id"] for line in lines if line["event"] == "shot.simulated"}
        chosen = {line["data"]["shot_id"] for line in lines
                  if line["event"] == "opponent.decision" and line["data"]["kind"] == "shot"}
        decisions = [line["data"] for line in lines if line["event"] == "rules.decision"]
        self.assertEqual(len(shots), len(decisions))
        for decision in decisions:
            self.assertIn(decision["shot_id"], shots)
            self.assertIn(decision["shot_id"], chosen)
            self.assertRegex(decision["match_id"], r"^sim-3-\d+$")
        # Match 0's lines come before match 1's, whichever thread finished first.
        matches = [line["data"]["match_id"] for line in lines if "match_id" in line["data"]]
        self.assertEqual(matches, sorted(matches))
        for line in lines:
            self.assertNotIn(pathlib.Path.home().name, json.dumps(line))

    def test_the_same_seed_gives_the_same_summary_whatever_the_threads(self):
        one = summary_line(self.match(frames=4, seed=11, threads=1))
        many = summary_line(self.match(frames=4, seed=11, threads=4))
        self.assertEqual(one["summary"], many["summary"])
        self.assertEqual(one["timing"]["threads"], 1)
        self.assertEqual(many["timing"]["threads"], 4)
        other = summary_line(self.match(frames=4, seed=12, threads=4))
        self.assertNotEqual(one["summary"]["frames_digest"], other["summary"]["frames_digest"])

    def test_the_log_is_the_same_whatever_the_threads(self):
        logs = []
        for threads in (1, 3):
            log = self.tmp / f"threads{threads}.jsonl"
            self.match("--log", log, frames=3, seed=11, threads=threads)
            lines = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]
            # The session and how long things took differ from run to run; what was logged does not.
            for line in lines:
                line["data"].pop("us", None)
                line["data"].get("result", {}).pop("sim_us", None)
            logs.append([(line["event"], line["data"]) for line in lines])
        self.assertEqual(logs[0], logs[1])

    def test_frames_stop_at_the_count_asked_for(self):
        result = run("match", "--frames", 2, "--best-of", 3, "--seed", 5, "--threads", 2, "--out",
                     self.tmp / "failures", timeout=300)
        self.assert_exit(result, 0)
        summary = summary_line(result)["summary"]
        self.assertEqual(summary["frames"], 2)
        self.assertEqual(summary["best_of"], 3)
        self.assertEqual(summary["matches"], 1)

    def test_a_frame_that_cannot_end_stops_the_run_with_a_repro_file(self):
        # With the slowest cue the physics allows, the balls barely move: every dozen strokes the
        # frame is racked again (S3.17) and it never ends. Half a minute against a Debug build.
        params = json.loads(PARAMS_FILE.read_text(encoding="utf-8"))
        params["values"]["max_cue_speed_m_s"] = 0.5
        slow = self.tmp / "slow_params.json"
        slow.write_text(json.dumps(params), encoding="utf-8")
        failures = self.tmp / "failures"
        failures.mkdir()
        stale = failures / "match-9-000000-f01-s0001.json"
        stale.write_text("{}", encoding="utf-8")
        result = run("match", "--frames", 1, "--best-of", 1, "--seed", 4, "--threads", 1, "--params", slow,
                     "--out", failures, timeout=600)
        self.assert_exit(result, 1)
        self.assertRegex(result.stdout, r"(?m)^BREACH match 0, frame 1, stroke 3000 \(stroke_cap\): the frame is "
                                        r"not over after 3000 strokes$")
        summary = summary_line(result)["summary"]
        self.assertTrue(summary["breached"])
        self.assertEqual(summary["frames"], 0)
        # Only this run's failure is in the folder.
        repros = list(failures.glob("*.json"))
        self.assertEqual([path.name for path in repros], ["match-4-000000-f01-s3000.json"])
        self.assertIn(repros[0].name, result.stdout)
        repro = json.loads(repros[0].read_text(encoding="utf-8"))
        self.assertEqual(repro["breach"]["kind"], "stroke_cap")
        self.assertEqual(repro["match"], {"seed": "4", "match": 0, "match_seed": repro["match"]["match_seed"],
                                          "best_of": 1, "frame": 1, "stroke": 3000})
        for key in ("frame", "after", "stroke", "ref"):
            self.assertIn(key, repro)
        self.assertFalse(repro["frame"]["over"])
        self.assertIn("balls", repro["frame"])

    def test_bad_arguments_exit_2(self):
        for args, message in ((("--frames", "0"), "--frames: '0'"),
                              (("--frames", "many"), "--frames: 'many'"),
                              (("--best-of", "2"), "--best-of"),
                              (("--best-of", "37"), "--best-of: '37'"),
                              (("--threads", "0"), "--threads: '0'"),
                              (("--seed", "-1"), "--seed: '-1'"),
                              (("--bogus", "1"), "unknown option --bogus"),
                              (("--frames",), "--frames needs a value"),
                              (("extra",), "unexpected argument"),
                              (("--table", self.tmp / "missing.json"), "cannot open")):
            with self.subTest(args=args):
                result = run("match", *args)
                self.assert_exit(result, 2)
                self.assertIn(message, result.stderr)


class RefereeTest(MatchCase):
    def decision_numbers(self, lines):
        return [number for number, line in enumerate(lines, start=1) if line["event"] == "rules.decision"]

    def test_a_changed_score_fails_with_the_line_it_is_on(self):
        _, _, lines = self.logged_match()
        numbers = self.decision_numbers(lines)
        # A stroke with no foul and most of the reds left: a point more changes the score and nothing else.
        number = next(n for n in numbers if not lines[n - 1]["data"]["judgement"]["fouls"] and
                      sum(ball["ball"] == "red" for ball in lines[n - 1]["data"]["pre"]["balls"]) >= 10)
        tampered = copy.deepcopy(lines)
        tampered[number - 1]["data"]["pre"]["scores"][0] += 1
        log = self.write_jsonl("tampered.jsonl", tampered)
        result = run("referee", log)
        self.assert_exit(result, 1)
        differing = [line for line in result.stdout.splitlines() if "DIFFERS" in line]
        self.assertEqual(len(differing), 1, result.stdout)
        self.assertRegex(differing[0], rf"^line {number}\s+rules\.decision\s+DIFFERS\s+"
                                       r"after\.scores\[0\]: logged \d+, re-judged \d+")
        self.assertIn("1 differences", result.stdout)

        self.assert_exit(run("referee", log, "--line", number), 1)
        clean = run("referee", log, "--line", next(n for n in numbers if n != number))
        self.assert_exit(clean, 0)
        self.assertIn("re-judged 1 decisions, replayed 1 of their shots", clean.stdout)

    def test_a_changed_ruling_fails(self):
        _, _, lines = self.logged_match()
        fouls = [number for number in self.decision_numbers(lines)
                 if lines[number - 1]["data"]["judgement"]["penalty"] > 0]
        self.assertTrue(fouls, "the run had no foul to tamper with")
        tampered = copy.deepcopy(lines)
        tampered[fouls[0] - 1]["data"]["judgement"]["penalty"] += 1
        result = run("referee", self.write_jsonl("tampered.jsonl", tampered))
        self.assert_exit(result, 1)
        self.assertRegex(result.stdout, rf"line {fouls[0]}\s+rules\.decision\s+DIFFERS\s+judgement\.penalty: logged")

    def test_a_fact_the_shot_does_not_bear_out_fails(self):
        _, _, lines = self.logged_match()
        numbers = self.decision_numbers(lines)
        tampered = copy.deepcopy(lines)
        number = numbers[3]
        facts = tampered[number - 1]["data"]["facts"]
        cue_ball = tampered[number - 1]["data"]["pre"]["cue_ball"]
        facts["rest"][cue_ball][0] += 0.01
        result = run("referee", self.write_jsonl("tampered.jsonl", tampered))
        self.assert_exit(result, 1)
        self.assertRegex(result.stdout, rf"line {number}\s+shot\.simulated\s+DIFFERS\s+\(shot\.simulated at line "
                                        rf"\d+\) facts\.rest\[{cue_ball}\]")

    def test_decisions_without_their_shots_are_rejudged_and_fail_unless_allowed(self):
        _, _, lines = self.logged_match()
        kept = [line for line in lines if line["event"] != "shot.simulated"]
        numbers = self.decision_numbers(kept)
        log = self.write_jsonl("no_shots.jsonl", kept)
        result = run("referee", log)
        self.assert_exit(result, 1)
        self.assertIn(f"re-judged {len(numbers)} decisions, replayed 0 of their shots: 0 differences, 0 unreadable, "
                      f"{len(numbers)} not replayed", result.stdout)
        unreplayed = [line for line in result.stdout.splitlines() if line.startswith("NOT REPLAYED")]
        self.assertEqual(len(unreplayed), len(numbers))
        self.assertEqual(unreplayed[0], f"NOT REPLAYED line {numbers[0]} shot_id "
                                        f"{kept[numbers[0] - 1]['data']['shot_id']}")
        allowed = run("referee", log, "--allow-unreplayed")
        self.assert_exit(allowed, 0)
        self.assertIn(f"{len(numbers)} not replayed", allowed.stdout)

    def test_a_decision_whose_shot_id_matches_no_shot_is_not_replayed(self):
        _, _, lines = self.logged_match()
        numbers = self.decision_numbers(lines)
        tampered = copy.deepcopy(lines)
        tampered[numbers[2] - 1]["data"]["shot_id"] = "another-shot"
        del tampered[numbers[4] - 1]["data"]["shot_id"]
        result = run("referee", self.write_jsonl("other_shot.jsonl", tampered))
        self.assert_exit(result, 1)
        self.assertNotIn("DIFFERS", result.stdout)
        unreplayed = [line for line in result.stdout.splitlines() if line.startswith("NOT REPLAYED")]
        self.assertEqual(unreplayed, [f"NOT REPLAYED line {numbers[2]} shot_id another-shot",
                                      f"NOT REPLAYED line {numbers[4]} shot_id (none)"])
        self.assertIn(f"replayed {len(numbers) - 2} of their shots: 0 differences, 0 unreadable, 2 not replayed",
                      result.stdout)

    def test_a_shot_from_another_table_is_not_compared(self):
        _, log, lines = self.logged_match()
        table = json.loads(TABLE_FILE.read_text(encoding="utf-8"))
        table["_note"] += " A different file, so a different hash."
        other = self.tmp / "other_table.json"
        other.write_text(json.dumps(table), encoding="utf-8")
        result = run("referee", log, "--table", other, "--line", self.decision_numbers(lines)[0])
        self.assert_exit(result, 2)
        self.assertIn("the shot ran on table", result.stdout)
        self.assertIn("pass that table's file with --table", result.stdout)

    def test_a_line_that_is_not_json_is_reported(self):
        _, log, lines = self.logged_match()
        damaged = self.tmp / "damaged.jsonl"
        damaged.write_text(log.read_text(encoding="utf-8") + '{"event": "rules.decision", "data": {"pre"\n',
                           encoding="utf-8")
        result = run("referee", damaged)
        self.assert_exit(result, 2)
        self.assertRegex(result.stdout, rf"line {len(lines) + 1}\s+ERROR\s+not a JSON object")
        self.assertIn("0 differences, 1 unreadable", result.stdout)

    def test_bad_arguments_and_input_exit_2(self):
        _, log, lines = self.logged_match()
        not_a_decision = next(number for number, line in enumerate(lines, start=1)
                              if line["event"] == "frame.started")
        shot_log = self.tmp / "shot.jsonl"
        self.assert_exit(run("simulate", STRAIGHT, "--log", shot_log), 0)
        for args, message in (((), "missing argument"),
                              ((self.tmp / "missing.jsonl",), "cannot open"),
                              ((log, "--line", "0"), "--line: '0'"),
                              ((log, "--line", len(lines) + 5), f"has {len(lines)} lines"),
                              ((log, "--line", not_a_decision), "is not a rules.decision event"),
                              ((log, "--table", self.tmp / "missing.json"), "was refused"),
                              ((log, "--bogus"), "unknown option --bogus"),
                              ((shot_log,), "has no rules.decision lines")):
            with self.subTest(args=args):
                result = run("referee", *args)
                self.assert_exit(result, 2)
                self.assertIn(message, result.stderr)


if __name__ == "__main__":
    unittest.main()
