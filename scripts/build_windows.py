#!/usr/bin/env python3
"""The playable Windows build: a release extension and a Godot export to dist/windows/.

    python scripts/build_windows.py              # dist/windows/OneFourSeven.exe
    python scripts/build_windows.py --shortcut   # also "One Four Seven.lnk" on the desktop
    python scripts/build_windows.py --check      # export to a temp folder and start it headless once
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import gate
import godot_tools

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / "game"
BUILD = ROOT / "build" / "windows-release"
DIST = ROOT / "dist" / "windows"
EXE = "OneFourSeven.exe"
DLL = "libonefourseven.windows.template_release.x86_64.dll"


def must(ok_output: tuple[bool, str], what: str) -> str:
    ok, output = ok_output
    if not ok:
        raise SystemExit(f"{what} failed:\n" + "\n".join(output.splitlines()[-40:]))
    return output


def export(destination: Path) -> Path:
    must(gate.run([sys.executable, ROOT / "scripts" / "sync_data.py"]), "sync_data")
    for step in gate.configure_and_build(BUILD, "Release", ["-DGODOTCPP_TARGET=template_release",
                                                            "-DOFS_BUILD_TESTS=OFF"]):
        must(step(), "release build")
    if not (GAME / "bin" / DLL).exists():
        raise SystemExit(f"the release build did not produce game/bin/{DLL}")
    godot_tools.ensure_templates()
    godot = str(godot_tools.find_godot())
    must(gate.run([godot, "--headless", "--path", GAME, "--import"], timeout=600), "Godot import")
    destination.mkdir(parents=True, exist_ok=True)
    exe = destination / EXE
    exe.unlink(missing_ok=True)
    must(gate.run([godot, "--headless", "--path", GAME, "--export-release", "Windows Desktop", exe],
                  timeout=900), "Godot export")
    if not exe.exists():
        raise SystemExit(f"Godot reported success but wrote no {exe}")
    # Godot copies a GDExtension's library next to the exe; without it the game starts with no table.
    if not (destination / DLL).exists():
        raise SystemExit(f"the export has no {DLL} next to {EXE}: check game/bin/onefourseven.gdextension")
    return exe


def smoke(exe: Path) -> None:
    must(gate.run([exe, "--headless", "--quit-after", "120"], cwd=exe.parent, timeout=120),
         f"starting {exe.name}")


def desktop_shortcut(exe: Path) -> Path:
    # Asked from Windows rather than built from %USERPROFILE%, which misses a OneDrive-redirected desktop.
    desktop = subprocess.run(["powershell", "-NoProfile", "-Command",
                              "[Environment]::GetFolderPath('Desktop')"],
                             capture_output=True, text=True, check=True).stdout.strip()
    return make_shortcut(exe, Path(desktop) / "One Four Seven.lnk")


def make_shortcut(exe: Path, link: Path) -> Path:
    script =("$s = (New-Object -ComObject WScript.Shell).CreateShortcut($env:OFS_LINK); "
              "$s.TargetPath = $env:OFS_EXE; $s.WorkingDirectory = $env:OFS_DIR; $s.Save()")
    # Paths go through the environment so quotes or spaces in them never reach the script text.
    subprocess.run(["powershell", "-NoProfile", "-Command", script], check=True,
                   env={**os.environ, "OFS_LINK": str(link), "OFS_EXE": str(exe),
                        "OFS_DIR": str(exe.parent)})
    return link


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="export to a temp folder and start it once")
    parser.add_argument("--shortcut", action="store_true", help="put a shortcut on the desktop")
    args = parser.parse_args()
    if sys.platform != "win32":
        raise SystemExit("build_windows.py runs on Windows")
    if args.check:
        with tempfile.TemporaryDirectory(prefix="ofs-export-") as temp:
            exe = export(Path(temp))
            smoke(exe)
            print(f"export check passed ({exe.name}, {DLL})")
        return 0
    exe = export(DIST)
    if args.shortcut:
        print(f"shortcut: {desktop_shortcut(exe)}")
    print(f'launch: "{exe}"')
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
