extends GdUnitTestSuite
## M0.7: OrbitCamera zoom clamping, log-space easing, focus point and input.

const DT: float = 1.0 / 60.0

var _emitted: Array[float] = []


func _make_camera() -> OrbitCamera:
	var camera: OrbitCamera = auto_free(OrbitCamera.new())
	add_child(camera)
	_emitted.clear()
	camera.zoom_changed.connect(_on_zoom_changed)
	return camera


func _on_zoom_changed(z: float) -> void:
	_emitted.append(z)


func _settle(camera: OrbitCamera, max_steps: int = 600) -> int:
	var steps: int = 0
	while camera.is_zooming() and steps < max_steps:
		camera.advance(DT)
		steps += 1
	return steps


func test_target_is_clamped_to_range() -> void:
	var camera: OrbitCamera = _make_camera()
	camera.set_zoom_target(1000.0)
	assert_float(camera.get_zoom_target()).is_equal(20.0)
	camera.set_zoom_target(0.0001)
	assert_float(camera.get_zoom_target()).is_equal(0.05)
	camera.set_zoom_target(1.0)
	camera.zoom_by(1.0e6, Vector2.ZERO)
	assert_float(camera.get_zoom_target()).is_equal(20.0)
	camera.zoom_by(1.0e-9, Vector2.ZERO)
	assert_float(camera.get_zoom_target()).is_equal(0.05)
	# Non-positive factors are ignored.
	camera.zoom_by(0.0, Vector2.ZERO)
	camera.zoom_by(-2.0, Vector2.ZERO)
	assert_float(camera.get_zoom_target()).is_equal(0.05)


func test_full_range_is_reached_smoothly() -> void:
	var camera: OrbitCamera = _make_camera()
	camera.snap_zoom(0.05)
	assert_float(camera.zoom.x).is_equal_approx(0.05, 0.000001)
	camera.set_zoom_target(20.0)
	var previous: float = camera.get_zoom_value()
	var steps: int = 0
	while camera.is_zooming() and steps < 600:
		camera.advance(DT)
		var z: float = camera.get_zoom_value()
		# Monotonic, never overshoots, and no jump larger than the easing allows.
		assert_bool(z > previous and z <= 20.0).is_true()
		assert_bool(z / previous < 3.5).is_true()
		previous = z
		steps += 1
	assert_float(camera.get_zoom_value()).is_equal(20.0)
	assert_float(camera.zoom.x).is_equal(20.0)
	assert_float(camera.zoom.y).is_equal(20.0)
	assert_bool(steps > 10 and steps < 600).is_true()
	camera.set_zoom_target(0.05)
	_settle(camera)
	assert_float(camera.zoom.x).is_equal_approx(0.05, 0.000001)


func test_easing_is_exponential_in_log_space() -> void:
	var camera: OrbitCamera = _make_camera()
	camera.zoom_rate = 10.0
	camera.snap_zoom(1.0)
	camera.set_zoom_target(16.0)
	camera.advance(0.1)
	var t: float = 1.0 - exp(-10.0 * 0.1)
	assert_float(camera.get_zoom_value()).is_equal_approx(pow(16.0, t), 0.0001)
	# Zooming out by the same ratio takes the mirrored path.
	camera.snap_zoom(1.0)
	camera.set_zoom_target(1.0 / 16.0)
	camera.advance(0.1)
	assert_float(camera.get_zoom_value()).is_equal_approx(pow(1.0 / 16.0, t), 0.0001)
	# Frame-rate independent: two half steps equal one full step.
	camera.snap_zoom(1.0)
	camera.set_zoom_target(16.0)
	camera.advance(0.05)
	camera.advance(0.05)
	assert_float(camera.get_zoom_value()).is_equal_approx(pow(16.0, t), 0.0001)


func test_zero_delta_does_not_move() -> void:
	var camera: OrbitCamera = _make_camera()
	camera.set_zoom_target(4.0)
	camera.advance(0.0)
	assert_float(camera.get_zoom_value()).is_equal(1.0)
	assert_int(_emitted.size()).is_equal(0)


func test_zoom_changed_emits_each_step_and_final_value() -> void:
	var camera: OrbitCamera = _make_camera()
	camera.set_zoom_target(2.0)
	var steps: int = _settle(camera)
	assert_int(_emitted.size()).is_equal(steps)
	assert_float(_emitted[_emitted.size() - 1]).is_equal(2.0)
	# Settled: no more signals.
	camera.advance(DT)
	assert_int(_emitted.size()).is_equal(steps)


func test_focus_point_stays_fixed_while_zooming() -> void:
	var camera: OrbitCamera = _make_camera()
	camera.global_position = Vector2(300.0, -120.0)
	var focus: Vector2 = Vector2(200.0, 150.0)
	var world_before: Vector2 = camera.screen_to_world(focus)
	camera.zoom_by(6.0, focus)
	for i: int in 5:
		camera.advance(DT)
		assert_bool(camera.screen_to_world(focus).is_equal_approx(world_before)).is_true()
	_settle(camera)
	assert_float(camera.get_zoom_value()).is_equal(6.0)
	assert_bool(camera.screen_to_world(focus).is_equal_approx(world_before)).is_true()
	assert_bool(camera.global_position.is_equal_approx(Vector2(300.0, -120.0))).is_false()


func test_set_zoom_target_keeps_the_centre() -> void:
	var camera: OrbitCamera = _make_camera()
	camera.global_position = Vector2(50.0, 60.0)
	camera.zoom_by(2.0, Vector2(10.0, 10.0))
	camera.set_zoom_target(8.0)
	_settle(camera)
	assert_bool(camera.global_position.is_equal_approx(Vector2(50.0, 60.0))).is_true()


func test_wheel_and_magnify_input() -> void:
	var camera: OrbitCamera = _make_camera()
	var wheel: InputEventMouseButton = InputEventMouseButton.new()
	wheel.button_index = MOUSE_BUTTON_WHEEL_UP
	wheel.pressed = true
	wheel.position = Vector2(100.0, 100.0)
	camera._unhandled_input(wheel)
	assert_float(camera.get_zoom_target()).is_equal_approx(camera.wheel_step, 0.0001)
	wheel.button_index = MOUSE_BUTTON_WHEEL_DOWN
	camera._unhandled_input(wheel)
	camera._unhandled_input(wheel)
	assert_float(camera.get_zoom_target()).is_equal_approx(1.0 / camera.wheel_step, 0.0001)
	var magnify: InputEventMagnifyGesture = InputEventMagnifyGesture.new()
	magnify.factor = 3.0
	magnify.position = Vector2(10.0, 20.0)
	camera._unhandled_input(magnify)
	assert_float(camera.get_zoom_target()).is_equal_approx(3.0 / camera.wheel_step, 0.0001)
	# Input can be left to external code.
	camera.handle_input = false
	camera._unhandled_input(magnify)
	assert_float(camera.get_zoom_target()).is_equal_approx(3.0 / camera.wheel_step, 0.0001)


func _touch(camera: OrbitCamera, index: int, pos: Vector2, pressed: bool) -> void:
	var touch: InputEventScreenTouch = InputEventScreenTouch.new()
	touch.index = index
	touch.position = pos
	touch.pressed = pressed
	camera._unhandled_input(touch)


func _drag(camera: OrbitCamera, index: int, pos: Vector2) -> void:
	var drag: InputEventScreenDrag = InputEventScreenDrag.new()
	drag.index = index
	drag.position = pos
	camera._unhandled_input(drag)


func test_two_finger_pinch_zooms_around_the_pinch_centre() -> void:
	var camera: OrbitCamera = _make_camera()
	_touch(camera, 0, Vector2(100.0, 200.0), true)
	# One finger alone never zooms or pans.
	_drag(camera, 0, Vector2(120.0, 200.0))
	assert_float(camera.get_zoom_value()).is_equal(1.0)
	assert_bool(camera.global_position.is_equal_approx(Vector2.ZERO)).is_true()
	_drag(camera, 0, Vector2(100.0, 200.0))
	_touch(camera, 1, Vector2(200.0, 200.0), true)
	var centre: Vector2 = Vector2(150.0, 200.0)
	var world_before: Vector2 = camera.screen_to_world(centre)
	# Spread both fingers symmetrically: distance 100 -> 200, centre unchanged.
	_drag(camera, 0, Vector2(75.0, 200.0))
	_drag(camera, 1, Vector2(250.0, 200.0))
	_drag(camera, 0, Vector2(50.0, 200.0))
	assert_float(camera.get_zoom_value()).is_equal_approx(2.0, 0.0001)
	assert_bool(camera.screen_to_world(centre).is_equal_approx(world_before)).is_true()
	# After lifting a finger the remaining one does nothing.
	_touch(camera, 1, Vector2(250.0, 200.0), false)
	_drag(camera, 0, Vector2(0.0, 0.0))
	assert_float(camera.get_zoom_value()).is_equal_approx(2.0, 0.0001)


func test_batch_renderer_reads_camera_zoom() -> void:
	var camera: OrbitCamera = _make_camera()
	var renderer: BatchRenderer = auto_free(BatchRenderer.new())
	add_child(renderer)
	renderer.camera = camera
	camera.snap_zoom(0.05)
	renderer._process(DT)
	assert_float(renderer.get_zoom()).is_equal_approx(0.05, 0.000001)
	camera.snap_zoom(20.0)
	renderer._process(DT)
	assert_float(renderer.get_zoom()).is_equal(20.0)
