extends OfsDevlink
## Link autoload: the phone link (docs/devlink-protocol.md).
##
## Defines no _ready or _process: a script's version would replace OfsDevlink's.


func _enter_tree() -> void:
	if not params_received.is_connected(Params.apply_remote):
		params_received.connect(Params.apply_remote)
