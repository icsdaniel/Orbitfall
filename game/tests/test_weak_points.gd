extends GdUnitTestSuite
## M2.4 through the binding and the client: Bulwark and Lance content, the
## weak-point hint (which way and how far to turn), turning by it lines the Lance
## up and scores weak-point hits (debug stats and WeakPointHit events), the aim
## assist label, and the mock giving the same hint.


func _server() -> SimServer:
	var server: SimServer = auto_free(SimServer.new())
	assert_array(server.load_content("res://content")).is_empty()
	return server


func _index(defs: Array[Dictionary], id: String) -> int:
	for def: Dictionary in defs:
		if str(def["id"]) == id:
			return int(def["index"])
	return -1


## Steps until the hint is active (a Bulwark is out); returns it.
func _wait_for_hint(sim: Object, max_ticks: int) -> Dictionary:
	var done: int = 0
	while done < max_ticks:
		sim.step_ticks(10)
		done += 10
		var hint: Dictionary = sim.get_weak_point_hint()
		if bool(hint.get("active", false)):
			return hint
	return {"active": false}


func test_trial_wave_set_hint_turn_and_weak_hits() -> void:
	var server: SimServer = _server()
	var trial: int = server.get_wave_sets().find("trial_bulwark")
	assert_int(trial).is_greater(0)
	server.new_run(3, trial)
	assert_bool(bool(server.get_weak_point_hint()["active"])).is_false()  # nothing out yet
	var lance: int = _index(server.get_building_defs(), "lance")
	server.place_building(0, lance, 0.0, 0)
	server.start_wave()
	var hint: Dictionary = _wait_for_hint(server, 600)
	assert_bool(bool(hint["active"])).is_true()
	assert_int(int(hint["site"])).is_equal(0)
	var turn: float = float(hint["turn"])
	# The Bulwark comes down lane A (30°, ±25°); the Lance sits at 0°.
	assert_float(rad_to_deg(float(hint["bearing"]))).is_between(0.0, 60.0)
	assert_float(turn).is_equal_approx(wrapf(float(hint["bearing"]) - server.get_planet_rotation(), -PI, PI), 0.01)
	assert_float(float(hint["window"])).is_equal_approx(deg_to_rad(8.0), 0.001)
	# Turn the way the hint says until lined up, like a player on the buttons.
	var ticks: int = 0
	while ticks < 300 and absf(float(server.get_weak_point_hint()["turn"])) > deg_to_rad(2.0):
		var t: float = float(server.get_weak_point_hint()["turn"])
		server.rotate_input(clampf(t / deg_to_rad(20.0), -1.0, 1.0))
		server.step_ticks(1)
		ticks += 1
	server.rotate_input(0.0)
	assert_float(absf(float(server.get_weak_point_hint()["turn"]))).is_less(deg_to_rad(8.0))
	# Hold it under the Bulwark: the Lance scores weak-point hits.
	var weak_events: int = 0
	for i: int in 60:
		var t2: float = float(server.get_weak_point_hint().get("turn", 0.0))
		server.rotate_input(clampf(t2 / deg_to_rad(20.0), -1.0, 1.0))
		server.step_ticks(3)
		for event: Dictionary in server.drain_events():
			if str(event["type"]) == "WeakPointHit":
				weak_events += 1
				assert_int(int(event["b"])).is_equal(lance)
	assert_int(weak_events).is_greater(0)
	assert_int(int(server.get_debug_stats()["weak_hits"])).is_greater_equal(weak_events)


func test_no_hint_without_a_precision_tower() -> void:
	var server: SimServer = _server()
	server.new_run(3, server.get_wave_sets().find("trial_bulwark"))
	server.place_building(0, _index(server.get_building_defs(), "gatling"), 0.0, 0)
	server.start_wave()
	var hint: Dictionary = _wait_for_hint(server, 400)
	assert_bool(bool(hint["active"])).is_false()


func test_assist_label_names_the_button_and_the_degrees() -> void:
	var assist: WeakPointAssist = auto_free(WeakPointAssist.new())
	assist.hint = {"active": false}
	assert_str(assist.label_text()).is_empty()
	assist.hint = {"active": true, "turn": deg_to_rad(23.0), "bearing": 1.0, "window": deg_to_rad(8.0)}
	assert_str(assist.label_text()).is_equal("23° >")
	assist.hint["turn"] = deg_to_rad(-41.0)
	assert_str(assist.label_text()).is_equal("< 41°")
	assist.hint["turn"] = deg_to_rad(5.0)
	assert_bool(assist.is_lined_up()).is_true()
	assert_str(assist.label_text()).is_equal("WEAK")


func test_mock_gives_the_same_hint() -> void:
	var sim: MockSimServer = auto_free(MockSimServer.new())
	sim.auto_advance = false
	add_child(sim)
	sim.load_content("res://content")
	sim.new_run(7, 0)
	assert_array(sim.get_wave_sets()).contains(["m1", "trial_bulwark"])
	assert_bool(bool(sim.get_weak_point_hint()["active"])).is_false()
	sim.debug_set_stardust(1000.0)
	sim.place_building(9, _index(sim.get_building_defs(), "lance"), 0.0, 0)  # 90°
	sim.debug_spawn("bulwark", Vector2.from_angle(deg_to_rad(60.0)) * 300.0)
	var hint: Dictionary = sim.get_weak_point_hint()
	assert_bool(bool(hint["active"])).is_true()
	assert_int(int(hint["site"])).is_equal(9)
	assert_float(rad_to_deg(float(hint["turn"]))).is_equal_approx(-30.0, 0.5)
	assert_float(float(hint["window"])).is_equal_approx(deg_to_rad(8.0), 0.001)
