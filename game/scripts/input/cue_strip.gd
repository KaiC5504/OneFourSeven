class_name CueStrip
extends RefCounted
## The power cue on the right edge, drawn for StrokeControl (Push) and PullControl (Pull and
## release): a cue standing on its butt, tip at the top, that slides down as the thumb pulls it
## back. Drawn in code, no assets.

## Where the tip rests, from the strip's top.
const TIP_REST := 26.0
const TIP_WIDTH := 13.0
const BUTT_WIDTH := 34.0
const TIP_CAP := 7.0
const FERRULE := 12.0
## Fractions of the cue's drawn length from the tip.
const BUTT_FROM := 0.62
const JOINT_AT := 0.6
## Above the rest the tip can go this far on a push's follow-through.
const MAX_THROUGH := 18.0

const SHAFT := Color(0.86, 0.75, 0.57)
const SHAFT_DARK := Color(0.66, 0.54, 0.38)
const BUTT := Color(0.2, 0.12, 0.08)
const BUTT_LIGHT := Color(0.34, 0.22, 0.14)
const JOINT := Color(0.78, 0.68, 0.42)
const FERRULE_COLOUR := Color(0.93, 0.92, 0.86)
const TIP_COLOUR := Color(0.25, 0.42, 0.62)
const REST_MARK := Color(1.0, 1.0, 1.0, 0.35)
const SHADOW := Color(0.0, 0.0, 0.0, 0.35)
const IDLE_ALPHA := 0.9


## The drawn tip's y in a strip `height` tall, pulled back `pull` canvas units (negative is past
## the rest, a push going through).
static func tip_y(height: float, pull: float) -> float:
	return TIP_REST + clampf(pull, -MAX_THROUGH, maxf(height * 0.75, 0.0))


static func draw(canvas: CanvasItem, size: Vector2, pull: float, pressed: bool) -> void:
	var x := size.x / 2.0
	var length := maxf(size.y - TIP_REST, 0.0)
	if length <= 0.0:
		return
	var top := tip_y(size.y, pull)
	var alpha := 1.0 if pressed else IDLE_ALPHA
	if pressed and absf(pull) > 1.0:
		for side: float in [-1.0, 1.0]:
			var from := Vector2(x + side * (TIP_WIDTH / 2.0 + 6.0), TIP_REST)
			canvas.draw_line(from, from + Vector2(side * 14.0, 0.0), REST_MARK, 2.0, true)
	var bottom := top + length
	var butt_top := top + length * BUTT_FROM
	_taper(canvas, x + 3.0, top + 3.0, bottom + 3.0, Color(SHADOW, SHADOW.a * alpha))
	_section(canvas, x, top, top, butt_top, length, SHAFT, SHAFT_DARK, alpha)
	_section(canvas, x, top, butt_top, bottom, length, BUTT_LIGHT, BUTT, alpha)
	var joint := top + length * JOINT_AT
	var joint_half := _half_width(JOINT_AT) + 0.5
	canvas.draw_rect(Rect2(x - joint_half, joint - 1.5, joint_half * 2.0, 3.0), Color(JOINT, alpha))
	var tip_half := TIP_WIDTH / 2.0
	canvas.draw_rect(Rect2(x - tip_half, top + TIP_CAP, tip_half * 2.0, FERRULE), Color(FERRULE_COLOUR, alpha))
	canvas.draw_rect(Rect2(x - tip_half + 0.5, top, tip_half * 2.0 - 1.0, TIP_CAP), Color(TIP_COLOUR, alpha))


static func _half_width(fraction: float) -> float:
	return lerpf(TIP_WIDTH, BUTT_WIDTH, clampf(fraction, 0.0, 1.0)) / 2.0


static func _taper(canvas: CanvasItem, x: float, top: float, bottom: float, colour: Color) -> void:
	var a := TIP_WIDTH / 2.0
	var b := BUTT_WIDTH / 2.0
	canvas.draw_colored_polygon(PackedVector2Array([Vector2(x - a, top), Vector2(x + a, top),
			Vector2(x + b, bottom), Vector2(x - b, bottom)]), colour)


## The length of the cue from y0 to y1 (its tip at `top`), lit from the left so it reads as round.
static func _section(canvas: CanvasItem, x: float, top: float, y0: float, y1: float, length: float,
		light: Color, dark: Color, alpha: float) -> void:
	var h0 := _half_width((y0 - top) / length)
	var h1 := _half_width((y1 - top) / length)
	var points := PackedVector2Array([Vector2(x - h0, y0), Vector2(x + h0, y0), Vector2(x + h1, y1), Vector2(x - h1, y1)])
	var colours := PackedColorArray([Color(light, alpha), Color(dark, alpha), Color(dark, alpha), Color(light, alpha)])
	canvas.draw_polygon(points, colours)
	# Polygons aren't antialiased; a near-vertical edge would show its pixel steps.
	canvas.draw_line(points[0], points[3], Color(light, alpha), 1.0, true)
	canvas.draw_line(points[1], points[2], Color(dark, alpha), 1.0, true)
