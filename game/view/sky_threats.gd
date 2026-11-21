class_name SkyThreats
extends Node2D
## M2.10: everything about falling objects and shields that is drawn in world space,
## over the entities: shield domes (energy arc, offline recharge progress, lit cue),
## warning streaks from each warned object to its impact point with a shrinking ETA
## ring (red = will hit the planet, cyan = a shield covers it now), the next wave's
## volley landing zones, and short impact / absorb flashes. The objects themselves
## are drawn by EntityView's MultiMesh batch. It only reads SimServer getters.

const FLASH_SECONDS: float = 0.6
const EVENT_IMPACT: String = "FallingImpact"
const EVENT_ABSORBED: String = "ShieldAbsorbed"

var sim: Node
## Camera zoom (screen px per world unit), for constant-pixel-size strokes.
var view_zoom: float = 1.0
## 0 in the Orbit band, 1 in the Surface band.
var surface_weight: float = 1.0
## Latest forecast (get_forecast); entry 0's volleys get landing-zone markers.
var forecast: Array[Dictionary] = []
var planet_radius: float = 100.0
var shields: Array[Dictionary] = []
var warnings: Array[Dictionary] = []
var warn_seconds: float = 3.0
## Live flashes: {pos: Vector2, color: Color, age: float, size: float}.
var flashes: Array[Dictionary] = []
var _font: Font


func _ready() -> void:
	_font = ThemeDB.fallback_font


func _process(delta: float) -> void:
	age_flashes(delta)
	sync()


## Copies shields and warnings from the sim and schedules a redraw.
func sync() -> void:
	if sim == null or not sim.has_method(&"get_shields"):
		return
	planet_radius = float(sim.get_planet_radius())
	shields = sim.get_shields()
	warnings = sim.get_falling_warnings()
	warn_seconds = maxf(float(sim.get_falling_warn_seconds()), 0.1)
	queue_redraw()


## Starts a flash for an impact or absorb event. Returns true when the event was
## one of those.
func handle_event(event: Dictionary) -> bool:
	var type: String = str(event.get("type", ""))
	var pos: Vector2 = Vector2(float(event.get("x", 0.0)), float(event.get("y", 0.0)))
	if type == EVENT_IMPACT:
		flashes.append({"pos": pos, "color": Palette.THREAT, "age": 0.0, "size": 22.0})
		return true
	if type == EVENT_ABSORBED:
		flashes.append({"pos": pos, "color": Palette.SHIELD, "age": 0.0, "size": 16.0})
		return true
	return false


func age_flashes(delta: float) -> void:
	for i: int in range(flashes.size() - 1, -1, -1):
		flashes[i]["age"] = float(flashes[i]["age"]) + delta
		if float(flashes[i]["age"]) >= FLASH_SECONDS:
			flashes.remove_at(i)


## Colour of a warning streak: cyan when a shield covers the impact, else red.
static func warning_color(warning: Dictionary) -> Color:
	return Palette.SHIELD if int(warning.get("shield_slot", -1)) >= 0 else Palette.THREAT


## Radius (screen px) of the ETA ring: shrinks from 18 px at the warning to 4 px
## at impact.
static func eta_ring_px(eta: float, lead: float) -> float:
	return 4.0 + 14.0 * clampf(eta / maxf(lead, 0.01), 0.0, 1.0)


## Points of an arc of radius r centred on `angle`, `half` either side.
static func arc_points(angle: float, half: float, r: float, steps: int) -> PackedVector2Array:
	var pts: PackedVector2Array = PackedVector2Array()
	for i: int in steps + 1:
		pts.append(Vector2.from_angle(angle - half + 2.0 * half * float(i) / float(steps)) * r)
	return pts


## Dome outline of a shield (or its placement ghost) on any canvas item: the arc at
## the dome radius and two spokes down to the planet surface.
static func draw_dome(canvas: CanvasItem, angle: float, half: float, dome: float, surface: float,
		color: Color, width: float) -> void:
	var pts: PackedVector2Array = arc_points(angle, half, dome, 32)
	canvas.draw_polyline(pts, color, width, true)
	for a: float in [angle - half, angle + half]:
		canvas.draw_line(Vector2.from_angle(a) * surface, Vector2.from_angle(a) * dome,
			Color(color, color.a * 0.5), width * 0.6, true)


func _draw() -> void:
	var px: float = 1.0 / maxf(view_zoom, 0.01)
	_draw_volleys(px)
	_draw_shields(px)
	_draw_warnings(px)
	_draw_flashes(px)


func _draw_volleys(px: float) -> void:
	if forecast.is_empty():
		return
	var entries: Array = forecast[0].get("entries", [])
	var color: Color = Palette.named("falling.meteor")
	for entry: Dictionary in entries:
		if not bool(entry.get("volley", false)):
			continue
		var bearing: float = Palette.angle_of(entry.get("lane_bearing", 0.0))
		var spread: float = maxf(Palette.angle_of(entry.get("spread", 0.0)), deg_to_rad(3.0))
		var dir: Vector2 = Vector2.from_angle(bearing)
		# Landing zone on the surface and a dashed fall line from the sky.
		draw_polyline(arc_points(bearing, spread, planet_radius + 3.0 * px, 16),
			Color(color, 0.85), 4.0 * px, true)
		draw_dashed_line(dir * (planet_radius + 12.0 * px), dir * (planet_radius + 420.0),
			Color(color, 0.35), 2.0 * px, 10.0 * px, true)
		var tip: Vector2 = dir * (planet_radius + 12.0 * px)
		var side: Vector2 = dir.orthogonal()
		var s: float = 9.0 * px
		draw_colored_polygon(PackedVector2Array(
			[tip, tip + dir * s * 1.5 + side * s, tip + dir * s * 1.5 - side * s]), Color(color, 0.9))


func _draw_shields(px: float) -> void:
	for shield: Dictionary in shields:
		var angle: float = float(shield.get("angle", 0.0))
		var half: float = float(shield.get("half_width", 0.5))
		var dome: float = float(shield.get("dome_radius", planet_radius + 40.0))
		var online: bool = bool(shield.get("online", true))
		if online:
			var frac: float = clampf(float(shield.get("energy_frac", 1.0)), 0.0, 1.0)
			# Translucent dome, solid rim; brighter with more energy.
			var fill: PackedVector2Array = arc_points(angle, half, dome, 32)
			var inner: PackedVector2Array = arc_points(angle, half, planet_radius, 32)
			inner.reverse()
			fill.append_array(inner)
			draw_colored_polygon(fill, Color(Palette.SHIELD, 0.05 + 0.07 * frac))
			draw_dome(self, angle, half, dome, planet_radius,
				Color(Palette.SHIELD, 0.45 + 0.5 * frac), 3.0 * px)
			# Energy gauge: an arc just inside the rim, its length the energy left.
			if frac > 0.0:
				draw_polyline(arc_points(angle - half + half * frac, half * frac, dome - 5.0 * px, 24),
					Color(Palette.GOOD, 0.9), 3.0 * px, true)
		else:
			# Offline: dashed dim rim and the recharge progress growing along it.
			var pts: PackedVector2Array = arc_points(angle, half, dome, 32)
			for i: int in range(0, pts.size() - 1, 2):
				draw_line(pts[i], pts[i + 1], Color(Palette.UNLIT, 0.8), 2.0 * px, true)
			var charge: float = clampf(float(shield.get("charge_frac", 0.0)), 0.0, 1.0)
			if charge > 0.0:
				draw_polyline(arc_points(angle - half + half * charge, half * charge, dome - 5.0 * px, 24),
					Color(Palette.TEXT, 0.85), 3.0 * px, true)
		if bool(shield.get("lit", false)):
			# Lit cue: a small sun dot on the rim (charges ~2x faster in the light).
			var at: Vector2 = Vector2.from_angle(angle) * (dome + 8.0 * px)
			draw_circle(at, 4.0 * px, Palette.LIT)


func _draw_warnings(px: float) -> void:
	for warning: Dictionary in warnings:
		var pos: Vector2 = warning.get("pos", Vector2.ZERO)
		var impact: Vector2 = warning.get("impact", Vector2.ZERO)
		var eta: float = float(warning.get("eta", 0.0))
		var color: Color = warning_color(warning)
		var urgency: float = 1.0 - clampf(eta / warn_seconds, 0.0, 1.0)
		draw_line(pos, impact, Color(color, 0.25 + 0.5 * urgency), (2.0 + 2.0 * urgency) * px, true)
		draw_arc(impact, eta_ring_px(eta, warn_seconds) * px, 0.0, TAU, 20, Color(color, 0.9),
			2.0 * px, true)
		draw_circle(impact, 2.5 * px, color)


func _draw_flashes(px: float) -> void:
	for flash: Dictionary in flashes:
		var t: float = clampf(float(flash["age"]) / FLASH_SECONDS, 0.0, 1.0)
		var color: Color = flash["color"]
		var r: float = float(flash["size"]) * (0.4 + t) * maxf(px, 0.6)
		draw_circle(flash["pos"], r, Color(color, 0.45 * (1.0 - t)))
		draw_arc(flash["pos"], r, 0.0, TAU, 20, Color(color, 1.0 - t), 2.0 * px, true)
