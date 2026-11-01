#!/usr/bin/env python3
"""Copies core/data/*.json into game/data/ and writes game/data/build_info.json.

    python scripts/sync_data.py        # OFS_BUILD_NUMBER sets the build number (default "dev")

The game embeds its own copy so exports never reach outside res://.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "core" / "data"
TARGET = ROOT / "game" / "data"


def project_version() -> str:
    text = (ROOT / "game" / "project.godot").read_text(encoding="utf-8")
    match = re.search(r'^config/version="([^"]+)"', text, re.MULTILINE)
    if not match:
        raise SystemExit("game/project.godot has no application/config/version")
    return match.group(1)


def git_sha() -> str:
    result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"git rev-parse HEAD failed: {result.stderr.strip()}")
    return result.stdout.strip()


def main() -> int:
    TARGET.mkdir(parents=True, exist_ok=True)
    sources = sorted(SOURCE.glob("*.json"))
    wanted = {path.name for path in sources} | {"build_info.json"}
    for stale in TARGET.glob("*.json"):
        if stale.name not in wanted:
            stale.unlink()
    for path in sources:
        shutil.copyfile(path, TARGET / path.name)
    info = {
        "version": project_version(),
        "build_number": os.environ.get("OFS_BUILD_NUMBER") or "dev",
        "git_sha": git_sha(),
    }
    (TARGET / "build_info.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    print(f"synced {len(sources)} data files; build {info['version']} ({info['build_number']}) {info['git_sha'][:12]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
