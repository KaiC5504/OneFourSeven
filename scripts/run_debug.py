#!/usr/bin/env python3
"""The debug build in one command: sync the data, build the Debug extension, import the project
when anything changed, then start the game in a window.

    python scripts/run_debug.py                            # the table, phone link on
    python scripts/run_debug.py -- --demo-screen=table     # everything after -- goes to the game

The game picks up parameters live from `uv run tools/devlink.py` running in another terminal,
which serves core/data/table_params.json (spec 17.1).
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import gate
import godot_tools

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / "game"
# Inside .godot, so deleting Godot's cache also forces the next import.
IMPORT_STAMP = GAME / ".godot" / "ofs-import.stamp"
# Build output the Debug build rewrites every time; none of it is an imported resource.
NOT_IMPORTED = {".dll", ".pdb", ".exp", ".lib", ".ilk"}


def must(what: str, step: gate.Step) -> None:
    print(f"{what}...", flush=True)
    ok, output = step()
    if not ok:
        raise SystemExit(f"{what} failed:\n" + "\n".join(output.splitlines()[-40:]))


def import_needed() -> bool:
    if not IMPORT_STAMP.exists():
        return True
    since = IMPORT_STAMP.stat().st_mtime
    for path in GAME.rglob("*"):
        if ".godot" in path.relative_to(GAME).parts or path.suffix in NOT_IMPORTED:
            continue
        if path.is_file() and path.stat().st_mtime > since:
            return True
    return False


def main(argv: list[str]) -> int:
    if argv and argv[0] != "--":
        print(__doc__.strip(), file=sys.stderr)
        return 2
    game_args = argv[1:]
    must("sync_data", gate.step([sys.executable, ROOT / "scripts" / "sync_data.py"]))
    configure, build = gate.configure_and_build(gate.BUILD, "Debug")
    must("configure", configure)
    must("Debug build", build)
    godot = str(godot_tools.find_godot())
    if import_needed():
        must("Godot import", gate.step([godot, "--headless", "--path", GAME, "--import"], timeout=600))
        IMPORT_STAMP.touch()
    command = [godot, "--path", str(GAME), "--windowed"] + (["--", *game_args] if game_args else [])
    print("$ " + " ".join(command), flush=True)
    return subprocess.call(command, cwd=ROOT)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
