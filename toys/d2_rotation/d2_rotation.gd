extends Node2D
## Toy D2: rotation and control schemes (BRIEF §10, DESIGN Q3). Throwaway.
##
## Spin the planet with one of three schemes and feel how rotation aims the
## towers: each tower fires only inside its arc, dots fly in on two lanes.
## Touch first; desktop: mouse acts as a finger, A/D or Q/E rotate, 1/2/3 pick
## the scheme.

enum Scheme { DRAG, BUTTONS, WHEEL }

const PANEL_W: float = 300.0
const PLANET_R: float = 100.0
const SLOTS: int = 36
const VIEW_R: float = 520.0  # world units from the centre to the nearest screen edge
const LANES_DEG: Array[float] = [30.0, 170.0]
const SPAWN_R: float = 700.0
const ENEMY_SPEED: float = 40.0
const SHOT_SPEED: float = 900.0
const COL_BG: Color = Color(0.02, 0.02, 0.05)
const COL_PLANET: Color = Color(0.16, 0.2, 0.3)
const COL_ENEMY: Color = Color(0.9, 0.25, 0.25)
const COL_TEXT: Color = Color(0.85, 0.88, 0.95)

# slot, half arc (deg), range, shots per second, colour: Gatling and Cannon numbers from BRIEF §8.2.
const TOWERS: Array[Dictionary] = [
	{"slot": 0, "arc": 40.0, "range": 350.0, "rate": 6.0, "color": Color(0.4, 0.8, 1.0)},
	{"slot": 5, "arc": 25.0, "range": 450.0, "rate": 0.8, "color": Color(1.0, 0.7, 0.3)},
	{"slot": 16, "arc": 40.0, "range": 350.0, "rate": 6.0, "color": Color(0.4, 0.8, 1.0)},
	{"slot": 23, "arc": 25.0, "range": 450.0, "rate": 0.8, "color": Color(1.0, 0.7, 0.3)},
]

# Tunables (all on screen as sliders).
var max_speed_deg: float = 90.0
var accel_deg: float = 360.0
var damping: float = 4.0
var drag_px: float = 120.0  # drag distance for full input
var wheel_gain: float = 1.0  # planet turns per knob turn
var spawn_rate: float = 0.8  # dots per second per lane
var invert: bool = false
var scheme: Scheme = Scheme.DRAG

var rot: float = 0.0  # planet rotation, radians
var rot_vel: float = 0.0  # rad/s
var input_value: float = 0.0  # [-1, 1]

var _touches: Dictionary[int, Vector2] = {}  # finger -> current position
var _touch_start: Dictionary[int, Vector2] = {}
var _rotate_finger: int = -1
var _wheel_last_angle: float = 0.0
var _wheel_pending: float = 0.0  # radians the planet still owes the knob
var _knob_angle: float = 0.0

var _enemies: Array[Vector2] = []
var _shots: Array[Vector2] = []
var _shot_targets: Array[int] = []
var _cooldowns: Array[float] = []
var _has_target: Array[bool] = []
var _spawn_acc: Array[float] = [0.0, 0.0]
var _kills: int = 0
var _leaks: int = 0
var _time: float = 0.0
var _time_in_arc: float = 0.0
var _touch_count: int = 0
var _scheme_buttons: Array[Button] = []
var _font: Font


func _ready() -> void:
	Input.emulate_touch_from_mouse = true
	RenderingServer.set_default_clear_color(COL_BG)
	_font = ThemeDB.fallback_font
	for i: int in range(TOWERS.size()):
		_cooldowns.append(0.0)
		_has_target.append(false)
	_build_ui()


# ---- layout -----------------------------------------------------------------

func _play_size() -> Vector2:
	var s: Vector2 = get_viewport_rect().size
	return Vector2(maxf(s.x - PANEL_W, 200.0), s.y)


func _center() -> Vector2:
	return _play_size() * 0.5


func _world_scale() -> float:
	var p: Vector2 = _play_size()
	return minf(p.x, p.y) * 0.5 / VIEW_R * 1.15


func _button_rect(right: bool) -> Rect2:
	var p: Vector2 = _play_size()
	var size: float = minf(p.y * 0.22, 150.0)
	var x: float = 16.0 + (size + 12.0 if right else 0.0)
	return Rect2(x, p.y - size - 16.0, size, size)


func _knob_center() -> Vector2:
	var p: Vector2 = _play_size()
	return Vector2(_knob_radius() + 24.0, p.y - _knob_radius() - 24.0)


func _knob_radius() -> float:
	return minf(_play_size().y * 0.2, 130.0)


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
	var margin: MarginContainer = MarginContainer.new()
	for side: String in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 10)
	panel.add_child(margin)
	var box: VBoxContainer = VBoxContainer.new()
	box.add_theme_constant_override("separation", 4)
	margin.add_child(box)

	var title: Label = Label.new()
	title.text = "D2 rotation toy"
	box.add_child(title)
	var names: Array[String] = ["1 Drag (left half)", "2 Hold buttons", "3 Wheel"]
	for i: int in range(names.size()):
		var b: Button = Button.new()
		b.text = names[i]
		b.toggle_mode = true
		b.custom_minimum_size = Vector2(0, 40)
		b.focus_mode = Control.FOCUS_NONE
		b.pressed.connect(_set_scheme.bind(i))
		box.add_child(b)
		_scheme_buttons.append(b)
	_set_scheme(Scheme.DRAG)

	_add_slider(box, "rotMaxSpeed °/s", 20.0, 360.0, 5.0, max_speed_deg, func(v: float) -> void: max_speed_deg = v)
	_add_slider(box, "rotAccel °/s²", 30.0, 1440.0, 10.0, accel_deg, func(v: float) -> void: accel_deg = v)
	_add_slider(box, "rotDamping 1/s", 0.0, 12.0, 0.25, damping, func(v: float) -> void: damping = v)
	_add_slider(box, "drag: px for full input", 30.0, 300.0, 5.0, drag_px, func(v: float) -> void: drag_px = v)
	_add_slider(box, "wheel: gain", 0.25, 4.0, 0.05, wheel_gain, func(v: float) -> void: wheel_gain = v)
	_add_slider(box, "dots per s per lane", 0.0, 3.0, 0.1, spawn_rate, func(v: float) -> void: spawn_rate = v)

	var inv: CheckButton = CheckButton.new()
	inv.text = "Invert direction"
	inv.focus_mode = Control.FOCUS_NONE
	inv.toggled.connect(func(on: bool) -> void: invert = on)
	box.add_child(inv)
	var reset: Button = Button.new()
	reset.text = "Reset counters"
	reset.focus_mode = Control.FOCUS_NONE
	reset.custom_minimum_size = Vector2(0, 36)
	reset.pressed.connect(_reset_counters)
	box.add_child(reset)


func _add_slider(box: VBoxContainer, title: String, lo: float, hi: float, step: float, value: float, apply: Callable) -> void:
	var label: Label = Label.new()
	label.add_theme_font_size_override("font_size", 14)
	box.add_child(label)
	var slider: HSlider = HSlider.new()
	slider.min_value = lo
	slider.max_value = hi
	slider.step = step
	slider.value = value
	slider.custom_minimum_size = Vector2(0, 30)
	slider.focus_mode = Control.FOCUS_NONE
	box.add_child(slider)
	var update: Callable = func(v: float) -> void:
		label.text = "%s: %s" % [title, String.num(v, 2)]
		apply.call(v)
	slider.value_changed.connect(update)
	update.call(value)


func _set_scheme(index: int) -> void:
	scheme = index as Scheme
	for i: int in range(_scheme_buttons.size()):
		_scheme_buttons[i].set_pressed_no_signal(i == index)
	_rotate_finger = -1
	_wheel_pending = 0.0


func _reset_counters() -> void:
	_kills = 0
	_leaks = 0
	_time = 0.0
	_time_in_arc = 0.0
	_touch_count = 0


# ---- input ----------------------------------------------------------------------

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventScreenTouch:
		var touch: InputEventScreenTouch = event
		if touch.pressed:
			if touch.position.x > _play_size().x:
				return  # on the tuning panel
			_touches[touch.index] = touch.position
			_touch_start[touch.index] = touch.position
			_touch_count += 1
			_on_press(touch.index, touch.position)
		else:
			_touches.erase(touch.index)
			_touch_start.erase(touch.index)
			if touch.index == _rotate_finger:
				_rotate_finger = -1
	elif event is InputEventScreenDrag:
		var drag: InputEventScreenDrag = event
		if _touches.has(drag.index):
			_touches[drag.index] = drag.position
			if scheme == Scheme.WHEEL and drag.index == _rotate_finger:
				var a: float = (drag.position - _knob_center()).angle()
				var d: float = wrapf(a - _wheel_last_angle, -PI, PI)
				_wheel_last_angle = a
				_knob_angle += d
				_wheel_pending += d * wheel_gain * (-1.0 if invert else 1.0)
	elif event is InputEventKey:
		var key: InputEventKey = event
		if key.pressed and not key.echo:
			match key.keycode:
				KEY_1: _set_scheme(Scheme.DRAG)
				KEY_2: _set_scheme(Scheme.BUTTONS)
				KEY_3: _set_scheme(Scheme.WHEEL)


func _on_press(finger: int, pos: Vector2) -> void:
	match scheme:
		Scheme.DRAG:
			if pos.x < _play_size().x * 0.5 and _rotate_finger == -1:
				_rotate_finger = finger
		Scheme.WHEEL:
			if pos.distance_to(_knob_center()) < _knob_radius() * 1.5 and _rotate_finger == -1:
				_rotate_finger = finger
				_wheel_last_angle = (pos - _knob_center()).angle()


func _read_input() -> float:
	var v: float = 0.0
	match scheme:
		Scheme.DRAG:
			if _rotate_finger != -1 and _touches.has(_rotate_finger):
				v = (_touches[_rotate_finger].x - _touch_start[_rotate_finger].x) / drag_px
		Scheme.BUTTONS:
			for pos: Vector2 in _touches.values():
				if _button_rect(false).has_point(pos):
					v -= 1.0
				if _button_rect(true).has_point(pos):
					v += 1.0
		Scheme.WHEEL:
			# The planet chases the knob through the same accel/max limits;
			# the knob already carries the invert flag.
			return clampf(_wheel_pending / deg_to_rad(12.0), -1.0, 1.0) + _key_input()
	if invert:
		v = -v
	return clampf(v + _key_input(), -1.0, 1.0)


func _key_input() -> float:
	var v: float = 0.0
	if Input.is_physical_key_pressed(KEY_A) or Input.is_physical_key_pressed(KEY_Q):
		v -= 1.0
	if Input.is_physical_key_pressed(KEY_D) or Input.is_physical_key_pressed(KEY_E):
		v += 1.0
	return v


# ---- simulation -------------------------------------------------------------

func _process(delta: float) -> void:
	var dt: float = minf(delta, 0.05)
	input_value = clampf(_read_input(), -1.0, 1.0)
	_step_rotation(dt)
	_step_enemies(dt)
	_step_towers(dt)
	_step_shots(dt)
	_time += dt
	if _has_target.has(true):
		_time_in_arc += dt
	queue_redraw()


## BRIEF §4.1: input accelerates toward input × rotMaxSpeed with rotAccel,
## and the speed decays with rotDamping when released.
func _step_rotation(dt: float) -> void:
	var max_speed: float = deg_to_rad(max_speed_deg)
	if absf(input_value) > 0.01:
		rot_vel = move_toward(rot_vel, input_value * max_speed, deg_to_rad(accel_deg) * dt)
	else:
		rot_vel *= exp(-damping * dt)
	var step: float = rot_vel * dt
	rot = wrapf(rot + step, -PI, PI)
	if scheme == Scheme.WHEEL:
		if _rotate_finger == -1 and absf(_wheel_pending) < deg_to_rad(1.0):
			_wheel_pending = 0.0
		else:
			_wheel_pending -= step


func _step_enemies(dt: float) -> void:
	for lane: int in range(LANES_DEG.size()):
		_spawn_acc[lane] += spawn_rate * dt
		while _spawn_acc[lane] >= 1.0:
			_spawn_acc[lane] -= 1.0
			var bearing: float = deg_to_rad(LANES_DEG[lane] + randf_range(-6.0, 6.0))
			_enemies.append(Vector2.from_angle(bearing) * SPAWN_R)
	var i: int = _enemies.size() - 1
	while i >= 0:
		var p: Vector2 = _enemies[i]
		p -= p.normalized() * ENEMY_SPEED * dt
		_enemies[i] = p
		if p.length() <= PLANET_R:
			_leaks += 1
			_remove_enemy(i)
		i -= 1


func _remove_enemy(index: int) -> void:
	var last: int = _enemies.size() - 1
	_enemies[index] = _enemies[last]
	_enemies.pop_back()
	# Shots aimed at the removed dot are dropped; those aimed at the moved one follow it.
	var s: int = _shots.size() - 1
	while s >= 0:
		if _shot_targets[s] == index:
			_shots.remove_at(s)
			_shot_targets.remove_at(s)
		elif _shot_targets[s] == last:
			_shot_targets[s] = index
		s -= 1


func _tower_pos(tower: Dictionary) -> Vector2:
	var slot: int = tower["slot"]
	return Vector2.from_angle(TAU * float(slot) / float(SLOTS) + rot) * PLANET_R


## BRIEF §6 tower arc: in range and within arcDeg of the aim axis (slot normal).
func _step_towers(dt: float) -> void:
	for t: int in range(TOWERS.size()):
		var tower: Dictionary = TOWERS[t]
		var pos: Vector2 = _tower_pos(tower)
		var axis: Vector2 = pos.normalized()
		var arc: float = deg_to_rad(tower["arc"])
		var reach: float = tower["range"]
		var best: int = -1
		var best_dist: float = INF
		for e: int in range(_enemies.size()):
			var to: Vector2 = _enemies[e] - pos
			if to.length() > reach or absf(axis.angle_to(to)) > arc:
				continue
			var d: float = _enemies[e].length()  # "first": closest to the planet
			if d < best_dist:
				best_dist = d
				best = e
		_has_target[t] = best != -1
		_cooldowns[t] = maxf(_cooldowns[t] - dt, 0.0)
		if best != -1 and _cooldowns[t] <= 0.0:
			var rate: float = tower["rate"]
			_cooldowns[t] = 1.0 / rate
			_shots.append(pos)
			_shot_targets.append(best)


func _step_shots(dt: float) -> void:
	for i: int in range(_shots.size()):
		_shots[i] = _shots[i].move_toward(_enemies[_shot_targets[i]], SHOT_SPEED * dt)
	var s: int = _shots.size() - 1
	while s >= 0:
		if _shots[s].distance_to(_enemies[_shot_targets[s]]) < 4.0:
			_kills += 1
			_remove_enemy(_shot_targets[s])  # drops every shot aimed at it
			s = _shots.size()  # indices moved: scan again from the end
		s -= 1


# ---- drawing --------------------------------------------------------------------

func _draw() -> void:
	var c: Vector2 = _center()
	var k: float = _world_scale()
	draw_set_transform(c, 0.0, Vector2(k, k))
	for deg: float in LANES_DEG:
		var dir: Vector2 = Vector2.from_angle(deg_to_rad(deg))
		draw_dashed_line(dir * PLANET_R, dir * SPAWN_R, Color(1, 1, 1, 0.12), 2.0, 12.0)
	for t: int in range(TOWERS.size()):
		_draw_arc_wedge(TOWERS[t], _has_target[t])
	draw_circle(Vector2.ZERO, PLANET_R, COL_PLANET)
	for i: int in range(SLOTS):
		var dir: Vector2 = Vector2.from_angle(TAU * float(i) / float(SLOTS) + rot)
		draw_line(dir * (PLANET_R - 6.0), dir * PLANET_R, Color(1, 1, 1, 0.5 if i == 0 else 0.15), 1.5)
	# A spoke so the spin is readable.
	draw_line(Vector2.ZERO, Vector2.from_angle(rot) * (PLANET_R - 10.0), Color(1, 1, 1, 0.25), 2.0)
	for tower: Dictionary in TOWERS:
		var pos: Vector2 = _tower_pos(tower)
		var col: Color = tower["color"]
		draw_set_transform(c + pos * k, pos.angle(), Vector2(k, k))
		draw_rect(Rect2(-5.0, -7.0, 12.0, 14.0), col)
		draw_set_transform(c, 0.0, Vector2(k, k))
	for p: Vector2 in _enemies:
		draw_circle(p, 8.0, COL_ENEMY)
	for p: Vector2 in _shots:
		draw_circle(p, 3.0, Color.WHITE)
	draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
	_draw_controls()
	_draw_hud()


func _draw_arc_wedge(tower: Dictionary, active: bool) -> void:
	var pos: Vector2 = _tower_pos(tower)
	var arc: float = deg_to_rad(tower["arc"])
	var reach: float = tower["range"]
	var col: Color = tower["color"]
	var pts: PackedVector2Array = PackedVector2Array([pos])
	var steps: int = 16
	for i: int in range(steps + 1):
		var a: float = pos.angle() - arc + 2.0 * arc * float(i) / float(steps)
		pts.append(pos + Vector2.from_angle(a) * reach)
	col.a = 0.16 if active else 0.07
	draw_colored_polygon(pts, col)
	col.a = 0.5
	pts.append(pos)
	draw_polyline(pts, col, 1.0)


func _draw_controls() -> void:
	var p: Vector2 = _play_size()
	match scheme:
		Scheme.DRAG:
			draw_rect(Rect2(0.0, 0.0, p.x * 0.5, p.y), Color(1, 1, 1, 0.03))
			draw_line(Vector2(p.x * 0.5, 0.0), Vector2(p.x * 0.5, p.y), Color(1, 1, 1, 0.08), 1.0)
			if _rotate_finger != -1 and _touches.has(_rotate_finger):
				var start: Vector2 = _touch_start[_rotate_finger]
				draw_line(start - Vector2(drag_px, 0.0), start + Vector2(drag_px, 0.0), Color(1, 1, 1, 0.3), 4.0)
				draw_circle(start, 6.0, Color(1, 1, 1, 0.4))
				draw_circle(Vector2(clampf(_touches[_rotate_finger].x, start.x - drag_px, start.x + drag_px), start.y), 18.0, Color(1, 1, 1, 0.5))
		Scheme.BUTTONS:
			for right: bool in [false, true]:
				var r: Rect2 = _button_rect(right)
				var held: bool = false
				for pos: Vector2 in _touches.values():
					held = held or r.has_point(pos)
				draw_rect(r, Color(1, 1, 1, 0.3 if held else 0.1))
				draw_rect(r, Color(1, 1, 1, 0.4), false, 2.0)
				_draw_turn_arrow(r.get_center(), r.size.x * 0.28, right)
		Scheme.WHEEL:
			var kc: Vector2 = _knob_center()
			var kr: float = _knob_radius()
			draw_circle(kc, kr, Color(1, 1, 1, 0.08))
			draw_arc(kc, kr, 0.0, TAU, 48, Color(1, 1, 1, 0.4), 2.0)
			for i: int in range(8):
				var dir: Vector2 = Vector2.from_angle(_knob_angle + TAU * float(i) / 8.0)
				draw_line(kc + dir * kr * 0.75, kc + dir * kr, Color(1, 1, 1, 0.9 if i == 0 else 0.3), 3.0)
			draw_circle(kc + Vector2.from_angle(_knob_angle) * kr * 0.55, kr * 0.14, Color(1, 1, 1, 0.6))


## A three-quarter circle with an arrow head: the hold-to-turn glyph.
func _draw_turn_arrow(center: Vector2, radius: float, clockwise: bool) -> void:
	var from: float = -PI * 0.5
	var to: float = from + (TAU * 0.75 if clockwise else -TAU * 0.75)
	draw_arc(center, radius, from, to, 24, Color.WHITE, 4.0)
	var tip: Vector2 = center + Vector2.from_angle(to) * radius
	var tangent: Vector2 = Vector2.from_angle(to + (PI * 0.5 if clockwise else -PI * 0.5))
	var normal: Vector2 = Vector2.from_angle(to)
	draw_colored_polygon(PackedVector2Array([tip + tangent * 14.0, tip + normal * 10.0, tip - normal * 10.0]), Color.WHITE)


func _draw_hud() -> void:
	var in_arc: float = 100.0 * _time_in_arc / maxf(_time, 0.001)
	var per_min: float = 60.0 * float(_touch_count) / maxf(_time, 1.0)
	var hints: Array[String] = [
		"drag left/right in the left half",
		"hold the two buttons (bottom left)",
		"spin the knob (bottom left)",
	]
	var lines: Array[String] = [
		"Scheme: %s  |  keys A/D or Q/E, 1-3 switch" % hints[scheme],
		"input %+.2f   speed %+.0f°/s   angle %.0f°" % [input_value, rad_to_deg(rot_vel), rad_to_deg(rot)],
		"kills %d   leaks %d   target in arc %.0f%%   touches/min %.0f" % [_kills, _leaks, in_arc, per_min],
	]
	for i: int in range(lines.size()):
		draw_string(_font, Vector2(14.0, 24.0 + 22.0 * float(i)), lines[i], HORIZONTAL_ALIGNMENT_LEFT, -1.0, 16, COL_TEXT)
	# Input bar under the text.
	var bar: Rect2 = Rect2(14.0, 80.0, 200.0, 8.0)
	draw_rect(bar, Color(1, 1, 1, 0.12))
	draw_rect(Rect2(bar.position.x + 100.0, bar.position.y, 100.0 * input_value, 8.0), Color(0.5, 0.9, 1.0))
