extends Node2D
## M0.4 demo: animates N instances through BatchRenderer with an FPS label.
## M0.7: OrbitCamera zoom with mouse wheel or pinch; +/- keys step the zoom target.
## User args (after `--`): --count=N (default 20000), --zoom=Z, --grid (static legend
## of every shape, tier and trait), --screenshot <path> (save a PNG after a few
## frames and quit), --frames=N (quit after N frames and print timings).

const ARCHETYPE_SHAPES: int = 0
const ARCHETYPE_DOTS: int = 1
const SHAPE_SIZE: float = 26.0
const DOT_SIZE: float = 5.0
const FIELD_RADIUS: float = 1400.0
const SCREENSHOT_FRAME: int = 12
const F: int = BatchRenderer.FLOATS_PER_INSTANCE

var _renderer: BatchRenderer
var _camera: OrbitCamera
var _label: Label
var _count: int = 20000
var _grid: bool = false
var _screenshot_path: String = ""
var _quit_after_frames: int = 0
var _start_zoom: float = 0.25

var _shape_count: int = 0
var _dot_count: int = 0
var _shape_data: PackedFloat32Array = PackedFloat32Array()
var _dot_data: PackedFloat32Array = PackedFloat32Array()
var _radius: PackedFloat32Array = PackedFloat32Array()
var _phase: PackedFloat32Array = PackedFloat32Array()
var _speed: PackedFloat32Array = PackedFloat32Array()
var _scale: PackedFloat32Array = PackedFloat32Array()

var _time: float = 0.0
var _frame: int = 0
var _fill_usec: int = 0
var _upload_usec: int = 0
var _timed_frames: int = 0
var _total_fill_usec: int = 0
var _total_upload_usec: int = 0
var _label_timer: float = 0.0


func _ready() -> void:
	_parse_args()
	_renderer = BatchRenderer.new()
	add_child(_renderer)
	_renderer.define_archetype(ARCHETYPE_SHAPES, SHAPE_SIZE)
	_renderer.define_archetype(ARCHETYPE_DOTS, DOT_SIZE)
	_setup_camera()
	var layer: CanvasLayer = CanvasLayer.new()
	add_child(layer)
	_label = Label.new()
	_label.position = Vector2(8.0, 6.0)
	layer.add_child(_label)
	if _grid:
		_build_grid()
	else:
		_build_field()


func _setup_camera() -> void:
	_camera = OrbitCamera.new()
	add_child(_camera)
	_camera.make_current()
	_camera.snap_zoom(_start_zoom)
	_renderer.camera = _camera


func _unhandled_key_input(event: InputEvent) -> void:
	var key: InputEventKey = event as InputEventKey
	if key == null or not key.pressed:
		return
	if key.keycode == KEY_EQUAL or key.keycode == KEY_KP_ADD:
		_camera.set_zoom_target(_camera.get_zoom_target() * 2.0)
	elif key.keycode == KEY_MINUS or key.keycode == KEY_KP_SUBTRACT:
		_camera.set_zoom_target(_camera.get_zoom_target() * 0.5)


func _parse_args() -> void:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	var i: int = 0
	while i < args.size():
		var arg: String = args[i]
		if arg.begins_with("--count="):
			_count = maxi(arg.get_slice("=", 1).to_int(), 1)
		elif arg.begins_with("--zoom="):
			_start_zoom = maxf(arg.get_slice("=", 1).to_float(), 0.001)
		elif arg.begins_with("--frames="):
			_quit_after_frames = arg.get_slice("=", 1).to_int()
		elif arg == "--grid":
			_grid = true
			_start_zoom = 1.0
		elif arg == "--screenshot" and i + 1 < args.size():
			i += 1
			_screenshot_path = args[i]
		i += 1


## Static legend: rows = shapes, columns = 8 tiers x (plain, camo, armored, shielded).
func _build_grid() -> void:
	var traits: PackedInt32Array = PackedInt32Array([
		0, BatchRenderer.TRAIT_CAMO, BatchRenderer.TRAIT_ARMORED, BatchRenderer.TRAIT_SHIELDED])
	var tiers: int = _renderer.palette_colors.size()
	var cols: int = tiers * traits.size()
	_shape_count = cols * BatchRenderer.SHAPE_COUNT
	_shape_data.resize(_shape_count * F)
	var spacing: Vector2 = Vector2(39.0, 62.0)
	var origin: Vector2 = -0.5 * Vector2(float(cols - 1), float(BatchRenderer.SHAPE_COUNT)) * spacing
	var index: int = 0
	for shape: int in BatchRenderer.SHAPE_COUNT:
		for col: int in cols:
			@warning_ignore("integer_division")
			var trait_flags: int = traits[col / tiers]
			var pos: Vector2 = origin + Vector2(float(col), float(shape)) * spacing
			BatchRenderer.write_instance(
				_shape_data, index, Transform2D(0.0, pos), Color.WHITE,
				BatchRenderer.make_custom(shape, col % tiers, trait_flags))
			index += 1
	_renderer.set_batch(ARCHETYPE_SHAPES, _shape_data, _shape_count)
	# Bottom row: instance-colour dots (tier < 0) at world sizes below the minimum pixel size.
	_dot_count = cols
	_dot_data.resize(_dot_count * F)
	for col: int in cols:
		var pos: Vector2 = origin + Vector2(float(col), float(BatchRenderer.SHAPE_COUNT)) * spacing
		var s: float = 0.1 + 0.2 * float(col)
		BatchRenderer.write_instance(
			_dot_data, col, Transform2D(0.0, Vector2(s, s), 0.0, pos),
			Color.from_hsv(float(col) / float(cols), 0.7, 1.0),
			BatchRenderer.make_custom(BatchRenderer.Shape.CIRCLE, -1))
	_renderer.set_batch(ARCHETYPE_DOTS, _dot_data, _dot_count)


func _build_field() -> void:
	var rng: RandomNumberGenerator = RandomNumberGenerator.new()
	rng.seed = 20261112
	@warning_ignore("integer_division")
	_dot_count = _count / 4
	_shape_count = _count - _dot_count
	# Buffers sized to the renderer capacity take the zero-copy upload path.
	_renderer.reserve(ARCHETYPE_SHAPES, _shape_count)
	_renderer.reserve(ARCHETYPE_DOTS, _dot_count)
	_shape_data.resize(_renderer.get_capacity(ARCHETYPE_SHAPES) * F)
	_dot_data.resize(_renderer.get_capacity(ARCHETYPE_DOTS) * F)
	_radius.resize(_count)
	_phase.resize(_count)
	_speed.resize(_count)
	_scale.resize(_count)
	var tiers: int = _renderer.palette_colors.size()
	for i: int in _count:
		var r: float = FIELD_RADIUS * sqrt(rng.randf_range(0.02, 1.0))
		_radius[i] = r
		_phase[i] = rng.randf() * TAU
		_speed[i] = rng.randf_range(0.05, 0.4) * (300.0 / (r + 100.0)) * (1.0 if i % 2 == 0 else -1.0)
		_scale[i] = rng.randf_range(0.7, 1.6)
		if i < _shape_count:
			var traits: int = 0
			if rng.randf() < 0.3:
				traits = 1 << rng.randi_range(0, 2)
			BatchRenderer.write_instance(
				_shape_data, i, Transform2D.IDENTITY, Color.WHITE,
				BatchRenderer.make_custom(i % BatchRenderer.SHAPE_COUNT, rng.randi_range(0, tiers - 1), traits))
		else:
			BatchRenderer.write_instance(
				_dot_data, i - _shape_count, Transform2D.IDENTITY,
				Color.from_hsv(rng.randf_range(0.05, 0.16), 0.8, 1.0),
				BatchRenderer.make_custom(BatchRenderer.Shape.CIRCLE, -1))


func _process(delta: float) -> void:
	_frame += 1
	if not _grid:
		_time += delta
		var t0: int = Time.get_ticks_usec()
		_fill(_shape_data, 0, _shape_count)
		_fill(_dot_data, _shape_count, _dot_count)
		var t1: int = Time.get_ticks_usec()
		_renderer.set_batch(ARCHETYPE_SHAPES, _shape_data, _shape_count)
		_renderer.set_batch(ARCHETYPE_DOTS, _dot_data, _dot_count)
		var t2: int = Time.get_ticks_usec()
		_fill_usec = t1 - t0
		_upload_usec = t2 - t1
		if _frame > 3:
			_timed_frames += 1
			_total_fill_usec += _fill_usec
			_total_upload_usec += _upload_usec
	_label_timer -= delta
	if _label_timer <= 0.0:
		_label_timer = 0.25
		_label.text = "%d fps | %d instances | fill %.2f ms | upload %.2f ms | zoom %.2f" % [
			int(Engine.get_frames_per_second()), _shape_count + _dot_count,
			float(_fill_usec) / 1000.0, float(_upload_usec) / 1000.0, _camera.zoom.x]
	if _screenshot_path != "" and _frame == SCREENSHOT_FRAME:
		_save_screenshot()
	elif _quit_after_frames > 0 and _frame >= _quit_after_frames:
		_finish()


## Rewrites only the transform floats; colour and custom data stay as built.
func _fill(data: PackedFloat32Array, first: int, count: int) -> void:
	var t: float = _time
	for j: int in count:
		var i: int = first + j
		var a: float = _phase[i] + t * _speed[i]
		var c: float = cos(a)
		var s: float = sin(a)
		var r: float = _radius[i]
		var k: float = _scale[i]
		var o: int = j * F
		data[o] = c * k
		data[o + 1] = -s * k
		data[o + 3] = c * r
		data[o + 4] = s * k
		data[o + 5] = c * k
		data[o + 7] = s * r


func _save_screenshot() -> void:
	await RenderingServer.frame_post_draw
	var image: Image = get_viewport().get_texture().get_image()
	var err: Error = image.save_png(_screenshot_path)
	print("render_demo: screenshot %s -> %s" % [_screenshot_path, error_string(err)])
	_finish()


func _finish() -> void:
	if _timed_frames > 0:
		print("render_demo: %d instances, %d frames, avg fill %.3f ms, avg upload %.3f ms, fps %.1f" % [
			_shape_count + _dot_count, _timed_frames,
			float(_total_fill_usec) / float(_timed_frames) / 1000.0,
			float(_total_upload_usec) / float(_timed_frames) / 1000.0,
			Engine.get_frames_per_second()])
	get_tree().quit()
