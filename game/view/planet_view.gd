class_name PlanetView
extends Node2D
## Draws everything that is one-of-a-kind in world space with _draw: the sun beam
## cone, caster shadows, the planet disc with its lit cap and shadow wedges, the slot
## ring with lit/shaded tint, buildings, aim axes, the selection, the placement ghost
## (arc + range) and the forecast's lane markers and ghost shadow arcs.
## M2.1: also the elevated ring, multi-slot footprints and adjacency links. A
## "site" is slot + layer * slot_count (layer 0 ground, 1 elevated).
## It only reads SimServer snapshot getters (CONTRACT.md); it never simulates.

const FLAG_LIT: int = 1
const FLAG_SHADED: int = 2
const FLAG_TARGET: int = 4
const BEAM_LENGTH: float = 4000.0
const BUILDING_RADIUS: float = 6.2
const BUILDING_OFFSET: float = 7.5
## Buildings never draw smaller than this on screen (Orbit band readability).
const BUILDING_MIN_PX: float = 7.5
## Gap between a ground marker and the elevated marker above it (world units).
const ELEVATED_GAP: float = 3.0
## Link colour per aura stat: range, fire rate, damage, income, detect (M2.6),
## shield damage (M2.6).
const LINK_COLORS: Array[Color] = [
	Color(0.40, 0.85, 1.0), Color(1.0, 0.62, 0.25), Color(1.0, 0.35, 0.30), Color(1.0, 0.80, 0.30),
	Color(0.45, 0.95, 0.55), Color(0.90, 0.45, 0.95)]
## M2.6: bands of the gradient range ring (falloff curve) of the selected tower.
const FALLOFF_BANDS: int = 10
## M2.6: floats per Sun Lens beam in get_beams().
const BEAM_FLOATS: int = 7
const STAT_NAMES: Array[String] = ["range", "fire rate", "damage", "income"]

var sim: Node
## 0 in the Orbit band, 1 in the Surface band; surface-only detail fades with it.
var surface_weight: float = 1.0
## Camera zoom (screen px per world unit), for constant-pixel-size markers.
var view_zoom: float = 1.0
var selected_slot: int = -1
## Layer of the selection: 0 ground, 1 elevated (M2.1).
var selected_layer: int = 0
## Building def index previewed on the selected empty slot, or -1.
var ghost_building: int = -1
## M2.2: upgraded stats of the selected building (get_slot_upgrades "stats"); its
## arc and range replace the definition's when drawing the selected sector.
var selected_stats: Dictionary = {}
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
# M2.1, all indexed by site.
var layer_count: int = 1
var elevated_height: float = 0.0
var elevated_range_bonus: float = 0.0
var site_buildings: PackedInt32Array = PackedInt32Array()
var site_flags: PackedInt32Array = PackedInt32Array()
var site_owners: PackedInt32Array = PackedInt32Array()
var site_modifiers: PackedFloat32Array = PackedFloat32Array()
var adjacency_links: PackedInt32Array = PackedInt32Array()
## M2.6: Sun Lens beams of the latest tick (see SimServer.get_beams).
var lens_beams: PackedFloat32Array = PackedFloat32Array()

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
	if sim.has_method(&"get_site_buildings"):
		layer_count = sim.get_layer_count()
		elevated_height = sim.get_elevated_height()
		elevated_range_bonus = sim.get_elevated_range_bonus()
		site_buildings = sim.get_site_buildings()
		site_flags = sim.get_site_flags()
		site_owners = sim.get_site_owners()
		site_modifiers = sim.get_site_modifiers()
		adjacency_links = sim.get_adjacency_links()
		if sim.has_method(&"get_beams"):  # M2.6
			lens_beams = sim.get_beams()
	else:
		# A sim without the M2.1 getters: one ring of 1-slot buildings.
		layer_count = 1
		site_buildings = slot_buildings
		site_flags = slot_flags
		site_owners = PackedInt32Array()
		site_owners.resize(slot_buildings.size())
		for i: int in slot_buildings.size():
			site_owners[i] = i if slot_buildings[i] >= 0 else -1
		site_modifiers = PackedFloat32Array()
		adjacency_links = PackedInt32Array()
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


## Sector outline from any origin (elevated towers stand above the surface).
func sector_polygon(origin: Vector2, axis: float, arc: float, tower_range: float) -> PackedVector2Array:
	var pts: PackedVector2Array = PackedVector2Array()
	pts.append(origin)
	var steps: int = 24
	for i: int in steps + 1:
		var a: float = axis - arc + 2.0 * arc * float(i) / float(steps)
		pts.append(origin + Vector2.from_angle(a) * tower_range)
	return pts


# ---- M2.1 sites -------------------------------------------------------------

func site_of(slot: int, layer: int) -> int:
	return posmod(slot, maxi(slot_count, 1)) + layer * slot_count


func site_slot(site: int) -> int:
	return site % maxi(slot_count, 1)


func site_layer(site: int) -> int:
	return site / maxi(slot_count, 1)


## Anchor site of the building covering `site`, or -1 when the slot is free.
func site_owner(site: int) -> int:
	return site_owners[site] if site >= 0 and site < site_owners.size() else -1


## Building def index anchored on `site`, or -1.
func site_building(site: int) -> int:
	return site_buildings[site] if site >= 0 and site < site_buildings.size() else -1


## Slots covered by the building anchored on `site` (1 when there is none).
func site_width(site: int) -> int:
	return clampi(int(building_def(site_building(site)).get("footprint", 1)), 1, 3)


## Multiplier `stat` (0 range, 1 fire rate, 2 damage, 3 income) of a site.
func site_modifier(site: int, stat: int) -> float:
	var i: int = site * 4 + stat
	return site_modifiers[i] if site >= 0 and i < site_modifiers.size() else 1.0


## World angle of a site: the centre of the footprint for an anchor.
func site_world_angle(site: int) -> float:
	var extra: float = 0.5 * float(site_width(site) - 1) if site_building(site) >= 0 else 0.0
	return TAU * (float(site_slot(site)) + extra) / float(maxi(slot_count, 1)) + rotation_angle


## Distance from the planet centre at which a layer's markers are drawn.
func marker_radius(layer: int) -> float:
	var br: float = building_radius()
	return radius + BUILDING_OFFSET + br - BUILDING_RADIUS + float(layer) * (2.0 * br + ELEVATED_GAP)


## Centre of a site's marker (where it is drawn and tapped).
func site_position(site: int) -> Vector2:
	return Vector2.from_angle(site_world_angle(site)) * marker_radius(site_layer(site))


## True when the elevated slot above ground `slot` has a foundation and is free.
func can_build_above(slot: int) -> bool:
	return layer_count > 1 and site_owner(slot) >= 0 and site_owner(slot + slot_count) < 0


## Client-side copy of the sim's placement rules, for the ghost's colour only
## (the sim decides): free slots under the whole footprint, the right layer,
## and a foundation under every elevated slot.
func ghost_fits(slot: int, layer: int, building: int) -> bool:
	var def: Dictionary = building_def(building)
	if def.is_empty() or layer >= layer_count:
		return false
	var allowed: int = int(def.get("layer", 2))
	if allowed != 2 and allowed != layer:
		return false
	for k: int in clampi(int(def.get("footprint", 1)), 1, 3):
		if site_owner(site_of(slot + k, layer)) >= 0:
			return false
		if layer > 0 and site_owner(site_of(slot + k, 0)) < 0:
			return false
	return true


## The site whose marker is nearest to `world`, or -1 when further than
## `tolerance`. Elevated markers count when they hold a building or could (a
## foundation below); slots covered by a wide building resolve to themselves
## (see site_owner for the anchor).
func site_at(world: Vector2, tolerance: float) -> int:
	if slot_count <= 0:
		return -1
	var local: float = wrapf(world.angle() - rotation_angle, 0.0, TAU)
	var slot: int = roundi(local / TAU * float(slot_count)) % slot_count
	var dir: Vector2 = Vector2.from_angle(slot_world_angle(slot))
	var best: int = -1
	var best_dist: float = tolerance
	for layer: int in layer_count:
		if layer > 0 and site_owner(slot) < 0 and site_owner(slot + slot_count) < 0:
			continue
		var d: float = (dir * marker_radius(layer)).distance_to(world)
		if d <= best_dist:
			best_dist = d
			best = slot + layer * slot_count
	return best

# ---- end M2.1 ---------------------------------------------------------------


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
	_draw_lens_beams()
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
		var owner: int = site_owner(slot)
		if owner < 0:
			if surface_weight > 0.02:
				draw_arc(site_position(slot), 3.4, 0.0, TAU, 14,
					Color(Palette.TEXT_DIM, 0.55 * surface_weight), 0.8, true)
		elif owner == slot:
			_draw_building(slot, br)
		# else: covered by a wide building, drawn at its anchor.
	if layer_count > 1:
		_draw_elevated(br, pitch)
	_draw_links()


## The elevated ring (M2.1): a faint guide ring and an open marker above every
## foundation in the Build view; elevated buildings in every view.
func _draw_elevated(br: float, pitch: float) -> void:
	var ring: float = marker_radius(1)
	if surface_weight > 0.02:
		for slot: int in slot_count:
			var angle: float = slot_world_angle(slot)
			var has_foundation: bool = site_owner(slot) >= 0
			draw_arc(Vector2.ZERO, ring, angle - pitch * 0.30, angle + pitch * 0.30, 4,
				Color(Palette.TEXT_DIM, (0.42 if has_foundation else 0.14) * surface_weight),
				0.8, true)
			if has_foundation and site_owner(slot + slot_count) < 0:
				draw_arc(site_position(slot + slot_count), 3.4, 0.0, TAU, 14,
					Color(Palette.GOOD, 0.75 * surface_weight), 0.9, true)
	for slot: int in slot_count:
		var site: int = slot + slot_count
		if site_owner(site) == site:
			_draw_building(site, br)


## One building at its anchor site: footprint band, shape, light tint, aim axis.
func _draw_building(site: int, br: float) -> void:
	var def: Dictionary = building_def(site_building(site))
	var flags: int = site_flags[site] if site < site_flags.size() else 0
	var lit: bool = (flags & FLAG_LIT) != 0
	var shaded: bool = (flags & FLAG_SHADED) != 0
	var angle: float = site_world_angle(site)
	var dir: Vector2 = Vector2.from_angle(angle)
	var centre: Vector2 = site_position(site)
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
	var layer: int = site_layer(site)
	_draw_footprint(site_slot(site), layer, site_width(site), br, color, 1.0)
	if layer > 0:
		# A short strut down to the foundation.
		draw_line(centre - dir * br, centre - dir * (br + ELEVATED_GAP), Color(color, 0.9),
			1.4, true)
	ShapeDraw.draw(self, Palette.shape_of(def.get("shape", 0)), centre, br,
		angle + PI * 0.5, color, 0.8)
	if float(def.get("arc", 0.0)) > 0.0 and str(def.get("kind", "")) == "tower":
		var has_target: bool = (flags & FLAG_TARGET) != 0
		var axis_alpha: float = 0.9 if has_target else 0.45 * surface_weight
		if axis_alpha > 0.02:
			draw_line(centre + dir * br, centre + dir * (br + 9.0),
				Color(Palette.THREAT if has_target else Palette.TEXT, axis_alpha), 1.0, true)


## Band over the slots a building of `width` covers from `slot` on (nothing for
## 1-slot buildings): the footprint.
func _draw_footprint(slot: int, layer: int, width: int, br: float, color: Color, alpha: float) -> void:
	if width <= 1:
		return
	var pitch: float = TAU / float(slot_count)
	var a0: float = slot_world_angle(slot) - pitch * 0.42
	var a1: float = slot_world_angle(slot + width - 1) + pitch * 0.42
	var r: float = marker_radius(layer)
	draw_arc(Vector2.ZERO, r, a0, a1, 4 * width, Color(color, 0.30 * alpha), br * 1.7)
	for edge: float in [r - br * 0.85, r + br * 0.85]:
		draw_arc(Vector2.ZERO, edge, a0, a1, 4 * width, Color(color, 0.85 * alpha), 0.8, true)
	for a: float in [a0, a1]:
		var d: Vector2 = Vector2.from_angle(a)
		draw_line(d * (r - br * 0.85), d * (r + br * 0.85), Color(color, 0.85 * alpha), 0.8, true)


## Adjacency links (M2.1), Build view: a line from each aura building to every
## building it buffs, coloured by the stat.
func _draw_links() -> void:
	if surface_weight <= 0.02:
		return
	var px: float = 1.0 / maxf(view_zoom, 0.01)
	var i: int = 0
	while i + 2 < adjacency_links.size():
		var from: Vector2 = site_position(adjacency_links[i])
		var to: Vector2 = site_position(adjacency_links[i + 1])
		var color: Color = LINK_COLORS[clampi(adjacency_links[i + 2], 0, LINK_COLORS.size() - 1)]
		i += 3
		draw_line(from, to, Color(color, 0.85 * surface_weight), maxf(1.6 * px, 0.9), true)
		draw_circle(to, maxf(2.6 * px, 1.4), Color(color, 0.95 * surface_weight))


func _draw_selection() -> void:
	var br: float = building_radius()
	if selected_slot < 0 or selected_slot >= slot_count:
		return
	var px: float = 1.0 / maxf(view_zoom, 0.01)
	var site: int = site_of(selected_slot, selected_layer)
	var owner: int = site_owner(site)
	var existing: int = site_building(owner) if owner >= 0 else -1
	var def: Dictionary = building_def(existing if existing >= 0 else ghost_building)
	var width: int = site_width(owner) if owner >= 0 else int(def.get("footprint", 1))
	var first: int = site_slot(owner) if owner >= 0 else selected_slot
	var angle: float = slot_world_angle(first) + 0.5 * float(width - 1) * TAU / float(slot_count)
	var dir: Vector2 = Vector2.from_angle(angle)
	var centre: Vector2 = dir * marker_radius(selected_layer)
	if not def.is_empty():
		var color: Color = Palette.color_of(def.get("color", Color.WHITE))
		var arc: float = Palette.angle_of(def.get("arc", 0.0))
		var tower_range: float = float(def.get("range", 0.0))
		if owner >= 0 and not selected_stats.is_empty():  # M2.2: upgrades x M2.1 bonuses
			arc = float(selected_stats.get("arc", arc))
			tower_range = float(selected_stats.get("range", tower_range))
		elif owner >= 0:
			tower_range *= site_modifier(owner, 0)
		elif selected_layer > 0:
			tower_range *= 1.0 + elevated_range_bonus
		if arc > 0.0 and tower_range > 0.0:
			var origin: Vector2 = dir * (radius + (elevated_height if selected_layer > 0 else 0.0))
			var sector: PackedVector2Array = sector_polygon(origin, angle, arc, tower_range)
			_draw_falloff_ring(origin, angle, arc, tower_range, def.get("falloff", PackedFloat32Array()), color)
			sector.append(sector[0])
			draw_polyline(sector, Color(color, 0.75), 1.5 * px, true)
			draw_line(sector[0], sector[0] + dir * tower_range, Color(color, 0.35), 1.0 * px, true)
		_draw_aura_reach(def, first, width)
		if str(def.get("kind", "")) == "shield":  # M2.10: the dome it raises
			var half: float = float(def.get("shield_width", 0.0)) * 0.5
			SkyThreats.draw_dome(self, angle, half, radius + float(def.get("shield_height", 0.0)),
				radius, Color(Palette.SHIELD, 0.8), 2.0 * px)
		if owner < 0:
			var ok: bool = ghost_fits(selected_slot, selected_layer, ghost_building)
			var ghost: Color = color if ok else Palette.THREAT
			_draw_footprint(first, selected_layer, width, br, ghost, 0.7)
			ShapeDraw.draw(self, Palette.shape_of(def.get("shape", 0)), centre,
				br, angle + PI * 0.5, Color(ghost, 0.6), 0.8)
	draw_arc(centre, br + 3.5, 0.0, TAU, 24, Palette.TEXT, maxf(1.2, 2.0 * px), true)


## M2.6 gradient range ring: the arc sector filled in bands whose opacity
## follows the tower's damage falloff curve (Flak bright close, Railgun even).
func _draw_falloff_ring(
		origin: Vector2, axis: float, arc: float, tower_range: float, falloff: Variant, color: Color
) -> void:
	var curve: PackedFloat32Array = falloff if falloff is PackedFloat32Array else PackedFloat32Array()
	var top: float = 1.0
	for i: int in range(1, curve.size(), 2):
		top = maxf(top, curve[i])
	var steps: int = maxi(6, int(arc * 24.0))
	for band: int in FALLOFF_BANDS:
		var t0: float = float(band) / float(FALLOFF_BANDS)
		var t1: float = float(band + 1) / float(FALLOFF_BANDS)
		var mult: float = falloff_at(curve, 0.5 * (t0 + t1))
		var points: PackedVector2Array = PackedVector2Array()
		for k: int in steps + 1:
			var a: float = axis - arc + 2.0 * arc * float(k) / float(steps)
			points.append(origin + Vector2.from_angle(a) * tower_range * t1)
		for k: int in range(steps, -1, -1):
			var a: float = axis - arc + 2.0 * arc * float(k) / float(steps)
			points.append(origin + Vector2.from_angle(a) * tower_range * t0)
		draw_colored_polygon(points, Color(color, 0.03 + 0.17 * mult / top))


## Falloff multiplier at `t` (distance / range) of a curve [t0, m0, t1, m1, ...];
## 1 for an empty curve. Same interpolation as core falloffAt.
static func falloff_at(curve: PackedFloat32Array, t: float) -> float:
	var n: int = curve.size() >> 1
	if n == 0:
		return 1.0
	if t <= curve[0]:
		return curve[1]
	for k: int in range(1, n):
		var t0: float = curve[2 * k - 2]
		var t1: float = curve[2 * k]
		if t <= t1:
			var span: float = t1 - t0
			if span <= 0.0:
				return curve[2 * k + 1]
			return lerpf(curve[2 * k - 1], curve[2 * k + 1], (t - t0) / span)
	return curve[2 * n - 1]


## M2.6 Sun Lens beams: a warm line whose width and brightness follow the beam
## power (ramp x channel), so an Eclipser shading the panels visibly dims it;
## Overcharge adds a white core.
func _draw_lens_beams() -> void:
	var px: float = 1.0 / maxf(view_zoom, 0.01)
	for i: int in range(0, lens_beams.size() - BEAM_FLOATS + 1, BEAM_FLOATS):
		var from: Vector2 = Vector2(lens_beams[i], lens_beams[i + 1])
		var to: Vector2 = Vector2(lens_beams[i + 2], lens_beams[i + 3])
		var power: float = clampf(lens_beams[i + 4], 0.0, 4.0)
		var over: bool = lens_beams[i + 5] > 0.5
		var shown: float = clampf(power, 0.0, 1.0)
		draw_line(from, to, Color(Palette.SUN_WARM, 0.15 + 0.35 * shown), (2.0 + 6.0 * shown) * px, true)
		draw_line(from, to, Color(Palette.SUN_WARM.lightened(0.3), 0.35 + 0.6 * shown),
			(0.8 + 1.8 * shown) * px, true)
		if over:
			draw_line(from, to, Color(1.0, 1.0, 1.0, 0.9), 1.6 * px, true)
		draw_circle(to, (2.0 + 4.0 * shown) * px, Color(Palette.SUN_WARM, 0.3 + 0.5 * shown))


## Reach of a building's auras along the ring: `radius` slots past each end of
## its footprint.
func _draw_aura_reach(def: Dictionary, first: int, width: int) -> void:
	var auras: Array = def.get("auras", [])
	var pitch: float = TAU / float(slot_count)
	for aura: Dictionary in auras:
		var k: int = int(aura.get("radius", 0))
		var color: Color = LINK_COLORS[clampi(int(aura.get("stat", 0)), 0, LINK_COLORS.size() - 1)]
		var a0: float = slot_world_angle(first - k) - pitch * 0.5
		var a1: float = slot_world_angle(first + width - 1 + k) + pitch * 0.5
		if a1 - a0 >= TAU:
			a1 = a0 + TAU
		var outer: float = marker_radius(layer_count - 1) + building_radius() + 5.0
		draw_arc(Vector2.ZERO, outer, a0, a1, 24, Color(color, 0.8), 1.6, true)
		for a: float in [a0, a1]:
			draw_line(Vector2.from_angle(a) * radius, Vector2.from_angle(a) * outer,
				Color(color, 0.45), 0.8, true)


func _draw_lanes() -> void:
	if forecast.is_empty():
		return
	var entries: Array = forecast[0].get("entries", [])
	var seen: Dictionary[int, bool] = {}
	var px: float = 1.0 / maxf(view_zoom, 0.01)
	for entry: Dictionary in entries:
		if bool(entry.get("volley", false)):
			continue  # M2.10: volleys land from the sky; SkyThreats marks them
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
