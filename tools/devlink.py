# /// script
# requires-python = ">=3.12"
# dependencies = ["zeroconf"]
# ///
"""The PC end of the phone link (docs/devlink-protocol.md).

Stores the game's logs in logs/<date>/<session>.jsonl with a live tail, and serves
core/data/table_params.json at GET /params, read again for every request.
"""
import argparse
import datetime as dt
import json
import os
import pathlib
import re
import socket
import socketserver
import sys
import threading
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

import check_logs

ROOT = pathlib.Path(__file__).resolve().parent.parent
PORT = 47147
VERSION = 1
SERVICE_TYPE = "_ofs-devlink._tcp.local."
MAX_BODY = 1024 * 1024
MAX_LINES = 512
MAX_DRAIN = 16 * MAX_BODY
TAIL_WIDTH = 200
# The session names the log file, so it must not be able to climb out of logs/ or hit a
# Windows device name.
SESSION_RE = re.compile(r"[A-Za-z0-9_-]{1,64}")
DEVICE_NAMES = {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(10)), *(f"LPT{i}" for i in range(10))}

DIM, BOLD, INVERSE, RESET = "\033[90m", "\033[1m", "\033[7m", "\033[0m"
LEVEL_COLORS = {"debug": "\033[90m", "info": "", "warn": "\033[33m", "error": "\033[31m"}


def canonical(value):
    """json.dumps(sort_keys=True, separators=(",", ":"), ensure_ascii=False) as UTF-8, except
    where nlohmann's dump() writes numbers differently, so both sides hash the same bytes."""
    return _canonical(value).encode("utf-8")


def _canonical(value):
    if isinstance(value, dict):
        return "{" + ",".join(f"{json.dumps(k, ensure_ascii=False)}:{_canonical(value[k])}" for k in sorted(value)) + "}"
    if isinstance(value, list):
        return "[" + ",".join(_canonical(v) for v in value) + "]"
    if isinstance(value, int) and not isinstance(value, bool) and not -2**63 <= value < 2**64:
        # nlohmann parses integers outside int64/uint64 as doubles.
        try:
            value = float(value)
        except OverflowError:
            raise ValueError(f"number {value} is out of range") from None
    if isinstance(value, float) and 1e15 <= abs(value) < 1e16:
        # nlohmann switches to an exponent one decade before Python's repr does.
        digits = repr(abs(value)).replace(".", "").rstrip("0")
        mantissa = digits[0] + (f".{digits[1:]}" if len(digits) > 1 else "")
        return f"{'-' if value < 0 else ''}{mantissa}e+15"
    # nlohmann writes NaN and Infinity as null; refusing them beats a silently different hash.
    return json.dumps(value, ensure_ascii=False, allow_nan=False)


def fnv1a64_hex(data):
    h = 0xCBF29CE484222325
    for byte in data:
        h = ((h ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return f"{h:016x}"


def content_hash(value):
    return fnv1a64_hex(canonical(value))


def lan_addresses():
    addresses = set()
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            # connect() on UDP sends nothing; it only picks the outbound interface.
            probe.connect(("192.0.2.1", 9))
            addresses.add(probe.getsockname()[0])
    except OSError:
        pass
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            addresses.add(info[4][0])
    except OSError:
        pass
    return sorted(a for a in addresses if not a.startswith("127."))


def session_ok(session):
    return bool(SESSION_RE.fullmatch(session)) and session.upper() not in DEVICE_NAMES


def utc_day(ts):
    try:
        return dt.datetime.fromtimestamp(ts, dt.timezone.utc).strftime("%Y-%m-%d")
    except (OverflowError, OSError, ValueError):
        return None


def local_time(ts):
    try:
        return dt.datetime.fromtimestamp(ts).strftime("%H:%M:%S.%f")[:-3]
    except (OverflowError, OSError, ValueError):
        return "--:--:--.---"


def format_event(event, color=True):
    level = event["level"]
    data = json.dumps(event["data"], separators=(",", ":"), ensure_ascii=False)
    line = f"{local_time(event['ts'])} {event['platform']:<13} {level:<5} {event['event']:<20} {data}"
    if len(line) > TAIL_WIDTH:
        line = line[: TAIL_WIDTH - 1] + "…"
    if not color:
        return line
    style = LEVEL_COLORS.get(level, "")
    if level in ("warn", "error"):
        style = BOLD + style
    if event["event"] == "app.error":
        style = INVERSE + style
    return f"{style}{line}{RESET}" if style else line


class Devlink:
    def __init__(self, root, quiet=False, color=False):
        self.root = pathlib.Path(root)
        self.logs = self.root / "logs"
        self.params_path = self.root / "core" / "data" / "table_params.json"
        self.schema = check_logs.load_schema(self.root / "core" / "data" / "log_schema.json")
        self.quiet = quiet
        self.color = color
        self.write_lock = threading.Lock()
        self.print_lock = threading.Lock()

    def say(self, text, style=""):
        if self.quiet:
            return
        if style and self.color:
            text = f"{style}{text}{RESET}"
        # The files are the record; a console that cannot show a line must not cost the reply.
        try:
            with self.print_lock:
                print(text, flush=True)
        except (UnicodeError, OSError, ValueError):
            pass

    def store(self, body, client):
        """Returns (status, JSON payload or None)."""
        raw_lines = body.split(b"\n")
        numbered = [(i, raw.rstrip(b"\r")) for i, raw in enumerate(raw_lines) if raw.strip()]
        if len(numbered) > MAX_LINES:
            return 413, {"error": f"{len(numbered)} lines, at most {MAX_LINES} per request"}
        bad = []
        targets = {}
        events = []
        for index, raw in numbered:
            try:
                text = raw.decode("utf-8")
            except UnicodeDecodeError:
                bad.append((index, "not UTF-8"))
                continue
            reason = check_logs.validate_line(text, self.schema)
            event = None
            if reason is None:
                event = check_logs.loads_strict(text)
                day = utc_day(event["ts"])
                if day is None:
                    reason = f"ts {event['ts']} is out of range"
                elif not session_ok(event["session"]):
                    reason = f"session {event['session']!r} is not a usable file name"
            if reason:
                bad.append((index, reason))
                continue
            path = self.logs / day / f"{event['session']}.jsonl"
            targets.setdefault(path, []).append(raw)
            events.append(event)
        try:
            with self.write_lock:
                for path, lines in targets.items():
                    path.parent.mkdir(parents=True, exist_ok=True)
                    with open(path, "ab") as handle:
                        handle.write(b"".join(line + b"\n" for line in lines))
        except OSError as error:
            self.say(f"log write failed: {error}", BOLD + LEVEL_COLORS["error"])
            return 500, {"error": f"log write failed: {type(error).__name__}"}
        for event in events:
            self.say(format_event(event, self.color))
        for index, reason in bad:
            self.say(f"bad line {index} from {client}: {reason}", LEVEL_COLORS["warn"])
        if bad:
            return 400, {"stored": len(events), "bad_lines": [index for index, _ in bad]}
        return 204, None

    def params(self):
        """Returns (hash, response bytes); raises ValueError with a readable reason."""
        try:
            text = self.params_path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            raise ValueError(f"cannot read {self.params_path}: {error}") from error
        try:
            parsed = check_logs.loads_strict(text)
        except (ValueError, RecursionError) as error:
            raise ValueError(f"{self.params_path} is not valid JSON: {error}") from error
        files = {"table_params": parsed}
        h = content_hash(files)
        return h, json.dumps({"hash": h, "files": files}, ensure_ascii=False).encode("utf-8")


class Handler(BaseHTTPRequestHandler):
    server_version = f"devlink/{VERSION}"
    # A client that promises more body than it sends would otherwise hold a thread forever.
    timeout = 15

    @property
    def app(self):
        return self.server.app

    def do_GET(self):
        self._guard(self._get)

    def do_POST(self):
        self._guard(self._post)

    def _guard(self, action):
        try:
            action()
        except ConnectionError:
            # The game gave up on this request (timeout, app backgrounded); it retries.
            pass
        except Exception as error:
            self.app.say(f"{self.command} {self.path} failed: {type(error).__name__}: {error}", LEVEL_COLORS["error"])
            try:
                self._json(500, {"error": type(error).__name__})
            except OSError:
                pass

    def _get(self):
        path = urlsplit(self.path).path
        if path == "/health":
            self._json(200, {"ok": True, "v": VERSION})
        elif path == "/params":
            self._params()
        else:
            self._json(404, {"error": "not found"})

    def _params(self):
        try:
            h, body = self.app.params()
        except ValueError as error:
            self.app.say(f"params: {error}", BOLD + LEVEL_COLORS["error"])
            self._json(500, {"error": str(error)})
            return
        etag = f'"{h}"'
        wanted = [tag.strip().removeprefix("W/") for tag in self.headers.get("If-None-Match", "").split(",")]
        if etag in wanted or "*" in wanted:
            self.send_response(304)
            self.send_header("ETag", etag)
            self.end_headers()
            return
        self.app.say(f"params {h} sent to {self.client_address[0]}", DIM)
        self._reply(200, body, "application/json", {"ETag": etag})

    def _post(self):
        if urlsplit(self.path).path != "/logs":
            self._json(404, {"error": "not found"})
            return
        header = self.headers.get("Content-Length")
        if header is None:
            self._json(411, {"error": "Content-Length required"})
            return
        try:
            length = int(header)
            if length < 0:
                raise ValueError
        except ValueError:
            self._json(400, {"error": f"bad Content-Length {header!r}"})
            return
        if length > MAX_BODY:
            self.app.say(f"rejected {length} byte batch from {self.client_address[0]}", LEVEL_COLORS["warn"])
            self._drain(length)
            self._json(413, {"error": f"body is {length} bytes, at most {MAX_BODY}"})
            return
        body = self.rfile.read(length)
        if len(body) < length:
            self._json(400, {"error": f"body ended after {len(body)} of {length} bytes"})
            return
        status, payload = self.app.store(body, self.client_address[0])
        if payload is None:
            self._reply(status, b"", None)
        else:
            self._json(status, payload)

    def _drain(self, length):
        # Closing with the body unread makes Windows reset the connection, and the client
        # then never sees the 413.
        remaining = min(length, MAX_DRAIN)
        while remaining > 0:
            chunk = self.rfile.read(min(remaining, 65536))
            if not chunk:
                break
            remaining -= len(chunk)

    def _json(self, status, payload):
        self._reply(status, json.dumps(payload).encode("utf-8"), "application/json")

    def _reply(self, status, body, content_type, headers=None):
        self.send_response(status)
        if content_type:
            self.send_header("Content-Type", content_type)
        if status != 204:
            self.send_header("Content-Length", str(len(body)))
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        if body:
            self.wfile.write(body)

    def log_message(self, *args):
        pass


class Server(ThreadingHTTPServer):
    # On Windows SO_REUSEADDR lets a second server bind the same port and steal requests;
    # exclusive use makes a second devlink fail to bind instead.
    allow_reuse_address = os.name != "nt"

    def server_bind(self):
        if os.name == "nt":
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        # HTTPServer looks up this host's FQDN here, which takes a second or more on Windows; nothing uses it.
        socketserver.TCPServer.server_bind(self)
        self.server_name, self.server_port = self.server_address[:2]


def make_server(app, host="0.0.0.0", port=PORT):
    server = Server((host, port), Handler, bind_and_activate=False)
    server.app = app
    try:
        server.server_bind()
        server.server_activate()
    except BaseException:
        server.server_close()
        raise
    return server


def devlink_answers(host, port):
    try:
        with urllib.request.urlopen(f"http://{host}:{port}/health", timeout=2) as response:
            reply = json.loads(response.read())
    except (OSError, ValueError):
        return False
    return isinstance(reply, dict) and reply.get("v") == VERSION


def advertise(port):
    from zeroconf import ServiceInfo, Zeroconf

    addresses = lan_addresses()
    if not addresses:
        return None, None
    zeroconf = Zeroconf()
    name = f"One Four Seven devlink on {socket.gethostname()}.{SERVICE_TYPE}"
    info = ServiceInfo(SERVICE_TYPE, name, addresses=[socket.inet_aton(a) for a in addresses],
                       port=port, properties={"v": str(VERSION)})
    try:
        zeroconf.register_service(info)
    except BaseException:
        zeroconf.close()
        raise
    return zeroconf, info


def enable_ansi():
    if not sys.stdout.isatty():
        return False
    if os.name == "nt":
        try:
            import ctypes

            kernel32 = ctypes.windll.kernel32
            handle = kernel32.GetStdHandle(-11)
            mode = ctypes.c_uint32()
            if not kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
                return False
            # ENABLE_VIRTUAL_TERMINAL_PROCESSING, off by default in the classic console.
            kernel32.SetConsoleMode(handle, mode.value | 0x0004)
        except (AttributeError, OSError):
            return False
    return True


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--host", default="0.0.0.0", help="IPv4 address to listen on (default: all)")
    parser.add_argument("--root", type=pathlib.Path, default=ROOT,
                        help="repo root holding logs/ and core/data/ (default: this repo)")
    parser.add_argument("--no-bonjour", action="store_true")
    parser.add_argument("--quiet", action="store_true", help="no live tail")
    args = parser.parse_args(argv)
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    root = args.root.resolve()
    try:
        app = Devlink(root, args.quiet, enable_ansi())
    except (OSError, ValueError) as error:
        print(f"devlink: cannot load the log schema from {root / 'core' / 'data'}: {error}")
        return 1
    try:
        server = make_server(app, args.host, args.port)
    except OSError as error:
        probe = "127.0.0.1" if args.host == "0.0.0.0" else args.host
        if devlink_answers(probe, args.port):
            print(f"devlink already running on :{args.port}")
            return 0
        print(f"devlink: cannot listen on {args.host}:{args.port}: {error.strerror or error}"
              f" (another program holds the port; pick another with --port)")
        return 1
    hosts = ["127.0.0.1", *lan_addresses()] if args.host == "0.0.0.0" else [args.host]
    port = server.server_address[1]
    print(f"devlink v{VERSION} listening on {', '.join(f'{h}:{port}' for h in hosts)}")
    print(f"logs to {app.logs}, params from {app.params_path}")
    zeroconf = None
    if not args.no_bonjour:
        try:
            zeroconf, info = advertise(port)
        except ImportError:
            print("Bonjour off: zeroconf is not installed (run with `uv run tools/devlink.py`)")
        except Exception as error:
            print(f"Bonjour failed, the phone cannot find this PC: {type(error).__name__}: {error}")
        else:
            if zeroconf:
                print(f"Bonjour: {info.name}")
            else:
                print("Bonjour off: no LAN IPv4 address, the phone cannot find this PC")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if zeroconf:
            zeroconf.unregister_all_services()
            zeroconf.close()
        server.server_close()
        print("devlink stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
