extends TestCase
## OfsDevlink against FakeDevlinkServer on 127.0.0.1 (docs/devlink-protocol.md). Intervals
## run at SCALE: uploads every 0.1 s, /params every 0.3 s, /health retries and timeouts 0.5 s.
##
## Tests that inspect uploaded lines need OfsLog's upload queue; against the contract stub
## (content_hash returns "") they pass without checking anything.

const SCALE := 0.1
const MARKER := "devlink_test"

var _server: FakeDevlinkServer
var _link: OfsDevlink
var _link_was_enabled := false
var _statuses: Array[String] = []
var _params: Array[PackedStringArray] = []


func test_off_until_in_tree() -> void:
	var link := OfsDevlink.new()
	check_eq(link.get_status(), "off", "status before _ready")
	check_eq(link.get_host(), "", "host before _ready")
	link.free()


func test_discovers_through_health() -> void:
	await _start()
	check(await _until(func() -> bool: return _link.get_status() == "connected"), "connects")
	check_eq(_statuses, ["searching", "connected"] as Array[String], "status changes")
	check_eq(_link.get_host(), "127.0.0.1", "host")
	check_eq(_link.get_port(), _server.port, "port")
	check_eq(_server.requests[0]["path"], "/health", "first request")
	await _finish()


func test_refused_keeps_searching() -> void:
	var closed := FakeDevlinkServer.new()
	closed.listen()
	var port := closed.port
	closed.free()
	await _start(port)
	await _wait(1.5)
	check_eq(_link.get_status(), "searching", "status with nothing listening")
	check_eq(_statuses, ["searching"] as Array[String], "status changes")
	await _finish()


func test_health_timeout_keeps_searching_then_connects() -> void:
	await _start(0, FakeDevlinkServer.Mode.HANG)
	await _wait(1.2)
	check_eq(_link.get_status(), "searching", "status while /health hangs")
	check(_server.requests_to("/health").size() >= 2, "/health retried after a timeout")
	_server.mode = FakeDevlinkServer.Mode.NORMAL
	check(await _until(func() -> bool: return _link.get_status() == "connected"), "connects once answered")
	await _finish()


func test_disabling_goes_off() -> void:
	await _start()
	await _until(func() -> bool: return _link.get_status() == "connected")
	_link.link_enabled = false
	check_eq(_link.get_status(), "off", "status")
	check_eq(_statuses.back(), "off", "last status change")
	var seen := _server.requests.size()
	await _wait(0.6)
	check_eq(_server.requests.size(), seen, "no requests while off")
	await _finish()


func test_params_200_then_304() -> void:
	await _start()
	check(await _until(func() -> bool: return _params.size() == 1), "params_received")
	check_eq(_params[0][1], _server.params_hash, "hash")
	check(_params[0][0].contains('"schema_version":1'), "integers stay integers: " + _params[0][0])
	var files: Variant = JSON.parse_string(_params[0][0])
	check(files is Dictionary and (files as Dictionary).get("table_params") is Dictionary, "files object")
	_link.applied_params_hash = _server.params_hash
	var etag := '"%s"' % _server.params_hash
	var conditional := func() -> int:
		return _server.requests_to("/params").filter(
			func(r: Dictionary) -> bool: return (r["headers"] as Dictionary).get("if-none-match", "") == etag
		).size()
	check(await _until(func() -> bool: return conditional.call() >= 2), "If-None-Match sent")
	check_eq(_params.size(), 1, "no signal on 304")
	_server.params_hash = "fedcba9876543210"
	check(await _until(func() -> bool: return _params.size() == 2), "params_received for a new hash")
	if _params.size() == 2:
		check_eq(_params[1][1], "fedcba9876543210", "new hash")
	await _finish()


func test_params_received_only_for_new_hash() -> void:
	await _start(0, FakeDevlinkServer.Mode.NORMAL, func(link: OfsDevlink, server: FakeDevlinkServer) -> void:
		server.ignore_if_none_match = true
		link.applied_params_hash = server.params_hash)
	check(await _until(func() -> bool: return _server.requests_to("/params").size() >= 3), "/params polled")
	check_eq(_params.size(), 0, "no signal for the applied hash")
	await _finish()


func test_rejected_params_not_offered_again() -> void:
	await _start(0, FakeDevlinkServer.Mode.NORMAL, func(_unused: OfsDevlink, server: FakeDevlinkServer) -> void:
		server.ignore_if_none_match = true)
	check(await _until(func() -> bool: return _server.requests_to("/params").size() >= 3), "/params polled")
	check_eq(_params.size(), 1, "offered once while unapplied")
	await _finish()


func test_lost_when_server_stops_answering_then_recovers() -> void:
	await _start()
	await _until(func() -> bool: return _link.get_status() == "connected")
	_server.mode = FakeDevlinkServer.Mode.CLOSE
	check(await _until(func() -> bool: return _statuses.has("lost")), "lost")
	check(await _until(func() -> bool: return _link.get_status() == "searching"), "searching again")
	check_eq(_statuses, ["searching", "connected", "lost", "searching"] as Array[String], "status changes")
	_server.mode = FakeDevlinkServer.Mode.NORMAL
	check(await _until(func() -> bool: return _link.get_status() == "connected"), "reconnects")
	await _finish()


func test_lost_after_timeouts() -> void:
	await _start()
	await _until(func() -> bool: return _link.get_status() == "connected")
	var started := Time.get_ticks_msec()
	_server.mode = FakeDevlinkServer.Mode.HANG
	check(await _until(func() -> bool: return _statuses.has("lost"), 5.0), "lost after three timeouts")
	check(Time.get_ticks_msec() - started >= 1400, "waited out the timeouts")
	await _finish()


func test_batches_arrive_intact_and_in_order() -> void:
	await _start()
	if not _log_ready():
		await _finish()
		return
	await _until(func() -> bool: return _link.get_status() == "connected")
	_drain()
	for i in 600:
		_emit_marker(i)
	check(await _until(func() -> bool: return _markers().size() == 600), "all lines arrive")
	check_eq(_markers(), range(600), "order")
	for request in _server.requests_to("/logs"):
		var lines := _lines(request)
		check(lines.size() <= 512, "at most 512 lines per POST")
		check_eq((request["headers"] as Dictionary).get("content-type", ""), "application/x-ndjson", "content type")
		for line in lines:
			check(JSON.parse_string(line) is Dictionary, "each line is a JSON object: " + line)
	await _finish()


func test_batches_capped_at_one_mib() -> void:
	await _start()
	if not _log_ready():
		await _finish()
		return
	await _until(func() -> bool: return _link.get_status() == "connected")
	_drain()
	# Just over 1 MiB, to stay inside the upload ring buffer's own cap.
	var filler := "x".repeat(8000)
	for i in 140:
		OfsLog.emit("harness", "info", "harness.command", {"command": MARKER, "args": [i, filler], "ok": true})
	check(await _until(func() -> bool: return _markers().size() == 140), "all lines arrive")
	check_eq(_markers(), range(140), "order")
	check(_server.requests_to("/logs").size() >= 2, "split into several POSTs")
	for request in _server.requests_to("/logs"):
		check((request["body"] as String).to_utf8_buffer().size() <= 1024 * 1024, "POST body within 1 MiB")
	await _finish()


func test_bad_lines_dropped_not_retried() -> void:
	await _start()
	if not _log_ready():
		await _finish()
		return
	await _until(func() -> bool: return _link.get_status() == "connected")
	_drain()
	_server.log_replies = [{"status": 400, "body": '{"stored": 9, "bad_lines": [0]}'}]
	for i in 10:
		_emit_marker(i)
	check(await _until(func() -> bool: return not _logged("devlink.upload_failed").is_empty()), "upload_failed arrives")
	await _wait(0.5)
	check_eq(_markers(), range(10), "the rejected batch is not sent again")
	var failures := _logged("devlink.upload_failed")
	if not failures.is_empty():
		check_eq(failures[0]["status"], 400, "status")
		check((failures[0]["error"] as String).contains("1 of"), "error names the bad line count")
	check_eq(_link.get_status(), "connected", "still connected")
	await _finish()


func test_failed_upload_restored_with_backoff() -> void:
	await _start()
	if not _log_ready():
		await _finish()
		return
	await _until(func() -> bool: return _link.get_status() == "connected")
	_drain()
	var earlier := _server.requests_to("/logs").size()
	_server.log_replies = [{"status": 503, "body": ""}, {"status": 503, "body": ""}]
	for i in 5:
		_emit_marker(i)
	check(await _until(func() -> bool: return _markers().size() >= 5), "lines arrive after two failures")
	var posts := _server.requests_to("/logs").slice(earlier)
	check(posts.size() >= 3, "retried")
	if posts.size() >= 3:
		check(posts[1]["time"] - posts[0]["time"] >= 0.09, "1 s backoff (scaled)")
		check(posts[2]["time"] - posts[1]["time"] >= 0.19, "2 s backoff (scaled)")
	check_eq(_markers(), range(5), "restored lines keep their order")
	await _until(func() -> bool: return not _logged("devlink.upload_failed").is_empty())
	var failures := _logged("devlink.upload_failed")
	check_eq(failures.size(), 1, "second failure within 30 s is not logged")
	if not failures.is_empty():
		check_eq(failures[0]["status"], 503, "status")
		check_eq(failures[0]["attempt"], 1, "attempt")
	check(not _statuses.has("lost"), "answered failures don't lose the link")
	await _finish()


func test_server_down_restores_then_delivers() -> void:
	await _start()
	if not _log_ready():
		await _finish()
		return
	await _until(func() -> bool: return _link.get_status() == "connected")
	_drain()
	_server.mode = FakeDevlinkServer.Mode.CLOSE
	for i in 20:
		_emit_marker(i)
	check(await _until(func() -> bool: return _statuses.has("lost")), "lost")
	_server.mode = FakeDevlinkServer.Mode.NORMAL
	check(await _until(func() -> bool: return not _logged("devlink.lost").is_empty(), 8.0), "lost line delivered")
	check_eq(_markers(), range(20), "every queued line delivered once, in order")
	var failures := _logged("devlink.upload_failed")
	check(not failures.is_empty() and failures[0]["status"] == 0, "upload_failed with status 0")
	var lost := _logged("devlink.lost")
	if not lost.is_empty():
		check(lost[0].has("reason") and lost[0].has("queued_lines"), "lost fields")
	await _finish()


func test_link_autoload_runs_and_feeds_params() -> void:
	check(Link.link_enabled, "Link enabled by default")
	check(Link.get_status() in ["searching", "connected"], "link.gd keeps OfsDevlink's _ready: " + Link.get_status())
	check(Link.params_received.is_connected(Params.apply_remote), "params_received -> Params.apply_remote")


func test_thermal_state_unknown_on_desktop() -> void:
	check_eq(OfsDevice.thermal_state(), "unknown", "thermal state")


func _start(port: int = 0, mode: FakeDevlinkServer.Mode = FakeDevlinkServer.Mode.NORMAL,
		configure: Callable = Callable()) -> void:
	# The Link autoload shares OfsLog's upload queue; it must not drain it under a test.
	_link_was_enabled = Link.link_enabled
	Link.link_enabled = false
	_server = FakeDevlinkServer.new()
	check_eq(_server.listen(), OK, "fake server listens")
	_server.mode = mode
	tree.root.add_child(_server)
	_link = OfsDevlink.new()
	_link.set_test_target("127.0.0.1", port if port != 0 else _server.port)
	_link.set_test_time_scale(SCALE)
	if configure.is_valid():
		configure.call(_link, _server)
	_link.status_changed.connect(func(status: String) -> void: _statuses.append(status))
	_link.params_received.connect(func(json_text: String, hash: String) -> void:
		_params.append(PackedStringArray([json_text, hash])))
	tree.root.add_child(_link)
	await tree.process_frame


func _finish() -> void:
	_link.queue_free()
	_server.queue_free()
	await tree.process_frame
	Link.link_enabled = _link_was_enabled


func _until(condition: Callable, seconds: float = 4.0) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000.0)
	while not condition.call():
		if Time.get_ticks_msec() > deadline:
			return false
		await tree.process_frame
	return true


func _wait(seconds: float) -> void:
	await tree.create_timer(seconds).timeout


func _log_ready() -> bool:
	return OfsLog.content_hash("{}") != ""


func _drain() -> void:
	while not OfsLog.take_batch(4096).is_empty():
		pass


func _emit_marker(i: int) -> void:
	OfsLog.emit("harness", "info", "harness.command", {"command": MARKER, "args": [i], "ok": true})


func _lines(request: Dictionary) -> PackedStringArray:
	return (request["body"] as String).split("\n", false)


## Lines the server stored: those of POSTs it answered with 204, and the good ones of a 400.
func _received() -> Array[Dictionary]:
	var lines: Array[Dictionary] = []
	for request in _server.requests_to("/logs"):
		if request.get("status", 0) not in [204, 400]:
			continue
		for line in _lines(request):
			var parsed: Variant = JSON.parse_string(line)
			if parsed is Dictionary:
				lines.append(parsed)
	return lines


func _markers() -> Array:
	var order := []
	for line in _received():
		var data: Dictionary = line.get("data", {})
		if line.get("event") == "harness.command" and data.get("command") == MARKER:
			order.append(int(data["args"][0]))
	return order


func _logged(event: String) -> Array[Dictionary]:
	var found: Array[Dictionary] = []
	for line in _received():
		if line.get("event") == event:
			found.append(line.get("data", {}))
	return found
