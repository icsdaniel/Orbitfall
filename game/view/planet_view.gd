class_name PlanetView
extends Node2D
## Draws everything that is one-of-a-kind in world space with _draw: the sun beam
## cone, caster shadows, the planet disc with its lit cap and shadow wedges, the slot
## ring with lit/shaded tint, buildings, aim axes, the selection, the placement ghost
## (arc + range) and the forecast's lane markers and ghost shadow arcs.
## It only reads SimServer snapshot getters (CONTRACT.md); it never simulates.

const FLAG_LIT: int = 1
const FLAG_SHADED: int = 2
const FLAG_TARGET: int = 4
const BEAM_LENGTH: float = 4000.0
const BUILDING_RADIUS: float = 6.2
const BUILDING_OFFSET: float = 7.5
## Buildings never draw smaller than this on screen (Orbit band readability).
const BUILDING_MIN_PX: float = 7.5

var sim: Node
## 0 in the Orbit band, 1 in the Surface band; surface-only detail fades with it.
var surface_weight: float = 1.0
## Camera zoom (screen px per world unit), for constant-pixel-size markers.
var view_zoom: float = 1.0
var selected_slot: int = -1
## Building def index previewed on the selected empty slot, or -1.
var ghost_building: int = -1
## Latest forecast (get_forecast); entry 0 drives lane markers and ghost arcs.
var forecast: Array[Dictionary] = []

var radius: float = 100.0
var rotation_angle: float = 0.0
var slot_count: int = 0
var sun_bearing: float = 0.0
var beam_width: float = 0.0
var beam_strength: float = 1.0
var slot_buildings: PackedInt32Array = PackedInt32Array()
var slot_flags: PackedInt32Array = PackedInt32Array()
var shade_intervals: PackedFloat32Array = PackedFloat32Array()
var large_units: Array[Dictionary] = []
var building_defs: Array[Dictionary] = []

var _beam: Node2D
var _font: Font


func _ready() -> void:
	_font = ThemeDB.fallback_font
	_beam = Node2D.new()
	_beam.name = "Beam"
	_beam.show_behind_parent = true
	var additive: CanvasItemMaterial = CanvasItemMaterial.new()
	additive.blend_mode = CanvasItemMaterial.BLEND_MODE_ADD
	_beam.material = additive
	_beam.draw.connect(_draw_beam)
	add_child(_beam)


func _process(_delta: float) -> void:
	sync()


## Copies the current snapshot from the sim and schedules a redraw.
func sync() -> void:
	if sim == null:
		return
	if building_defs.is_empty():
		building_defs = sim.get_building_defs()
	radius = sim.get_planet_radius()
	rotation_angle = sim.get_planet_rotation()
	slot_count = sim.get_slot_count()
	sun_bearing = sim.get_sun_bearing()
	beam_width = sim.get_beam_width()
	beam_strength = sim.get_beam_strength()
	slot_buildings = sim.get_slot_buildings()
	slot_flags = sim.get_slot_flags()
	shade_intervals = sim.get_shade_intervals()
	large_units = sim.get_large_units()
	queue_redraw()
	if _beam != null:
		_beam.queue_redraw()


func slot_world_angle(slot: int) -> float:
	return TAU * float(slot) / float(maxi(slot_count, 1)) + rotation_angle


func slot_position(slot: int) -> Vector2:
	return Vector2.from_angle(slot_world_angle(slot)) * radius


## Half of the lit arc: asin(W / 2R) (BRIEF §6).
func lit_half_angle() -> float:
	if radius <= 0.0:
		return 0.0
	return asin(clampf(beam_width / (2.0 * radius), 0.0, 1.0))


## The slot whose marker is nearest to `world`, or -1 when further than `tolerance`.
func slot_at(world: Vector2, tolerance: float) -> int:
	if slot_count <= 0:
		return -1
	var local: float = wrapf(world.angle() - rotation_angle, 0.0, TAU)
	var slot: int = roundi(local / TAU * float(slot_count)) % slot_count
	var marker: Vector2 = Vector2.from_angle(slot_world_angle(slot)) * (
		radius + BUILDING_OFFSET + building_radius() - BUILDING_RADIUS)
	if marker.distance_to(world) > tolerance:
		return -1
	return slot


## World-space outline of a tower's firing sector: origin, then the arc at `range`.
func arc_polygon(slot: int, arc: float, tower_range: float, aim_offset: float = 0.0) -> PackedVector2Array:
	var pts: PackedVector2Array = PackedVector2Array()
	var origin: Vector2 = slot_position(slot)
	var axis: float = slot_world_angle(slot) + aim_offset
	pts.append(origin)
	var steps: int = 24
	for i: int in steps + 1:
		var a: float = axis - arc + 2.0 * arc * float(i) / float(steps)
		pts.append(origin + Vector2.from_angle(a) * tower_range)
	return pts


## Building marker radius in world units: at least BUILDING_MIN_PX on screen, but
## never so large that neighbours on the ring overlap.
func building_radius() -> float:
	var cap: float = PI * radius / float(maxi(slot_count, 1)) * 0.98
	return clampf(BUILDING_MIN_PX / maxf(view_zoom, 0.01), BUILDING_RADIUS, maxf(cap, BUILDING_RADIUS))


func building_def(index: int) -> Dictionary:
	if index < 0 or index >= building_defs.size():
		return {}
	return building_defs[index]


func _draw_beam() -> void:
	if beam_width <= 0.0 or beam_strength <= 0.0:
		return
	var s: Vector2 = Vector2.from_angle(sun_bearing)
	var n: Vector2 = s.orthogonal()
	var half: float = beam_width * 0.5
	var k: float = clampf(beam_strength, 0.0, 1.5)
	var core: Color = Color(Palette.SUN_WARM, 0.20 * k)
	var clear: Color = Color(Palette.SUN_WARM, 0.0)
	var far: Vector2 = s * BEAM_LENGTH
	# Core band plus soft fringes fading to nothing on either side.
	_beam.draw_colored_polygon(PackedVector2Array(
		[n * half * 0.8, far + n * half * 0.8, far - n * half * 0.8, -n * half * 0.8]), core)
	for side: float in [-1.0, 1.0]:
		var inner: Vector2 = n * half * 0.8 * side
		var outer: Vector2 = n * half * 1.12 * side
		_beam.draw_polygon(
			PackedVector2Array([inner, far + inner, far + outer, outer]),
			PackedColorArray([core, core, clear, clear]))
	# Brighter centre line so the bearing reads at a glance.
	_beam.draw_colored_polygon(PackedVector2Array(
		[n * half * 0.35, far + n * half * 0.35, far - n * half * 0.35, -n * half * 0.35]),
		Color(Palette.SUN_WARM, 0.09 * k))


func _draw() -> void:
	if slot_count <= 0:
		return
	_draw_lanes()
	_draw_caster_shadows()
	_draw_planet()
	_draw_shade_wedges()
	_draw_ghost_arcs()
	_draw_slots()
	_draw_selection()


func _draw_caster_shadows() -> void:
	var s: Vector2 = Vector2.from_angle(sun_bearing)
	var n: Vector2 = s.orthogonal()
	for unit: Dictionary in large_units:
		if not bool(unit.get("shadow", false)):
			continue
		var p: Vector2 = unit.get("pos", Vector2.ZERO)
		var r: float = unit.get("radius", 0.0)
		var along: float = p.dot(s)
		if along <= 0.0:
			continue
		var foot: Vector2 = p - s * along
		draw_colored_polygon(PackedVector2Array(
			[p + n * r, p - n * r, foot - n * r, foot + n * r]), Color(Palette.SHADOW, 0.62))


func _draw_planet() -> void:
	draw_circle(Vector2.ZERO, radius, Palette.PLANET)
	# Lit cap: the part of the disc inside the beam on the sun side.
	var half: float = lit_half_angle()
	if half > 0.0 and beam_strength > 0.0:
		var k: float = clampf(beam_strength, 0.0, 1.2)
		# Thick arcs instead of polygons: a warm band just inside the lit rim.
		draw_arc(Vector2.ZERO, radius - 11.0, sun_bearing - half, sun_bearing + half, 32,
			Color(Palette.LIT, 0.20 * k), 22.0, true)
		draw_arc(Vector2.ZERO, radius, sun_bearing - half, sun_bearing + half, 32,
			Color(Palette.LIT, 0.35 + 0.6 * clampf(beam_strength, 0.0, 1.0)), 2.2, true)
	# Night side: an arc of radius R/2 and width R covers exactly the far half-disc.
	draw_arc(Vector2.ZERO, radius * 0.5, sun_bearing + PI * 0.5, sun_bearing + PI * 1.5, 48,
		Color(0.0, 0.0, 0.03, 0.42), radius)
	draw_arc(Vector2.ZERO, radius, 0.0, TAU, 96, Color(Palette.PLANET_RIM, 0.55), 1.0, true)


func _draw_shade_wedges() -> void:
	var i: int = 0
	while i + 1 < shade_intervals.size():
		var a0: float = shade_intervals[i]
		var a1: float = shade_intervals[i + 1]
		i += 2
		if a1 - a0 < 0.0005:
			continue
		# Dark wedge reaching into the planet from the shaded stretch of rim.
		draw_arc(Vector2.ZERO, radius * 0.62, a0, a1, 12, Color(Palette.SHADOW, 0.72),
			radius * 0.76)
		draw_arc(Vector2.ZERO, radius, a0, a1, 12, Palette.SHADED, 2.6, true)


func _draw_ghost_arcs() -> void:
	if forecast.is_empty():
		return
	var arcs: PackedFloat32Array = forecast[0].get("eclipser_ghost_arcs", PackedFloat32Array())
	var i: int = 0
	while i + 1 < arcs.size():
		var a0: float = arcs[i]
		var a1: float = arcs[i + 1]
		i += 2
		# Dashed arc just outside the rim: "an Eclipser is expected to park here".
		var dashes: int = 5
		var step: float = (a1 - a0) / float(dashes)
		for d: int in dashes:
			var start: float = a0 + step * float(d)
			draw_arc(Vector2.ZERO, radius + 17.0, start, start + step * 0.6, 4,
				Color(Palette.SHADED, 0.9), 3.0, true)
		var wedge: PackedVector2Array = _arc_points(a0, a1, radius + 15.0, 8)
		wedge.append_array(_arc_points(a1, a0, radius, 8))
		draw_colored_polygon(wedge, Color(Palette.SHADED, 0.16))


func _draw_slots() -> void:
	var br: float = building_radius()
	var pitch: float = TAU / float(slot_count)
	for slot: int in slot_count:
		var angle: float = slot_world_angle(slot)
		var dir: Vector2 = Vector2.from_angle(angle)
		var flags: int = slot_flags[slot] if slot < slot_flags.size() else 0
		var lit: bool = (flags & FLAG_LIT) != 0
		var shaded: bool = (flags & FLAG_SHADED) != 0
		var pad: Color = Palette.UNLIT
		if shaded:
			pad = Palette.SHADED
		elif lit:
			pad = Palette.LIT
		# Slot pad on the rim: warm = lit, violet = shaded, grey = dark.
		draw_arc(Vector2.ZERO, radius - 3.0, angle - pitch * 0.36, angle + pitch * 0.36, 4,
			Color(pad, 0.95 if (lit or shaded) else 0.5), 4.0, true)
		var building: int = slot_buildings[slot] if slot < slot_buildings.size() else -1
		var centre: Vector2 = dir * (radius + BUILDING_OFFSET + br - 6.2)
		if building < 0:
			if surface_weight > 0.02:
				draw_arc(centre, 3.4, 0.0, TAU, 14,
					Color(Palette.TEXT_DIM, 0.55 * surface_weight), 0.8, true)
			continue
		var def: Dictionary = building_def(building)
		var color: Color = Palette.color_of(def.get("color", Color.WHITE))
		var is_panel: bool = str(def.get("kind", "")) == "panel"
		if is_panel:
			if lit and not shaded:
				draw_circle(centre, br * 1.75, Color(Palette.LIT, 0.38))
				color = color.lerp(Palette.LIT, 0.45)
			else:
				color = color.darkened(0.55)
		elif shaded:
			color = color.darkened(0.35)
		ShapeDraw.draw(self, Palette.shape_of(def.get("shape", 0)), centre, br,
			angle + PI * 0.5, color, 0.8)
		if not is_panel:
			var has_target: bool = (flags & FLAG_TARGET) != 0
			var axis_alpha: float = 0.9 if has_target else 0.45 * surface_weight
			if axis_alpha > 0.02:
				draw_line(centre + dir * br, centre + dir * (br + 9.0),
					Color(Palette.THREAT if has_target else Palette.TEXT, axis_alpha), 1.0, true)


func _draw_selection() -> void:
	var br: float = building_radius()
	if selected_slot < 0 or selected_slot >= slot_count:
		return
	var angle: float = slot_world_angle(selected_slot)
	var dir: Vector2 = Vector2.from_angle(angle)
	var centre: Vector2 = dir * (radius + BUILDING_OFFSET + br - 6.2)
	var existing: int = slot_buildings[selected_slot] if selected_slot < slot_buildings.size() else -1
	var def: Dictionary = building_def(existing if existing >= 0 else ghost_building)
	if not def.is_empty():
		var color: Color = Palette.color_of(def.get("color", Color.WHITE))
		var arc: float = Palette.angle_of(def.get("arc", 0.0))
		var tower_range: float = float(def.get("range", 0.0))
		if arc > 0.0 and tower_range > 0.0:
			var sector: PackedVector2Array = arc_polygon(selected_slot, arc, tower_range)
			draw_colored_polygon(sector, Color(color, 0.10))
			sector.append(sector[0])
			draw_polyline(sector, Color(color, 0.75), 1.5 / maxf(view_zoom, 0.01), true)
			draw_line(sector[0], sector[0] + dir * tower_range, Color(color, 0.35),
				1.0 / maxf(view_zoom, 0.01), true)
		if existing < 0:
			ShapeDraw.draw(self, Palette.shape_of(def.get("shape", 0)), centre,
				br, angle + PI * 0.5, Color(color, 0.6), 0.8)
	draw_arc(centre, br + 3.5, 0.0, TAU, 24, Palette.TEXT,
		maxf(1.2, 2.0 / maxf(view_zoom, 0.01)), true)


func _draw_lanes() -> void:
	if forecast.is_empty():
		return
	var entries: Array = forecast[0].get("entries", [])
	var seen: Dictionary[int, bool] = {}
	var px: float = 1.0 / maxf(view_zoom, 0.01)
	for entry: Dictionary in entries:
		var bearing: float = Palette.angle_of(entry.get("lane_bearing", 0.0))
		var key: int = roundi(rad_to_deg(bearing))
		if seen.has(key):
			continue
		seen[key] = true
		var dir: Vector2 = Vector2.from_angle(bearing)
		var side: Vector2 = dir.orthogonal()
		var near: float = radius + 26.0 + 10.0 * px
		draw_line(dir * near, dir * 1200.0, Color(Palette.THREAT, 0.16), 2.0 * px, true)
		# Chevron pointing at the planet, constant size on screen.
		var tip: Vector2 = dir * near
		var size: float = 13.0 * px
		draw_colored_polygon(PackedVector2Array(
			[tip, tip + dir * size * 1.4 + side * size, tip + dir * size * 1.4 - side * size]),
			Color(Palette.THREAT, 0.9))


func _arc_points(from: float, to: float, r: float, steps: int) -> PackedVector2Array:
	var pts: PackedVector2Array = PackedVector2Array()
	for i: int in steps + 1:
		pts.append(Vector2.from_angle(lerpf(from, to, float(i) / float(steps))) * r)
	return pts
