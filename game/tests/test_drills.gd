extends ClientTestBase
## M2.7: drills, instability and coolers through the binding (getters, events,
## nothing damaged), the mock's copy of the same API, and the client: both
## buildings in the build menu, the HUD instability meter and the drill heat
## state the planet view draws.

const RICH_CONTENT: String = "user://drill_test_content"
const INSTABILITY_KEYS: Array[String] = [
	"level", "threshold", "load", "cooling", "drills", "running", "overheated", "coolers",
	"drill_income", "overheat_seconds",
]
const DRILL_KEYS: Array[String] = ["site", "building", "heat", "overheated", "cooldown", "cooldown_max"]
const NIGHT_SLOTS: Array[int] = [20, 21, 22, 23, 24]


## The shipped content with 5000 start stardust.
func _rich_content() -> String:
	_copy_dir("res://content", RICH_CONTENT)
	var planet_path: String = RICH_CONTENT + "/planets/medium.json"
	var planet: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(planet_path))
	planet["startStardust"] = 5000
	_write(planet_path, JSON.stringify(planet))
	return RICH_CONTENT


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


func _index(sim: Node, id: String) -> int:
	for def: Dictionary in sim.get_building_defs():
		if str(def["id"]) == id:
			return int(def["index"])
	return -1


func _server() -> SimServer:
	var server: SimServer = auto_free(SimServer.new())
	assert_array(server.load_content(_rich_content())).is_empty()
	server.new_run(5)
	return server


func _mock() -> MockSimServer:
	var sim: MockSimServer = auto_free(MockSimServer.new())
	sim.auto_advance = false
	add_child(sim)
	sim.load_content(_rich_content())
	sim.new_run(5, 0)
	return sim


## Five drills on the night side (above the threshold), wave started, ~4 s run.
## Returns the drained events.
func _overheat_run(sim: Node) -> Array[Dictionary]:
	var drill: int = _index(sim, "drill")
	for slot: int in NIGHT_SLOTS:
		sim.place_building(slot, drill)
	sim.step_ticks(1)
	sim.start_wave()
	sim.step_ticks(120)
	var out: Array[Dictionary] = []
	out.assign(sim.drain_events())
	return out


func _count(events: Array[Dictionary], type: String) -> int:
	var n: int = 0
	for e: Dictionary in events:
		n += 1 if str(e["type"]) == type else 0
	return n


func test_building_defs_list_drill_and_cooler() -> void:
	for sim: Node in [_server(), _mock()]:
		var drill: Dictionary = sim.get_building_defs()[_index(sim, "drill")]
		var cooler: Dictionary = sim.get_building_defs()[_index(sim, "cooler")]
		assert_str(str(drill["kind"])).is_equal("drill")
		assert_float(float(drill["drill_rate"])).is_greater(0.0)
		assert_float(float(drill["instability"])).is_greater(0.0)
		assert_str(str(cooler["kind"])).is_equal("cooler")
		assert_float(float(cooler["cooling"])).is_greater(0.0)
		assert_float(float(cooler["lit_cooling_mult"])).is_less(1.0)


func test_overheat_through_the_binding_and_the_mock_damages_nothing() -> void:
	for sim: Node in [_server(), _mock()]:
		var drill: int = _index(sim, "drill")
		var hp: float = sim.get_planet_hp()
		var events: Array[Dictionary] = _overheat_run(sim)
		assert_int(_count(events, "DrillOverheated")).is_greater_equal(1)
		assert_int(_count(events, "BuildingSold")).is_equal(0)
		var info: Dictionary = sim.get_instability()
		for key: String in INSTABILITY_KEYS:
			assert_bool(info.has(key)).override_failure_message("missing " + key).is_true()
		assert_int(int(info["drills"])).is_equal(5)
		assert_int(int(info["overheated"])).is_greater_equal(1)
		assert_int(int(info["running"])).is_equal(5 - int(info["overheated"]))
		var states: Array = sim.get_drill_states()
		assert_int(states.size()).is_equal(5)
		var hot: int = 0
		for state: Dictionary in states:
			for key: String in DRILL_KEYS:
				assert_bool(state.has(key)).is_true()
			if bool(state["overheated"]):
				hot += 1
				assert_float(float(state["cooldown"])).is_greater(0.0)
		assert_int(hot).is_equal(int(info["overheated"]))
		# Every drill still stands; no landing reached the planet in these 4 s.
		var buildings: PackedInt32Array = sim.get_slot_buildings()
		for slot: int in NIGHT_SLOTS:
			assert_int(buildings[slot]).is_equal(drill)
		assert_float(sim.get_planet_hp()).is_equal(hp)
		assert_float(sim.get_drill_income_per_second()).is_greater(0.0)


func test_cooler_lowers_instability_in_both_servers() -> void:
	for sim: Node in [_server(), _mock()]:
		var drill: int = _index(sim, "drill")
		for slot: int in [20, 21, 22, 23]:
			sim.place_building(slot, drill)
		sim.step_ticks(1)
		var before: float = float(sim.get_instability()["level"])
		sim.place_building(19, _index(sim, "cooler"))
		sim.step_ticks(1)
		var info: Dictionary = sim.get_instability()
		assert_float(float(info["cooling"])).is_greater(0.0)
		assert_float(float(info["level"])).is_less(before)


func test_build_menu_offers_drill_and_cooler_and_hud_meter_shows_up() -> void:
	var game: Game = auto_free(Game.new())
	game.force_mock = true
	game.planet_picker_enabled = false
	add_child(game)
	var sim: MockSimServer = game.sim as MockSimServer
	sim.auto_advance = false
	sim.debug_set_stardust(1000.0)
	game.planet_view.sync()
	game.hud.build_menu.open_empty(20, game.planet_view.building_defs, 1000.0)
	assert_object(game.hud.build_menu.building_button(_index(sim, "drill"))).is_not_null()
	assert_object(game.hud.build_menu.building_button(_index(sim, "cooler"))).is_not_null()
	game.hud.build_menu.close()
	game.hud.refresh()
	assert_bool(game.hud.instability.visible).is_false()  # nothing built yet
	sim.place_building(20, _index(sim, "drill"))
	sim.step_ticks(1)
	game.hud.refresh()
	game.planet_view.sync()
	assert_bool(game.hud.instability.visible).is_true()
	assert_bool(game.planet_view.drill_states.has(20)).is_true()
	# Over the threshold the meter turns hot.
	for slot: int in [21, 22, 23, 24]:
		sim.place_building(slot, _index(sim, "drill"))
	sim.step_ticks(1)
	game.hud.refresh()
	assert_float(game.hud.instability.fill_fraction()).is_greater(0.5)
	assert_that(game.hud.instability.bar_color()).is_equal(Palette.DRILL_HOT)
