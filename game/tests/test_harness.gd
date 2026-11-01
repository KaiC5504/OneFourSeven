extends TestCase

const HarnessScript := preload("res://scripts/autoload/harness.gd")


func test_parses_user_args() -> void:
	var p := HarnessScript.parse_args(
		PackedStringArray(["--screenshot=shots/a.png", "--dump-state=C:/tmp/s.json", "--quit-after=4", "--demo-screen=table"]),
		PackedStringArray())
	check_eq(p["screenshot"], "shots/a.png", "screenshot")
	check_eq(p["dump_state"], "C:/tmp/s.json", "dump-state")
	check_eq(p["quit_after"], 4.0, "quit-after")
	check_eq(p["demo_screen"], "table", "demo-screen")
	check_eq(p["invalid"].size(), 0, "no invalid args")


func test_defaults_when_no_args() -> void:
	var p := HarnessScript.parse_args(PackedStringArray(), PackedStringArray())
	check_eq(p["screenshot"], "", "screenshot")
	check_eq(p["quit_after"], -1.0, "quit-after off")
	check_eq(p["demo_screen"], "", "demo-screen")


func test_ios_style_demo_screen() -> void:
	var p := HarnessScript.parse_args(PackedStringArray(), PackedStringArray(["--some-engine-flag", "-DemoScreen", "table"]))
	check_eq(p["demo_screen"], "table", "from launch args")
	p = HarnessScript.parse_args(PackedStringArray(["-DemoScreen", "table"]), PackedStringArray())
	check_eq(p["demo_screen"], "table", "from user args")
	check_eq(p["invalid"].size(), 0, "the value is not an invalid arg")


func test_flags_bad_and_unknown_args() -> void:
	var p := HarnessScript.parse_args(PackedStringArray(["--quit-after=soon", "--quit-after=-2", "--screenshot=", "--frobnicate=1"]), PackedStringArray())
	check_eq(p["quit_after"], -1.0, "bad quit-after ignored")
	check_eq(p["invalid"], PackedStringArray(["--quit-after=soon", "--quit-after=-2", "--screenshot=", "--frobnicate=1"]), "invalid")


func test_quit_after_must_be_finite() -> void:
	var args := PackedStringArray(["--quit-after=1e400", "--quit-after=inf", "--quit-after=nan", "--quit-after=-0.5"])
	var p := HarnessScript.parse_args(args, PackedStringArray())
	check_eq(p["quit_after"], -1.0, "rejected")
	check_eq(p["invalid"], args, "all reported")
	check_eq(HarnessScript.parse_args(PackedStringArray(["--quit-after=0"]), PackedStringArray())["quit_after"], 0.0, "zero is allowed")


func test_demo_screen_without_a_value_keeps_the_next_flag() -> void:
	var p := HarnessScript.parse_args(PackedStringArray(["-DemoScreen", "--quit-after=3"]), PackedStringArray())
	check_eq(p["demo_screen"], "", "no demo screen")
	check_eq(p["quit_after"], 3.0, "--quit-after still read")
	check_eq(p["invalid"], PackedStringArray(["-DemoScreen"]), "-DemoScreen reported")
	p = HarnessScript.parse_args(PackedStringArray(["-DemoScreen"]), PackedStringArray(["-DemoScreen", "-v"]))
	check_eq(p["demo_screen"], "", "launch args: a flag is not a screen name")
	check_eq(p["invalid"], PackedStringArray(["-DemoScreen"]), "trailing -DemoScreen reported")


func test_repeated_flags_keep_the_last_value() -> void:
	var p := HarnessScript.parse_args(
		PackedStringArray(["--quit-after=3", "--screenshot=a.png", "--quit-after=5", "--quit-after=7", "-DemoScreen", "table", "--demo-screen=other"]),
		PackedStringArray())
	check_eq(p["quit_after"], 7.0, "last quit-after")
	check_eq(p["demo_screen"], "other", "last demo screen")
	check_eq(p["screenshot"], "a.png", "screenshot")
	check_eq(p["repeated"], PackedStringArray(["--quit-after", "--demo-screen"]), "repeated, each once")
	check_eq(p["invalid"].size(), 0, "nothing invalid")


func test_flags_for_other_autoloads_are_accepted() -> void:
	var p := HarnessScript.parse_args(PackedStringArray(["--no-link", "--quit-after=2"]), PackedStringArray())
	check_eq(p["invalid"].size(), 0, "--no-link is not reported")
	check_eq(p["quit_after"], 2.0, "quit-after")


func test_relative_paths_go_under_user() -> void:
	check_eq(HarnessScript.resolve_path("shots/a.png"), "user://shots/a.png", "relative")
	check_eq(HarnessScript.resolve_path("C:/tmp/a.png"), "C:/tmp/a.png", "windows absolute")
	check_eq(HarnessScript.resolve_path("user://a.png"), "user://a.png", "user://")


func test_state_lists_balls_in_core_coordinates() -> void:
	var scene := Node3D.new()
	var balls := Node3D.new()
	balls.name = "Balls"
	scene.add_child(balls)
	var pink := Node3D.new()
	pink.name = "pink"
	pink.set_meta("ball", "pink")
	pink.set_meta("index", 0)
	pink.position = TableSpace.core_to_godot(0.89225, 0.0, 0.02625)
	balls.add_child(pink)
	var state := HarnessScript.build_state(scene, "table", "s1", "searching", "abc")
	scene.free()
	check_eq(state["screen"], "table", "screen")
	check_eq(state["session"], "s1", "session")
	check_eq(state["link_status"], "searching", "link_status")
	check_eq(state["params_hash"], "abc", "params_hash")
	check_eq(state["balls"].size(), 1, "ball count")
	var ball: Dictionary = state["balls"][0]
	check_eq(ball["name"], "pink", "name")
	check_eq(ball["ball"], "pink", "ball")
	check_eq(ball["index"], 0, "index")
	check_near(ball["position"][0], 0.89225, 1e-6, "core x")
	check_near(ball["position"][1], 0.0, 1e-6, "core y")
	check_near(ball["position"][2], 0.02625, 1e-6, "height")
	check(JSON.parse_string(JSON.stringify(state)) is Dictionary, "state serialises to JSON")


func test_state_prefers_double_precision_core_xy() -> void:
	var scene := Node3D.new()
	var balls := Node3D.new()
	balls.name = "Balls"
	scene.add_child(balls)
	var cue := Node3D.new()
	cue.name = "cue"
	cue.set_meta("core_xy", [-1.0475, -0.146])
	cue.position = TableSpace.core_to_godot(-1.0475, -0.146, 0.02625)
	balls.add_child(cue)
	var state := HarnessScript.build_state(scene, "table", "", "off", "")
	scene.free()
	check_eq(state["balls"][0]["position"][0], -1.0475, "exact core x")
	check_eq(state["balls"][0]["position"][1], -0.146, "exact core y")
