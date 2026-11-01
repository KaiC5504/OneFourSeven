"""scripts/gate.py's own logic: how lanes fail, what the Godot runner and the session log must show,
and when the gate-passed record that testflight.py trusts is written or cleared."""

import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import gate  # noqa: E402

GOOD_RUN = "pass  test_a.gd::test_one\nloaded 2/2 test files\n1/1 passed\n"


def event(name, **data):
    return json.dumps({"ts": 1.0, "session": "s", "build": "0.1.0 (dev)", "platform": "windows",
                       "subsystem": name.split(".")[0], "level": "error", "event": name, "data": data})


class TestRunOutput(unittest.TestCase):
    def test_a_clean_run_has_no_problems(self):
        self.assertEqual([], gate.test_run_problems(GOOD_RUN, 2))

    def test_a_runner_without_the_loaded_line_fails(self):
        problems = gate.test_run_problems("1/1 passed\n", 2)
        self.assertIn("printed 0 'loaded N/M test files' lines", problems[0])

    def test_a_test_file_that_did_not_load_fails(self):
        problems = gate.test_run_problems(GOOD_RUN.replace("loaded 2/2", "loaded 1/2"), 2)
        self.assertIn("loaded 1/2 test files; game/tests has 2 test_*.gd", problems[0])

    def test_a_test_file_the_runner_never_saw_fails(self):
        self.assertTrue(gate.test_run_problems(GOOD_RUN, 3))

    def test_script_errors_fail_a_passing_run(self):
        output = GOOD_RUN + "SCRIPT ERROR: Invalid call. Nonexistent function 'x'.\n"
        problems = gate.test_run_problems(output, 2)
        self.assertEqual(1, len(problems))
        self.assertIn("Nonexistent function", problems[0])


class SessionErrors(unittest.TestCase):
    def write(self, folder, *lines):
        path = Path(folder) / "session.jsonl"
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return [path]

    def test_reports_each_app_error(self):
        with tempfile.TemporaryDirectory() as temp:
            paths = self.write(temp, event("app.start"), event("app.error", kind="script", message="boom"),
                               "not json", event("perf.second"))
            self.assertEqual(["session.jsonl:2: script: boom"], gate.unexpected_app_errors(paths))

    def test_allow_list_entries_match_kind_and_message(self):
        with tempfile.TemporaryDirectory() as temp:
            paths = self.write(temp, event("app.error", kind="warning", message="known harmless thing"),
                               event("app.error", kind="error", message="known harmless thing"))
            with mock.patch.object(gate, "ALLOWED_APP_ERRORS", [("warning", r"^known harmless")]):
                found = gate.unexpected_app_errors(paths)
        self.assertEqual(["session.jsonl:2: error: known harmless thing"], found)

    def test_the_allow_list_starts_empty(self):
        self.assertEqual([], gate.ALLOWED_APP_ERRORS)


class SessionLogRun(unittest.TestCase):
    """The logs lane runs the game with its own user:// and the link off."""

    def run_lane(self, session_lines):
        calls = []

        def fake_run(command, cwd=gate.ROOT, timeout=900, env=None):
            calls.append((list(command), env))
            if "--quit-after" in command:
                base = Path(env.get("APPDATA") or env.get("HOME") or env.get("XDG_DATA_HOME"))
                logs = gate.isolated_user_dir(base)[1] / "logs"
                logs.mkdir(parents=True)
                (logs / "abc.jsonl").write_text("\n".join(session_lines) + "\n", encoding="utf-8")
            return True, "ok"

        with tempfile.TemporaryDirectory() as temp, \
                mock.patch.object(gate, "LOG_RUN_USER", Path(temp) / "user"), \
                mock.patch.object(gate, "LOGS", Path(temp) / "gate"), \
                mock.patch.object(gate, "godot", return_value="godot"), \
                mock.patch.object(gate, "run", side_effect=fake_run):
            stale = gate.isolated_user_dir(Path(temp) / "user")[1] / "logs" / "old.jsonl"
            stale.parent.mkdir(parents=True)
            stale.write_text(event("app.error", kind="error", message="from another run") + "\n")
            ok, output = gate.session_log_run()
        return ok, output, calls

    def test_passes_a_clean_session_and_ignores_older_ones(self):
        ok, output, calls = self.run_lane([event("app.start")])
        self.assertTrue(ok, output)
        game, env = calls[0]
        self.assertEqual(["--", "--no-link"], game[-2:])
        self.assertTrue(env)
        self.assertIn("check_logs.py", str(calls[1][0][1]))

    def test_fails_on_an_app_error(self):
        ok, output, _ = self.run_lane([event("app.start"), event("app.error", kind="error", message="x")])
        self.assertFalse(ok)
        self.assertIn("abc.jsonl:2: error: x", output)

    def test_user_dir_is_where_godot_puts_user_data(self):
        env, user = gate.isolated_user_dir(Path("base"))
        self.assertEqual(Path("base"), Path(next(iter(env.values()))))
        self.assertEqual("One Four Seven", user.name)
        self.assertEqual("app_userdata", user.parent.name)


class Lanes(unittest.TestCase):
    def test_the_tools_tests_get_the_params_hash_tool(self):
        lanes = {lane.name: lane for lane in gate.lanes(quick=True)}
        self.assertEqual(["build"], lanes["tools"].after)
        self.assertIn(gate.params_hash_built, lanes["build"].steps)
        with mock.patch.object(gate, "run", return_value=(True, "")) as run, \
                mock.patch.object(gate, "params_hash_tool", return_value=Path("hash.exe")):
            lanes["tools"].steps[0]()
        self.assertEqual({"OFS_PARAMS_HASH": "hash.exe"}, run.call_args.kwargs["env"])

    def test_a_missing_params_hash_tool_fails_the_build(self):
        with tempfile.TemporaryDirectory() as temp, mock.patch.object(gate, "BUILD", Path(temp)):
            with self.assertRaises(SystemExit) as caught:
                gate.params_hash_built()
        self.assertIn("ofs_params_hash", str(caught.exception))

    def test_a_missing_test_folder_fails(self):
        ok, output = gate.python_tests("no_such_folder")()
        self.assertEqual((False, "no_such_folder/ is missing"), (ok, output))

    def test_quick_leaves_out_only_the_export(self):
        full = [lane.name for lane in gate.lanes(quick=False)]
        quick = [lane.name for lane in gate.lanes(quick=True)]
        self.assertEqual(full, quick + ["windows-export"])


class RunLane(unittest.TestCase):
    def run_one(self, *steps, after=(), futures=None):
        with tempfile.TemporaryDirectory() as temp, mock.patch.object(gate, "LOGS", Path(temp)):
            result = gate.run_lane(gate.Lane("x", list(steps), list(after)), futures or {})
            log = (Path(temp) / "x.log").read_text(encoding="utf-8") if (Path(temp) / "x.log").exists() else ""
        return result, log

    def test_an_unexpected_exception_fails_the_lane_with_its_traceback(self):
        def broken():
            raise KeyError("missing thing")
        (name, status, _, tail), log = self.run_one(broken)
        self.assertEqual("FAIL", status)
        self.assertIn("Traceback", log)
        self.assertIn("KeyError: 'missing thing'", log)
        self.assertIn("KeyError", tail)

    def test_system_exit_fails_the_lane_with_its_message(self):
        def stop():
            raise SystemExit("no vswhere.exe")
        (_, status, _, tail), log = self.run_one(stop)
        self.assertEqual("FAIL", status)
        self.assertIn("no vswhere.exe", log)

    def test_steps_after_a_failure_do_not_run(self):
        later = mock.Mock(return_value=(True, "later"))
        (_, status, _, _), log = self.run_one(lambda: (False, "first"), later)
        self.assertEqual("FAIL", status)
        later.assert_not_called()

    def test_a_failed_dependency_blocks_the_lane(self):
        failed = mock.Mock()
        failed.result.return_value = ("build", "FAIL", 1.0, "")
        step = mock.Mock(return_value=(True, ""))
        (_, status, _, tail), _ = self.run_one(step, after=["build"], futures={"build": failed})
        self.assertEqual(("blocked", "not run: build did not pass"), (status, tail))
        step.assert_not_called()


class Main(unittest.TestCase):
    HEAD = "0123456789abcdef0123456789abcdef01234567"

    def main(self, plan, argv=(), dirty=False, recorded="old"):
        with tempfile.TemporaryDirectory() as temp:
            stamp = Path(temp) / "gate-passed"
            if recorded:
                stamp.write_text(recorded + "\n")

            def git(*args):
                return {("rev-parse", "--git-path", "gate-passed"): str(stamp),
                        ("status", "--porcelain"): " M x" if dirty else "",
                        ("rev-parse", "HEAD"): self.HEAD}[args]

            lanes = plan if callable(plan) else (lambda quick: plan)
            with mock.patch.object(gate, "LOGS", Path(temp) / "gate"), \
                    mock.patch.object(gate, "git", side_effect=git), \
                    mock.patch.object(gate, "lanes", side_effect=lanes), \
                    mock.patch.object(sys, "argv", ["gate.py", *argv]), \
                    mock.patch("sys.stdout", io.StringIO()) as out:
                code = gate.main()
            record = stamp.read_text().strip() if stamp.exists() else None
        return code, record, out.getvalue()

    def passing(self, name="a"):
        return gate.Lane(name, [lambda: (True, "fine")])

    def test_a_full_pass_on_a_clean_tree_records_the_commit(self):
        code, record, out = self.main([self.passing()])
        self.assertEqual((0, self.HEAD), (code, record))
        self.assertIn("gate passed", out)

    def test_a_pass_with_uncommitted_changes_records_nothing(self):
        code, record, out = self.main([self.passing()], dirty=True)
        self.assertEqual((0, None), (code, record))
        self.assertIn("uncommitted changes", out)

    def test_a_quick_pass_leaves_the_record_alone(self):
        self.assertEqual((0, "old"), self.main([self.passing()], argv=["--quick"])[:2])

    def test_a_failed_lane_fails_the_gate_and_clears_the_record(self):
        failing = gate.Lane("b", [lambda: (False, "it broke")])
        for argv in ((), ("--quick",)):
            code, record, out = self.main([self.passing(), failing], argv=argv)
            self.assertEqual((1, None), (code, record), argv)
            self.assertIn("FAIL  b", out)
            self.assertIn("it broke", out)
            self.assertIn("gate failed", out)

    def test_a_crashing_lane_still_prints_the_summary(self):
        def crash():
            raise RuntimeError("bad lane")
        code, record, out = self.main([self.passing(), gate.Lane("c", [crash])])
        self.assertEqual((1, None), (code, record))
        self.assertIn("pass  a", out)
        self.assertIn("RuntimeError: bad lane", out)

    def test_a_crash_outside_the_lanes_fails_and_clears_the_record(self):
        def broken_plan(quick):
            raise ValueError("no plan")
        code, record, out = self.main(broken_plan, argv=["--quick"])
        self.assertEqual((1, None), (code, record))
        self.assertIn("ValueError: no plan", out)
        self.assertIn("gate failed", out)

    def test_a_gate_with_nothing_to_run_fails(self):
        self.assertEqual((1, None), self.main([])[:2])

    def test_a_lane_after_an_unknown_lane_fails(self):
        waiting = gate.Lane("d", [lambda: (True, "")], after=["missing"])
        code, record, out = self.main([waiting])
        self.assertEqual((1, None), (code, record))
        self.assertIn("KeyError: 'missing'", out)


if __name__ == "__main__":
    unittest.main()
