extends Node
## Log autoload: GDScript's logging API over OfsLog (spec 16.10). Agent C owns this file.
##
## Every other script logs through these four functions. Event names and their fields
## must be in core/data/log_schema.json.


func debug(subsystem: String, event: String, data: Dictionary = {}) -> void:
	pass


func info(subsystem: String, event: String, data: Dictionary = {}) -> void:
	pass


func warn(subsystem: String, event: String, data: Dictionary = {}) -> void:
	pass


func error(subsystem: String, event: String, data: Dictionary = {}) -> void:
	pass


func session() -> String:
	return ""
