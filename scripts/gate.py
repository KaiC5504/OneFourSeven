#!/usr/bin/env python3
"""The local gate: everything that can be checked on this PC, in parallel lanes.

    python scripts/gate.py           # all lanes; a pass on a clean tree is recorded for testflight.py
    python scripts/gate.py --quick   # without the Windows export, for the edit loop; records nothing

One Debug configure and build of the core and the extension, then in parallel: the core tests
(ctest), the Godot tests (sync_data, --import, res://tests/run_tests.gd), a short headless run of
the game whose session log is checked by tools/check_logs.py, the PC tools' tests and the workflow
tests. The full gate adds a Windows export (build_windows.py --check). A pass on a clean tree is
recorded in `git rev-parse --git-path gate-passed` (the commit), so testflight.py can tell the
commit it ships passed without running anything again. Every lane's full output is in build/gate/.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import Future, ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

import godot_tools

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "windows"
LOGS = ROOT / "build" / "gate"
GAME = ROOT / "game"
# About four seconds at 60 fps: long enough for startup and a few perf.second events.
LOG_RUN_FRAMES = 240

if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8")

Result = tuple[bool, str]
Step = Callable[[], Result]


class Skip(Exception):
    """A lane that cannot run yet for a reason outside this repo's control (a file not written yet)."""


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


def step(command: list, cwd: Path = ROOT, timeout: float = 900) -> Step:
    return lambda: run(command, cwd, timeout)


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


def godot() -> str:
    return str(godot_tools.find_godot())


def user_data_dir() -> Path:
    """Where Godot puts user:// for this project on Windows (no custom user dir is set)."""
    text = (GAME / "project.godot").read_text(encoding="utf-8")
    if "use_custom_user_dir=true" in text:
        raise SystemExit("project.godot uses a custom user dir; teach gate.py where it is")
    name = re.search(r'^config/name="([^"]+)"', text, re.MULTILINE).group(1)
    if sys.platform == "win32":
        return Path(os.environ["APPDATA"]) / "Godot" / "app_userdata" / name
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support" / "Godot" / "app_userdata" / name
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share")) / "godot" / "app_userdata" / name


def godot_import() -> Result:
    return run([godot(), "--headless", "--path", GAME, "--import"], timeout=600)


def godot_tests() -> Result:
    return run([godot(), "--headless", "--path", GAME, "-s", "res://tests/run_tests.gd"], timeout=600)


def session_log_run() -> Result:
    checker = ROOT / "tools" / "check_logs.py"
    if not checker.exists():
        raise Skip("tools/check_logs.py does not exist yet")
    logs = user_data_dir() / "logs"
    before = {p: p.stat().st_mtime for p in logs.rglob("*.jsonl")} if logs.exists() else {}
    ok, output = run([godot(), "--headless", "--path", GAME, "--max-fps", "60",
                      "--quit-after", str(LOG_RUN_FRAMES)], timeout=120)
    if not ok:
        return False, output
    errors = [line for line in output.splitlines() if "SCRIPT ERROR" in line]
    if errors:
        return False, output + "\n\nthe game printed script errors:\n" + "\n".join(errors)
    fresh = [p for p in (logs.rglob("*.jsonl") if logs.exists() else [])
             if before.get(p) != p.stat().st_mtime]
    if not fresh:
        return False, output + f"\n\nthe game wrote no session log in {logs}"
    # The checker gets a folder of just this run's sessions, the same shape CI pulls off the simulator.
    picked = LOGS / "session-logs"
    shutil.rmtree(picked, ignore_errors=True)
    picked.mkdir(parents=True)
    for path in fresh:
        shutil.copy2(path, picked / path.name)
    ok, checked = run([sys.executable, checker, picked])
    return ok, output + "\n\n" + checked


def python_tests(folder: str) -> Step:
    def lane() -> Result:
        if not (ROOT / folder).is_dir():
            raise Skip(f"{folder}/ does not exist yet")
        return run([sys.executable, "-m", "unittest", "discover", "-s", folder, "-p", "test_*.py"])
    return lane


def lanes(quick: bool) -> list[Lane]:
    result = [
        Lane("build", configure_and_build(BUILD, "Debug")),
        Lane("workflows", [python_tests("test_ci")]),
        Lane("tools", [python_tests("tools")]),
        Lane("core", [step(["ctest", "--test-dir", BUILD, "-C", "Debug", "--output-on-failure"])],
             after=["build"]),
        Lane("godot", [step([sys.executable, ROOT / "scripts" / "sync_data.py"]), godot_import, godot_tests],
             after=["build"]),
        # After the Godot tests, so the only new session log is this run's.
        Lane("logs", [session_log_run], after=["godot"]),
    ]
    if not quick:
        # Last: it imports and exports the same project the Godot lanes read.
        result.append(Lane("windows-export", [step([sys.executable, ROOT / "scripts" / "build_windows.py",
                                                    "--check"], timeout=1800)], after=["logs"]))
    return result


def run_lane(lane: Lane, futures: dict[str, Future]) -> tuple[str, str, float, str]:
    for dependency in lane.after:
        _, status, _, _ = futures[dependency].result()
        # A dependency that skipped itself still ran nothing that could break this lane.
        if status not in ("pass", "skip"):
            return lane.name, "blocked", 0.0, f"skipped: {dependency} did not pass"
    start = time.monotonic()
    outputs = []
    status = "pass"
    for lane_step in lane.steps:
        try:
            ok, output = lane_step()
        except Skip as reason:
            status, output = "skip", f"skipped: {reason}"
            outputs.append(output)
            break
        except SystemExit as e:
            ok, output = False, str(e)
        outputs.append(output)
        if not ok:
            status = "FAIL"
            break
    full = "\n\n".join(outputs)
    (LOGS / f"{lane.name}.log").write_text(full + "\n", encoding="utf-8")
    tail = "\n".join(full.splitlines()[-40:]) if status == "FAIL" else (outputs[-1] if status == "skip" else "")
    return lane.name, status, time.monotonic() - start, tail


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True).stdout.strip()


def main() -> int:
    quick = "--quick" in sys.argv[1:]
    stamp = ROOT / git("rev-parse", "--git-path", "gate-passed")
    head = None if quick or git("status", "--porcelain") else git("rev-parse", "HEAD")
    start = time.monotonic()
    LOGS.mkdir(parents=True, exist_ok=True)
    plan = lanes(quick)
    futures: dict[str, Future] = {}
    with ThreadPoolExecutor(max_workers=len(plan)) as pool:
        for lane in plan:
            futures[lane.name] = pool.submit(run_lane, lane, futures)
        results = [futures[lane.name].result() for lane in plan]
    for name, status, seconds, tail in results:
        print(f"{'skip' if status == 'blocked' else status:<4}  {name}  {seconds:.0f}s")
        if tail:
            print("      " + tail.replace("\n", "\n      "))
    # A skip is a lane waiting on another agent's file, not a pass: it never records the commit.
    passed = all(status == "pass" for _, status, _, _ in results)
    failed = any(status == "FAIL" for _, status, _, _ in results)
    print(f"gate {'failed' if failed else 'passed' if passed else 'incomplete'} "
          f"in {time.monotonic() - start:.0f}s (logs in {LOGS.relative_to(ROOT)})")
    if not quick:
        if passed and head:
            stamp.write_text(head + "\n", encoding="utf-8")
        else:
            stamp.unlink(missing_ok=True)
            if passed:
                print("(uncommitted changes: commit, then run the gate again to record the pass)")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
