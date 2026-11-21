class_name EdgeIndicators
extends Control
## Arrows at the screen edge for large units (shadow-casters first) that are outside
## the view, so a threat to the light is never invisible in the Surface band.
## M2.10: also a diamond with the seconds to impact for each warned falling object
## (inside the warning lead, ~3 s) that is still off screen: red when it will hit
## the planet, cyan when a shield covers it.

var sim: Node
var camera: OrbitCamera
const INSET: float = 34.0


func _init() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)


func _process(_delta: float) -> void:
	queue_redraw()


## Screen position of a world point with the current camera.
func world_to_screen(world: Vector2) -> Vector2:
	return (world - camera.global_position) * camera.get_zoom_value() + size * 0.5


## Point on the inset rectangle along the ray from the centre toward `screen`.
func edge_point(screen: Vector2) -> Vector2:
	var inner: Rect2 = Rect2(Vector2.ZERO, size).grow(-INSET)
	var centre: Vector2 = size * 0.5
	var dir: Vector2 = (screen - centre).normalized()
	var t: float = minf(
		(inner.size.x * 0.5) / maxf(absf(dir.x), 0.0001),
		(inner.size.y * 0.5) / maxf(absf(dir.y), 0.0001))
	return centre + dir * t


## M2.10: warned falling objects that are off screen: [{at, dir, color, eta}].
func falling_markers() -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	if sim == null or camera == null or not sim.has_method(&"get_falling_warnings"):
		return out
	var inner: Rect2 = Rect2(Vector2.ZERO, size).grow(-INSET)
	var warnings: Array[Dictionary] = sim.get_falling_warnings()
	for warning: Dictionary in warnings:
		var screen: Vector2 = world_to_screen(warning.get("pos", Vector2.ZERO))
		if inner.has_point(screen):
			continue
		out.append({
			"at": edge_point(screen), "dir": (screen - size * 0.5).normalized(),
			"color": SkyThreats.warning_color(warning), "eta": float(warning.get("eta", 0.0)),
		})
	return out


func _draw() -> void:
	if sim == null or camera == null:
		return
	_draw_falling()  # M2.10
	var units: Array[Dictionary] = sim.get_large_units()
	var inner: Rect2 = Rect2(Vector2.ZERO, size).grow(-INSET)
	var centre: Vector2 = size * 0.5
	for unit: Dictionary in units:
		var pos: Vector2 = unit.get("pos", Vector2.ZERO)
		var screen: Vector2 = world_to_screen(pos)
		if inner.has_point(screen):
			continue
		var dir: Vector2 = (screen - centre).normalized()
		# Clamp along the ray from the centre to the inset rectangle.
		var t: float = minf(
			(inner.size.x * 0.5) / maxf(absf(dir.x), 0.0001),
			(inner.size.y * 0.5) / maxf(absf(dir.y), 0.0001))
		var at: Vector2 = centre + dir * t
		var side: Vector2 = dir.orthogonal()
		var color: Color = Palette.SHADED if bool(unit.get("shadow", false)) else Palette.THREAT
		draw_circle(at - dir * 8.0, 15.0, Color(0.0, 0.0, 0.0, 0.85))
		draw_arc(at - dir * 8.0, 15.0, 0.0, TAU, 24, color, 2.5, true)
		draw_colored_polygon(PackedVector2Array(
			[at + dir * 20.0, at + dir * 6.0 + side * 10.0, at + dir * 6.0 - side * 10.0]), color)


func _draw_falling() -> void:
	var font: Font = ThemeDB.fallback_font
	for marker: Dictionary in falling_markers():
		var at: Vector2 = marker["at"]
		var dir: Vector2 = marker["dir"]
		var color: Color = marker["color"]
		var side: Vector2 = dir.orthogonal()
		var c: Vector2 = at - dir * 6.0
		draw_colored_polygon(PackedVector2Array(
			[c + dir * 14.0, c + side * 11.0, c - dir * 14.0, c - side * 11.0]), Color(0.0, 0.0, 0.0, 0.85))
		draw_polyline(PackedVector2Array(
			[c + dir * 14.0, c + side * 11.0, c - dir * 14.0, c - side * 11.0, c + dir * 14.0]),
			color, 2.5, true)
		draw_string(font, c + Vector2(-12.0, 6.0), "%.1f" % float(marker["eta"]),
			HORIZONTAL_ALIGNMENT_CENTER, 24.0, 14, color)
