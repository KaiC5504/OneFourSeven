extends Label
## The "PC link" status line, top-left inside the display's safe area, and the safe-area helpers
## and layout the free table's controls use.

const MARGIN := Vector2(16.0, 10.0)
# Canvas units. The canvas is 880 tall, so on the 1320 px phone screen this is 36 px, 12 pt.
const FONT_SIZE := 24
# Apple's smallest legible text size.
const MIN_POINTS := 11.0
# iOS can report the safe area late and changes it on rotation without a resize, so it is
# re-read on a timer as well as on size and resume notifications.
const SAFE_AREA_POLL_SECONDS := 1.0
## The free table's layout, canvas units: from the safe edge, and between neighbours.
const EDGE := 16.0
const GAP := 12.0
const WHEEL_SIZE := Vector2(120.0, 380.0)
const STROKE_WIDTH := 200.0
const POWER_WIDTH := 160.0
const ELEVATION_SIZE := Vector2(120.0, 380.0)
const STATUS_TEXT := {
	"off": "off",
	"searching": "searching…",
	"connected": "connected",
	"lost": "lost",
}


static func text_for(status: String) -> String:
	return "PC link: " + String(STATUS_TEXT.get(status, status))


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


## Where the free table's controls go (spec 8.9) inside `safe` (canvas units), the table left
## clear in the middle. FreeTableBar centres itself on the left edge ("bar", from its size). The
## right thumb's column has the fine-tune wheel at the edge, where a thumb wheel sits on a real
## device, and the stroke area inside it, full height, where the thumb's pad pushes straight up
## without curling; the power bar takes the stroke area's place. The spin widget grows up and
## right from its corner ("spin_corner") at the bottom beside the bar, the elevation control next
## to it; the edit palette goes along the bottom between the bar and the right edge ("bottom").
static func layout(safe: Rect2, bar_size: Vector2) -> Dictionary:
	var bar := Rect2(Vector2(safe.position.x + FreeTableBar.EDGE_MARGIN,
			safe.get_center().y - bar_size.y / 2.0).round(), bar_size)
	var wheel := Rect2(Vector2(safe.end.x - EDGE - WHEEL_SIZE.x,
			safe.get_center().y - WHEEL_SIZE.y / 2.0).round(), WHEEL_SIZE)
	var stroke := Rect2(Vector2(wheel.position.x - GAP - STROKE_WIDTH, safe.position.y + EDGE).round(),
			Vector2(STROKE_WIDTH, safe.size.y - 2.0 * EDGE).round())
	var power := Rect2(stroke.position + Vector2((STROKE_WIDTH - POWER_WIDTH) / 2.0, 0.0),
			Vector2(POWER_WIDTH, stroke.size.y))
	var spin_corner := Vector2(bar.end.x + GAP, safe.end.y - EDGE).round()
	var elevation := Rect2(Vector2(spin_corner.x + SpinWidget.COLLAPSED_SIZE + GAP,
			spin_corner.y - ELEVATION_SIZE.y), ELEVATION_SIZE)
	var bottom := Rect2(Vector2(bar.end.x + GAP, safe.position.y + EDGE),
			Vector2(safe.end.x - EDGE - bar.end.x - GAP, safe.size.y - 2.0 * EDGE))
	return {"bar": bar, "wheel": wheel, "stroke": stroke, "power": power, "spin_corner": spin_corner,
			"elevation": elevation, "bottom": bottom}


## Font size in canvas units: FONT_SIZE, or more if that would come out under MIN_POINTS.
## screen_scale is pixels per point, canvas_scale pixels per canvas unit.
static func font_size_for(screen_scale: float, canvas_scale: float) -> int:
	return maxi(FONT_SIZE, ceili(MIN_POINTS * maxf(screen_scale, 1.0) / maxf(canvas_scale, 0.01)))


func _ready() -> void:
	add_theme_color_override("font_color", Color(0.86, 0.88, 0.86, 0.8))
	add_theme_color_override("font_shadow_color", Color(0.0, 0.0, 0.0, 0.6))
	text = text_for(Link.get_status())
	Link.status_changed.connect(_on_status_changed)
	get_viewport().size_changed.connect(_place)
	var poll := Timer.new()
	poll.wait_time = SAFE_AREA_POLL_SECONDS
	poll.timeout.connect(_place)
	add_child(poll)
	poll.start()
	_place()


func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_RESUMED or what == NOTIFICATION_APPLICATION_FOCUS_IN:
		_place()


func _on_status_changed(status: String) -> void:
	text = text_for(status)


func _place() -> void:
	if not is_inside_tree():
		return
	var window := get_window()
	place_in(Rect2(DisplayServer.get_display_safe_area()), Rect2(Vector2(window.position), Vector2(window.size)),
			get_viewport_rect().size, DisplayServer.screen_get_scale())


## Sets position and font size for a safe area and window in screen pixels and the canvas
## size in canvas units. Writes only what changed, so a steady poll never makes
## low-processor mode redraw.
func place_in(safe_area: Rect2, window_rect: Rect2, canvas_size: Vector2, screen_scale: float) -> void:
	var font_size := font_size_for(screen_scale, window_rect.size.y / maxf(canvas_size.y, 1.0))
	if get_theme_font_size("font_size") != font_size:
		add_theme_font_size_override("font_size", font_size)
	var origin := safe_origin(safe_area, window_rect, canvas_size) + MARGIN
	if position != origin:
		position = origin
