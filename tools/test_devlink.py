import contextlib
import http.client
import http.server
import io
import json
import pathlib
import shutil
import socket
import tempfile
import threading
import unittest

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

    def test_canonical_is_json_dumps_outside_the_nlohmann_band(self):
        value = {"z": [0.1, 1e-07, 2.5, 1.0, 0.0, -0.0, 999999999999999.9, 1e16, 1.2345678901234568e17,
                       1e21, 5e-324, 1.7976931348623157e308, 0.30000000000000004, -1.5e-10, 2**63 - 1,
                       -2**63, 2**64 - 1, "\u0001\n\t ", {"b": None, "a": False}], "é": 1, "a": True}
        expected = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
        self.assertEqual(devlink.canonical(value), expected.encode("utf-8"))

    def test_canonical_refuses_nan_and_infinity(self):
        for number in (float("nan"), float("inf"), float("-inf")):
            with self.assertRaises(ValueError):
                devlink.canonical({"x": [number]})

    def test_canonical_matches_nlohmann_between_1e15_and_1e16(self):
        cases = {1e15: "1e+15", -1e15: "-1e+15", 1234567890123456.0: "1.234567890123456e+15",
                 9999999999999998.0: "9.999999999999998e+15",
                 1000000000000000.1: "1.0000000000000001e+15", 2**64: "1.8446744073709552e+19"}
        for number, text in cases.items():
            self.assertEqual(devlink.canonical([number]), f"[{text}]".encode(), number)


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
        self.root = pathlib.Path(tempfile.mkdtemp())
        shutil.copytree(ROOT / "core" / "data", self.root / "core" / "data")
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

    def test_health(self):
        status, _, reply = self.request("GET", "/health")
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(reply), {"ok": True, "v": 1})

    def test_unknown_path_is_404(self):
        self.assertEqual(self.request("GET", "/nope")[0], 404)
        self.assertEqual(self.request("POST", "/nope", b"")[0], 404)

    def test_valid_batch_is_stored_verbatim(self):
        odd = line().replace(", ", ",   ")
        status, _, reply = self.request("POST", "/logs", body(line(), odd))
        self.assertEqual(status, 204)
        self.assertEqual(reply, b"")
        self.assertEqual(self.log_file("2025-11-02").read_bytes(), body(line(), odd))

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

    def test_unsafe_session_is_a_bad_line(self):
        for session in ("../escape", "a/b", "CON", "x" * 65, ""):
            status, _, reply = self.request("POST", "/logs", body(line(session=session)))
            self.assertEqual(status, 400, session)
            self.assertEqual(json.loads(reply), {"stored": 0, "bad_lines": [0]})
        self.assertFalse((self.root / "escape.jsonl").exists())
        self.assertFalse((self.root / "logs").exists())

    def test_out_of_range_ts_is_a_bad_line(self):
        status, _, reply = self.request("POST", "/logs", body(line(ts=1e300)))
        self.assertEqual(status, 400)
        self.assertEqual(json.loads(reply)["bad_lines"], [0])

    def test_invalid_utf8_is_a_bad_line(self):
        status, _, reply = self.request("POST", "/logs", body(line()) + b"\xff\xfe\n")
        self.assertEqual(status, 400)
        self.assertEqual(json.loads(reply), {"stored": 1, "bad_lines": [1]})

    def test_body_over_1_mib_is_413(self):
        big = b" " * (devlink.MAX_BODY + 1)
        status, _, _ = self.request("POST", "/logs", big)
        self.assertEqual(status, 413)
        self.assertFalse((self.root / "logs").exists())

    def test_512_lines_ok_513_is_413(self):
        self.assertEqual(self.request("POST", "/logs", body(*[line()] * 512))[0], 204)
        self.assertEqual(self.request("POST", "/logs", body(*[line()] * 513))[0], 413)
        self.assertEqual(len(self.log_file("2025-11-02").read_bytes().split(b"\n")) - 1, 512)

    def test_missing_content_length_is_411(self):
        reply = self.raw(b"POST /logs HTTP/1.0\r\n\r\n")
        self.assertTrue(reply.startswith(b"HTTP/1.0 411"), reply)

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

    def test_params_reread_on_every_request(self):
        path = self.root / "core" / "data" / "table_params.json"
        _, headers, first = self.request("GET", "/params")
        path.write_text(json.dumps({"schema_version": 1, "values": {"mu_s": 0.2}}), encoding="utf-8")
        status, _, second = self.request("GET", "/params", headers={"If-None-Match": headers["ETag"]})
        self.assertEqual(status, 200)
        self.assertNotEqual(json.loads(first)["hash"], json.loads(second)["hash"])
        self.assertEqual(json.loads(second)["files"]["table_params"]["values"], {"mu_s": 0.2})

    def test_broken_params_file_is_500(self):
        path = self.root / "core" / "data" / "table_params.json"
        path.write_text("{broken", encoding="utf-8")
        status, _, reply = self.request("GET", "/params")
        self.assertEqual(status, 500)
        self.assertIn("not valid JSON", json.loads(reply)["error"])
        path.unlink()
        status, _, reply = self.request("GET", "/params")
        self.assertEqual(status, 500)
        self.assertIn("cannot read", json.loads(reply)["error"])

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


if __name__ == "__main__":
    unittest.main()
