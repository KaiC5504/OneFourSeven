extends Node
## Params autoload: the physics parameters in use, embedded or live from the PC (spec
## 7.10). Agent D owns this file.

signal changed(hash: String)

const EMBEDDED_PATH := "res://data/table_params.json"

var _table_params: Dictionary = {}
var _hash := ""


func _ready() -> void:
	var text := FileAccess.get_file_as_string(EMBEDDED_PATH)
	var parsed: Variant = JSON.parse_string(text) if not text.is_empty() else null
	if not parsed is Dictionary:
		_warn("params: %s is missing or not a JSON object" % EMBEDDED_PATH)
		return
	_table_params = parsed
	# Hash the file's own text, not a re-serialised Dictionary: Godot turns integers into
	# floats, which would change the canonical bytes and no longer match devlink.py's hash.
	_hash = OfsLog.content_hash('{"table_params":%s}' % text)
	Log.info("params", "params.loaded", {"source": "embedded", "hash": _hash})
	Link.applied_params_hash = _hash


func current() -> Dictionary:
	return _table_params


func current_hash() -> String:
	return _hash


## json_text is the "files" object of a /params answer; hash is the server's hash of it.
## M0 has no shots, so the new set applies at once.
func apply_remote(json_text: String, hash: String) -> void:
	# A JSON instance, unlike JSON.parse_string, doesn't push an engine error on bad input.
	var json := JSON.new()
	var parsed: Variant = json.data if json.parse(json_text) == OK else null
	if not parsed is Dictionary:
		_warn("params: devlink sent parameters that are not a JSON object (hash %s)" % hash)
		return
	var files: Dictionary = parsed
	if not files.get("table_params") is Dictionary:
		_warn("params: devlink parameters have no table_params object (hash %s)" % hash)
		return
	var old_hash := _hash
	_table_params = files["table_params"]
	_hash = hash
	Log.info("params", "params.applied", {"source": "devlink", "old_hash": old_hash, "new_hash": hash})
	Link.applied_params_hash = hash
	changed.emit(hash)


func _warn(message: String) -> void:
	Log.warn("app", "app.error", {"kind": "message", "message": message})
