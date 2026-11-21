extends ClientTestBase
## M2.3 client: the enemy codex and trait legend are generated from content
## (mock and real SimServer), open with one tap or one key, and large units get
## a segmented HP bar with a shield strip.

const TEST_CONTENT: String = "user://codex_test_content"


func _make_game() -> Game:
	var game: Game = auto_free(Game.new())
	game.force_mock = true
	game.planet_picker_enabled = false  # M2.8
	add_child(game)
	var sim: MockSimServer = game.sim as MockSimServer
	sim.auto_advance = false
	sim.clear_calls()
	return game


func _server(dir: String = "res://content") -> SimServer:
	var server: SimServer = auto_free(SimServer.new())
	assert_array(server.load_content(dir)).is_empty()
	return server


func _by_id(defs: Array[Dictionary], id: String) -> Dictionary:
	for def: Dictionary in defs:
		if str(def["id"]) == id:
			return def
	return {}


func _codex(sim: Node) -> CodexPanel:
	var codex: CodexPanel = auto_free(CodexPanel.new())
	add_child(codex)
	codex.build_from(sim)
	return codex


## A copy of the shipped content plus one enemy that exists only in this test.
func _content_with_extra_enemy() -> String:
	var root: String = ProjectSettings.globalize_path(TEST_CONTENT)
	for sub: String in ["", "buildings", "enemies", "falling", "planets", "sun", "waves"]:
		DirAccess.make_dir_recursive_absolute(root.path_join(sub))
		var src: String = "res://content".path_join(sub)
		for file_name: String in DirAccess.get_files_at(src):
			if file_name.ends_with(".json"):
				var out: FileAccess = FileAccess.open(root.path_join(sub).path_join(file_name), FileAccess.WRITE)
				out.store_string(FileAccess.get_file_as_string(src.path_join(file_name)))
				out.close()
	var extra: FileAccess = FileAccess.open(root.path_join("enemies/test_brick.json"), FileAccess.WRITE)
	extra.store_string(JSON.stringify({
		"id": "test_brick", "behavior": "lane", "layers": 1, "hp": 12, "speed": 22, "size": 11,
		"landDamage": 3, "bounty": 2, "traits": ["armored", "heat_resistant"], "shield": 6,
		"shadowCaster": false, "hpBar": true, "hpBarSegments": 3, "popsInto": null,
		"shape": "square", "visual": {"color": "tier.yellow"},
	}))
	extra.close()
	return TEST_CONTENT


# --- codex ------------------------------------------------------------------

func test_codex_lists_every_enemy_and_tower_from_the_sim_defs() -> void:
	var server: SimServer = _server()
	var codex: CodexPanel = _codex(server)
	var enemies: Array[Dictionary] = server.get_enemy_defs()
	assert_int(codex.enemy_rows.size()).is_equal(enemies.size())
	for def: Dictionary in enemies:
		assert_bool(codex.enemy_rows.has(str(def["id"]))).is_true()
	# Towers declare a damage type; the panel (no projectile) is not a tower row.
	assert_str(codex.tower_text["gatling"]).contains("Kinetic damage")
	assert_str(codex.tower_text["cannon"]).contains("Explosive damage")
	assert_bool(codex.tower_rows.has("solar_panel")).is_false()
	# M1 content: layered drifters name their layers and what they pop into.
	assert_str(codex.enemy_text["drifter_green"]).contains("3 layers")
	assert_str(codex.enemy_text["drifter_green"]).contains("pops into Drifter Blue")
	assert_str(codex.enemy_text["drifter_green"]).contains("Traits: Layered")
	assert_str(codex.enemy_text["drifter_red"]).contains("Traits: none")
	assert_str(codex.enemy_text["dart"]).contains("Traits: Fast")
	assert_str(codex.enemy_text["eclipser"]).contains("casts a shadow")


func test_codex_trait_legend_comes_from_damage_json() -> void:
	var server: SimServer = _server()
	var types: PackedStringArray = server.get_damage_types()
	assert_array(types).is_equal(PackedStringArray(
		["kinetic", "explosive", "energy", "cryo", "emp", "pierce"]))
	var traits: Array[Dictionary] = server.get_trait_defs()
	assert_int(traits.size()).is_equal(13)
	var codex: CodexPanel = _codex(server)
	for id: String in ["camo", "armored", "shielded", "fast", "layered", "regrow", "splitter",
			"heat_resistant"]:
		assert_bool(codex.trait_rows.has(id)).override_failure_message("no legend row for " + id).is_true()
		assert_str(codex.trait_text[id]).contains("Looks: " + str(_by_id(traits, id)["cue"]))
	assert_str(codex.trait_text["armored"]).contains("Kinetic x0.5")
	assert_str(codex.trait_text["heat_resistant"]).contains("Energy x0.5")
	assert_str(codex.trait_text["shielded"]).contains("Against the shield: Emp x2")
	# Traits of later milestones have no legend entry yet and are left out.
	assert_bool(codex.trait_rows.has("bulwark")).is_false()
	# No multiplier in the matrix is below the floor.
	for def: Dictionary in traits:
		for m: float in (def["mult"] as PackedFloat32Array):
			assert_float(m).is_greater_equal(0.25)
	for m: float in server.get_shield_mults():
		assert_float(m).is_greater_equal(0.25)


func test_codex_shows_a_new_enemy_without_any_code_change() -> void:
	var server: SimServer = _server(_content_with_extra_enemy())
	var codex: CodexPanel = _codex(server)
	assert_bool(codex.enemy_rows.has("test_brick")).is_true()
	var text: String = codex.enemy_text["test_brick"]
	assert_str(text).contains("HP 12")
	assert_str(text).contains("shield 6")
	assert_str(text).contains("Traits: Armored, Shielded, Heat-resistant")
	assert_str(text).contains("Kinetic x0.5")
	assert_str(text).contains("Energy x0.5")
	assert_str(text).contains("Pierce x1.5")
	var def: Dictionary = _by_id(server.get_enemy_defs(), "test_brick")
	assert_int(int(def["hp_bar_segments"])).is_equal(3)
	for m: float in (def["damage_mult"] as PackedFloat32Array):
		assert_float(m).is_greater_equal(0.25)
	var icon: CodexPanel.CueIcon = codex.enemy_rows["test_brick"].get_child(0) as CodexPanel.CueIcon
	assert_int(icon.traits & BatchRenderer.TRAIT_ARMORED).is_equal(BatchRenderer.TRAIT_ARMORED)
	assert_int(icon.shape).is_equal(BatchRenderer.Shape.SQUARE)
	assert_object(icon.color).is_equal(Palette.tier(3))


func test_mock_and_real_sim_give_the_codex_the_same_text() -> void:
	var real: CodexPanel = _codex(_server())
	var mock: CodexPanel = _codex(make_sim())
	for id: String in ["armored", "shielded", "heat_resistant", "fast", "layered"]:
		assert_str(mock.trait_text[id]).is_equal(real.trait_text[id])
	for id: String in ["drifter_green", "dart", "drifter_red"]:
		assert_str(mock.enemy_text[id]).is_equal(real.enemy_text[id])
	assert_str(mock.tower_text["cannon"]).is_equal(real.tower_text["cannon"])


func test_codex_is_one_tap_or_one_key_away() -> void:
	var game: Game = _make_game()
	var hud: Hud = game.hud
	assert_bool(hud.codex.visible).is_false()
	assert_bool(hud.codex_button.is_visible_in_tree()).is_true()
	# Touch / mouse: the "?" button (a Button takes both).
	hud.codex_button.pressed.emit()
	assert_bool(hud.codex.visible).is_true()
	assert_int(hud.codex.enemy_rows.size()).is_equal(game.sim.get_enemy_defs().size())
	# The open codex blocks world gestures under it.
	assert_bool(hud.is_point_over_ui(hud.codex.get_global_rect().get_center())).is_true()
	(hud.codex.find_child("Close", true, false) as Button).pressed.emit()
	assert_bool(hud.codex.visible).is_false()
	# Keyboard: C toggles, Escape closes.
	game.input_router.handle_event(key(KEY_C, true))
	assert_bool(hud.codex.visible).is_true()
	game.input_router.handle_event(key(KEY_ESCAPE, true))
	assert_bool(hud.codex.visible).is_false()
	game.input_router.handle_event(key(KEY_C, true))
	game.input_router.handle_event(key(KEY_C, true))
	assert_bool(hud.codex.visible).is_false()


func test_codex_fits_any_aspect_ratio() -> void:
	for viewport: Vector2 in [Vector2(1280.0, 720.0), Vector2(2400.0, 1080.0),
			Vector2(800.0, 360.0), Vector2(1024.0, 768.0), Vector2(640.0, 480.0)]:
		var fit: Vector2 = CodexPanel.fit_size(viewport)
		assert_float(fit.x).is_less_equal(viewport.x - 100.0)
		assert_float(fit.y).is_less_equal(viewport.y - 150.0)
		assert_float(fit.x).is_less_equal(900.0)


# --- HP bars ----------------------------------------------------------------

func test_hp_bar_is_segmented_from_the_unit_data() -> void:
	var unit: Dictionary = {
		"pos": Vector2(100.0, 50.0), "radius": 40.0, "hp_frac": 0.6, "segments": 5,
		"shield_frac": 0.0}
	var bar: Dictionary = EntityView.hp_bar_layout(unit, 1.0)
	var back: Rect2 = bar["back"]
	var fill: Rect2 = bar["fill"]
	var dividers: PackedFloat32Array = bar["dividers"]
	assert_int(int(bar["segments"])).is_equal(5)
	assert_int(dividers.size()).is_equal(4)
	assert_int(int(bar["full_segments"])).is_equal(3)
	assert_float(fill.size.x).is_equal_approx(80.0 * 0.6, 0.001)
	# Dividers are evenly spaced inside the bar, which sits above the unit.
	for i: int in dividers.size():
		assert_float(dividers[i]).is_equal_approx(60.0 + 16.0 * float(i + 1), 0.001)
	assert_float(back.end.y).is_less(50.0 - 40.0)
	assert_float((bar["shield"] as Rect2).size.x).is_equal(0.0)


func test_hp_bar_shield_strip_and_defaults() -> void:
	var shielded: Dictionary = EntityView.hp_bar_layout(
		{"pos": Vector2.ZERO, "radius": 30.0, "hp_frac": 1.0, "segments": 4, "shield_frac": 0.5}, 1.0)
	var strip: Rect2 = shielded["shield"]
	assert_float(strip.size.x).is_equal_approx(30.0, 0.001)
	assert_float(strip.end.y).is_less_equal((shielded["fill"] as Rect2).position.y)
	assert_int(int(shielded["full_segments"])).is_equal(4)
	# An M1-style unit without the new keys is one unsegmented bar.
	var plain: Dictionary = EntityView.hp_bar_layout({"pos": Vector2.ZERO, "radius": 30.0, "hp_frac": 0.25}, 1.0)
	assert_int((plain["dividers"] as PackedFloat32Array).size()).is_equal(0)
	# Zoomed far out the bar keeps a minimum on-screen width (44 px).
	var far: Dictionary = EntityView.hp_bar_layout({"pos": Vector2.ZERO, "radius": 5.0, "hp_frac": 1.0}, 10.0)
	assert_float((far["fill"] as Rect2).size.x).is_equal_approx(440.0, 0.001)


func test_large_units_carry_segments_and_shield_through_the_binding() -> void:
	var server: SimServer = _server()
	var eclipser: Dictionary = _by_id(server.get_enemy_defs(), "eclipser")
	assert_bool(bool(eclipser["hp_bar"])).is_true()
	assert_int(int(eclipser["hp_bar_segments"])).is_equal(4)
	# Small enemies never get a bar (readability rule 7).
	for id: String in ["dart", "drifter_red", "drifter_blue", "drifter_green"]:
		assert_bool(bool(_by_id(server.get_enemy_defs(), id)["hp_bar"])).is_false()
	var sim: MockSimServer = make_sim()
	sim.debug_spawn("eclipser", Vector2(260.0, 0.0))
	sim.debug_spawn("drifter_red", Vector2(300.0, 40.0))
	var units: Array[Dictionary] = sim.get_large_units()
	assert_int(units.size()).is_equal(1)
	assert_bool(units[0].has_all(["pos", "radius", "hp_frac", "shadow", "segments", "shield_frac"])).is_true()
	assert_int(int(units[0]["segments"])).is_equal(4)
	var bar: Dictionary = EntityView.hp_bar_layout(units[0], 1.0)
	assert_int((bar["dividers"] as PackedFloat32Array).size()).is_equal(3)


func test_enemy_buffer_carries_trait_bits_for_the_shader() -> void:
	var sim: MockSimServer = make_sim()
	sim.debug_spawn("dart", Vector2(300.0, 0.0))
	sim.debug_spawn("drifter_green", Vector2(320.0, 0.0))
	var buffer: PackedFloat32Array = sim.get_enemy_buffer()
	var custom: int = BatchRenderer.OFFSET_CUSTOM
	assert_float(buffer[custom + 2]).is_equal(float(BatchRenderer.TRAIT_FAST))
	assert_float(buffer[BatchRenderer.FLOATS_PER_INSTANCE + custom + 2]).is_equal(
		float(BatchRenderer.TRAIT_LAYERED))
