extends ClientTestBase
## M2.11 bent aim and focus fire in the client: the SimServer block on the real
## binding and the mock (get_site_aims, get_aim_rules, get_reaim_cost,
## get_focus_points), priced re-aiming between waves, the build menu's mount
## row (touch < > and keyboard , .), placement with a chosen mount, and the
## reticles / axis lines the planet view draws from the focus points.

const M211_METHODS: Array[String] = [
	"set_aim", "get_site_aims", "get_aim_rules", "get_reaim_cost", "get_focus_points"]


func _make_game() -> Game:
	var game: Game = auto_free(Game.new())
	game.force_mock = true
	game.planet_picker_enabled = false
	add_child(game)
	var sim: MockSimServer = game.sim as MockSimServer
	sim.auto_advance = false
	sim.debug_set_stardust(5000.0)
	game.planet_view.view_zoom = game.camera.get_zoom_value()
	game.planet_view.sync()
	sim.drain_events()
	sim.clear_calls()
	return game


func _index(defs: Array, id: String) -> int:
	for def: Dictionary in defs:
		if str(def["id"]) == id:
			return int(def["index"])
	return -1


func _tap_site(game: Game, site: int) -> void:
	game.input_router.advance(1.0)
	tap(game.input_router, game.site_screen_position(site))


func _press(menu: BuildMenu, code: Key) -> void:
	menu._unhandled_key_input(key(code, true))


func test_real_and_mock_sim_have_the_m2_11_block() -> void:
	var server: SimServer = auto_free(SimServer.new())
	assert_array(server.load_content("res://content")).is_empty()
	server.new_run(3)
	var mock: MockSimServer = auto_free(MockSimServer.new())
	for method: String in M211_METHODS:
		assert_bool(server.has_method(method)).override_failure_message("SimServer lacks " + method).is_true()
		assert_bool(mock.has_method(method)).override_failure_message("mock lacks " + method).is_true()
	var rules: Dictionary = server.get_aim_rules()
	var offsets: PackedFloat32Array = rules["offsets"]
	assert_int(offsets.size()).is_equal(5)
	assert_float(offsets[4]).is_equal_approx(deg_to_rad(60.0), 0.0001)
	assert_float(float(rules["reaim_cost_frac"])).is_equal_approx(0.1, 0.0001)
	assert_bool(bool(rules["between_waves_only"])).is_true()
	assert_float(float(rules["focus_bonus"])).is_equal_approx(0.25, 0.0001)
	assert_float(float(rules["focus_cap"])).is_equal_approx(0.5, 0.0001)
	assert_float(float(rules["focus_radius"])).is_equal_approx(50.0, 0.0001)
	# Same rules through the mock.
	mock.load_content("res://content")
	mock.new_run(3, 0)
	var mock_rules: Dictionary = mock.get_aim_rules()
	assert_int((mock_rules["offsets"] as PackedFloat32Array).size()).is_equal(5)
	assert_float(float(mock_rules["focus_radius"])).is_equal_approx(50.0, 0.0001)


## Two Gatlings leaning toward lane A (30 deg) form one focus point; re-aiming
## is priced and refused mid-wave. Runs on the real binding and on the mock.
func _focus_flow(server: Object) -> void:
	var defs: Array[Dictionary] = server.get_building_defs()
	var gatling: int = _index(defs, "gatling")
	assert_bool(bool(defs[gatling]["aim_mount"])).is_true()
	server.place_building(1, gatling, deg_to_rad(30.0), 0)
	server.place_building(5, gatling, deg_to_rad(-29.0), 0)  # snaps to -30
	server.step_ticks(1)
	var aims: PackedFloat32Array = server.get_site_aims()
	assert_float(aims[5]).is_equal_approx(deg_to_rad(-30.0), 0.0001)
	var points: Array = server.get_focus_points()
	assert_int(points.size()).is_equal(1)
	var f: Dictionary = points[0]
	var local: Vector2 = f["local"]
	assert_float(rad_to_deg(local.angle())).is_equal_approx(30.0, 0.05)
	assert_float(local.length()).is_equal_approx(100.0 * sin(deg_to_rad(150.0)) / sin(deg_to_rad(10.0)), 0.5)
	assert_int((f["feeders"] as PackedInt32Array).size()).is_equal(2)
	assert_float(float(f["mult"])).is_equal_approx(1.25, 0.0001)
	# Re-aim: 10% of the Gatling's 60.
	assert_float(server.get_reaim_cost(1)).is_equal_approx(6.0, 0.001)
	assert_float(server.get_reaim_cost(20)).is_equal(-1.0)  # empty
	server.drain_events()
	var before: float = server.get_stardust()
	server.set_aim(1, 0.0)
	server.step_ticks(1)
	var changed: bool = false
	for event: Dictionary in server.drain_events():
		changed = changed or (str(event["type"]) == "AimChanged" and int(event["a"]) == 1)
	assert_bool(changed).is_true()
	assert_float(server.get_stardust()).is_less(before - 5.9)
	assert_int(server.get_focus_points().size()).is_equal(0)  # straight from 1 meets 5's axis only out of range
	server.start_wave()
	server.step_ticks(2)
	server.drain_events()
	server.set_aim(1, deg_to_rad(30.0))
	server.step_ticks(1)
	var refused: bool = false
	for event: Dictionary in server.drain_events():
		refused = refused or (str(event["type"]) == "CommandRejected" and int(event["b"]) == 7)
	assert_bool(refused).is_true()


func test_real_sim_solves_focus_and_prices_re_aiming() -> void:
	var server: SimServer = auto_free(SimServer.new())
	assert_array(server.load_content("res://content")).is_empty()
	server.new_run(3)
	_focus_flow(server)


func test_mock_sim_solves_focus_and_prices_re_aiming() -> void:
	var mock: MockSimServer = auto_free(MockSimServer.new())
	mock.auto_advance = false
	add_child(mock)
	mock.load_content("res://content")
	mock.new_run(3, 0)
	_focus_flow(mock)


func test_mount_row_sets_the_ghost_mount_by_touch_and_keys() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var menu: BuildMenu = game.hud.build_menu
	var defs: Array[Dictionary] = game.planet_view.building_defs
	_tap_site(game, 1)
	assert_bool(menu.is_open()).is_true()
	# Preview the Gatling: aim-mounted, so the mount row shows.
	menu.building_button(_index(defs, "gatling")).pressed.emit()
	assert_bool(menu.aim_row_visible()).is_true()
	assert_str(menu.aim_text()).contains("+0°")
	menu.aim_button(1).pressed.emit()  # touch: lean right
	assert_str(menu.aim_text()).contains("+30°")
	_press(menu, KEY_PERIOD)  # keyboard: lean right again
	assert_str(menu.aim_text()).contains("+60°")
	_press(menu, KEY_PERIOD)  # clamped at the last step
	assert_str(menu.aim_text()).contains("+60°")
	_press(menu, KEY_COMMA)
	assert_str(menu.aim_text()).contains("+30°")
	assert_float(game.planet_view.ghost_aim).is_equal_approx(deg_to_rad(30.0), 0.0001)
	# A panel has no mount: no row.
	menu.building_button(_index(defs, "solar_panel")).pressed.emit()
	assert_bool(menu.aim_row_visible()).is_false()
	menu.building_button(_index(defs, "gatling")).pressed.emit()
	menu.build_button().pressed.emit()
	var placed: Array[Array] = sim.calls_to(&"place_building")
	assert_int(placed.size()).is_equal(1)
	assert_float(float(placed[0][2])).is_equal_approx(deg_to_rad(30.0), 0.0001)


func test_re_aim_from_the_building_menu_between_waves_only() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var defs: Array[Dictionary] = game.planet_view.building_defs
	sim.place_building(3, _index(defs, "gatling"), 0.0)
	sim.step_ticks(1)
	game.planet_view.sync()
	sim.clear_calls()
	_tap_site(game, 3)
	var menu: BuildMenu = game.hud.build_menu
	assert_bool(menu.aim_row_visible()).is_true()
	assert_str(menu.aim_text()).contains("re-aim 6")
	_press(menu, KEY_COMMA)
	var aims: Array[Array] = sim.calls_to(&"set_aim")
	assert_int(aims.size()).is_equal(1)
	assert_int(int(aims[0][0])).is_equal(3)
	assert_float(float(aims[0][1])).is_equal_approx(deg_to_rad(-30.0), 0.0001)
	sim.step_ticks(1)
	game._process(0.0)
	assert_str(menu.aim_text()).contains("-30°")
	# Mid-wave the arrows are off and say why.
	sim.debug_set_phase(1)
	game._process(0.0)
	assert_bool(menu.aim_button(1).disabled).is_true()
	assert_str(menu.aim_text()).contains("between waves")


func test_planet_view_draws_focus_points_and_feeding_axes() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var defs: Array[Dictionary] = game.planet_view.building_defs
	sim.place_building(1, _index(defs, "gatling"), deg_to_rad(30.0))
	sim.place_building(5, _index(defs, "gatling"), deg_to_rad(-30.0))
	sim.step_ticks(1)
	game.planet_view.sync()
	assert_int(game.planet_view.focus_points.size()).is_equal(1)
	var feeders: Dictionary[int, bool] = game.planet_view.focus_feeders()
	assert_bool(feeders.has(1) and feeders.has(5)).is_true()
	assert_float(game.planet_view.site_aim(5)).is_equal_approx(deg_to_rad(-30.0), 0.0001)
	# Focus points turn with the planet: world pos = local rotated by the rotation.
	var f: Dictionary = game.planet_view.focus_points[0]
	var local: Vector2 = f["local"]
	var pos: Vector2 = f["pos"]
	assert_float(pos.distance_to(local.rotated(game.planet_view.rotation_angle))).is_less(0.01)
	game.planet_view.queue_redraw()
	await get_tree().process_frame  # draws without errors
