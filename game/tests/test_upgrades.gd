extends ClientTestBase
## M2.2: upgrade paths through the binding (command, tiers, preview, refund,
## replay), the mock's copy of the same API, and the upgrade UI in the build menu
## (both paths, current tiers, cost in stardust, stats preview diff, Build view only).

const RICH_CONTENT: String = "user://upgrade_test_content"
const PREVIEW_KEYS: Array[String] = [
	"path", "id", "name", "tier", "max_tier", "cap", "has_next", "next_name", "cost",
	"affordable", "can_upgrade", "locked", "before", "after", "changes",
]


func _server(dir: String = "res://content") -> SimServer:
	var server: SimServer = auto_free(SimServer.new())
	assert_array(server.load_content(dir)).is_empty()
	server.new_run(5)
	return server


## The shipped content with 5000 start stardust, so whole trees can be bought.
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


func _events(server: Node, type: String) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	for event: Dictionary in server.drain_events():
		if str(event["type"]) == type:
			out.append(event)
	return out


func _make_game() -> Game:
	var game: Game = auto_free(Game.new())
	game.force_mock = true
	game.planet_picker_enabled = false  # M2.8
	add_child(game)
	var sim: MockSimServer = game.sim as MockSimServer
	sim.auto_advance = false
	game.planet_view.view_zoom = game.camera.get_zoom_value()
	game.planet_view.sync()
	sim.clear_calls()
	return game


func _tap_slot(game: Game, slot: int) -> void:
	game.input_router.advance(1.0)
	tap(game.input_router, game.slot_screen_position(slot))


## A game on the mock with a gatling on slot 9, its menu open in the Build view.
func _game_with_gatling(stardust: float = 1000.0) -> Game:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	sim.debug_set_stardust(stardust + 60.0)
	sim.place_building(9, _index(sim, "gatling"), 0.0)
	sim.clear_calls()
	_tap_slot(game, 9)
	return game


# --- binding -------------------------------------------------------------------

func test_upgrade_building_pays_stardust_and_raises_the_tier() -> void:
	var server: SimServer = _server()
	server.place_building(3, _index(server, "gatling"))
	server.step_ticks(1)
	assert_array(server.get_slot_tiers(3)).is_equal(PackedInt32Array([0, 0]))
	assert_float(server.get_stardust()).is_equal(90.0)
	server.drain_events()

	server.upgrade_building(3, 0)
	server.step_ticks(1)
	assert_array(server.get_slot_tiers(3)).is_equal(PackedInt32Array([1, 0]))
	assert_float(server.get_stardust()).is_equal(55.0)
	var upgraded: Array[Dictionary] = _events(server, "BuildingUpgraded")
	assert_int(upgraded.size()).is_equal(1)
	assert_int(int(upgraded[0]["a"])).is_equal(3)
	assert_int(int(upgraded[0]["b"])).is_equal(0)
	assert_float(float(upgraded[0]["x"])).is_equal(1.0)
	assert_float(float(upgraded[0]["value"])).is_equal(35.0)

	# Empty and invalid slots have no tiers.
	assert_array(server.get_slot_tiers(4)).is_equal(PackedInt32Array([0, 0]))
	assert_array(server.get_slot_tiers(-1)).is_equal(PackedInt32Array([0, 0]))
	assert_array(server.get_slot_tiers(999)).is_equal(PackedInt32Array([0, 0]))


func test_preview_gives_cost_and_the_stats_before_and_after() -> void:
	var server: SimServer = _server()
	server.place_building(3, _index(server, "gatling"))
	server.step_ticks(1)
	var preview: Dictionary = server.get_upgrade_preview(3, 0)
	for key: String in PREVIEW_KEYS:
		assert_bool(preview.has(key)).override_failure_message("preview lacks " + key).is_true()
	assert_str(str(preview["id"])).is_equal("rapid")
	assert_str(str(preview["name"])).is_equal("Rapid Fire")
	assert_str(str(preview["next_name"])).is_equal("Oiled Feed")
	assert_int(int(preview["tier"])).is_equal(0)
	assert_int(int(preview["max_tier"])).is_equal(4)
	assert_int(int(preview["cap"])).is_equal(4)
	assert_float(float(preview["cost"])).is_equal(35.0)
	assert_bool(preview["can_upgrade"]).is_true()
	assert_bool(preview["locked"]).is_false()
	assert_float(float(preview["before"]["fire_rate"])).is_equal_approx(5.0, 0.001)
	assert_float(float(preview["after"]["fire_rate"])).is_equal_approx(6.5, 0.001)
	assert_float(float(preview["after"]["range"])).is_equal_approx(350.0, 0.001)
	var changes: Array = preview["changes"]
	assert_int(changes.size()).is_equal(1)
	assert_str(str(changes[0]["stat"])).is_equal("fire_rate")
	assert_float(float(changes[0]["before"])).is_equal_approx(5.0, 0.001)
	assert_float(float(changes[0]["after"])).is_equal_approx(6.5, 0.001)

	# No such slot, building or path.
	assert_dict(server.get_upgrade_preview(4, 0)).is_empty()
	assert_dict(server.get_upgrade_preview(3, 2)).is_empty()
	assert_dict(server.get_upgrade_preview(3, -1)).is_empty()
	assert_dict(server.get_slot_upgrades(4)).is_empty()

	var info: Dictionary = server.get_slot_upgrades(3)
	assert_int(int(info["building"])).is_equal(_index(server, "gatling"))
	assert_array(info["tiers"]).is_equal(PackedInt32Array([0, 0]))
	assert_int((info["paths"] as Array).size()).is_equal(2)
	assert_float(float(info["stats"]["damage"])).is_equal_approx(1.0, 0.001)
	assert_float(float(info["spent"])).is_equal(0.0)
	assert_float(float(info["sell_refund"])).is_equal_approx(42.0, 0.001)


func test_unaffordable_upgrade_is_rejected_with_its_cost() -> void:
	var server: SimServer = _server()
	server.place_building(3, _index(server, "gatling"))
	server.upgrade_building(3, 0)  # 35
	server.upgrade_building(3, 1)  # 35, leaves 20
	server.step_ticks(1)
	server.drain_events()
	var preview: Dictionary = server.get_upgrade_preview(3, 0)
	assert_bool(preview["can_upgrade"]).is_false()
	assert_bool(preview["affordable"]).is_false()
	assert_bool(preview["locked"]).is_false()
	server.upgrade_building(3, 0)  # 55
	server.step_ticks(1)
	assert_array(server.get_slot_tiers(3)).is_equal(PackedInt32Array([1, 1]))
	assert_float(server.get_stardust()).is_equal(20.0)
	var rejected: Array[Dictionary] = _events(server, "CommandRejected")
	assert_int(rejected.size()).is_equal(1)
	assert_int(int(rejected[0]["b"])).is_equal(4)  # Unaffordable
	assert_float(float(rejected[0]["value"])).is_equal(55.0)


func test_paths_are_exclusive_past_tier_two_and_selling_refunds_upgrades() -> void:
	var server: SimServer = _server(_rich_content())
	server.place_building(3, _index(server, "cannon"))
	for i: int in 2:
		server.upgrade_building(3, 1)
	for i: int in 4:
		server.upgrade_building(3, 0)
	server.step_ticks(1)
	assert_array(server.get_slot_tiers(3)).is_equal(PackedInt32Array([4, 2]))
	server.drain_events()

	var top: Dictionary = server.get_upgrade_preview(3, 0)
	assert_bool(top["has_next"]).is_false()
	assert_bool(top["can_upgrade"]).is_false()
	var capped: Dictionary = server.get_upgrade_preview(3, 1)
	assert_bool(capped["has_next"]).is_true()
	assert_bool(capped["locked"]).is_true()
	assert_bool(capped["can_upgrade"]).is_false()
	assert_int(int(capped["cap"])).is_equal(2)

	server.upgrade_building(3, 1)
	server.upgrade_building(3, 0)
	server.step_ticks(1)
	assert_array(server.get_slot_tiers(3)).is_equal(PackedInt32Array([4, 2]))
	var rejected: Array[Dictionary] = _events(server, "CommandRejected")
	assert_int(rejected.size()).is_equal(2)
	for event: Dictionary in rejected:
		assert_int(int(event["b"])).is_equal(11)  # UpgradeBlocked

	# 110 + (65 + 100 + 155 + 240) + (65 + 100) = 835 spent in all; 70% back.
	var info: Dictionary = server.get_slot_upgrades(3)
	assert_float(float(info["spent"])).is_equal(725.0)
	assert_float(float(info["sell_refund"])).is_equal_approx(584.5, 0.01)
	assert_float(float(info["stats"]["damage"])).is_equal_approx(7.5, 0.001)
	var before: float = server.get_stardust()
	assert_float(before).is_equal(5000.0 - 835.0)
	server.sell_building(3)
	server.step_ticks(1)
	assert_float(server.get_stardust()).is_equal_approx(before + 584.5, 0.01)
	assert_array(server.get_slot_tiers(3)).is_equal(PackedInt32Array([0, 0]))


func test_upgrades_replay_from_the_command_log() -> void:
	var live: SimServer = _server()
	live.place_building(3, _index(live, "gatling"))
	live.place_building(0, _index(live, "solar_panel"))
	live.upgrade_building(3, 0)
	live.upgrade_building(0, 1)  # unaffordable by then: rejected, still logged
	live.step_ticks(2)
	live.start_wave()
	live.step_ticks(300)
	live.upgrade_building(3, 1)
	live.step_ticks(300)
	var copy: SimServer = _server()
	assert_bool(copy.replay(5, live.get_command_log())).is_true()
	assert_int(copy.get_state_hash()).is_equal(live.get_state_hash())
	assert_array(copy.get_slot_tiers(3)).is_equal(live.get_slot_tiers(3))
	assert_int(live.get_slot_tiers(3)[0]).is_equal(1)

	# The same run without the first upgrade ends elsewhere.
	var plain: SimServer = _server()
	plain.place_building(3, _index(plain, "gatling"))
	plain.place_building(0, _index(plain, "solar_panel"))
	plain.step_ticks(2)
	plain.start_wave()
	plain.step_ticks(600)
	assert_int(plain.get_state_hash()).is_not_equal(live.get_state_hash())


# --- mock ----------------------------------------------------------------------

func test_mock_has_the_m22_methods_and_matches_the_real_preview() -> void:
	var mock: MockSimServer = make_sim()
	for method: String in ["upgrade_building", "get_slot_tiers", "get_upgrade_preview", "get_slot_upgrades"]:
		assert_bool(mock.has_method(method)).override_failure_message("mock lacks " + method).is_true()
		assert_bool(ClassDB.class_has_method(&"SimServer", method)).is_true()
	var server: SimServer = _server()
	for id: String in ["gatling", "cannon", "solar_panel"]:
		mock.new_run(7, 0)
		server.new_run(5)
		mock.place_building(3, _index(mock, id), 0.0)
		server.place_building(3, _index(server, id))
		mock.upgrade_building(3, 1)
		server.upgrade_building(3, 1)
		server.step_ticks(1)
		assert_array(mock.get_slot_tiers(3)).is_equal(server.get_slot_tiers(3))
		assert_float(mock.get_stardust()).is_equal_approx(server.get_stardust(), 0.001)
		for path: int in 2:
			var a: Dictionary = mock.get_upgrade_preview(3, path)
			var b: Dictionary = server.get_upgrade_preview(3, path)
			for key: String in PREVIEW_KEYS:
				assert_bool(a.has(key)).override_failure_message("mock preview lacks " + key).is_true()
			for key: String in ["id", "name", "next_name"]:
				assert_str(str(a[key])).is_equal(str(b[key]))
			for key: String in ["tier", "max_tier", "cap"]:
				assert_int(int(a[key])).is_equal(int(b[key]))
			for key: String in ["has_next", "affordable", "can_upgrade", "locked"]:
				assert_bool(a[key]).is_equal(b[key])
			assert_float(float(a["cost"])).is_equal_approx(float(b["cost"]), 0.001)
			for stat: String in (b["after"] as Dictionary):
				assert_float(float(a["after"][stat])).is_equal_approx(float(b["after"][stat]), 0.001)
				assert_float(float(a["before"][stat])).is_equal_approx(float(b["before"][stat]), 0.001)
			assert_int((a["changes"] as Array).size()).is_equal((b["changes"] as Array).size())
		var mock_info: Dictionary = mock.get_slot_upgrades(3)
		var real_info: Dictionary = server.get_slot_upgrades(3)
		assert_float(float(mock_info["spent"])).is_equal_approx(float(real_info["spent"]), 0.001)
		assert_float(float(mock_info["sell_refund"])).is_equal_approx(float(real_info["sell_refund"]), 0.001)


func test_mock_exclusivity_rejection_and_refund() -> void:
	var mock: MockSimServer = make_sim()
	mock.debug_set_stardust(5000.0)
	mock.place_building(3, _index(mock, "gatling"), 0.0)
	for i: int in 3:
		mock.upgrade_building(3, 0)
	for i: int in 3:
		mock.upgrade_building(3, 1)  # the third is capped
	assert_array(mock.get_slot_tiers(3)).is_equal(PackedInt32Array([3, 2]))
	assert_bool(mock.get_upgrade_preview(3, 1)["locked"]).is_true()
	var rejected: Array[Dictionary] = _events(mock, "CommandRejected")
	assert_int(rejected.size()).is_equal(1)
	assert_int(int(rejected[0]["b"])).is_equal(11)
	# 60 + 35 + 55 + 85 + 35 + 55 = 325 spent; the mock refunds whole stardust.
	var before: float = mock.get_stardust()
	assert_float(before).is_equal(5000.0 - 325.0)
	mock.sell_building(3)
	assert_float(mock.get_stardust()).is_equal(before + floorf(325.0 * 0.7))
	assert_array(mock.get_slot_tiers(3)).is_equal(PackedInt32Array([0, 0]))
	# Unaffordable.
	mock.debug_set_stardust(70.0)
	mock.place_building(3, _index(mock, "gatling"), 0.0)
	mock.drain_events()
	mock.upgrade_building(3, 0)
	rejected = _events(mock, "CommandRejected")
	assert_int(rejected.size()).is_equal(1)
	assert_int(int(rejected[0]["b"])).is_equal(4)
	assert_array(mock.get_slot_tiers(3)).is_equal(PackedInt32Array([0, 0]))


func test_mock_upgraded_panel_earns_more() -> void:
	var mock: MockSimServer = make_sim()
	mock.debug_set_stardust(1000.0)
	mock.place_building(0, _index(mock, "solar_panel"), 0.0)  # slot 0 faces the sun
	var base: float = mock.get_income_per_second()
	assert_float(base).is_greater(0.0)
	mock.upgrade_building(0, 0)  # income x1.25
	assert_float(mock.get_income_per_second()).is_equal_approx(base * 1.25, 0.001)


# --- UI ------------------------------------------------------------------------

func test_menu_for_a_built_tower_shows_both_paths_tiers_cost_and_diff() -> void:
	var game: Game = _game_with_gatling()
	var menu: BuildMenu = game.hud.build_menu
	assert_bool(menu.is_open()).is_true()
	assert_bool(menu.upgrades.visible).is_true()
	assert_int(menu.upgrades.path_count()).is_equal(2)
	assert_bool(menu.sell_button().visible).is_true()
	assert_str(menu.sell_button().text).is_equal("Sell  +42")
	assert_str(menu.upgrades.stats_label().text).is_equal(
		"Damage 1   Fire rate 5/s   Range 350   Arc ±40°")

	assert_str(menu.upgrades.title_label(0).text).is_equal("Rapid Fire   tier 0/4")
	assert_str(menu.upgrades.title_label(1).text).is_equal("Long Reach   tier 0/4")
	# Cost in stardust on the button, the diff always visible beside it (no hover).
	assert_str(menu.upgrades.upgrade_button(0).text).is_equal("[1] Oiled Feed   35")
	assert_str(menu.upgrades.upgrade_button(1).text).is_equal("[2] Long Barrel   35")
	assert_bool(menu.upgrades.upgrade_button(0).disabled).is_false()
	assert_bool(menu.upgrades.diff_label(0).visible).is_true()
	assert_str(menu.upgrades.diff_label(0).text).is_equal("Fire rate 5/s -> 6.5/s")
	assert_str(menu.upgrades.diff_label(1).text).is_equal("Range 350 -> 402.5")
	assert_str(menu.upgrades.upgrade_button(0).tooltip_text).is_empty()


func test_pressing_an_upgrade_sends_the_command_and_the_menu_follows() -> void:
	var game: Game = _game_with_gatling()
	var sim: MockSimServer = game.sim as MockSimServer
	var menu: BuildMenu = game.hud.build_menu
	menu.upgrades.upgrade_button(0).pressed.emit()
	assert_int(sim.calls_to(&"upgrade_building").size()).is_equal(1)
	assert_array(sim.calls_to(&"upgrade_building")[0]).is_equal([9, 0])
	assert_array(sim.get_slot_tiers(9)).is_equal(PackedInt32Array([1, 0]))
	game._process(0.016)
	# The menu stays open on the same building and shows the next tier.
	assert_bool(menu.is_open()).is_true()
	assert_int(game.selected_slot).is_equal(9)
	assert_str(menu.upgrades.title_label(0).text).is_equal("Rapid Fire   tier 1/4")
	assert_str(menu.upgrades.upgrade_button(0).text).is_equal("[1] Twin Barrels   55")
	assert_str(menu.upgrades.diff_label(0).text).is_equal("Fire rate 6.5/s -> 8.45/s")
	assert_str(menu.upgrades.diff_label(1).text).is_equal("Range 350 -> 402.5")
	assert_str(menu.upgrades.stats_label().text).contains("Fire rate 6.5/s")
	assert_str(menu.sell_button().text).is_equal("Sell  +66")  # (60 + 35) x 0.7

	# The fourth tier changes two stats: one line each.
	sim.upgrade_building(9, 0)
	sim.upgrade_building(9, 0)
	game._process(0.016)
	assert_str(menu.upgrades.diff_label(0).text).is_equal(
		"Fire rate 11.41/s -> 15.97/s\nArc ±40° -> ±50°")
	# The selected tower's drawn sector follows its upgraded arc.
	sim.upgrade_building(9, 0)
	game._process(0.016)
	assert_float(float(game.planet_view.selected_stats.get("arc", 0.0))).is_equal_approx(
		deg_to_rad(50.0), 0.001)
	game.clear_selection()
	assert_bool(game.planet_view.selected_stats.is_empty()).is_true()


func test_number_keys_upgrade_like_the_buttons() -> void:
	var game: Game = _game_with_gatling()
	var sim: MockSimServer = game.sim as MockSimServer
	game.hud.build_menu.upgrades._unhandled_key_input(key(KEY_2, true))
	assert_array(sim.calls_to(&"upgrade_building")[0]).is_equal([9, 1])
	assert_array(sim.get_slot_tiers(9)).is_equal(PackedInt32Array([0, 1]))


func test_unaffordable_tier_is_disabled_and_names_its_cost() -> void:
	var game: Game = _game_with_gatling(20.0)
	var sim: MockSimServer = game.sim as MockSimServer
	var menu: BuildMenu = game.hud.build_menu
	var button: Button = menu.upgrades.upgrade_button(0)
	assert_bool(button.disabled).is_true()
	assert_str(button.text).is_equal("Oiled Feed   need 35")
	assert_str(menu.upgrades.diff_label(0).text).is_equal("Fire rate 5/s -> 6.5/s")  # still previewed
	button.pressed.emit()
	game.hud.build_menu.upgrades._unhandled_key_input(key(KEY_1, true))
	assert_int(sim.calls_to(&"upgrade_building").size()).is_equal(0)
	# Stardust arriving enables it without reopening the menu.
	sim.debug_set_stardust(40.0)
	game._process(0.016)
	assert_bool(button.disabled).is_false()
	assert_str(button.text).is_equal("[1] Oiled Feed   35")


func test_committing_to_a_path_locks_the_other_and_the_top_tier_ends_it() -> void:
	var game: Game = _game_with_gatling(5000.0)
	var sim: MockSimServer = game.sim as MockSimServer
	var menu: BuildMenu = game.hud.build_menu
	for i: int in 2:
		sim.upgrade_building(9, 1)
	for i: int in 4:
		sim.upgrade_building(9, 0)
	game._process(0.016)
	assert_str(menu.upgrades.title_label(0).text).is_equal("Rapid Fire   tier 4/4")
	assert_str(menu.upgrades.upgrade_button(0).text).is_equal("Top tier")
	assert_bool(menu.upgrades.upgrade_button(0).disabled).is_true()
	assert_bool(menu.upgrades.diff_label(0).visible).is_false()
	assert_str(menu.upgrades.title_label(1).text).is_equal("Long Reach   tier 2/4")
	assert_str(menu.upgrades.upgrade_button(1).text).is_equal("Heavy Rounds   locked: other path chosen")
	assert_bool(menu.upgrades.upgrade_button(1).disabled).is_true()


func test_upgrades_show_in_the_build_view_only() -> void:
	var game: Game = _game_with_gatling()
	var sim: MockSimServer = game.sim as MockSimServer
	var menu: BuildMenu = game.hud.build_menu
	assert_bool(menu.upgrades.visible).is_true()
	# Watch view: a long-press still inspects (name, sell), without the upgrade paths.
	game.bands.snap_to(BandController.Band.ORBIT)
	game._on_long_pressed(game.slot_screen_position(9))
	game._process(0.016)
	assert_bool(menu.is_open()).is_true()
	assert_bool(menu.sell_button().visible).is_true()
	assert_bool(menu.upgrades.visible).is_false()
	game._on_upgrade_requested(0)
	assert_int(sim.calls_to(&"upgrade_building").size()).is_equal(0)
	# Back in the Build view they return.
	game.bands.snap_to(BandController.Band.SURFACE)
	game._process(0.016)
	assert_bool(menu.upgrades.visible).is_true()


func test_empty_slot_menu_and_panel_menu() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var menu: BuildMenu = game.hud.build_menu
	_tap_slot(game, 9)
	assert_bool(menu.build_button().visible).is_true()
	assert_bool(menu.upgrades.visible).is_false()
	game.clear_selection()
	sim.debug_set_stardust(500.0)
	sim.place_building(12, _index(sim, "solar_panel"), 0.0)
	_tap_slot(game, 12)
	assert_bool(menu.upgrades.visible).is_true()
	assert_str(menu.upgrades.stats_label().text).is_equal("Income 3/s")
	assert_str(menu.upgrades.title_label(0).text).is_equal("Efficiency   tier 0/4")
	assert_str(menu.upgrades.diff_label(0).text).is_equal("Income 3/s -> 3.75/s")
	assert_str(menu.upgrades.diff_label(1).text).is_equal("Income 3/s -> 3.5/s")
	assert_str(menu.upgrades.upgrade_button(1).text).is_equal("[2] Extra Cell   30")


func test_upgrade_menu_fits_a_landscape_phone_and_a_desktop_window() -> void:
	var game: Game = _game_with_gatling()
	var menu: BuildMenu = game.hud.build_menu
	await await_idle_frame()
	var size: Vector2 = menu.get_combined_minimum_size()
	# 1280 x 720 is the base canvas; the menu is pinned to the right edge and must
	# leave the planet (left and centre) free.
	assert_float(size.y).is_less(700.0)
	assert_float(size.x).is_less(480.0)
	for path: int in 2:
		assert_float(menu.upgrades.upgrade_button(path).custom_minimum_size.y).is_greater_equal(56.0)


func test_upgrade_menu_stays_on_screen_pinned_to_the_right_edge() -> void:
	var game: Game = _game_with_gatling()
	var menu: BuildMenu = game.hud.build_menu
	await await_idle_frame()
	await await_idle_frame()
	var screen: Rect2 = menu.get_viewport_rect()
	var rect: Rect2 = menu.get_global_rect()
	# Growing with the upgrade rows must not push it off the right or bottom edge.
	assert_bool(screen.encloses(rect)).is_true()
	assert_float(rect.end.x).is_equal_approx(screen.end.x - 12.0, 1.0)
	# Back to a plain building view it shrinks around the same pin.
	menu.show_upgrades({}, false)
	await await_idle_frame()
	assert_bool(screen.encloses(menu.get_global_rect())).is_true()
	assert_float(menu.get_global_rect().end.x).is_equal_approx(screen.end.x - 12.0, 1.0)


# --- M2.1 interplay ---------------------------------------------------------------

func test_upgrade_rows_and_build_above_share_the_menu_of_a_ground_tower() -> void:
	var game: Game = _game_with_gatling()
	var menu: BuildMenu = game.hud.build_menu
	assert_bool(menu.above_button().visible).is_true()
	assert_bool(menu.upgrades.visible).is_true()
	assert_int(menu.upgrades.path_count()).is_equal(2)
	await await_idle_frame()
	assert_bool(menu.get_viewport_rect().encloses(menu.get_global_rect())).is_true()


func test_an_elevated_tower_upgrades_through_its_site_with_the_elevated_bonus() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	sim.debug_set_stardust(1000.0)
	var n: int = sim.get_slot_count()
	sim.place_building(9, _index(sim, "solar_panel"), 0.0)
	sim.place_building(9, _index(sim, "gatling"), 0.0, 1)
	sim.clear_calls()
	game._select_slot(9, 1)
	var menu: BuildMenu = game.hud.build_menu
	assert_bool(menu.upgrades.visible).is_true()
	assert_bool(menu.above_button().visible).is_false()
	# The stats line and the diff include the elevated +15% range.
	assert_str(menu.upgrades.diff_label(1).text).is_equal("Range 402.5 -> 462.87")
	menu.upgrades.upgrade_button(1).pressed.emit()
	assert_array(sim.calls_to(&"upgrade_building")[0]).is_equal([n + 9, 1])
	assert_array(sim.get_slot_tiers(n + 9)).is_equal(PackedInt32Array([0, 1]))
	assert_array(sim.get_slot_tiers(9)).is_equal(PackedInt32Array([0, 0]))
	game._process(0.016)
	assert_str(menu.upgrades.title_label(1).text).is_equal("Long Reach   tier 1/4")
	assert_float(float(game.planet_view.selected_stats.get("range", 0.0))).is_equal_approx(
		350.0 * 1.15 * 1.15, 0.01)
