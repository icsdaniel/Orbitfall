extends GdUnitTestSuite
## M0.4: BatchRenderer buffer layout, capacity growth and pixel size clamp.

const F: int = BatchRenderer.FLOATS_PER_INSTANCE


func _make_renderer() -> BatchRenderer:
	var renderer: BatchRenderer = auto_free(BatchRenderer.new())
	add_child(renderer)
	return renderer


func test_write_instance_uses_native_2d_layout() -> void:
	var data: PackedFloat32Array = PackedFloat32Array()
	data.resize(2 * F)
	var xform: Transform2D = Transform2D(Vector2(1.0, 2.0), Vector2(3.0, 4.0), Vector2(5.0, 6.0))
	BatchRenderer.write_instance(
		data, 1, xform, Color(0.1, 0.2, 0.3, 0.4),
		BatchRenderer.make_custom(BatchRenderer.Shape.STAR, BatchRenderer.Tier.PINK, 5, 0.25))
	# Instance 0 untouched.
	for i: int in F:
		assert_float(data[i]).is_equal(0.0)
	# x.x, y.x, pad, origin.x, x.y, y.y, pad, origin.y
	var expected: PackedFloat32Array = PackedFloat32Array([
		1.0, 3.0, 0.0, 5.0, 2.0, 4.0, 0.0, 6.0,
		0.1, 0.2, 0.3, 0.4,
		4.0, 4.0, 5.0, 0.25])
	for i: int in F:
		assert_float(data[F + i]).is_equal_approx(expected[i], 0.00001)


func test_layout_matches_rendering_server() -> void:
	# The headless (dummy) rendering server stores no multimesh data; the cross-check
	# against the engine only runs with a real renderer (e.g. under xvfb).
	if DisplayServer.get_name() == "headless":
		return
	var renderer: BatchRenderer = _make_renderer()
	renderer.define_archetype(0, 8.0)
	var capacity: int = renderer.get_capacity(0)
	var data: PackedFloat32Array = PackedFloat32Array()
	data.resize(capacity * F)
	var xform: Transform2D = Transform2D(0.5, Vector2(2.0, 3.0), 0.0, Vector2(70.0, -40.0))
	var color: Color = Color(0.25, 0.5, 0.75, 1.0)
	var custom: Color = BatchRenderer.make_custom(BatchRenderer.Shape.DISC, 3, 2, 0.5)
	BatchRenderer.write_instance(data, 5, xform, color, custom)
	renderer.set_batch(0, data, 6)
	var mm: RID = renderer.get_multimesh_rid(0)
	assert_bool(RenderingServer.multimesh_instance_get_transform_2d(mm, 5).is_equal_approx(xform)).is_true()
	assert_bool(RenderingServer.multimesh_instance_get_color(mm, 5).is_equal_approx(color)).is_true()
	assert_bool(RenderingServer.multimesh_instance_get_custom_data(mm, 5).is_equal_approx(custom)).is_true()
	assert_int(RenderingServer.multimesh_get_visible_instances(mm)).is_equal(6)


func test_grown_capacity_doubles_only_when_needed() -> void:
	var m: int = BatchRenderer.MIN_CAPACITY
	assert_int(BatchRenderer.grown_capacity(0, 1)).is_equal(m)
	assert_int(BatchRenderer.grown_capacity(m, m)).is_equal(m)
	assert_int(BatchRenderer.grown_capacity(m, m + 1)).is_equal(2 * m)
	assert_int(BatchRenderer.grown_capacity(m, 5 * m)).is_equal(8 * m)
	assert_int(BatchRenderer.grown_capacity(8 * m, 3)).is_equal(8 * m)
	assert_int(BatchRenderer.grown_capacity(64, 20000)).is_equal(32768)


func test_set_batch_grows_and_tracks_count() -> void:
	var renderer: BatchRenderer = _make_renderer()
	renderer.define_archetype(7, 16.0)
	assert_bool(renderer.has_archetype(7)).is_true()
	assert_bool(renderer.has_archetype(8)).is_false()
	assert_int(renderer.get_capacity(7)).is_equal(BatchRenderer.MIN_CAPACITY)
	assert_int(renderer.get_count(7)).is_equal(0)

	var data: PackedFloat32Array = PackedFloat32Array()
	data.resize(10 * F)
	renderer.set_batch(7, data, 10)
	assert_int(renderer.get_capacity(7)).is_equal(BatchRenderer.MIN_CAPACITY)
	assert_int(renderer.get_count(7)).is_equal(10)

	data.resize(300 * F)
	renderer.set_batch(7, data, 300)
	assert_int(renderer.get_capacity(7)).is_equal(512)
	assert_int(renderer.get_count(7)).is_equal(300)

	# Shrinking the count keeps the capacity.
	renderer.set_batch(7, data, 20)
	assert_int(renderer.get_capacity(7)).is_equal(512)
	assert_int(renderer.get_count(7)).is_equal(20)

	# Count is clamped to what the buffer holds; the caller's buffer is not resized.
	renderer.set_batch(7, data, 100000)
	assert_int(renderer.get_count(7)).is_equal(300)
	assert_int(data.size()).is_equal(300 * F)

	renderer.set_batch(7, PackedFloat32Array(), 0)
	assert_int(renderer.get_count(7)).is_equal(0)


func test_reserve_and_capacity_sized_buffer() -> void:
	var renderer: BatchRenderer = _make_renderer()
	renderer.define_archetype(0, 4.0)
	renderer.reserve(0, 1000)
	assert_int(renderer.get_capacity(0)).is_equal(1024)
	var data: PackedFloat32Array = PackedFloat32Array()
	data.resize(renderer.get_capacity(0) * F)
	renderer.set_batch(0, data, 1000)
	assert_int(renderer.get_capacity(0)).is_equal(1024)
	assert_int(renderer.get_count(0)).is_equal(1000)


func test_palette_is_built_in_code_with_eight_tiers() -> void:
	var renderer: BatchRenderer = _make_renderer()
	var image: Image = renderer.get_palette_image()
	assert_int(image.get_width()).is_equal(8)
	assert_int(image.get_height()).is_equal(1)
	var red: Color = image.get_pixel(BatchRenderer.Tier.RED, 0)
	assert_bool(red.r > red.g and red.r > red.b).is_true()
	var blue: Color = image.get_pixel(BatchRenderer.Tier.BLUE, 0)
	assert_bool(blue.b > blue.r and blue.b > blue.g).is_true()
	assert_bool(image.get_pixel(BatchRenderer.Tier.BLACK, 0).v < 0.2).is_true()
	assert_bool(image.get_pixel(BatchRenderer.Tier.WHITE, 0).v > 0.9).is_true()


func test_pixel_size_clamp() -> void:
	assert_float(BatchRenderer.pixel_size(4.0, 0.05, 3.0, 64.0)).is_equal(3.0)
	assert_float(BatchRenderer.pixel_size(4.0, 2.0, 3.0, 64.0)).is_equal(8.0)
	assert_float(BatchRenderer.pixel_size(4.0, 20.0, 3.0, 64.0)).is_equal(64.0)
	# A max below the min never wins over the minimum.
	assert_float(BatchRenderer.pixel_size(4.0, 1.0, 6.0, 2.0)).is_equal(6.0)


func test_zoom_override_is_used() -> void:
	var renderer: BatchRenderer = _make_renderer()
	renderer.zoom_override = 0.5
	renderer._process(0.016)
	assert_float(renderer.get_zoom()).is_equal(0.5)
