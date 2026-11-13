class_name Game
extends Node2D
## M1 client root (CONTRACT.md scene layout). Owns the SimServer (real GDExtension
## node, or MockSimServer when the binding is missing or `--mock` is passed), the
## OrbitCamera, PlanetView, EntityView (BatchRenderer), InputRouter, BandController
## and the HUD, and turns UI intents into SimServer commands.
## User args (after `--`): --mock, --seed=N, --band=surface|orbit, --demo (scripted
## mid-wave state), --select=SLOT, --debug, --end=won|lost (mock), --screenshot PATH.

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
var _last_building: int = -1
var _phase: int = -1
var _forecast_timer: float = 0.0
var _frame: int = 0
## Set before the node enters the tree (or pass --mock) to skip the real SimServer.
var force_mock: bool = false
var _demo: bool = false
var _start_band: String = ""
var _select_arg: int = -1
var _debug_arg: bool = false
var _end_arg: String = ""
var _screenshot_path: String = ""
var _slot_pick_px: float = 44.0


func _ready() -> void:
	_parse_args()
	config = ClientConfig.load_default()
	_slot_pick_px = config.num("input", "slotPickPx", _slot_pick_px)
	_create_sim()
	_build_scene()
	_start_run(run_seed)
	var start_band: String = _start_band if _start_band != "" else config.text("bands", "startBand", "surface")
	bands.snap_to(BandController.Band.ORBIT if start_band == "orbit" else BandController.Band.SURFACE)
	if _demo:
		_setup_demo()
	if _select_arg >= 0:
		planet_view.sync()
		_select_slot(_select_arg)
	if _debug_arg:
		hud.set_debug_visible(true)


func _process(delta: float) -> void:
	_frame += 1
	var zoom: float = camera.get_zoom_value()
	planet_view.view_zoom = zoom
	planet_view.surface_weight = bands.surface_weight
	entity_view.view_zoom = zoom
	_handle_events()
	_track_phase()
	_forecast_timer -= delta
	if _forecast_timer <= 0.0:
		_refresh_forecast()
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


func restart() -> void:
	_start_run(_new_seed())


func clear_selection() -> void:
	selected_slot = -1
	planet_view.selected_slot = -1
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
	sim.set_threaded(threaded and not _demo and _screenshot_path == "")


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
	input_router.debug_toggled.connect(hud.toggle_debug)
	bands.band_changed.connect(_on_band_changed)
	hud.debug_toggle_requested.connect(hud.toggle_debug)
	hud.restart_requested.connect(restart)
	hud.build_menu.preview_changed.connect(_on_preview_changed)
	hud.build_menu.build_requested.connect(_on_build_requested)
	hud.build_menu.sell_requested.connect(_on_sell_requested)
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
			hud.toast("Can't do that (not enough stardust?)")


func _refresh_forecast() -> void:
	_forecast_timer = FORECAST_REFRESH_SECONDS
	var count: int = int(config.num("sim", "forecastCount", 5.0))
	var forecast: Array[Dictionary] = sim.get_forecast(count)
	planet_view.forecast = forecast
	hud.show_forecast(forecast)


func _slot_at_screen(screen_pos: Vector2) -> int:
	planet_view.sync()
	var world: Vector2 = camera.screen_to_world(screen_pos)
	return planet_view.slot_at(world, _slot_pick_px / maxf(camera.get_zoom_value(), 0.01))


func _select_slot(slot: int) -> void:
	if slot < 0 or slot >= planet_view.slot_count or _phase >= PHASE_WON:
		clear_selection()
		return
	selected_slot = slot
	planet_view.selected_slot = slot
	var building: int = planet_view.slot_buildings[slot]
	if building >= 0:
		planet_view.ghost_building = -1
		var flags: int = planet_view.slot_flags[slot]
		hud.build_menu.open_building(slot, planet_view.building_def(building),
			(flags & PlanetView.FLAG_LIT) != 0, (flags & PlanetView.FLAG_SHADED) != 0)
	else:
		hud.build_menu.open_empty(
			slot, planet_view.building_defs, float(sim.get_stardust()), _last_building)
	# open_* may have reset the selection through the menu's `closed` signal.
	selected_slot = slot
	planet_view.selected_slot = slot


func _on_tapped(screen_pos: Vector2) -> void:
	# Building happens at Surface zoom (BRIEF §4.1); in Orbit a tap only deselects.
	if bands.band != BandController.Band.SURFACE:
		clear_selection()
		return
	_select_slot(_slot_at_screen(screen_pos))


func _on_long_pressed(screen_pos: Vector2) -> void:
	# Long-press inspects in any band.
	var slot: int = _slot_at_screen(screen_pos)
	if slot >= 0:
		_select_slot(slot)


func _on_double_tapped(_screen_pos: Vector2) -> void:
	clear_selection()
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
	sim.place_building(selected_slot, building, 0.0)
	clear_selection()


func _on_sell_requested() -> void:
	if selected_slot < 0:
		return
	sim.sell_building(selected_slot)
	clear_selection()


func _on_menu_closed() -> void:
	selected_slot = -1
	planet_view.selected_slot = -1
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
		elif arg == "--debug":
			_debug_arg = true
		elif arg.begins_with("--seed="):
			run_seed = arg.get_slice("=", 1).to_int()
		elif arg.begins_with("--band="):
			_start_band = arg.get_slice("=", 1)
		elif arg.begins_with("--select="):
			_select_arg = arg.get_slice("=", 1).to_int()
		elif arg.begins_with("--end="):
			_end_arg = arg.get_slice("=", 1)
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


func _save_screenshot() -> void:
	await RenderingServer.frame_post_draw
	var image: Image = get_viewport().get_texture().get_image()
	var error: Error = image.save_png(_screenshot_path)
	if error != OK:
		push_error("screenshot failed: %s" % error_string(error))
	else:
		print("screenshot saved: ", _screenshot_path)
	get_tree().quit()
