class_name Hud
extends CanvasLayer
## HUD built in code (BRIEF §4.1, §4.8, §8.3): stardust and income, planet HP, wave
## counter, Start Wave (bottom right), rotate buttons (bottom left), forecast strip,
## build menu, intermission hint, win/lose screen, debug overlay and tuning panel.
## It reads SimServer snapshot getters every frame and never simulates.

signal restart_requested
signal debug_toggle_requested

const PHASE_INTERMISSION: int = 0
const PHASE_WAVE: int = 1
const PHASE_WON: int = 2
const PHASE_LOST: int = 3

var sim: Node
var bands: BandController
var router: InputRouter

var root: Control
var forecast: ForecastStrip
var build_menu: BuildMenu
var end_screen: EndScreen
var debug_overlay: DebugOverlay
var tuning_panel: TuningPanel
var edge_indicators: EdgeIndicators
var start_button: Button
var rotate_left: Button
var rotate_right: Button
var debug_button: Button

var _stardust: Label
var _income: Label
var _hp_bar: ProgressBar
var _hp_text: Label
var _wave: Label
var _hint: Label
var _toast_seconds: float = 0.0
var _top_bar: PanelContainer
var _interactive: Array[Control] = []


func _init() -> void:
	layer = 10
	root = Control.new()
	root.name = "Root"
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(root)


func setup(sim_node: Node, band_controller: BandController, input_router: InputRouter,
		config: ClientConfig, camera: OrbitCamera) -> void:
	sim = sim_node
	bands = band_controller
	router = input_router

	edge_indicators = EdgeIndicators.new()
	edge_indicators.sim = sim
	edge_indicators.camera = camera
	root.add_child(edge_indicators)

	_build_top_bar()
	forecast = ForecastStrip.new()
	forecast.set_enemy_defs(sim.get_enemy_defs())
	_pin(forecast, 1.0, 0.0, Vector2(-80.0, 10.0), Control.GROW_DIRECTION_BEGIN, Control.GROW_DIRECTION_END)

	debug_button = UiKit.button("DBG", Vector2(60.0, 56.0), Color(0.14, 0.16, 0.22))
	debug_button.add_theme_font_size_override(&"font_size", 16)
	debug_button.pressed.connect(func() -> void: debug_toggle_requested.emit())
	_pin(debug_button, 1.0, 0.0, Vector2(-10.0, 10.0), Control.GROW_DIRECTION_BEGIN, Control.GROW_DIRECTION_END)

	start_button = UiKit.button("START WAVE", Vector2(250.0, 88.0), Color(0.14, 0.46, 0.26))
	start_button.name = "StartWave"
	start_button.add_theme_font_size_override(&"font_size", 30)
	start_button.pressed.connect(_on_start_pressed)
	_pin(start_button, 1.0, 1.0, Vector2(-16.0, -16.0), Control.GROW_DIRECTION_BEGIN, Control.GROW_DIRECTION_BEGIN)

	var rotate_row: HBoxContainer = HBoxContainer.new()
	rotate_row.add_theme_constant_override(&"separation", 12)
	rotate_left = _rotate_button("<", -1.0)
	rotate_right = _rotate_button(">", 1.0)
	rotate_row.add_child(rotate_left)
	rotate_row.add_child(rotate_right)
	_pin(rotate_row, 0.0, 1.0, Vector2(16.0, -16.0), Control.GROW_DIRECTION_END, Control.GROW_DIRECTION_BEGIN)
	_interactive.erase(rotate_row)
	_interactive.append(rotate_left)
	_interactive.append(rotate_right)

	_hint = UiKit.label("", 22, Palette.TEXT_DIM)
	_hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_pin(_hint, 0.5, 1.0, Vector2(0.0, -18.0), Control.GROW_DIRECTION_BOTH, Control.GROW_DIRECTION_BEGIN)
	_interactive.erase(_hint)

	build_menu = BuildMenu.new()
	_pin(build_menu, 1.0, 0.5, Vector2(-12.0, 10.0), Control.GROW_DIRECTION_BEGIN, Control.GROW_DIRECTION_BOTH)

	debug_overlay = DebugOverlay.new()
	debug_overlay.sim = sim
	debug_overlay.bands = bands
	debug_overlay.router = router
	_pin(debug_overlay, 0.0, 0.0, Vector2(12.0, 128.0), Control.GROW_DIRECTION_END, Control.GROW_DIRECTION_END)

	tuning_panel = TuningPanel.new()
	tuning_panel.sim = sim
	tuning_panel.build(config.tunables())
	_pin(tuning_panel, 0.0, 1.0, Vector2(330.0, -16.0), Control.GROW_DIRECTION_END, Control.GROW_DIRECTION_BEGIN)

	end_screen = EndScreen.new()
	end_screen.restart_requested.connect(func() -> void: restart_requested.emit())
	root.add_child(end_screen)
	_interactive.append(end_screen)


func _process(delta: float) -> void:
	_toast_seconds = maxf(_toast_seconds - delta, 0.0)
	refresh()


## Pulls the current snapshot into the widgets.
func refresh() -> void:
	if sim == null or _stardust == null:
		return
	var phase: int = sim.get_phase()
	var stardust: float = sim.get_stardust()
	var income: float = sim.get_income_per_second()
	_set_text(_stardust, "%d" % floori(stardust))
	_set_text(_income, "+%.1f/s" % income)
	_income.add_theme_color_override(
		&"font_color", Palette.SUN_WARM if income > 0.0 else Palette.TEXT_DIM)
	var hp: float = sim.get_planet_hp()
	var hp_max: float = maxf(float(sim.get_planet_hp_max()), 1.0)
	_hp_bar.max_value = hp_max
	_hp_bar.value = hp
	_set_text(_hp_text, "HP %d / %d" % [ceili(hp), roundi(hp_max)])
	var wave_count: int = sim.get_wave_count()
	var wave_index: int = sim.get_wave_index()
	_set_text(_wave, "Wave %d / %d" % [mini(wave_index + 1, wave_count), wave_count])
	start_button.visible = phase == PHASE_INTERMISSION
	build_menu.set_stardust(stardust)
	if _toast_seconds <= 0.0:
		if phase == PHASE_INTERMISSION:
			_set_text(_hint, "Intermission: rotate, build, read the forecast, then Start Wave")
		elif phase == PHASE_WAVE:
			var scale: float = sim.get_time_scale()
			_set_text(_hint, "Wave %d   time x%.2f" % [wave_index + 1, scale]
				if scale < 0.995 else "Wave %d" % (wave_index + 1))
		else:
			_set_text(_hint, "")


func show_forecast(data: Array[Dictionary]) -> void:
	forecast.show_forecast(data, sim != null and int(sim.get_phase()) == PHASE_WAVE)


func show_end(won: bool, run_seed: int, waves_survived: int, wave_count: int) -> void:
	build_menu.close()
	end_screen.show_result(won, run_seed, waves_survived, wave_count)


func hide_end() -> void:
	end_screen.visible = false


## Short message in the hint line (e.g. a rejected command).
func toast(text: String, seconds: float = 1.6) -> void:
	_toast_seconds = seconds
	_set_text(_hint, text)


func toggle_debug() -> void:
	set_debug_visible(not debug_overlay.visible)


func set_debug_visible(on: bool) -> void:
	debug_overlay.visible = on
	tuning_panel.visible = on


## True when a screen position is over a visible interactive widget, so world
## gestures (rotate, tap a slot) must ignore the touch.
func is_point_over_ui(pos: Vector2) -> bool:
	for control: Control in _interactive:
		if control.is_visible_in_tree() and control.get_global_rect().has_point(pos):
			return true
	return false


func _build_top_bar() -> void:
	_top_bar = UiKit.panel()
	_top_bar.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var box: VBoxContainer = VBoxContainer.new()
	box.add_theme_constant_override(&"separation", 4)
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_top_bar.add_child(box)
	var row: HBoxContainer = HBoxContainer.new()
	row.add_theme_constant_override(&"separation", 10)
	row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	box.add_child(row)
	row.add_child(ShapeIcon.new(BatchRenderer.Shape.STAR, Palette.SUN_WARM, 32.0))
	_stardust = UiKit.label("0", 32)
	row.add_child(_stardust)
	_income = UiKit.label("+0.0/s", 22, Palette.TEXT_DIM)
	row.add_child(_income)
	_wave = UiKit.label("Wave 1 / 8", 22)
	_wave.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_wave.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	row.add_child(_wave)
	_hp_bar = ProgressBar.new()
	_hp_bar.custom_minimum_size = Vector2(330.0, 30.0)
	_hp_bar.show_percentage = false
	_hp_bar.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var fill: StyleBoxFlat = StyleBoxFlat.new()
	fill.bg_color = Color(0.25, 0.75, 0.40)
	fill.set_corner_radius_all(5)
	var back: StyleBoxFlat = StyleBoxFlat.new()
	back.bg_color = Color(0.30, 0.10, 0.10)
	back.set_corner_radius_all(5)
	_hp_bar.add_theme_stylebox_override(&"fill", fill)
	_hp_bar.add_theme_stylebox_override(&"background", back)
	box.add_child(_hp_bar)
	_hp_text = UiKit.label("HP", 20)
	_hp_text.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_hp_text.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_hp_text.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_hp_bar.add_child(_hp_text)
	_pin(_top_bar, 0.0, 0.0, Vector2(10.0, 10.0), Control.GROW_DIRECTION_END, Control.GROW_DIRECTION_END)
	_interactive.erase(_top_bar)


func _rotate_button(text: String, value: float) -> Button:
	var b: Button = UiKit.button(text, Vector2(92.0, 88.0), Color(0.16, 0.20, 0.32, 0.8))
	b.add_theme_font_size_override(&"font_size", 40)
	b.button_down.connect(func() -> void: router.set_button_rotation(value))
	b.button_up.connect(func() -> void: router.set_button_rotation(0.0))
	return b


## Anchors a control at a fraction of the screen with a pixel offset, growing away
## from that corner as its content needs.
func _pin(control: Control, ax: float, ay: float, offset: Vector2, grow_h: int, grow_v: int) -> void:
	root.add_child(control)
	control.anchor_left = ax
	control.anchor_right = ax
	control.anchor_top = ay
	control.anchor_bottom = ay
	control.offset_left = offset.x
	control.offset_right = offset.x
	control.offset_top = offset.y
	control.offset_bottom = offset.y
	control.grow_horizontal = grow_h as Control.GrowDirection
	control.grow_vertical = grow_v as Control.GrowDirection
	_interactive.append(control)


func _on_start_pressed() -> void:
	if sim != null and int(sim.get_phase()) == PHASE_INTERMISSION:
		sim.start_wave()


func _set_text(label: Label, text: String) -> void:
	if label.text != text:
		label.text = text
