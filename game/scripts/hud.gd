extends Label
## The "PC link" status line, top-left inside the display's safe area.

const MARGIN := Vector2(16.0, 10.0)
const FONT_SIZE := 18
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
	var inside := safe_area.intersection(window_rect)
	if not inside.has_area():
		inside = window_rect
	var scale := canvas_size / window_rect.size.max(Vector2.ONE)
	return (inside.position - window_rect.position) * scale


func _ready() -> void:
	add_theme_font_size_override("font_size", FONT_SIZE)
	add_theme_color_override("font_color", Color(0.86, 0.88, 0.86, 0.8))
	add_theme_color_override("font_shadow_color", Color(0.0, 0.0, 0.0, 0.6))
	text = text_for(Link.get_status())
	Link.status_changed.connect(_on_status_changed)
	get_viewport().size_changed.connect(_place)
	_place()


func _on_status_changed(status: String) -> void:
	text = text_for(status)


func _place() -> void:
	var window := get_window()
	var window_rect := Rect2(Vector2(window.position), Vector2(window.size))
	var safe := Rect2(DisplayServer.get_display_safe_area())
	position = safe_origin(safe, window_rect, get_viewport_rect().size) + MARGIN
