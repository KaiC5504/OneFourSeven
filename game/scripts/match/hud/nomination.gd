class_name Nomination
extends Node3D
## Nomination (spec 8.7): which colour the striker declares. The game pre-selects the ball the aim
## line hits first; a tap on any eligible ball changes it; the declared ball wears a ring on the
## cloth. This node is the ring; the static functions are the choosing, with no state.
##
## The ring is a broad band in the declared ball's colour, well outside the ball: the aim guide's
## ghost-ball ring is a thin line one ball radius round, white or red, so the two never read as
## one. The free table's edit selection is gold.

const Kit := preload("res://scripts/match/hud/hud_kit.gd")
## The balls' colours, lifted to show on the cloth.
const COLOURS := {
	"red": Color(0.92, 0.16, 0.12),
	"yellow": Color(1.0, 0.85, 0.15),
	"green": Color(0.45, 0.95, 0.5),
	"brown": Color(0.6, 0.33, 0.13),
	"blue": Color(0.25, 0.47, 1.0),
	"pink": Color(1.0, 0.58, 0.74),
	"black": Color(0.03, 0.03, 0.03),
}
## When no ball is named.
const NEUTRAL := Color(0.94, 0.97, 0.95)
## The balls too dark to show on the cloth, edged inside and out in a lighter shade of their own:
## not white, which would read as the ghost ring.
const RIMS := {
	"brown": Color(0.88, 0.66, 0.42),
	"black": Color(0.6, 0.6, 0.6),
}
## In ball radii.
const INNER := 1.55
const OUTER := 1.95
const RIM := 0.14
## Metres above the cloth, clear of its surface.
const HEIGHT := 0.002
const WHO := "Nomination: a ball"

var _ring := MeshInstance3D.new()
var _rims: Array[MeshInstance3D] = [MeshInstance3D.new(), MeshInstance3D.new()]


func _init() -> void:
	var r := float(OfsTable.dimensions().get("ball_radius", BallsView.RADIUS_FALLBACK))
	_ring.name = "Ring"
	_flat_ring(_ring, r * INNER, r * OUTER, NEUTRAL)
	_rims[0].name = "RimInside"
	_flat_ring(_rims[0], r * (INNER - RIM), r * INNER, NEUTRAL)
	_rims[1].name = "RimOutside"
	_flat_ring(_rims[1], r * OUTER, r * (OUTER + RIM), NEUTRAL)


## Rings the ball at `xy` (core metres) in the colour of `ball` ("blue"; "" for no colour); null
## hides the ring.
func mark(xy: Variant, ball: String = "") -> void:
	var shown := xy is Vector2
	if shown:
		var at := TableSpace.core_to_godot((xy as Vector2).x, (xy as Vector2).y, HEIGHT)
		for ring: MeshInstance3D in [_ring, _rims[0], _rims[1]]:
			if ring.position != at:
				ring.position = at
		if not ball.is_empty() and not COLOURS.has(ball):
			Kit.problem("Nomination: no colour for the ball \"%s\"" % ball)
		_paint(_ring, COLOURS.get(ball, NEUTRAL))
		if RIMS.has(ball):
			for rim in _rims:
				_paint(rim, RIMS[ball])
	var rimmed := shown and RIMS.has(ball)
	if _ring.visible != shown:
		_ring.visible = shown
	for rim in _rims:
		if rim.visible != rimmed:
			rim.visible = rimmed


func is_marking() -> bool:
	return _ring.visible


static func _paint(ring: MeshInstance3D, colour: Color) -> void:
	var material := (ring.mesh as TorusMesh).material as StandardMaterial3D
	if material.albedo_color != colour:
		material.albedo_color = colour


# A torus flattened onto the cloth, so it reads as a ring round the ball from any height.
func _flat_ring(ring: MeshInstance3D, inner: float, outer: float, colour: Color) -> void:
	var mesh := TorusMesh.new()
	mesh.inner_radius = inner
	mesh.outer_radius = outer
	mesh.rings = 64
	mesh.ring_segments = 8
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = colour
	mesh.material = material
	ring.mesh = mesh
	ring.scale = Vector3(1.0, 0.1, 1.0)
	ring.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	ring.layers = Stage.MOVING_LAYER
	ring.visible = false
	add_child(ring)


## The place in `balls` of the first ball the cue ball would hit travelling along `aim_rad` from
## its place, taking both as circles of `radius`; -1 when it would hit none.
static func first_on_line(balls: Array[Dictionary], cue_ball: int, aim_rad: float, radius: float) -> int:
	if cue_ball < 0 or cue_ball >= balls.size() or not _placed(balls[cue_ball]):
		return -1
	# In doubles throughout: a Vector2 would round a ball's place to single precision.
	var from_x := float(balls[cue_ball]["x"])
	var from_y := float(balls[cue_ball]["y"])
	var along_x := cos(aim_rad)
	var along_y := sin(aim_rad)
	var reach := 2.0 * radius
	var first := -1
	var nearest := INF
	for i in balls.size():
		if i == cue_ball or not _placed(balls[i]):
			continue
		var to_x := float(balls[i]["x"]) - from_x
		var to_y := float(balls[i]["y"]) - from_y
		var ahead := to_x * along_x + to_y * along_y
		var aside := absf(to_x * along_y - to_y * along_x)
		if ahead <= 0.0 or aside >= reach:
			continue
		# How far the cue ball travels before the two touch.
		var travel := ahead - sqrt(reach * reach - aside * aside)
		if travel < nearest:
			nearest = travel
			first = i
	return first


## The colour to pre-select: the first ball on the aim line when it is one of `eligible` (colour
## names), else "" (the line points at no eligible ball).
static func preselect(balls: Array[Dictionary], cue_ball: int, aim_rad: float, eligible: Array[String],
		radius: float) -> String:
	return tapped(balls, first_on_line(balls, cue_ball, aim_rad, radius), eligible)


## The colour a tap chose: the ball at place `tapped` when it is one of `eligible`, else "".
static func tapped(balls: Array[Dictionary], tapped_place: int, eligible: Array[String]) -> String:
	if tapped_place < 0 or tapped_place >= balls.size():
		return ""
	var ball := Kit.text(balls[tapped_place], "ball", WHO)
	return ball if eligible.has(ball) else ""


## The place of the colour `ball` in `balls`; -1 when it is not on the table.
static func place_of(balls: Array[Dictionary], ball: String) -> int:
	for i in balls.size():
		if balls[i].get("ball") == ball:
			return i
	return -1


# Whether a ball has a place in core metres; logged once when it has none.
static func _placed(ball: Dictionary) -> bool:
	for key: String in ["x", "y"]:
		var value: Variant = ball.get(key)
		if not (value is float or value is int) or not is_finite(value):
			Kit.missing(WHO, key)
			return false
	return true
