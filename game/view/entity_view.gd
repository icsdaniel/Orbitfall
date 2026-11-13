class_name EntityView
extends Node2D
## Feeds the sim's packed render buffers to BatchRenderer (no node per entity) and
## draws the few HP bars for large units (carrier size and up, readability rule 7).
## Buffer convention (one for core, binding and mock; see core/sim/render_snapshot.h):
## the instance scale is the entity's radius in world units, so both archetypes use
## a shape of radius 1 (shape size 2); the shape's "up" points along the velocity.

const ARCHETYPE_ENEMIES: int = 0
const ARCHETYPE_PROJECTILES: int = 1
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
	_large_units = sim.get_large_units()
	queue_redraw()


func _draw() -> void:
	var px: float = 1.0 / maxf(view_zoom, 0.01)
	for unit: Dictionary in _large_units:
		var pos: Vector2 = unit.get("pos", Vector2.ZERO)
		var r: float = unit.get("radius", 10.0)
		var frac: float = clampf(float(unit.get("hp_frac", 1.0)), 0.0, 1.0)
		var w: float = maxf(r * 2.0, 44.0 * px)
		var h: float = 6.0 * px
		var origin: Vector2 = pos + Vector2(-w * 0.5, -r - 12.0 * px)
		draw_rect(Rect2(origin - Vector2(px, px), Vector2(w + 2.0 * px, h + 2.0 * px)),
			Color(0.0, 0.0, 0.0, 0.8))
		draw_rect(Rect2(origin, Vector2(w * frac, h)),
			Palette.GOOD.lerp(Palette.THREAT, 1.0 - frac))
		# Segments.
		for i: int in range(1, 4):
			var x: float = origin.x + w * float(i) / 4.0
			draw_line(Vector2(x, origin.y), Vector2(x, origin.y + h), Color(0, 0, 0, 0.7), px)
