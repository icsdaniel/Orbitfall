extends ClientTestBase
## M1.3/M1.4/M1.7-M1.11 client: build-menu flow, placement ghost, bands and slow
## time, forecast strip, HUD, end screens, debug overlay and tuning panel, all
## against the mock SimServer through the contract methods only.


func _make_game() -> Game:
	var game: Game = auto_free(Game.new())
	game.force_mock = true
	add_child(game)
	var sim: MockSimServer = game.sim as MockSimServer
	sim.auto_advance = false
	game.planet_view.view_zoom = game.camera.get_zoom_value()
	game.planet_view.sync()
	sim.clear_calls()
	return game


func _tap_slot(game: Game, slot: int) -> void:
	game.input_router.advance(1.0)  # never a double-tap with the previous one
	tap(game.input_router, game.slot_screen_position(slot))


func _building(game: Game, kind: String) -> int:
	for def: Dictionary in game.sim.get_building_defs():
		if str(def["kind"]) == kind:
			return int(def["index"])
	return -1


func test_game_uses_the_mock_when_forced_and_contract_surface_is_complete() -> void:
	var game: Game = _make_game()
	assert_bool(game.is_mock).is_true()
	for method: String in [
		"load_content", "new_run", "set_threaded", "step_ticks", "get_alpha", "rotate_input",
		"place_building", "sell_building", "set_aim", "set_targeting", "start_wave",
		"set_time_scale", "get_command_log", "replay", "get_tick", "get_state_hash",
		"get_phase", "get_wave_index", "get_wave_count", "get_stardust", "get_planet_hp",
		"get_planet_hp_max", "get_time_scale", "get_planet_radius", "get_planet_rotation",
		"get_planet_angular_velocity", "get_slot_count", "get_slot_buildings",
		"get_slot_flags", "get_sun_bearing", "get_beam_width", "get_beam_strength",
		"get_shade_intervals", "get_income_per_second", "get_enemy_buffer",
		"get_enemy_count", "get_projectile_buffer", "get_projectile_count",
		"get_large_units", "get_building_defs", "get_enemy_defs", "get_forecast",
		"get_debug_stats", "set_tunable", "drain_events",
	]:
		assert_bool(game.sim.has_method(method)).override_failure_message(
			"mock lacks contract method " + method).is_true()


func test_tap_empty_slot_opens_menu_and_build_sends_place_building() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var menu: BuildMenu = game.hud.build_menu
	assert_int(game.bands.band).is_equal(BandController.Band.SURFACE)
	_tap_slot(game, 9)
	assert_bool(menu.is_open()).is_true()
	assert_int(game.selected_slot).is_equal(9)
	assert_int(menu.slot).is_equal(9)
	var gatling: int = 1
	menu.building_button(gatling).pressed.emit()
	assert_int(game.planet_view.ghost_building).is_equal(gatling)
	assert_int(sim.calls_to(&"place_building").size()).is_equal(0)
	menu.build_button().pressed.emit()
	var placed: Array[Array] = sim.calls_to(&"place_building")
	assert_int(placed.size()).is_equal(1)
	assert_int(placed[0][0]).is_equal(9)
	assert_int(placed[0][1]).is_equal(gatling)
	assert_float(placed[0][2]).is_equal(0.0)
	assert_bool(menu.is_open()).is_false()
	assert_int(game.selected_slot).is_equal(-1)
	assert_int(sim.get_slot_buildings()[9]).is_equal(gatling)


func test_tapping_the_previewed_building_again_builds_it() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	_tap_slot(game, 4)
	var first: int = game.hud.build_menu.previewed
	assert_int(first).is_greater_equal(0)
	game.hud.build_menu.building_button(first).pressed.emit()
	assert_int(sim.calls_to(&"place_building").size()).is_equal(1)
	assert_int(sim.calls_to(&"place_building")[0][0]).is_equal(4)


func test_unaffordable_building_cannot_be_built() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	sim.debug_set_stardust(10.0)
	_tap_slot(game, 4)
	var menu: BuildMenu = game.hud.build_menu
	assert_bool(menu.build_button().disabled).is_true()
	menu.building_button(menu.previewed).pressed.emit()
	assert_int(sim.calls_to(&"place_building").size()).is_equal(0)


func test_tap_building_shows_sell_and_sell_sends_sell_building() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	sim.place_building(12, _building(game, "panel"), 0.0)
	sim.clear_calls()
	_tap_slot(game, 12)
	var menu: BuildMenu = game.hud.build_menu
	assert_bool(menu.is_open()).is_true()
	assert_bool(menu.sell_button().visible).is_true()
	assert_bool(menu.build_button().visible).is_false()
	menu.sell_button().pressed.emit()
	assert_int(sim.calls_to(&"sell_building").size()).is_equal(1)
	assert_int(sim.calls_to(&"sell_building")[0][0]).is_equal(12)
	assert_int(sim.get_slot_buildings()[12]).is_equal(-1)


func test_tap_away_from_the_ring_closes_the_menu() -> void:
	var game: Game = _make_game()
	_tap_slot(game, 9)
	assert_bool(game.hud.build_menu.is_open()).is_true()
	game.input_router.advance(1.0)
	tap(game.input_router, game.world_to_screen(Vector2.ZERO))
	assert_bool(game.hud.build_menu.is_open()).is_false()
	assert_int(game.planet_view.selected_slot).is_equal(-1)


func test_tap_in_orbit_band_does_not_open_the_menu() -> void:
	var game: Game = _make_game()
	game.bands.snap_to(BandController.Band.ORBIT)
	game.planet_view.view_zoom = game.camera.get_zoom_value()
	_tap_slot(game, 9)
	assert_bool(game.hud.build_menu.is_open()).is_false()


func test_ghost_arc_rotates_with_the_planet() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var view: PlanetView = game.planet_view
	_tap_slot(game, 0)
	var def: Dictionary = view.building_def(game.hud.build_menu.previewed)
	var arc: float = Palette.angle_of(def["arc"])
	var tower_range: float = def["range"]
	assert_float(arc).is_greater(0.0)
	var before: PackedVector2Array = view.arc_polygon(0, arc, tower_range)
	sim.rotate_input(1.0)
	sim.step_ticks(20)
	view.sync()
	var turned: float = sim.get_planet_rotation()
	assert_float(absf(turned)).is_greater(0.2)
	var after: PackedVector2Array = view.arc_polygon(0, arc, tower_range)
	assert_int(after.size()).is_equal(before.size())
	for i: int in before.size():
		assert_vector(after[i]).is_equal_approx(before[i].rotated(turned), Vector2(0.01, 0.01))
	# The sector is +-arc around the slot normal, out to the range.
	var origin: Vector2 = view.slot_position(0)
	assert_float(after[1].distance_to(origin)).is_equal_approx(tower_range, 0.01)
	assert_float(absf((after[1] - origin).angle_to(origin))).is_equal_approx(arc, 0.001)


func test_slot_picking_matches_slot_positions_after_rotation() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	sim.rotate_input(-1.0)
	sim.step_ticks(17)
	sim.rotate_input(0.0)
	for slot: int in [0, 5, 18, 35]:
		game.clear_selection()  # an open menu may cover part of the ring
		_tap_slot(game, slot)
		assert_int(game.selected_slot).is_equal(slot)


func test_double_tap_toggles_band_and_slow_time_is_sent_as_a_command() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var bands: BandController = game.bands
	# Intermission in Surface: normal time.
	bands.update()
	sim.start_wave()
	bands.update()
	var scales: Array[Array] = sim.calls_to(&"set_time_scale")
	assert_float(scales[-1][0]).is_equal_approx(bands.surface_time_scale, 0.0001)
	assert_float(scales[-1][1]).is_equal_approx(0.3, 0.0001)
	assert_float(sim.get_time_scale()).is_equal_approx(0.75, 0.0001)
	# Double-tap: Orbit, back to normal speed.
	var count: int = scales.size()
	tap(game.input_router, Vector2(640.0, 200.0))
	game.input_router.advance(0.1)
	tap(game.input_router, Vector2(640.0, 200.0))
	assert_int(bands.band).is_equal(BandController.Band.ORBIT)
	scales = sim.calls_to(&"set_time_scale")
	assert_int(scales.size()).is_equal(count + 1)
	assert_float(scales[-1][0]).is_equal(1.0)
	assert_float(scales[-1][1]).is_equal_approx(0.3, 0.0001)
	# No repeat while nothing changes.
	bands.update()
	bands.update()
	assert_int(sim.calls_to(&"set_time_scale").size()).is_equal(count + 1)
	# Double-tap again: Surface, slow again.
	game.input_router.advance(1.0)
	tap(game.input_router, Vector2(640.0, 200.0))
	game.input_router.advance(0.1)
	tap(game.input_router, Vector2(640.0, 200.0))
	assert_int(bands.band).is_equal(BandController.Band.SURFACE)
	assert_float(sim.calls_to(&"set_time_scale")[-1][0]).is_equal_approx(0.75, 0.0001)


func test_no_slow_time_in_intermission() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	game.bands.update()
	for call: Array in sim.calls_to(&"set_time_scale"):
		assert_float(call[0]).is_equal(1.0)


func test_zoom_gesture_crosses_the_band_threshold_and_cross_fades() -> void:
	var game: Game = _make_game()
	var bands: BandController = game.bands
	assert_float(bands.surface_weight).is_greater(0.95)
	assert_float(bands.surface_zoom()).is_greater(bands.threshold_zoom())
	assert_float(bands.threshold_zoom()).is_greater(bands.orbit_zoom())
	for i: int in 12:
		game.input_router.handle_event(wheel(MOUSE_BUTTON_WHEEL_DOWN, Vector2(640.0, 360.0)))
	assert_int(bands.band).is_equal(BandController.Band.ORBIT)
	# The fade follows the eased zoom: part-way first, fully out at the end.
	game.camera.advance(0.03)
	bands.update()
	assert_float(bands.surface_weight).is_between(0.0, 1.0)
	for i: int in 200:
		game.camera.advance(1.0 / 60.0)
	bands.update()
	assert_float(bands.surface_weight).is_less(0.05)
	# The planet stays centred while zooming.
	assert_float(absf(game.camera.global_position.x)).is_less(0.001)


func test_time_scale_eases_in_the_mock() -> void:
	var sim: MockSimServer = auto_free(MockSimServer.new())
	add_child(sim)
	sim.set_process(false)
	sim.set_time_scale(0.75, 0.3)
	sim._process(0.15)
	assert_float(sim.get_time_scale()).is_equal_approx(0.875, 0.001)
	sim._process(0.2)
	assert_float(sim.get_time_scale()).is_equal_approx(0.75, 0.001)


func test_forecast_strip_shows_the_entries_of_get_forecast() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var strip: ForecastStrip = game.hud.forecast
	var wave_count: int = sim.get_wave_count()
	# Walk every wave: the strip always mirrors get_forecast exactly.
	for wave: int in wave_count:
		var forecast: Array[Dictionary] = sim.get_forecast(5)
		strip.show_forecast(forecast)
		assert_int(strip.card_count()).is_equal(forecast.size())
		assert_int(strip.card_count()).is_equal(mini(5, wave_count - wave))
		var expected: int = 0
		for card: Dictionary in forecast:
			var entries: Array = card["entries"]
			expected += entries.size()
		assert_int(strip.entry_count()).is_equal(expected)
		# Next wave is exact: counts, bearings and shadow-caster marks match.
		var first: Array = forecast[0]["entries"]
		var icons: Array[ForecastStrip.EntryIcon] = strip.entries_of(0)
		assert_int(icons.size()).is_equal(first.size())
		assert_bool(bool(forecast[0]["exact"])).is_true()
		for i: int in first.size():
			var entry: Dictionary = first[i]
			assert_int(icons[i].count).is_equal(int(entry["count"]))
			assert_float(icons[i].bearing).is_equal_approx(float(entry["lane_bearing"]), 0.0001)
			assert_bool(icons[i].shadow_caster).is_equal(bool(entry["shadow_caster"]))
		# Later cards carry no counts.
		if forecast.size() > 1:
			for icon: ForecastStrip.EntryIcon in strip.entries_of(1):
				assert_int(icon.count).is_equal(-1)
		sim.debug_skip_wave()


func test_forecast_ghost_arcs_reach_the_planet_view() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var found: bool = false
	for wave: int in sim.get_wave_count():
		game._refresh_forecast()
		var arcs: PackedFloat32Array = game.planet_view.forecast[0]["eclipser_ghost_arcs"]
		var has_caster: bool = false
		var entries: Array = game.planet_view.forecast[0]["entries"]
		for entry: Dictionary in entries:
			has_caster = has_caster or bool(entry["shadow_caster"])
		assert_bool(arcs.size() >= 2).is_equal(has_caster)
		found = found or has_caster
		sim.debug_skip_wave()
	assert_bool(found).is_true()


func test_hud_shows_stardust_hp_wave_and_start_wave_button() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var hud: Hud = game.hud
	hud.refresh()
	assert_str(hud._stardust.text).is_equal("150")
	assert_str(hud._hp_text.text).is_equal("HP 100 / 100")
	assert_str(hud._wave.text).is_equal("Wave 1 / %d" % sim.get_wave_count())
	assert_bool(hud.start_button.visible).is_true()
	assert_float(hud.start_button.custom_minimum_size.y).is_greater_equal(48.0)
	assert_str(hud._hint.text).contains("Intermission")
	hud.start_button.pressed.emit()
	assert_int(sim.calls_to(&"start_wave").size()).is_equal(1)
	hud.refresh()
	assert_bool(hud.start_button.visible).is_false()
	sim.debug_set_planet_hp(42.0)
	hud.refresh()
	assert_str(hud._hp_text.text).is_equal("HP 42 / 100")


func test_lit_panel_income_shows_in_the_hud() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var panel: int = _building(game, "panel")
	sim.place_building(0, panel, 0.0)  # under the sun at bearing 0
	sim.place_building(18, panel, 0.0)  # night side
	game.hud.refresh()
	assert_str(game.hud._income.text).is_equal("+3.0/s")
	assert_int(sim.get_slot_flags()[0] & PlanetView.FLAG_LIT).is_equal(PlanetView.FLAG_LIT)
	assert_int(sim.get_slot_flags()[18] & PlanetView.FLAG_LIT).is_equal(0)


func test_eclipser_shades_slots_and_reports_shade_intervals() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	sim.debug_spawn("eclipser", Vector2(260.0, 0.0))
	game.planet_view.sync()
	assert_int(game.planet_view.shade_intervals.size()).is_equal(2)
	var flags: int = game.planet_view.slot_flags[0]
	assert_int(flags & PlanetView.FLAG_SHADED).is_equal(PlanetView.FLAG_SHADED)
	var units: Array[Dictionary] = sim.get_large_units()
	assert_int(units.size()).is_equal(1)
	assert_bool(bool(units[0]["shadow"])).is_true()
	assert_str(game.hud.debug_overlay.build_text()).contains("shade [")


func test_win_and_lose_screens_and_restart() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var hud: Hud = game.hud
	game.run_seed = 4242
	sim.start_wave()
	sim.debug_set_planet_hp(0.0)
	sim.debug_set_phase(MockSimServer.PHASE_LOST)
	game._track_phase()
	assert_bool(hud.end_screen.visible).is_true()
	assert_str(hud.end_screen.title_text()).is_equal("PLANET LOST")
	assert_str(hud.end_screen.detail_text()).contains("Seed: 4242")
	assert_str(hud.end_screen.detail_text()).contains("Waves survived: 0 / %d" % sim.get_wave_count())
	sim.clear_calls()
	hud.end_screen.restart_button().pressed.emit()
	assert_int(sim.calls_to(&"new_run").size()).is_equal(1)
	assert_bool(hud.end_screen.visible).is_false()
	assert_int(sim.get_phase()).is_equal(MockSimServer.PHASE_INTERMISSION)
	assert_float(sim.get_planet_hp()).is_equal(sim.get_planet_hp_max())
	sim.debug_set_phase(MockSimServer.PHASE_WON)
	game._track_phase()
	assert_str(hud.end_screen.title_text()).is_equal("VICTORY")
	assert_str(hud.end_screen.detail_text()).contains(
		"Waves survived: %d / %d" % [sim.get_wave_count(), sim.get_wave_count()])


func test_debug_overlay_toggles_and_lists_the_required_stats() -> void:
	var game: Game = _make_game()
	var hud: Hud = game.hud
	assert_bool(hud.debug_overlay.visible).is_false()
	game.input_router.handle_event(key(KEY_QUOTELEFT, true))
	assert_bool(hud.debug_overlay.visible).is_true()
	assert_bool(hud.tuning_panel.visible).is_true()
	var text: String = hud.debug_overlay.build_text()
	for needle: String in [
		"fps", "ms/tick", "enemies", "projectiles", "band Surface", "sun", "shade",
		"income", "taps/min", "time lit",
	]:
		assert_str(text).contains(needle)
	game.input_router.handle_event(key(KEY_QUOTELEFT, false))
	game.input_router.handle_event(key(KEY_QUOTELEFT, true))
	assert_bool(hud.debug_overlay.visible).is_false()


func test_tuning_panel_calls_set_tunable_and_sun_speed_changes_drift() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var slider: HSlider = game.hud.tuning_panel.slider("sun.speedDegPerSec")
	assert_object(slider).is_not_null()
	sim.start_wave()
	var a: float = sim.get_sun_bearing()
	sim.step_ticks(30)
	var slow: float = sim.get_sun_bearing() - a
	slider.value = 9.0
	var tunes: Array[Array] = sim.calls_to(&"set_tunable")
	assert_int(tunes.size()).is_equal(1)
	assert_str(tunes[0][0]).is_equal("sun.speedDegPerSec")
	assert_float(tunes[0][1]).is_equal(9.0)
	a = sim.get_sun_bearing()
	sim.step_ticks(30)
	assert_float(sim.get_sun_bearing() - a).is_equal_approx(slow * 6.0, 0.001)


func test_percent_time_lit_integrates_lit_fraction_during_waves() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var overlay: DebugOverlay = game.hud.debug_overlay
	var panel: int = _building(game, "panel")
	sim.place_building(0, panel, 0.0)
	sim.place_building(18, panel, 0.0)
	overlay._process(1.0)  # intermission: not counted
	assert_float(overlay.percent_time_lit()).is_equal(0.0)
	sim.start_wave()
	sim.step_ticks(1)
	overlay._process(1.0)
	assert_float(overlay.percent_time_lit()).is_equal_approx(50.0, 0.01)


func test_entities_reach_the_batch_renderer_from_the_buffers() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	sim.start_wave()
	sim.step_ticks(120)
	assert_int(sim.get_enemy_count()).is_greater(0)
	assert_int(sim.get_enemy_buffer().size()).is_equal(sim.get_enemy_count() * 16)
	game.entity_view.sync()
	assert_int(game.entity_view.renderer.get_count(EntityView.ARCHETYPE_ENEMIES)).is_equal(
		sim.get_enemy_count())
	# No scene-tree node per entity.
	assert_int(game.entity_view.get_child_count()).is_equal(1)


func test_ui_blocks_world_gestures_over_buttons() -> void:
	var game: Game = _make_game()
	await get_tree().process_frame
	await get_tree().process_frame
	var rect: Rect2 = game.hud.start_button.get_global_rect()
	assert_bool(game.hud.is_point_over_ui(rect.get_center())).is_true()
	assert_bool(game.hud.is_point_over_ui(game.world_to_screen(Vector2.ZERO))).is_false()
	assert_float(rect.size.x).is_greater_equal(48.0)
	assert_float(game.hud.rotate_left.get_global_rect().size.y).is_greater_equal(48.0)
