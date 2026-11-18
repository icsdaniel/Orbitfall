extends ClientTestBase
## M2.1 client: the elevated ring ("Build above"), multi-slot footprints and
## adjacency links in the Build view, against the mock SimServer through the
## contract methods (M2.1 block) only.

const N: int = 36


func _make_game() -> Game:
	var game: Game = auto_free(Game.new())
	game.force_mock = true
	add_child(game)
	var sim: MockSimServer = game.sim as MockSimServer
	sim.auto_advance = false
	sim.debug_set_stardust(5000.0)
	game.planet_view.view_zoom = game.camera.get_zoom_value()
	game.planet_view.sync()
	sim.drain_events()
	sim.clear_calls()
	return game


func _tap_site(game: Game, site: int) -> void:
	game.input_router.advance(1.0)  # never a double-tap with the previous one
	tap(game.input_router, game.site_screen_position(site))


func _building(game: Game, id: String) -> int:
	for def: Dictionary in game.sim.get_building_defs():
		if str(def["id"]) == id:
			return int(def["index"])
	return -1


func _rejections(sim: MockSimServer) -> Array[int]:
	var out: Array[int] = []
	for event: Dictionary in sim.drain_events():
		if str(event.get("type", "")) == "CommandRejected":
			out.append(int(event.get("b", 0)))
	return out


func test_mock_has_the_m2_1_contract_block() -> void:
	var game: Game = _make_game()
	for method: String in [
		"get_layer_count", "get_elevated_height", "get_elevated_range_bonus",
		"get_site_buildings", "get_site_flags", "get_site_owners", "get_site_modifiers",
		"get_adjacency_links",
	]:
		assert_bool(game.sim.has_method(method)).override_failure_message(
			"mock lacks M2.1 method " + method).is_true()
	assert_int(game.sim.get_layer_count()).is_equal(2)
	assert_int(game.sim.get_site_buildings().size()).is_equal(2 * N)
	assert_int(game.sim.get_slot_buildings().size()).is_equal(N)
	assert_float(game.sim.get_elevated_range_bonus()).is_equal_approx(0.15, 0.0001)
	for def: Dictionary in game.sim.get_building_defs():
		assert_int(int(def["footprint"])).is_equal(1)
		assert_bool(def.has("layer")).is_true()
		assert_bool(def.has("auras")).is_true()


func test_place_building_layer_argument_is_backward_compatible() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var gatling: int = _building(game, "gatling")
	sim.place_building(3, gatling)  # the M1 call: ground
	assert_int(sim.get_slot_buildings()[3]).is_equal(gatling)
	sim.place_building(3, gatling, 0.0, 1)
	assert_int(sim.get_site_buildings()[N + 3]).is_equal(gatling)
	assert_int(sim.get_slot_buildings()[3]).is_equal(gatling)
	assert_array(_rejections(sim)).is_empty()


func test_elevated_needs_a_foundation_and_a_foundation_in_use_cannot_be_sold() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var gatling: int = _building(game, "gatling")
	sim.place_building(5, gatling, 0.0, 1)
	assert_array(_rejections(sim)).contains_exactly([MockSimServer.REJECT_NO_FOUNDATION])
	sim.place_building(5, _building(game, "solar_panel"))
	sim.place_building(5, _building(game, "solar_panel"), 0.0, 1)  # panels are ground only
	assert_array(_rejections(sim)).contains_exactly([MockSimServer.REJECT_WRONG_LAYER])
	sim.place_building(5, gatling, 0.0, 1)
	assert_int(sim.get_site_buildings()[N + 5]).is_equal(gatling)
	assert_float(sim.get_site_modifiers()[4 * (N + 5)]).is_equal_approx(1.15, 0.0001)
	sim.sell_building(5)
	assert_array(_rejections(sim)).contains_exactly([MockSimServer.REJECT_SUPPORTS_ELEVATED])
	assert_int(sim.get_slot_buildings()[5]).is_greater_equal(0)
	sim.sell_building(N + 5)
	sim.sell_building(5)
	assert_array(_rejections(sim)).is_empty()
	assert_int(sim.get_site_owners()[5]).is_equal(-1)


func test_tap_a_ground_building_then_build_above_places_on_the_elevated_layer() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var menu: BuildMenu = game.hud.build_menu
	var gatling: int = _building(game, "gatling")
	var panel: int = _building(game, "solar_panel")
	sim.place_building(9, panel)
	sim.clear_calls()
	_tap_site(game, 9)
	assert_bool(menu.is_open()).is_true()
	assert_bool(menu.above_button().visible).is_true()
	assert_bool(menu.sell_button().disabled).is_false()
	menu.above_button().pressed.emit()
	# The build list for the slot above: only what may stand there.
	assert_bool(menu.is_open()).is_true()
	assert_int(menu.layer).is_equal(1)
	assert_int(game.selected_slot).is_equal(9)
	assert_int(game.selected_layer).is_equal(1)
	assert_int(game.planet_view.selected_layer).is_equal(1)
	assert_object(menu.building_button(gatling)).is_not_null()
	assert_object(menu.building_button(panel)).is_null()
	assert_str(menu.info_text()).contains("+15% range")
	menu.building_button(gatling).pressed.emit()
	assert_int(game.planet_view.ghost_building).is_equal(gatling)
	assert_bool(game.planet_view.ghost_fits(9, 1, gatling)).is_true()
	menu.build_button().pressed.emit()
	var placed: Array[Array] = sim.calls_to(&"place_building")
	assert_int(placed.size()).is_equal(1)
	assert_array(placed[0]).contains_exactly([9, gatling, 0.0, 1])
	assert_int(sim.get_site_buildings()[N + 9]).is_equal(gatling)
	assert_bool(menu.is_open()).is_false()
	assert_int(game.selected_layer).is_equal(0)


func test_selling_a_foundation_in_use_toasts_the_rejection_and_hides_build_above() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var menu: BuildMenu = game.hud.build_menu
	var gatling: int = _building(game, "gatling")
	sim.place_building(9, gatling)
	sim.place_building(9, gatling, 0.0, 1)
	sim.clear_calls()
	_tap_site(game, 9)
	assert_bool(menu.above_button().visible).is_false()
	assert_str(menu.info_text()).contains("sell that one first")
	# Sell stays pressable: the sim refuses and the HUD says why.
	assert_bool(menu.sell_button().disabled).is_false()
	menu.sell_button().pressed.emit()
	assert_array(sim.calls_to(&"sell_building")[0]).contains_exactly([9])
	game._handle_events()
	assert_str(game.hud._hint.text).is_equal("Sell the building above first")
	assert_int(sim.get_slot_buildings()[9]).is_equal(gatling)
	assert_int(sim.get_site_buildings()[N + 9]).is_equal(gatling)
	# Tapping the elevated marker selects the elevated building; selling it
	# sends its site index.
	_tap_site(game, N + 9)
	assert_int(game.selected_layer).is_equal(1)
	assert_str(menu.info_text()).contains("Above slot 9")
	assert_str(menu.info_text()).contains("+15% range")
	assert_bool(menu.sell_button().disabled).is_false()
	menu.sell_button().pressed.emit()
	assert_array(sim.calls_to(&"sell_building")[1]).contains_exactly([N + 9])
	assert_int(sim.get_site_buildings()[N + 9]).is_equal(-1)
	# Now the foundation can go.
	_tap_site(game, 9)
	assert_bool(menu.above_button().visible).is_true()
	assert_bool(menu.sell_button().disabled).is_false()


func test_keyboard_w_and_s_step_between_a_foundation_and_the_slot_above() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var menu: BuildMenu = game.hud.build_menu
	var gatling: int = _building(game, "gatling")
	sim.place_building(7, gatling)
	sim.clear_calls()
	# Nothing selected: W does nothing.
	game.input_router.handle_event(key(KEY_W, true))
	game.input_router.handle_event(key(KEY_W, false))
	assert_bool(menu.is_open()).is_false()
	# Click the ground building, W opens the build list above it (as "Build above").
	_tap_site(game, 7)
	game.input_router.handle_event(key(KEY_W, true))
	game.input_router.handle_event(key(KEY_W, false))
	assert_int(game.selected_slot).is_equal(7)
	assert_int(game.selected_layer).is_equal(1)
	assert_int(menu.layer).is_equal(1)
	assert_bool(menu.build_button().visible).is_true()
	menu.building_button(gatling).pressed.emit()  # preview
	menu.building_button(gatling).pressed.emit()  # build
	assert_array(sim.calls_to(&"place_building")[0]).contains_exactly([7, gatling, 0.0, 1])
	# Select the elevated tower; Down goes back to its foundation.
	_tap_site(game, N + 7)
	assert_int(game.selected_layer).is_equal(1)
	game.input_router.handle_event(key(KEY_DOWN, true))
	game.input_router.handle_event(key(KEY_DOWN, false))
	assert_int(game.selected_layer).is_equal(0)
	assert_int(menu.layer).is_equal(0)
	assert_bool(menu.sell_button().visible).is_true()
	# Up on a foundation already carrying a building does not open anything new.
	game.input_router.handle_event(key(KEY_UP, true))
	game.input_router.handle_event(key(KEY_UP, false))
	assert_int(game.selected_layer).is_equal(0)


func test_elevated_marker_is_only_tappable_above_a_foundation() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var view: PlanetView = game.planet_view
	# No foundation: a tap where the elevated marker would be falls to the ground slot.
	var world: Vector2 = Vector2.from_angle(view.slot_world_angle(4)) * view.marker_radius(1)
	assert_int(view.site_at(world, 1000.0)).is_equal(4)
	sim.place_building(4, _building(game, "gatling"))
	view.sync()
	assert_int(view.site_at(world, 1000.0)).is_equal(N + 4)
	_tap_site(game, N + 4)
	assert_int(game.selected_layer).is_equal(1)
	assert_int(game.hud.build_menu.layer).is_equal(1)
	assert_bool(game.hud.build_menu.build_button().visible).is_true()


func test_wide_footprint_covers_slots_and_any_covered_slot_opens_its_menu() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var view: PlanetView = game.planet_view
	var wide: int = sim.debug_add_building("wide", {"footprint": 3, "layer": 0})
	view.building_defs = sim.get_building_defs()
	var gatling: int = _building(game, "gatling")
	sim.place_building(11, gatling)
	view.sync()
	# The ghost of a 3-slot building does not fit over slot 11.
	assert_bool(view.ghost_fits(9, 0, wide)).is_false()
	assert_bool(view.ghost_fits(12, 0, wide)).is_true()
	sim.place_building(9, wide)
	assert_array(_rejections(sim)).contains_exactly([MockSimServer.REJECT_OCCUPIED])
	sim.place_building(12, wide)
	view.sync()
	assert_array(view.site_owners.slice(12, 15)).contains_exactly([12, 12, 12])
	assert_int(view.site_width(12)).is_equal(3)
	# Drawn at the centre of its footprint (slot 13).
	assert_float(view.site_world_angle(12)).is_equal_approx(view.slot_world_angle(13), 0.0001)
	# The build menu lists it with its size.
	_tap_site(game, 20)
	assert_str(game.hud.build_menu.building_button(wide).text).contains("(3 slots)")
	# Tapping a covered slot opens the building; Sell frees all three slots.
	_tap_site(game, 14)
	var menu: BuildMenu = game.hud.build_menu
	assert_bool(menu.sell_button().visible).is_true()
	menu.sell_button().pressed.emit()
	assert_array(sim.get_site_owners().slice(12, 15)).contains_exactly([-1, -1, -1])


func test_adjacency_links_and_buffs_reach_the_view_and_the_menu() -> void:
	var game: Game = _make_game()
	var sim: MockSimServer = game.sim as MockSimServer
	var view: PlanetView = game.planet_view
	var amp: int = sim.debug_add_building("amplifier", {
		"auras": [{"stat": 0, "amount": 0.2, "radius": 2, "kinds": MockSimServer.KIND_BITS["tower"]}]})
	view.building_defs = sim.get_building_defs()
	var gatling: int = _building(game, "gatling")
	sim.place_building(8, gatling)    # 2 slots away: linked
	sim.place_building(13, gatling)   # 3 away: not linked
	sim.place_building(9, _building(game, "solar_panel"))  # wrong kind
	sim.place_building(10, amp)
	view.sync()
	assert_array(view.adjacency_links).contains_exactly([10, 8, 0])
	assert_float(view.site_modifier(8, 0)).is_equal_approx(1.2, 0.0001)
	assert_float(view.site_modifier(13, 0)).is_equal_approx(1.0, 0.0001)
	# Elevated above the linked gatling: both bonuses.
	sim.place_building(8, gatling, 0.0, 1)
	view.sync()
	assert_int(view.adjacency_links.size()).is_equal(6)
	assert_float(view.site_modifier(N + 8, 0)).is_equal_approx(1.2 * 1.15, 0.0001)
	# The building's menu says what it gets.
	_tap_site(game, 8)
	assert_str(game.hud.build_menu.info_text()).contains("+20% range")
	# Selling the amplifier removes the links at once.
	sim.sell_building(10)
	view.sync()
	assert_int(view.adjacency_links.size()).is_equal(0)
	assert_float(view.site_modifier(8, 0)).is_equal_approx(1.0, 0.0001)


func test_rejections_explain_themselves() -> void:
	var game: Game = _make_game()
	assert_str(game._reject_text(MockSimServer.REJECT_NO_FOUNDATION)).contains("building below")
	assert_str(game._reject_text(MockSimServer.REJECT_SUPPORTS_ELEVATED)).contains("above first")
	assert_str(game._reject_text(MockSimServer.REJECT_UNAFFORDABLE)).contains("stardust")


# ---- the real SimServer through the binding ----

func _real_building(server: SimServer, id: String) -> int:
	for def: Dictionary in server.get_building_defs():
		if str(def["id"]) == id:
			return int(def["index"])
	return -1


func _real_rejections(server: SimServer) -> Array[int]:
	var out: Array[int] = []
	for event: Dictionary in server.drain_events():
		if str(event["type"]) == "CommandRejected":
			out.append(int(event["b"]))
	return out


func test_real_sim_elevated_layer_through_the_binding() -> void:
	var server: SimServer = auto_free(SimServer.new())
	assert_array(server.load_content("res://content")).is_empty()
	server.new_run(3)
	var n: int = server.get_slot_count()
	assert_int(server.get_layer_count()).is_equal(2)
	assert_float(server.get_elevated_range_bonus()).is_equal_approx(0.15, 0.0001)
	assert_float(server.get_elevated_height()).is_equal_approx(12.0, 0.0001)
	var gatling: int = _real_building(server, "gatling")
	var defs: Array[Dictionary] = server.get_building_defs()
	assert_int(int(defs[gatling]["layer"])).is_equal(2)  # any
	assert_int(int(defs[gatling]["footprint"])).is_equal(1)

	# No foundation yet: rejected. The M1 call (no layer) builds on the ground.
	server.place_building(3, gatling, 0.0, 1)
	server.step_ticks(1)
	assert_array(_real_rejections(server)).contains_exactly([MockSimServer.REJECT_NO_FOUNDATION])
	server.place_building(3, gatling)
	server.place_building(3, gatling, 0.0, 1)
	server.step_ticks(2)
	assert_array(_real_rejections(server)).is_empty()
	assert_int(server.get_slot_buildings().size()).is_equal(n)
	assert_int(server.get_slot_buildings()[3]).is_equal(gatling)
	var sites: PackedInt32Array = server.get_site_buildings()
	assert_int(sites.size()).is_equal(2 * n)
	assert_int(sites[n + 3]).is_equal(gatling)
	assert_array(server.get_site_owners().slice(n + 2, n + 5)).contains_exactly([-1, n + 3, -1])
	var mods: PackedFloat32Array = server.get_site_modifiers()
	assert_int(mods.size()).is_equal(8 * n)
	assert_float(mods[4 * (n + 3)]).is_equal_approx(1.15, 0.0001)
	assert_float(mods[4 * 3]).is_equal_approx(1.0, 0.0001)
	assert_int(server.get_site_flags().size()).is_equal(2 * n)
	assert_int(server.get_adjacency_links().size()).is_equal(0)  # no aura buildings in M1 content

	# The foundation cannot be sold while it carries the elevated tower.
	server.sell_building(3)
	server.step_ticks(1)
	assert_array(_real_rejections(server)).contains_exactly([MockSimServer.REJECT_SUPPORTS_ELEVATED])
	server.sell_building(n + 3)
	server.sell_building(3)
	server.step_ticks(2)
	assert_array(_real_rejections(server)).is_empty()
	assert_int(server.get_site_owners()[3]).is_equal(-1)
	# A layer that does not exist is refused, not wrapped onto another slot.
	server.place_building(3, gatling, 0.0, 2)
	server.step_ticks(1)
	assert_array(_real_rejections(server)).contains_exactly([MockSimServer.REJECT_INVALID_SLOT])
