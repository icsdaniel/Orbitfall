class_name Game
extends Node2D
## M1 client root (CONTRACT.md scene layout). Owns the SimServer (real GDExtension
## node, or MockSimServer when the binding is missing or `--mock` is passed), the
## OrbitCamera, PlanetView, EntityView (BatchRenderer), InputRouter, BandController
## and the HUD, and turns UI intents into SimServer commands.
## User args (after `--`): --mock, --seed=N, --band=surface|orbit, --demo (scripted
## mid-wave state on the mock), --select=SLOT, --debug, --end=won|lost (mock),
## --screenshot PATH. Playtest bot (see autoplay.gd): --play=sensible|towers|none keeps
## playing live; with --ff-wave=N [--ff-seconds=S], --ff-shadow or --ff-end the run is
## first fast-forwarded to that moment (wave N running for S seconds, or its
## intermission when S < 0; the first enemy shadow on the planet; the end screen).

const PHASE_INTERMISSION: int = 0
const PHASE_WAVE: int = 1
const PHASE_WON: int = 2
const PHASE_LOST: int = 3
const FORECAST_REFRESH_SECONDS: float = 0.25
const SCREENSHOT_FRAME: int = 24

var sim: Node
var is_mock: bool = false
var config: ClientConfig
var camera: OrbitCamera
var planet_view: PlanetView
var entity_view: EntityView
var input_router: InputRouter
var bands: BandController
var hud: Hud
var run_seed: int = 0

var selected_slot: int = -1
## Layer of the selection: 0 ground, 1 elevated (M2.1).
var selected_layer: int = 0
var _last_building: int = -1
var _phase: int = -1
var _forecast_timer: float = 0.0
var _frame: int = 0
## Set before the node enters the tree (or pass --mock) to skip the real SimServer.
var force_mock: bool = false
var _demo: bool = false
## --demo-placement: an M2.1 layout for screenshots (elevated towers; on the
## mock also a wide building and an aura building, which M1 content lacks).
var _demo_placement: bool = false
var _start_band: String = ""
var _tap_switched_view: bool = false
var _select_arg: int = -1
var _debug_arg: bool = false
var _end_arg: String = ""
var _screenshot_path: String = ""
var _codex_arg: bool = false
var _slot_pick_px: float = 44.0
var _play: String = ""
var _autoplay: Autoplay
var _ff_wave: int = 0
var _ff_seconds: float = 0.0
var _ff_shadow: bool = false
var _ff_end: bool = false


func _ready() -> void:
	_parse_args()
	config = ClientConfig.load_default()
	_slot_pick_px = config.num("input", "slotPickPx", _slot_pick_px)
	_create_sim()
	_build_scene()
	_start_run(run_seed)
	var start_band: String = _start_band if _start_band != "" else config.text("bands", "startBand", "surface")
	bands.snap_to(BandController.Band.ORBIT if start_band == "orbit" else BandController.Band.SURFACE)
	if _start_band != "":
		bands.auto_switch = false  # screenshots and bots ask for a fixed view
	if _demo:
		_setup_demo()
	if _demo_placement:
		_setup_placement_demo()
	if _play != "":
		_autoplay = Autoplay.new()
		_autoplay.setup(sim, _play)
		_fast_forward()
	if _select_arg >= 0:
		planet_view.sync()
		_select_slot(_select_arg)
	if _debug_arg:
		hud.set_debug_visible(true)


func _process(delta: float) -> void:
	_frame += 1
	if _autoplay != null and _screenshot_path == "":
		_autoplay.act()
	var zoom: float = camera.get_zoom_value()
	planet_view.view_zoom = zoom
	planet_view.surface_weight = bands.surface_weight
	entity_view.view_zoom = zoom
	input_router.planet_centre = world_to_screen(Vector2.ZERO)
	input_router.planet_radius_px = float(sim.get_planet_radius()) * zoom
	_handle_events()
	_track_phase()
	_forecast_timer -= delta
	if _forecast_timer <= 0.0:
		_refresh_forecast()
	if _codex_arg and _frame == 2 and not hud.codex.visible:
		hud.toggle_codex()
	if _screenshot_path != "" and _frame == SCREENSHOT_FRAME:
		_save_screenshot()


## True when the real GDExtension SimServer with the M1 contract is available.
static func real_sim_available() -> bool:
	return ClassDB.class_exists(&"SimServer") and ClassDB.class_has_method(&"SimServer", &"get_phase")


## Screen (viewport) position of a world point with the current camera.
func world_to_screen(world: Vector2) -> Vector2:
	return ((world - camera.global_position) * camera.get_zoom_value()
		+ get_viewport().get_visible_rect().size * 0.5)


## Screen position of a slot's marker (where a tap selects it).
func slot_screen_position(slot: int) -> Vector2:
	planet_view.sync()
	var r: float = (planet_view.radius + PlanetView.BUILDING_OFFSET
		+ planet_view.building_radius() - PlanetView.BUILDING_RADIUS)
	return world_to_screen(Vector2.from_angle(planet_view.slot_world_angle(slot)) * r)


## Screen position of a site's marker (M2.1; site = slot + layer * slot count).
func site_screen_position(site: int) -> Vector2:
	planet_view.sync()
	return world_to_screen(planet_view.site_position(site))


func restart() -> void:
	_start_run(_new_seed())


func clear_selection() -> void:
	selected_slot = -1
	selected_layer = 0
	planet_view.selected_slot = -1
	planet_view.selected_layer = 0
	planet_view.ghost_building = -1
	if hud.build_menu.is_open():
		hud.build_menu.close()


func _create_sim() -> void:
	if not force_mock and real_sim_available():
		sim = ClassDB.instantiate(&"SimServer") as Node
	if sim == null:
		sim = MockSimServer.new()
		is_mock = true
	sim.name = "SimServer"
	add_child(sim)
	var errors: PackedStringArray = sim.load_content("res://content")
	for message: String in errors:
		push_warning("content: " + message)
	if sim.has_method(&"set_palette"):
		# Instance colours for palette keys that are not colour tiers.
		sim.set_palette(Palette.NAMED)
	# Screenshots and scripted demos step the sim by hand, so keep it on this thread.
	var threaded: bool = config.flag("sim", "threaded", true) and not OS.has_feature("web")
	sim.set_threaded(threaded and not _demo and _screenshot_path == "" and _play == "")


func _build_scene() -> void:
	camera = OrbitCamera.new()
	camera.name = "OrbitCamera"
	camera.handle_input = false  # InputRouter owns gestures; the planet stays centred.
	add_child(camera)
	camera.make_current()

	planet_view = PlanetView.new()
	planet_view.name = "PlanetView"
	planet_view.sim = sim
	add_child(planet_view)

	entity_view = EntityView.new()
	entity_view.name = "EntityView"
	entity_view.sim = sim
	add_child(entity_view)
	entity_view.renderer.camera = camera

	input_router = InputRouter.new()
	input_router.name = "InputRouter"
	input_router.sim = sim
	input_router.configure(config)
	add_child(input_router)

	bands = BandController.new()
	bands.name = "BandController"
	bands.sim = sim
	bands.camera = camera
	bands.configure(config)
	add_child(bands)

	hud = Hud.new()
	hud.name = "HUD"
	add_child(hud)
	hud.setup(sim, bands, input_router, config, camera)
	input_router.ui_blocker = hud.is_point_over_ui

	input_router.tapped.connect(_on_tapped)
	input_router.long_pressed.connect(_on_long_pressed)
	input_router.double_tapped.connect(_on_double_tapped)
	input_router.zoom_requested.connect(_on_zoom_requested)
	input_router.pinch_ended.connect(bands.settle)
	input_router.view_step_requested.connect(bands.step)
	input_router.debug_toggled.connect(hud.toggle_debug)
	input_router.codex_toggled.connect(hud.toggle_codex)  # M2.3
	input_router.codex_close_requested.connect(hud.codex.close)
	input_router.layer_step_requested.connect(_on_layer_step_requested)  # M2.1
	bands.band_changed.connect(_on_band_changed)
	hud.debug_toggle_requested.connect(hud.toggle_debug)
	hud.restart_requested.connect(restart)
	hud.build_menu.preview_changed.connect(_on_preview_changed)
	hud.build_menu.build_requested.connect(_on_build_requested)
	hud.build_menu.sell_requested.connect(_on_sell_requested)
	hud.build_menu.build_above_requested.connect(_on_build_above_requested)
	hud.build_menu.closed.connect(_on_menu_closed)


func _start_run(new_seed: int) -> void:
	run_seed = new_seed
	sim.new_run(run_seed, 0)
	_phase = -1
	bands.reset_time_scale()
	input_router.reset_stats()
	hud.debug_overlay.reset_stats()
	hud.hide_end()
	clear_selection()
	_refresh_forecast()


func _new_seed() -> int:
	return int(Time.get_unix_time_from_system()) % 1000000 + randi() % 1000


func _track_phase() -> void:
	var phase: int = sim.get_phase()
	if phase == _phase:
		return
	if bands.auto_switch and _phase >= 0:
		# M1.12: watch the wave come in, then come back close to build.
		if phase == PHASE_WAVE and _phase == PHASE_INTERMISSION:
			bands.go_to(BandController.Band.ORBIT)
		elif phase == PHASE_INTERMISSION and _phase == PHASE_WAVE:
			bands.go_to(BandController.Band.SURFACE)
	_phase = phase
	_refresh_forecast()
	if phase == PHASE_WON or phase == PHASE_LOST:
		clear_selection()
		var wave_count: int = sim.get_wave_count()
		var survived: int = wave_count if phase == PHASE_WON else int(sim.get_wave_index())
		hud.show_end(phase == PHASE_WON, run_seed, survived, wave_count)
	else:
		hud.hide_end()


func _handle_events() -> void:
	var events: Array[Dictionary] = sim.drain_events()
	for event: Dictionary in events:
		if str(event.get("type", "")) == "CommandRejected":
			hud.toast(_reject_text(int(event.get("b", 0))))


func _refresh_forecast() -> void:
	_forecast_timer = FORECAST_REFRESH_SECONDS
	var count: int = int(config.num("sim", "forecastCount", 5.0))
	var forecast: Array[Dictionary] = sim.get_forecast(count)
	planet_view.forecast = forecast
	hud.show_forecast(forecast)


## Why the sim refused a command (RejectReason in core/sim/events.h).
func _reject_text(reason: int) -> String:
	match reason:
		3:
			return "No room there"
		4:
			return "Not enough stardust"
		8:
			return "Needs a building below"
		9:
			return "That can't be built on this layer"
		10:
			return "Sell the building above first"
	return "Can't do that"


func _slot_at_screen(screen_pos: Vector2) -> int:
	planet_view.sync()
	var world: Vector2 = camera.screen_to_world(screen_pos)
	return planet_view.slot_at(world, _slot_pick_px / maxf(camera.get_zoom_value(), 0.01))


## M2.1: the site (either ring) under a screen position, or -1.
func _site_at_screen(screen_pos: Vector2) -> int:
	planet_view.sync()
	var world: Vector2 = camera.screen_to_world(screen_pos)
	return planet_view.site_at(world, _slot_pick_px / maxf(camera.get_zoom_value(), 0.01))


func _select_slot(slot: int, layer: int = 0) -> void:
	planet_view.sync()
	if (slot < 0 or slot >= planet_view.slot_count or layer < 0
			or layer >= planet_view.layer_count or _phase >= PHASE_WON):
		clear_selection()
		return
	var site: int = planet_view.site_of(slot, layer)
	var owner: int = planet_view.site_owner(site)
	if owner >= 0:
		planet_view.ghost_building = -1
		var flags: int = planet_view.site_flags[owner]
		hud.build_menu.open_building(slot, planet_view.building_def(planet_view.site_building(owner)),
			(flags & PlanetView.FLAG_LIT) != 0, (flags & PlanetView.FLAG_SHADED) != 0,
			_building_extra(slot, layer, owner))
	else:
		hud.build_menu.open_empty(
			slot, planet_view.building_defs, float(sim.get_stardust()), _last_building,
			layer, planet_view.elevated_range_bonus)
	# open_* may have reset the selection through the menu's `closed` signal.
	selected_slot = slot
	selected_layer = layer
	planet_view.selected_slot = slot
	planet_view.selected_layer = layer


## M2.1 details for the menu of the building anchored on `owner`, tapped at
## `slot`: can something be built above this slot, is it a foundation in use,
## and which bonuses it has.
func _building_extra(slot: int, layer: int, owner: int) -> Dictionary:
	var notes: Array[String] = []
	var kind: String = str(planet_view.building_def(planet_view.site_building(owner)).get("kind", ""))
	for stat: int in PlanetView.STAT_NAMES.size():
		if (stat == 3) != (kind == "panel") or kind == "support":
			continue  # towers show range, fire rate, damage; panels show income
		var mult: float = planet_view.site_modifier(owner, stat)
		if absf(mult - 1.0) > 0.001:
			notes.append("%+d%% %s" % [roundi((mult - 1.0) * 100.0), PlanetView.STAT_NAMES[stat]])
	var carrying: bool = false
	if layer == 0 and planet_view.layer_count > 1:
		var first: int = planet_view.site_slot(owner)
		for k: int in planet_view.site_width(owner):
			if planet_view.site_owner(planet_view.site_of(first + k, 1)) >= 0:
				carrying = true
	return {
		"layer": layer, "notes": notes, "carrying": carrying,
		"can_build_above": layer == 0 and planet_view.can_build_above(slot),
	}


func _select_site(site: int) -> void:
	if site < 0:
		clear_selection()
		return
	_select_slot(planet_view.site_slot(site), planet_view.site_layer(site))


func _on_tapped(screen_pos: Vector2) -> void:
	# Building happens in the Build view (BRIEF §4.1). In the Watch view a tap on
	# or near the planet goes to the Build view; elsewhere it only deselects.
	if bands.band != BandController.Band.SURFACE:
		clear_selection()
		var r: float = float(sim.get_planet_radius()) * camera.get_zoom_value()
		if screen_pos.distance_to(world_to_screen(Vector2.ZERO)) <= r * 1.6 + _slot_pick_px:
			bands.go_to(BandController.Band.SURFACE)
			_tap_switched_view = true
		return
	_tap_switched_view = false
	_select_site(_site_at_screen(screen_pos))


func _on_long_pressed(screen_pos: Vector2) -> void:
	# Long-press inspects in any band.
	var site: int = _site_at_screen(screen_pos)
	if site >= 0:
		_select_site(site)


func _on_double_tapped(_screen_pos: Vector2) -> void:
	clear_selection()
	if _tap_switched_view:
		# The first tap already took the Watch view to Build.
		_tap_switched_view = false
		return
	bands.toggle()


func _on_zoom_requested(factor: float, _screen_pos: Vector2) -> void:
	bands.zoom_by(factor)


func _on_band_changed(band: int) -> void:
	if band != BandController.Band.SURFACE and planet_view.ghost_building >= 0:
		clear_selection()


func _on_preview_changed(building: int) -> void:
	planet_view.ghost_building = building


func _on_build_requested(building: int) -> void:
	if selected_slot < 0:
		return
	_last_building = building
	if selected_layer > 0:
		sim.place_building(selected_slot, building, 0.0, selected_layer)
	else:
		sim.place_building(selected_slot, building, 0.0)
	clear_selection()


## M2.1: from a ground building's menu to the build list of the slot above it.
func _on_build_above_requested() -> void:
	var slot: int = selected_slot
	if slot < 0 or not planet_view.can_build_above(slot):
		return
	_select_slot(slot, 1)


## M2.1 keyboard path (W/Up, S/Down): up from the selected ground building to the
## elevated slot above it (same as "Build above"), down from an elevated site to
## its foundation.
func _on_layer_step_requested(direction: int) -> void:
	if selected_slot < 0 or not hud.build_menu.is_open():
		return
	if direction > 0 and selected_layer == 0:
		_on_build_above_requested()
	elif direction < 0 and selected_layer > 0:
		_select_slot(selected_slot, 0)


func _on_sell_requested() -> void:
	if selected_slot < 0:
		return
	sim.sell_building(planet_view.site_of(selected_slot, selected_layer))
	clear_selection()


func _on_menu_closed() -> void:
	selected_slot = -1
	selected_layer = 0
	planet_view.selected_slot = -1
	planet_view.selected_layer = 0
	planet_view.ghost_building = -1


func _parse_args() -> void:
	run_seed = _new_seed()
	var args: PackedStringArray = OS.get_cmdline_user_args()
	var i: int = 0
	while i < args.size():
		var arg: String = args[i]
		if arg == "--mock":
			force_mock = true
		elif arg == "--demo":
			_demo = true
		elif arg == "--demo-placement":
			_demo_placement = true
		elif arg == "--debug":
			_debug_arg = true
		elif arg.begins_with("--seed="):
			run_seed = arg.get_slice("=", 1).to_int()
		elif arg.begins_with("--band="):
			_start_band = arg.get_slice("=", 1)
		elif arg.begins_with("--select="):
			_select_arg = arg.get_slice("=", 1).to_int()
		elif arg.begins_with("--play="):
			_play = arg.get_slice("=", 1)
		elif arg.begins_with("--ff-wave="):
			_ff_wave = arg.get_slice("=", 1).to_int()
		elif arg.begins_with("--ff-seconds="):
			_ff_seconds = arg.get_slice("=", 1).to_float()
		elif arg == "--ff-shadow":
			_ff_shadow = true
		elif arg == "--ff-end":
			_ff_end = true
		elif arg.begins_with("--end="):
			_end_arg = arg.get_slice("=", 1)
		elif arg == "--codex":  # M2.3: start with the codex open (screenshots)
			_codex_arg = true
		elif arg == "--screenshot" and i + 1 < args.size():
			i += 1
			_screenshot_path = args[i]
		i += 1


## Scripted mid-wave state for screenshots: panels in the beam, towers on both
## lanes, a wave in flight. Uses contract commands; extras only on the mock.
func _setup_demo() -> void:
	var mock: MockSimServer = sim as MockSimServer
	if mock != null:
		mock.debug_set_stardust(2000.0)
	var defs: Array[Dictionary] = sim.get_building_defs()
	var panel: int = -1
	var towers: Array[int] = []
	for def: Dictionary in defs:
		if str(def.get("kind", "")) == "panel":
			panel = int(def.get("index", -1))
		else:
			towers.append(int(def.get("index", -1)))
	var n: int = sim.get_slot_count()
	if panel >= 0:
		for slot: int in [n - 3, n - 2, n - 1, 0, 1]:
			sim.place_building(slot, panel, 0.0)
	if not towers.is_empty():
		@warning_ignore("integer_division")
		var lane_slots: Array[int] = [2, 3, 4, n / 2 - 2, n / 2 - 1, n / 2]
		for k: int in lane_slots.size():
			sim.place_building(lane_slots[k], towers[k % towers.size()], 0.0)
	sim.step_ticks(2)
	sim.start_wave()
	sim.step_ticks(30 * 19)
	if mock != null:
		var sun: Vector2 = Vector2.from_angle(float(sim.get_sun_bearing()))
		mock.debug_spawn("eclipser", sun * 260.0 + sun.orthogonal() * 22.0)
		if _end_arg == "won":
			mock.debug_set_phase(PHASE_WON)
		elif _end_arg == "lost":
			mock.debug_set_planet_hp(0.0)
			mock.debug_set_phase(PHASE_LOST)
	bands.update()


## M2.1 screenshot layout, between waves: towers with towers above them, and on
## the mock a 3-slot building and an aura building with its links.
func _setup_placement_demo() -> void:
	var mock: MockSimServer = sim as MockSimServer
	if mock != null:
		mock.debug_set_stardust(3000.0)
	var panel: int = -1
	var towers: Array[int] = []
	for def: Dictionary in sim.get_building_defs():
		if str(def.get("kind", "")) == "panel":
			panel = int(def.get("index", -1))
		elif str(def.get("kind", "")) == "tower":
			towers.append(int(def.get("index", -1)))
	if towers.is_empty():
		return
	var n: int = sim.get_slot_count()
	for slot: int in [n - 2, n - 1, 0, 1]:
		if panel >= 0:
			sim.place_building(slot, panel, 0.0)
	for k: int in 5:
		sim.place_building(3 + k, towers[k % towers.size()], 0.0)
	for slot: int in [3, 4, 6]:
		sim.place_building(slot, towers[0], 0.0, 1)
	if mock != null:
		var amp: int = mock.debug_add_building("amplifier", {
			"cost": 80.0, "color": "building.amplifier",
			"auras": [{"stat": 0, "amount": 0.2, "radius": 2, "kinds": 1}]})
		var wide: int = mock.debug_add_building("depot", {
			"cost": 60.0, "footprint": 3, "layer": 0, "shape": 1, "color": "building.depot"})
		planet_view.building_defs = sim.get_building_defs()
		sim.place_building(8, amp, 0.0)
		sim.place_building(11, wide, 0.0)
		sim.place_building(12, towers[0], 0.0, 1)
		sim.place_building(16, towers[towers.size() - 1], 0.0)
	sim.step_ticks(2)
	bands.update()


## Plays the run with the playtest bot, as fast as the sim steps, up to the moment
## the --ff-* arguments ask for. Uses contract calls only.
func _fast_forward() -> void:
	if _ff_wave <= 0 and not _ff_shadow and not _ff_end:
		return
	for i: int in 40000:
		var phase: int = sim.get_phase()
		if phase >= PHASE_WON:
			break
		if _ff_shadow and sim.get_shade_intervals().size() > 0:
			break
		if _ff_wave > 0 and int(sim.get_wave_index()) == _ff_wave - 1:
			if phase == PHASE_INTERMISSION and _ff_seconds < 0.0:
				break
			if phase == PHASE_WAVE and _ff_seconds >= 0.0 \
					and float(sim.get_debug_stats().get("wave_time", 0.0)) >= _ff_seconds:
				break
		_autoplay.act()
		sim.step_ticks(3)
	sim.drain_events()
	bands.update()


func _save_screenshot() -> void:
	await RenderingServer.frame_post_draw
	var image: Image = get_viewport().get_texture().get_image()
	var error: Error = image.save_png(_screenshot_path)
	if error != OK:
		push_error("screenshot failed: %s" % error_string(error))
	else:
		print("screenshot saved: ", _screenshot_path)
	get_tree().quit()
