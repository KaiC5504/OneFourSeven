import contextlib
import io
import json
import os
import pathlib
import tempfile
import unittest
from unittest import mock

import play_match
from harness_client import HarnessError


def line(event, data, level="info"):
    return json.dumps({"ts": 1.0, "session": "s", "build": "dev", "platform": "windows", "subsystem": "x",
                       "level": level, "event": event, "data": data})


def match_log(match_id="s-m1", frames=((63, 41, 0), (20, 71, 1), (55, 54, 0)), errors=(), ended=True):
    """A session log of one match: a decision or two a frame, each frame's end, the match's."""
    lines = [line("app.start", {}), line("match.started", {"match_id": match_id})]
    won = [0, 0]
    for number, (mine, theirs, winner) in enumerate(frames, 1):
        lines.append(line("rules.decision", {"match_id": match_id, "frame": number}))
        lines.append(line("rules.decision", {"match_id": match_id, "frame": number}))
        won[winner] += 1
        lines.append(line("frame.ended", {"match_id": match_id, "summary": {
            "number": number, "winner": winner, "reason": "black", "scores": [mine, theirs], "strokes": 2}}))
    for message in errors:
        lines.append(line("app.error", {"kind": "message", "message": message}, "warn"))
    if ended:
        lines.append(line("match.ended", {"match_id": match_id, "winner": 0 if won[0] > won[1] else 1, "frames": won,
                                          "frames_played": len(frames), "reason": "frames"}))
    return "\n".join(lines) + "\n"


def levelled_log(match_id="s-m1", level="beginner"):
    """A match against a level: match_log with an opponent.decision of that level."""
    return match_log(match_id) + line("opponent.decision", {"match_id": match_id, "kind": "shot", "player": 1,
                                                            "seed": "7", "level": level}) + "\n"


def exhibition_log(match_id="s-m1", break_points=147, why="maximum"):
    """Watch a 147: the 147's strokes, then exhibition.ended and the abandoned match."""
    lines = [line("app.start", {}), line("match.started", {"match_id": match_id, "level": "maximum", "exhibition": True})]
    for _ in range(3):
        lines.append(line("opponent.decision", {"match_id": match_id, "kind": "shot", "player": 1, "seed": "1",
                                                 "level": "maximum"}))
        lines.append(line("rules.decision", {"match_id": match_id, "frame": 1}))
    lines.append(line("exhibition.ended", {"match_id": match_id, "break_points": break_points,
                                           "maximum": break_points >= 147, "why": why, "strokes": 3}))
    lines.append(line("match.ended", {"match_id": match_id, "winner": -1, "frames": [0, 0], "reason": "abandoned"}))
    return "\n".join(lines) + "\n"


class FakeGame:
    """Stands in for harness_client.Harness: answers each command from a script and, when it
    stops, leaves the session log the real game would have written."""

    def __init__(self, waits, user_dir=None, log_text=None, exit_code=0, start_error=None, screen="frame_summary"):
        self.waits = list(waits)
        self.user_dir = user_dir
        self.log_text = log_text
        self.exit_code = exit_code
        self.start_error = start_error
        self.screen = screen
        self.calls = []
        self.started = False
        self.stopped = False

    def start(self):
        if self.start_error:
            raise HarnessError(self.start_error)
        self.started = True

    def call(self, cmd, **args):
        self.calls.append((cmd, args))
        if cmd == "start_match":
            return {"match_id": "s-m1"}
        if cmd == "start_exhibition":
            return {"match_id": "s-m1", "layout": "open reds"}
        if cmd == "match_wait":
            if not self.waits:
                raise HarnessError("match_wait: still waiting on \"opponent\" after 1.0 s")
            return {"waiting": self.waits.pop(0)}
        if cmd == "match_continue":
            self.screen = "match_summary" if not self.waits else "match"
            return {"screen": self.screen}
        if cmd == "dump_state":
            if "path" in args:
                self.saved(args["path"]).write_text("{}", encoding="utf-8")
            return {"screen": self.screen}
        if cmd == "screenshot":
            path = self.saved(args["path"])
            path.write_bytes(b"png")
            return str(path)
        raise AssertionError(f"unexpected command {cmd}")

    def saved(self, path):
        """Where the game saves a path it is given: a relative one in its user folder (user://)."""
        assert not pathlib.Path(path).is_absolute(), f"an absolute path reaches the game's log: {path}"
        if self.user_dir is None:
            self.user_dir = pathlib.Path(tempfile.mkdtemp())
        self.user_dir.mkdir(parents=True, exist_ok=True)
        return self.user_dir / path

    def stop(self):
        self.stopped = True
        if self.user_dir is not None and self.log_text is not None:
            logs = self.user_dir / "logs"
            logs.mkdir(parents=True, exist_ok=True)
            (logs / "session.jsonl").write_text(self.log_text, encoding="utf-8")
        return self.exit_code

    def tail(self):
        return "the game's last words"

    def commands(self):
        return [cmd for cmd, _ in self.calls]


class ArgumentTests(unittest.TestCase):
    def test_defaults_are_a_fast_best_of_three(self):
        args = play_match.parse_args([])
        self.assertEqual((args.best_of, args.seed, args.first_breaker, args.slow, args.show), (3, 147, "random", False, False))
        self.assertEqual((args.level, args.exhibition), ("", None), "the stand-in, a match")
        self.assertEqual(args.resolution, "1912x880")
        self.assertEqual(args.out, play_match.DEFAULT_OUT)

    def test_everything_can_be_set(self):
        args = play_match.parse_args(["--best-of", "5", "--seed", "9", "--first-breaker", "computer", "--slow",
                                      "--resolution", "956x440", "--out", "x", "--timeout", "30", "--godot", "g.exe", "--show"])
        self.assertEqual((args.best_of, args.seed, args.first_breaker, args.slow, args.show), (5, 9, "computer", True, True))
        self.assertEqual((args.resolution, args.out, args.timeout, args.godot), ("956x440", pathlib.Path("x"), 30.0, "g.exe"))

    def test_a_level_or_an_exhibition(self):
        self.assertEqual(play_match.parse_args(["--level", "maximum"]).level, "maximum")
        self.assertEqual(play_match.parse_args(["--exhibition"]).exhibition, 0)
        self.assertEqual(play_match.parse_args(["--exhibition", "2"]).exhibition, 2)

    def test_bad_arguments_exit_2(self):
        for bad in (["--best-of", "4"], ["--first-breaker", "me"], ["--seed", "-1"], ["--resolution", "big"],
                    ["--timeout", "0"], ["--frobnicate"], ["--exhibition", "-1"], ["--level", "Pro Level"],
                    ["--level", "beginner", "--exhibition"]):
            with self.subTest(bad=bad), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as raised:
                    play_match.parse_args(bad)
                self.assertEqual(raised.exception.code, 2)

    def test_main_hides_itself_on_windows_unless_shown(self):
        with mock.patch.object(play_match, "hidden", return_value=7) as hidden, \
                mock.patch.object(play_match, "run", return_value=3) as run, \
                mock.patch.object(play_match.sys, "platform", "win32"), \
                mock.patch.dict(os.environ, clear=False):
            os.environ.pop(play_match.HIDDEN, None)
            self.assertEqual(play_match.main(["--seed", "5"]), 7)
            hidden.assert_called_once_with(["--seed", "5"])
            run.assert_not_called()
            self.assertEqual(play_match.main(["--show"]), 3)
            os.environ[play_match.HIDDEN] = "1"
            self.assertEqual(play_match.main([]), 3)
            self.assertEqual(hidden.call_count, 1)


class SimCliTests(unittest.TestCase):
    def test_the_environment_names_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = pathlib.Path(tmp) / "my_cli"
            exe.write_text("")
            self.assertEqual(play_match.find_sim_cli({"OFS_SIM_CLI": str(exe)}, pathlib.Path(tmp)), exe)
            self.assertIsNone(play_match.find_sim_cli({"OFS_SIM_CLI": str(exe) + ".gone"}, pathlib.Path(tmp)))

    def test_else_the_debug_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            self.assertIsNone(play_match.find_sim_cli({}, root))
            name = "ofs_sim_cli.exe" if os.name == "nt" else "ofs_sim_cli"
            exe = root / "build" / "windows" / "tools" / "sim_cli" / "Debug" / name
            exe.parent.mkdir(parents=True)
            exe.write_text("")
            self.assertEqual(play_match.find_sim_cli({}, root), exe)


class LogTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = pathlib.Path(self.tmp.name)

    def write(self, name, text):
        path = self.dir / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def test_the_newest_session_is_found_wherever_it_lies(self):
        self.assertIsNone(play_match.newest_session_log(self.dir / "logs"))
        old = self.write("logs/a.jsonl", "{}\n")
        new = self.write("logs/2026-11-15/b.jsonl", "{}\n")
        self.write("logs/rejected.jsonl", "{}\n")
        os.utime(old, (1, 1))
        os.utime(new, (2, 2))
        os.utime(self.dir / "logs" / "rejected.jsonl", (3, 3))
        self.assertEqual(play_match.newest_session_log(self.dir / "logs"), new)

    def test_a_match_is_read_from_its_lines(self):
        seen = play_match.read_log(self.write("m.jsonl", match_log()), "s-m1")
        self.assertEqual(seen["strokes"], 6)
        self.assertEqual([f["scores"] for f in seen["frames"]], [[63, 41], [20, 71], [55, 54]])
        self.assertEqual((seen["result"]["winner"], seen["result"]["frames"], seen["result"]["reason"]), (0, [2, 1], "frames"))
        self.assertEqual((seen["errors"], seen["not_json"]), ([], 0))

    def test_another_matchs_lines_do_not_count_but_every_error_does(self):
        text = match_log("s-m1") + match_log("s-m2", frames=((10, 90, 1),), errors=("something broke",))
        seen = play_match.read_log(self.write("m.jsonl", text), "s-m2")
        self.assertEqual((seen["strokes"], len(seen["frames"]), seen["result"]["frames"]), (2, 1, [0, 1]))
        self.assertEqual(seen["errors"], ["warn message: something broke"])
        everything = play_match.read_log(self.write("m.jsonl", text))
        self.assertEqual((everything["strokes"], len(everything["frames"])), (8, 4))

    def test_broken_lines_are_counted_and_a_match_cut_short_has_no_result(self):
        text = match_log(ended=False) + "not json\n[1, 2]\n\n"
        seen = play_match.read_log(self.write("m.jsonl", text), "s-m1")
        self.assertIsNone(seen["result"])
        self.assertEqual(seen["not_json"], 2)

    def test_a_frame_reads_in_one_line(self):
        said = play_match.frame_line({"number": 2, "winner": 1, "reason": "conceded", "scores": [12, 70], "strokes": 31}, 2)
        self.assertEqual(said, "  frame 2: 12-70, computer (conceded, 31 strokes)")
        self.assertIn("frame 4: ?-?, nobody", play_match.frame_line({}, 4))


class RunTests(unittest.TestCase):
    """The whole run against a scripted game and referee: no game is started."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = pathlib.Path(self.tmp.name)
        self.user = self.dir / "out" / "user" / "app"
        self.cli = self.dir / "ofs_sim_cli"
        self.cli.write_text("")
        self.args = play_match.parse_args(["--out", str(self.dir / "out"), "--timeout", "60", "--show"])
        self.referee_calls = []
        self.opponent_calls = []
        self.made = []

    def run_with(self, game, verdict=(0, "re-judged 6 decisions: all agree\n"), cli=True, off_schema=(),
                 remade=(0, "made again 1 of 1 decisions: 0 differences\n")):
        def referee(sim_cli, log):
            self.referee_calls.append((sim_cli, log))
            return verdict

        def opponent(sim_cli, log):
            self.opponent_calls.append((sim_cli, log))
            return remade

        def make_game(**kwargs):
            self.made.append(kwargs)
            return game

        said = io.StringIO()
        environ = {"OFS_SIM_CLI": str(self.cli)} if cli else {"OFS_SIM_CLI": str(self.cli) + ".gone"}
        # The logs written here are a match's bones, not whole schema lines.
        with mock.patch.dict(os.environ, environ), \
                mock.patch.object(play_match.check_logs, "check_file", return_value=(0, list(off_schema))):
            code = play_match.run(self.args, make_game=make_game, referee=referee, opponent=opponent,
                                  user_dir_for=lambda base: ({"OFS_TEST_USER": str(base)}, self.user), out=said)
            self.moved_user_dir = os.environ.get("OFS_TEST_USER")
        return code, said.getvalue()

    def game(self, **more):
        return FakeGame(["frame_summary", "frame_summary", "match_summary"], self.user, match_log(), **more)

    def test_a_clean_match_the_referee_agrees_with_is_0(self):
        game = self.game()
        code, said = self.run_with(game)
        self.assertEqual(code, 0, said)
        self.assertEqual(game.calls[0], ("start_match", {"best_of": 3, "first_breaker": "random", "seed": 147,
                                                         "autoplay": True, "fast": True}))
        self.assertEqual(game.commands(), ["start_match", "match_wait", "match_continue", "match_wait", "match_continue",
                                           "match_wait", "dump_state", "screenshot"])
        self.assertEqual([args["path"] for cmd, args in game.calls if "path" in args], ["state.json", "match_summary.png"])
        self.assertTrue(game.stopped)
        self.assertEqual(self.made, [{"resolution": "1912x880", "extra_args": ["--no-link"], "godot": None}])
        self.assertEqual(self.moved_user_dir, str((self.dir / "out").resolve() / "user"))
        self.assertTrue((self.dir / "out" / "state.json").is_file() and (self.dir / "out" / "match_summary.png").is_file())
        self.assertFalse((self.user / "state.json").exists(), "moved out of the user folder")
        self.assertEqual(self.referee_calls, [(self.cli, self.user / "logs" / "session.jsonl")])
        for words in ("6 strokes judged, 3 frames ended", "frame 2: 20-71, computer (black, 2 strokes)",
                      "result: you, frames [2, 1], frames", "referee (ofs_sim_cli referee): exit 0", "all agree", "ok:"):
            self.assertIn(words, said)

    def test_the_stand_ins_match_is_not_made_again(self):
        self.assertEqual(self.run_with(self.game())[0], 0)
        self.assertEqual(self.opponent_calls, [])

    def test_a_level_plays_the_opponents_side_and_its_decisions_are_made_again(self):
        self.args.level = "beginner"
        game = FakeGame(["frame_summary", "frame_summary", "match_summary"], self.user, levelled_log())
        code, said = self.run_with(game)
        self.assertEqual(code, 0, said)
        self.assertEqual(game.calls[0], ("start_match", {"best_of": 3, "first_breaker": "random", "seed": 147,
                                                         "autoplay": True, "fast": True, "level": "beginner"}))
        self.assertEqual(self.opponent_calls, [(self.cli, self.user / "logs" / "session.jsonl")])
        for words in ("best of 3, against beginner", "opponent (ofs_sim_cli opponent): exit 0", "0 differences"):
            self.assertIn(words, said)

    def test_a_level_whose_decisions_differ_fails(self):
        self.args.level = "beginner"
        game = FakeGame(["frame_summary", "frame_summary", "match_summary"], self.user, levelled_log())
        code, said = self.run_with(game, remade=(1, "line 7  strike DIFFERS  logged 1, remade 2\n"))
        self.assertEqual(code, 1)
        self.assertIn("strike DIFFERS", said)
        self.assertIn("not made again as logged", said)

    def test_a_level_with_no_decision_of_its_own_fails(self):
        self.args.level = "beginner"
        code, said = self.run_with(self.game())
        self.assertEqual(code, 1)
        self.assertIn("no opponent.decision of a level", said)

    def test_an_exhibition_is_watched_to_its_summary_and_reports_the_break(self):
        self.args.exhibition = 1
        game = FakeGame(["exhibition_summary"], self.user, exhibition_log())
        code, said = self.run_with(game)
        self.assertEqual(code, 0, said)
        self.assertEqual(game.calls[0], ("start_exhibition", {"layout": 1, "seed": 147, "fast": True}))
        self.assertEqual(game.commands(), ["start_exhibition", "match_wait", "dump_state", "screenshot"])
        self.assertTrue((self.dir / "out" / "exhibition_summary.png").is_file())
        self.assertEqual(len(self.opponent_calls), 1)
        for words in ("exhibition s-m1: table 1 (open reds)", "break: 147 (a maximum), ended maximum", "ok: the exhibition"):
            self.assertIn(words, said)

    def test_an_exhibition_that_breaks_down_still_reports_its_break(self):
        self.args.exhibition = 0
        code, said = self.run_with(FakeGame(["exhibition_summary"], self.user, exhibition_log(break_points=56, why="missed")))
        self.assertEqual(code, 0, said)
        self.assertIn("break: 56, ended missed", said)

    def test_an_exhibition_left_or_unended_fails(self):
        self.args.exhibition = 0
        code, said = self.run_with(FakeGame(["exhibition_summary"], self.user, exhibition_log(why="left")))
        self.assertEqual(code, 1)
        self.assertIn("was left", said)
        code, said = self.run_with(FakeGame(["shot"], self.user, match_log(ended=False)))
        self.assertEqual(code, 1)
        self.assertIn("who only watches", said)
        self.assertIn("no exhibition.ended", said)

    def test_slow_plays_every_stroke_out(self):
        self.args.slow = True
        game = self.game()
        self.assertEqual(self.run_with(game)[0], 0)
        self.assertFalse(game.calls[0][1]["fast"])

    def test_an_old_sessions_log_is_cleared_first(self):
        stale = self.user / "logs" / "old.jsonl"
        stale.parent.mkdir(parents=True)
        stale.write_text(match_log(errors=("from an earlier run",)), encoding="utf-8")
        code, said = self.run_with(self.game())
        self.assertEqual(code, 0, said)
        self.assertFalse(stale.exists())

    def test_an_app_error_in_the_log_fails(self):
        game = FakeGame(["frame_summary", "frame_summary", "match_summary"], self.user,
                        match_log(errors=("MatchController: engine.judge refused",)))
        code, said = self.run_with(game)
        self.assertEqual(code, 1)
        self.assertIn("1 app.error or rules.invalid lines", said)
        self.assertIn("engine.judge refused", said)

    def test_a_rules_invalid_in_the_log_fails(self):
        text = match_log() + line("rules.invalid", {"what": "OfsMatch.judge", "detail": "the record is another table's"}, "warn") + "\n"
        code, said = self.run_with(FakeGame(["frame_summary", "frame_summary", "match_summary"], self.user, text))
        self.assertEqual(code, 1)
        self.assertIn("rules.invalid OfsMatch.judge: the record is another table's", said)

    def test_a_line_off_the_schema_fails(self):
        code, said = self.run_with(self.game(), off_schema=[(12, "unknown event 'match.frobnicated'")])
        self.assertEqual(code, 1)
        self.assertIn("line 12: unknown event 'match.frobnicated'", said)

    def test_the_schema_check_is_check_logs_own(self):
        log = self.dir / "real.jsonl"
        log.write_text(line("frame.ended", {"summary": {}}) + "\n" + line("no.such.event", {}) + "\n", encoding="utf-8")
        count, bad = play_match.check_logs.check_file(log, play_match.check_logs.load_schema())
        self.assertEqual(count, 2)
        self.assertEqual([number for number, _ in bad], [1, 2])

    def test_a_referee_who_disagrees_fails(self):
        code, said = self.run_with(self.game(), verdict=(1, "line 40: points 4, the log says 7\n"))
        self.assertEqual(code, 1)
        self.assertIn("the log says 7", said)
        self.assertIn("does not agree", said)

    def test_a_match_that_never_ends_fails_and_still_reads_the_log(self):
        game = FakeGame(["frame_summary"], self.user, match_log(frames=((63, 41, 0),), ended=False))
        code, said = self.run_with(game)
        self.assertEqual(code, 1)
        self.assertIn("did not finish", said)
        self.assertIn("still waiting", said)
        self.assertIn("no match.ended", said)
        self.assertTrue(game.stopped)
        self.assertEqual(len(self.referee_calls), 1)

    def test_a_match_waiting_for_the_person_fails(self):
        code, said = self.run_with(FakeGame(["shot"], self.user, match_log(ended=False)))
        self.assertEqual(code, 1)
        self.assertIn("waits for the person ('shot')", said)

    def test_a_game_that_crashes_on_the_way_out_fails(self):
        code, said = self.run_with(self.game(exit_code=-11))
        self.assertEqual(code, 1)
        self.assertIn("exited with code -11", said)
        self.assertIn("the game's last words", said)

    def test_no_log_fails(self):
        game = FakeGame(["match_summary"], None, None, screen="match_summary")
        code, said = self.run_with(game)
        self.assertEqual(code, 1)
        self.assertIn("no session log", said)

    def test_an_abandoned_or_empty_match_fails(self):
        text = "\n".join([line("match.ended", {"match_id": "s-m1", "winner": -1, "frames": [0, 0], "reason": "abandoned"})]) + "\n"
        code, said = self.run_with(FakeGame(["match_summary"], self.user, text))
        self.assertEqual(code, 1)
        self.assertIn("abandoned", said)
        self.assertIn("no rules.decision", said)

    def test_without_the_game_or_the_referee_it_is_2(self):
        code, said = self.run_with(FakeGame([], self.user, None, start_error="cannot start godot"))
        self.assertEqual(code, 2)
        self.assertIn("cannot start godot", said)
        code, said = self.run_with(self.game(), cli=False)
        self.assertEqual(code, 2)
        self.assertIn("OFS_SIM_CLI", said)
        self.assertEqual(self.made[1:], [], "the game is not started with no referee to check it")

    def test_the_time_allowed_is_the_whole_matchs(self):
        ticks = iter([0.0, 10.0, 59.0, 61.0, 62.0])
        game = FakeGame(["frame_summary", "frame_summary", "frame_summary"])
        played = play_match.play(game, self.args, self.dir, clock=lambda: next(ticks))
        self.assertFalse(played["finished"])
        self.assertIn("not over after 60 s (2 frames played)", played["problem"])
        self.assertEqual([args["timeout_s"] for cmd, args in game.calls if cmd == "match_wait"], [50.0, 1.0])


if __name__ == "__main__":
    unittest.main()
