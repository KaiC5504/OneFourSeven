extends Node
## Params autoload: the physics parameters in use, embedded or live from the PC (spec
## 7.10). Agent D owns this file.

signal changed(hash: String)


func current() -> Dictionary:
	return {}


func current_hash() -> String:
	return ""
