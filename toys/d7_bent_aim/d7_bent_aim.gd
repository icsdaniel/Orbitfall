extends Node2D
## Toy D7: bent aim and focus fire (BRIEF §4.4, §6; DESIGN Q33, Q34). Throwaway.
##
## Towers fire along slot normal + an aim offset. Where two or more aim axes
## cross within range a focus point forms in the planet frame; hits inside its
## zone get a bonus per extra feeding tower. Switch the FORM of the bend to
## compare: straight, fixed mount, re-aim between waves, auto-converging twins.
## Drag anywhere to spin the planet (same grab-to-spin as the game), tap a slot
## to build, aim or sell. Desktop: mouse is a finger, A/D rotate, 1-4 pick the
## form, Space starts the wave.

enum Form { STRAIGHT, MOUNT, REAIM, TWIN }

const FORM_NAMES: Array[String] = ["1 Straight", "2 Fixed mount", "3 Re-aim", "4 Twins"]
const FORM_HELP: Array[String] = [
	"Baseline: every tower fires straight up from its slot. No bend at all.",
	"Pick the lean when you build (0, ±1 or ±2 steps). Fixed for good: sell to change it.",
	"Build with a lean, then re-aim between waves for 10% of the price.",
	"Neighbouring towers pair up and converge by themselves at a set distance.",
]

const PANEL_W: float = 300.0
const BAR_H: float = 72.0
const PLANET_R: float = 100.0
const SLOTS: int = 18  # every other slot of the medium planet, so each is a thumb-sized target
const VIEW_R: float = 400.0  # world units from the centre to the nearest screen edge
# Planet rotation and grab-to-spin, same numbers as the game (content/planets/medium.json, input_router.gd).
const ROT_MAX: float = deg_to_rad(90.0)
const ROT_ACCEL: float = deg_to_rad(360.0)
const ROT_DAMPING: float = 4.0
const GRAB_FULL_SPEED: float = deg_to_rad(90.0)
const GRAB_SMOOTHING: float = 30.0
const TAP_MAX_PX: float = 18.0
# Tower: one cannon-like type.
const TOWER_COST: int = 50
const REAIM_COST: int = 5  # 10% of the price
const TOWER_RANGE: float = 300.0
const TOWER_ARC: float = deg_to_rad(25.0)
const TOWER_RATE: float = 4.0
const TOWER_DAMAGE: float = 5.0
const TWIN_MAX_GAP: int = 4  # slots; towers further apart do not pair
const TWIN_MAX_OFFSET: float = deg_to_rad(60.0)
# Enemies and waves.
const ENEMY_HP: float = 60.0  # two unfocused towers cannot quite hold a lane; focus tips it
const ENEMY_SPEED: float = 30.0
const ENEMY_R: float = 7.0
const CLUMP: int = 6  # enemies arrive in clumps of this many
const CLUMP_SPACING: float = 12.0
const SPAWN_R: float = 440.0
const KILL_REWARD: int = 2
const WAVE_SECONDS: float = 25.0
const LANE_SETS: Array[Array] = [[30.0, 170.0], [100.0, 250.0], [330.0, 200.0]]
const LANE_SWAY_DEG: float = 12.0  # lanes sway so the focus point has to be swept after them
const START_STARDUST: int = 150
# Slot, aim step: two converging pairs, one with a third tower through the same point.
const PRESET: Array[Vector2i] = [Vector2i(0, 1), Vector2i(1, 0), Vector2i(2, -1), Vector2i(7, 1), Vector2i(9, -1)]
const START_ROT: float = deg_to_rad(10.0)

const COL_BG: Color = Color(0.02, 0.02, 0.05)
const COL_PLANET: Color = Color(0.16, 0.2, 0.3)
const COL_TOWER: Color = Color(0.4, 0.8, 1.0)
const COL_FOCUS: Color = Color(1.0, 0.85, 0.3)
const COL_ENEMY: Color = Color(0.9, 0.25, 0.25)
const COL_TEXT: Color = Color(0.85, 0.88, 0.95)

# Tunables (all on screen as sliders). Defaults are the Q34 proposal.
var focus_bonus: float = 25.0  # % per extra feeding tower
var focus_cap: float = 50.0  # %
var focus_radius: float = 30.0
var offset_step_deg: float = 30.0  # mounts are 0, ±1 and ±2 steps
var twin_distance: float = 190.0  # above the surface
var spawn_rate: float = 0.8  # enemies per second per lane

var form: Form = Form.MOUNT
var rot: float = START_ROT
var rot_vel: float = 0.0
var stardust: int = START_STARDUST

# Buildings: plain arrays indexed by slot.
var _tower: Array[bool] = []
var _step: Array[int] = []  # chosen mount, in steps (-2..2)
var _offset: Array[float] = []  # aim offset in effect under the current form, radians
var _partner: Array[int] = []  # twin partner slot or -1
var _cooldown: Array[float] = []
# Focus points, planet-local; recomputed only when a building, aim or slider changes.
var _focus_pos: Array[Vector2] = []
var _focus_feed: Array[PackedInt32Array] = []
var _focus_hot: Array[bool] = []  # an enemy is inside right now

var _enemy_pos: Array[Vector2] = []
var _enemy_hp: Array[float] = []
var _tracer_from: Array[Vector2] = []
var _tracer_to: Array[Vector2] = []
var _tracer_age: Array[float] = []
var _tracer_boost: Array[bool] = []

var _wave: int = 1
var _wave_on: bool = true
var _wave_time: float = 0.0
var _spawn_acc: Array[float] = [5.0, 5.0]
var _kills: int = 0
var _leaks: int = 0
var _wave_base: float = 0.0  # damage this wave without the focus bonus
var _wave_bonus: float = 0.0  # extra damage from focus this wave
var _frame_base: float = 0.0
var _frame_bonus: float = 0.0
var _dps_base: float = 0.0
var _dps_bonus: float = 0.0
var _results: Array[String] = ["", "", "", ""]  # last finished wave per form

var _selected: int = -1
var _finger: int = -1
var _finger_start: Vector2 = Vector2.ZERO
var _finger_moved: bool = false
var _grab_last: Vector2 = Vector2.ZERO
var _grab_accum: float = 0.0
var _grab_rate: float = 0.0
var _form_buttons: Array[Button] = []
var _help_label: Label
var _dps_label: Label
var _bar: HBoxContainer
var _font: Font


func _ready() -> void:
	Input.emulate_touch_from_mouse = true
	RenderingServer.set_default_clear_color(COL_BG)
	_font = ThemeDB.fallback_font
	for i: int in range(SLOTS):
		_tower.append(false)
		_step.append(0)
		_offset.append(0.0)
		_partner.append(-1)
		_cooldown.append(0.0)
	_build_ui()
	_reset()


# ---- layout -----------------------------------------------------------------

func _play_size() -> Vector2:
	var s: Vector2 = get_viewport_rect().size
	return Vector2(maxf(s.x - PANEL_W, 200.0), s.y)


func _center() -> Vector2:
	return _play_size() * 0.5


func _world_scale() -> float:
	var p: Vector2 = _play_size()
	return minf(p.x, p.y) * 0.5 / VIEW_R


func _slot_angle(slot: int) -> float:
	return TAU * float(slot) / float(SLOTS)


## Planet-local tower position and aim axis direction.
func _tower_pos(slot: int) -> Vector2:
	return Vector2.from_angle(_slot_angle(slot)) * PLANET_R


func _axis(slot: int) -> Vector2:
	return Vector2.from_angle(_slot_angle(slot) + _offset[slot])


func _focus_mult(feeders: int) -> float:
	return 1.0 + minf(focus_bonus * float(feeders - 1), focus_cap) / 100.0


# ---- UI -----------------------------------------------------------------------

func _build_ui() -> void:
	var layer: CanvasLayer = CanvasLayer.new()
	add_child(layer)
	var panel: PanelContainer = PanelContainer.new()
	panel.anchor_left = 1.0
	panel.anchor_right = 1.0
	panel.anchor_bottom = 1.0
	panel.offset_left = -PANEL_W
	layer.add_child(panel)
	var scroll: ScrollContainer = ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	panel.add_child(scroll)
	var margin: MarginContainer = MarginContainer.new()
	margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	for side: String in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 10)
	scroll.add_child(margin)
	var box: VBoxContainer = VBoxContainer.new()
	box.add_theme_constant_override("separation", 2)
	margin.add_child(box)

	var title: Label = Label.new()
	title.text = "D7 bent aim and focus fire"
	box.add_child(title)
	var grid: GridContainer = GridContainer.new()
	grid.columns = 2
	box.add_child(grid)
	for i: int in range(FORM_NAMES.size()):
		var b: Button = Button.new()
		b.text = FORM_NAMES[i]
		b.toggle_mode = true
		b.custom_minimum_size = Vector2(0, 52)
		b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		b.focus_mode = Control.FOCUS_NONE
		b.pressed.connect(_set_form.bind(i))
		grid.add_child(b)
		_form_buttons.append(b)
	_help_label = _add_text(box, 14)
	_help_label.custom_minimum_size = Vector2(0, 62)
	_dps_label = _add_text(box, 15)
	_dps_label.custom_minimum_size = Vector2(0, 62)

	_add_slider(box, "focus bonus % per extra tower", 0.0, 50.0, 1.0, focus_bonus, func(v: float) -> void: focus_bonus = v)
	_add_slider(box, "focus bonus cap %", 0.0, 100.0, 5.0, focus_cap, func(v: float) -> void: focus_cap = v)
	_add_slider(box, "focus zone radius", 10.0, 80.0, 1.0, focus_radius, func(v: float) -> void: focus_radius = v)
	_add_slider(box, "mount step ° (0, ±1, ±2 steps)", 10.0, 45.0, 1.0, offset_step_deg, func(v: float) -> void: offset_step_deg = v)
	_add_slider(box, "twin focus distance", 60.0, 300.0, 5.0, twin_distance, func(v: float) -> void: twin_distance = v)
	_add_slider(box, "enemies per s per lane", 0.2, 3.0, 0.1, spawn_rate, func(v: float) -> void: spawn_rate = v)
	var reset: Button = Button.new()
	reset.text = "Reset towers and counters"
	reset.focus_mode = Control.FOCUS_NONE
	reset.custom_minimum_size = Vector2(0, 48)
	reset.pressed.connect(_reset)
	box.add_child(reset)

	# Action bar along the bottom of the play area: build, aim, sell, start wave.
	_bar = HBoxContainer.new()
	_bar.anchor_top = 1.0
	_bar.anchor_right = 1.0
	_bar.anchor_bottom = 1.0
	_bar.offset_left = 12.0
	_bar.offset_right = -PANEL_W - 12.0
	_bar.offset_top = -BAR_H - 10.0
	_bar.offset_bottom = -10.0
	_bar.add_theme_constant_override("separation", 8)
	_bar.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(_bar)


func _add_text(box: Container, font_size: int) -> Label:
	var label: Label = Label.new()
	label.add_theme_font_size_override("font_size", font_size)
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	box.add_child(label)
	return label


func _add_slider(box: VBoxContainer, title: String, lo: float, hi: float, step: float, value: float, apply: Callable) -> void:
	var label: Label = Label.new()
	label.add_theme_font_size_override("font_size", 14)
	box.add_child(label)
	var slider: HSlider = HSlider.new()
	slider.min_value = lo
	slider.max_value = hi
	slider.step = step
	slider.value = value
	slider.custom_minimum_size = Vector2(0, 32)
	slider.focus_mode = Control.FOCUS_NONE
	box.add_child(slider)
	var update: Callable = func(v: float) -> void:
		label.text = "%s: %s" % [title, String.num(v, 2)]
		apply.call(v)
		_recompute()
	slider.value_changed.connect(update)
	update.call(value)


func _bar_button(text: String, action: Callable, enabled: bool = true) -> void:
	var b: Button = Button.new()
	b.text = text
	b.disabled = not enabled
	b.focus_mode = Control.FOCUS_NONE
	b.custom_minimum_size = Vector2(92, BAR_H)
	b.add_theme_font_size_override("font_size", 20)
	b.pressed.connect(action)
	_bar.add_child(b)


func _bar_text(text: String) -> void:
	var label: Label = Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", 16)
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_bar.add_child(label)


func _step_text(step: int) -> String:
	if step == 0:
		return "0°"
	return "%+d°" % roundi(float(step) * offset_step_deg)


## Rebuilds the action bar for the current selection, form and wave phase.
func _refresh_bar() -> void:
	if _bar == null:
		return
	for child: Node in _bar.get_children():
		_bar.remove_child(child)
		child.queue_free()
	var bends: bool = form == Form.MOUNT or form == Form.REAIM
	if _selected == -1:
		_bar_text("Drag anywhere to spin.\nTap a slot to build, aim or sell.")
	elif not _tower[_selected]:
		var can: bool = stardust >= TOWER_COST
		if bends:
			_bar_text("Build (%d)\nleaning:" % TOWER_COST)
			for s: int in range(-2, 3):
				_bar_button(_step_text(s), _build.bind(s), can)
		else:
			_bar_button("Build (%d)" % TOWER_COST, _build.bind(0), can)
	else:
		if form == Form.REAIM:
			var open: bool = not _wave_on and stardust >= REAIM_COST
			_bar_text("Re-aim only\nbetween waves" if _wave_on else "Re-aim (%d):" % REAIM_COST)
			for s: int in range(-2, 3):
				_bar_button(_step_text(s), _reaim.bind(s), open and s != _step[_selected])
		elif form == Form.MOUNT:
			_bar_text("Mount %s\nis fixed" % _step_text(_step[_selected]))
		elif form == Form.TWIN:
			_bar_text("Twinned" if _partner[_selected] != -1 else "No partner within\n%d slots: fires straight" % TWIN_MAX_GAP)
		_bar_button("Sell (+%d)" % TOWER_COST, _sell)
	var spacer: Control = Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	spacer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_bar.add_child(spacer)
	if not _wave_on:
		_bar_button("Start wave %d" % _wave, _start_wave)


func _set_form(index: int) -> void:
	form = index as Form
	for i: int in range(_form_buttons.size()):
		_form_buttons[i].set_pressed_no_signal(i == index)
	_help_label.text = FORM_HELP[index]
	_recompute()


func _reset() -> void:
	for i: int in range(SLOTS):
		_tower[i] = false
		_step[i] = 0
		_cooldown[i] = 0.0
	for entry: Vector2i in PRESET:
		_tower[entry.x] = true
		_step[entry.x] = entry.y
	_enemy_pos.clear()
	_enemy_hp.clear()
	stardust = START_STARDUST
	rot = START_ROT
	rot_vel = 0.0
	_wave = 1
	_wave_on = true
	_wave_time = 0.0
	_spawn_acc = [5.0, 5.0]
	_kills = 0
	_leaks = 0
	_wave_base = 0.0
	_wave_bonus = 0.0
	_results = ["", "", "", ""]
	_selected = -1
	_set_form(form)


# ---- building and aiming ----------------------------------------------------

func _build(step: int) -> void:
	if _selected == -1 or _tower[_selected] or stardust < TOWER_COST:
		return
	stardust -= TOWER_COST
	_tower[_selected] = true
	_step[_selected] = step
	_cooldown[_selected] = 0.0
	_recompute()


func _reaim(step: int) -> void:
	if _selected == -1 or not _tower[_selected] or _wave_on or stardust < REAIM_COST:
		return
	stardust -= REAIM_COST
	_step[_selected] = step
	_recompute()


func _sell() -> void:
	if _selected == -1 or not _tower[_selected]:
		return
	stardust += TOWER_COST
	_tower[_selected] = false
	_recompute()


func _start_wave() -> void:
	if _wave_on:
		return
	_wave_on = true
	_wave_time = 0.0
	_spawn_acc = [5.0, 5.0]
	_kills = 0
	_leaks = 0
	_wave_base = 0.0
	_wave_bonus = 0.0
	_refresh_bar()


## Aim offsets under the current form, then the focus points. Runs only when a
## building, an aim, the form or a slider changes.
func _recompute() -> void:
	if _tower.is_empty():
		return
	var bends: bool = form == Form.MOUNT or form == Form.REAIM
	for i: int in range(SLOTS):
		_partner[i] = -1
		_offset[i] = deg_to_rad(float(_step[i]) * offset_step_deg) if bends else 0.0
	if form == Form.TWIN:
		_pair_twins()
	_solve_focus()
	_refresh_bar()


## Twins: ring neighbours pair up, closest pairs first; each pair converges on a
## point `twin_distance` above the surface over the middle of the pair.
func _pair_twins() -> void:
	var slots: Array[int] = []
	for i: int in range(SLOTS):
		if _tower[i]:
			slots.append(i)
	var gaps: Array[Vector2i] = []  # (gap in slots, first slot)
	for n: int in range(slots.size()):
		var first: int = slots[n]
		var gap: int = posmod(slots[(n + 1) % slots.size()] - first, SLOTS)
		if gap >= 1 and gap <= TWIN_MAX_GAP:
			gaps.append(Vector2i(gap, first))
	gaps.sort_custom(func(p: Vector2i, q: Vector2i) -> bool: return p.x * 100 + p.y < q.x * 100 + q.y)
	for entry: Vector2i in gaps:
		var a: int = entry.y
		var b: int = (a + entry.x) % SLOTS
		if _partner[a] != -1 or _partner[b] != -1:
			continue
		_partner[a] = b
		_partner[b] = a
		var mid: float = _slot_angle(a) + TAU * float(entry.x) / float(SLOTS) * 0.5
		var point: Vector2 = Vector2.from_angle(mid) * (PLANET_R + twin_distance)
		for slot: int in [a, b]:
			var lean: float = angle_difference(_slot_angle(slot), (point - _tower_pos(slot)).angle())
			_offset[slot] = clampf(lean, -TWIN_MAX_OFFSET, TWIN_MAX_OFFSET)


## BRIEF §6: for each pair solve T_A + a·d_A = T_B + b·d_B; a focus point exists
## iff a, b > 0 and within both ranges. Points closer than focusRadius merge.
func _solve_focus() -> void:
	_focus_pos.clear()
	_focus_feed.clear()
	_focus_hot.clear()
	var weight: Array[int] = []
	for a: int in range(SLOTS):
		if not _tower[a]:
			continue
		for b: int in range(a + 1, SLOTS):
			if not _tower[b]:
				continue
			var da: Vector2 = _axis(a)
			var db: Vector2 = _axis(b)
			var cross: float = da.cross(db)
			if absf(cross) < 0.001:
				continue  # parallel axes never meet
			var diff: Vector2 = _tower_pos(b) - _tower_pos(a)
			var along_a: float = diff.cross(db) / cross
			var along_b: float = diff.cross(da) / cross
			if along_a <= 0.0 or along_b <= 0.0 or along_a > TOWER_RANGE or along_b > TOWER_RANGE:
				continue
			var point: Vector2 = _tower_pos(a) + da * along_a
			if point.length() < PLANET_R + 10.0:
				continue
			var found: int = -1
			for f: int in range(_focus_pos.size()):
				if _focus_pos[f].distance_to(point) < focus_radius:
					found = f
					break
			if found == -1:
				_focus_pos.append(point)
				_focus_feed.append(PackedInt32Array())
				_focus_hot.append(false)
				weight.append(0)
				found = _focus_pos.size() - 1
			_focus_pos[found] = (_focus_pos[found] * float(weight[found]) + point) / float(weight[found] + 1)
			weight[found] += 1
			for slot: int in [a, b]:
				if not _focus_feed[found].has(slot):
					_focus_feed[found].append(slot)


# ---- input ----------------------------------------------------------------------

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventScreenTouch:
		var touch: InputEventScreenTouch = event
		if touch.pressed:
			if touch.position.x > _play_size().x or _finger != -1:
				return  # on the tuning panel, or a second finger
			_finger = touch.index
			_finger_start = touch.position
			_finger_moved = false
			_grab_last = touch.position
			_grab_accum = 0.0
			_grab_rate = 0.0
		elif touch.index == _finger:
			_finger = -1
			if not _finger_moved:
				_tap((touch.position - _center()) / _world_scale())
	elif event is InputEventScreenDrag:
		var drag: InputEventScreenDrag = event
		if drag.index != _finger:
			return
		if drag.position.distance_to(_finger_start) > TAP_MAX_PX:
			_finger_moved = true
		if _finger_moved:
			# The movement before the drag threshold counts too.
			_grab_accum += _angle_step(_grab_last, drag.position)
			_grab_last = drag.position
	elif event is InputEventKey:
		var key: InputEventKey = event
		if key.pressed and not key.echo:
			match key.physical_keycode:
				KEY_1: _set_form(Form.STRAIGHT)
				KEY_2: _set_form(Form.MOUNT)
				KEY_3: _set_form(Form.REAIM)
				KEY_4: _set_form(Form.TWIN)
				KEY_SPACE: _start_wave()


## Planet rotation that moving a finger from `a` to `b` asks for: the change of
## its angle around the planet centre (game/input/input_router.gd). Close to the
## centre a horizontal drag counts as dragging the planet's top edge.
func _angle_step(a: Vector2, b: Vector2) -> float:
	var ra: Vector2 = a - _center()
	var rb: Vector2 = b - _center()
	var planet_px: float = PLANET_R * _world_scale()
	var min_r: float = maxf(planet_px * 0.35, 40.0)
	if ra.length() < min_r or rb.length() < min_r:
		return (b.x - a.x) / maxf(planet_px, 1.0)
	return wrapf(rb.angle() - ra.angle(), -PI, PI)


func _tap(world: Vector2) -> void:
	var d: float = world.length()
	var slot: int = -1
	if d > PLANET_R * 0.3 and d < PLANET_R + 90.0:
		slot = posmod(roundi((world.angle() - rot) / TAU * float(SLOTS)), SLOTS)
	_selected = -1 if slot == _selected else slot
	_refresh_bar()


func _rotate_input(delta: float) -> float:
	var v: float = 0.0
	if _finger != -1 and _finger_moved and delta > 0.0:
		var raw: float = _grab_accum / delta
		_grab_accum = 0.0
		_grab_rate = lerpf(_grab_rate, raw, 1.0 - exp(-GRAB_SMOOTHING * delta))
		v = _grab_rate / GRAB_FULL_SPEED
	if Input.is_physical_key_pressed(KEY_A) or Input.is_physical_key_pressed(KEY_Q):
		v -= 1.0
	if Input.is_physical_key_pressed(KEY_D) or Input.is_physical_key_pressed(KEY_E):
		v += 1.0
	return clampf(v, -1.0, 1.0)


# ---- simulation -------------------------------------------------------------

func _process(delta: float) -> void:
	var dt: float = clampf(delta, 0.001, 0.05)
	var input_value: float = _rotate_input(delta)
	if absf(input_value) > 0.01:
		rot_vel = move_toward(rot_vel, input_value * ROT_MAX, ROT_ACCEL * dt)
	else:
		rot_vel *= exp(-ROT_DAMPING * dt)
	rot = wrapf(rot + rot_vel * dt, -PI, PI)
	_step_wave(dt)
	_step_enemies(dt)
	_step_towers(dt)
	var blend: float = 1.0 - exp(-dt / 1.5)
	_dps_base = lerpf(_dps_base, _frame_base / dt, blend)
	_dps_bonus = lerpf(_dps_bonus, _frame_bonus / dt, blend)
	_wave_base += _frame_base
	_wave_bonus += _frame_bonus
	_frame_base = 0.0
	_frame_bonus = 0.0
	_update_dps_label()
	queue_redraw()


func _lane_bearing(lane: int) -> float:
	var base: float = LANE_SETS[(_wave - 1) % LANE_SETS.size()][lane]
	var sway: float = sin(_wave_time * 0.35 + float(lane) * 2.0) if _wave_on else 0.0
	return deg_to_rad(base + LANE_SWAY_DEG * sway)


func _step_wave(dt: float) -> void:
	if not _wave_on:
		return
	_wave_time += dt
	if _wave_time < WAVE_SECONDS:
		for lane: int in range(2):
			_spawn_acc[lane] += spawn_rate * dt
			# Swarm clumps: a steady trickle dies at the edge of range and never
			# reaches a focus zone.
			while _spawn_acc[lane] >= float(CLUMP):
				_spawn_acc[lane] -= float(CLUMP)
				for n: int in range(CLUMP):
					var bearing: float = _lane_bearing(lane) + deg_to_rad(randf_range(-3.0, 3.0))
					_enemy_pos.append(Vector2.from_angle(bearing) * (SPAWN_R + CLUMP_SPACING * float(n)))
					_enemy_hp.append(ENEMY_HP)
	elif _enemy_pos.is_empty():
		_wave_on = false
		var share: float = 100.0 * _wave_bonus / maxf(_wave_base + _wave_bonus, 1.0)
		_results[form] = "wave %d: %d killed, %d leaked, focus %.0f%% of damage" % [_wave, _kills, _leaks, share]
		_wave += 1
		_refresh_bar()


func _step_enemies(dt: float) -> void:
	var i: int = _enemy_pos.size() - 1
	while i >= 0:
		var p: Vector2 = _enemy_pos[i]
		p -= p.normalized() * ENEMY_SPEED * dt
		_enemy_pos[i] = p
		if p.length() <= PLANET_R + ENEMY_R:
			_leaks += 1
			_remove_enemy(i)
		i -= 1


func _remove_enemy(index: int) -> void:
	var last: int = _enemy_pos.size() - 1
	_enemy_pos[index] = _enemy_pos[last]
	_enemy_hp[index] = _enemy_hp[last]
	_enemy_pos.pop_back()
	_enemy_hp.pop_back()


## Number of towers feeding the best focus zone that `slot` feeds and that
## holds the world point `p`; 1 when there is none.
func _feeders_at(slot: int, p: Vector2) -> int:
	var best: int = 1
	for f: int in range(_focus_pos.size()):
		if _focus_feed[f].has(slot) and _focus_pos[f].rotated(rot).distance_to(p) <= focus_radius:
			best = maxi(best, _focus_feed[f].size())
	return best


## BRIEF §6 tower arc: target iff within range and within the arc around the
## aim axis α = θ + ρ + aimOffset. Targets inside the tower's own focus zone go
## first, then whatever is closest to the planet.
func _step_towers(dt: float) -> void:
	for f: int in range(_focus_pos.size()):
		_focus_hot[f] = false
		var centre: Vector2 = _focus_pos[f].rotated(rot)
		for p: Vector2 in _enemy_pos:
			if centre.distance_to(p) <= focus_radius:
				_focus_hot[f] = true
				break
	for slot: int in range(SLOTS):
		if not _tower[slot]:
			continue
		_cooldown[slot] -= dt
		if _cooldown[slot] > 0.0:
			continue
		var origin: Vector2 = _tower_pos(slot).rotated(rot)
		var axis: float = _slot_angle(slot) + rot + _offset[slot]
		var target: int = -1
		var target_feeders: int = 1
		var target_score: float = INF
		for i: int in range(_enemy_pos.size()):
			var to: Vector2 = _enemy_pos[i] - origin
			if to.length() > TOWER_RANGE or absf(angle_difference(axis, to.angle())) > TOWER_ARC:
				continue
			var feeders: int = _feeders_at(slot, _enemy_pos[i])
			var score: float = _enemy_pos[i].length() - (10000.0 if feeders > 1 else 0.0)
			if score < target_score:
				target = i
				target_feeders = feeders
				target_score = score
		if target == -1:
			_cooldown[slot] = 0.0
			continue
		_cooldown[slot] = 1.0 / TOWER_RATE
		var mult: float = _focus_mult(target_feeders)
		_frame_base += TOWER_DAMAGE
		_frame_bonus += TOWER_DAMAGE * (mult - 1.0)
		_tracer_from.append(origin)
		_tracer_to.append(_enemy_pos[target])
		_tracer_age.append(0.0)
		_tracer_boost.append(target_feeders > 1)
		_enemy_hp[target] -= TOWER_DAMAGE * mult
		if _enemy_hp[target] <= 0.0:
			_kills += 1
			stardust += KILL_REWARD
			_remove_enemy(target)
	var t: int = _tracer_age.size() - 1
	while t >= 0:
		_tracer_age[t] += dt
		if _tracer_age[t] > 0.09:
			_tracer_from.remove_at(t)
			_tracer_to.remove_at(t)
			_tracer_age.remove_at(t)
			_tracer_boost.remove_at(t)
		t -= 1


func _update_dps_label() -> void:
	var gain: float = 100.0 * _dps_bonus / _dps_base if _dps_base > 0.5 else 0.0
	var best: int = 1
	for feed: PackedInt32Array in _focus_feed:
		best = maxi(best, feed.size())
	_dps_label.text = "DPS with focus: %.0f\nwithout focus: %.0f  (%+.0f%%)\nfocus points: %d, best ×%.2f" % [
		_dps_base + _dps_bonus, _dps_base, gain, _focus_pos.size(), _focus_mult(best)]


# ---- drawing --------------------------------------------------------------------

func _draw() -> void:
	var k: float = _world_scale()
	draw_set_transform(_center(), 0.0, Vector2(k, k))

	# Lanes: this wave's, or the next wave's forecast between waves.
	for lane: int in range(2):
		var lane_dir: Vector2 = Vector2.from_angle(_lane_bearing(lane))
		draw_line(lane_dir * (PLANET_R + 20.0), lane_dir * SPAWN_R, Color(COL_ENEMY, 0.12 if _wave_on else 0.3), 14.0)
		draw_circle(lane_dir * (VIEW_R - 12.0), 9.0, Color(COL_ENEMY, 0.8))

	draw_circle(Vector2.ZERO, PLANET_R, COL_PLANET)
	draw_line(Vector2.ZERO, Vector2.from_angle(rot) * (PLANET_R - 14.0), Color(1, 1, 1, 0.15), 2.0)

	# Candidate mounts for an empty selected slot.
	if _selected != -1 and not _tower[_selected] and (form == Form.MOUNT or form == Form.REAIM):
		var from: Vector2 = _tower_pos(_selected).rotated(rot)
		for s: int in range(-2, 3):
			var a: float = _slot_angle(_selected) + rot + deg_to_rad(float(s) * offset_step_deg)
			draw_dashed_line(from, from + Vector2.from_angle(a) * TOWER_RANGE, Color(1, 1, 1, 0.3), 1.5, 8.0)
			draw_string(_font, from + Vector2.from_angle(a) * 150.0 + Vector2(-14.0, 5.0), _step_text(s), HORIZONTAL_ALIGNMENT_LEFT, -1.0, 15, COL_TEXT)

	# Slots, towers, aim axes and arcs.
	for slot: int in range(SLOTS):
		var pos: Vector2 = _tower_pos(slot).rotated(rot)
		var normal: Vector2 = pos / PLANET_R
		if slot == _selected:
			draw_arc(pos + normal * 8.0, 17.0, 0.0, TAU, 24, Color(1, 1, 1, 0.9), 2.5)
		if not _tower[slot]:
			draw_circle(pos + normal * 6.0, 3.5, Color(1, 1, 1, 0.3))
			continue
		var axis: float = _slot_angle(slot) + rot + _offset[slot]
		var dir: Vector2 = Vector2.from_angle(axis)
		var wedge: PackedVector2Array = PackedVector2Array([pos])
		for n: int in range(9):
			wedge.append(pos + Vector2.from_angle(axis + lerpf(-TOWER_ARC, TOWER_ARC, float(n) / 8.0)) * TOWER_RANGE)
		draw_colored_polygon(wedge, Color(COL_TOWER, 0.045))
		draw_line(pos, pos + dir * TOWER_RANGE, Color(COL_TOWER, 0.45), 1.5)
		if _partner[slot] > slot:
			var other: Vector2 = _tower_pos(_partner[slot]).rotated(rot)
			draw_line(pos + normal * 8.0, other + other / PLANET_R * 8.0, Color(COL_FOCUS, 0.5), 3.0)
		var side: Vector2 = dir.orthogonal() * 8.0
		var base: Vector2 = pos + normal * 4.0
		draw_colored_polygon(PackedVector2Array([base + dir * 18.0, base + side, base - side]), COL_TOWER)

	# Focus points: bright axis segments up to the point, zone, reticle, bonus.
	for f: int in range(_focus_pos.size()):
		var p: Vector2 = _focus_pos[f].rotated(rot)
		var hot: bool = _focus_hot[f]
		var col: Color = Color(COL_FOCUS, 1.0 if hot else 0.7)
		for slot: int in _focus_feed[f]:
			draw_line(_tower_pos(slot).rotated(rot), p, Color(COL_FOCUS, 0.55), 2.0)
		draw_circle(p, focus_radius, Color(COL_FOCUS, 0.2 if hot else 0.08))
		draw_arc(p, focus_radius, 0.0, TAU, 40, col, 3.0 if hot else 2.0)
		for n: int in range(4):
			var tick: Vector2 = Vector2.from_angle(float(n) * PI * 0.5)
			draw_line(p + tick * (focus_radius - 7.0), p + tick * (focus_radius + 7.0), col, 2.0)
		draw_circle(p, 2.5, col)
		var label: String = "+%d%%" % roundi((_focus_mult(_focus_feed[f].size()) - 1.0) * 100.0)
		draw_string(_font, p + Vector2(focus_radius + 10.0, 6.0), label, HORIZONTAL_ALIGNMENT_LEFT, -1.0, 18, col)

	for i: int in range(_tracer_age.size()):
		var boosted: bool = _tracer_boost[i]
		draw_line(_tracer_from[i], _tracer_to[i], Color(COL_FOCUS if boosted else COL_TOWER, 0.9), 3.0 if boosted else 1.5)
	for i: int in range(_enemy_pos.size()):
		var health: float = clampf(_enemy_hp[i] / ENEMY_HP, 0.0, 1.0)
		draw_circle(_enemy_pos[i], ENEMY_R, COL_ENEMY.lerp(Color(1.0, 0.8, 0.5), 1.0 - health))

	draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
	_draw_hud()


func _draw_hud() -> void:
	var phase: String = "between waves: build and re-aim, then Start"
	if _wave_on:
		phase = "wave %d, %d s of spawns left" % [_wave, ceili(maxf(WAVE_SECONDS - _wave_time, 0.0))]
	var lines: Array[String] = [
		"Stardust %d    %s" % [stardust, phase],
		"killed %d, leaked %d" % [_kills, _leaks],
	]
	for i: int in range(_results.size()):
		if _results[i] != "":
			lines.append("%s, %s" % [FORM_NAMES[i].substr(2), _results[i]])
	for i: int in range(lines.size()):
		draw_string(_font, Vector2(14.0, 26.0 + 22.0 * float(i)), lines[i], HORIZONTAL_ALIGNMENT_LEFT, -1.0, 16, COL_TEXT)
