extends GdUnitTestSuite
## M0.5: SimServer round trips, threaded vs single-threaded determinism, events,
## content loading, run log replay and the CONTRACT.md surface.

const CONTRACT_METHODS: PackedStringArray = [
	"load_content", "new_run", "set_threaded", "step_ticks", "get_alpha",
	"rotate_input", "place_building", "sell_building", "set_aim", "set_targeting",
	"start_wave", "set_time_scale", "get_command_log", "replay",
	"get_tick", "get_state_hash", "get_phase", "get_wave_index", "get_wave_count",
	"get_stardust", "get_planet_hp", "get_planet_hp_max", "get_time_scale",
	"get_planet_radius", "get_planet_rotation", "get_planet_angular_velocity",
	"get_slot_count", "get_slot_buildings", "get_slot_flags",
	"get_sun_bearing", "get_beam_width", "get_beam_strength", "get_shade_intervals",
	"get_income_per_second", "get_enemy_buffer", "get_enemy_count",
	"get_projectile_buffer", "get_projectile_count", "get_large_units",
	"get_building_defs", "get_enemy_defs", "get_forecast", "get_debug_stats",
	"set_tunable", "drain_events", "core_answer", "core_version",
]


func _server() -> SimServer:
	var server: SimServer = auto_free(SimServer.new())
	return server


## A fixed script of commands spread over `ticks` ticks; returns hashes at checkpoints.
func _scripted_run(server: SimServer, run_seed: int, ticks: int) -> PackedInt64Array:
	var hashes: PackedInt64Array = PackedInt64Array()
	server.new_run(run_seed)
	var done: int = 0
	var step: int = 0
	while done < ticks:
		match step % 6:
			0: server.rotate_input(0.5 if step % 12 == 0 else -1.0)
			1: server.place_building(step % 36, step % 3, 0.25)
			2: server.set_time_scale(0.75, 0.3)
			3: server.set_aim(step % 36, -0.5)
			4: server.set_time_scale(1.0)
			5:
				server.sell_building(step % 36)
				server.set_targeting(3, 1)
				server.start_wave()
		var chunk: int = mini(1 + (step * 7) % 23, ticks - done)
		server.step_ticks(chunk)
		done += chunk
		step += 1
		if step % 10 == 0:
			hashes.append(server.get_state_hash())
	hashes.append(server.get_state_hash())
	return hashes


func test_contract_methods_are_bound() -> void:
	var server: SimServer = _server()
	for method: String in CONTRACT_METHODS:
		assert_bool(server.has_method(method)).override_failure_message(
			"SimServer is missing %s" % method).is_true()


func test_round_trip_commands_ticks_and_hash() -> void:
	var server: SimServer = _server()
	server.new_run(7)
	assert_int(server.get_tick()).is_equal(0)
	var start_hash: int = server.get_state_hash()
	server.set_time_scale(0.5)
	server.rotate_input(1.0)
	server.step_ticks(10)
	assert_int(server.get_tick()).is_equal(10)
	assert_float(server.get_time_scale()).is_equal_approx(0.5, 0.0001)
	assert_int(server.get_state_hash()).is_not_equal(start_hash)
	assert_float(server.get_alpha()).is_equal(1.0)
	# Commands are stamped with the next tick: an eased change is not complete one tick later.
	server.set_time_scale(1.0, 1.0)
	server.step_ticks(1)
	assert_float(server.get_time_scale()).is_between(0.5, 0.99)
	server.step_ticks(30)
	assert_float(server.get_time_scale()).is_equal_approx(1.0, 0.0001)
	assert_int(server.get_tick()).is_equal(41)


func test_same_seed_and_commands_give_equal_hashes() -> void:
	var a: PackedInt64Array = _scripted_run(_server(), 42, 600)
	var b: PackedInt64Array = _scripted_run(_server(), 42, 600)
	var c: PackedInt64Array = _scripted_run(_server(), 43, 600)
	assert_array(a).is_equal(b)
	assert_int(a[a.size() - 1]).is_not_equal(c[c.size() - 1])


func test_commands_change_the_hash() -> void:
	var with_commands: SimServer = _server()
	with_commands.new_run(5)
	with_commands.place_building(2, 1)
	with_commands.step_ticks(5)
	var without: SimServer = _server()
	without.new_run(5)
	without.step_ticks(5)
	assert_int(with_commands.get_state_hash()).is_not_equal(without.get_state_hash())


func test_threaded_and_single_threaded_give_equal_hashes() -> void:
	var single: SimServer = _server()
	single.set_threaded(false)
	var threaded: SimServer = _server()
	threaded.set_threaded(true)
	assert_bool(threaded.is_threaded()).is_true()
	assert_bool(single.is_threaded()).is_false()
	for scenario: bool in [false, true]:
		single.set_debug_scenario(scenario)
		threaded.set_debug_scenario(scenario)
		var a: PackedInt64Array = _scripted_run(single, 1234, 3000)
		var b: PackedInt64Array = _scripted_run(threaded, 1234, 3000)
		assert_int(single.get_tick()).is_equal(3000)
		assert_int(threaded.get_tick()).is_equal(3000)
		assert_array(b).is_equal(a)
	threaded.set_threaded(false)


func test_real_time_advance_matches_in_both_modes() -> void:
	var single: SimServer = _server()
	var threaded: SimServer = _server()
	threaded.set_threaded(true)
	var hashes: PackedInt64Array = PackedInt64Array()
	for server: SimServer in [single, threaded]:
		server.set_debug_scenario(true)
		server.new_run(99)
		var total: int = 0
		for frame: int in 240:
			if frame % 40 == 0:
				server.set_time_scale(0.25 + 0.25 * float(frame % 3), 0.2)
			if frame % 17 == 0:
				server.rotate_input(-0.3)
			total += server.advance(1.0 / 60.0 if frame % 5 != 0 else 0.021)
			assert_float(server.get_alpha()).is_between(0.0, 0.9999)
		server.step_ticks(0)  # wait for the sim thread and latch its last snapshot
		assert_int(server.get_tick()).is_equal(total)
		assert_int(total).is_between(118, 126)
		hashes.append(server.get_state_hash())
	assert_int(hashes[1]).is_equal(hashes[0])
	threaded.set_threaded(false)


func test_switching_modes_mid_run_keeps_the_hash() -> void:
	var plain: SimServer = _server()
	plain.new_run(8)
	plain.rotate_input(1.0)
	plain.step_ticks(50)
	plain.set_time_scale(2.0)
	plain.step_ticks(50)
	var switched: SimServer = _server()
	switched.new_run(8)
	switched.rotate_input(1.0)
	switched.set_threaded(true)
	switched.step_ticks(50)
	switched.set_time_scale(2.0)
	switched.set_threaded(false)
	switched.step_ticks(50)
	assert_int(switched.get_tick()).is_equal(100)
	assert_int(switched.get_state_hash()).is_equal(plain.get_state_hash())


func test_advance_caps_the_backlog() -> void:
	var server: SimServer = _server()
	server.new_run(1)
	assert_int(server.advance(5.0)).is_equal(8)
	assert_int(server.get_tick()).is_equal(8)
	assert_int(server.advance(0.0)).is_equal(0)
	assert_int(server.advance(-1.0)).is_equal(0)


func test_events_drain_once_in_both_modes() -> void:
	for threaded: bool in [false, true]:
		var server: SimServer = _server()
		server.set_threaded(threaded)
		server.set_debug_scenario(true)
		server.new_run(3)
		assert_array(server.drain_events()).is_empty()
		server.set_time_scale(0.5)
		server.step_ticks(5)
		var events: Array[Dictionary] = server.drain_events()
		assert_int(events.size()).is_equal(5)
		for i: int in events.size():
			var event: Dictionary = events[i]
			assert_str(event["type"]).is_equal("None")
			assert_int(event["tick"]).is_equal(i)
			assert_int(event["a"]).is_equal(1)  # commands applied so far
			assert_float(event["value"]).is_equal_approx(0.5, 0.0001)
			assert_bool(event.has_all(["b", "x", "y"])).is_true()
		assert_array(server.drain_events()).is_empty()
		server.step_ticks(2)
		assert_int(server.drain_events().size()).is_equal(2)
		server.set_threaded(false)


func test_node_advances_itself_in_the_tree() -> void:
	var server: SimServer = _server()
	add_child(server)
	await get_tree().process_frame
	await get_tree().process_frame
	assert_int(server.get_tick()).is_equal(0)  # no run yet: nothing ticks
	server.set_threaded(true)
	server.new_run(11)
	var deadline: int = Time.get_ticks_msec() + 5000
	while server.get_tick() < 3 and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	assert_int(server.get_tick()).is_greater_equal(3)
	server.set_threaded(false)
	remove_child(server)


func test_load_content_shipped_content_has_no_errors() -> void:
	var server: SimServer = _server()
	assert_array(server.load_content()).is_empty()
	server.new_run(1, 0)
	assert_float(server.get_planet_radius()).is_equal_approx(100.0, 0.001)
	assert_int(server.get_slot_count()).is_equal(36)
	assert_int(server.get_wave_count()).is_equal(8)
	assert_float(server.get_planet_hp_max()).is_equal_approx(100.0, 0.001)
	assert_float(server.get_planet_hp()).is_equal_approx(100.0, 0.001)
	assert_float(server.get_beam_width()).is_equal_approx(141.0, 0.001)
	assert_int(server.get_slot_buildings().size()).is_equal(36)
	assert_int(server.get_slot_buildings()[0]).is_equal(-1)
	assert_int(server.get_slot_flags().size()).is_equal(36)
	assert_int(server.get_phase()).is_equal(0)
	assert_int(server.get_shade_intervals().size()).is_equal(0)

	var buildings: Array[Dictionary] = server.get_building_defs()
	assert_int(buildings.size()).is_equal(3)
	var ids: Array[String] = []
	for def: Dictionary in buildings:
		assert_bool(def.has_all(
			["index", "id", "name", "cost", "arc", "range", "kind", "shape", "color"])).is_true()
		assert_int(def["index"]).is_equal(ids.size())
		ids.append(def["id"])
	assert_array(ids).contains(["cannon", "gatling", "solar_panel"])
	var cannon: Dictionary = buildings[ids.find("cannon")]
	assert_float(cannon["cost"]).is_equal_approx(90.0, 0.001)
	assert_float(cannon["arc"]).is_equal_approx(deg_to_rad(25.0), 0.0001)
	assert_str(cannon["kind"]).is_equal("tower")
	assert_str(buildings[ids.find("solar_panel")]["kind"]).is_equal("panel")
	assert_str(buildings[ids.find("solar_panel")]["name"]).is_equal("Solar Panel")
	assert_int(server.get_enemy_defs().size()).is_equal(5)


func test_load_content_reports_errors() -> void:
	var server: SimServer = _server()
	var missing: PackedStringArray = server.load_content("res://no_such_content_dir")
	assert_int(missing.size()).is_greater(0)
	# A folder of JSON that is not content must fail validation, not crash.
	var dir: String = "user://bad_content_test"
	DirAccess.make_dir_recursive_absolute(dir + "/enemies")
	var file: FileAccess = FileAccess.open(dir + "/enemies/broken.json", FileAccess.WRITE)
	file.store_string("{ \"id\": \"broken\", \"speed\": -1 ")
	file.close()
	assert_int(server.load_content(dir).size()).is_greater(0)
	DirAccess.remove_absolute(dir + "/enemies/broken.json")
	DirAccess.remove_absolute(dir + "/enemies")
	DirAccess.remove_absolute(dir)
	assert_int(server.get_building_defs().size()).is_equal(0)


func test_forecast_reads_wave_data() -> void:
	var server: SimServer = _server()
	assert_array(server.get_forecast(3)).is_empty()  # no content yet
	assert_array(server.load_content()).is_empty()
	server.new_run(1)
	var forecast: Array[Dictionary] = server.get_forecast(5)
	assert_int(forecast.size()).is_equal(5)
	var enemies: Array[Dictionary] = server.get_enemy_defs()
	var next: Dictionary = forecast[0]
	assert_int(next["wave"]).is_equal(1)
	assert_bool(next["exact"]).is_true()
	assert_float(next["light_mult"]).is_equal_approx(1.0, 0.0001)
	var entries: Array = next["entries"]
	assert_int(entries.size()).is_equal(1)
	var entry: Dictionary = entries[0]
	assert_int(entry["count"]).is_equal(8)
	assert_str(enemies[entry["enemy"]]["id"]).is_equal("drifter_red")
	assert_float(entry["lane_bearing"]).is_equal_approx(deg_to_rad(30.0), 0.0001)
	assert_bool(entry["shadow_caster"]).is_false()
	# +2 and +3: icons without counts; beyond: light level only.
	assert_bool(forecast[1]["exact"]).is_false()
	var icons: Array = forecast[1]["entries"]
	assert_int(icons.size()).is_equal(2)
	assert_int((icons[0] as Dictionary)["count"]).is_equal(-1)
	assert_int((forecast[2]["entries"] as Array).size()).is_greater(0)
	assert_int((forecast[3]["entries"] as Array).size()).is_equal(0)
	assert_bool(forecast[3].has("light_mult")).is_true()
	assert_bool(next["eclipser_ghost_arcs"] is PackedFloat32Array).is_true()
	assert_int(server.get_forecast(100).size()).is_equal(8)


func test_run_log_replays_to_the_same_state() -> void:
	var original: SimServer = _server()
	original.set_debug_scenario(true)
	var hashes: PackedInt64Array = _scripted_run(original, 77, 400)
	var log: PackedByteArray = original.get_command_log()
	assert_int(log.size()).is_greater(20)
	for threaded: bool in [false, true]:
		var copy: SimServer = _server()
		copy.set_debug_scenario(true)
		copy.set_threaded(threaded)
		assert_bool(copy.replay(77, log)).is_true()
		assert_int(copy.get_tick()).is_equal(400)
		assert_int(copy.get_state_hash()).is_equal(hashes[hashes.size() - 1])
		assert_array(copy.get_command_log()).is_equal(log)
		copy.set_threaded(false)
	var other_seed: SimServer = _server()
	other_seed.set_debug_scenario(true)
	assert_bool(other_seed.replay(78, log)).is_true()
	assert_int(other_seed.get_state_hash()).is_not_equal(hashes[hashes.size() - 1])


func test_render_buffers_use_the_batch_layout_and_interpolate() -> void:
	var server: SimServer = _server()
	assert_array(server.load_content()).is_empty()
	server.set_debug_scenario(true)
	server.new_run(1)
	assert_int(server.get_enemy_count()).is_equal(0)
	assert_int(server.get_enemy_buffer().size()).is_equal(0)
	server.step_ticks(30)  # debug enemies fly at (-30, 0) from x = 300
	assert_int(server.get_enemy_count()).is_equal(3)
	assert_int(server.get_projectile_count()).is_equal(1)
	var enemies: Array[Dictionary] = server.get_enemy_defs()
	var buffer: PackedFloat32Array = server.get_enemy_buffer()
	assert_int(buffer.size()).is_equal(3 * BatchRenderer.FLOATS_PER_INSTANCE)
	for i: int in 3:
		var at: int = i * BatchRenderer.FLOATS_PER_INSTANCE
		var def: Dictionary = enemies[i]
		assert_float(buffer[at + 3]).is_equal_approx(270.0, 0.01)
		assert_float(buffer[at + 7]).is_equal_approx(40.0 * float(i - 1), 0.01)
		# Scale = diameter, x axis along the velocity (-x).
		assert_float(buffer[at + 0]).is_equal_approx(-2.0 * float(def["size"]), 0.001)
		assert_float(buffer[at + 5]).is_equal_approx(-2.0 * float(def["size"]), 0.001)
		assert_float(buffer[at + BatchRenderer.OFFSET_CUSTOM]).is_equal(float(def["shape"]))
		assert_float(buffer[at + BatchRenderer.OFFSET_CUSTOM + 1]).is_equal(float(def["tier"]))
		assert_float(buffer[at + BatchRenderer.OFFSET_COLOR + 3]).is_equal(1.0)
	var shots: PackedFloat32Array = server.get_projectile_buffer()
	assert_int(shots.size()).is_equal(BatchRenderer.FLOATS_PER_INSTANCE)
	assert_float(shots[7]).is_equal_approx(60.0, 0.01)
	assert_float(shots[BatchRenderer.OFFSET_CUSTOM + 1]).is_equal(-1.0)

	# Half a tick short of the next tick: drawn half a tick behind the latest snapshot.
	assert_int(server.advance(1.5 / 30.0)).is_equal(1)
	assert_float(server.get_alpha()).is_equal_approx(0.5, 0.001)
	assert_float(server.get_enemy_buffer()[3]).is_equal_approx(269.0 + 0.5, 0.01)

	var stats: Dictionary = server.get_debug_stats()
	assert_bool(stats.has_all(["ms_per_tick", "enemies", "projectiles", "lit_fraction"])).is_true()
	assert_int(stats["enemies"]).is_equal(3)
	assert_int(stats["projectiles"]).is_equal(1)


func test_large_units_and_palette() -> void:
	var server: SimServer = _server()
	assert_array(server.load_content()).is_empty()
	var enemies: Array[Dictionary] = server.get_enemy_defs()
	var bar_index: int = -1
	for def: Dictionary in enemies:
		if def["hp_bar"] and int(def["index"]) < 3:
			bar_index = def["index"]
	server.set_palette({"enemy.eclipser": Color(0.2, 0.4, 0.6)})
	server.set_debug_scenario(true)
	server.new_run(1)
	server.step_ticks(1)
	var large: Array[Dictionary] = server.get_large_units()
	if bar_index < 0:
		assert_array(large).is_empty()
		return
	assert_int(large.size()).is_equal(1)
	var unit: Dictionary = large[0]
	assert_bool(unit.has_all(["pos", "radius", "hp_frac", "shadow"])).is_true()
	assert_float(unit["radius"]).is_equal_approx(float(enemies[bar_index]["size"]), 0.001)
	assert_float(unit["hp_frac"]).is_between(0.0, 1.0)
	var at: int = bar_index * BatchRenderer.FLOATS_PER_INSTANCE + BatchRenderer.OFFSET_COLOR
	if String(enemies[bar_index]["color"]) == "enemy.eclipser":
		assert_float(server.get_enemy_buffer()[at]).is_equal_approx(0.2, 0.0001)


func test_set_tunable() -> void:
	var server: SimServer = _server()
	assert_bool(server.set_tunable("sun.beamWidth", 200.0)).is_false()  # no content
	assert_array(server.load_content()).is_empty()
	server.set_threaded(true)
	server.new_run(1)
	assert_bool(server.set_tunable("sun.beamWidth", 200.0)).is_true()
	assert_bool(server.set_tunable("sun.speedDegPerSec", 3.0)).is_true()
	assert_bool(server.set_tunable("sun.noSuchKey", 1.0)).is_false()
	assert_bool(server.set_tunable("sun.beamWidth", NAN)).is_false()
	server.step_ticks(1)
	assert_float(server.get_beam_width()).is_equal_approx(200.0, 0.001)
	server.set_threaded(false)
