extends Node
## Params autoload: the physics parameters in use, embedded or live from the PC (spec
## 7.10).

signal changed(hash: String)

const EMBEDDED_PATH := "res://data/table_params.json"

## (json_text, key) -> json_text[key] as JSON text with integers intact, "" when there is none.
## Tests swap it out; OfsPhysics.member_json is the real one.
var extract_member: Callable = func(json_text: String, key: String) -> String:
	return OfsPhysics.member_json(json_text, key)

## (table_params_text, hash) -> true once physics runs on them; false when it refuses them and
## keeps what it had. Tests swap it out; Physics.try_params is the real one.
var configure_physics: Callable = func(text: String, params_hash: String) -> bool:
	return Physics.try_params(text, params_hash)

var _table_params: Dictionary = {}
var _hash := ""
var _text := ""
var _pending_params: Dictionary = {}
var _pending_hash := ""
var _pending_text := ""


func _ready() -> void:
	var text := FileAccess.get_file_as_string(EMBEDDED_PATH)
	var parsed: Variant = JSON.parse_string(text) if not text.is_empty() else null
	if not parsed is Dictionary:
		_warn("params: %s is missing or not a JSON object" % EMBEDDED_PATH)
		return
	_table_params = parsed
	_text = text
	# Hash the file's own text, not a re-serialised Dictionary: Godot turns integers into
	# floats, which would change the canonical bytes and no longer match devlink.py's hash.
	_hash = OfsLog.content_hash('{"table_params":%s}' % text)
	Log.info("params", "params.loaded", {"source": "embedded", "hash": _hash})
	Link.applied_params_hash = _hash


func current() -> Dictionary:
	return _table_params


func current_hash() -> String:
	return _hash


## table_params.json's text as in use, integers intact (OfsPhysics.configure takes it).
func current_text() -> String:
	return _text


## The hash of parameters received but not yet applied; "" when none are waiting.
func pending_hash() -> String:
	return _pending_hash


## Applies waiting parameters at a shot boundary (spec 16.10: at the next shot, never mid-shot),
## logging params.applied with the shot's id and emitting changed. Physics takes them first: a
## set it refuses is dropped and logged as params.refused, and everything stays on the parameters
## in use, Link.applied_params_hash included, so the PC sees they never applied. False when
## nothing was applied.
func commit_pending(shot_id: String) -> bool:
	if _pending_hash.is_empty():
		return false
	var params := _pending_params
	var text := _pending_text
	var new_hash := _pending_hash
	_pending_params = {}
	_pending_text = ""
	_pending_hash = ""
	if not configure_physics.call(text, new_hash):
		Log.warn("params", "params.refused", {"hash": new_hash, "current_hash": _hash, "shot_id": shot_id})
		return false
	var old_hash := _hash
	_table_params = params
	_text = text
	_hash = new_hash
	Link.applied_params_hash = _hash
	Log.info("params", "params.applied", {"source": "devlink", "old_hash": old_hash, "new_hash": _hash, "shot_id": shot_id})
	changed.emit(_hash)
	return true


## json_text is the "files" object of a /params answer; hash is the server's hash of it. The set
## waits for commit_pending; a newer one replaces it.
func apply_remote(json_text: String, hash: String) -> void:
	if hash == _hash or hash == _pending_hash:
		return
	var text: String = extract_member.call(json_text, "table_params")
	# A JSON instance, unlike JSON.parse_string, doesn't push an engine error on bad input.
	var json := JSON.new()
	if text.is_empty():
		if json.parse(json_text) != OK or not json.data is Dictionary:
			_warn("params: devlink sent parameters that are not a JSON object (hash %s)" % hash)
		else:
			_warn("params: devlink parameters have no table_params object (hash %s)" % hash)
		return
	if json.parse(text) != OK or not json.data is Dictionary:
		_warn("params: devlink parameters have no table_params object (hash %s)" % hash)
		return
	_pending_params = json.data
	_pending_text = text
	_pending_hash = hash
	Log.info("params", "params.pending", {"hash": hash, "current_hash": _hash})


func _warn(message: String) -> void:
	Log.warn("app", "app.error", {"kind": "message", "message": message})
