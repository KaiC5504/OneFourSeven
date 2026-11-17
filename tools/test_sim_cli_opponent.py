"""Tests of ofs_sim_cli's opponent command through the command line, against the build
test_sim_cli.py finds (the Debug build, or the one OFS_SIM_CLI names)."""
import copy
import json
import pathlib
import re
import tempfile
import unittest

from test_sim_cli import CliTestCase, ROOT, run, shown

BEGINNER = ROOT / "core" / "data" / "opponent_beginner.json"


class OpponentCase(CliTestCase):
    """One logged tournament of four frames of Beginner against itself, shared by the class's tests."""

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls._shared = tempfile.TemporaryDirectory()
        folder = pathlib.Path(cls._shared.name)
        cls.log = folder / "tournament.jsonl"
        result = run("tournament", "--a", "beginner", "--b", "beginner", "--frames", 4, "--seed", 5, "--out",
                     folder / "failures", "--log", cls.log, timeout=300)
        if result.returncode != 0:
            raise AssertionError("the shared tournament run failed:\n" + shown(result))
        cls.lines = [json.loads(line) for line in cls.log.read_text(encoding="utf-8").splitlines()]

    @classmethod
    def tearDownClass(cls):
        cls._shared.cleanup()

    def numbers(self, kind):
        return [number for number, line in enumerate(self.lines, start=1)
                if line["event"] == "opponent.decision" and line["data"]["kind"] == kind]

    def write_jsonl(self, name, lines):
        path = self.tmp / name
        path.write_text("".join(json.dumps(line) + "\n" for line in lines), encoding="utf-8")
        return path

    def tampered(self, number, change):
        lines = copy.deepcopy(self.lines)
        change(lines[number - 1]["data"])
        return self.write_jsonl("tampered.jsonl", lines)

    def from_platform(self, platform):
        lines = copy.deepcopy(self.lines)
        for line in lines:
            line["platform"] = platform
        return lines

    @staticmethod
    def bundle_of(lines, shot_id):
        return next(line["data"]["bundle"] for line in lines
                    if line["event"] == "shot.simulated" and line["data"]["context"]["shot_id"] == shot_id)

    def nudged(self, lines, number, by):
        """The stroke's strike moved by `by` where it was decided and where it was played, as another libm can."""
        data = lines[number - 1]["data"]
        for strike in (data["strike"], self.bundle_of(lines, data["shot_id"])["strike"]):
            strike["aim_rad"] += by
        data["meant"]["speed_m_s"] += by
        return lines


class OpponentTest(OpponentCase):
    def test_every_decision_of_the_log_is_made_again_alike(self):
        shots, choices = self.numbers("shot"), self.numbers("choice")
        self.assertTrue(shots)
        self.assertTrue(choices, "the run had no choice to make again")
        in_hand = [n for n in shots if "cue" in self.lines[n - 1]["data"]]
        self.assertTrue(in_hand, "the run had no stroke with the cue ball in hand")
        for n in shots + choices:
            self.assertRegex(self.lines[n - 1]["data"]["pre_hash"], r"^[0-9a-f]+$")
        result = run("opponent", self.log, timeout=300)
        self.assert_exit(result, 0)
        total = len(shots) + len(choices)
        self.assertIn(f"made again {total} of {total} decisions: 0 differences, 0 unreadable, 0 not replayed, "
                      "0 frames not rebuilt, 0 struck otherwise; 0 differ across platforms; 0 refused in the game; "
                      "0 without a level skipped", result.stdout)
        self.assertRegex(result.stdout, r"(?m)^level beginner: the log's values, the same as "
                                        r"core/data/opponent_beginner\.json \(rules\.configured at line 4, "
                                        r"hash [0-9a-f]+\)$")
        self.assertNotIn("DIFFERS", result.stdout)
        self.assertNotIn("stand-in:", result.stdout)

    def test_a_log_from_another_platform_is_compared_within_1e_9(self):
        number = self.numbers("shot")[3]
        log = self.write_jsonl("ios.jsonl", self.nudged(self.from_platform("ios"), number, 1e-12))
        result = run("opponent", log, "--line", number, timeout=300)
        self.assert_exit(result, 0)
        self.assertRegex(result.stdout, rf"(?m)^line {number} was logged on ios, this build is \w+: its numbers are "
                                        r"compared within 1e-09$")
        self.assertIn("made again 1 of 1 decisions: 0 differences", result.stdout)
        self.assertIn("0 differ across platforms", result.stdout)

        here = self.write_jsonl("here.jsonl", self.nudged(copy.deepcopy(self.lines), number, 1e-12))
        result = run("opponent", here, "--line", number, timeout=300)
        self.assert_exit(result, 1)
        self.assertRegex(result.stdout, rf"(?m)^line {number}\s+strike\s+DIFFERS\s+logged")
        self.assertRegex(result.stdout, rf"(?m)^line {number}\s+meant\s+DIFFERS\s+logged")

    def test_a_decision_that_parts_across_platforms_is_reported_apart(self):
        number = self.numbers("shot")[3]
        lines = self.nudged(self.from_platform("ios"), number, 1e-6)
        lines[number - 1]["data"]["why"]["target"] = "a ball not there"
        log = self.write_jsonl("ios.jsonl", lines)
        result = run("opponent", log, "--line", number, timeout=300)
        self.assert_exit(result, 0)
        tag = r"DIFFERS ACROSS PLATFORMS \(ios -> \w+\)"
        for what in ("why.target", "strike", "meant"):
            self.assertRegex(result.stdout, rf"(?m)^line {number}\s+{re.escape(what)}\s+{tag}  logged .+, remade .+$")
        self.assertRegex(result.stdout, rf"(?m)^line {number}\s+think\s+{tag}  logged \{{\"candidates\":\d+,"
                                        r"\"sims\":\d+,\"steps\":\d+\}, remade \{\"candidates\":\d+,")
        self.assertIn("made again 1 of 1 decisions: 0 differences, 0 unreadable, 0 not replayed, 0 frames not "
                      "rebuilt, 0 struck otherwise; 1 differ across platforms;", result.stdout)
        self.assert_exit(run("opponent", log, "--line", number, "--strict", timeout=300), 1)

    def test_a_log_made_with_other_level_values_is_made_again_with_them(self):
        tuned = json.loads(BEGINNER.read_text(encoding="utf-8"))
        tuned["values"]["aim_sigma_rad"] = 0.004
        tuned["values"]["speeds"] = 3
        level = self.tmp / "tuned.json"
        level.write_text(json.dumps(tuned), encoding="utf-8")
        log = self.tmp / "tuned.jsonl"
        self.assert_exit(run("tournament", "--a-file", level, "--b-file", level, "--frames", 1, "--seed", 5,
                             "--threads", 1, "--out", self.tmp / "failures", "--log", log, timeout=300), 0)
        result = run("opponent", log, timeout=300)
        self.assert_exit(result, 0)
        self.assertRegex(result.stdout, r"(?m)^level beginner: the log's values differ from core/data's; using the "
                                        r"log's \(rules\.configured at line 4, hash [0-9a-f]+\)$")
        self.assertIn(": 0 differences, 0 unreadable", result.stdout)

        lines = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]
        without = self.write_jsonl("unconfigured.jsonl",
                                   [line for line in lines if line["event"] != "rules.configured"])
        result = run("opponent", without, timeout=300)
        self.assert_exit(result, 1)
        self.assertRegex(result.stdout, r"(?m)^level beginner: the log has no rules\.configured with its values; "
                                        r"using core/data/opponent_beginner\.json, hash [0-9a-f]+$")
        self.assertIn("stand-in: the log has no rules.configured with its values; using core/data/standin_params.json",
                      result.stdout)
        self.assertIn("DIFFERS", result.stdout)

    def test_the_logs_standin_values_are_used(self):
        lines = copy.deepcopy(self.lines)
        configured = next(line for line in lines if line["event"] == "rules.configured")
        configured["data"]["standin"]["safety_speed_m_s"] = 1.7
        result = run("opponent", self.write_jsonl("standin.jsonl", lines), "--line", self.numbers("shot")[0],
                     timeout=300)
        self.assertRegex(result.stdout, r"(?m)^stand-in: the log's values differ from core/data's; using the log's "
                                        r"\(rules\.configured at line 4\)$")

    def test_a_decision_the_game_refused_is_reported_not_made_again(self):
        number = self.numbers("shot")[4]
        lines = copy.deepcopy(self.lines)
        line = lines[number - 1]
        shot_id = line["data"]["shot_id"]
        line["level"] = "warn"
        line["data"]["error"] = "its cue ball place was refused"
        line["data"]["pre"] = next(l["data"]["pre"] for l in lines
                                   if l["event"] == "rules.decision" and l["data"]["shot_id"] == shot_id)
        result = run("opponent", self.write_jsonl("refused.jsonl", lines), "--line", number, timeout=300)
        self.assert_exit(result, 0)
        self.assertRegex(result.stdout, rf"(?m)^line {number}\s+shot\s+refused in the game: its cue ball place was "
                                        rf"refused \(shot_id {re.escape(shot_id)}\); the frame it was decided from is "
                                        r"in its \"pre\", to rebuild from$")
        self.assertIn("made again 0 of 0 decisions", result.stdout)
        self.assertIn("; 1 refused in the game;", result.stdout)

    def test_a_frame_that_does_not_rebuild_is_its_own_finding(self):
        number = next(n for n in self.numbers("shot") if "cue" in self.lines[n - 1]["data"])
        log = self.tampered(number, lambda data: data.update(pre_hash="0123456789abcdef"))
        result = run("opponent", log, "--line", number, timeout=300)
        self.assert_exit(result, 1)
        self.assertRegex(result.stdout, rf"(?m)^line {number}\s+frame\s+NOT REBUILT  the frame could not be rebuilt: "
                                        r"pre_hash 0123456789abcdef, rebuilt [0-9a-f]+ \(the cue ball in hand put back "
                                        r"at \(.+\)\)$")
        self.assertIn("made again 0 of 1 decisions: 0 differences, 0 unreadable, 0 not replayed, 1 frames not rebuilt",
                      result.stdout)
        self.assertNotIn("DIFFERS", result.stdout)

        lines = self.from_platform("ios")
        lines[number - 1]["data"]["pre_hash"] = "0123456789abcdef"
        across = self.write_jsonl("ios.jsonl", lines)
        result = run("opponent", across, "--line", number, timeout=300)
        self.assert_exit(result, 0)
        self.assertIn("(logged on ios)", result.stdout)
        self.assert_exit(run("opponent", across, "--line", number, "--strict", timeout=300), 1)

    def test_a_strike_other_than_the_one_decided_is_a_finding(self):
        number = self.numbers("shot")[2]
        lines = copy.deepcopy(self.lines)
        data = lines[number - 1]["data"]
        data["params_changed"] = True
        self.bundle_of(lines, data["shot_id"])["strike"]["speed_m_s"] *= 1.01
        result = run("opponent", self.write_jsonl("struck.jsonl", lines), "--line", number, timeout=300)
        self.assert_exit(result, 1)
        self.assertRegex(result.stdout, rf"(?m)^line {number}\s+struck\s+DIFFERS  decided .+, struck .+ "
                                        r"\(shot\.simulated at line \d+\) \(params_changed: the physics values changed "
                                        r"while it thought\)$")
        self.assertIn("made again 1 of 1 decisions: 0 differences, 0 unreadable, 0 not replayed, 0 frames not "
                      "rebuilt, 1 struck otherwise", result.stdout)

    def test_a_changed_seed_fails_on_its_line(self):
        number = self.numbers("shot")[5]
        log = self.tampered(number, lambda data: data.update(seed=str(int(data["seed"]) + 1)))
        result = run("opponent", log, timeout=300)
        self.assert_exit(result, 1)
        differing = [line for line in result.stdout.splitlines() if "DIFFERS" in line]
        self.assertTrue(differing, result.stdout)
        for line in differing:
            self.assertRegex(line, rf"^line {number}\s+\w+\s+DIFFERS\s+logged .+, remade .+$")
        self.assertRegex(result.stdout, r"0 unreadable, 0 not replayed")

        self.assert_exit(run("opponent", log, "--line", number), 1)
        clean = run("opponent", log, "--line", self.numbers("shot")[6])
        self.assert_exit(clean, 0)
        self.assertIn("made again 1 of 1 decisions: 0 differences", clean.stdout)

    def test_a_changed_choice_fails(self):
        number = self.numbers("choice")[0]
        other = {"play": "play_again", "play_again": "play", "replace": "play"}

        def change(data):
            data["choice"] = other.get(data["choice"], "play")

        result = run("opponent", self.tampered(number, change), timeout=300)
        self.assert_exit(result, 1)
        self.assertRegex(result.stdout, rf"(?m)^line {number}\s+choice\s+DIFFERS\s+logged \"")

    def test_a_changed_strike_number_fails_at_full_precision(self):
        number = self.numbers("shot")[2]

        def change(data):
            data["strike"]["aim_rad"] = data["strike"]["aim_rad"] * (1 + 1e-15) + 1e-17

        result = run("opponent", self.tampered(number, change), "--line", number, timeout=300)
        self.assert_exit(result, 1)
        self.assertRegex(result.stdout, rf"(?m)^line {number}\s+strike\s+DIFFERS")

    def test_a_stroke_without_its_decision_is_not_replayed(self):
        number = self.numbers("shot")[1]
        shot_id = self.lines[number - 1]["data"]["shot_id"]
        kept = [line for line in self.lines
                if not (line["event"] == "rules.decision" and line["data"].get("shot_id") == shot_id)]
        result = run("opponent", self.write_jsonl("cut.jsonl", kept), "--line", number, timeout=300)
        self.assert_exit(result, 0)
        self.assertIn(f"NOT REPLAYED line {number}: no rules.decision line with shot_id {shot_id}", result.stdout)
        self.assertIn("made again 0 of 1 decisions: 0 differences, 0 unreadable, 1 not replayed", result.stdout)

    def test_a_level_that_does_not_load_is_unreadable(self):
        number = self.numbers("shot")[0]
        result = run("opponent", self.tampered(number, lambda data: data.update(level="nosuch")), timeout=300)
        self.assert_exit(result, 1)
        self.assertRegex(result.stdout, rf"(?m)^line {number}\s+ERROR\s+level 'nosuch': no core/data/opponent_nosuch")
        self.assertIn("1 unreadable", result.stdout)

    def test_the_standins_decisions_are_skipped(self):
        log = self.tmp / "match.jsonl"
        self.assert_exit(run("match", "--frames", 1, "--best-of", 1, "--seed", 2, "--threads", 1, "--out",
                             self.tmp / "failures", "--log", log, timeout=300), 0)
        decisions = sum(json.loads(line)["event"] == "opponent.decision"
                        for line in log.read_text(encoding="utf-8").splitlines())
        result = run("opponent", log, timeout=300)
        self.assert_exit(result, 0)
        self.assertIn(f"made again 0 of 0 decisions: 0 differences, 0 unreadable, 0 not replayed, 0 frames not "
                      f"rebuilt, 0 struck otherwise; 0 differ across platforms; 0 refused in the game; "
                      f"{decisions} without a level skipped", result.stdout)

    def test_bad_arguments_and_input_exit_2(self):
        not_a_decision = next(number for number, line in enumerate(self.lines, start=1)
                              if line["event"] == "frame.started")
        empty = self.write_jsonl("no_decisions.jsonl", [line for line in self.lines
                                                        if line["event"] != "opponent.decision"])
        for args, message in (((), "missing argument"),
                              ((self.tmp / "missing.jsonl",), "cannot open"),
                              ((self.log, "--line", "0"), "--line: '0'"),
                              ((self.log, "--line", len(self.lines) + 5), f"has {len(self.lines)} lines"),
                              ((self.log, "--line", not_a_decision), "is not an opponent.decision event"),
                              ((self.log, "--table", self.tmp / "missing.json"), "was refused"),
                              ((self.log, "--bogus"), "unknown option --bogus"),
                              ((empty,), "has no opponent.decision lines")):
            with self.subTest(args=args):
                result = run("opponent", *args)
                self.assert_exit(result, 2)
                self.assertIn(message, result.stderr)


if __name__ == "__main__":
    unittest.main()
