import contextlib
import io
import json
import pathlib
import shutil
import tempfile
import unittest

import check_logs

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "core" / "tests" / "fixtures" / "log" / "valid_events.jsonl"


def event(name="app.lifecycle", **overrides):
    defaults = {
        "app.lifecycle": ("app", "info", {"state": "foreground"}),
        "app.error": ("app", "error", {"kind": "script", "message": "boom", "line": 12}),
        "perf.second": ("perf", "info", {
            "fps": 60, "frame_ms_avg": 16.6, "frame_ms_p95": 17.1, "frame_ms_max": 20.0,
            "dropped_frames": 0, "static_memory_mb": 120.5, "thermal_state": "nominal", "idle": True}),
        "harness.command": ("harness", "info", {"command": "dump", "args": [], "ok": True}),
    }
    subsystem, level, data = defaults[name]
    line = {"ts": 1762041600.125, "session": "20261102-000000-abcdef", "build": "dev",
            "platform": "windows", "subsystem": subsystem, "level": level, "event": name, "data": data}
    line.update(overrides)
    return line


def dumps(value):
    return json.dumps(value)


class ValidateLineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.schema = check_logs.load_schema()

    def check(self, line):
        return check_logs.validate_line(line if isinstance(line, str) else dumps(line), self.schema)

    def assert_bad(self, line, fragment):
        reason = self.check(line)
        self.assertIsNotNone(reason, f"accepted {line}")
        self.assertIn(fragment, reason)

    def test_valid_events_pass(self):
        for name in ("app.lifecycle", "app.error", "perf.second", "harness.command"):
            self.assertIsNone(self.check(event(name)), name)

    def test_not_json(self):
        self.assert_bad("{not json", "not JSON")

    def test_nan_is_not_json(self):
        self.assert_bad(dumps(event()).replace("1762041600.125", "NaN"), "not JSON")

    def test_not_an_object(self):
        self.assert_bad("[1, 2]", "not an object")

    def test_missing_envelope_key(self):
        line = event()
        del line["build"]
        self.assert_bad(line, "missing key build")

    def test_extra_envelope_key(self):
        self.assert_bad(event(extra=1), "unexpected key extra")

    def test_ts_must_be_a_number(self):
        self.assert_bad(event(ts="1762041600"), "ts is string")
        self.assert_bad(event(ts=True), "ts is boolean")
        self.assertIsNone(self.check(event(ts=1762041600)))

    def test_envelope_strings(self):
        for key in ("session", "build", "platform", "subsystem", "level", "event"):
            self.assert_bad(event(**{key: 5}), f"{key} is integer")

    def test_data_must_be_an_object(self):
        self.assert_bad(event(data=[]), "data is array")

    def test_level_must_be_known(self):
        self.assert_bad(event(level="fatal"), "level 'fatal' is not one of")

    def test_unknown_event(self):
        self.assert_bad(event(event="app.exploded"), "unknown event 'app.exploded'")

    def test_subsystem_must_match_event(self):
        self.assert_bad(event(subsystem="perf"), "does not match")

    def test_level_must_be_allowed_for_event(self):
        self.assert_bad(event(level="warn"), "not allowed for app.lifecycle")
        self.assertIsNone(self.check(event("app.error", level="warn")))

    def test_required_field_missing(self):
        self.assert_bad(event(data={}), "missing required field 'state'")

    def test_optional_field_may_be_absent(self):
        self.assertIsNone(self.check(event("app.error", data={"kind": "assert", "message": "x"})))

    def test_unlisted_fields_allowed(self):
        self.assertIsNone(self.check(event(data={"state": "background", "extra": [1]})))

    def test_field_types(self):
        cases = [
            ("perf.second", "dropped_frames", 1.0, "not integer"),
            ("perf.second", "dropped_frames", False, "not integer"),
            ("perf.second", "fps", True, "not number"),
            ("perf.second", "fps", "60", "not number"),
            ("perf.second", "idle", 0, "not boolean"),
            ("app.lifecycle", "state", 3, "not string"),
            ("harness.command", "args", {}, "not array"),
        ]
        for name, field, value, fragment in cases:
            line = event(name)
            line["data"] = dict(line["data"], **{field: value})
            self.assert_bad(line, f"data.{field}")
            self.assert_bad(line, fragment)
        line = event("perf.second")
        line["data"]["fps"] = 59
        self.assertIsNone(self.check(line), "integer is a number")

    def test_object_type(self):
        schema = {"events": {"x.y": {"subsystem": "x", "levels": ["info"],
                                     "fields": {"o": {"type": "object", "required": True}}}}}
        line = dict(event(), event="x.y", subsystem="x", data={"o": []})
        self.assertIn("not object", check_logs.validate_line(dumps(line), schema))
        line["data"] = {"o": {}}
        self.assertIsNone(check_logs.validate_line(dumps(line), schema))

    def test_any_accepts_everything(self):
        for value in (None, 1, "x", [1], {"a": 1}, True):
            line = event("harness.command")
            line["data"]["result"] = value
            self.assertIsNone(self.check(line), value)

    @unittest.skipUnless(FIXTURE.exists(), f"{FIXTURE} is missing (agent A creates it)")
    def test_fixture_lines_all_valid(self):
        lines = [l for l in FIXTURE.read_text(encoding="utf-8").split("\n") if l.strip()]
        self.assertTrue(lines)
        for number, line in enumerate(lines, start=1):
            self.assertIsNone(self.check(line), f"{FIXTURE.name}:{number}")


class MainTest(unittest.TestCase):
    def setUp(self):
        self.dir = pathlib.Path(tempfile.mkdtemp())

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def write(self, relative, lines):
        path = self.dir / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"".join((l if isinstance(l, bytes) else dumps(l).encode()) + b"\n" for l in lines))
        return path

    def run_main(self, *args):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = check_logs.main([str(a) for a in args])
        return code, out.getvalue()

    def test_good_file(self):
        path = self.write("a.jsonl", [event(), event("perf.second")])
        code, out = self.run_main(path)
        self.assertEqual(code, 0)
        self.assertIn("ok 2 lines", out)

    def test_bad_lines_reported_with_line_numbers(self):
        path = self.write("a.jsonl", [event(), event(level="nope"), b"garbage"])
        code, out = self.run_main(path)
        self.assertEqual(code, 1)
        self.assertIn(f"{path}:2: level 'nope'", out)
        self.assertIn(f"{path}:3: not JSON", out)
        self.assertNotIn(":1:", out)

    def test_empty_line_and_bad_utf8(self):
        path = self.write("a.jsonl", [event(), b"", b"\xff\xfe"])
        code, out = self.run_main(path)
        self.assertEqual(code, 1)
        self.assertIn(":2: empty line", out)
        self.assertIn(":3: not UTF-8", out)

    def test_folders_are_searched_recursively(self):
        self.write("logs/2026-11-02/one.jsonl", [event()])
        self.write("logs/2026-11-03/deep/two.jsonl", [event(), event()])
        self.write("logs/notes.txt", [b"not a log"])
        code, out = self.run_main(self.dir / "logs")
        self.assertEqual(code, 0)
        self.assertIn("one.jsonl: ok 1 lines", out)
        self.assertIn("two.jsonl: ok 2 lines", out)
        self.assertNotIn("notes.txt", out)

    def test_no_files_fails(self):
        (self.dir / "empty").mkdir()
        code, out = self.run_main(self.dir / "empty")
        self.assertEqual(code, 1)
        self.assertIn("no log files", out)

    def test_missing_path_fails(self):
        code, out = self.run_main(self.dir / "nope.jsonl")
        self.assertEqual(code, 1)
        self.assertIn("no such file", out)


if __name__ == "__main__":
    unittest.main()
