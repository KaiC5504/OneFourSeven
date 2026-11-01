extends SceneTree
## Runs every res://tests/test_*.gd headlessly:
##   godot --headless --path game -s res://tests/run_tests.gd
## Exit code 0 when all pass. Async tests may await; each gets up to 10 s.


func _init() -> void:
	_run.call_deferred()


func _run() -> void:
	var failed := 0
	var total := 0
	var files := DirAccess.get_files_at("res://tests")
	files.sort()
	for file in files:
		if not (file.begins_with("test_") and file.ends_with(".gd")):
			continue
		var script: GDScript = load("res://tests/" + file)
		for method in script.get_script_method_list():
			var name: String = method["name"]
			if not name.begins_with("test_"):
				continue
			total += 1
			var case: TestCase = script.new()
			case.tree = self
			await case.call(name)
			if case.failures.is_empty():
				print("pass  %s::%s" % [file, name])
			else:
				failed += 1
				print("FAIL  %s::%s" % [file, name])
				for failure in case.failures:
					print("      " + failure)
	print("%d/%d passed" % [total - failed, total])
	quit(1 if failed > 0 else 0)
