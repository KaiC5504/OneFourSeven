extends TestCase
## OfsDevlink against FakeDevlinkServer on 127.0.0.1 (docs/devlink-protocol.md). Intervals
## run at SCALE: uploads every 0.1 s, /params every 0.3 s, /health retries and timeouts 0.5 s.
##
## Each test link uploads its own folder of session files under user://, never the real
## user://logs; what the link itself logs is read back from the real session file.

const SCALE := 0.1
const MARKER := "devlink_test"
const EARLIER := "20260101-000000-aaaaaa.jsonl"
const CURRENT := "20260102-000000-cccccc.jsonl"
const LinkScript := preload("res://scripts/autoload/link.gd")

var _server: FakeDevlinkServer
var _link: OfsDevlink
var _link_was_enabled := false
var _dir := ""
var _log_start := 0
var _statuses: Array[String] = []
var _status_times: Array[float] = []
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
	var connected := _logged("devlink.connected")
	check_eq(connected.size(), 1, "devlink.connected logged")
	if not connected.is_empty():
		check_eq(connected[0]["server_version"], 1.0, "server_version")
	await _finish()


func test_refused_keeps_searching_and_logs_once() -> void:
	var closed := FakeDevlinkServer.new()
	closed.listen()
	var port := closed.port
	closed.free()
	var started := Time.get_unix_time_from_system()
	await _start(port)
	await _wait(1.5)
	var times := _logged_times("devlink.request_failed")
	# Windows retries a refused localhost connect for about 2 s; 127.0.0.1 gives up after 1 s.
	check(not times.is_empty() and times[0] - started < 3.0 * SCALE,
			"the first failure within the 1 s local connect timeout (scaled): %s" % [times.map(func(t: float) -> float: return t - started)])
	check_eq(_link.get_status(), "searching", "status with nothing listening")
	check_eq(_statuses, ["searching"] as Array[String], "status changes")
	var failures := _logged("devlink.request_failed")
	check(not failures.is_empty(), "devlink.request_failed for /health")
	if not failures.is_empty():
		check_eq(failures[0]["endpoint"], "/health", "endpoint")
		check_eq(failures[0]["status"], 0.0, "status 0: no answer")
		check_eq(failures[0]["attempt"], 1.0, "first attempt")
		var same := failures.filter(func(f: Dictionary) -> bool: return f["error"] == failures[0]["error"])
		check_eq(same.size(), 1, "a repeated error within 30 s is not logged again: %s" % [failures])
	await _finish()


func test_health_timeout_keeps_searching_then_connects() -> void:
	await _start(0, FakeDevlinkServer.Mode.HANG)
	await _wait(1.2)
	check_eq(_link.get_status(), "searching", "status while /health hangs")
	check(_server.requests_to("/health").size() >= 2, "/health retried after a timeout")
	_server.mode = FakeDevlinkServer.Mode.NORMAL
	check(await _until(func() -> bool: return _link.get_status() == "connected"), "connects once answered")
	await _finish()


func test_health_not_ok_keeps_searching() -> void:
	await _start(0, FakeDevlinkServer.Mode.NORMAL, func(_unused: OfsDevlink, server: FakeDevlinkServer) -> void:
		server.queue_reply("/health", 200, '{"ok": false}')
		server.queue_reply("/health", 404, '{"error": "not found"}'))
	check(await _until(func() -> bool: return _server.requests_to("/health").size() >= 2), "/health asked again")
	check_eq(_statuses, ["searching"] as Array[String], "not connected by ok:false or 404")
	var failures := _logged("devlink.request_failed")
	check_eq(failures.size(), 2, "one request_failed per distinct answer")
	if failures.size() == 2:
		check_eq(failures[0]["status"], 200.0, "ok:false status")
		check((failures[0]["error"] as String).contains("ok"), "error says what was wrong: %s" % failures[0])
		check_eq(failures[0]["body"], '{"ok": false}', "body")
		check_eq(failures[1]["status"], 404.0, "404 status")
		check_eq(failures[1]["body"], "not found", "the server's error text")
		check_eq(failures[1]["attempt"], 2.0, "attempt counts on")
	check(await _until(func() -> bool: return _link.get_status() == "connected"), "connects once /health is ok")
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
	check(_logged("devlink.request_failed").is_empty(), "200 and 304 are not failures")
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


func test_params_200_without_hash_is_a_failure() -> void:
	await _start(0, FakeDevlinkServer.Mode.NORMAL, func(_unused: OfsDevlink, server: FakeDevlinkServer) -> void:
		server.queue_reply("/params", 200, '{"files": {}}')
		server.queue_reply("/params", 200, '{"files": {}}'))
	check(await _until(func() -> bool: return _params.size() == 1), "the next good answer is used")
	var failures := _logged("devlink.request_failed")
	check_eq(failures.size(), 1, "logged once, not for the repeat")
	if not failures.is_empty():
		check_eq(failures[0]["endpoint"], "/params", "endpoint")
		check_eq(failures[0]["status"], 200.0, "status")
		check((failures[0]["error"] as String).contains("hash"), "error: %s" % failures[0])
		check_eq(failures[0]["body"], '{"files": {}}', "body")
	check_eq(_link.get_status(), "connected", "still connected")
	await _finish()


func test_params_500_logs_the_server_reason() -> void:
	var reason := "core/data/table_params.json is not valid JSON: Expecting value: line 1 column 1"
	await _start(0, FakeDevlinkServer.Mode.NORMAL, func(_unused: OfsDevlink, server: FakeDevlinkServer) -> void:
		server.queue_reply("/params", 500, JSON.stringify({"error": reason + "x".repeat(600)})))
	check(await _until(func() -> bool: return _params.size() == 1), "params once the server recovers")
	var failures := _logged("devlink.request_failed")
	check_eq(failures.size(), 1, "request_failed")
	if not failures.is_empty():
		check_eq(failures[0]["status"], 500.0, "status")
		check((failures[0]["body"] as String).begins_with(reason), "body is the server's reason")
		check_eq((failures[0]["body"] as String).length(), 500, "body capped at 500 characters")
	await _finish()


func test_lost_when_server_stops_answering_then_recovers() -> void:
	await _start()
	await _until(func() -> bool: return _link.get_status() == "connected")
	_server.mode = FakeDevlinkServer.Mode.CLOSE
	check(await _until(func() -> bool: return _statuses.has("lost")), "lost")
	check(await _until(func() -> bool: return _link.get_status() == "searching"), "searching again")
	check_eq(_statuses, ["searching", "connected", "lost", "searching"] as Array[String], "status changes")
	if _statuses.size() == 4:
		check(_status_times[3] - _status_times[2] >= 3.0 * SCALE - 0.02,
				"lost shows for 3 s (scaled): %.3f" % (_status_times[3] - _status_times[2]))
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


func test_503_loses_the_link() -> void:
	await _start()
	await _until(func() -> bool: return _link.get_status() == "connected")
	_server.fixed_status = 503
	check(await _until(func() -> bool: return _statuses.has("lost")), "three 5xx answers lose the link")
	var lost := _logged("devlink.lost")
	check_eq(lost.size(), 1, "devlink.lost")
	if not lost.is_empty():
		check_eq(lost[0]["reason"], "HTTP 503", "reason")
	await _finish()


func test_batches_arrive_intact_and_in_order() -> void:
	_prepare()
	_append(CURRENT, _marker_lines(0, 600))
	await _start()
	check(await _until(func() -> bool: return _markers().size() == 600), "all lines arrive")
	check_eq(_markers(), range(600), "order")
	for request in _server.requests_to("/logs"):
		var lines := _lines(request)
		check(lines.size() <= 512, "at most 512 lines per POST")
		check_eq((request["headers"] as Dictionary).get("content-type", ""), "application/x-ndjson", "content type")
		for line in lines:
			check(JSON.parse_string(line) is Dictionary, "each line is a JSON object: " + line)
	_append(CURRENT, _marker_lines(600, 610))
	check(await _until(func() -> bool: return _markers().size() == 610), "lines written later follow")
	check_eq(_markers(), range(610), "nothing sent twice")
	await _finish()


func test_batches_capped_at_one_mib() -> void:
	_prepare()
	_append(CURRENT, _marker_lines(0, 140, "x".repeat(8000)))
	await _start()
	check(await _until(func() -> bool: return _markers().size() == 140), "all lines arrive")
	check_eq(_markers(), range(140), "order")
	check(_server.requests_to("/logs").size() >= 2, "split into several POSTs")
	for request in _server.requests_to("/logs"):
		check((request["body"] as String).to_utf8_buffer().size() <= 1024 * 1024, "POST body within 1 MiB")
	await _finish()


func test_line_over_one_mib_skipped_and_counted() -> void:
	_prepare()
	_append(CURRENT, _marker_lines(0, 2))
	_append(CURRENT, _marker_lines(99, 100, "y".repeat(1024 * 1024 + 10)))
	_append(CURRENT, _marker_lines(2, 4))
	await _start()
	check(await _until(func() -> bool: return _markers().size() == 4), "the lines around it arrive")
	check_eq(_markers(), [0, 1, 2, 3], "the oversized line is not sent")
	for request in _server.requests_to("/logs"):
		check((request["body"] as String).length() <= 1024 * 1024, "nothing over 1 MiB is sent")
	var rejected := _logged("devlink.upload_rejected")
	check_eq(rejected.size(), 1, "upload_rejected")
	if not rejected.is_empty():
		check_eq(rejected[0]["status"], 413.0, "status")
		check_eq(rejected[0]["dropped_lines"], 1.0, "one line dropped")
		check((rejected[0]["body"] as String).contains(CURRENT), "names the file: %s" % rejected[0])
	await _finish()


func test_bad_lines_dropped_not_retried() -> void:
	_prepare()
	_append(CURRENT, _marker_lines(0, 10))
	await _start(0, FakeDevlinkServer.Mode.NORMAL, func(_unused: OfsDevlink, server: FakeDevlinkServer) -> void:
		server.queue_reply("/logs", 400, '{"stored": 9, "bad_lines": [0]}'))
	check(await _until(func() -> bool: return not _logged("devlink.upload_rejected").is_empty()), "upload_rejected")
	await _wait(0.5)
	check_eq(_markers(), range(10), "the rejected batch is not sent again")
	var rejected := _logged("devlink.upload_rejected")
	if not rejected.is_empty():
		check_eq(rejected[0]["status"], 400.0, "status")
		check_eq(rejected[0]["dropped_lines"], 1.0, "only the bad line is lost")
		check_eq(rejected[0]["suppressed_since_last"], 0.0, "nothing suppressed")
		check((rejected[0]["body"] as String).contains("bad_lines"), "body: %s" % rejected[0])
	check(_logged("devlink.upload_failed").is_empty(), "a 400 is not a failure to retry")
	check_eq(_link.get_status(), "connected", "still connected")
	await _finish()


func test_413_batch_dropped_not_retried() -> void:
	_prepare()
	_append(CURRENT, _marker_lines(0, 5))
	await _start(0, FakeDevlinkServer.Mode.NORMAL, func(_unused: OfsDevlink, server: FakeDevlinkServer) -> void:
		server.queue_reply("/logs", 413, '{"error": "body is 2000000 bytes, at most 1048576"}'))
	check(await _until(func() -> bool: return not _logged("devlink.upload_rejected").is_empty()), "upload_rejected")
	_append(CURRENT, _marker_lines(5, 8))
	check(await _until(func() -> bool: return _markers().size() == 3), "later lines still arrive")
	await _wait(0.3)
	check_eq(_markers(), [5, 6, 7], "the 413 batch is dropped, not resent")
	var rejected := _logged("devlink.upload_rejected")
	if not rejected.is_empty():
		check_eq(rejected[0]["status"], 413.0, "status")
		check_eq(rejected[0]["dropped_lines"], 5.0, "the whole batch")
		check_eq(rejected[0]["body"], "body is 2000000 bytes, at most 1048576", "the server's reason")
	await _finish()


func test_failed_upload_kept_with_backoff() -> void:
	_prepare()
	_append(CURRENT, _marker_lines(0, 5))
	await _start(0, FakeDevlinkServer.Mode.NORMAL, func(_unused: OfsDevlink, server: FakeDevlinkServer) -> void:
		server.queue_reply("/logs", 503)
		server.queue_reply("/logs", 503))
	check(await _until(func() -> bool: return _markers().size() >= 5), "lines arrive after two failures")
	var posts := _server.requests_to("/logs")
	check(posts.size() >= 3, "retried")
	if posts.size() >= 3:
		check(posts[1]["time"] - posts[0]["time"] >= 0.09, "1 s backoff (scaled)")
		check(posts[2]["time"] - posts[1]["time"] >= 0.19, "2 s backoff (scaled)")
		check_eq(posts[0]["body"], posts[2]["body"], "the same batch is sent again")
	check_eq(_markers(), range(5), "kept lines keep their order and arrive once")
	# Both failures are over by now, so this counts what was logged for them.
	var failures := _logged("devlink.upload_failed")
	check_eq(failures.size(), 1, "the second failure within 30 s is not logged")
	if not failures.is_empty():
		check_eq(failures[0]["status"], 503.0, "status")
		check_eq(failures[0]["attempt"], 1.0, "attempt")
		check_eq(failures[0]["queued_lines"], 5.0, "queued_lines counts the waiting lines")
	check(not _statuses.has("lost"), "two answered failures don't lose the link")
	await _finish()


func test_backoff_capped_at_30_s() -> void:
	var scale := 0.02
	_prepare()
	_append(CURRENT, _marker_lines(0, 1))
	# 429 is retried like a 5xx but, being an answer, never loses the link.
	var seven_429s := func(_unused: OfsDevlink, server: FakeDevlinkServer) -> void:
		for _i in 7:
			server.queue_reply("/logs", 429)
	await _start(0, FakeDevlinkServer.Mode.NORMAL, seven_429s, scale)
	check(await _until(func() -> bool: return _markers().size() == 1, 6.0), "delivered after seven failures")
	var posts := _server.requests_to("/logs")
	check_eq(posts.size(), 8, "seven retries")
	if posts.size() == 8:
		var gaps: Array[float] = []
		for i in 7:
			gaps.append(float(posts[i + 1]["time"]) - float(posts[i]["time"]))
		check(gaps[4] >= 16.0 * scale - 0.02, "16 s before the sixth try: %s" % [gaps])
		check(gaps[5] >= 30.0 * scale - 0.02 and gaps[6] >= 30.0 * scale - 0.02, "30 s gaps: %s" % [gaps])
		check(gaps[6] < 45.0 * scale, "capped at 30 s, not 64: %s" % [gaps])
	await _finish()


func test_server_down_keeps_lines_then_delivers() -> void:
	await _start()
	await _until(func() -> bool: return _link.get_status() == "connected")
	_server.mode = FakeDevlinkServer.Mode.CLOSE
	_append(CURRENT, _marker_lines(0, 20))
	check(await _until(func() -> bool: return _statuses.has("lost")), "lost")
	_server.mode = FakeDevlinkServer.Mode.NORMAL
	check(await _until(func() -> bool: return _markers().size() == 20, 8.0), "delivered after reconnecting")
	check_eq(_markers(), range(20), "every line delivered once, in order")
	var failures := _logged("devlink.upload_failed")
	check(not failures.is_empty() and failures[0]["status"] == 0.0, "upload_failed with status 0")
	var lost := _logged("devlink.lost")
	check_eq(lost.size(), 1, "devlink.lost")
	if not lost.is_empty():
		check(lost[0].has("reason"), "lost reason")
		check_eq(lost[0]["queued_lines"], 20.0, "lost counts the lines waiting")
	await _finish()


func test_earlier_session_uploaded_and_reported() -> void:
	_prepare()
	_append(EARLIER, _marker_lines(0, 5))
	_append(CURRENT, _marker_lines(5, 8))
	await _start()
	check(await _until(func() -> bool: return _markers().size() == 8), "both sessions arrive")
	var sorted := _markers()
	sorted.sort()
	check_eq(sorted, range(8), "every line once")
	var backlog := _logged("devlink.backlog")
	check_eq(backlog.size(), 1, "devlink.backlog on connect")
	if not backlog.is_empty():
		check_eq(backlog[0]["sessions"], 1.0, "one earlier session")
		check_eq(backlog[0]["bytes"], float(_size(EARLIER)), "its bytes")
	var offsets := _saved_offsets()
	check_eq(offsets.get(EARLIER, -1.0), float(_size(EARLIER)), "earlier session acknowledged to its end")
	check_eq(offsets.get(CURRENT, -1.0), float(_size(CURRENT)), "current session acknowledged to its end")
	await _finish()


func test_restart_resumes_from_saved_offsets() -> void:
	_prepare()
	# A session the previous run had uploaded up to line 4.
	var earlier := _marker_lines(0, 10)
	_append(EARLIER, earlier)
	var uploaded := 0
	for i in 4:
		uploaded += earlier[i].to_utf8_buffer().size() + 1
	_save_offsets({EARLIER: uploaded})
	_append(CURRENT, _marker_lines(100, 103))
	await _start()
	check(await _until(func() -> bool: return _markers().size() == 9), "the rest arrives")
	var sorted := _markers()
	sorted.sort()
	check_eq(sorted, [4, 5, 6, 7, 8, 9, 100, 101, 102], "sent from the saved offset")
	# The next run, part way through a file: picks up where this one stopped.
	_link.queue_free()
	await tree.process_frame
	_append(CURRENT, _marker_lines(103, 105))
	_link = _new_link(SCALE)
	_link.set_test_target("127.0.0.1", _server.port)
	tree.root.add_child(_link)
	check(await _until(func() -> bool: return _markers().size() == 11), "the new lines arrive")
	await _wait(0.3)
	sorted = _markers()
	sorted.sort()
	check_eq(sorted, [4, 5, 6, 7, 8, 9, 100, 101, 102, 103, 104], "nothing sent twice across the restart")
	await _finish()


func test_unfinished_last_line_waits() -> void:
	_prepare()
	var lines := _marker_lines(0, 2)
	_append(CURRENT, lines)
	var half := _marker_lines(2, 3)[0]
	_append_raw(CURRENT, half.substr(0, 20))
	await _start()
	check(await _until(func() -> bool: return _markers().size() == 2), "complete lines arrive")
	await _wait(0.3)
	_append_raw(CURRENT, half.substr(20) + "\n")
	check(await _until(func() -> bool: return _markers().size() == 3), "the line arrives once finished")
	check_eq(_markers(), [0, 1, 2], "whole, once")
	await _finish()


func test_bonjour_found_connects() -> void:
	await _start_bonjour()
	_link.push_test_bonjour({"kind": "found", "name": "PC", "host": "127.0.0.1", "port": _server.port})
	check(await _until(func() -> bool: return _link.get_status() == "connected"), "connects to the found PC")
	var discovered := _logged("devlink.discovered")
	check_eq(discovered.size(), 1, "discovered once")
	if not discovered.is_empty():
		check_eq(discovered[0]["via"], "bonjour", "via")
		check_eq(discovered[0]["port"], float(_server.port), "port")
	await _finish()


func test_resolve_failures_logged_once_per_error() -> void:
	await _start_bonjour()
	var waiting := {"kind": "resolve_failed", "name": "PC", "error": "resolving is waiting: denied (-65570)", "denied": true}
	_link.push_test_bonjour(waiting)
	_link.push_test_bonjour(waiting)
	_link.push_test_bonjour({"kind": "resolve_failed", "name": "PC", "error": "resolved to an IPv6 address only"})
	await _wait(0.1)
	var failures := _logged("devlink.resolve_failed")
	check_eq(failures.size(), 2, "the repeat within 30 s is not logged: %s" % [failures])
	if failures.size() == 2:
		check_eq(failures[0], {"name": "PC", "error": waiting["error"], "local_network_denied": true}, "fields")
		check_eq(failures[1]["local_network_denied"], false, "not a permission problem")
	check_eq(_link.get_status(), "searching", "still searching")
	await _finish()


func test_browse_failure_logged_and_browsing_restarted() -> void:
	await _start_bonjour(0.01)
	check_eq(_link.get_test_bonjour_starts(), 1, "browsing started")
	_link.push_test_bonjour({"kind": "browse_failed", "error": "Bonjour browsing failed: x (-65563)"})
	check(await _until(func() -> bool: return _link.get_test_bonjour_starts() == 2), "restarted after 30 s (scaled)")
	var failures := _logged("devlink.browse_failed")
	check_eq(failures.size(), 1, "browse_failed")
	if not failures.is_empty():
		check_eq(failures[0]["local_network_denied"], false, "local_network_denied")
	await _finish()


func test_no_link_argument_turns_the_link_off() -> void:
	check(not LinkScript.enabled_by(PackedStringArray(["--no-link"])), "--no-link")
	check(not LinkScript.enabled_by(PackedStringArray(["--quit-after=3", "--no-link"])), "among other arguments")
	check(LinkScript.enabled_by(PackedStringArray()), "on without arguments")
	check(LinkScript.enabled_by(PackedStringArray(["--no-linking"])), "only the exact argument")
	_prepare()
	var link: OfsDevlink = LinkScript.new()
	link.set_test_log_dir(_dir, CURRENT)
	link.set_test_target("127.0.0.1", 9)
	tree.root.add_child(link)
	check_eq(link.link_enabled, LinkScript.enabled_by(OS.get_cmdline_user_args()), "_enter_tree reads the arguments")
	link.free()
	await _finish()


func test_link_autoload_runs_and_feeds_params() -> void:
	# The test runner switches the autoload off; this test turns it on for itself.
	var was_enabled := Link.link_enabled
	Link.link_enabled = true
	check(Link.get_status() in ["searching", "connected"], "link.gd keeps OfsDevlink's _ready: " + Link.get_status())
	check(Link.params_received.is_connected(Params.apply_remote), "params_received -> Params.apply_remote")
	Link.link_enabled = was_enabled
	if not was_enabled:
		check_eq(Link.get_status(), "off", "switched back off")


func test_thermal_state_unknown_on_desktop() -> void:
	check_eq(OfsDevice.thermal_state(), "unknown", "thermal state")


func test_memory_footprint() -> void:
	var footprint := OfsDevice.memory_footprint_mb()
	if OS.has_feature("windows"):
		check(footprint > 10.0 and footprint < 64000.0, "private bytes in MB: %f" % footprint)
	else:
		check(footprint >= 0.0, "footprint: %f" % footprint)


func _prepare() -> void:
	if not _dir.is_empty():
		return
	_link_was_enabled = Link.link_enabled
	# The Link autoload logs too; it must not add lines to the ones these tests count.
	Link.link_enabled = false
	_dir = ProjectSettings.globalize_path("user://test_devlink/%d-%d" % [Time.get_ticks_usec(), randi()])
	DirAccess.make_dir_recursive_absolute(_dir)
	OfsLog.flush()
	_log_start = FileAccess.get_file_as_bytes(OfsLog.file_path()).size()


func _start(port: int = 0, mode: FakeDevlinkServer.Mode = FakeDevlinkServer.Mode.NORMAL,
		configure: Callable = Callable(), scale: float = SCALE) -> void:
	_prepare()
	_server = FakeDevlinkServer.new()
	check_eq(_server.listen(), OK, "fake server listens")
	_server.mode = mode
	tree.root.add_child(_server)
	_link = _new_link(scale)
	_link.set_test_target("127.0.0.1", port if port != 0 else _server.port)
	if configure.is_valid():
		configure.call(_link, _server)
	tree.root.add_child(_link)
	await tree.process_frame


func _start_bonjour(scale: float = SCALE) -> void:
	_prepare()
	_server = FakeDevlinkServer.new()
	check_eq(_server.listen(), OK, "fake server listens")
	tree.root.add_child(_server)
	_link = _new_link(scale)
	_link.set_test_bonjour(true)
	tree.root.add_child(_link)
	await tree.process_frame


func _new_link(scale: float) -> OfsDevlink:
	var link := OfsDevlink.new()
	link.set_test_time_scale(scale)
	link.set_test_log_dir(_dir, CURRENT)
	link.status_changed.connect(func(status: String) -> void:
		_statuses.append(status)
		_status_times.append(Time.get_ticks_msec() / 1000.0))
	link.params_received.connect(func(json_text: String, hash: String) -> void:
		_params.append(PackedStringArray([json_text, hash])))
	return link


func _finish() -> void:
	if _link != null:
		_link.queue_free()
	if _server != null:
		_server.queue_free()
	await tree.process_frame
	Link.link_enabled = _link_was_enabled
	for file in DirAccess.get_files_at(_dir):
		DirAccess.remove_absolute(_dir.path_join(file))
	DirAccess.remove_absolute(_dir)


func _until(condition: Callable, seconds: float = 4.0) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000.0)
	while not condition.call():
		if Time.get_ticks_msec() > deadline:
			return false
		await tree.process_frame
	return true


func _wait(seconds: float) -> void:
	await tree.create_timer(seconds).timeout


## Lines shaped like OfsLog's, each carrying its number in data.args[0].
func _marker_lines(from: int, to: int, filler: String = "") -> PackedStringArray:
	var lines := PackedStringArray()
	for i in range(from, to):
		var data := {"command": MARKER, "args": [i, filler] if not filler.is_empty() else [i], "ok": true}
		var line := {"ts": Time.get_unix_time_from_system(), "session": CURRENT.get_basename(), "build": "dev",
				"platform": "windows", "subsystem": "harness", "level": "info", "event": "harness.command",
				"data": data}
		lines.append(JSON.stringify(line, "", false))
	return lines


func _append(file_name: String, lines: PackedStringArray) -> void:
	_append_raw(file_name, "\n".join(lines) + "\n")


func _append_raw(file_name: String, text: String) -> void:
	var path := _dir.path_join(file_name)
	var file := FileAccess.open(path, FileAccess.READ_WRITE if FileAccess.file_exists(path) else FileAccess.WRITE)
	file.seek_end()
	file.store_string(text)
	file.close()


func _size(file_name: String) -> int:
	return FileAccess.get_file_as_bytes(_dir.path_join(file_name)).size()


func _save_offsets(offsets: Dictionary) -> void:
	var file := FileAccess.open(_dir.path_join("upload_state.json"), FileAccess.WRITE)
	file.store_string(JSON.stringify({"v": 1, "offsets": offsets}))
	file.close()


func _saved_offsets() -> Dictionary:
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(_dir.path_join("upload_state.json")))
	if parsed is Dictionary and (parsed as Dictionary).get("offsets") is Dictionary:
		return (parsed as Dictionary)["offsets"]
	return {}


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


## The data of what this test's link logged, read from the real session file.
func _logged(event: String) -> Array[Dictionary]:
	var found: Array[Dictionary] = []
	for line in _logged_lines(event):
		found.append(line.get("data", {}))
	return found


func _logged_times(event: String) -> Array[float]:
	var times: Array[float] = []
	for line in _logged_lines(event):
		times.append(float(line["ts"]))
	return times


func _logged_lines(event: String) -> Array[Dictionary]:
	OfsLog.flush()
	var bytes := FileAccess.get_file_as_bytes(OfsLog.file_path())
	var found: Array[Dictionary] = []
	for line in bytes.slice(_log_start).get_string_from_utf8().split("\n", false):
		if not line.contains('"event":"%s"' % event):
			continue
		var parsed: Variant = JSON.parse_string(line)
		if parsed is Dictionary:
			found.append(parsed)
	return found
