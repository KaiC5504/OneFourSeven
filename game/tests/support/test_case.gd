class_name TestCase
extends RefCounted
## Base for every test file under res://tests. A test is a method named test_* taking no
## arguments. Failures are recorded, not thrown, so one failing check doesn't hide the rest.

var failures: PackedStringArray = []
## The running SceneTree, for tests that need nodes in the tree or frames to pass.
var tree: SceneTree


func check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)


func check_eq(actual: Variant, expected: Variant, message: String = "") -> void:
	if actual != expected:
		failures.append("%s: expected %s, got %s" % [message, var_to_str(expected), var_to_str(actual)])


func check_near(actual: float, expected: float, tolerance: float, message: String = "") -> void:
	if absf(actual - expected) > tolerance:
		failures.append("%s: expected %f +- %f, got %f" % [message, expected, tolerance, actual])
