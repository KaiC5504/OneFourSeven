import contextlib
import io
import json
import pathlib
import socket
import sys
import tempfile
import textwrap
import threading
import unittest

import harness_client

# A stand-in for the debug game: the channel's wire format on 127.0.0.1, run as a child process
# the way Harness runs Godot. "argv" answers with its command line; "fail" fails; "quit" answers,
# then exits; "silent_exit" exits without a word.
FAKE_GAME = textwrap.dedent("""
    import json, socket, sys
    if "--no-channel" in sys.argv:
        print("starting, but no channel", flush=True)
        sys.exit(3)
    server = socket.socket()
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    print("some engine banner", flush=True)
    print("harness listening on 127.0.0.1:%d" % server.getsockname()[1], flush=True)
    while True:
        conn, _ = server.accept()
        reader = conn.makefile("rb")
        for raw in reader:
            request = json.loads(raw)
            cmd = request["cmd"]
            if cmd == "silent_exit":
                sys.exit(0)
            if cmd == "fail":
                response = {"id": request["id"], "ok": False, "error": "it failed"}
            elif cmd == "argv":
                response = {"id": request["id"], "ok": True, "result": sys.argv[1:]}
            else:
                response = {"id": request["id"], "ok": True, "result": {"cmd": cmd, "args": request.get("args", {})}}
            conn.sendall((json.dumps(response) + "\\n").encode())
            if cmd == "quit":
                conn.close()
                sys.exit(0)
""")


class FakeServer:
    """A one-thread TCP server on 127.0.0.1 answering each line with `answer(request)`."""

    def __init__(self, answer):
        self.answer = answer
        self.requests = []
        self.sock = socket.socket()
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(4)
        self.port = self.sock.getsockname()[1]
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def _serve(self):
        while True:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            with conn, conn.makefile("rb") as reader:
                for raw in reader:
                    request = json.loads(raw)
                    self.requests.append(request)
                    reply = self.answer(request)
                    if reply is None:
                        break
                    conn.sendall(reply if isinstance(reply, bytes) else (json.dumps(reply) + "\n").encode())

    def close(self):
        self.sock.close()


def echo(request):
    return {"id": request["id"], "ok": request["cmd"] != "fail",
            **({"error": "it failed"} if request["cmd"] == "fail" else {"result": request})}


class ConnectionTest(unittest.TestCase):
    def setUp(self):
        self.server = FakeServer(echo)
        self.addCleanup(self.server.close)

    def test_ids_are_added_and_results_returned(self):
        connection = harness_client.Connection(self.server.port, timeout=5)
        self.addCleanup(connection.close)
        first = connection.call("ping")
        second = connection.call("set_setting", key="guide_level", value=2)
        self.assertEqual(first["id"], 1)
        self.assertEqual(second["id"], 2)
        self.assertEqual(second["args"], {"key": "guide_level", "value": 2})
        response = connection.request({"id": "mine", "cmd": "ping"})
        self.assertEqual(response["id"], "mine")

    def test_a_failed_command_raises_with_the_games_error(self):
        connection = harness_client.Connection(self.server.port, timeout=5)
        self.addCleanup(connection.close)
        with self.assertRaisesRegex(harness_client.HarnessError, "fail: it failed"):
            connection.call("fail")
        self.assertFalse(connection.request({"cmd": "fail"})["ok"])

    def test_wait_idle_gets_a_longer_socket_timeout(self):
        connection = harness_client.Connection(self.server.port, timeout=5)
        self.addCleanup(connection.close)
        connection.call("wait_idle", timeout_s=60)
        self.assertGreaterEqual(connection._sock.gettimeout(), 60 + harness_client.WAIT_MARGIN)

    def test_wrong_id_bad_json_and_a_closed_connection(self):
        replies = iter([{"id": 99, "ok": True}, b"not json\n", None])
        server = FakeServer(lambda request: next(replies))
        self.addCleanup(server.close)
        connection = harness_client.Connection(server.port, timeout=5)
        self.addCleanup(connection.close)
        with self.assertRaisesRegex(harness_client.HarnessError, "answer to id 1"):
            connection.call("ping")
        with self.assertRaisesRegex(harness_client.HarnessError, "not JSON"):
            connection.call("ping")
        with self.assertRaisesRegex(harness_client.HarnessError, "closed the connection"):
            connection.call("ping")

    def test_nothing_listening(self):
        unused = socket.socket()
        unused.bind(("127.0.0.1", 0))
        port = unused.getsockname()[1]
        unused.close()
        with self.assertRaisesRegex(harness_client.HarnessError, "cannot connect"):
            harness_client.Connection(port, timeout=2)


class SendCliTest(unittest.TestCase):
    def setUp(self):
        self.server = FakeServer(echo)
        self.addCleanup(self.server.close)

    def send(self, request):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = harness_client.main(["send", "--port", str(self.server.port), "--timeout", "5", request])
        return code, out.getvalue(), err.getvalue()

    def test_prints_the_response(self):
        code, out, _ = self.send('{"cmd": "ping"}')
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(out)["result"]["cmd"], "ping")

    def test_failure_and_bad_requests(self):
        code, out, _ = self.send('{"cmd": "fail"}')
        self.assertEqual(code, 1)
        self.assertEqual(json.loads(out)["error"], "it failed")
        code, _, err = self.send("[1]")
        self.assertEqual(code, 2)
        self.assertIn("bad request", err)


class HarnessTest(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.fake = pathlib.Path(folder.name) / "fake_game.py"
        self.fake.write_text(FAKE_GAME, encoding="utf-8")

    def harness(self, **kwargs):
        return harness_client.Harness(godot=[sys.executable, str(self.fake)], startup_timeout=20, command_timeout=10, **kwargs)

    def test_launches_muted_on_a_free_port_and_quits(self):
        game = self.harness(resolution="800x600", position="lag", extra_args=["--no-link"])
        with game:
            self.assertIsNotNone(game.port)
            argv = game.call("argv")
            self.assertEqual(game.call("ping"), {"cmd": "ping", "args": {}})
        self.assertEqual(argv[:6], ["--path", str(harness_client.GAME), "--windowed", "--resolution", "800x600", "--"])
        self.assertEqual(argv[6:], ["--mute", "--harness-port=0", "--demo-position=lag", "--no-link"])
        self.assertFalse(game.running())
        self.assertIn("some engine banner", game.output)

    def test_a_game_that_never_opens_the_channel(self):
        game = self.harness(extra_args=["--no-channel"])
        with self.assertRaisesRegex(harness_client.HarnessError, "exited with code 3.*\n.*no channel"):
            game.start()

    def test_run_session_prints_responses_and_reports_failure(self):
        out = io.StringIO()
        lines = ["# a comment", "", '{"cmd": "ping"}', "not json", '{"id": 7, "cmd": "fail"}', '{"cmd": "screenshot", "args": {"path": "a.png"}}']
        code = harness_client.run_session(self.harness(), lines, out)
        responses = [json.loads(line) for line in out.getvalue().splitlines()]
        self.assertEqual(code, 1)
        self.assertEqual([r.get("ok") for r in responses], [True, False, False, True])
        self.assertEqual(responses[1]["line"], 4)
        self.assertEqual(responses[2]["id"], 7)
        self.assertEqual(responses[3]["result"]["args"], {"path": "a.png"})

    def test_run_session_ends_at_quit(self):
        out = io.StringIO()
        game = self.harness()
        code = harness_client.run_session(game, ['{"cmd": "ping"}', '{"cmd": "quit"}', '{"cmd": "ping"}'], out)
        self.assertEqual(code, 0)
        self.assertEqual(len(out.getvalue().splitlines()), 2)
        self.assertFalse(game.running())

    def test_a_game_that_dies_mid_session(self):
        out = io.StringIO()
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            code = harness_client.run_session(self.harness(), ['{"cmd": "silent_exit"}', '{"cmd": "ping"}'], out)
        self.assertEqual(code, 1)
        self.assertIn("closed the connection", out.getvalue())


class RunDebugTest(unittest.TestCase):
    """scripts/run_debug.py starts the game muted unless told --sound."""

    def setUp(self):
        sys.path.insert(0, str(harness_client.ROOT / "scripts"))
        self.addCleanup(sys.path.remove, str(harness_client.ROOT / "scripts"))
        import run_debug
        self.args_for = run_debug.game_args_for

    def test_muted_by_default(self):
        self.assertEqual(self.args_for([]), ["--mute"])
        self.assertEqual(self.args_for(["--", "--demo-screen=table"]), ["--mute", "--demo-screen=table"])

    def test_sound_opts_out(self):
        self.assertEqual(self.args_for(["--sound"]), [])
        self.assertEqual(self.args_for(["--sound", "--", "--no-link"]), ["--no-link"])

    def test_anything_else_before_the_separator_is_refused(self):
        self.assertIsNone(self.args_for(["--loud"]))
        self.assertIsNone(self.args_for(["--sound", "--mute"]))


if __name__ == "__main__":
    unittest.main()
