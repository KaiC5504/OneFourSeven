import contextlib
import datetime as dt
import functools
import http.client
import http.server
import io
import json
import math
import os
import pathlib
import random
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import unittest

import check_logs
import devlink

ROOT = pathlib.Path(__file__).resolve().parent.parent
SESSION = "20261102-101500-a1b2c3"


def line(ts=1762078500.25, session=SESSION, event="app.lifecycle", level="info", data=None):
    subsystem = event.split(".")[0]
    return json.dumps({"ts": ts, "session": session, "build": "dev", "platform": "windows",
                       "subsystem": subsystem, "level": level, "event": event,
                       "data": {"state": "foreground"} if data is None else data})


def body(*lines):
    return "".join(l + "\n" for l in lines).encode("utf-8")


@functools.cache
def committed(path):
    """The file as committed: a live-params check may be editing the working tree's copy."""
    return subprocess.run(["git", "-C", str(ROOT), "show", f"HEAD:{path}"], capture_output=True, check=True).stdout


def make_root():
    root = pathlib.Path(tempfile.mkdtemp())
    data = root / "core" / "data"
    data.mkdir(parents=True)
    for name in ("table_params.json", "log_schema.json"):
        (data / name).write_bytes(committed(f"core/data/{name}"))
    return root


class HashTest(unittest.TestCase):
    def test_protocol_vectors(self):
        self.assertEqual(devlink.fnv1a64_hex(b""), "cbf29ce484222325")
        self.assertEqual(devlink.fnv1a64_hex(b"a"), "af63dc4c8601ec8c")
        value = {"b": 1, "a": [True, None, "x"]}
        self.assertEqual(devlink.canonical(value), b'{"a":[true,null,"x"],"b":1}')
        self.assertEqual(devlink.content_hash(value), "8efc364b3052045e")
        value = {"values": {}, "schema_version": 1}
        self.assertEqual(devlink.canonical(value), b'{"schema_version":1,"values":{}}')
        self.assertEqual(devlink.content_hash(value), "b99af9e0a04c6e42")

    def test_canonical_keeps_utf8(self):
        self.assertEqual(devlink.canonical({"k": "é"}), '{"k":"é"}'.encode("utf-8"))

    def test_integers_are_decimal_and_other_numbers_17_significant_digits(self):
        cases = [(0, "0"), (-7, "-7"), (2**63 - 1, "9223372036854775807"), (-2**63, "-9223372036854775808"),
                 (2**64 - 1, "18446744073709551615"), (0.0, "0.0"), (-0.0, "-0.0"), (1.0, "1.0"),
                 (2.5, "2.5"), (100.0, "100.0"), (0.1, "0.10000000000000001"),
                 (0.30000000000000004, "0.30000000000000004"), (0.0001, "0.0001"),
                 (1e-5, "1.0000000000000001e-05"), (1e15, "1000000000000000.0"),
                 (1e16, "10000000000000000.0"), (1e17, "1e+17"), (1.2345678901234568e17, "1.2345678901234568e+17"),
                 (5e-324, "4.9406564584124654e-324"), (1.7976931348623157e308, "1.7976931348623157e+308")]
        for number, text in cases:
            self.assertEqual(devlink.canonical([number]), f"[{text}]".encode(), repr(number))

    def test_canonical_refuses_nan_and_infinity(self):
        for number in (float("nan"), float("inf"), float("-inf")):
            with self.assertRaises(ValueError):
                devlink.canonical({"x": [number]})

    def test_integers_outside_int64_and_uint64_are_refused(self):
        for number in (2**64, -2**63 - 1, 10**30):
            with self.assertRaises(ValueError):
                devlink.canonical([number])

    def test_strings_escape_only_what_json_requires(self):
        text = "\x01\x1f\n\t\"\\/\x7fé \U0001f3b1"
        expected = '["\\u0001\\u001f\\n\\t\\"\\\\/\x7fé \U0001f3b1"]'
        self.assertEqual(devlink.canonical([text]), expected.encode("utf-8"))

    def test_keys_sort_by_code_point(self):
        value = {"\U0001f3b1": 1, "￿": 2, "a": 3, "Z": 4, "": 5}
        self.assertEqual(devlink.canonical(value), '{"":5,"Z":4,"a":3,"￿":2,"\U0001f3b1":1}'.encode("utf-8"))

    def test_lone_surrogates_are_refused(self):
        with self.assertRaises(ValueError):
            devlink.canonical({"k": "\ud800"})


BANDS = ((1e-3, 1e3), (1e15, 1e16), (0.0, 1e-3), (1e-7, 1e-4), (1e3, 1e15), (1e16, 1e22),
         (1e300, 1.7976931348623157e308), (1e-320, 1e-300))
CHARS = "aZ09 _-./\\\"\x00\x01\x1f\x7féß漢字 ﻿\U0001f3b1"
# Lines both sides must refuse, and integer tokens that need care.
EDGE_LINES = ['{"x":NaN}', '{"x":Infinity}', '{"x":1e999}', '{"x":-1e999}', '{"x":"\\ud800"}', '{"x":',
              '[1,2', '{"x":-0}', '{"x":1E2}', '{"x":0.0e0}', '{"x":-0.0}', '3.5', '"s"', 'null', '[]', '{}',
              '{"x":9223372036854775807}', '{"x":-9223372036854775808}', '{"x":18446744073709551615}']


def random_double(rng):
    pick = rng.random()
    if pick < 0.75:
        x = rng.uniform(*rng.choice(BANDS))
    elif pick < 0.85:
        # Any finite bit pattern, subnormals included.
        x = math.inf
        while not math.isfinite(x):
            x = struct.unpack("<d", rng.getrandbits(64).to_bytes(8, "little"))[0]
    elif pick < 0.95:
        # Short decimals, the way tuning values are typed.
        x = round(rng.uniform(0, 100), rng.randrange(1, 7))
    else:
        x = float(rng.randrange(-10**9, 10**9))
    return -x if rng.random() < 0.5 else x


def random_int(rng):
    return rng.choice((rng.randrange(-1000, 1000), rng.randrange(-2**63, 2**63), rng.randrange(2**63, 2**64),
                       rng.choice((0, -2**63, 2**63 - 1, 2**64 - 1, 2**53 + 1))))


def random_string(rng):
    return "".join(rng.choice(CHARS) for _ in range(rng.randrange(12)))


def random_value(rng, depth):
    kinds = ["double"] * 6 + ["int"] * 2 + ["string", "bool", "null"] + (["list", "object"] if depth < 3 else [])
    kind = rng.choice(kinds)
    if kind == "double":
        return random_double(rng)
    if kind == "int":
        return random_int(rng)
    if kind == "string":
        return random_string(rng)
    if kind == "bool":
        return rng.random() < 0.5
    if kind == "null":
        return None
    if kind == "list":
        return [random_value(rng, depth + 1) for _ in range(rng.randrange(5))]
    return {random_string(rng): random_value(rng, depth + 1) for _ in range(rng.randrange(5))}


def random_line(rng):
    value = {random_string(rng): random_value(rng, 1) for _ in range(rng.randrange(1, 6))}
    return json.dumps(value, ensure_ascii=rng.random() < 0.5)


def python_hash(text):
    try:
        return devlink.content_hash(check_logs.loads_strict(text))
    except (ValueError, RecursionError):
        return "error"


def params_hash_tool():
    """OFS_PARAMS_HASH, else the newest ofs_params_hash in build/gate or build/windows."""
    named = os.environ.get("OFS_PARAMS_HASH")
    if named:
        return pathlib.Path(named)
    found = [path for build in ("gate", "windows")
             for pattern in ("*/ofs_params_hash*", "ofs_params_hash*")
             for path in (ROOT / "build" / build / "core").glob(pattern)
             if path.is_file() and path.suffix in ("", ".exe")]
    return max(found, key=lambda path: path.stat().st_mtime, default=None)


class ParityTest(unittest.TestCase):
    def test_cpp_and_python_hash_the_same(self):
        tool = params_hash_tool()
        if tool is None or not tool.is_file():
            self.fail(f"ofs_params_hash not found ({tool or 'nothing in build/gate or build/windows'}): build it with "
                      "`cmake --build build/windows --config Debug --target ofs_params_hash` "
                      "(or run python scripts/gate.py), or set OFS_PARAMS_HASH to its path")
        rng = random.Random(147)
        lines = [random_line(rng) for _ in range(20000)] + EDGE_LINES
        result = subprocess.run([str(tool)], input=body(*lines), capture_output=True, timeout=300)
        got = result.stdout.decode("utf-8", "replace").splitlines()
        stderr = result.stderr.decode("utf-8", "replace").strip()
        self.assertEqual(len(got), len(lines),
                         f"{tool} printed {len(got)} lines for {len(lines)} (exit {result.returncode})"
                         + (f": {stderr[:500]}" if stderr else ""))
        mismatches = []
        for text, cpp in zip(lines, got):
            python = python_hash(text)
            if python != cpp.strip():
                mismatches.append((text, python, cpp))
        report = []
        for text, python, cpp in mismatches[:5]:
            try:
                canonical = devlink.canonical(check_logs.loads_strict(text)).decode("utf-8")
            except (ValueError, RecursionError) as error:
                canonical = f"refused: {error}"
            report.append(f"{text}\n  python {python} {canonical}\n  c++    {cpp}")
        self.assertFalse(mismatches, f"{len(mismatches)} of {len(lines)} lines hash differently:\n" + "\n".join(report))


class FormatTest(unittest.TestCase):
    def event(self, **overrides):
        return dict(json.loads(line()), **overrides)

    def test_plain_line_has_platform_level_event_and_data(self):
        text = devlink.format_event(self.event(), color=False)
        for part in ("windows", "info", "app.lifecycle", '{"state":"foreground"}'):
            self.assertIn(part, text)
        self.assertNotIn("\033", text)

    def test_long_data_is_truncated(self):
        text = devlink.format_event(self.event(data={"state": "x" * 1000}), color=False)
        self.assertEqual(len(text), devlink.TAIL_WIDTH)
        self.assertTrue(text.endswith("…"))

    def test_warnings_and_app_errors_stand_out(self):
        info = devlink.format_event(self.event(), color=True)
        warn = devlink.format_event(self.event(event="perf.hitch", level="warn"), color=True)
        error = devlink.format_event(self.event(event="app.error", level="error"), color=True)
        self.assertNotIn(devlink.BOLD, info)
        self.assertIn(devlink.BOLD + devlink.LEVEL_COLORS["warn"], warn)
        self.assertIn(devlink.INVERSE, error)
        self.assertIn(devlink.LEVEL_COLORS["error"], error)


class ServerTest(unittest.TestCase):
    def setUp(self):
        self.root = make_root()
        self.server = devlink.make_server(devlink.Devlink(self.root, quiet=True), "127.0.0.1", 0)
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, args=(0.05,), daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        shutil.rmtree(self.root, ignore_errors=True)

    def request(self, method, path, data=None, headers=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        try:
            connection.request(method, path, body=data, headers=headers or {})
            response = connection.getresponse()
            return response.status, response.headers, response.read()
        finally:
            connection.close()

    def raw(self, request):
        with socket.create_connection(("127.0.0.1", self.port), timeout=10) as sock:
            sock.sendall(request)
            chunks = []
            while chunk := sock.recv(4096):
                chunks.append(chunk)
        return b"".join(chunks)

    def log_file(self, day, session=SESSION):
        return self.root / "logs" / day / f"{session}.jsonl"

    def session_files(self):
        return sorted(p for p in (self.root / "logs").rglob("*.jsonl") if p.name != "rejected.jsonl")

    def rejected(self):
        entries = []
        for path in sorted((self.root / "logs").glob("*/rejected.jsonl")):
            entries.extend(json.loads(text) for text in path.read_text(encoding="utf-8").splitlines())
        return entries

    def test_health(self):
        status, _, reply = self.request("GET", "/health")
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(reply), {"ok": True, "v": 1})

    def test_unknown_path_is_404(self):
        self.assertEqual(self.request("GET", "/nope")[0], 404)
        self.assertEqual(self.request("POST", "/nope", b"")[0], 404)

    def test_query_strings_are_ignored(self):
        self.assertEqual(self.request("GET", "/health?probe=1")[0], 200)
        status, _, reply = self.request("GET", "/params?since=0")
        self.assertEqual(status, 200)
        self.assertIn("table_params", json.loads(reply)["files"])
        self.assertEqual(self.request("POST", "/logs?batch=3", body(line()))[0], 204)
        self.assertEqual(self.log_file("2025-11-02").read_bytes(), body(line()))

    def test_valid_batch_is_stored_verbatim(self):
        odd = line().replace(", ", ",   ")
        status, _, reply = self.request("POST", "/logs", body(line(), odd))
        self.assertEqual(status, 204)
        self.assertEqual(reply, b"")
        self.assertEqual(self.log_file("2025-11-02").read_bytes(), body(line(), odd))
        self.assertEqual(self.rejected(), [])

    def test_crlf_and_blank_lines_are_tolerated(self):
        status, _, _ = self.request("POST", "/logs", (line() + "\r\n\n" + line()).encode())
        self.assertEqual(status, 204)
        self.assertEqual(self.log_file("2025-11-02").read_bytes(), body(line(), line()))

    def test_requests_append(self):
        self.request("POST", "/logs", body(line()))
        self.request("POST", "/logs", body(line(ts=1762078501)))
        self.assertEqual(self.log_file("2025-11-02").read_bytes(), body(line(), line(ts=1762078501)))

    def test_routing_by_utc_date_and_session(self):
        late = line(ts=1735689599.5)
        early = line(ts=1735689600.0)
        other = line(ts=1735689600.0, session="20250101-000000-ffffff")
        self.assertEqual(self.request("POST", "/logs", body(late, early, other))[0], 204)
        self.assertEqual(self.log_file("2024-12-31").read_bytes(), body(late))
        self.assertEqual(self.log_file("2025-01-01").read_bytes(), body(early))
        self.assertEqual(self.log_file("2025-01-01", "20250101-000000-ffffff").read_bytes(), body(other))

    def test_bad_lines_are_reported_and_good_ones_kept(self):
        batch = body(line(), "not json", line(ts=1762078502), line(event="app.nope"),
                     line(level="debug"), line(data={}))
        status, _, reply = self.request("POST", "/logs", batch)
        self.assertEqual(status, 400)
        self.assertEqual(json.loads(reply), {"stored": 2, "bad_lines": [1, 3, 4, 5]})
        self.assertEqual(self.log_file("2025-11-02").read_bytes(), body(line(), line(ts=1762078502)))

    def test_rejected_lines_are_kept_with_their_reasons(self):
        before = dt.datetime.now(dt.timezone.utc)
        self.request("POST", "/logs", body(line(), "not json", line(event="app.nope")))
        after = dt.datetime.now(dt.timezone.utc)
        entries = self.rejected()
        self.assertEqual([entry["line"] for entry in entries], ["not json", line(event="app.nope")])
        self.assertIn("not JSON", entries[0]["reason"])
        self.assertIn("unknown event 'app.nope'", entries[1]["reason"])
        for entry in entries:
            self.assertEqual(set(entry), {"received", "reason", "line"})
            received = dt.datetime.fromisoformat(entry["received"])
            self.assertLessEqual(before - dt.timedelta(milliseconds=1), received)
            self.assertLessEqual(received, after)
        path, = (self.root / "logs").glob("*/rejected.jsonl")
        self.assertIn(path.parent.name, {before.strftime("%Y-%m-%d"), after.strftime("%Y-%m-%d")})

    def test_unsafe_session_is_a_bad_line(self):
        for session in ("../escape", "a/b", "CON", "x" * 65, ""):
            status, _, reply = self.request("POST", "/logs", body(line(session=session)))
            self.assertEqual(status, 400, session)
            self.assertEqual(json.loads(reply), {"stored": 0, "bad_lines": [0]})
        self.assertFalse((self.root / "escape.jsonl").exists())
        self.assertEqual(self.session_files(), [])
        self.assertEqual(len(self.rejected()), 5)

    def test_out_of_range_ts_is_a_bad_line(self):
        for ts in ("1e300", "1e999", "-1", "1e11"):
            text = line().replace("1762078500.25", ts)
            status, _, reply = self.request("POST", "/logs", body(text))
            self.assertEqual(status, 400, ts)
            self.assertEqual(json.loads(reply), {"stored": 0, "bad_lines": [0]}, ts)
            self.assertIn("is outside", self.rejected()[-1]["reason"])
        self.assertEqual(self.session_files(), [])

    def test_invalid_utf8_is_a_bad_line(self):
        status, _, reply = self.request("POST", "/logs", body(line()) + b"\xff\xfe\n")
        self.assertEqual(status, 400)
        self.assertEqual(json.loads(reply), {"stored": 1, "bad_lines": [1]})
        self.assertEqual(self.rejected()[0]["line"], "\\xff\\xfe")
        self.assertEqual(self.rejected()[0]["reason"], "not UTF-8")

    def test_body_over_1_mib_is_413(self):
        big = b" " * (devlink.MAX_BODY + 1)
        status, _, reply = self.request("POST", "/logs", big)
        self.assertEqual(status, 413)
        self.assertEqual(json.loads(reply), {"error": f"body is {devlink.MAX_BODY + 1} bytes, at most {devlink.MAX_BODY}"})
        self.assertEqual(self.session_files(), [])
        entry, = self.rejected()
        self.assertEqual(entry["line"], "")
        self.assertIn("at most 1048576", entry["reason"])

    def test_512_lines_ok_513_is_413(self):
        self.assertEqual(self.request("POST", "/logs", body(*[line()] * 512))[0], 204)
        status, _, reply = self.request("POST", "/logs", body(*[line()] * 513))
        self.assertEqual(status, 413)
        self.assertEqual(json.loads(reply), {"error": "513 lines, at most 512 per request"})
        self.assertEqual(len(self.log_file("2025-11-02").read_bytes().split(b"\n")) - 1, 512)
        entries = self.rejected()
        self.assertEqual(len(entries), 513)
        self.assertEqual({entry["line"] for entry in entries}, {line()})

    def test_missing_content_length_is_411(self):
        reply = self.raw(b"POST /logs HTTP/1.0\r\n\r\n")
        self.assertTrue(reply.startswith(b"HTTP/1.0 411"), reply)

    def test_bad_content_length_is_400(self):
        for header in (b"-5", b"banana", b"+5", b"1_0", b"0x10", b"1e3"):
            reply = self.raw(b"POST /logs HTTP/1.0\r\nContent-Length: " + header + b"\r\n\r\n")
            self.assertTrue(reply.startswith(b"HTTP/1.0 400"), (header, reply))
            self.assertIn(b"bad Content-Length", reply)
        self.assertFalse((self.root / "logs").exists())

    def test_garbage_request_does_not_stop_the_server(self):
        self.raw(b"\x00\x01garbage\r\n\r\n")
        self.raw(b"POST /logs HTTP/1.0\r\nContent-Length: banana\r\n\r\n")
        self.assertEqual(self.request("GET", "/health")[0], 200)

    def test_concurrent_posts_do_not_interleave(self):
        batches = [body(*[line(ts=1762078500 + t, data={"state": "s" * 300})] * 50) for t in range(8)]
        threads = [threading.Thread(target=self.request, args=("POST", "/logs", b)) for b in batches]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        stored = self.log_file("2025-11-02").read_text(encoding="utf-8").split("\n")
        self.assertEqual(stored.pop(), "")
        self.assertEqual(len(stored), 400)
        for text in stored:
            json.loads(text)

    def test_params_then_304(self):
        status, headers, reply = self.request("GET", "/params")
        self.assertEqual(status, 200)
        payload = json.loads(reply)
        params = json.loads((self.root / "core" / "data" / "table_params.json").read_text(encoding="utf-8"))
        self.assertEqual(payload["files"], {"table_params": params})
        self.assertEqual(payload["hash"], devlink.content_hash({"table_params": params}))
        self.assertRegex(payload["hash"], r"^[0-9a-f]{16}$")
        self.assertEqual(headers["ETag"], f'"{payload["hash"]}"')
        status, headers, reply = self.request("GET", "/params", headers={"If-None-Match": headers["ETag"]})
        self.assertEqual(status, 304)
        self.assertEqual(reply, b"")
        status, _, _ = self.request("GET", "/params", headers={"If-None-Match": '"0000000000000000"'})
        self.assertEqual(status, 200)

    def test_weak_list_and_star_if_none_match_are_304(self):
        _, headers, _ = self.request("GET", "/params")
        etag = headers["ETag"]
        for value in (f"W/{etag}", "*", f'"0000000000000000", {etag}', f'"0000000000000000",W/{etag}'):
            status, _, reply = self.request("GET", "/params", headers={"If-None-Match": value})
            self.assertEqual(status, 304, value)
            self.assertEqual(reply, b"")

    def test_params_reread_on_every_request(self):
        path = self.root / "core" / "data" / "table_params.json"
        _, headers, first = self.request("GET", "/params")
        path.write_text(json.dumps({"schema_version": 1, "values": {"mu_s": 0.2}}), encoding="utf-8")
        status, _, second = self.request("GET", "/params", headers={"If-None-Match": headers["ETag"]})
        self.assertEqual(status, 200)
        self.assertNotEqual(json.loads(first)["hash"], json.loads(second)["hash"])
        self.assertEqual(json.loads(second)["files"]["table_params"]["values"], {"mu_s": 0.2})

    def test_each_served_parameter_set_is_archived_once(self):
        archive = self.root / "logs" / "params"
        first = json.loads(self.request("GET", "/params")[2])
        saved = archive / f"{first['hash']}.json"
        files = json.loads(saved.read_text(encoding="utf-8"))
        self.assertEqual(files, first["files"])
        self.assertEqual(devlink.content_hash(files), first["hash"])
        saved.write_text("left alone", encoding="utf-8")
        self.request("GET", "/params")
        self.assertEqual(saved.read_text(encoding="utf-8"), "left alone")
        path = self.root / "core" / "data" / "table_params.json"
        path.write_text(json.dumps({"schema_version": 1, "values": {"mu_s": 0.2}}), encoding="utf-8")
        second = json.loads(self.request("GET", "/params")[2])
        self.assertEqual(sorted(p.name for p in archive.iterdir()), sorted(f"{h}.json" for h in (first["hash"], second["hash"])))
        self.assertEqual(json.loads((archive / f"{second['hash']}.json").read_text(encoding="utf-8")), second["files"])

    def test_broken_params_file_is_500(self):
        path = self.root / "core" / "data" / "table_params.json"
        path.write_text("{broken", encoding="utf-8")
        status, _, reply = self.request("GET", "/params")
        self.assertEqual(status, 500)
        self.assertIn("not valid JSON", json.loads(reply)["error"])
        path.write_text('{"values": {"mu_s": 1e999}}', encoding="utf-8")
        status, _, reply = self.request("GET", "/params")
        self.assertEqual(status, 500)
        self.assertIn("cannot be hashed", json.loads(reply)["error"])
        path.unlink()
        status, _, reply = self.request("GET", "/params")
        self.assertEqual(status, 500)
        self.assertIn("cannot read", json.loads(reply)["error"])
        self.assertFalse((self.root / "logs" / "params").exists())

    def run_main(self, port):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = devlink.main(["--host", "127.0.0.1", "--port", str(port), "--no-bonjour",
                                 "--quiet", "--root", str(self.root)])
        return code, out.getvalue()

    def test_second_devlink_on_the_port_exits_cleanly(self):
        code, out = self.run_main(self.port)
        self.assertEqual(code, 0)
        self.assertIn(f"devlink already running on :{self.port}", out)

    def test_port_held_by_something_else_is_an_error(self):
        class NotDevlink(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                self.send_error(404)

            def log_message(self, *args):
                pass

        other = devlink.Server(("127.0.0.1", 0), NotDevlink)
        thread = threading.Thread(target=other.serve_forever, args=(0.05,), daemon=True)
        thread.start()
        try:
            code, out = self.run_main(other.server_address[1])
        finally:
            other.shutdown()
            other.server_close()
            thread.join()
        self.assertEqual(code, 1)
        self.assertIn("cannot listen", out)


class ArchiveTest(unittest.TestCase):
    def setUp(self):
        self.root = make_root()

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def test_failed_archive_is_said_once_and_params_still_served(self):
        blocker = self.root / "logs" / "params"
        blocker.parent.mkdir()
        blocker.write_text("a file where the folder goes", encoding="utf-8")
        app = devlink.Devlink(self.root)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            first, _ = app.params()
            second, _ = app.params()
        self.assertEqual(first, second)
        self.assertEqual(out.getvalue().count("cannot archive params"), 1, out.getvalue())
        blocker.unlink()
        with contextlib.redirect_stdout(io.StringIO()):
            app.params()
        self.assertEqual([p.name for p in blocker.iterdir()], [f"{first}.json"])


class ConsoleTest(unittest.TestCase):
    def test_startup_lines_reach_a_pipe_at_once(self):
        root = make_root()
        env = {key: value for key, value in os.environ.items() if key != "PYTHONUNBUFFERED"}
        process = subprocess.Popen([sys.executable, str(ROOT / "tools" / "devlink.py"), "--host", "127.0.0.1",
                                    "--port", "0", "--no-bonjour", "--quiet", "--root", str(root)],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
        try:
            first = []
            reader = threading.Thread(target=lambda: first.append(process.stdout.readline()), daemon=True)
            reader.start()
            reader.join(20)
            self.assertTrue(first, "devlink printed nothing a pipe could read while running")
            self.assertIn(b"listening on 127.0.0.1:", first[0])
        finally:
            process.kill()
            process.wait(10)
            process.stdout.close()
            shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
