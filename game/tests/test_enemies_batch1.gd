extends ClientTestBase
## M2.5 client: the batch 1 enemies (Brick, Phantom, Bubble, Ember, Spore,
## Splitter) reach the codex with their cues and Regrow / Split lines, carry
## their trait bits and shapes into the render buffer for the SDF shader, and a
## Splitter killed on the real sim splits (EnemySplit event through the binding).

const TEST_CONTENT: String = "user://enemies_batch1_content"
const F: int = BatchRenderer.FLOATS_PER_INSTANCE
const NEW_ENEMIES: Array[String] = ["brick", "phantom", "bubble", "ember", "spore", "splitter"]


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


func _count(events: Array[Dictionary], type: String) -> int:
	var n: int = 0
	for event: Dictionary in events:
		if str(event["type"]) == type:
			n += 1
	return n


## A copy of the shipped content plus a one-wave set of Splitters on lane A.
func _content_with_split_wave() -> String:
	var root: String = ProjectSettings.globalize_path(TEST_CONTENT)
	for sub: String in ["", "buildings", "enemies", "falling", "planets", "sun", "waves"]:
		DirAccess.make_dir_recursive_absolute(root.path_join(sub))
		var src: String = "res://content".path_join(sub)
		for file_name: String in DirAccess.get_files_at(src):
			if file_name.ends_with(".json"):
				var out: FileAccess = FileAccess.open(root.path_join(sub).path_join(file_name), FileAccess.WRITE)
				out.store_string(FileAccess.get_file_as_string(src.path_join(file_name)))
				out.close()
	var extra: FileAccess = FileAccess.open(root.path_join("waves/zz_split_test.json"), FileAccess.WRITE)
	extra.store_string(JSON.stringify({
		"id": "zz_split_test", "planet": "medium", "sun": "default", "autoStartSeconds": 0,
		"startStardust": 500,
		"waves": [{"wave": 1, "lightMult": 1.0, "spawnSeconds": 2,
			"spawns": [{"lane": "A", "enemy": "splitter", "count": 2}],
			"forecast": {"exactAhead": 1, "iconsAhead": 3}}],
	}))
	extra.close()
	return TEST_CONTENT


func test_codex_lists_the_new_enemies_with_their_traits_regrow_and_split() -> void:
	var server: SimServer = _server()
	var codex: CodexPanel = _codex(server)
	for id: String in NEW_ENEMIES:
		assert_bool(codex.enemy_rows.has(id)).override_failure_message(id).is_true()
	assert_str(codex.enemy_text["brick"]).contains("Armored")
	assert_str(codex.enemy_text["phantom"]).contains("Camo")
	assert_str(codex.enemy_text["bubble"]).contains("Shielded")
	assert_str(codex.enemy_text["bubble"]).contains("shield 3")
	assert_str(codex.enemy_text["ember"]).contains("Fast, Heat-resistant")
	assert_str(codex.enemy_text["spore"]).contains("Regrow")
	assert_str(codex.enemy_text["spore"]).contains("regrows a layer after 2.5 s unhit")
	assert_str(codex.enemy_text["splitter"]).contains("Splitter")
	assert_str(codex.enemy_text["splitter"]).contains("splits into 3 Drifter Red")
	var splitter: Dictionary = _by_id(server.get_enemy_defs(), "splitter")
	assert_int(int(splitter["split_count"])).is_equal(3)
	assert_int(int(splitter["splits_into"])).is_equal(int(_by_id(server.get_enemy_defs(), "drifter_red")["index"]))
	# Cue icons draw the trait bits the shader uses.
	var icon: CodexPanel.CueIcon = codex.enemy_rows["brick"].get_child(0) as CodexPanel.CueIcon
	assert_int(icon.shape).is_equal(BatchRenderer.Shape.SQUARE)
	assert_int(icon.traits & BatchRenderer.TRAIT_ARMORED).is_equal(BatchRenderer.TRAIT_ARMORED)
	icon = codex.enemy_rows["splitter"].get_child(0) as CodexPanel.CueIcon
	assert_int(icon.traits & BatchRenderer.TRAIT_SPLITTER).is_equal(BatchRenderer.TRAIT_SPLITTER)


func test_mock_and_real_sim_give_the_codex_the_same_text_for_the_new_enemies() -> void:
	var real: CodexPanel = _codex(_server())
	var mock: CodexPanel = _codex(make_sim())
	for id: String in NEW_ENEMIES + ["spore_blue", "spore_red"]:
		assert_str(mock.enemy_text[id]).override_failure_message(id).is_equal(real.enemy_text[id])
	assert_str(mock.trait_text["regrow"]).is_equal(real.trait_text["regrow"])
	assert_str(mock.trait_text["splitter"]).is_equal(real.trait_text["splitter"])


func test_bestiary_wave_one_draws_bricks_and_bubbles_with_their_cues() -> void:
	var server: SimServer = _server()
	var sets: PackedStringArray = server.get_wave_sets()
	var set_index: int = sets.find("m2_bestiary")
	assert_int(set_index).is_greater_equal(0)
	server.new_run(3, set_index)
	server.start_wave()
	server.step_ticks(30 * 8)
	var count: int = server.get_enemy_count()
	assert_int(count).is_greater(0)
	var buffer: PackedFloat32Array = server.get_enemy_buffer()
	var custom: int = BatchRenderer.OFFSET_CUSTOM
	var squares_armored: int = 0
	var shielded: int = 0
	for i: int in count:
		var shape: int = int(buffer[i * F + custom])
		var traits: int = int(buffer[i * F + custom + 2])
		if shape == BatchRenderer.Shape.SQUARE and (traits & BatchRenderer.TRAIT_ARMORED) != 0:
			squares_armored += 1
		if (traits & BatchRenderer.TRAIT_SHIELDED) != 0:
			shielded += 1
	assert_int(squares_armored).is_greater(0)
	assert_int(shielded).is_greater(0)


func test_a_splitter_killed_on_the_real_sim_splits_into_children() -> void:
	var server: SimServer = _server(_content_with_split_wave())
	var set_index: int = server.get_wave_sets().find("zz_split_test")
	assert_int(set_index).is_greater_equal(0)
	server.new_run(4, set_index)
	var gatling: int = -1
	for def: Dictionary in server.get_building_defs():
		if str(def["id"]) == "gatling":
			gatling = int(def["index"])
	server.place_building(3, gatling)  # slot 3 sits under lane A (30 deg)
	server.place_building(2, gatling)
	server.step_ticks(2)
	server.start_wave()
	var events: Array[Dictionary] = []
	for k: int in 60:
		server.step_ticks(30)
		events.append_array(server.drain_events())
		if server.get_phase() != 1:
			break
	assert_int(_count(events, "EnemySplit")).is_equal(2)
	# Two parents and their 2 x 3 children were killed (or a child landed).
	assert_int(_count(events, "EnemyKilled") + _count(events, "PlanetHit")).is_equal(8)
	assert_int(server.get_phase()).is_not_equal(1)


func test_forecast_icons_carry_the_trait_cues_so_spore_and_splitter_tell_apart() -> void:
	var server: SimServer = _server()
	server.new_run(2, server.get_wave_sets().find("m2_bestiary"))
	var strip: ForecastStrip = auto_free(ForecastStrip.new())
	add_child(strip)
	strip.set_enemy_defs(server.get_enemy_defs())
	strip.show_forecast(server.get_forecast(4))
	var wave3: Array[ForecastStrip.EntryIcon] = strip.entries_of(2)  # spores and splitters
	assert_int(wave3.size()).is_equal(2)
	var masks: Array[int] = []
	for icon: ForecastStrip.EntryIcon in wave3:
		masks.append(icon.traits & (BatchRenderer.TRAIT_REGROW | BatchRenderer.TRAIT_SPLITTER))
	assert_array(masks).contains_exactly_in_any_order(
		[BatchRenderer.TRAIT_REGROW, BatchRenderer.TRAIT_SPLITTER])
	assert_that(wave3[0].color).is_equal(wave3[1].color)  # same colour tier: the cue tells them apart
