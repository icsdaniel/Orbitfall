extends GdUnitTestSuite
## M1 through the binding: a scripted run on the real World (place, start wave,
## enemies, kills, income, WaveCleared), the render buffers and their radius
## convention, large units and shadows, the forecast, live tuning, and equal
## hashes threaded / single-threaded / replayed.

const F: int = BatchRenderer.FLOATS_PER_INSTANCE
const FLAG_LIT: int = 1
const FLAG_SHADED: int = 2
const FLAG_TARGET: int = 4
const TEST_CONTENT: String = "user://sim_server_run_content"


func _server(dir: String = "res://content") -> SimServer:
	var server: SimServer = auto_free(SimServer.new())
	assert_array(server.load_content(dir)).is_empty()
	return server


func _building(server: SimServer, id: String) -> Dictionary:
	for def: Dictionary in server.get_building_defs():
		if str(def["id"]) == id:
			return def
	return {}


func _enemy(server: SimServer, id: String) -> Dictionary:
	for def: Dictionary in server.get_enemy_defs():
		if str(def["id"]) == id:
			return def
	return {}


func _count(events: Array[Dictionary], type: String) -> int:
	var n: int = 0
	for event: Dictionary in events:
		if str(event["type"]) == type:
			n += 1
	return n


## Steps until `type` shows up (or `max_ticks` pass); returns every event seen.
func _step_until(server: SimServer, type: String, max_ticks: int) -> Array[Dictionary]:
	var seen: Array[Dictionary] = []
	var done: int = 0
	while done < max_ticks:
		server.step_ticks(10)
		done += 10
		seen.append_array(server.drain_events())
		if _count(seen, type) > 0:
			break
	return seen


## A copy of the shipped content whose only wave is one Eclipser on lane A, with
## plenty of stardust, so shadows and HP bars can be tested through the binding.
func _write_eclipser_content() -> String:
	_copy_dir("res://content", TEST_CONTENT)
	var planet_path: String = TEST_CONTENT + "/planets/medium.json"
	var planet: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(planet_path))
	planet["startStardust"] = 2000
	_write(planet_path, JSON.stringify(planet))
	var waves_path: String = TEST_CONTENT + "/waves/m1.json"
	var waves: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(waves_path))
	waves["waves"] = [{
		"wave": 1, "lightMult": 1.0, "spawnSeconds": 5,
		"spawns": [{"lane": "A", "enemy": "eclipser", "count": 1}],
		"forecast": {"exactAhead": 1, "iconsAhead": 3},
	}]
	_write(waves_path, JSON.stringify(waves))
	return TEST_CONTENT


func _copy_dir(from: String, to: String) -> void:
	DirAccess.make_dir_recursive_absolute(to)
	for file_name: String in DirAccess.get_files_at(from):
		if file_name.ends_with(".json"):
			_write(to.path_join(file_name), FileAccess.get_file_as_string(from.path_join(file_name)))
	for dir_name: String in DirAccess.get_directories_at(from):
		_copy_dir(from.path_join(dir_name), to.path_join(dir_name))


func _write(path: String, text: String) -> void:
	var file: FileAccess = FileAccess.open(path, FileAccess.WRITE)
	file.store_string(text)
	file.close()


## Gatling under lane A, a panel in the beam, wave 1 to the end. Returns the hash.
func _play_wave_one(server: SimServer, run_seed: int) -> int:
	server.new_run(run_seed)
	server.place_building(3, int(_building(server, "gatling")["index"]))
	server.place_building(0, int(_building(server, "solar_panel")["index"]))
	server.step_ticks(2)
	server.start_wave()
	server.rotate_input(0.2)
	server.step_ticks(45)
	server.rotate_input(0.0)
	server.set_time_scale(0.75, 0.3)
	server.step_ticks(900)
	return server.get_state_hash()


func test_scripted_run_place_start_wave_kills_income_wave_cleared() -> void:
	var server: SimServer = _server()
	server.new_run(5)
	var gatling: Dictionary = _building(server, "gatling")
	var panel: Dictionary = _building(server, "solar_panel")
	var start: float = server.get_stardust()
	assert_float(start).is_greater_equal(float(gatling["cost"]) + float(panel["cost"]))

	# Lane A comes in at 30 degrees = slot 3; the sun starts at 0 degrees = slot 0.
	server.place_building(3, int(gatling["index"]))
	server.place_building(0, int(panel["index"]))
	server.place_building(3, int(panel["index"]))  # occupied
	server.step_ticks(2)
	var events: Array[Dictionary] = server.drain_events()
	assert_int(_count(events, "BuildingPlaced")).is_equal(2)
	assert_int(_count(events, "CommandRejected")).is_equal(1)
	assert_int(server.get_slot_buildings()[3]).is_equal(int(gatling["index"]))
	assert_int(server.get_slot_buildings()[0]).is_equal(int(panel["index"]))
	var after_build: float = start - float(gatling["cost"]) - float(panel["cost"])
	assert_float(server.get_stardust()).is_equal_approx(after_build, 0.001)
	assert_int(server.get_slot_flags()[0] & FLAG_LIT).is_equal(FLAG_LIT)
	assert_int(server.get_slot_flags()[18] & FLAG_LIT).is_equal(0)  # night side

	# Frozen between waves: no income, no enemies, the sun does not move.
	var sun: float = server.get_sun_bearing()
	server.step_ticks(60)
	assert_float(server.get_stardust()).is_equal_approx(after_build, 0.001)
	assert_float(server.get_sun_bearing()).is_equal(sun)
	assert_int(server.get_enemy_count()).is_equal(0)
	assert_int(server.get_phase()).is_equal(0)

	server.start_wave()
	server.step_ticks(3)
	assert_int(server.get_phase()).is_equal(1)
	assert_int(_count(server.drain_events(), "WaveStarted")).is_equal(1)
	assert_int(server.get_enemy_count()).is_greater(0)
	assert_float(server.get_income_per_second()).is_greater(0.0)
	assert_float(float(server.get_debug_stats()["lit_fraction"])).is_equal_approx(1.0, 0.001)

	var saw_target: bool = false
	var saw_shots: bool = false
	var seen: Array[Dictionary] = []
	for i: int in 400:
		server.step_ticks(10)
		seen.append_array(server.drain_events())
		saw_target = saw_target or (server.get_slot_flags()[3] & FLAG_TARGET) != 0
		saw_shots = saw_shots or server.get_projectile_count() > 0
		if _count(seen, "WaveCleared") > 0:
			break
	assert_bool(saw_target).is_true()
	assert_bool(saw_shots).is_true()
	assert_int(_count(seen, "EnemyKilled")).is_greater(0)
	assert_int(_count(seen, "StardustChanged")).is_greater(0)
	assert_int(_count(seen, "WaveCleared")).is_equal(1)
	assert_float(server.get_stardust()).is_greater(after_build)
	assert_float(server.get_sun_bearing()).is_greater(sun)
	assert_int(server.get_phase()).is_equal(0)
	assert_int(server.get_wave_index()).is_equal(1)
	assert_int(server.get_enemy_count()).is_equal(0)

	# Selling refunds part of the cost and frees the slot.
	var before_sell: float = server.get_stardust()
	server.sell_building(3)
	server.step_ticks(1)
	assert_int(server.get_slot_buildings()[3]).is_equal(-1)
	assert_float(server.get_stardust()).is_between(before_sell + 1.0, before_sell + float(gatling["cost"]))
	assert_int(_count(server.drain_events(), "BuildingSold")).is_equal(1)


func test_never_building_ends_in_game_over() -> void:
	var server: SimServer = _server()
	server.new_run(2)
	var seen: Array[Dictionary] = []
	for i: int in 2000:
		if server.get_phase() == 0:
			server.start_wave()
		server.step_ticks(30)
		seen.append_array(server.drain_events())
		if server.get_phase() >= 2:
			break
	assert_int(server.get_phase()).is_equal(3)
	assert_float(server.get_planet_hp()).is_equal(0.0)
	assert_int(_count(seen, "PlanetHit")).is_greater(0)
	assert_int(_count(seen, "GameOver")).is_equal(1)
	assert_int(_count(seen, "Victory")).is_equal(0)
	# The run is over: nothing more is accepted or simulated.
	var tick_hp: float = server.get_planet_hp()
	server.start_wave()
	server.step_ticks(30)
	assert_int(server.get_phase()).is_equal(3)
	assert_float(server.get_planet_hp()).is_equal(tick_hp)


func test_render_buffers_use_radius_scale_and_interpolate() -> void:
	var server: SimServer = _server()
	server.new_run(1)
	assert_int(server.get_enemy_buffer().size()).is_equal(0)
	server.place_building(3, int(_building(server, "gatling")["index"]))
	server.start_wave()
	server.step_ticks(30)
	var count: int = server.get_enemy_count()
	assert_int(count).is_greater(0)
	var first_wave: Dictionary = server.get_forecast(1)[0]
	var def: Dictionary = server.get_enemy_defs()[int((first_wave["entries"] as Array)[0]["enemy"])]
	var buffer: PackedFloat32Array = server.get_enemy_buffer()
	assert_int(buffer.size()).is_equal(count * F)
	# One convention everywhere: the basis is the radius (a batch shape of radius 1).
	var x_axis: Vector2 = Vector2(buffer[0], buffer[4])
	var y_axis: Vector2 = Vector2(buffer[1], buffer[5])
	assert_float(x_axis.length()).is_equal_approx(float(def["size"]), 0.001)
	assert_float(y_axis.length()).is_equal_approx(float(def["size"]), 0.001)
	var pos: Vector2 = Vector2(buffer[3], buffer[7])
	assert_float(pos.length()).is_between(float(server.get_planet_radius()), 901.0)
	# The shape's "up" (-y of the basis) points along the velocity: at the planet.
	assert_float((-y_axis).normalized().dot(-pos.normalized())).is_greater(0.99)
	assert_float(buffer[BatchRenderer.OFFSET_CUSTOM]).is_equal(float(def["shape"]))
	assert_float(buffer[BatchRenderer.OFFSET_CUSTOM + 1]).is_equal(float(def["tier"]))
	assert_float(buffer[BatchRenderer.OFFSET_CUSTOM + 3]).is_equal(0.0)  # default outline
	assert_float(buffer[BatchRenderer.OFFSET_COLOR + 3]).is_equal(1.0)

	# Half a tick short of the next tick: drawn half a tick behind the latest snapshot.
	assert_int(server.advance(1.5 / 30.0)).is_equal(1)
	assert_float(server.get_alpha()).is_equal_approx(0.5, 0.001)
	var half: PackedFloat32Array = server.get_enemy_buffer()
	server.step_ticks(0)  # alpha back to 1: exactly the latest tick
	var latest: PackedFloat32Array = server.get_enemy_buffer()
	var step: float = Vector2(latest[3] - half[3], latest[7] - half[7]).length()
	assert_float(step).is_equal_approx(float(def["speed"]) * 0.5 / 30.0, 0.01)

	# Projectiles: wait for the Gatling to open fire.
	var shots: PackedFloat32Array = PackedFloat32Array()
	for i: int in 900:
		server.step_ticks(1)
		if server.get_projectile_count() > 0:
			shots = server.get_projectile_buffer()
			break
	assert_int(shots.size()).is_greater_equal(F)
	assert_float(Vector2(shots[0], shots[4]).length()).is_between(0.5, 8.0)  # radius in world units
	assert_float(shots[BatchRenderer.OFFSET_CUSTOM + 1]).is_equal(-1.0)
	var stats: Dictionary = server.get_debug_stats()
	assert_bool(stats.has_all(["ms_per_tick", "enemies", "projectiles", "lit_fraction"])).is_true()
	assert_int(stats["projectiles"]).is_equal(server.get_projectile_count())


func test_eclipser_large_unit_shadow_intervals_and_palette() -> void:
	var server: SimServer = _server(_write_eclipser_content())
	server.set_palette({"enemy.eclipser": Color(0.2, 0.4, 0.6)})
	server.new_run(1)
	var panel: int = int(_building(server, "solar_panel")["index"])
	for slot: int in [35, 0, 1, 2]:
		server.place_building(slot, panel)
	server.step_ticks(1)
	# The ghost arc of the expected shadow is in the forecast before the wave.
	var forecast: Array[Dictionary] = server.get_forecast(3)
	assert_int(forecast.size()).is_equal(1)
	assert_bool((forecast[0]["entries"] as Array)[0]["shadow_caster"]).is_true()
	assert_int((forecast[0]["eclipser_ghost_arcs"] as PackedFloat32Array).size()).is_equal(2)
	server.start_wave()
	server.step_ticks(2)
	var large: Array[Dictionary] = server.get_large_units()
	assert_int(large.size()).is_equal(1)
	var unit: Dictionary = large[0]
	assert_bool(unit.has_all(["pos", "radius", "hp_frac", "shadow"])).is_true()
	assert_float(unit["radius"]).is_equal_approx(float(_enemy(server, "eclipser")["size"]), 0.001)
	assert_float(unit["hp_frac"]).is_equal_approx(1.0, 0.001)
	var buffer: PackedFloat32Array = server.get_enemy_buffer()
	assert_float(buffer[BatchRenderer.OFFSET_COLOR]).is_equal_approx(0.2, 0.0001)
	assert_float(buffer[BatchRenderer.OFFSET_CUSTOM]).is_equal(float(BatchRenderer.Shape.DISC))
	assert_float(Vector2(buffer[0], buffer[4]).length()).is_equal_approx(float(unit["radius"]), 0.001)

	# It flies to the sun line and parks its shadow on the panels.
	var shaded_panels: int = 0
	var events: Array[Dictionary] = []
	for i: int in 400:
		server.step_ticks(10)
		events.append_array(server.drain_events())
		shaded_panels = 0
		for slot: int in [35, 0, 1, 2]:
			if (server.get_slot_flags()[slot] & FLAG_SHADED) != 0:
				shaded_panels += 1
		if shaded_panels > 0:
			break
	assert_int(shaded_panels).is_greater(0)
	assert_int(_count(events, "ShadowChanged")).is_greater(0)
	var intervals: PackedFloat32Array = server.get_shade_intervals()
	assert_int(intervals.size()).is_equal(2)
	assert_float(intervals[1]).is_greater(intervals[0])
	assert_bool(server.get_large_units()[0]["shadow"]).is_true()
	assert_float(float(server.get_debug_stats()["lit_fraction"])).is_less(1.0)
	DirAccess.remove_absolute(TEST_CONTENT + "/waves/m1.json")


func test_forecast_reads_wave_data() -> void:
	var empty: SimServer = auto_free(SimServer.new())
	assert_array(empty.get_forecast(3)).is_empty()  # no content yet
	var server: SimServer = _server()
	server.new_run(1)
	var forecast: Array[Dictionary] = server.get_forecast(5)
	assert_int(forecast.size()).is_equal(5)
	var next: Dictionary = forecast[0]
	assert_int(next["wave"]).is_equal(1)
	assert_bool(next["exact"]).is_true()
	assert_float(next["light_mult"]).is_between(0.0, 1.0)
	var entries: Array = next["entries"]
	assert_int(entries.size()).is_greater(0)
	var entry: Dictionary = entries[0]
	assert_bool(entry.has_all(["lane_bearing", "enemy", "count", "shadow_caster"])).is_true()
	assert_int(entry["count"]).is_greater(0)
	assert_float(entry["lane_bearing"]).is_equal_approx(deg_to_rad(30.0), 0.0001)
	# +2 and +3: icons without counts; beyond: light level only.
	assert_bool(forecast[1]["exact"]).is_false()
	var icons: Array = forecast[1]["entries"]
	assert_int(icons.size()).is_greater(0)
	assert_int((icons[0] as Dictionary)["count"]).is_equal(-1)
	assert_int((forecast[2]["entries"] as Array).size()).is_greater(0)
	assert_int((forecast[3]["entries"] as Array).size()).is_equal(0)
	assert_bool(forecast[3].has("light_mult")).is_true()
	assert_bool(next["eclipser_ghost_arcs"] is PackedFloat32Array).is_true()
	assert_int(server.get_forecast(100).size()).is_equal(8)
	# During a wave the wave in flight comes first, then the upcoming ones.
	server.start_wave()
	server.step_ticks(2)
	var during: Array[Dictionary] = server.get_forecast(3)
	assert_int(during.size()).is_equal(3)
	assert_int(during[0]["wave"]).is_equal(1)
	assert_bool(during[0]["exact"]).is_true()
	assert_int(during[1]["wave"]).is_equal(2)
	assert_bool(during[1]["exact"]).is_true()


func test_set_tunable_changes_the_running_world() -> void:
	var server: SimServer = _server()
	server.set_threaded(true)
	server.new_run(1)
	server.start_wave()
	server.step_ticks(30)
	var before: float = server.get_sun_bearing()
	server.step_ticks(30)
	var drift: float = server.get_sun_bearing() - before
	assert_float(drift).is_greater(0.0)
	# Editing sun speed changes the drift live (M1.11).
	assert_bool(server.set_tunable("sun.speedDegPerSec", 12.0)).is_true()
	assert_bool(server.set_tunable("sun.beamWidth", 60.0)).is_true()
	assert_bool(server.set_tunable("planet.surfaceTimeScale", 0.5)).is_true()
	before = server.get_sun_bearing()
	server.step_ticks(30)
	assert_float(server.get_sun_bearing() - before).is_equal_approx(deg_to_rad(12.0), 0.001)
	assert_float(server.get_beam_width()).is_equal_approx(60.0, 0.001)
	assert_float(server.get_surface_time_scale()).is_equal_approx(0.5, 0.0001)
	server.set_threaded(false)


func test_real_run_hashes_match_threaded_single_and_replay() -> void:
	var single: SimServer = _server()
	var threaded: SimServer = _server()
	threaded.set_threaded(true)
	var a: int = _play_wave_one(single, 21)
	var b: int = _play_wave_one(threaded, 21)
	assert_int(b).is_equal(a)
	assert_int(threaded.get_tick()).is_equal(single.get_tick())
	assert_float(threaded.get_stardust()).is_equal(single.get_stardust())
	threaded.set_threaded(false)
	var copy: SimServer = _server()
	assert_bool(copy.replay(21, single.get_command_log())).is_true()
	assert_int(copy.get_state_hash()).is_equal(a)
	assert_int(_play_wave_one(_server(), 22)).is_not_equal(a)


func test_new_content_ends_the_run_in_progress() -> void:
	var server: SimServer = _server()
	server.new_run(1)
	server.start_wave()
	server.step_ticks(30)
	assert_int(server.get_enemy_count()).is_greater(0)
	assert_array(server.load_content()).is_empty()
	assert_int(server.get_enemy_count()).is_equal(0)
	assert_int(server.get_tick()).is_equal(0)
	server.new_run(1)
	assert_int(server.get_wave_count()).is_equal(8)


func test_game_runs_on_the_real_sim_by_default() -> void:
	var game: Game = auto_free(Game.new())
	add_child(game)
	assert_bool(game.is_mock).is_false()
	assert_bool(game.sim is SimServer).is_true()
	assert_bool(game.sim.is_threaded()).is_equal(not OS.has_feature("web"))
	assert_int(game.sim.get_phase()).is_equal(0)
	game.planet_view.sync()
	assert_int(game.planet_view.slot_count).is_equal(36)
	# Slow-time numbers come from content through the sim, not from client_config.
	assert_float(game.bands.surface_time_scale).is_equal(float(game.sim.get_surface_time_scale()))
	assert_float(game.bands.ease_seconds).is_equal(float(game.sim.get_surface_time_ease_seconds()))
	var gatling: int = -1
	for def: Dictionary in game.sim.get_building_defs():
		if str(def["id"]) == "gatling":
			gatling = int(def["index"])
	game.sim.place_building(3, gatling)
	game.sim.start_wave()
	game.sim.step_ticks(60)
	game.bands.update()
	game.sim.step_ticks(30)
	assert_int(game.sim.get_phase()).is_equal(1)
	assert_int(game.sim.get_enemy_count()).is_greater(0)
	# In the Surface band during a wave the sim was asked for slow time.
	assert_float(game.sim.get_time_scale()).is_equal_approx(game.bands.surface_time_scale, 0.001)
	game.entity_view.sync()
	assert_int(game.entity_view.renderer.get_count(EntityView.ARCHETYPE_ENEMIES)).is_equal(
		game.sim.get_enemy_count())
	game.sim.set_threaded(false)
