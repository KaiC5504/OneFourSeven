"""Static checks on .github/workflows/testflight.yml. The repo is public, so the
workflow's logs and artifacts are too; these keep the signing secrets out of both and
keep a dry run from ever uploading.

The structural tests need PyYAML; without it only the text checks run."""

import io
import re
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

try:
    import yaml
except ImportError:
    yaml = None

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import testflight  # noqa: E402

WORKFLOW = ROOT / ".github/workflows/testflight.yml"
REQUIREMENTS = ROOT / "scripts/testflight-requirements.txt"
SCRIPT = ROOT / "scripts/testflight.py"
PINNED = re.compile(r"^[\w.-]+/[\w.-]+@[0-9a-f]{40}$")
LOCKED_INSTALL = "pip install --quiet --require-hashes -r scripts/testflight-requirements.txt"
# Also catches secrets['X'], toJSON(secrets) and `secrets: inherit`.
SECRET_REF = re.compile(r"\bsecrets\b")
UNCONDITIONAL = re.compile(r"always\(\)|failure\(\)|cancelled\(\)")
TRACING = r"set -\w*x|-o xtrace|\bbash -\w*x"
PLACEHOLDER = "__ASC_APP_ID__"


def live(run: str) -> str:
    """The script without comment lines, with continuation lines joined."""
    code = "\n".join(l for l in run.splitlines() if not l.lstrip().startswith("#"))
    return re.sub(r"\\\n\s*", " ", code)


class TextTest(unittest.TestCase):
    """Holds without PyYAML, so a bare CI Python still checks the essentials."""

    @classmethod
    def setUpClass(cls):
        cls.text = WORKFLOW.read_text(encoding="utf-8")

    def test_only_started_by_hand(self):
        triggers = re.search(r"(?ms)^on:\n(.*?)^\S", self.text).group(1)
        self.assertEqual(["workflow_dispatch"], re.findall(r"(?m)^  (\w+):", triggers))

    def test_actions_are_pinned_with_their_version(self):
        uses = re.findall(r"(?m)^\s*-?\s*uses:\s*(\S+)(.*)$", self.text)
        self.assertTrue(uses)
        for ref, rest in uses:
            self.assertRegex(ref, PINNED, ref)
            self.assertRegex(rest, r"^\s+# v\d+\.\d+\.\d+$", ref)

    def test_no_shared_caches(self):
        self.assertRegex(self.text, r"(?m)^cache-mode: none$")
        self.assertNotIn("actions/cache", self.text)

    def test_nothing_is_uploaded_as_an_artifact(self):
        self.assertNotIn("upload-artifact", self.text)
        self.assertNotRegex(self.text, r"\.xcarchive|\.ipa\b[^\n]*artifact")

    def test_masks_every_derived_secret(self):
        self.assertIn('echo "::add-mask::$KEYCHAIN_PASSWORD"', self.text)
        self.assertIn('echo "::add-mask::$p12_password"', self.text)
        self.assertIn("grep -v -e '-----' \"$key\" | while IFS= read -r line; do "
                      'echo "::add-mask::$line"; done', self.text)

    def test_placeholder_app_id_stops_the_build(self):
        self.assertIn(f'if [ "$ASC_APP_ID" = "{PLACEHOLDER}" ]; then', self.text)
        value = re.search(r'(?m)^  ASC_APP_ID: "([^"]*)"$', self.text).group(1)
        self.assertTrue(value == PLACEHOLDER or value.isdigit(), value)

    def test_no_shell_tracing(self):
        self.assertNotRegex(live(self.text), TRACING)
        self.assertNotRegex(self.text, r"--verbose\b")

    def test_secrets_appear_only_as_step_env_values(self):
        for line in self.text.splitlines():
            if SECRET_REF.search(line) and not line.lstrip().startswith("#"):
                self.assertRegex(line, r"^ {10}[A-Z0-9_]+: \$\{\{ secrets\.[A-Z0-9_]+ \}\}$", line)


@unittest.skipIf(yaml is None, "PyYAML not installed")
class WorkflowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = WORKFLOW.read_text(encoding="utf-8")
        cls.doc = yaml.safe_load(cls.text)
        cls.jobs = cls.doc["jobs"]

    def steps(self, job=None):
        for job_name, spec in self.jobs.items():
            if job in (None, job_name):
                for step in spec.get("steps", []):
                    yield job_name, step

    def step(self, job, name):
        return next(s for _, s in self.steps(job) if s.get("name") == name)

    def names(self, job="build"):
        return [s.get("name", s.get("uses")) for _, s in self.steps(job)]

    def test_only_started_by_hand(self):
        # PyYAML reads the bare key `on` as True.
        self.assertEqual({"workflow_dispatch"}, set(self.doc[True]))

    def test_uploading_must_be_asked_for(self):
        inputs = self.doc[True]["workflow_dispatch"]["inputs"]
        for name in ("upload", "create_signing"):
            self.assertEqual((True, False), (inputs[name]["required"], inputs[name]["default"]))
        self.assertEqual("inputs.upload", self.step("build", "Upload to TestFlight")["if"])
        self.assertEqual("inputs.upload", self.jobs["processed"]["if"])

    def test_token_is_read_only(self):
        self.assertEqual({"contents": "read"}, self.doc["permissions"])
        for name, job in self.jobs.items():
            self.assertNotIn("permissions", job, name)

    def test_actions_are_pinned_to_commits(self):
        for job, step in self.steps():
            if "uses" in step:
                self.assertRegex(step["uses"], PINNED, f"{job}: {step['uses']}")

    def test_no_shared_caches(self):
        self.assertEqual("none", self.doc.get("cache-mode"))
        for job, step in self.steps():
            self.assertNotIn("cache", step.get("with", {}), f"{job}: {step.get('uses')}")

    def test_no_artifacts(self):
        for job, step in self.steps():
            self.assertNotIn("upload-artifact", step.get("uses", ""), job)

    def test_cli_tools_come_from_the_hash_lock(self):
        self.assertIn(LOCKED_INSTALL, self.step("build", "Toolchain")["run"])
        self.assertIn(LOCKED_INSTALL, self.step("processed", "Install cli-tools")["run"])
        self.assertNotRegex(self.text, r"pip3? install [^\n]*codemagic-cli-tools")

    def test_needs_the_ios_26_sdk(self):
        toolchain = self.step("build", "Toolchain")["run"]
        self.assertIn("sdks=$(xcodebuild -showsdks)", toolchain)
        self.assertIn('*"-sdk iphoneos26."*) ;;', toolchain)
        self.assertIn("exit 1", toolchain.split('*"-sdk iphoneos26."*) ;;', 1)[1].split("esac")[0])

    def test_cli_tools_never_caches_its_token_on_disk(self):
        self.assertEqual("1", str(self.doc["env"]["APP_STORE_CONNECT_DISABLE_JWT_CACHE"]))

    def test_secrets_only_reach_step_env_in_the_main_only_environment(self):
        self.assertNotRegex(yaml.safe_dump(self.doc.get("env", {})), SECRET_REF)
        for name, job in self.jobs.items():
            outside_steps = {k: v for k, v in job.items() if k != "steps"}
            self.assertNotRegex(yaml.safe_dump(outside_steps), SECRET_REF, name)
            if SECRET_REF.search(yaml.safe_dump(job)):
                self.assertEqual("testflight", job.get("environment"), name)
            for step in job.get("steps", []):
                label = f"{name}: {step.get('name', step.get('uses'))}"
                outside_env = {k: v for k, v in step.items() if k != "env"}
                self.assertNotRegex(yaml.safe_dump(outside_env), SECRET_REF, label)
                for value in step.get("env", {}).values():
                    if SECRET_REF.search(str(value)):
                        self.assertRegex(str(value), r"^\$\{\{ secrets\.[A-Z0-9_]+ \}\}$", label)

    def test_secrets_never_reach_github_env_or_outputs(self):
        secret_vars = {k for _, s in self.steps() for k, v in s.get("env", {}).items()
                       if SECRET_REF.search(str(v))}
        for job, step in self.steps():
            for line in step.get("run", "").splitlines():
                if re.search(r"GITHUB_(ENV|OUTPUT|PATH|STEP_SUMMARY)", line):
                    for var in secret_vars:
                        self.assertNotRegex(line, rf"\b{var}\b", f"{job}: {step.get('name')}")

    def test_builds_only_main(self):
        self.assertEqual("github.ref == 'refs/heads/main'", self.jobs["build"]["if"])
        self.assertEqual("testflight", self.jobs["build"]["environment"])
        self.assertEqual("build", self.jobs["processed"]["needs"])

    def test_first_step_pins_the_checked_commit(self):
        first = self.jobs["build"]["steps"][0]
        self.assertEqual("Build only the checked commit", first["name"])
        self.assertEqual("${{ inputs.sha }}", first["env"]["WANT"])
        self.assertIn('"$WANT" != "$GITHUB_SHA"', first["run"])

    def test_first_step_refuses_the_placeholder_app_id(self):
        run = self.jobs["build"]["steps"][0]["run"]
        guard = run[run.index(f'"$ASC_APP_ID" = "{PLACEHOLDER}"'):]
        self.assertIn("exit 1", guard.split("fi", 1)[0])
        value = str(self.doc["env"]["ASC_APP_ID"])
        self.assertTrue(value == PLACEHOLDER or value.isdigit(), value)

    def test_inputs_never_reach_a_shell_unquoted(self):
        for job, step in self.steps():
            self.assertNotIn("${{", step.get("run", ""), f"{job}: {step.get('name')}")

    def test_checkout_never_keeps_the_token(self):
        checkouts = [s for _, s in self.steps() if "actions/checkout" in s.get("uses", "")]
        self.assertEqual(2, len(checkouts))
        for step in checkouts:
            self.assertIs(False, step.get("with", {}).get("persist-credentials"))
        build = next(s for _, s in self.steps("build") if "actions/checkout" in s.get("uses", ""))
        # godot-cpp is a submodule.
        self.assertIs(True, build["with"]["submodules"])

    def test_no_shell_tracing(self):
        for job, step in self.steps():
            label = f"{job}: {step.get('name')}"
            run = live(step.get("run", ""))
            self.assertNotRegex(run, TRACING, label)
            self.assertNotRegex(str(step.get("shell", "")), TRACING + r"|-\w*x\b", label)
            self.assertNotRegex(run, r"(?m)(^|[;&|(]\s*)(env|printenv|set|export -p|declare -x)\s*($|[|>;&)])",
                                label)
        for scope in (self.doc, *self.jobs.values()):
            shell = str(scope.get("defaults", {}).get("run", {}).get("shell", ""))
            self.assertNotRegex(shell, TRACING + r"|-\w*x\b")

    def test_nothing_can_wave_a_failed_check_through(self):
        for name, job in self.jobs.items():
            self.assertNotIn("continue-on-error", job, name)
            self.assertNotRegex(str(job.get("if", "")), UNCONDITIONAL, name)
            for step in job.get("steps", []):
                self.assertNotIn("continue-on-error", step, f"{name}: {step.get('name')}")
                self.assertNotRegex(str(step.get("if", "")), UNCONDITIONAL, f"{name}: {step.get('name')}")

    def test_signing_creates_only_when_asked(self):
        signing = self.step("build", "Create signing files")
        self.assertEqual("${{ inputs.create_signing }}", signing["env"]["CREATE"])
        run = live(signing["run"])
        self.assertIn('if [ "$CREATE" = true ]; then create="--create"; fi', run)
        self.assertEqual(1, run.count("--create"))

    def test_signing_leaves_nothing_readable_behind(self):
        sign = live(self.step("build", "Create signing files")["run"])
        self.assertIn("umask 077", sign)
        self.assertIn("trap 'rm -f \"$key\"' EXIT", sign)
        self.assertRegex(sign, r"keychain initialize --password @env:\w+")
        self.assertNotIn('-k ""', sign)
        for folder in ("Library/Developer/Xcode/UserData/Certificates", "Library/MobileDevice/Certificates"):
            self.assertIn(f'"$HOME/{folder}/"*.p12', sign)
        self.assertIn('echo "::add-mask::$line"', sign)
        self.assertIn('echo "::add-mask::$KEYCHAIN_PASSWORD"', sign)
        self.assertIn('echo "::add-mask::$p12_password"', sign)
        self.assertIn("SIGNING_NAME", self.step("build", "Create signing files")["env"])

    def test_godot_project_identity(self):
        env = self.doc["env"]
        self.assertEqual("com.kaichuan.onefourseven", env["BUNDLE_ID"])
        self.assertEqual("build/ios/OneFourSeven.xcodeproj", env["PROJECT"])
        self.assertEqual("OneFourSeven", env["SCHEME"])
        check = self.step("build", "Check the scheme")["run"]
        self.assertIn('xcodebuild -list -json -project "$PROJECT"', check)
        self.assertIn('os.environ["SCHEME"]', check)

    def test_export_order(self):
        names = self.names()
        order = ["Build only the checked commit", "Toolchain", "Build the iOS extension",
                 "Build the macOS extension for the editor",
                 "Godot and its export templates", "Pick the build number",
                 "Export the Xcode project", "Make the project ours", "Check the scheme",
                 "Create signing files", "Apply signing settings", "Build IPA",
                 "Upload to TestFlight"]
        self.assertEqual(order, [n for n in names if n in order])

    def test_export_commands(self):
        self.assertEqual("bash scripts/build_ios_ext.sh release",
                         self.step("build", "Build the iOS extension")["run"].strip())
        # Built here every time: this job restores no cache (test_no_shared_caches).
        macos = self.step("build", "Build the macOS extension for the editor")
        self.assertEqual("bash scripts/build_macos_ext.sh", macos["run"].strip())
        self.assertNotIn("if", macos)
        godot = self.step("build", "Godot and its export templates")["run"]
        self.assertIn("python scripts/godot_tools.py editor", godot)
        self.assertIn("python scripts/godot_tools.py templates", godot)
        self.assertIn('echo "GODOT=$godot" >> "$GITHUB_ENV"', godot)
        # test_workflows.ExportSteps runs this step with a fake Godot.
        export = live(self.step("build", "Export the Xcode project")["run"]).splitlines()
        expected = [
            "set -o pipefail",
            "OFS_BUILD_NUMBER=$BUILD_NUMBER python scripts/sync_data.py",
            "mkdir -p build/ios build/godot-logs",
            "status=0",
            '"$GODOT" --headless --path game --import 2>&1 | tee build/godot-logs/import.log || status=1',
            '"$GODOT" --headless --path game --export-release "iOS" ../build/ios/OneFourSeven.xcodeproj 2>&1 '
            "| tee build/godot-logs/export.log || status=1",
            "python scripts/godot_tools.py check-godot-log build/godot-logs/import.log "
            "build/godot-logs/export.log || status=1",
            'test -f "$PROJECT/project.pbxproj" || '
            '{ echo "error: the export wrote no $PROJECT/project.pbxproj"; status=1; }',
            "python scripts/godot_tools.py ios-plist build/ios || status=1",
            "exit $status",
        ]
        self.assertEqual(expected, [re.sub(r"\s+", " ", l.strip()) for l in export if l.strip()])

    def test_project_is_patched_and_checked(self):
        ours = live(self.step("build", "Make the project ours")["run"])
        self.assertIn("set -e", ours)
        self.assertIn('grep -c "PRODUCT_BUNDLE_IDENTIFIER = $BUNDLE_ID;" "$pbx" | grep -qx 2', ours)
        self.assertIn("baseConfigurationReference", ours)
        self.assertIn("PlistBuddy -c 'Set :CFBundleVersion $(CURRENT_PROJECT_VERSION)'", ours)
        self.assertIn(":ITSAppUsesNonExemptEncryption", ours)
        self.assertIn("UISupportedInterfaceOrientations~ipad", ours)
        for key in ("aps-environment", "com.apple.developer.game-center",
                    "com.apple.security.application-groups", "com.apple.developer.icloud-services"):
            self.assertIn(key, ours)
        self.assertIn("unexpected entitlements", ours)

    def test_build_numbers_keep_rising(self):
        self.assertGreaterEqual(int(self.doc["env"]["BUILD_NUMBER_FLOOR"]), 0)
        step = self.step("build", "Pick the build number")
        self.assertEqual("number", step["id"])
        self.assertIn('get-latest-testflight-build-number "$ASC_APP_ID" --all-versions', step["run"])
        self.assertIn("''|*[!0-9]*)", step["run"])
        self.assertIn('echo "BUILD_NUMBER=$next" >> "$GITHUB_ENV"', step["run"])
        self.assertIn("CURRENT_PROJECT_VERSION=$BUILD_NUMBER", self.step("build", "Build IPA")["run"])

    def test_processing_waits_for_the_built_number(self):
        wait = self.step("processed", "Wait for Apple to finish processing")
        self.assertEqual("${{ needs.build.outputs.build_number }}", wait["env"]["BUILT"])
        self.assertIn('state="no answer', wait["run"])
        self.assertIn('if [ "$misses" -ge 8 ]', wait["run"])
        # macOS runners have no GNU timeout.
        self.assertNotRegex(wait["run"], r"(^|[\s(])timeout ")


@unittest.skipIf(yaml is None, "PyYAML not installed")
class OtherWorkflowsTest(unittest.TestCase):
    def test_only_this_workflow_touches_signing(self):
        # Environment secrets reach any job on main that names the environment,
        # whatever its trigger.
        for path in sorted(WORKFLOW.parent.glob("*.y*ml")):
            if path.name == WORKFLOW.name:
                continue
            text = path.read_text(encoding="utf-8")
            doc = yaml.safe_load(text) or {}
            for name, job in (doc.get("jobs") or {}).items():
                self.assertNotIn("environment", job, f"{path.name}: {name}")
            for needle in ("APP_STORE_CONNECT", "CERT_KEY_B64", "app-store-connect", "altool"):
                self.assertNotIn(needle, text, path.name)


class SupportFilesTest(unittest.TestCase):
    def test_requirements_are_hash_locked(self):
        text = REQUIREMENTS.read_text(encoding="utf-8")
        self.assertRegex(text, r"(?m)^codemagic-cli-tools==\d")
        for block in re.split(r"\n(?=\S)", text):
            if re.match(r"[A-Za-z0-9_.-]+==", block):
                self.assertIn("--hash=sha256:", block, block.splitlines()[0])

    def test_script_targets_this_repo(self):
        text = SCRIPT.read_text(encoding="utf-8")
        self.assertIn('REPO = "KaiC5504/OneFourSeven"', text)
        self.assertIn('WORKFLOW = "testflight.yml"', text)

    def test_script_checks_every_secret_the_workflow_uses(self):
        # An unset secret reaches the step as an empty string, and SIGNING_NAME then shows unmasked.
        used = set(re.findall(r"\bsecrets\.([A-Za-z0-9_]+)", WORKFLOW.read_text(encoding="utf-8")))
        self.assertEqual(used, testflight.SECRETS)


class StartTest(unittest.TestCase):
    """testflight.py start ships only a commit gate.py passed on this PC."""

    SHA = "0123456789abcdef0123456789abcdef01234567"

    def start(self, argv, recorded):
        with tempfile.TemporaryDirectory() as temp:
            record = Path(temp) / "gate-passed"
            if recorded is not None:
                record.write_text(recorded + "\n", encoding="utf-8")

            def git(*args):
                if args == ("rev-parse", "--git-path", "gate-passed"):
                    return str(record)
                if args == ("rev-parse", "origin/main"):
                    return self.SHA
                return ""

            with mock.patch.object(testflight, "git", side_effect=git), \
                    mock.patch.object(testflight, "environment_problems", return_value=[]), \
                    mock.patch.object(testflight, "last_shipped", return_value=None), \
                    mock.patch.object(testflight, "runs", return_value=[]), \
                    mock.patch.object(testflight, "dispatched_run", return_value=None), \
                    mock.patch.object(testflight, "run", return_value="") as run, \
                    mock.patch("sys.stdout", io.StringIO()) as out:
                code = testflight.cmd_start(argv)
        dispatched = any("workflow" in call.args for call in run.call_args_list)
        return code, dispatched, out.getvalue()

    def test_refuses_without_a_gate_pass(self):
        code, dispatched, out = self.start([], None)
        self.assertEqual((1, False), (code, dispatched))
        self.assertIn("no gate pass is recorded here: run python scripts/gate.py on main first", out)

    def test_refuses_when_the_pass_was_for_another_commit(self):
        code, dispatched, out = self.start([], "f" * 40)
        self.assertEqual((1, False), (code, dispatched))
        self.assertIn("the last gate pass here was ffffffff", out)
        self.assertIn("run python scripts/gate.py on main first", out)

    def test_starts_when_origin_main_passed(self):
        self.assertEqual((0, True), self.start([], self.SHA)[:2])

    def test_a_dry_run_needs_no_gate_pass(self):
        self.assertEqual((0, True), self.start(["--dry-run"], None)[:2])


if __name__ == "__main__":
    unittest.main()
