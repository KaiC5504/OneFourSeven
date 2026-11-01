extends OfsDevlink
## Link autoload: the phone link (docs/devlink-protocol.md).
##
## Defines no _ready or _process: a script's version would replace OfsDevlink's. Starts
## switched off when the game runs with the user argument --no-link (godot ... -- --no-link).


static func enabled_by(user_args: PackedStringArray) -> bool:
	return not user_args.has(Log.NO_LINK_ARG)


func _enter_tree() -> void:
	# Before OfsDevlink's _ready, which starts the link when it is enabled.
	if not enabled_by(OS.get_cmdline_user_args()):
		link_enabled = false
	if not params_received.is_connected(Params.apply_remote):
		params_received.connect(Params.apply_remote)
