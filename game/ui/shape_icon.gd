class_name ShapeIcon
extends Control
## A placeholder shape icon (same shapes and colours as in the world).

var shape: int = 0:
	set(value):
		shape = value
		queue_redraw()
var color: Color = Color.WHITE:
	set(value):
		color = value
		queue_redraw()


func _init(icon_shape: int = 0, icon_color: Color = Color.WHITE, px: float = 28.0) -> void:
	shape = icon_shape
	color = icon_color
	custom_minimum_size = Vector2(px, px)
	mouse_filter = Control.MOUSE_FILTER_IGNORE


func _draw() -> void:
	ShapeDraw.draw(self, shape, size * 0.5, minf(size.x, size.y) * 0.42, 0.0, color, 1.5)
