class_name BatchRenderer
extends Node2D
## M0.4 MultiMesh renderer: one RenderingServer multimesh batch per visual archetype,
## drawn with the SDF shape shader. No scene-tree node per entity.
##
## Buffer layout (Godot's native 2D multimesh layout, 16 floats per instance):
##   0..7   Transform2D: x.x, y.x, pad, origin.x, x.y, y.y, pad, origin.y
##   8..11  colour RGBA
##   12..15 custom: shape id, colour tier (< 0 = instance colour only),
##          trait bit flags, free (outline width override; 0 = default)

const FLOATS_PER_INSTANCE: int = 16
const OFFSET_COLOR: int = 8
const OFFSET_CUSTOM: int = 12
const MIN_CAPACITY: int = 64
## Must match QUAD_PAD in sdf_shapes.gdshader.
const QUAD_PAD: float = 1.5

enum Shape { CIRCLE, TRIANGLE, SQUARE, HEXAGON, STAR, DIAMOND, RING }
enum Tier { RED, BLUE, GREEN, YELLOW, PINK, BLACK, WHITE, CERAMIC }
const SHAPE_COUNT: int = 7
const TRAIT_CAMO: int = 1
const TRAIT_ARMORED: int = 2
const TRAIT_SHIELDED: int = 4

const SHAPE_SHADER: Shader = preload("res://game/render/shaders/sdf_shapes.gdshader")
const _HUGE: float = 1.0e9


class Batch:
	extends RefCounted
	var canvas_item: RID
	var multimesh: RID
	var mesh: RID
	var material: ShaderMaterial
	var shape_size: float = 0.0
	var capacity: int = 0
	var count: int = 0
	var min_px: float = -1.0  # < 0: use the renderer default
	var max_px: float = -1.0
	var scratch: PackedFloat32Array = PackedFloat32Array()


## Smallest on-screen shape diameter in pixels (default for all archetypes).
@export var min_pixel_size: float = 3.0:
	set(value):
		min_pixel_size = value
		_push_pixel_limits()
## Largest on-screen shape diameter in pixels (default for all archetypes).
@export var max_pixel_size: float = 4096.0:
	set(value):
		max_pixel_size = value
		_push_pixel_limits()
## Palette indexed by colour tier: red, blue, green, yellow, pink, black, white, ceramic.
@export var palette_colors: PackedColorArray = PackedColorArray([
	Color(0.90, 0.22, 0.20), Color(0.22, 0.48, 0.95), Color(0.25, 0.78, 0.33),
	Color(0.98, 0.84, 0.22), Color(0.98, 0.50, 0.75), Color(0.10, 0.10, 0.12),
	Color(0.96, 0.96, 0.96), Color(0.80, 0.62, 0.42),
]):
	set(value):
		palette_colors = value
		_rebuild_palette()
## Optional camera whose zoom drives the minimum pixel sizes. Without one the
## viewport's canvas transform is used.
@export var camera: Camera2D
## When > 0, used instead of the camera or viewport zoom.
var zoom_override: float = 0.0

var _batches: Dictionary[int, Batch] = {}
var _palette_texture: ImageTexture
var _zoom: float = 1.0


func _init() -> void:
	_rebuild_palette()


func _process(_delta: float) -> void:
	_set_zoom(_read_zoom())


func _notification(what: int) -> void:
	if what == NOTIFICATION_PREDELETE:
		for batch: Batch in _batches.values():
			RenderingServer.free_rid(batch.canvas_item)
			RenderingServer.free_rid(batch.multimesh)
			RenderingServer.free_rid(batch.mesh)
		_batches.clear()


## Creates the canvas item, quad mesh and multimesh for one visual archetype.
## shape_mesh_size is the shape diameter in world units at instance scale 1.
func define_archetype(id: int, shape_mesh_size: float) -> void:
	if _batches.has(id):
		push_error("BatchRenderer: archetype %d already defined" % id)
		return
	var batch: Batch = Batch.new()
	batch.shape_size = shape_mesh_size
	batch.mesh = _create_quad(shape_mesh_size * QUAD_PAD)
	batch.multimesh = RenderingServer.multimesh_create()
	RenderingServer.multimesh_set_mesh(batch.multimesh, batch.mesh)
	batch.material = ShaderMaterial.new()
	batch.material.shader = SHAPE_SHADER
	batch.material.set_shader_parameter(&"palette", _palette_texture)
	batch.material.set_shader_parameter(&"palette_size", float(maxi(palette_colors.size(), 1)))
	batch.material.set_shader_parameter(&"shape_size", shape_mesh_size)
	batch.material.set_shader_parameter(&"zoom", _zoom)
	batch.canvas_item = RenderingServer.canvas_item_create()
	RenderingServer.canvas_item_set_parent(batch.canvas_item, get_canvas_item())
	RenderingServer.canvas_item_set_material(batch.canvas_item, batch.material.get_rid())
	RenderingServer.canvas_item_set_draw_index(batch.canvas_item, id)
	# Batches are never culled as a whole; culling is done per grid cell upstream.
	RenderingServer.canvas_item_set_custom_rect(
		batch.canvas_item, true, Rect2(-_HUGE, -_HUGE, 2.0 * _HUGE, 2.0 * _HUGE))
	RenderingServer.canvas_item_add_multimesh(batch.canvas_item, batch.multimesh)
	_batches[id] = batch
	_allocate(batch, MIN_CAPACITY)
	RenderingServer.multimesh_set_visible_instances(batch.multimesh, 0)
	_apply_pixel_limits(batch)


## Uploads `count` instances from `data` (16 floats each, see the layout above).
## Fast path: pass a buffer of exactly get_capacity(id) * 16 floats to avoid a copy.
func set_batch(id: int, data: PackedFloat32Array, count: int) -> void:
	var batch: Batch = _batches.get(id)
	if batch == null:
		push_error("BatchRenderer: archetype %d not defined" % id)
		return
	@warning_ignore("integer_division")
	count = clampi(count, 0, data.size() / FLOATS_PER_INSTANCE)
	if count > batch.capacity:
		_allocate(batch, grown_capacity(batch.capacity, count))
	if count > 0:
		var needed: int = batch.capacity * FLOATS_PER_INSTANCE
		if data.size() == needed:
			RenderingServer.multimesh_set_buffer(batch.multimesh, data)
		else:
			batch.scratch = data.slice(0, count * FLOATS_PER_INSTANCE)
			batch.scratch.resize(needed)
			RenderingServer.multimesh_set_buffer(batch.multimesh, batch.scratch)
	RenderingServer.multimesh_set_visible_instances(batch.multimesh, count)
	batch.count = count


## Grows the capacity up front (doubling) so later set_batch calls do not reallocate.
## Reallocating drops the uploaded instances, so call this before set_batch.
func reserve(id: int, instances: int) -> void:
	var batch: Batch = _batches.get(id)
	if batch != null and instances > batch.capacity:
		_allocate(batch, grown_capacity(batch.capacity, instances))
		RenderingServer.multimesh_set_visible_instances(batch.multimesh, 0)
		batch.count = 0


## Per-archetype override of the pixel size clamp (negative = renderer default).
func set_archetype_pixel_limits(id: int, min_px: float, max_px: float) -> void:
	var batch: Batch = _batches.get(id)
	if batch == null:
		push_error("BatchRenderer: archetype %d not defined" % id)
		return
	batch.min_px = min_px
	batch.max_px = max_px
	_apply_pixel_limits(batch)


func has_archetype(id: int) -> bool:
	return _batches.has(id)


func get_capacity(id: int) -> int:
	var batch: Batch = _batches.get(id)
	return batch.capacity if batch != null else 0


func get_count(id: int) -> int:
	var batch: Batch = _batches.get(id)
	return batch.count if batch != null else 0


func get_multimesh_rid(id: int) -> RID:
	var batch: Batch = _batches.get(id)
	return batch.multimesh if batch != null else RID()


func get_palette_image() -> Image:
	return _palette_texture.get_image()


## The zoom currently used for pixel size clamping.
func get_zoom() -> float:
	return _zoom


## On-screen shape diameter in pixels, same formula as the shader.
static func pixel_size(world_size: float, zoom: float, min_px: float, max_px: float) -> float:
	return clampf(world_size * zoom, min_px, maxf(min_px, max_px))


## Capacity after growing by doubling until `needed` fits. Never shrinks.
static func grown_capacity(current: int, needed: int) -> int:
	var capacity: int = maxi(current, MIN_CAPACITY)
	while capacity < needed:
		capacity *= 2
	return capacity


## Writes one instance into a buffer using the native 2D multimesh layout.
static func write_instance(
		data: PackedFloat32Array, index: int, xform: Transform2D, color: Color, custom: Color
) -> void:
	var o: int = index * FLOATS_PER_INSTANCE
	data[o] = xform.x.x
	data[o + 1] = xform.y.x
	data[o + 2] = 0.0
	data[o + 3] = xform.origin.x
	data[o + 4] = xform.x.y
	data[o + 5] = xform.y.y
	data[o + 6] = 0.0
	data[o + 7] = xform.origin.y
	data[o + 8] = color.r
	data[o + 9] = color.g
	data[o + 10] = color.b
	data[o + 11] = color.a
	data[o + 12] = custom.r
	data[o + 13] = custom.g
	data[o + 14] = custom.b
	data[o + 15] = custom.a


## Packs shape id, colour tier, trait flags and the free slot into a custom value.
static func make_custom(shape: int, tier: int, traits: int = 0, free: float = 0.0) -> Color:
	return Color(float(shape), float(tier), float(traits), free)


func _allocate(batch: Batch, capacity: int) -> void:
	RenderingServer.multimesh_allocate_data(
		batch.multimesh, capacity, RenderingServer.MULTIMESH_TRANSFORM_2D, true, true)
	RenderingServer.multimesh_set_custom_aabb(
		batch.multimesh,
		AABB(Vector3(-_HUGE, -_HUGE, -1.0), Vector3(2.0 * _HUGE, 2.0 * _HUGE, 2.0)))
	batch.capacity = capacity


func _create_quad(size: float) -> RID:
	var h: float = size * 0.5
	var arrays: Array = []
	arrays.resize(RenderingServer.ARRAY_MAX)
	arrays[RenderingServer.ARRAY_VERTEX] = PackedVector2Array(
		[Vector2(-h, -h), Vector2(h, -h), Vector2(h, h), Vector2(-h, h)])
	arrays[RenderingServer.ARRAY_TEX_UV] = PackedVector2Array(
		[Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1)])
	arrays[RenderingServer.ARRAY_COLOR] = PackedColorArray(
		[Color.WHITE, Color.WHITE, Color.WHITE, Color.WHITE])
	arrays[RenderingServer.ARRAY_INDEX] = PackedInt32Array([0, 1, 2, 0, 2, 3])
	var mesh: RID = RenderingServer.mesh_create()
	RenderingServer.mesh_add_surface_from_arrays(
		mesh, RenderingServer.PRIMITIVE_TRIANGLES, arrays)
	return mesh


func _rebuild_palette() -> void:
	var n: int = maxi(palette_colors.size(), 1)
	var image: Image = Image.create_empty(n, 1, false, Image.FORMAT_RGBA8)
	for i: int in palette_colors.size():
		image.set_pixel(i, 0, palette_colors[i])
	_palette_texture = ImageTexture.create_from_image(image)
	for batch: Batch in _batches.values():
		batch.material.set_shader_parameter(&"palette", _palette_texture)
		batch.material.set_shader_parameter(&"palette_size", float(n))


func _apply_pixel_limits(batch: Batch) -> void:
	batch.material.set_shader_parameter(
		&"min_px", batch.min_px if batch.min_px >= 0.0 else min_pixel_size)
	batch.material.set_shader_parameter(
		&"max_px", batch.max_px if batch.max_px >= 0.0 else max_pixel_size)


func _push_pixel_limits() -> void:
	for batch: Batch in _batches.values():
		_apply_pixel_limits(batch)


func _read_zoom() -> float:
	if zoom_override > 0.0:
		return zoom_override
	if camera != null and is_instance_valid(camera):
		return camera.zoom.x
	if is_inside_tree():
		return get_viewport().canvas_transform.get_scale().x
	return _zoom


func _set_zoom(value: float) -> void:
	if is_equal_approx(value, _zoom):
		return
	_zoom = value
	for batch: Batch in _batches.values():
		batch.material.set_shader_parameter(&"zoom", _zoom)
