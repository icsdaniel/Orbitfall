extends ClientTestBase
## M2.6 towers batch 1 in the client: the SimServer block (activate_building,
## get_channel, get_beams, get_site_actives, building defs with falloff and
## actives) on the real binding and the mock, the build menu that still fits a
## landscape phone with the bigger roster (scroll, keys), the Overcharge button
## (touch and F), and the gradient range ring's falloff helper.

const PHONE_HEIGHT: float = 720.0  # the 1280x720 canvas_items base
const M26_METHODS: Array[String] = [
	"activate_building", "get_channel", "get_beams", "get_site_actives"]


func _make_game() -> Game:
	var game: Game = auto_free(Game.new())
	game.force_mock = true
	game.planet_picker_enabled = false  # M2.8
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
	var event: InputEventKey = key(code, true)
	menu._unhandled_key_input(event)


func test_real_and_mock_sim_have_the_m2_6_block() -> void:
	var server: SimServer = auto_free(SimServer.new())
	assert_array(server.load_content("res://content")).is_empty()
	server.new_run(3)
	var mock: MockSimServer = auto_free(MockSimServer.new())
	for method: String in M26_METHODS:
		assert_bool(server.has_method(method)).override_failure_message("SimServer lacks " + method).is_true()
		assert_bool(mock.has_method(method)).override_failure_message("mock lacks " + method).is_true()
	var defs: Array[Dictionary] = server.get_building_defs()
	assert_int(defs.size()).is_equal(13)  # M2.10 shield + M2.6 six + M2.7 drill, cooler + M2.4 lance
	var lens: Dictionary = defs[_index(defs, "sun_lens")]
	assert_str(str(lens["name"])).is_equal("Sun Lens")
	assert_int(int(lens["projectile_kind"])).is_equal(2)
	assert_bool(bool(lens["has_active"])).is_true()
	assert_str(str(lens["active"]["name"])).is_equal("Overcharge")
	var flak: Dictionary = defs[_index(defs, "flak")]
	var curve: PackedFloat32Array = flak["falloff"]
	assert_float(PlanetView.falloff_at(curve, 0.0)).is_greater(PlanetView.falloff_at(curve, 1.0))
	assert_int(int(defs[_index(defs, "railgun")]["pierce"])).is_greater(0)
	assert_float(float(defs[_index(defs, "cryo")]["slow"])).is_greater(0.0)


func test_real_sim_fires_the_sun_lens_and_overcharges_it() -> void:
	var server: SimServer = auto_free(SimServer.new())
	assert_array(server.load_content("res://content")).is_empty()
	server.new_run(3)
	var defs: Array[Dictionary] = server.get_building_defs()
	# A panel under the sun (0 deg) channels all the light; without panels, none.
	server.place_building(35, _index(defs, "solar_panel"), 0.0, 0)
	server.step_ticks(2)
	assert_float(server.get_channel()).is_equal_approx(1.0, 0.001)
	server.sell_building(35)  # 150 start stardust: the Lens alone, then
	# Lane A comes in at 30 deg: slot 3 of 36 faces it.
	server.place_building(3, _index(defs, "sun_lens"), 0.0, 0)
	server.step_ticks(2)
	assert_float(server.get_channel()).is_equal(0.0)
	server.start_wave()
	var beams: PackedFloat32Array = PackedFloat32Array()
	for i: int in 900:
		server.step_ticks(1)
		beams = server.get_beams()
		if not beams.is_empty():
			break
	assert_int(beams.size()).is_equal(PlanetView.BEAM_FLOATS)
	assert_int(int(beams[6])).is_equal(3)          # the site
	assert_float(beams[4]).is_greater(0.0)         # power
	server.drain_events()
	server.activate_building(3)
	server.step_ticks(1)
	var activated: bool = false
	for event: Dictionary in server.drain_events():
		activated = activated or (str(event["type"]) == "BuildingActivated" and int(event["a"]) == 3)
	assert_bool(activated).is_true()
	var actives: PackedFloat32Array = server.get_site_actives()
	assert_float(actives[6]).is_greater(0.0)  # cooldown
	assert_float(actives[7]).is_greater(0.0)  # running
	server.activate_building(3)  # not ready: refused
	server.step_ticks(1)
	var refused: bool = false
	for event: Dictionary in server.drain_events():
		refused = refused or (str(event["type"]) == "CommandRejected" and int(event["b"]) == 12)
	assert_bool(refused).is_true()


func test_build_menu_lists_every_building_and_fits_a_landscape_phone() -> void:
	var game: Game = _make_game()
	var menu: BuildMenu = game.hud.build_menu
	_tap_site(game, 9)
	assert_bool(menu.is_open()).is_true()
	var defs: Array[Dictionary] = game.sim.get_building_defs()
	for def: Dictionary in defs:
		if int(def.get("layer", 0)) != 1:
			assert_object(menu.building_button(int(def["index"]))).is_not_null()
	await get_tree().process_frame
	assert_float(menu.get_combined_minimum_size().y).is_less(PHONE_HEIGHT - 40.0)


func test_build_menu_keys_pick_and_build() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var menu: BuildMenu = game.hud.build_menu
	_tap_site(game, 9)
	var first: int = menu.previewed
	_press(menu, KEY_RIGHT)
	assert_int(menu.previewed).is_not_equal(first)
	_press(menu, KEY_LEFT)
	assert_int(menu.previewed).is_equal(first)
	_press(menu, KEY_BRACKETLEFT)  # wraps to the last building
	assert_int(menu.previewed).is_not_equal(first)
	_press(menu, KEY_BRACKETRIGHT)
	assert_int(menu.previewed).is_equal(first)
	_press(menu, KEY_ENTER)
	var placed: Array[Array] = sim.calls_to(&"place_building")
	assert_int(placed.size()).is_equal(1)
	assert_int(placed[0][1]).is_equal(first)


func test_overcharge_button_and_key_send_activate_building() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var menu: BuildMenu = game.hud.build_menu
	var lens: int = _index(sim.get_building_defs(), "sun_lens")
	sim.place_building(4, lens, 0.0, 0)
	game.planet_view.sync()
	_tap_site(game, 4)
	assert_bool(menu.is_open()).is_true()
	assert_bool(menu.active_button().visible).is_true()
	assert_bool(menu.active_button().disabled).is_false()
	sim.clear_calls()
	menu.active_button().pressed.emit()
	assert_int(sim.calls_to(&"activate_building").size()).is_equal(1)
	assert_int(sim.calls_to(&"activate_building")[0][0]).is_equal(4)
	game._refresh_active()
	assert_bool(menu.active_button().disabled).is_true()  # running, then cooling down
	assert_str(menu.active_button().text).contains("Overcharge")
	sim.drain_events()
	# Keyboard path: F (refused by the sim while it cools down, but sent).
	menu.set_active_state(0.0, 0.0)
	_press(menu, KEY_F)
	assert_int(sim.calls_to(&"activate_building").size()).is_equal(2)
	var refused: bool = false
	for event: Dictionary in sim.drain_events():
		refused = refused or (str(event["type"]) == "CommandRejected" and int(event["b"]) == 12)
	assert_bool(refused).is_true()
	# A tower without an active shows no button.
	sim.place_building(8, _index(sim.get_building_defs(), "gatling"), 0.0, 0)
	game.planet_view.sync()
	_tap_site(game, 8)
	assert_bool(menu.active_button().visible).is_false()


func test_falloff_helper_matches_the_curve() -> void:
	var curve: PackedFloat32Array = PackedFloat32Array([0.0, 1.6, 0.5, 1.0, 1.0, 0.5])
	assert_float(PlanetView.falloff_at(curve, 0.0)).is_equal_approx(1.6, 0.0001)
	assert_float(PlanetView.falloff_at(curve, 0.25)).is_equal_approx(1.3, 0.0001)
	assert_float(PlanetView.falloff_at(curve, 0.75)).is_equal_approx(0.75, 0.0001)
	assert_float(PlanetView.falloff_at(curve, 1.0)).is_equal_approx(0.5, 0.0001)
	assert_float(PlanetView.falloff_at(PackedFloat32Array(), 0.5)).is_equal(1.0)
