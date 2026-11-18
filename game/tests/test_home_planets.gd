extends ClientTestBase
## M2.8 home planets: the picker (touch / click / keyboard), the planet choice
## reaching new_run, Build/Watch views that fit every planet size, and the real
## SimServer's planet API.


func _make_game(picker: bool = true) -> Game:
	var game: Game = auto_free(Game.new())
	game.force_mock = true
	game.planet_picker_enabled = picker
	add_child(game)
	var sim: MockSimServer = game.sim as MockSimServer
	sim.auto_advance = false
	sim.clear_calls()
	return game


func _last_new_run(sim: MockSimServer) -> Array:
	var runs: Array[Array] = sim.calls_to(&"new_run")
	return runs[runs.size() - 1] if not runs.is_empty() else []


func test_mock_lists_three_planets_with_their_stats() -> void:
	var sim: MockSimServer = make_sim()
	var defs: Array[Dictionary] = sim.get_planet_defs()
	assert_int(defs.size()).is_equal(3)
	var by_id: Dictionary = {}
	for d: Dictionary in defs:
		by_id[str(d["id"])] = d
	assert_bool(bool(by_id["medium"]["default"])).is_true()
	assert_float(float(by_id["small"]["rot_max_speed"])).is_greater(float(by_id["medium"]["rot_max_speed"]))
	assert_float(float(by_id["large"]["hp"])).is_greater(float(by_id["medium"]["hp"]))
	assert_int(int(by_id["small"]["slots"])).is_less(int(by_id["large"]["slots"]))
	assert_float(float(by_id["small"]["lit_fraction"])).is_greater(float(by_id["large"]["lit_fraction"]))


func test_picker_opens_on_start_with_the_default_planet_selected() -> void:
	var game: Game = _make_game()
	var picker: PlanetPicker = game.planet_picker
	assert_bool(picker.visible).is_true()
	assert_int(picker.planet_count()).is_equal(3)
	# Smallest first; medium (the wave set's planet) is preselected.
	assert_str(picker.selected_id()).is_equal("medium")
	assert_str(picker.card(0).name).is_equal("Planet_small")
	assert_str(picker.card(2).name).is_equal("Planet_large")
	# The planet's gestures are blocked while it is open.
	assert_bool(bool(game.input_router.ui_blocker.call(Vector2(640.0, 360.0)))).is_true()


func test_keyboard_picks_a_planet_and_starts_the_run_on_it() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var picker: PlanetPicker = game.planet_picker
	picker._input(key(KEY_1, true))
	assert_str(picker.selected_id()).is_equal("small")
	assert_bool(picker.visible).is_true()
	picker._input(key(KEY_ENTER, true))
	assert_bool(picker.visible).is_false()
	assert_array(_last_new_run(sim)).is_equal([game.run_seed, 0, "small"])
	assert_float(float(sim.get_planet_radius())).is_equal(80.0)
	game.planet_view.sync()
	assert_int(game.planet_view.slot_count).is_equal(int(sim.get_slot_count()))
	assert_int(game.planet_view.slot_count).is_less(36)
	assert_bool(bool(game.input_router.ui_blocker.call(Vector2(640.0, 360.0)))).is_false()


func test_tap_selects_and_a_second_tap_or_start_plays() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var picker: PlanetPicker = game.planet_picker
	picker.card(2).pressed.emit()
	assert_str(picker.selected_id()).is_equal("large")
	assert_int(sim.calls_to(&"new_run").size()).is_equal(0)
	picker.card(2).pressed.emit()
	assert_bool(picker.visible).is_false()
	assert_array(_last_new_run(sim)).is_equal([game.run_seed, 0, "large"])
	assert_float(float(sim.get_planet_hp_max())).is_greater(100.0)
	# Restart opens the picker again on the current planet; Start keeps it.
	sim.clear_calls()
	game.restart()
	assert_bool(picker.visible).is_true()
	assert_str(picker.selected_id()).is_equal("large")
	assert_array(_last_new_run(sim)).is_equal([game.run_seed, 0, "large"])
	picker.start_button().pressed.emit()
	assert_bool(picker.visible).is_false()
	assert_int(sim.calls_to(&"new_run").size()).is_equal(1)  # same planet: no second restart


func test_picking_the_current_planet_does_not_restart() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	game.planet_picker._input(key(KEY_2, true))
	game.planet_picker._input(key(KEY_KP_ENTER, true))
	assert_int(sim.calls_to(&"new_run").size()).is_equal(0)
	assert_str(sim.get_planet_id()).is_equal("medium")


func test_disabled_picker_starts_straight_away() -> void:
	var game: Game = _make_game(false)
	assert_bool(game.planet_picker.visible).is_false()
	game.restart()
	assert_bool(game.planet_picker.visible).is_false()


func test_build_view_shows_the_whole_ring_on_every_planet() -> void:
	var game: Game = _make_game(false)
	var sim: MockSimServer = game.sim as MockSimServer
	var bands: BandController = game.bands
	bands.viewport_size_override = SCREEN
	var ring_px: Array[float] = []
	for id: String in ["small", "medium", "large"]:
		sim.new_run(1, 0, id)
		var r: float = float(sim.get_planet_radius())
		var ring: float = (r + bands.surface_ring_margin) * bands.surface_zoom()
		ring_px.append(ring)
		# The ring stays clear of the top bar and the bottom buttons (M1.13).
		assert_float(ring).is_less(SCREEN.y * 0.5 - 80.0)
		# The Watch view still shows the spawn ring around any planet.
		assert_float(bands.orbit_zoom()).is_less(bands.surface_zoom())
		assert_float(bands.surface_zoom()).is_greater(bands.threshold_zoom())
	assert_float(ring_px[0]).is_equal_approx(ring_px[1], 0.01)
	assert_float(ring_px[2]).is_equal_approx(ring_px[1], 0.01)
	# Medium keeps the M1.13 framing: the radius is surfacePlanetFrac of the height.
	sim.new_run(1, 0, "medium")
	assert_float(100.0 * bands.surface_zoom()).is_equal_approx(SCREEN.y * bands.surface_planet_frac, 0.01)


func test_real_sim_server_plays_the_chosen_planet() -> void:
	if not Game.real_sim_available():
		return
	var server: SimServer = auto_free(SimServer.new())
	add_child(server)
	assert_array(server.load_content("res://content")).is_empty()
	var defs: Array[Dictionary] = server.get_planet_defs()
	assert_int(defs.size()).is_equal(3)
	server.new_run(5)
	assert_str(server.get_planet_id()).is_equal("medium")
	assert_int(server.get_slot_count()).is_equal(36)
	var medium_hp: float = server.get_planet_hp_max()
	server.new_run(5, 0, "small")
	assert_str(server.get_planet_id()).is_equal("small")
	assert_float(server.get_planet_radius()).is_equal(80.0)
	assert_int(server.get_slot_count()).is_less(36)
	assert_float(server.get_planet_hp_max()).is_less(medium_hp)
	# The run log carries the planet: a replay plays it on the same planet.
	server.place_building(2, 0, 0.0)
	server.start_wave()
	server.step_ticks(90)
	var hash_small: int = server.get_state_hash()
	var log: PackedByteArray = server.get_command_log()
	server.new_run(9, 0, "large")
	assert_bool(server.replay(5, log)).is_true()
	assert_str(server.get_planet_id()).is_equal("small")
	assert_int(server.get_state_hash()).is_equal(hash_small)
