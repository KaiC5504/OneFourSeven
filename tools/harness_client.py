#!/usr/bin/env python3
"""Drives the debug game on Windows through the harness command channel (docs/harness.md).

    python tools/harness_client.py run [--resolution WxH] [--position NAME] [--godot EXE] [-- GAME_ARGS...]
    python tools/harness_client.py send --port N '{"cmd": "ping"}'

`run` starts the debug game in a window, muted, with --harness-port=0, reads the port it prints,
then sends each line of stdin as one request ({"cmd", "args"}; an "id" is added when missing)
and prints each response as one JSON line. Blank lines and lines starting with # are skipped. At
the end of stdin it sends quit unless a request already did, and waits for the game to exit. The
exit code is 1 when any request failed. The game's own output goes to stderr when it fails to
start. The Debug extension must be built first (python scripts/run_debug.py builds it).

`send` sends one request to a game already listening on --port and prints the response.

In Python:

    with Harness(resolution="1912x880") as game:
        game.call("load_position", name="straight_1m")
        shot = game.call("play_shot", aim_rad=0.0, tip_x=0.0, tip_y=0.0, elevation_rad=0.0, speed_m_s=2.0)
        game.call("wait_idle", timeout_s=30)
        game.call("screenshot", path="C:/tmp/after.png")
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Iterable, Sequence, TextIO

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / "game"
HOST = "127.0.0.1"
PORT_LINE = re.compile(r"harness listening on 127\.0\.0\.1:(\d+)")
STARTUP_TIMEOUT = 90.0
COMMAND_TIMEOUT = 120.0
# wait_idle's own timeout plus this, so the socket never gives up before the game answers.
WAIT_MARGIN = 15.0
QUIT_TIMEOUT = 20.0
OUTPUT_LINES = 400


class HarnessError(RuntimeError):
    pass


def find_godot() -> str:
    """The editor that runs the debug game: $OFS_GODOT, else the one scripts/godot_tools.py finds."""
    configured = os.environ.get("OFS_GODOT")
    if configured:
        return configured
    sys.path.insert(0, str(ROOT / "scripts"))
    import godot_tools
    return str(godot_tools.find_godot())


class Connection:
    """One client of the channel: requests go out one at a time, each answered in order."""

    def __init__(self, port: int, timeout: float = COMMAND_TIMEOUT):
        self.port = port
        self.timeout = timeout
        self._next_id = 1
        self._buffer = b""
        try:
            self._sock = socket.create_connection((HOST, port), timeout=timeout)
        except OSError as e:
            raise HarnessError(f"cannot connect to {HOST}:{port}: {e}") from e

    def close(self) -> None:
        self._sock.close()

    def request(self, request: dict) -> dict:
        """Sends one request and returns its response, whether ok or not."""
        request = dict(request)
        if request.get("id") is None:
            request["id"] = self._next_id
            self._next_id += 1
        timeout = self.timeout
        args = request.get("args")
        if isinstance(args, dict) and isinstance(args.get("timeout_s"), (int, float)):
            timeout = max(timeout, args["timeout_s"] + WAIT_MARGIN)
        self._sock.settimeout(timeout)
        try:
            self._sock.sendall((json.dumps(request) + "\n").encode("utf-8"))
            line = self._read_line()
        except (OSError, socket.timeout) as e:
            raise HarnessError(f"{request.get('cmd')!r} (id {request['id']}): {e}") from e
        try:
            response = json.loads(line)
        except ValueError as e:
            raise HarnessError(f"{request.get('cmd')!r}: the game answered something that is not JSON: {line[:200]!r}") from e
        if not isinstance(response, dict) or response.get("id") != request["id"]:
            raise HarnessError(f"{request.get('cmd')!r}: expected the answer to id {request['id']!r}, got {line[:200]!r}")
        return response

    def call(self, cmd: str, **args) -> object:
        """The result of one command; raises HarnessError with the game's error when it fails."""
        response = self.request({"cmd": cmd, "args": args})
        if not response.get("ok"):
            raise HarnessError(f"{cmd}: {response.get('error')}")
        return response.get("result")

    def _read_line(self) -> str:
        while b"\n" not in self._buffer:
            chunk = self._sock.recv(65536)
            if not chunk:
                raise HarnessError(f"the game closed the connection on port {self.port}")
            self._buffer += chunk
        line, _, self._buffer = self._buffer.partition(b"\n")
        return line.decode("utf-8", errors="replace")


class Harness:
    """The debug game started for driving: windowed, muted, on a free channel port."""

    def __init__(self, resolution: str | None = None, position: str | None = None, extra_args: Sequence[str] = (),
                 godot: str | Sequence[str] | None = None, startup_timeout: float = STARTUP_TIMEOUT,
                 command_timeout: float = COMMAND_TIMEOUT):
        # A sequence is a whole command prefix (the tests run a stand-in game through Python).
        if godot is None:
            godot = find_godot()
        self.prefix = [godot] if isinstance(godot, str) else list(godot)
        self.resolution = resolution
        self.position = position
        self.extra_args = list(extra_args)
        self.startup_timeout = startup_timeout
        self.command_timeout = command_timeout
        self.port: int | None = None
        self.output: collections.deque[str] = collections.deque(maxlen=OUTPUT_LINES)
        self._process: subprocess.Popen | None = None
        self._connection: Connection | None = None
        self._port_found = threading.Event()
        self._reader: threading.Thread | None = None

    def command(self) -> list[str]:
        engine = ["--path", str(GAME), "--windowed"]
        if self.resolution:
            engine += ["--resolution", self.resolution]
        game = ["--mute", "--harness-port=0"]
        if self.position:
            game.append(f"--demo-position={self.position}")
        return [*self.prefix, *engine, "--", *game, *self.extra_args]

    def start(self) -> int:
        command = self.command()
        try:
            self._process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                             stdin=subprocess.DEVNULL, text=True, encoding="utf-8", errors="replace")
        except OSError as e:
            raise HarnessError(f"cannot start {command[0]}: {e}") from e
        # Drained for the whole run, so a full pipe never stalls the game.
        self._reader = threading.Thread(target=self._read_output, daemon=True)
        self._reader.start()
        deadline = time.monotonic() + self.startup_timeout
        while not self._port_found.wait(0.1):
            if self._process.poll() is not None:
                self._reader.join(2.0)
                raise HarnessError(f"the game exited with code {self._process.returncode} before opening the channel; "
                                   f"its last output:\n{self.tail()}")
            if time.monotonic() > deadline:
                self.stop()
                raise HarnessError(f"no 'harness listening' line within {self.startup_timeout:.0f} s "
                                   f"(is this a debug build on Windows?); its last output:\n{self.tail()}")
        self._connection = Connection(self.port, self.command_timeout)
        return self.port

    def tail(self, lines: int = 40) -> str:
        return "\n".join(list(self.output)[-lines:])

    def request(self, request: dict) -> dict:
        if self._connection is None:
            raise HarnessError("the game is not started")
        return self._connection.request(request)

    def call(self, cmd: str, **args) -> object:
        if self._connection is None:
            raise HarnessError("the game is not started")
        return self._connection.call(cmd, **args)

    def running(self) -> bool:
        return self._process is not None and self._process.poll() is None

    def stop(self, timeout: float = QUIT_TIMEOUT) -> int | None:
        """Asks the game to quit (unless it already is), waits for it, kills it if it hangs."""
        if self._process is None:
            return None
        if self._connection is not None:
            if self.running():
                try:
                    self._connection.request({"cmd": "quit"})
                except HarnessError:
                    pass
            self._connection.close()
            self._connection = None
        try:
            self._process.wait(timeout)
        except subprocess.TimeoutExpired:
            self._process.kill()
            self._process.wait()
        if self._reader is not None:
            self._reader.join(2.0)
        return self._process.returncode

    def __enter__(self) -> Harness:
        self.start()
        return self

    def __exit__(self, *exc) -> None:
        self.stop()

    def _read_output(self) -> None:
        assert self._process is not None and self._process.stdout is not None
        for line in self._process.stdout:
            line = line.rstrip("\r\n")
            self.output.append(line)
            match = PORT_LINE.search(line)
            if match and not self._port_found.is_set():
                self.port = int(match.group(1))
                self._port_found.set()
        self._process.stdout.close()


def parse_request(line: str) -> dict:
    request = json.loads(line)
    if not isinstance(request, dict) or not isinstance(request.get("cmd"), str):
        raise ValueError('a request is a JSON object with a "cmd" string')
    return request


def run_session(harness: Harness, lines: Iterable[str], out: TextIO) -> int:
    """Sends each request line, prints each response; quits at the end. 1 when anything failed."""
    failed = False
    harness.start()
    try:
        for number, line in enumerate(lines, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            try:
                request = parse_request(line)
            except ValueError as e:
                print(json.dumps({"line": number, "ok": False, "error": f"bad request: {e}"}), file=out, flush=True)
                failed = True
                continue
            try:
                response = harness.request(request)
            except HarnessError as e:
                print(json.dumps({"id": request.get("id"), "ok": False, "error": str(e)}), file=out, flush=True)
                failed = True
                break
            print(json.dumps(response), file=out, flush=True)
            failed = failed or not response.get("ok")
            if request["cmd"] == "quit":
                break
    finally:
        code = harness.stop()
    if code not in (0, None):
        print(f"the game exited with code {code}; its last output:\n{harness.tail()}", file=sys.stderr)
        failed = True
    return 1 if failed else 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="harness_client.py", description=__doc__.split("\n\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("run", help="start the debug game and send stdin's requests")
    run.add_argument("--resolution", help="window size, WxH (Godot's --resolution)")
    run.add_argument("--position", help="a named position to load at start (--demo-position)")
    run.add_argument("--godot", help="the Godot editor executable (default: $OFS_GODOT or the pinned one)")
    run.add_argument("game_args", nargs="*", help="more user arguments for the game, after --")
    send = commands.add_parser("send", help="send one request to a game already listening")
    send.add_argument("--port", type=int, required=True)
    send.add_argument("--timeout", type=float, default=COMMAND_TIMEOUT)
    send.add_argument("request", help='one request as JSON, e.g. \'{"cmd": "ping"}\'')
    args = parser.parse_args(argv)

    if args.command == "send":
        try:
            request = parse_request(args.request)
        except ValueError as e:
            print(f"bad request: {e}", file=sys.stderr)
            return 2
        try:
            connection = Connection(args.port, args.timeout)
            try:
                response = connection.request(request)
            finally:
                connection.close()
        except HarnessError as e:
            print(str(e), file=sys.stderr)
            return 1
        print(json.dumps(response))
        return 0 if response.get("ok") else 1

    if args.resolution and not re.fullmatch(r"\d+x\d+", args.resolution):
        print(f"--resolution {args.resolution}: want WxH, e.g. 1912x880", file=sys.stderr)
        return 2
    try:
        harness = Harness(args.resolution, args.position, args.game_args, args.godot)
        return run_session(harness, sys.stdin, sys.stdout)
    except HarnessError as e:
        print(str(e), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
