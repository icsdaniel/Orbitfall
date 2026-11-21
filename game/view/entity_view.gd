class_name EntityView
extends Node2D
## Feeds the sim's packed render buffers to BatchRenderer (no node per entity) and
## draws the few HP bars for large units (carrier size and up, readability rule 7).
## Buffer convention (one for core, binding and mock; see core/sim/render_snapshot.h):
## the instance scale is the entity's radius in world units, so both archetypes use
## a shape of radius 1 (shape size 2); the shape's "up" points along the velocity.

const ARCHETYPE_ENEMIES: int = 0
const ARCHETYPE_PROJECTILES: int = 1
const ARCHETYPE_FALLING: int = 2  # M2.10 meteors and bombs
## Shape diameter at instance scale 1: a shape of radius 1.
const SHAPE_SIZE: float = 2.0

var sim: Node
var renderer: BatchRenderer
var view_zoom: float = 1.0
var _large_units: Array[Dictionary] = []


func _ready() -> void:
	renderer = BatchRenderer.new()
	renderer.name = "BatchRenderer"
	add_child(renderer)
	renderer.define_archetype(ARCHETYPE_ENEMIES, SHAPE_SIZE)
	renderer.define_archetype(ARCHETYPE_PROJECTILES, SHAPE_SIZE)
	renderer.set_archetype_pixel_limits(ARCHETYPE_ENEMIES, 7.0, 4096.0)
	renderer.set_archetype_pixel_limits(ARCHETYPE_PROJECTILES, 5.0, 4096.0)
	renderer.define_archetype(ARCHETYPE_FALLING, SHAPE_SIZE)  # M2.10
	renderer.set_archetype_pixel_limits(ARCHETYPE_FALLING, 8.0, 4096.0)


func _process(_delta: float) -> void:
	sync()


func sync() -> void:
	if sim == null or renderer == null:
		return
	var enemies: PackedFloat32Array = sim.get_enemy_buffer()
	var enemy_count: int = sim.get_enemy_count()
	renderer.set_batch(ARCHETYPE_ENEMIES, enemies, enemy_count)
	var shots: PackedFloat32Array = sim.get_projectile_buffer()
	var shot_count: int = sim.get_projectile_count()
	renderer.set_batch(ARCHETYPE_PROJECTILES, shots, shot_count)
	if sim.has_method("get_falling_buffer"):  # M2.10
		renderer.set_batch(ARCHETYPE_FALLING, sim.get_falling_buffer(), sim.get_falling_count())
	_large_units = sim.get_large_units()
	queue_redraw()


## M2.3: geometry of the segmented HP bar of one large unit, in world units.
## `px` is one screen pixel in world units. Returns {back: Rect2, fill: Rect2,
## color: Color, dividers: PackedFloat32Array (x of each divider, segments - 1 of
## them), segments: int, full_segments: int (segments still completely full),
## shield: Rect2 (zero size when no shield is up)}.
static func hp_bar_layout(unit: Dictionary, px: float) -> Dictionary:
	var pos: Vector2 = unit.get("pos", Vector2.ZERO)
	var r: float = unit.get("radius", 10.0)
	var frac: float = clampf(float(unit.get("hp_frac", 1.0)), 0.0, 1.0)
	var segments: int = clampi(int(unit.get("segments", 1)), 1, 64)
	var shield: float = clampf(float(unit.get("shield_frac", 0.0)), 0.0, 1.0)
	var w: float = maxf(r * 2.0, 44.0 * px)
	var h: float = 6.0 * px
	var origin: Vector2 = pos + Vector2(-w * 0.5, -r - 12.0 * px)
	var dividers: PackedFloat32Array = PackedFloat32Array()
	for i: int in range(1, segments):
		dividers.append(origin.x + w * float(i) / float(segments))
	var shield_rect: Rect2 = Rect2(origin + Vector2(0.0, -4.0 * px), Vector2.ZERO)
	if shield > 0.0:
		shield_rect.size = Vector2(w * shield, 3.0 * px)
	return {
		"back": Rect2(origin - Vector2(px, px), Vector2(w + 2.0 * px, h + 2.0 * px)),
		"fill": Rect2(origin, Vector2(w * frac, h)),
		"color": Palette.GOOD.lerp(Palette.THREAT, 1.0 - frac),
		"dividers": dividers,
		"segments": segments,
		"full_segments": floori(frac * float(segments) + 0.0001),
		"shield": shield_rect,
	}


func _draw() -> void:
	var px: float = 1.0 / maxf(view_zoom, 0.01)
	for unit: Dictionary in _large_units:
		var bar: Dictionary = hp_bar_layout(unit, px)
		var back: Rect2 = bar["back"]
		var fill: Rect2 = bar["fill"]
		draw_rect(back, Color(0.0, 0.0, 0.0, 0.8))
		draw_rect(fill, bar["color"])
		var dividers: PackedFloat32Array = bar["dividers"]
		for x: float in dividers:
			draw_line(Vector2(x, fill.position.y), Vector2(x, fill.end.y),
				Color(0.0, 0.0, 0.0, 0.85), 1.5 * px)
		var shield: Rect2 = bar["shield"]
		if shield.size.x > 0.0:
			draw_rect(shield, Palette.SHIELD)
