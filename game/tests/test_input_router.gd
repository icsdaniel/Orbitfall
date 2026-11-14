extends ClientTestBase
## M1.2: one test per gesture. Synthetic InputEvents go into InputRouter; the mock
## SimServer records which contract commands arrived.

var _taps: Array[Vector2] = []
var _long: Array[Vector2] = []
var _double: Array[Vector2] = []
var _zooms: Array[float] = []
var _debug_toggles: int = 0


func _wire(router: InputRouter) -> void:
	_taps.clear()
	_long.clear()
	_double.clear()
	_zooms.clear()
	_debug_toggles = 0
	router.tapped.connect(func(p: Vector2) -> void: _taps.append(p))
	router.long_pressed.connect(func(p: Vector2) -> void: _long.append(p))
	router.double_tapped.connect(func(p: Vector2) -> void: _double.append(p))
	router.zoom_requested.connect(func(f: float, _p: Vector2) -> void: _zooms.append(f))
	router.debug_toggled.connect(func() -> void: _debug_toggles += 1)


## Drags a finger along a circle of `radius` around the router's planet centre,
## from angle a0 to a1 (rad) in `steps` frames of `dt` seconds, finger held.
func _circle_drag(router: InputRouter, radius: float, a0: float, a1: float,
		steps: int, dt: float) -> void:
	router.handle_event(touch(0, router.planet_centre + Vector2.from_angle(a0) * radius, true))
	for i: int in steps:
		var a: float = lerpf(a0, a1, float(i + 1) / float(steps))
		router.handle_event(drag(0, router.planet_centre + Vector2.from_angle(a) * radius))
		router.advance(dt)


func test_grab_clockwise_spins_positive_at_the_finger_speed() -> void:
	var sim: MockSimServer = make_sim()
	var router: InputRouter = make_router(sim)
	_wire(router)
	# 45 deg/s on screen (clockwise, y down) is half of the 90 deg/s full speed.
	_circle_drag(router, 250.0, 0.0, deg_to_rad(45.0), 60, 1.0 / 60.0)
	assert_float(last_rotate(sim)).is_equal_approx(0.5, 0.03)
	router.handle_event(touch(0, router.planet_centre + Vector2.from_angle(deg_to_rad(45.0)) * 250.0, false))
	assert_float(last_rotate(sim)).is_equal(0.0)
	assert_int(_taps.size()).is_equal(0)


func test_grab_works_on_both_halves_and_counter_clockwise_is_negative() -> void:
	var sim: MockSimServer = make_sim()
	var router: InputRouter = make_router(sim)
	# Right of the planet, finger moving up = counter-clockwise on screen.
	_circle_drag(router, 300.0, 0.0, deg_to_rad(-60.0), 30, 1.0 / 60.0)
	assert_float(last_rotate(sim)).is_equal(-1.0)


func test_grab_holding_still_releases_the_input() -> void:
	var sim: MockSimServer = make_sim()
	var router: InputRouter = make_router(sim)
	_circle_drag(router, 250.0, 0.0, deg_to_rad(30.0), 20, 1.0 / 60.0)
	assert_float(last_rotate(sim)).is_greater(0.1)
	for i: int in 30:
		router.advance(1.0 / 60.0)
	assert_float(last_rotate(sim)).is_equal(0.0)


func test_grab_near_the_centre_uses_horizontal_motion() -> void:
	var sim: MockSimServer = make_sim()
	var router: InputRouter = make_router(sim)
	var c: Vector2 = router.planet_centre
	router.handle_event(touch(0, c, true))
	for i: int in 10:
		router.handle_event(drag(0, c + Vector2(float(i + 1) * 6.0, 0.0)))
		router.advance(1.0 / 60.0)
	assert_float(last_rotate(sim)).is_greater(0.0)


func test_rotate_input_is_quantised_and_only_sent_on_change() -> void:
	var sim: MockSimServer = make_sim()
	var router: InputRouter = make_router(sim)
	_circle_drag(router, 250.0, 0.0, deg_to_rad(120.0), 60, 1.0 / 60.0)
	var calls: int = sim.calls_to(&"rotate_input").size()
	assert_int(calls).is_less(60)
	for call: Array in sim.calls_to(&"rotate_input"):
		var v: float = call[0]
		assert_float(absf(v * 50.0 - roundf(v * 50.0))).is_less(0.001)


func test_tap_emits_tapped_with_position_and_no_command() -> void:
	var sim: MockSimServer = make_sim()
	var router: InputRouter = make_router(sim)
	_wire(router)
	tap(router, Vector2(900.0, 300.0))
	assert_int(_taps.size()).is_equal(1)
	assert_vector(_taps[0]).is_equal(Vector2(900.0, 300.0))
	assert_int(sim.calls.size()).is_equal(0)
	assert_int(router.tap_count).is_equal(1)


func test_long_press_emits_long_pressed_and_not_tapped() -> void:
	var sim: MockSimServer = make_sim()
	var router: InputRouter = make_router(sim)
	_wire(router)
	router.handle_event(touch(0, Vector2(900.0, 300.0), true))
	router.advance(router.long_press_seconds * 0.5)
	assert_int(_long.size()).is_equal(0)
	router.advance(router.long_press_seconds * 0.6)
	assert_int(_long.size()).is_equal(1)
	router.handle_event(touch(0, Vector2(900.0, 300.0), false))
	assert_int(_taps.size()).is_equal(0)


func test_double_tap_emits_double_tapped() -> void:
	var sim: MockSimServer = make_sim()
	var router: InputRouter = make_router(sim)
	_wire(router)
	tap(router, Vector2(700.0, 300.0))
	router.advance(0.1)
	tap(router, Vector2(705.0, 304.0))
	assert_int(_taps.size()).is_equal(1)
	assert_int(_double.size()).is_equal(1)


func test_two_slow_taps_are_two_taps() -> void:
	var sim: MockSimServer = make_sim()
	var router: InputRouter = make_router(sim)
	_wire(router)
	tap(router, Vector2(700.0, 300.0))
	router.advance(router.double_tap_seconds + 0.2)
	tap(router, Vector2(700.0, 300.0))
	assert_int(_taps.size()).is_equal(2)
	assert_int(_double.size()).is_equal(0)


func test_pinch_requests_zoom_cancels_rotation_and_ends() -> void:
	var sim: MockSimServer = make_sim()
	var router: InputRouter = make_router(sim)
	_wire(router)
	var ended: Array[int] = [0]
	router.pinch_ended.connect(func() -> void: ended[0] += 1)
	router.handle_event(touch(0, Vector2(500.0, 200.0), true))
	router.handle_event(drag(0, Vector2(500.0, 360.0)))
	router.advance(1.0 / 60.0)
	assert_float(absf(last_rotate(sim))).is_greater(0.0)
	router.handle_event(touch(1, Vector2(900.0, 360.0), true))
	assert_float(last_rotate(sim)).is_equal(0.0)
	router.handle_event(drag(1, Vector2(1300.0, 360.0)))
	assert_int(_zooms.size()).is_equal(1)
	assert_float(_zooms[0]).is_equal_approx(2.0, 0.001)
	router.handle_event(drag(1, Vector2(900.0, 360.0)))
	assert_float(_zooms[1]).is_equal_approx(0.5, 0.001)
	router.handle_event(touch(1, Vector2(900.0, 360.0), false))
	assert_int(ended[0]).is_equal(1)
	router.handle_event(touch(0, Vector2(500.0, 360.0), false))
	assert_int(_taps.size()).is_equal(0)


func test_wheel_steps_between_views() -> void:
	var sim: MockSimServer = make_sim()
	var router: InputRouter = make_router(sim)
	_wire(router)
	var steps: Array[int] = []
	router.view_step_requested.connect(func(d: int) -> void: steps.append(d))
	router.handle_event(wheel(MOUSE_BUTTON_WHEEL_UP, Vector2(640.0, 360.0)))
	router.handle_event(wheel(MOUSE_BUTTON_WHEEL_DOWN, Vector2(640.0, 360.0)))
	assert_array(steps).is_equal([1, -1])
	assert_int(_zooms.size()).is_equal(0)


func test_keys_a_d_q_e_rotate() -> void:
	var sim: MockSimServer = make_sim()
	var router: InputRouter = make_router(sim)
	router.handle_event(key(KEY_D, true))
	assert_float(last_rotate(sim)).is_equal(1.0)
	router.handle_event(key(KEY_D, false))
	assert_float(last_rotate(sim)).is_equal(0.0)
	router.handle_event(key(KEY_A, true))
	assert_float(last_rotate(sim)).is_equal(-1.0)
	router.handle_event(key(KEY_A, false))
	router.handle_event(key(KEY_E, true))
	assert_float(last_rotate(sim)).is_equal(1.0)
	router.handle_event(key(KEY_E, false))
	router.handle_event(key(KEY_Q, true))
	assert_float(last_rotate(sim)).is_equal(-1.0)
	router.handle_event(key(KEY_Q, false))
	assert_float(last_rotate(sim)).is_equal(0.0)


func test_space_starts_the_wave_only_in_intermission() -> void:
	var sim: MockSimServer = make_sim()
	var router: InputRouter = make_router(sim)
	router.handle_event(key(KEY_SPACE, true))
	assert_int(sim.calls_to(&"start_wave").size()).is_equal(1)
	assert_int(sim.get_phase()).is_equal(MockSimServer.PHASE_WAVE)
	router.handle_event(key(KEY_SPACE, false))
	router.handle_event(key(KEY_SPACE, true))
	assert_int(sim.calls_to(&"start_wave").size()).is_equal(1)


func test_backtick_toggles_debug() -> void:
	var sim: MockSimServer = make_sim()
	var router: InputRouter = make_router(sim)
	_wire(router)
	router.handle_event(key(KEY_QUOTELEFT, true))
	assert_int(_debug_toggles).is_equal(1)
	assert_int(sim.calls.size()).is_equal(0)


func test_touch_over_ui_is_ignored() -> void:
	var sim: MockSimServer = make_sim()
	var router: InputRouter = make_router(sim)
	_wire(router)
	router.ui_blocker = func(pos: Vector2) -> bool: return pos.y > 600.0
	router.handle_event(touch(0, Vector2(100.0, 650.0), true))
	router.handle_event(drag(0, Vector2(400.0, 650.0)))
	router.handle_event(touch(0, Vector2(400.0, 650.0), false))
	assert_int(sim.calls.size()).is_equal(0)
	assert_int(_taps.size()).is_equal(0)


func test_rotate_buttons_send_rotate_input() -> void:
	var sim: MockSimServer = make_sim()
	var router: InputRouter = make_router(sim)
	router.set_button_rotation(-1.0)
	assert_float(last_rotate(sim)).is_equal(-1.0)
	router.set_button_rotation(0.0)
	assert_float(last_rotate(sim)).is_equal(0.0)


func test_rotate_command_turns_the_planet() -> void:
	var sim: MockSimServer = make_sim()
	var router: InputRouter = make_router(sim)
	router.handle_event(key(KEY_D, true))
	sim.step_ticks(30)
	assert_float(sim.get_planet_rotation()).is_greater(0.5)
	assert_float(sim.get_planet_angular_velocity()).is_equal_approx(deg_to_rad(90.0), 0.01)
