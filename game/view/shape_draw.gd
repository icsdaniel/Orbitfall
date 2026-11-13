class_name ShapeDraw
extends RefCounted
## Draws the placeholder shapes (same ids as BatchRenderer.Shape) with CanvasItem
## draw calls, for the handful of things that are nodes: buildings and HUD icons.
## Shapes point "up" (-y) at rotation 0, like the SDF shader.


static func points(shape: int, radius: float) -> PackedVector2Array:
	var pts: PackedVector2Array = PackedVector2Array()
	match shape:
		BatchRenderer.Shape.TRIANGLE:
			for i: int in 3:
				pts.append(Vector2.from_angle(-PI * 0.5 + TAU * float(i) / 3.0) * radius)
		BatchRenderer.Shape.SQUARE:
			var h: float = radius * 0.8
			pts = PackedVector2Array(
				[Vector2(-h, -h), Vector2(h, -h), Vector2(h, h), Vector2(-h, h)])
		BatchRenderer.Shape.HEXAGON:
			for i: int in 6:
				pts.append(Vector2.from_angle(TAU * float(i) / 6.0) * radius)
		BatchRenderer.Shape.STAR:
			for i: int in 10:
				var r: float = radius if i % 2 == 0 else radius * 0.5
				pts.append(Vector2.from_angle(-PI * 0.5 + TAU * float(i) / 10.0) * r)
		BatchRenderer.Shape.DIAMOND:
			pts = PackedVector2Array([
				Vector2(0.0, -radius), Vector2(radius * 0.68, 0.0),
				Vector2(0.0, radius), Vector2(-radius * 0.68, 0.0)])
		_:
			for i: int in 20:
				pts.append(Vector2.from_angle(TAU * float(i) / 20.0) * radius)
	return pts


## Filled shape with a contrasting outline. `rotation` 0 points up on screen.
static func draw(
		item: CanvasItem, shape: int, centre: Vector2, radius: float, rotation: float,
		color: Color, outline_width: float = 1.0
) -> void:
	if shape == BatchRenderer.Shape.DISC:
		item.draw_circle(centre, radius, color)
		var rim: Color = color.lightened(0.55) if color.get_luminance() < 0.3 else color.darkened(0.6)
		rim.a = color.a
		item.draw_arc(centre, radius, 0.0, TAU, 24, rim, maxf(outline_width, 1.0), true)
		item.draw_arc(centre, radius * 0.58, 0.0, TAU, 24, rim, maxf(outline_width, 1.0), true)
		return
	var pts: PackedVector2Array = Transform2D(rotation, centre) * points(shape, radius)
	item.draw_colored_polygon(pts, color)
	if outline_width > 0.0:
		var lum: float = color.get_luminance()
		var line: Color = color.lightened(0.55) if lum < 0.3 else color.darkened(0.6)
		line.a = color.a
		pts.append(pts[0])
		item.draw_polyline(pts, line, outline_width, true)
