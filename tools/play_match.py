#!/usr/bin/env python3
"""Plays a whole match in the debug game and has every referee decision re-judged from its log.

    python tools/play_match.py [--best-of N] [--seed S] [--first-breaker random|you|computer]
                               [--level ID | --exhibition [TABLE]]
                               [--slow] [--resolution WxH] [--out DIR] [--timeout SECONDS]
                               [--godot EXE] [--show]

Starts the game through tools/harness_client.py with its user folder moved under --out (default
build/play-match), so the run's settings and logs are its own. The stand-in plays both sides
(start_match with autoplay and fast; --slow lets every stroke play out), match_wait and
match_continue take it through each frame's summary to the match's, where the state is dumped to
state.json and the screen saved as match_summary.png. Then the session log is read and
`ofs_sim_cli referee` (the Debug build's, or $OFS_SIM_CLI) re-judges every rules.decision in it.

--level ID plays the match against that level of the computer opponent (beginner, maximum), the
person's side still the stand-in's. --exhibition plays one Watch a 147 instead, on exhibition table
TABLE (default 0), to its summary (exhibition_summary.png), and reports the break. With either,
`ofs_sim_cli opponent` also makes every opponent.decision of the log again and must agree.

Prints the strokes, each frame and the result. Exit code 0 only when the match finished, the log
fits the schema (tools/check_logs.py) and has no app.error or rules.invalid, and the referee
(and with a level, the opponent check) agrees; 1 when any of those fails; 2 for bad arguments, or when the game or ofs_sim_cli could
not be run.

The seed is the match's: the rack and who breaks. The stand-in's choices and each shot's own
seed are fresh every run, so two runs on one seed are different matches.

On Windows it runs itself on the hidden desktop (scripts/background.py), so the game's window
never shows on the screen; --show keeps it there. The Debug extension must be built first
(python scripts/run_debug.py builds it).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Callable, Sequence, TextIO

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_logs  # noqa: E402
from harness_client import Harness, HarnessError  # noqa: E402

MATCH_LENGTHS = (1, 3, 5, 7, 9, 11)
BREAKERS = ("random", "you", "computer")
DEFAULT_OUT = ROOT / "build" / "play-match"
# Set for the copy of this script that runs on the hidden desktop.
HIDDEN = "OFS_PLAY_MATCH_HIDDEN"
REFEREE_TIMEOUT = 600
# Saved in the game's user folder, then moved to --out.
STATE_FILE = "state.json"
PICTURE_FILE = "match_summary.png"
EXHIBITION_PICTURE_FILE = "exhibition_summary.png"
EXIT_OK, EXIT_FAILED, EXIT_UNUSABLE = 0, 1, 2


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="play_match.py", description=__doc__.split("\n\n")[0])
    parser.add_argument("--best-of", type=int, default=3, choices=MATCH_LENGTHS, help="frames (default 3)")
    parser.add_argument("--seed", type=int, default=147, help="the match's seed (default 147)")
    parser.add_argument("--first-breaker", default="random", choices=BREAKERS)
    against = parser.add_mutually_exclusive_group()
    against.add_argument("--level", default="", help="the computer opponent's level id (default: the stand-in)")
    against.add_argument("--exhibition", type=int, nargs="?", const=0, default=None, metavar="TABLE",
                         help="play one Watch a 147 on this exhibition table (default 0) instead of a match")
    parser.add_argument("--slow", action="store_true", help="let every stroke play out instead of skipping it")
    parser.add_argument("--resolution", default="1912x880", help="window size, WxH")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="where the run's user folder and files go")
    parser.add_argument("--timeout", type=float, default=900.0, help="seconds the whole match may take")
    parser.add_argument("--godot", help="the Godot editor executable (default: $OFS_GODOT or the pinned one)")
    parser.add_argument("--show", action="store_true", help="leave the game's window on the screen")
    args = parser.parse_args(argv)
    if args.seed < 0:
        parser.error(f"--seed {args.seed}: want 0 or more")
    if not re.fullmatch(r"\d+x\d+", args.resolution):
        parser.error(f"--resolution {args.resolution}: want WxH, e.g. 1912x880")
    if not args.timeout > 0:
        parser.error(f"--timeout {args.timeout}: want more than 0 seconds")
    if args.exhibition is not None and args.exhibition < 0:
        parser.error(f"--exhibition {args.exhibition}: want a table index, 0 or more")
    if args.level and not re.fullmatch(r"[a-z0-9_]+", args.level):
        parser.error(f"--level {args.level}: want a level id such as beginner")
    return args


def isolated_user_dir(base: Path) -> tuple[dict[str, str], Path]:
    """The environment that moves the game's user:// under base, and the folder it becomes."""
    sys.path.insert(0, str(ROOT / "scripts"))
    import gate
    return gate.isolated_user_dir(base)


def find_sim_cli(environ: dict[str, str] | None = None, root: Path = ROOT) -> Path | None:
    """$OFS_SIM_CLI, else the Debug build's ofs_sim_cli; None when there is none."""
    environ = os.environ if environ is None else environ
    configured = environ.get("OFS_SIM_CLI")
    if configured:
        return Path(configured) if Path(configured).is_file() else None
    name = "ofs_sim_cli.exe" if sys.platform == "win32" else "ofs_sim_cli"
    folder = root / "build" / "windows" / "tools" / "sim_cli"
    for path in (folder / "Debug" / name, folder / name):
        if path.is_file():
            return path
    return None


def newest_session_log(logs: Path) -> Path | None:
    """The session log written last under the game's logs folder."""
    found = [p for p in logs.rglob("*.jsonl") if p.is_file() and p.name != "rejected.jsonl"] if logs.is_dir() else []
    return max(found, key=lambda p: p.stat().st_mtime) if found else None


def read_log(path: Path, match_id: str = "") -> dict:
    """What a session log says of a match: its strokes (rules.decision lines), each frame.ended
    summary, the match.ended line, every app.error and rules.invalid, and the lines that are not
    JSON. With match_id, only that match's rules, frame and match lines count; app.error and
    rules.invalid lines always do."""
    seen = {"lines": 0, "strokes": 0, "frames": [], "result": None, "exhibition": None, "levelled": 0, "errors": [],
            "not_json": 0}
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if not line.strip():
                continue
            seen["lines"] += 1
            try:
                event = json.loads(line)
            except ValueError:
                seen["not_json"] += 1
                continue
            if not isinstance(event, dict):
                seen["not_json"] += 1
                continue
            name = event.get("event")
            data = event.get("data") if isinstance(event.get("data"), dict) else {}
            if name == "app.error":
                seen["errors"].append(f"{event.get('level', '?')} {data.get('kind', '?')}: {data.get('message', '')}")
                continue
            if name == "rules.invalid":
                seen["errors"].append(f"{event.get('level', '?')} rules.invalid {data.get('what', '?')}: {data.get('detail', '')}")
                continue
            if match_id and data.get("match_id") != match_id:
                continue
            if name == "rules.decision":
                seen["strokes"] += 1
            elif name == "frame.ended":
                seen["frames"].append(data.get("summary") if isinstance(data.get("summary"), dict) else {})
            elif name == "match.ended":
                seen["result"] = data
            elif name == "exhibition.ended":
                seen["exhibition"] = data
            elif name == "opponent.decision" and data.get("level"):
                seen["levelled"] += 1
    return seen


def play(game, args: argparse.Namespace, out: Path, clock: Callable[[], float] = time.monotonic) -> dict:
    """Plays the match to its summary. {"match_id", "frames" (frame summaries left behind),
    "finished", "problem" ("" when it finished)}. The state dump and the screenshot are saved on
    the way as STATE_FILE and PICTURE_FILE in the game's user folder: a relative path keeps the
    user's own folders out of the harness.command lines, and run() moves them to `out`."""
    played = {"match_id": "", "frames": 0, "finished": False, "problem": ""}
    deadline = clock() + args.timeout
    try:
        if args.exhibition is not None:
            return watch(game, args, played, deadline, clock)
        level = {"level": args.level} if args.level else {}
        started = game.call("start_match", best_of=args.best_of, first_breaker=args.first_breaker, seed=args.seed,
                            autoplay=True, fast=not args.slow, **level)
        played["match_id"] = started["match_id"]
        while True:
            left = deadline - clock()
            if left <= 0:
                played["problem"] = f"the match was not over after {args.timeout:.0f} s ({played['frames']} frames played)"
                return played
            waiting = game.call("match_wait", timeout_s=left)["waiting"]
            if waiting == "frame_summary":
                played["frames"] += 1
                game.call("match_continue")
            elif waiting == "match_summary":
                break
            else:
                played["problem"] = f"the match waits for the person ({waiting!r}) though the stand-in plays for them"
                return played
        game.call("dump_state", path=STATE_FILE)
        game.call("screenshot", path=PICTURE_FILE)
        played["finished"] = True
    except HarnessError as error:
        played["problem"] = str(error)
    return played


def watch(game, args: argparse.Namespace, played: dict, deadline: float, clock: Callable[[], float]) -> dict:
    """Watch a 147 to its summary, play()'s answer for an exhibition; the picture is
    EXHIBITION_PICTURE_FILE. Raises HarnessError as the harness does."""
    started = game.call("start_exhibition", layout=args.exhibition, seed=args.seed, fast=not args.slow)
    played["match_id"] = started["match_id"]
    played["layout"] = started.get("layout", "")
    left = deadline - clock()
    if left <= 0:
        played["problem"] = f"the exhibition was not over after {args.timeout:.0f} s"
        return played
    waiting = game.call("match_wait", timeout_s=left)["waiting"]
    if waiting != "exhibition_summary":
        played["problem"] = f"the exhibition waits for the person ({waiting!r}), who only watches"
        return played
    game.call("dump_state", path=STATE_FILE)
    game.call("screenshot", path=EXHIBITION_PICTURE_FILE)
    played["finished"] = True
    return played


def run_referee(sim_cli: Path, log: Path) -> tuple[int, str]:
    done = subprocess.run([str(sim_cli), "referee", str(log)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, encoding="utf-8", errors="replace", timeout=REFEREE_TIMEOUT)
    return done.returncode, done.stdout


def run_opponent(sim_cli: Path, log: Path) -> tuple[int, str]:
    """`ofs_sim_cli opponent`, which reads the levels from core/data: run from the repository."""
    done = subprocess.run([str(sim_cli), "opponent", str(log)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, encoding="utf-8", errors="replace", timeout=REFEREE_TIMEOUT, cwd=str(ROOT))
    return done.returncode, done.stdout


def frame_line(summary: dict, number: int) -> str:
    scores = summary.get("scores") if isinstance(summary.get("scores"), list) else ["?", "?"]
    winner = {0: "you", 1: "computer"}.get(summary.get("winner"), "nobody")
    return (f"  frame {summary.get('number', number)}: {scores[0]}-{scores[1]}, {winner} ({summary.get('reason', '?')}, "
            f"{summary.get('strokes', '?')} strokes)")


def run(args: argparse.Namespace, make_game: Callable[..., object] = Harness,
        referee: Callable[[Path, Path], tuple[int, str]] = run_referee,
        opponent: Callable[[Path, Path], tuple[int, str]] = run_opponent,
        user_dir_for: Callable[[Path], tuple[dict[str, str], Path]] = isolated_user_dir,
        out: TextIO = sys.stdout) -> int:
    def say(text: str) -> None:
        print(text, file=out, flush=True)

    sim_cli = find_sim_cli()
    if sim_cli is None:
        say("no ofs_sim_cli to re-judge the match: build the Debug tree (python scripts/run_debug.py) or set OFS_SIM_CLI")
        return EXIT_UNUSABLE
    folder = args.out.resolve()
    env, user_dir = user_dir_for(folder / "user")
    # Each run reads only its own session.
    shutil.rmtree(user_dir / "logs", ignore_errors=True)
    folder.mkdir(parents=True, exist_ok=True)
    os.environ.update(env)

    game = make_game(resolution=args.resolution, extra_args=["--no-link"], godot=args.godot)
    try:
        game.start()
    except HarnessError as error:
        say(f"the game did not start: {error}")
        return EXIT_UNUSABLE
    try:
        played = play(game, args, folder)
    finally:
        code = game.stop()
    for name in (STATE_FILE, PICTURE_FILE, EXHIBITION_PICTURE_FILE):
        if (user_dir / name).is_file():
            shutil.move(str(user_dir / name), str(folder / name))
    failures = []
    exhibition = args.exhibition is not None
    if not played["finished"]:
        failures.append(f"the {'exhibition' if exhibition else 'match'} did not finish: {played['problem']}")
    if code not in (0, None):
        failures.append(f"the game exited with code {code}; its last output:\n{game.tail()}")

    log = newest_session_log(user_dir / "logs")
    if log is None:
        say(f"the game wrote no session log under {user_dir / 'logs'}")
        for failure in failures:
            say("FAILED: " + failure)
        return EXIT_FAILED
    seen = read_log(log, played["match_id"])
    if exhibition:
        say(f"exhibition {played['match_id'] or '(not started)'}: table {args.exhibition} "
            f"({played.get('layout') or '?'}), seed {args.seed}, log {log}")
    else:
        against = f", against {args.level}" if args.level else ""
        say(f"match {played['match_id'] or '(not started)'}: best of {args.best_of}{against}, seed {args.seed}, log {log}")
    say(f"  {seen['strokes']} strokes judged, {len(seen['frames'])} frames ended")
    for number, summary in enumerate(seen["frames"], 1):
        say(frame_line(summary, number))
    result = seen["result"]
    if exhibition:
        ended = seen["exhibition"]
        if ended is None:
            failures.append("the log has no exhibition.ended line for the exhibition")
        else:
            maximum = " (a maximum)" if ended.get("maximum") else ""
            say(f"  break: {ended.get('break_points')}{maximum}, ended {ended.get('why')}")
            if ended.get("why") == "left":
                failures.append("the exhibition was left, not watched to its end")
    elif result is None:
        failures.append("the log has no match.ended line for the match")
    else:
        winner = {0: "you", 1: "computer"}.get(result.get("winner"), "nobody")
        say(f"  result: {winner}, frames {result.get('frames')}, {result.get('reason')}")
        if result.get("reason") == "abandoned":
            failures.append("the match was abandoned, not played out")
    if seen["errors"]:
        failures.append(f"the log has {len(seen['errors'])} app.error or rules.invalid lines:\n    "
                        + "\n    ".join(seen["errors"][:20]))
    if seen["not_json"]:
        failures.append(f"{seen['not_json']} lines of the log are not JSON")
    _, off_schema = check_logs.check_file(log, check_logs.load_schema())
    if off_schema:
        failures.append(f"{len(off_schema)} lines of the log do not fit the schema (tools/check_logs.py):\n    "
                        + "\n    ".join(f"line {number}: {reason}" for number, reason in off_schema[:10]))
    if seen["strokes"] == 0:
        failures.append("the log has no rules.decision to re-judge")

    try:
        verdict, output = referee(sim_cli, log)
    except (OSError, subprocess.TimeoutExpired) as error:
        say(f"{sim_cli} referee could not be run: {error}")
        return EXIT_UNUSABLE
    say(f"referee ({sim_cli.name} referee): exit {verdict}")
    for line in output.strip().splitlines()[-15:]:
        say("  " + line)
    if verdict != 0:
        failures.append(f"the referee does not agree with the game's decisions (exit {verdict})")

    if args.level or exhibition:
        if seen["levelled"] == 0:
            failures.append("the log has no opponent.decision of a level to make again")
        try:
            remade, output = opponent(sim_cli, log)
        except (OSError, subprocess.TimeoutExpired) as error:
            say(f"{sim_cli} opponent could not be run: {error}")
            return EXIT_UNUSABLE
        say(f"opponent ({sim_cli.name} opponent): exit {remade}")
        for line in output.strip().splitlines()[-15:]:
            say("  " + line)
        if remade != 0:
            failures.append(f"the opponent's decisions are not made again as logged (exit {remade})")

    for failure in failures:
        say("FAILED: " + failure)
    if not failures:
        say(f"ok: the {'exhibition' if exhibition else 'match'} finished, the log is clean and the referee agrees; "
            f"state and picture in {folder}")
    return EXIT_FAILED if failures else EXIT_OK


def hidden(argv: Sequence[str]) -> int:
    """This script again, on the hidden desktop, where the game's window never shows."""
    sys.path.insert(0, str(ROOT / "scripts"))
    import background
    os.environ[HIDDEN] = "1"
    return background.run([sys.executable, str(Path(__file__).resolve()), *argv], str(ROOT))


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    if sys.platform == "win32" and not args.show and os.environ.get(HIDDEN) != "1":
        return hidden(list(argv))
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
