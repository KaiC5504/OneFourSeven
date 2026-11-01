"""Static checks on windows-ci.yml and ios-compile.yml. The repo is public, so their logs and
artifacts are too: no secrets, no tracing, no unpinned code, no unchecked downloads."""

import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = ROOT / ".github" / "workflows"
NAMES = ("windows-ci.yml", "ios-compile.yml")
PINNED = re.compile(r"^[\w.-]+/[\w.-]+@[0-9a-f]{40}$")
PINNED_LINE = re.compile(r"uses: ([\w.-]+/[\w.-]+@[0-9a-f]{40}) # v\d+(?:\.\d+)*$", re.MULTILINE)
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
        order = ["bash scripts/build_ios_ext.sh debug",
                 "scripts/godot_tools.py editor",
                 "scripts/godot_tools.py templates",
                 "scripts/sync_data.py",
                 "--headless --path game --import",
                 '--export-debug "iOS" ../build/ios/OneFourSeven.xcodeproj',
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

    def session_check(self):
        run = next(s["run"] for s in self.steps if s.get("name") == "Check the session log")
        self.assertIn("python -I - shots/logs <<'EOF'", run)
        return run.split("<<'EOF'\n", 1)[1].split("\nEOF", 1)[0]

    def test_session_log_check_fails_on_app_error(self):
        script = self.session_check()
        self.assertIn("allowed = []", script)

        def line(name, **data):
            return json.dumps({"event": name, "data": data})

        for lines, code in (([line("app.start"), line("perf.second")], 0),
                            ([line("app.start"), line("app.error", kind="script", message="boom")], 1)):
            with tempfile.TemporaryDirectory() as temp:
                (Path(temp) / "s.jsonl").write_text("\n".join(lines) + "\n", encoding="utf-8")
                result = subprocess.run([sys.executable, "-I", "-", temp], input=script,
                                        capture_output=True, text=True)
            self.assertEqual(code, result.returncode, result.stdout + result.stderr)
            if code:
                self.assertIn("s.jsonl:2: script: boom", result.stdout)

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


if __name__ == "__main__":
    unittest.main()
