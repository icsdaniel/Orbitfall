class_name EdgeIndicators
extends Control
## Arrows at the screen edge for large units (shadow-casters first) that are outside
## the view, so a threat to the light is never invisible in the Surface band.

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


func _draw() -> void:
	if sim == null or camera == null:
		return
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
