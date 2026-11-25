extends TestCase
## PositionReader: the reader configured from the real files and asked on the real engine, and a
## reader that is refused, which answers nothing and says so once, so the game keeps to geometry.

var _log_start := 0


func test_configured_from_the_real_files_it_reads_and_thinks() -> void:
	var reader := PositionReader.new()
	_mark_log()
	check(reader.configure(), "configured")
	check(reader.is_ready(), "and ready")
	check(reader.engine is OfsMatch, "on an engine of its own")
	var balls := _easy_black()
	var read := reader.read(balls, 0, {"kind": "any_colour"})
	check(read.get("ball") == "black" and float(read.get("worth", 0.0)) > 20.0, "an easy black reads high: %s" % [read])
	check(reader.think(balls, 0, {"kind": "any_colour"}, 5), "thinks")
	var deadline := Time.get_ticks_msec() + 5000
	while Time.get_ticks_msec() < deadline and bool(reader.engine.reader_thinking()):
		reader.poll()
		await tree.process_frame
	var answer := reader.answer()
	check_eq(answer.get("ball"), "black", "and would pot the black: %s" % [answer])
	check_eq(reader.answer(), {}, "the answer is used once")
	check_eq(_logged("app.error").size(), 0, "nothing went wrong")


func test_refused_it_answers_nothing_and_logs_once() -> void:
	var engine := FakeReaderEngine.new()
	engine.refuse = true
	var reader := PositionReader.new()
	_mark_log()
	check(not reader.configure(engine), "refused")
	check(not reader.configure(engine), "and again")
	check(not reader.is_ready(), "not ready")
	var errors := _logged("app.error")
	check_eq(errors.size(), 1, "logged once")
	if errors.size() == 1:
		check(String(errors[0]["message"]).contains("the test refuses the reader")
				and String(errors[0]["message"]).contains("geometry"), "with why, and what the game does instead: %s" % errors[0])
	check_eq(reader.read(_easy_black(), 0, {"kind": "any_colour"}), {}, "no reading")
	check(not reader.think(_easy_black(), 0, {"kind": "any_colour"}, 1), "no thinking")
	check_eq(reader.answer(), {}, "no answer")
	check(engine.thought.is_empty() and engine.reads.is_empty(), "the engine is never asked")
	var unconfigured := PositionReader.new()
	check_eq(unconfigured.read(_easy_black(), 0, {"kind": "red"}), {}, "a reader never configured reads nothing")


func test_an_answer_not_there_in_time_is_dropped() -> void:
	var engine := FakeReaderEngine.new()
	engine.takes_before = -1
	var reader := PositionReader.new()
	check(reader.configure(engine), "configured on the fake")
	check(reader.think(_easy_black(), 0, {"kind": "any_colour"}, 1), "thinks")
	reader.poll()
	check_eq(reader.answer(), {}, "not ready at the return")
	check(not bool(engine.reader_thinking()) and engine.cancels == 1, "and the thinking is stopped")
	engine.takes_before = 2
	engine.answer = {"ok": true, "error": "", "ball": "black", "pocket": 3}
	reader.think(_easy_black(), 0, {"kind": "any_colour"}, 2)
	for i in 3:
		reader.poll()
	check_eq(reader.answer().get("ball"), "black", "taken while the shot played, used at the return")


func _easy_black() -> Array:
	var corner := Vector2(1.7, 0.81)
	var black := corner - Vector2(1.0, 1.0).normalized() * 0.3
	var cue := black - Vector2(1.0, 1.0).normalized() * 0.3
	return [{"ball": "cue", "index": 0, "x": cue.x, "y": cue.y}, {"ball": "black", "index": 0, "x": black.x, "y": black.y}]


func _mark_log() -> void:
	OfsLog.flush()
	_log_start = FileAccess.get_file_as_bytes(OfsLog.file_path()).size()


func _logged(event: String) -> Array[Dictionary]:
	OfsLog.flush()
	var text := FileAccess.get_file_as_bytes(OfsLog.file_path()).slice(_log_start).get_string_from_utf8()
	var found: Array[Dictionary] = []
	for line in text.split("\n", false):
		var parsed: Variant = JSON.parse_string(line)
		if parsed is Dictionary and (parsed as Dictionary).get("event") == event:
			found.append((parsed as Dictionary).get("data", {}))
	return found
