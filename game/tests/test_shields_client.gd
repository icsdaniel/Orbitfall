extends ClientTestBase
## M2.10 client: shields drawn from get_shields, falling-object warnings (streak,
## ETA ring, off-screen edge marker), impact / absorb flashes from events, volley
## entries in the forecast strip, the shield info line and --waves wave sets.


func _shield_index(sim: MockSimServer) -> int:
	for def: Dictionary in sim.get_building_defs():
		if str(def.get("kind", "")) == "shield":
			return int(def["index"])
	return -1


func _make_game() -> Game:
	var game: Game = auto_free(Game.new())
	game.force_mock = true
	game.planet_picker_enabled = false
	add_child(game)
	var sim: MockSimServer = game.sim as MockSimServer
	sim.auto_advance = false
	return game


func test_mock_lists_shield_building_falling_defs_and_wave_sets() -> void:
	var sim: MockSimServer = make_sim()
	assert_int(_shield_index(sim)).is_greater_equal(0)
	var ids: Array[String] = []
	for def: Dictionary in sim.get_falling_defs():
		ids.append(str(def["id"]))
	assert_array(ids).contains(["bomb", "meteor"])
	assert_bool(sim.get_wave_sets().has("m2_storm")).is_true()
	assert_float(float(sim.get_falling_warn_seconds())).is_equal_approx(3.0, 0.001)


func test_placed_shield_is_listed_online_and_full() -> void:
	var sim: MockSimServer = make_sim()
	sim.debug_set_stardust(1000.0)
	sim.place_building(3, _shield_index(sim), 0.0)
	sim.step_ticks(1)
	var shields: Array[Dictionary] = sim.get_shields()
	assert_int(shields.size()).is_equal(1)
	assert_bool(bool(shields[0]["online"])).is_true()
	assert_float(float(shields[0]["energy_frac"])).is_equal_approx(1.0, 0.001)
	assert_float(float(shields[0]["dome_radius"])).is_greater(float(sim.get_planet_radius()))


func test_warning_appears_inside_the_lead_and_is_red_when_unshielded() -> void:
	var sim: MockSimServer = make_sim()
	var radius: float = float(sim.get_planet_radius())
	# Meteor (speed 70) 8 s out: not yet warned; 2 s out: warned.
	sim.debug_spawn_falling("meteor", 1.0, radius + 70.0 * 8.0)
	assert_int(sim.get_falling_warnings().size()).is_equal(0)
	sim.debug_spawn_falling("meteor", 2.0, radius + 70.0 * 2.0)
	var warnings: Array[Dictionary] = sim.get_falling_warnings()
	assert_int(warnings.size()).is_equal(1)
	assert_float(float(warnings[0]["eta"])).is_less_equal(3.0)
	assert_int(int(warnings[0]["shield_slot"])).is_equal(-1)
	assert_that(SkyThreats.warning_color(warnings[0])).is_equal(Palette.THREAT)
	assert_that(SkyThreats.warning_color({"shield_slot": 4})).is_equal(Palette.SHIELD)


func test_eta_ring_shrinks_toward_impact() -> void:
	assert_float(SkyThreats.eta_ring_px(3.0, 3.0)).is_equal_approx(18.0, 0.001)
	assert_float(SkyThreats.eta_ring_px(0.0, 3.0)).is_equal_approx(4.0, 0.001)
	assert_float(SkyThreats.eta_ring_px(1.5, 3.0)).is_less(SkyThreats.eta_ring_px(2.5, 3.0))


func test_impact_and_absorb_events_flash_and_fade() -> void:
	var view: SkyThreats = auto_free(SkyThreats.new())
	assert_bool(view.handle_event({"type": "FallingImpact", "x": 100.0, "y": 0.0})).is_true()
	assert_bool(view.handle_event({"type": "ShieldAbsorbed", "x": 0.0, "y": 140.0})).is_true()
	assert_bool(view.handle_event({"type": "WaveStarted"})).is_false()
	assert_int(view.flashes.size()).is_equal(2)
	assert_that(view.flashes[0]["color"]).is_equal(Palette.THREAT)
	assert_that(view.flashes[1]["color"]).is_equal(Palette.SHIELD)
	view.age_flashes(SkyThreats.FLASH_SECONDS + 0.01)
	assert_int(view.flashes.size()).is_equal(0)


func test_game_feeds_sky_threats_and_draws_the_falling_batch() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	sim.debug_set_stardust(1000.0)
	sim.place_building(0, _shield_index(sim), 0.0)
	sim.debug_spawn_falling("meteor", 2.5, float(sim.get_planet_radius()) + 120.0)
	sim.step_ticks(1)
	game.sky_threats.sync()
	game.entity_view.sync()
	assert_int(game.sky_threats.shields.size()).is_equal(1)
	assert_int(game.sky_threats.warnings.size()).is_equal(1)
	assert_int(game.entity_view.renderer.get_count(EntityView.ARCHETYPE_FALLING)).is_equal(1)


func test_off_screen_warned_object_gets_an_edge_marker() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var edges: EdgeIndicators = game.hud.edge_indicators
	game.bands.auto_switch = false
	game.camera.set_zoom_now(4.0)  # close in: the planet rim fills the view
	var radius: float = float(sim.get_planet_radius())
	var pos: Vector2 = Vector2.from_angle(0.0) * (radius + 200.0)  # inside the 3 s lead
	sim.debug_spawn_falling("meteor", 0.0, radius + 200.0)
	var inner: Rect2 = Rect2(Vector2.ZERO, edges.size).grow(-EdgeIndicators.INSET)
	assert_bool(inner.has_point(edges.world_to_screen(pos))).is_false()
	var markers: Array[Dictionary] = edges.falling_markers()
	assert_int(markers.size()).is_equal(1)
	var at: Vector2 = markers[0]["at"]
	assert_bool(Rect2(Vector2.ZERO, edges.size).has_point(at)).is_true()
	assert_that(markers[0]["color"]).is_equal(Palette.THREAT)
	assert_float(float(markers[0]["eta"])).is_less_equal(3.0)


func test_forecast_strip_shows_volley_entries_with_the_falling_icon() -> void:
	var strip: ForecastStrip = auto_free(ForecastStrip.new())
	add_child(strip)
	strip.set_enemy_defs([{"index": 0, "id": "scout", "shape": 1, "color": "tier.red"}])
	strip.set_falling_defs([{"index": 0, "id": "meteor", "shape": 4, "color": "falling.meteor"}])
	strip.show_forecast([{"wave": 1, "light_mult": 1.0, "exact": true, "entries": [
		{"lane_bearing": 0.5, "enemy": 0, "count": 4, "shadow_caster": false, "volley": false, "drops": -1},
		{"lane_bearing": 1.5, "enemy": -1, "falling": 0, "count": 8, "volley": true, "spread": 0.1},
	]}])
	var icons: Array[ForecastStrip.EntryIcon] = strip.entries_of(0)
	assert_int(icons.size()).is_equal(2)
	assert_bool(icons[0].volley).is_false()
	assert_bool(icons[1].volley).is_true()
	assert_int(icons[1].shape).is_equal(4)
	assert_that(icons[1].color).is_equal(Palette.named("falling.meteor"))
	assert_int(icons[1].count).is_equal(8)


func test_shield_note_reports_energy_and_recharge() -> void:
	var def: Dictionary = {"shield_width": deg_to_rad(60.0), "shield_energy": 24.0, "recharge_seconds": 12.0}
	assert_str(BuildMenu.shield_note(def)).contains("60° wide")
	assert_str(BuildMenu.shield_note(def, {"online": true, "energy": 10.0})).contains("10 / 24")
	assert_str(BuildMenu.shield_note(def, {"online": false, "charge_frac": 0.5})).contains("50%")


func test_selected_shield_menu_shows_its_energy() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	sim.debug_set_stardust(1000.0)
	sim.place_building(5, _shield_index(sim), 0.0)
	sim.step_ticks(1)
	game._select_site(5)
	assert_str(game.hud.build_menu.info_text()).contains("Shield up")


func test_waves_arg_picks_the_storm_set() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	sim.clear_calls()
	game._waves_arg = "m2_storm"
	game._resolve_wave_set()
	game._start_run(11)
	var runs: Array[Array] = sim.calls_to(&"new_run")
	assert_int(game.wave_set).is_equal(sim.get_wave_sets().find("m2_storm"))
	assert_int(int(runs[runs.size() - 1][1])).is_equal(game.wave_set)
	assert_bool(game.planet_picker_enabled).is_false()
	# The storm set forecasts volleys.
	var volleys: int = 0
	for wave: Dictionary in sim.get_forecast(5):
		for entry: Dictionary in wave.get("entries", []):
			if bool(entry.get("volley", false)):
				volleys += 1
	assert_int(volleys).is_greater(0)
