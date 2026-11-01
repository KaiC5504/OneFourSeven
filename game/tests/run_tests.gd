extends SceneTree
## Runs every res://tests/test_*.gd headlessly:
##   godot --headless --path game -s res://tests/run_tests.gd
## Exit code 0 only when every file loads and every test passes. A test fails when it records
## a failure, raises a GDScript error, or is still running after TEST_TIMEOUT_SECONDS.

const TEST_DIR := "res://tests"
const TestCaseScript := preload("res://tests/support/test_case.gd")
# The slowest devlink tests wait on several scaled timeouts; this is well past them.
const TEST_TIMEOUT_SECONDS := 30.0


## Collects GDScript errors (ERROR_TYPE_SCRIPT) so a test that crashes fails instead of
## passing with the checks it never reached. push_error is ERROR_TYPE_ERROR and is left alone,
## since tests use it on purpose. Godot calls loggers from any thread.
class ScriptErrors extends Logger:
	var _mutex := Mutex.new()
	var _errors := PackedStringArray()

	func _log_error(function: String, file: String, line: int, code: String, rationale: String,
			_editor_notify: bool, error_type: int, _script_backtraces: Array[ScriptBacktrace]) -> void:
		if error_type != ERROR_TYPE_SCRIPT:
			return
		_mutex.lock()
		_errors.append("%s:%d %s: %s" % [file, line, function, rationale if not rationale.is_empty() else code])
		_mutex.unlock()

	func take() -> PackedStringArray:
		_mutex.lock()
		var errors := _errors
		_errors = PackedStringArray()
		_mutex.unlock()
		return errors


var _script_errors := ScriptErrors.new()


# _initialize runs after the autoloads exist but before their _ready, so the link never
# starts: a test run must not talk to a devlink server running on this PC. The autoload
# is looked up by path because this script compiles before autoload names are registered.
func _initialize() -> void:
	var link := root.get_node_or_null("Link")
	if link == null:
		printerr("run_tests: no Link autoload to switch off")
	else:
		link.set("link_enabled", false)
	OS.add_logger(_script_errors)
	_run.call_deferred()


func _run() -> void:
	var files: Array[String] = []
	for file in DirAccess.get_files_at(TEST_DIR):
		if file.begins_with("test_") and file.ends_with(".gd"):
			files.append(file)
	files.sort()
	var loaded := 0
	var failed := 0
	var total := 0
	for file in files:
		var script := _load_test_script(file)
		if script == null:
			continue
		loaded += 1
		for method in script.get_script_method_list():
			var name: String = method["name"]
			if not name.begins_with("test_"):
				continue
			total += 1
			var failures := await _run_test(script, name)
			if failures.is_empty():
				print("pass  %s::%s" % [file, name])
			else:
				failed += 1
				print("FAIL  %s::%s" % [file, name])
				for failure in failures:
					print("      " + failure)
	print("loaded %d/%d test files" % [loaded, files.size()])
	print("%d/%d passed" % [total - failed, total])
	OS.remove_logger(_script_errors)
	quit(1 if failed > 0 or loaded < files.size() else 0)


func _load_test_script(file: String) -> GDScript:
	_script_errors.take()
	var script := load(TEST_DIR.path_join(file)) as GDScript
	var reason := ""
	if script == null:
		reason = "does not load"
	elif not script.can_instantiate():
		reason = "cannot be instantiated"
	elif not _extends_test_case(script):
		reason = "does not extend TestCase"
	var errors := _script_errors.take()
	if reason.is_empty() and errors.is_empty():
		return script
	print("FAIL  %s: %s" % [file, reason if not reason.is_empty() else "script error while loading"])
	for error in errors:
		print("      " + error)
	return null


func _extends_test_case(script: Script) -> bool:
	while script != null:
		if script == TestCaseScript:
			return true
		script = script.get_base_script()
	return false


func _run_test(script: GDScript, name: String) -> PackedStringArray:
	_script_errors.take()
	var case: TestCase = script.new()
	case.tree = self
	# A lambda can't assign the caller's locals, so completion goes through a container.
	var finished := [false]
	var body := func() -> void:
		await case.call(name)
		finished[0] = true
	body.call()
	var deadline := Time.get_ticks_msec() + int(TEST_TIMEOUT_SECONDS * 1000.0)
	while not finished[0] and Time.get_ticks_msec() < deadline:
		await process_frame
	var failures := case.failures.duplicate()
	if not finished[0]:
		failures.append("still running after %d s; abandoned" % int(TEST_TIMEOUT_SECONDS))
	for error in _script_errors.take():
		failures.append("script error: " + error)
	return failures
