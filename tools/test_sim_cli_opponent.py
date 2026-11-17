"""Tests of ofs_sim_cli's opponent command through the command line, against the build
test_sim_cli.py finds (the Debug build, or the one OFS_SIM_CLI names)."""
import copy
import json
import pathlib
import tempfile
import unittest

from test_sim_cli import CliTestCase, run, shown


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


class OpponentTest(OpponentCase):
    def test_every_decision_of_the_log_is_made_again_alike(self):
        shots, choices = self.numbers("shot"), self.numbers("choice")
        self.assertTrue(shots)
        self.assertTrue(choices, "the run had no choice to make again")
        result = run("opponent", self.log, timeout=300)
        self.assert_exit(result, 0)
        total = len(shots) + len(choices)
        self.assertIn(f"made again {total} of {total} decisions: 0 differences, 0 unreadable, 0 not replayed; "
                      "0 without a level skipped", result.stdout)
        self.assertRegex(result.stdout, r"(?m)^level beginner: core/data/opponent_beginner\.json, hash [0-9a-f]+$")
        self.assertNotIn("DIFFERS", result.stdout)

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
        self.assertIn(f"made again 0 of 0 decisions: 0 differences, 0 unreadable, 0 not replayed; "
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
