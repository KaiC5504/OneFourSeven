class_name Stage
extends Node3D
## The scene around the table (spec 13.1): a plain, softly lit dark backdrop and one overhead
## table light with soft contact shadows. Children: Light (DirectionalLight3D), WorldEnvironment.


func setup() -> void:
	pass


## Fits the shadow range to what `camera` sees of the table; call when the camera moves.
func fit_shadows(_camera: Camera3D) -> void:
	pass
