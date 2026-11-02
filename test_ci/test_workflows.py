"""Static checks on windows-ci.yml and ios-compile.yml. The repo is public, so their logs and
artifacts are too: no secrets, no tracing, no unpinned code, no unchecked downloads. ExportSteps
also runs both iOS workflows' export step under bash with a fake Godot."""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parent))

import test_build_scripts  # noqa: E402  Builds an export shaped like Godot's.

ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = ROOT / ".github" / "workflows"
NAMES = ("windows-ci.yml", "ios-compile.yml")
# An action may live in a folder of its repo (actions/cache/restore).
PINNED = re.compile(r"^[\w.-]+/[\w.-]+(?:/[\w.-]+)*@[0-9a-f]{40}$")
PINNED_LINE = re.compile(r"uses: ([\w.-]+/[\w.-]+(?:/[\w.-]+)*@[0-9a-f]{40}) # v\d+(?:\.\d+)*$", re.MULTILINE)
MACOS_DYLIB = "game/bin/libonefourseven.macos.template_debug.arm64.dylib"
TRACING = re.compile(r"set -\w*x|-o xtrace|\bbash -\w*x|Set-PSDebug")
DOWNLOADERS = re.compile(r"\b(curl|wget|Invoke-WebRequest|iwr|Invoke-RestMethod|brew install|pip install(?![^\n]*--require-hashes))\b")


def load(name):
    text = (WORKFLOWS / name).read_text(encoding="utf-8")
    return text, yaml.safe_load(text)


def steps(doc):
    for job_name, job in doc["jobs"].items():
        for step in job.get("steps", []):
            yield job_name, step


class BothWorkflows(unittest.TestCase):
    def each(self):
        return [(name, *load(name)) for name in NAMES]

    def test_actions_are_pinned_to_commits_with_a_version_comment(self):
        for name, text, doc in self.each():
            uses = [step["uses"] for _, step in steps(doc) if "uses" in step]
            self.assertTrue(uses)
            for use in uses:
                self.assertRegex(use, PINNED)
            commented = PINNED_LINE.findall(text)
            self.assertEqual(sorted(uses), sorted(commented))

    def test_checkout_never_keeps_the_token_and_brings_godot_cpp(self):
        for name, text, doc in self.each():
            checkouts = [s for _, s in steps(doc) if s.get("uses", "").startswith("actions/checkout@")]
            self.assertEqual(1, len(checkouts))
            self.assertIs(False, checkouts[0]["with"]["persist-credentials"])
            self.assertIs(True, checkouts[0]["with"]["submodules"])

    def test_no_secrets_anywhere(self):
        for name, text, doc in self.each():
            self.assertNotRegex(text, r"\bsecrets\b")
            self.assertNotIn("environment", {k for job in doc["jobs"].values() for k in job})

    def test_token_is_read_only(self):
        for name, text, doc in self.each():
            self.assertEqual({"contents": "read"}, doc["permissions"])
            for job in doc["jobs"].values():
                self.assertNotIn("permissions", job)

    def test_no_shell_tracing(self):
        for name, text, doc in self.each():
            for _, step in steps(doc):
                self.assertNotRegex(step.get("run", ""), TRACING)

    def test_expressions_never_reach_a_shell(self):
        for name, text, doc in self.each():
            for _, step in steps(doc):
                self.assertNotIn("${{", step.get("run", ""), step.get("name"))

    def test_downloads_only_through_checked_helpers(self):
        # godot_tools.py checks every download against a sha512 kept in the repo; pip only
        # installs from a hash lock.
        for name, text, doc in self.each():
            for _, step in steps(doc):
                self.assertNotRegex(step.get("run", ""), DOWNLOADERS, step.get("name"))
                for line in step.get("run", "").splitlines():
                    if "pip install" in line:
                        self.assertIn("--require-hashes -r test_ci/requirements.txt", line)

    def test_pip_lock_has_hashes(self):
        lock = (ROOT / "test_ci" / "requirements.txt").read_text(encoding="utf-8")
        self.assertRegex(lock, r"^pyyaml==\d+\.\d+\.\d+ \\")
        self.assertGreaterEqual(len(re.findall(r"--hash=sha256:[0-9a-f]{64}", lock)), 1)


class WindowsCi(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text, cls.doc = load("windows-ci.yml")
        cls.steps = [s for _, s in steps(cls.doc)]

    def test_runs_on_pushes_to_dev_and_main_and_on_pull_requests(self):
        on = self.doc[True]  # PyYAML reads the bare key `on` as True.
        self.assertEqual(["dev", "main"], on["push"]["branches"])
        self.assertNotIn("paths", on["push"])
        self.assertIn("pull_request", on)

    def test_runs_the_full_gate_on_windows(self):
        # The full gate includes the Windows export spec 16.8 asks CI for.
        self.assertEqual("windows-latest", self.doc["jobs"]["gate"]["runs-on"])
        runs = [s.get("run", "") for s in self.steps]
        self.assertIn("python scripts/gate.py", runs)
        self.assertFalse(any("--quick" in r for r in runs))
        self.assertTrue(any("scripts/godot_tools.py editor" in r for r in runs))

    def test_uploads_the_gate_logs_only_on_failure(self):
        uploads = [s for s in self.steps if "upload-artifact" in s.get("uses", "")]
        self.assertEqual(1, len(uploads))
        self.assertEqual("failure()", uploads[0]["if"])
        self.assertEqual("build/gate/", uploads[0]["with"]["path"])


class IosCompile(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text, cls.doc = load("ios-compile.yml")
        cls.steps = [s for _, s in steps(cls.doc)]
        cls.script = "\n".join(s.get("run", "") for s in cls.steps)

    def test_triggers(self):
        on = self.doc[True]
        self.assertEqual(["dev", "main"], on["push"]["branches"])
        self.assertIn("workflow_dispatch", on)
        paths = on["push"]["paths"]
        for needed in ("core/**", "gdext/**", "game/**", "scripts/build_ios_ext.sh",
                       "scripts/godot_tools.py", ".github/workflows/ios-compile.yml"):
            self.assertIn(needed, paths)

    def test_paths_filter_covers_every_input_the_steps_read(self):
        paths = self.doc[True]["push"]["paths"]
        for script in re.findall(r"\b(?:scripts|tools)/[\w./-]+\.(?:py|sh)\b", self.script):
            self.assertTrue(any(Path(script).match(p) or p == script for p in paths), script)

    def test_runs_on_macos_26_with_the_newest_xcode(self):
        self.assertEqual("macos-26", self.doc["jobs"]["build"]["runs-on"])
        self.assertIn("ls -d /Applications/Xcode_*.app 2>/dev/null | sort -V | tail -1", self.script)

    def test_pipeline_order(self):
        order = ["bash scripts/build_macos_ext.sh",
                 "bash scripts/build_ios_ext.sh debug",
                 "scripts/godot_tools.py editor",
                 "scripts/godot_tools.py templates",
                 "scripts/sync_data.py",
                 "--headless --path game --import",
                 '--export-debug "iOS" ../build/ios/OneFourSeven.xcodeproj',
                 "scripts/godot_tools.py check-godot-log shots/godot/import.log shots/godot/export.log",
                 "scripts/godot_tools.py ios-plist build/ios",
                 "xcodebuild -list -json",
                 "xcrun simctl install",
                 "\"$UDID\" \"$APP_ID\" -DemoScreen table",
                 "get_app_container \"$UDID\" \"$APP_ID\" data",
                 "'\"table.loaded\"'",
                 "xcrun simctl io \"$UDID\" screenshot shots/",
                 "python tools/check_logs.py shots/logs"]
        positions = [self.script.find(part) for part in order]
        for part, at in zip(order, positions):
            self.assertNotEqual(-1, at, part)
        self.assertEqual(sorted(positions), positions)

    def test_needs_the_ios_26_sdk(self):
        toolchain = next(s["run"] for s in self.steps if s.get("name") == "Toolchain")
        self.assertIn("sdks=$(xcodebuild -showsdks)", toolchain)
        self.assertIn('*"-sdk iphoneos26."*) ;;', toolchain)
        self.assertIn("exit 1", toolchain.split('*"-sdk iphoneos26."*) ;;', 1)[1].split("esac")[0])

    def test_macos_extension_is_cached_on_what_it_is_built_from(self):
        def step(**match):
            return next(s for s in self.steps if all(str(s.get(k, "")).startswith(v) for k, v in match.items()))

        key_step = step(id="macos-ext-key")
        restore = step(uses="actions/cache/restore@")
        build = step(name="Build the macOS extension for the editor")
        save = step(uses="actions/cache/save@")
        ios = step(name="Build the iOS extension")
        positions = [self.steps.index(s) for s in (key_step, restore, build, save, ios)]
        self.assertEqual(list(range(positions[0], positions[0] + 5)), positions)

        self.assertIn('"godot_cpp=$(git rev-parse HEAD:thirdparty/godot-cpp)" >> "$GITHUB_OUTPUT"', key_step["run"])
        self.assertEqual("bash scripts/build_macos_ext.sh", build["run"].strip())
        miss = "steps.macos-ext.outputs.cache-hit != 'true'"
        self.assertEqual((miss, miss), (build["if"], save["if"]))
        self.assertEqual("macos-ext", restore["id"])
        self.assertEqual((MACOS_DYLIB, MACOS_DYLIB), (restore["with"]["path"], save["with"]["path"]))
        self.assertEqual("${{ steps.macos-ext.outputs.cache-primary-key }}", save["with"]["key"])
        key = restore["with"]["key"]
        self.assertIn("${{ steps.macos-ext-key.outputs.godot_cpp }}", key)
        patterns = re.findall(r"'([^']+)'", re.search(r"hashFiles\(([^)]*)\)", key).group(1))

        def hashed(path):
            return any(path == p or (p.endswith("/**") and path.startswith(p[:-2])) for p in patterns)

        # Everything compiled into the dylib, and how; core's tests, test tools and data are not.
        compiled = [p.relative_to(ROOT).as_posix() for folder in ("core", "gdext", "thirdparty/nlohmann")
                    for p in (ROOT / folder).rglob("*") if p.is_file()]
        compiled = [p for p in compiled if not p.startswith(("core/tests/", "core/tools/", "core/data/"))]
        self.assertTrue(compiled)
        for path in compiled + ["CMakeLists.txt", "scripts/build_macos_ext.sh"]:
            self.assertTrue(hashed(path), path)
        self.assertFalse(hashed("core/data/table_params.json"))

    def session_check(self):
        run = next(s["run"] for s in self.steps if s.get("name") == "Check the session log")
        self.assertIn("python -I - shots/logs <<'EOF'", run)
        return run.split("<<'EOF'\n", 1)[1].split("\nEOF", 1)[0]

    def test_session_log_check_fails_on_app_error(self):
        script = self.session_check()

        def line(name, **data):
            return json.dumps({"event": name, "data": data})

        gles3 = "Image format RGBAFloat not supported by hardware, converting to RGBAHalf."
        for lines, code in (([line("app.start"), line("perf.second")], 0),
                            ([line("app.start"), line("app.error", kind="script", message="boom")], 1),
                            ([line("app.start"), line("app.error", kind="warning", message=gles3)], 0),
                            ([line("app.start"), line("app.error", kind="error", message=gles3)], 1),
                            ([line("app.start"), line("app.error", kind="warning",
                                                      message="Image format RGBAFloat not supported")], 1)):
            with tempfile.TemporaryDirectory() as temp:
                (Path(temp) / "s.jsonl").write_text("\n".join(lines) + "\n", encoding="utf-8")
                result = subprocess.run([sys.executable, "-I", "-", temp], input=script,
                                        capture_output=True, text=True)
            self.assertEqual(code, result.returncode, result.stdout + result.stderr)
            if code:
                self.assertIn(f"s.jsonl:2: {json.loads(lines[1])['data']['kind']}: ", result.stdout)

    def test_builds_unsigned_for_a_simulator(self):
        build = next(s["run"] for s in self.steps if s.get("name") == "Build for the simulator")
        self.assertIn('-destination "generic/platform=iOS Simulator"', build)
        self.assertIn('ARCHS=x86_64', build)
        self.assertIn('CODE_SIGN_IDENTITY="-"', build)
        self.assertIn("DEVELOPMENT_TEAM= ", build)
        self.assertNotIn("archive", build)

    def test_bundle_id_matches_the_preset(self):
        presets = (ROOT / "game" / "export_presets.cfg").read_text(encoding="utf-8")
        self.assertIn(f'application/bundle_identifier="{self.doc["env"]["APP_ID"]}"', presets)

    def test_screenshots_and_logs_are_uploaded_even_on_failure(self):
        uploads = [s for s in self.steps if "upload-artifact" in s.get("uses", "")]
        self.assertEqual(1, len(uploads))
        self.assertEqual("always()", uploads[0]["if"])
        self.assertEqual("simulator-screenshots", uploads[0]["with"]["name"])
        self.assertEqual("shots/", uploads[0]["with"]["path"])
        crashes = next(s for s in self.steps if s.get("name") == "Collect crash reports")
        self.assertEqual("always()", crashes["if"])


def bash():
    """Git's bash on Windows (System32's bash.exe starts WSL), the one on PATH elsewhere."""
    if sys.platform == "win32":
        git_bash = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Git" / "bin" / "bash.exe"
        return str(git_bash) if git_bash.exists() else None
    return shutil.which("bash")


# Stands in for python and the Godot editor in an export step. "broken" fails the import and
# prints a script error, then lets the export "succeed" with an extension error.
FAKE_TOOLS = r"""
python() { "$OFS_TEST_PYTHON" "$@"; }
fake_godot() {
  echo "Godot Engine v4.7.1.stable.official.a13da4feb - https://godotengine.org"
  if [ "$OFS_FAKE_GODOT" = clean ]; then return 0; fi
  case " $* " in
    *" --import "*)
      echo 'SCRIPT ERROR: Parse Error: Identifier "OfsLog" not declared in the current scope.'
      echo '          at: GDScript::reload (res://scripts/autoload/params.gd:22)'
      return 3 ;;
    *)
      echo 'ERROR: No GDExtension library found for current OS and architecture (macos.arm64) in configuration file: res://bin/onefourseven.gdextension' >&2
      return 0 ;;
  esac
}
GODOT=fake_godot
"""


class ExportSteps(unittest.TestCase):
    """Both iOS workflows' export steps, run with a fake Godot: every command runs, the checker
    reads both logs, and the step fails only after reporting every problem."""

    def run_step(self, workflow, mode, premade_project):
        shell = bash()
        if shell is None:
            self.skipTest("no bash to run the step with")
        run = next(s["run"] for _, s in steps(load(workflow)[1]) if s.get("name") == "Export the Xcode project")
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            (work / "scripts").mkdir()
            (work / "game").mkdir()
            shutil.copy(ROOT / "scripts" / "godot_tools.py", work / "scripts")
            (work / "scripts" / "sync_data.py").write_text("", encoding="utf-8")
            if premade_project:
                (work / "build" / "ios").mkdir(parents=True)
                test_build_scripts.IosPlistFix().make_export(work / "build" / "ios")
            (work / "step.sh").write_text(FAKE_TOOLS + run, encoding="utf-8", newline="\n")
            env = {**os.environ, "OFS_TEST_PYTHON": sys.executable, "OFS_FAKE_GODOT": mode,
                   "BUILD_NUMBER": "7", "PROJECT": "build/ios/OneFourSeven.xcodeproj"}
            result = subprocess.run([shell, "-e", "step.sh"], cwd=work, env=env, capture_output=True,
                                    text=True, timeout=120)
        return result.returncode, result.stdout + result.stderr

    def test_a_broken_export_reports_every_problem(self):
        for workflow, logs in (("ios-compile.yml", "shots/godot"), ("testflight.yml", "build/godot-logs")):
            code, out = self.run_step(workflow, "broken", premade_project=False)
            self.assertEqual(1, code, out)
            self.assertIn(f'error: {logs}/import.log:2: SCRIPT ERROR: Parse Error: Identifier "OfsLog" not '
                          "declared in the current scope. (res://scripts/autoload/params.gd:22)", out)
            self.assertIn(f"error: {logs}/export.log:2: ERROR: No GDExtension library found", out)
            self.assertIn("error: 2 script or extension problems", out)
            self.assertRegex(out, r"error: expected one \.xcodeproj in build.ios")

    def test_a_clean_export_passes(self):
        for workflow, logs in (("ios-compile.yml", "shots/godot"), ("testflight.yml", "build/godot-logs")):
            code, out = self.run_step(workflow, "clean", premade_project=True)
            self.assertEqual(0, code, out)
            self.assertIn(f"{logs}/import.log, {logs}/export.log: no script or extension errors", out)
            self.assertIn("Info.plist and build settings ok", out)


if __name__ == "__main__":
    unittest.main()
