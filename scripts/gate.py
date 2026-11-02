#!/usr/bin/env python3
"""The local gate: everything that can be checked on this PC.

    python scripts/gate.py           # all lanes; a pass on a clean tree is recorded for testflight.py
    python scripts/gate.py --quick   # without the Windows export, for the edit loop; records no pass

One Debug configure and build of the core, the extension and ofs_params_hash, then the core tests
(ctest), a Release build of the core alone for the physics lane (the calibration suite, a fuzz of
random shots: 2,000 with --quick, otherwise the full 100,000 of spec C14 on each of two seeds, and
the simulation benchmarks against the 2 ms budget and the baseline, only reported on CI), the
Godot tests (sync_data, --import, res://tests/run_tests.gd), a short headless run of the game
whose session log must pass tools/check_logs.py with no app.error, the PC tools' tests
and the workflow tests. The full gate adds a Windows export (build_windows.py --check). Lanes run
in parallel unless one depends on another.

A full pass on a clean tree records the commit in `git rev-parse --git-path gate-passed`, so
testflight.py can tell the commit it ships passed without running anything again. A failed or
crashed gate, or one where no lane passed, clears that record. Every lane's full output is in
build/gate/.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import time
import traceback
from concurrent.futures import Future, ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

import godot_tools

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "windows"
# Release, core only: the fuzz and benchmarks measure optimised code, without godot-cpp's build.
PHYSICS_BUILD = ROOT / "build" / "windows-physics"
FUZZ_SHOTS_QUICK = 2000
FUZZ_SHOTS_FULL = 100_000
# The full gate fuzzes a second, unrelated shot set as well, so a fix tuned to seed 1 can't pass alone.
FUZZ_SEEDS_FULL = (1, 4242)
LOGS = ROOT / "build" / "gate"
GAME = ROOT / "game"
# The logs lane's own user:// (Godot has no flag for it), so no other run's session can mix in.
LOG_RUN_USER = ROOT / "build" / "gate-user"
# About four seconds at 60 fps: long enough for startup and a few perf.second events.
LOG_RUN_FRAMES = 240
TESTS_LOADED = re.compile(r"\bloaded (\d+)/(\d+) test files\b")
# app.error lines a clean headless run is known to produce, as (kind, regex on message), each with
# the reason it is harmless. Any other app.error in the session log fails the logs lane.
ALLOWED_APP_ERRORS: list[tuple[str, str]] = []

if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8")

Result = tuple[bool, str]
Step = Callable[[], Result]


@dataclass
class Lane:
    name: str
    steps: list[Step]
    after: list[str] = field(default_factory=list)


def run(command: list[str], cwd: Path = ROOT, timeout: float = 900, env: dict | None = None) -> Result:
    shown = " ".join(str(c) for c in command)
    try:
        result = subprocess.run([str(c) for c in command], cwd=cwd, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=timeout,
                                env={**os.environ, **(env or {})})
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or "") + (e.stderr or "") if isinstance(e.stdout, str) else ""
        return False, f"$ {shown}\n{out}\ntimed out after {timeout:.0f}s"
    except FileNotFoundError:
        return False, f"$ {shown}\nnot found: {command[0]}"
    output = f"$ {shown}\n{result.stdout}{result.stderr}".rstrip()
    if result.returncode != 0:
        output += f"\nexit code {result.returncode}"
    return result.returncode == 0, output


def step(command: list, cwd: Path = ROOT, timeout: float = 900, env: dict | None = None) -> Step:
    return lambda: run(command, cwd, timeout, env)


def cmake_generator() -> list[str]:
    if sys.platform != "win32":
        return []
    configured = os.environ.get("OFS_CMAKE_GENERATOR")
    if configured:
        return ["-G", configured, "-A", "x64"]
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
        "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        raise SystemExit("no vswhere.exe: install Visual Studio or set OFS_CMAKE_GENERATOR")
    version = subprocess.run([str(vswhere), "-latest", "-products", "*", "-requires",
                              "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                              "-property", "installationVersion"],
                             capture_output=True, text=True).stdout.strip()
    # CMake names generators by year, vswhere reports the major version.
    years = {"17": "2022", "18": "2026"}
    major = version.split(".")[0]
    if major not in years:
        raise SystemExit(f"Visual Studio {version or '(none with C++ tools)'}: set OFS_CMAKE_GENERATOR")
    return ["-G", f"Visual Studio {major} {years[major]}", "-A", "x64"]


def configure_and_build(build_dir: Path, config: str, extra: list[str] = ()) -> list[Step]:
    def configure() -> Result:
        # A configured tree keeps its generator; passing -G again only matters the first time.
        generator = [] if (build_dir / "CMakeCache.txt").exists() else cmake_generator()
        return run(["cmake", "-S", ROOT, "-B", build_dir, *generator, *extra])
    return [configure, step(["cmake", "--build", build_dir, "--config", config, "--parallel"], timeout=1800)]


def params_hash_tool() -> Path:
    """ofs_params_hash from the Debug build; tools/test_devlink.py checks devlink.py's hashes with it."""
    name = "ofs_params_hash.exe" if sys.platform == "win32" else "ofs_params_hash"
    # Multi-config generators (Visual Studio) add the config folder; single-config ones don't.
    for path in (BUILD / "core" / "Debug" / name, BUILD / "core" / name):
        if path.exists():
            return path
    raise SystemExit(f"the build made no {name} in {BUILD / 'core'}")


def params_hash_built() -> Result:
    return True, f"built {params_hash_tool().relative_to(ROOT)}"


def sim_cli() -> Path:
    name = "ofs_sim_cli.exe" if sys.platform == "win32" else "ofs_sim_cli"
    for path in (PHYSICS_BUILD / "tools" / "sim_cli" / "Release" / name, PHYSICS_BUILD / "tools" / "sim_cli" / name):
        if path.exists():
            return path
    raise SystemExit(f"the physics build made no {name} in {PHYSICS_BUILD / 'tools' / 'sim_cli'}")


def on_ci() -> bool:
    return os.environ.get("CI") == "true" or os.environ.get("GITHUB_ACTIONS") == "true"


def fuzz_step(shots: int, seed: int) -> Step:
    # Each seed's failure bundles get their own folder, so the second run doesn't clear the first's.
    out = ROOT / "build" / "fuzz-failures" / f"seed-{seed}"
    return lambda: run([sim_cli(), "fuzz", "--shots", str(shots), "--seed", str(seed), "--out", out], timeout=1800)


def physics_steps(quick: bool) -> list[Step]:
    fuzz = [fuzz_step(FUZZ_SHOTS_QUICK, 1)] if quick else [fuzz_step(FUZZ_SHOTS_FULL, seed) for seed in FUZZ_SEEDS_FULL]
    # A CI runner is a shared VM, neither the desktop core the 2 ms budget is for nor the PC the
    # baseline was timed on, so there the benchmarks are only reported.
    bench = ["bench"] if on_ci() else ["bench", "--check"]
    return configure_and_build(PHYSICS_BUILD, "Release", ["-DOFS_BUILD_GDEXT=OFF"]) + [
        step(["ctest", "--test-dir", PHYSICS_BUILD, "-C", "Release", "-L", "calibration", "--output-on-failure"]),
        # Both report every failing shot before exiting, each with a replayable bundle.
        *fuzz,
        lambda: run([sim_cli(), *bench]),
    ]


def godot() -> str:
    return str(godot_tools.find_godot())


def isolated_user_dir(base: Path) -> tuple[dict[str, str], Path]:
    """The environment that moves this project's user:// under base, and the folder it becomes."""
    text = (GAME / "project.godot").read_text(encoding="utf-8")
    if "use_custom_user_dir=true" in text:
        raise SystemExit("project.godot uses a custom user dir; teach gate.py where it is")
    name = re.search(r'^config/name="([^"]+)"', text, re.MULTILINE).group(1)
    if sys.platform == "win32":
        return {"APPDATA": str(base)}, base / "Godot" / "app_userdata" / name
    if sys.platform == "darwin":
        return {"HOME": str(base)}, base / "Library" / "Application Support" / "Godot" / "app_userdata" / name
    return {"XDG_DATA_HOME": str(base)}, base / "godot" / "app_userdata" / name


def godot_import() -> Result:
    # On a fresh .godot the editor loads the extension midway through its first scan and can
    # crash on exit after a finished import (seen on the windows-latest runner, never on this
    # PC). A second import then exits cleanly; a second crash is a real failure.
    ok, output = run([godot(), "--headless", "--path", GAME, "--import"], timeout=600)
    if ok:
        return ok, output
    ok, again = run([godot(), "--headless", "--path", GAME, "--import"], timeout=600)
    return ok, output + "\n\nfirst import failed; retried once:\n" + again


def test_run_problems(output: str, test_files: int) -> list[str]:
    """What the runner's output shows went wrong even when its exit code says pass."""
    problems = []
    loaded = TESTS_LOADED.findall(output)
    if len(loaded) != 1:
        problems.append(f"run_tests.gd printed {len(loaded)} 'loaded N/M test files' lines, want one")
    else:
        done, found = (int(n) for n in loaded[0])
        if not done == found == test_files:
            problems.append(f"run_tests.gd loaded {done}/{found} test files; "
                            f"game/tests has {test_files} test_*.gd")
    errors = [line for line in output.splitlines() if "SCRIPT ERROR" in line]
    if errors:
        problems.append("script errors:\n" + "\n".join(errors))
    return problems


def godot_tests() -> Result:
    env, _ = isolated_user_dir(ROOT / "build" / "gate-tests-user")
    ok, output = run([godot(), "--headless", "--path", GAME, "-s", "res://tests/run_tests.gd"], timeout=600,
                     env=env)
    problems = test_run_problems(output, len(list((GAME / "tests").glob("test_*.gd"))))
    if problems:
        return False, output + "\n\n" + "\n".join(problems)
    return ok, output


def unexpected_app_errors(paths: list[Path]) -> list[str]:
    found = []
    for path in paths:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        for number, line in enumerate(lines, 1):
            try:
                event = json.loads(line)
            except ValueError:
                # check_logs.py reports lines that are not JSON.
                continue
            if not isinstance(event, dict) or event.get("event") != "app.error":
                continue
            data = event["data"] if isinstance(event.get("data"), dict) else {}
            kind, message = str(data.get("kind", "")), str(data.get("message", ""))
            if not any(kind == allowed and re.search(pattern, message)
                       for allowed, pattern in ALLOWED_APP_ERRORS):
                found.append(f"{path.name}:{number}: {kind}: {message}")
    return found


def session_log_run() -> Result:
    if LOG_RUN_USER.exists():
        shutil.rmtree(LOG_RUN_USER)
    env, user_dir = isolated_user_dir(LOG_RUN_USER)
    logs = user_dir / "logs"
    ok, output = run([godot(), "--headless", "--path", GAME, "--max-fps", "60",
                      "--quit-after", str(LOG_RUN_FRAMES), "--", "--no-link"], timeout=120, env=env)
    if not ok:
        return False, output
    errors = [line for line in output.splitlines() if "SCRIPT ERROR" in line]
    if errors:
        return False, output + "\n\nthe game printed script errors:\n" + "\n".join(errors)
    sessions = sorted(logs.rglob("*.jsonl")) if logs.exists() else []
    if not sessions:
        return False, output + f"\n\nthe game wrote no session log in {logs}"
    # The checker gets a folder of just this run's sessions, the same shape CI pulls off the simulator.
    picked = LOGS / "session-logs"
    if picked.exists():
        shutil.rmtree(picked)
    picked.mkdir(parents=True)
    for path in sessions:
        shutil.copy2(path, picked / path.name)
    ok, checked = run([sys.executable, ROOT / "tools" / "check_logs.py", picked])
    output += "\n\n" + checked
    app_errors = unexpected_app_errors(sorted(picked.glob("*.jsonl")))
    if app_errors:
        return False, output + "\n\nthe session log has app.error lines:\n" + "\n".join(app_errors)
    return ok, output


def python_tests(folder: str, env: Callable[[], dict] | None = None) -> Step:
    def lane() -> Result:
        if not (ROOT / folder).is_dir():
            return False, f"{folder}/ is missing"
        return run([sys.executable, "-m", "unittest", "discover", "-s", folder, "-p", "test_*.py"],
                   env=env() if env else None)
    return lane


def lanes(quick: bool) -> list[Lane]:
    result = [
        Lane("build", configure_and_build(BUILD, "Debug") + [params_hash_built]),
        Lane("workflows", [python_tests("test_ci")]),
        Lane("tools", [python_tests("tools", lambda: {"OFS_PARAMS_HASH": str(params_hash_tool())})],
             after=["build"]),
        # The calibration suite runs in the physics lane, optimised.
        Lane("core", [step(["ctest", "--test-dir", BUILD, "-C", "Debug", "-LE", "calibration",
                            "--output-on-failure"])], after=["build"]),
        Lane("physics", physics_steps(quick)),
        Lane("godot", [step([sys.executable, ROOT / "scripts" / "sync_data.py"]), godot_import, godot_tests],
             after=["build"]),
        # Needs the Godot lane's import.
        Lane("logs", [session_log_run], after=["godot"]),
    ]
    if not quick:
        # Last: it imports and exports the same project the Godot lanes read.
        result.append(Lane("windows-export", [step([sys.executable, ROOT / "scripts" / "build_windows.py",
                                                    "--check"], timeout=1800)], after=["logs"]))
    return result


def run_lane(lane: Lane, futures: dict[str, Future]) -> tuple[str, str, float, str]:
    start = time.monotonic()
    outputs = []
    status = "pass"
    try:
        for dependency in lane.after:
            _, dependency_status, _, _ = futures[dependency].result()
            if dependency_status != "pass":
                return lane.name, "blocked", 0.0, f"not run: {dependency} did not pass"
        for lane_step in lane.steps:
            try:
                ok, output = lane_step()
            except SystemExit as e:
                ok, output = False, str(e)
            outputs.append(output)
            if not ok:
                status = "FAIL"
                break
    except Exception:
        outputs.append("the gate crashed in this lane:\n" + traceback.format_exc().rstrip())
        status = "FAIL"
    full = "\n\n".join(outputs)
    try:
        (LOGS / f"{lane.name}.log").write_text(full + "\n", encoding="utf-8")
    except OSError as e:
        full += f"\n\ncould not write {lane.name}.log: {e}"
        status = "FAIL"
    tail = "\n".join(full.splitlines()[-40:]) if status == "FAIL" else ""
    return lane.name, status, time.monotonic() - start, tail


def git(*args: str) -> str:
    result = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout.strip()


def main() -> int:
    quick = "--quick" in sys.argv[1:]
    start = time.monotonic()
    stamp: Path | None = None
    head = None
    results = []
    crash = None
    try:
        stamp = ROOT / git("rev-parse", "--git-path", "gate-passed")
        if not quick:
            # Gone before anything runs, so a gate that dies on the way leaves no old pass behind.
            stamp.unlink(missing_ok=True)
            head = None if git("status", "--porcelain") else git("rev-parse", "HEAD")
        LOGS.mkdir(parents=True, exist_ok=True)
        plan = lanes(quick)
        futures: dict[str, Future] = {}
        with ThreadPoolExecutor(max_workers=max(1, len(plan))) as pool:
            for lane in plan:
                futures[lane.name] = pool.submit(run_lane, lane, futures)
            results = [futures[lane.name].result() for lane in plan]
    except (Exception, SystemExit):
        crash = traceback.format_exc().rstrip()
    for name, status, seconds, tail in results:
        print(f"{'skip' if status == 'blocked' else status:<4}  {name}  {seconds:.0f}s")
        if tail:
            print("      " + tail.replace("\n", "\n      "))
    if crash:
        print("the gate crashed:\n      " + crash.replace("\n", "\n      "))
    passed = crash is None and bool(results) and all(status == "pass" for _, status, _, _ in results)
    print(f"gate {'passed' if passed else 'failed'} in {time.monotonic() - start:.0f}s "
          f"(logs in {LOGS.relative_to(ROOT) if LOGS.is_relative_to(ROOT) else LOGS})")
    if not passed:
        if stamp:
            stamp.unlink(missing_ok=True)
        return 1
    if not quick:
        if head:
            stamp.write_text(head + "\n", encoding="utf-8")
        else:
            print("(uncommitted changes: commit, then run the gate again to record the pass)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
