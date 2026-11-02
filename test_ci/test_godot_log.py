"""`godot_tools.py check-godot-log`: the export steps fail on any script or extension error in
the headless editor's output, which Godot itself exits 0 on."""

import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import godot_tools  # noqa: E402

ESC = "\x1b"


def progress(text):
    return f"[  33% ] {ESC}[90m{ESC}[1mfirst_scan_filesystem{ESC}[22m | {text}{ESC}[39m{ESC}[0m"


# Shaped like the macOS editor's import without its extension (run 41346458241): stdout progress
# lines land between an error and its "at:" line.
BROKEN = [
    "Godot Engine v4.7.1.stable.official.a13da4feb - https://godotengine.org",
    "",
    progress("Loading global class names..."),
    "ERROR: No GDExtension library found for current OS and architecture (macos.arm64) in configuration "
    "file: res://bin/onefourseven.gdextension",
    progress("Verifying GDExtensions..."),
    "   at: parse_gdextension_file (core/extension/gdextension_library_loader.cpp:369)",
    "ERROR: GDExtension dynamic library not found: 'res://bin/onefourseven.gdextension'.",
    "   at: open_library (core/extension/gdextension.cpp:810)",
    'SCRIPT ERROR: Parse Error: Identifier "OfsLog" not declared in the current scope.',
    "          at: GDScript::reload (res://scripts/autoload/params.gd:22)",
    'ERROR: Failed to load script "res://scripts/autoload/params.gd" with error "Parse error".',
    "   at: load (modules/gdscript/gdscript_resource_format.cpp:46)",
    progress("Initializing plugins..."),
    "ERROR: Failed to create an autoload, script 'res://scripts/autoload/params.gd' is not compiling.",
    'SCRIPT ERROR: Parse Error: Could not find base class "OfsDevlink".',
    f"{ESC}[0m",
    progress("Starting file scan..."),
    "          at: GDScript::reload (res://scripts/autoload/link.gd:1)",
    "ERROR: Can't open dynamic library: res://bin/libonefourseven.macos.template_debug.arm64.dylib.",
    "ERROR: Error loading extension: 'res://bin/onefourseven.gdextension'.",
    "   at: load_extensions (core/extension/gdextension_manager.cpp:333)",
]

CLEAN = [
    "Godot Engine v4.7.1.stable.official.a13da4feb - https://godotengine.org",
    "",
    progress("Verifying GDExtensions..."),
    f"{ESC}[92m[ DONE ]{ESC}[39m {ESC}[1mfirst_scan_filesystem{ESC}[22m",
    "WARNING: res://icons/icon.png: Texture detected as used in 3D. Enabling mipmap generation.",
    "   at: _scan_new_dir (editor/file_system/editor_file_system.cpp:1)",
    "savepack: begin: Packing",
    "savepack: end",
]


def number_of(lines, text):
    return next(i for i, line in enumerate(lines, 1) if line.startswith(text))


class Problems(unittest.TestCase):
    def write(self, folder, name, lines, end="\n"):
        path = Path(folder) / name
        path.write_text(end.join(lines) + end, encoding="utf-8", newline="")
        return path

    def test_every_script_and_extension_error_with_its_line(self):
        with tempfile.TemporaryDirectory() as temp:
            path = self.write(temp, "import.log", BROKEN)
            problems = godot_tools.godot_log_problems(path)
        where = path.as_posix()

        def at(text):
            return f"{where}:{number_of(BROKEN, text)}: {text}"

        self.assertEqual([
            at("ERROR: No GDExtension library found for current OS and architecture (macos.arm64) in configuration "
               "file: res://bin/onefourseven.gdextension"),
            at("ERROR: GDExtension dynamic library not found: 'res://bin/onefourseven.gdextension'."),
            at('SCRIPT ERROR: Parse Error: Identifier "OfsLog" not declared in the current scope.')
            + " (res://scripts/autoload/params.gd:22)",
            at('ERROR: Failed to load script "res://scripts/autoload/params.gd" with error "Parse error".'),
            at("ERROR: Failed to create an autoload, script 'res://scripts/autoload/params.gd' is not compiling."),
            # Its "at:" line comes after two progress lines.
            at('SCRIPT ERROR: Parse Error: Could not find base class "OfsDevlink".')
            + " (res://scripts/autoload/link.gd:1)",
            at("ERROR: Can't open dynamic library: res://bin/libonefourseven.macos.template_debug.arm64.dylib."),
            at("ERROR: Error loading extension: 'res://bin/onefourseven.gdextension'."),
        ], problems)

    def test_a_clean_log_has_none(self):
        with tempfile.TemporaryDirectory() as temp:
            self.assertEqual([], godot_tools.godot_log_problems(self.write(temp, "export.log", CLEAN)))

    def test_crlf_and_colour_codes_do_not_hide_or_shift_anything(self):
        with tempfile.TemporaryDirectory() as temp:
            plain = godot_tools.godot_log_problems(self.write(temp, "a.log", BROKEN))
            crlf = godot_tools.godot_log_problems(self.write(temp, "b.log", BROKEN, end="\r\n"))
        self.assertEqual([p.replace("/a.log:", "/b.log:") for p in plain], crlf)
        self.assertFalse(any(ESC in p for p in plain))

    def test_a_script_error_without_a_script_location_is_still_listed(self):
        lines = ["SCRIPT ERROR: Invalid call. Nonexistent function 'x' in base 'Nil'.",
                 "   at: call (core/variant/variant_call.cpp:1)"]
        with tempfile.TemporaryDirectory() as temp:
            problems = godot_tools.godot_log_problems(self.write(temp, "run.log", lines))
        self.assertEqual(1, len(problems))
        self.assertTrue(problems[0].endswith(":1: SCRIPT ERROR: Invalid call. Nonexistent function 'x' in base 'Nil'."))

    def test_a_missing_or_empty_log_is_a_problem(self):
        with tempfile.TemporaryDirectory() as temp:
            missing = godot_tools.godot_log_problems(Path(temp) / "never-written.log")
            empty = godot_tools.godot_log_problems(self.write(temp, "empty.log", [""]))
        self.assertEqual(1, len(missing))
        self.assertTrue(missing[0].endswith("never-written.log: no such log"))
        self.assertEqual(1, len(empty))
        self.assertTrue(empty[0].endswith("empty.log: empty; Godot printed nothing"))


class Command(unittest.TestCase):
    def run_main(self, *args):
        with mock.patch("sys.stdout", io.StringIO()) as out, mock.patch("sys.stderr", io.StringIO()):
            code = godot_tools.main(["check-godot-log", *args])
        return code, out.getvalue()

    def test_reports_every_problem_in_every_log_then_fails(self):
        with tempfile.TemporaryDirectory() as temp:
            broken = Path(temp) / "import.log"
            broken.write_text("\n".join(BROKEN) + "\n", encoding="utf-8")
            clean = Path(temp) / "export.log"
            clean.write_text("\n".join(CLEAN) + "\n", encoding="utf-8")
            missing = Path(temp) / "missing.log"
            code, out = self.run_main(str(broken), str(clean), str(missing))
        self.assertEqual(1, code)
        errors = [line for line in out.splitlines() if line.startswith("error: ")]
        self.assertEqual(8 + 1 + 1, len(errors), out)
        self.assertEqual(8, sum("import.log:" in line for line in errors))
        self.assertIn("missing.log: no such log", out)
        self.assertTrue(errors[-1].startswith("error: 9 script or extension problems in "))

    def test_passes_clean_logs(self):
        with tempfile.TemporaryDirectory() as temp:
            paths = []
            for name in ("import.log", "export.log"):
                paths.append(Path(temp) / name)
                paths[-1].write_text("\n".join(CLEAN) + "\n", encoding="utf-8")
            code, out = self.run_main(*map(str, paths))
        self.assertEqual(0, code, out)
        self.assertIn("no script or extension errors", out)
        self.assertNotIn("error:", out)

    def test_needs_at_least_one_log(self):
        self.assertEqual(2, self.run_main()[0])


if __name__ == "__main__":
    unittest.main()
