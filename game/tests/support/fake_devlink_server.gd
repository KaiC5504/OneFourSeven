class_name FakeDevlinkServer
extends Node
## Stands in for tools/devlink.py in the link tests: a tiny HTTP/1.1 server on 127.0.0.1,
## polled from _process, that records every request it reads.

enum Mode { NORMAL, CLOSE, HANG }

## NORMAL answers; CLOSE reads each request and hangs up without answering; HANG reads it
## and never answers.
var mode := Mode.NORMAL
var port := 0
var server_version := 1
var params_hash := "0123456789abcdef"
var params_files: Dictionary = {"table_params": {"schema_version": 1, "values": {}}}
## When true /params answers 200 even if If-None-Match matches.
var ignore_if_none_match := false
## Queued answers per path, each {"status": int, "body": String}, used up before the normal
## answer.
var replies: Dictionary[String, Array] = {}
## When not 0, every request gets this status and fixed_body.
var fixed_status := 0
var fixed_body := ""
## Every request read: {"method", "path", "headers" (lower-case keys), "body", "time"}, plus
## "status" once answered.
var requests: Array[Dictionary] = []

var _server := TCPServer.new()
var _clients: Array[Dictionary] = []


func listen() -> Error:
	var err := _server.listen(0, "127.0.0.1")
	if err == OK:
		port = _server.get_local_port()
	return err


func queue_reply(path: String, status: int, body: String = "") -> void:
	if not replies.has(path):
		replies[path] = []
	replies[path].append({"status": status, "body": body})


func _exit_tree() -> void:
	for client in _clients:
		(client["peer"] as StreamPeerTCP).disconnect_from_host()
	_clients.clear()
	_server.stop()


func requests_to(path: String) -> Array[Dictionary]:
	var matching: Array[Dictionary] = []
	for request in requests:
		if request["path"] == path:
			matching.append(request)
	return matching


func _process(_delta: float) -> void:
	while _server.is_connection_available():
		_clients.append({"peer": _server.take_connection(), "buffer": PackedByteArray(), "done": false})
	var open: Array[Dictionary] = []
	for client in _clients:
		var peer: StreamPeerTCP = client["peer"]
		peer.poll()
		if peer.get_status() != StreamPeerTCP.STATUS_CONNECTED:
			continue
		if not client["done"]:
			var available := peer.get_available_bytes()
			if available > 0:
				var chunk: Array = peer.get_data(available)
				var buffer: PackedByteArray = client["buffer"]
				buffer.append_array(chunk[1])
				client["buffer"] = buffer
			var request := _parse(client["buffer"])
			if not request.is_empty():
				client["done"] = true
				requests.append(request)
				if mode == Mode.NORMAL:
					_answer(peer, request)
				if mode != Mode.HANG:
					peer.disconnect_from_host()
					continue
		open.append(client)
	_clients = open


func _parse(buffer: PackedByteArray) -> Dictionary:
	var head_end := _find_head_end(buffer)
	if head_end < 0:
		return {}
	var lines := buffer.slice(0, head_end).get_string_from_ascii().split("\r\n")
	var parts := lines[0].split(" ")
	var headers := {}
	for i in range(1, lines.size()):
		var colon := lines[i].find(":")
		if colon > 0:
			headers[lines[i].substr(0, colon).strip_edges().to_lower()] = lines[i].substr(colon + 1).strip_edges()
	var length := int(headers.get("content-length", "0"))
	var body_start := head_end + 4
	if buffer.size() < body_start + length:
		return {}
	return {
		"method": parts[0],
		"path": parts[1] if parts.size() > 1 else "",
		"headers": headers,
		"body": buffer.slice(body_start, body_start + length).get_string_from_utf8(),
		"time": Time.get_ticks_msec() / 1000.0,
	}


# Only the start is searched, so a 1 MiB body isn't decoded again on every poll.
func _find_head_end(buffer: PackedByteArray) -> int:
	return buffer.slice(0, 16384).get_string_from_ascii().find("\r\n\r\n")


func _answer(peer: StreamPeerTCP, request: Dictionary) -> void:
	var path: String = request["path"]
	if fixed_status != 0:
		_send(peer, request, fixed_status, fixed_body)
		return
	var queued: Array = replies.get(path, [])
	if not queued.is_empty():
		var reply: Dictionary = queued.pop_front()
		_send(peer, request, reply["status"], reply["body"])
		return
	if path == "/health":
		_send(peer, request, 200, JSON.stringify({"ok": true, "v": server_version}))
	elif path == "/logs":
		_send(peer, request, 204, "")
	elif path == "/params":
		var headers: Dictionary = request["headers"]
		if not ignore_if_none_match and headers.get("if-none-match", "") == '"%s"' % params_hash:
			_send(peer, request, 304, "")
		else:
			_send(peer, request, 200, JSON.stringify({"hash": params_hash, "files": params_files}))
	else:
		_send(peer, request, 404, JSON.stringify({"error": "not found"}))


func _send(peer: StreamPeerTCP, request: Dictionary, status: int, body: String) -> void:
	request["status"] = status
	var bytes := body.to_utf8_buffer()
	var head := "HTTP/1.1 %d X\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % [status, bytes.size()]
	peer.put_data(head.to_ascii_buffer())
	if not bytes.is_empty():
		peer.put_data(bytes)
