extends RefCounted
## The free table's layout (spec 8.9, KaiC's M2 phone session: the table is the screen) and the
## safe-area helpers it uses.

## Canvas units: from the safe edge, and between neighbours.
const EDGE := 16.0
const GAP := 12.0
## Its track is about 218 units, fine enough for small elevations.
const ELEVATION_HEIGHT := 300.0
const CUE_WIDTH := 120.0
const WHEEL_SIZE := Vector2(88.0, 340.0)


## The safe area's top-left corner in canvas units. The safe area is in screen pixels and
## covers the whole screen on desktop, so it is clipped to the window first.
static func safe_origin(safe_area: Rect2, window_rect: Rect2, canvas_size: Vector2) -> Vector2:
	return safe_rect(safe_area, window_rect, canvas_size).position


## The safe area in canvas units, clipped to the window as safe_origin does.
static func safe_rect(safe_area: Rect2, window_rect: Rect2, canvas_size: Vector2) -> Rect2:
	var inside := safe_area.intersection(window_rect)
	if not inside.has_area():
		inside = window_rect
	var scale := canvas_size / window_rect.size.max(Vector2.ONE)
	return Rect2((inside.position - window_rect.position) * scale, inside.size * scale)


## Where the free table's controls go inside `safe` (canvas units), every one at an edge and the
## table clear in the middle:
## - "bar": FreeTableBar's row (`bar_size`) at the top-left;
## - "spin": the spin ball at the bottom-left, and "elevation" right above it, as wide: the left
##   thumb sets where the tip meets the ball and how steep the cue is, the bridge hand's work;
## - "cue": the power cue, the full usable height of the right edge, for the right thumb, which
##   pushes or pulls straight down the strip without curling;
## - "wheel": the fine-tune wheel beside the cue, low, where the right thumb rests, so a last
##   nudge of the aim runs straight into the stroke;
## - "bottom": edit mode's row, along the bottom (the shot controls are hidden then).
## canvas, when given, lets the bottom controls use the margin the phone's home indicator leaves
## below the safe area, which keeps the spin ball below the green at the opening view.
static func layout(safe: Rect2, bar_size: Vector2, canvas := Vector2.ZERO) -> Dictionary:
	var top := safe.position.y + EDGE
	var below_safe := maxf(canvas.y - safe.end.y, 0.0)
	var bottom := safe.end.y - maxf(EDGE - below_safe, 0.0)
	var bar := Rect2((safe.position + Vector2(EDGE, EDGE)).round(), bar_size)
	var side := SpinWidget.SIZE
	var spin := Rect2(Vector2(safe.position.x + EDGE, bottom - side).round(), Vector2(side, side))
	var elevation_top := maxf(bar.end.y + GAP, spin.position.y - GAP - ELEVATION_HEIGHT)
	var elevation := Rect2(Vector2(spin.position.x, elevation_top).round(),
			Vector2(side, spin.position.y - GAP - elevation_top).round())
	var cue := Rect2(Vector2(safe.end.x - EDGE - CUE_WIDTH, top).round(), Vector2(CUE_WIDTH, bottom - top).round())
	var wheel := Rect2(Vector2(cue.position.x - GAP - WHEEL_SIZE.x, bottom - WHEEL_SIZE.y).round(), WHEEL_SIZE)
	var edit_row := Rect2(Vector2(safe.position.x + EDGE, top), Vector2(safe.size.x - 2.0 * EDGE, bottom - top))
	return {"bar": bar, "spin": spin, "elevation": elevation, "cue": cue, "wheel": wheel, "bottom": edit_row}
