extends Node2D
## Toy D3: sun beam, shadows and panel income (BRIEF §10, §6; DESIGN Q12). Throwaway.
##
## A band of parallel rays drifts around the planet. Tap a slot to toggle a
## solar panel; panels earn only while lit and unshaded. Eclipsers park on the
## sun line over your lit panels and cast a shadow; tap one to shoot it.
## Drag left/right (or A/D) to rotate the planet and chase the light.

const PANEL_W: float = 300.0
const PLANET_R: float = 100.0
const SLOTS: int = 36
const VIEW_R: float = 300.0  # world units from the centre to the nearest screen edge
const LANES_DEG: Array[float] = [30.0, 170.0]
# Planet rotation, BRIEF §8.2 defaults (tune these in toy D2).
const ROT_MAX: float = deg_to_rad(90.0)
const ROT_ACCEL: float = deg_to_rad(360.0)
const ROT_DAMPING: float = 4.0
const DRAG_PX: float = 120.0
const TAP_SLOP_PX: float = 14.0
# Eclipser, BRIEF §8.2. It spawns just off screen here (the game uses 900) so
# the toy does not make you wait.
const ECL_RADIUS: float = 28.0
const ECL_HP: float = 40.0
const ECL_SPEED: float = 25.0
const ECL_HOVER_R: float = 260.0
const ECL_HOVER_SECONDS: float = 15.0
const ECL_DESCEND_SPEED: float = 10.0
const ECL_SPAWN_R: float = 430.0
const ECL_RESPAWN_SECONDS: float = 3.0
const TAP_DAMAGE: float = 10.0

const COL_BG: Color = Color(0.02, 0.02, 0.05)
const COL_PLANET: Color = Color(0.13, 0.16, 0.25)
const COL_BEAM: Color = Color(1.0, 0.85, 0.45)
const COL_PANEL_LIT: Color = Color(1.0, 0.9, 0.3)
const COL_PANEL_SHADED: Color = Color(0.5, 0.3, 0.7)
const COL_PANEL_DARK: Color = Color(0.25, 0.35, 0.55)
const COL_TEXT: Color = Color(0.85, 0.88, 0.95)

enum Phase { FLY, HOVER, DESCEND }

# Tunables (all on screen as sliders).
var beam_width: float = 141.0
var drift_deg: float = 1.5  # sun drift, °/s
var panel_rate: float = 3.0  # stardust per second per lit panel
var eclipser_count: int = 1
var light_mult: float = 1.0  # wave lightMult (dim rounds)

var sun: float = 0.0  # sun bearing σ, radians
var rot: float = 0.0
var rot_vel: float = 0.0
var stardust: float = 0.0
var income: float = 0.0  # per second, right now

var _panels: Array[bool] = []
var _lit: Array[bool] = []  # inside the beam
var _shaded: Array[bool] = []  # inside the beam but behind an Eclipser
# Eclipsers as parallel arrays.
var _ecl_pos: Array[Vector2] = []
var _ecl_hp: Array[float] = []
var _ecl_phase: Array[Phase] = []
var _ecl_timer: Array[float] = []
var _ecl_radius: Array[float] = []  # current distance kept along the sun line
var _respawn: float = 0.0
var _spawned: int = 0
var _landed: int = 0
var _killed: int = 0
# Where the possible income went, summed over time.
var _earned: float = 0.0
var _lost_shade: float = 0.0
var _lost_unlit: float = 0.0

var _finger: int = -1
var _finger_start: Vector2 = Vector2.ZERO
var _finger_pos: Vector2 = Vector2.ZERO
var _finger_moved: bool = false
var _font: Font


func _ready() -> void:
	Input.emulate_touch_from_mouse = true
	RenderingServer.set_default_clear_color(COL_BG)
	_font = ThemeDB.fallback_font
	for i: int in range(SLOTS):
		_panels.append(i <= 2 or i >= SLOTS - 2)  # five panels facing the sun at the start
		_lit.append(false)
		_shaded.append(false)
	_build_ui()


# ---- layout -----------------------------------------------------------------

func _play_size() -> Vector2:
	var s: Vector2 = get_viewport_rect().size
	return Vector2(maxf(s.x - PANEL_W, 200.0), s.y)


func _center() -> Vector2:
	return _play_size() * 0.5


func _world_scale() -> float:
	var p: Vector2 = _play_size()
	return minf(p.x, p.y) * 0.5 / VIEW_R


func _to_world(screen: Vector2) -> Vector2:
	return (screen - _center()) / _world_scale()


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
	title.text = "D3 sun and economy toy"
	box.add_child(title)
	_add_slider(box, "beamWidth", 20.0, 200.0, 1.0, beam_width, func(v: float) -> void: beam_width = v)
	_add_slider(box, "sun drift °/s", -8.0, 8.0, 0.1, drift_deg, func(v: float) -> void: drift_deg = v)
	_add_slider(box, "panelRate /s", 0.0, 10.0, 0.25, panel_rate, func(v: float) -> void: panel_rate = v)
	_add_slider(box, "Eclipsers", 0.0, 4.0, 1.0, float(eclipser_count), func(v: float) -> void: eclipser_count = int(v))
	_add_slider(box, "lightMult (dim round)", 0.1, 1.0, 0.05, light_mult, func(v: float) -> void: light_mult = v)

	var buttons: Array[Array] = [
		["Fill every slot", _set_all_panels.bind(true)],
		["Clear panels", _set_all_panels.bind(false)],
		["Reset counters", _reset_counters],
	]
	for entry: Array in buttons:
		var b: Button = Button.new()
		b.text = entry[0]
		b.focus_mode = Control.FOCUS_NONE
		b.custom_minimum_size = Vector2(0, 38)
		var action: Callable = entry[1]
		b.pressed.connect(action)
		box.add_child(b)
	var help: Label = Label.new()
	help.add_theme_font_size_override("font_size", 13)
	help.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	help.text = "Tap a slot: panel on/off.\nTap an Eclipser: shoot it.\nDrag left/right or A/D: rotate."
	box.add_child(help)


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


func _set_all_panels(on: bool) -> void:
	for i: int in range(SLOTS):
		_panels[i] = on


func _reset_counters() -> void:
	stardust = 0.0
	_earned = 0.0
	_lost_shade = 0.0
	_lost_unlit = 0.0
	_landed = 0
	_killed = 0


# ---- input ----------------------------------------------------------------------

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventScreenTouch:
		var touch: InputEventScreenTouch = event
		if touch.pressed:
			if touch.position.x > _play_size().x or _finger != -1:
				return  # on the tuning panel, or a second finger
			_finger = touch.index
			_finger_start = touch.position
			_finger_pos = touch.position
			_finger_moved = false
		elif touch.index == _finger:
			_finger = -1
			if not _finger_moved:
				_tap(_to_world(touch.position))
	elif event is InputEventScreenDrag:
		var drag: InputEventScreenDrag = event
		if drag.index == _finger:
			_finger_pos = drag.position
			if _finger_pos.distance_to(_finger_start) > TAP_SLOP_PX:
				_finger_moved = true


func _tap(world: Vector2) -> void:
	for i: int in range(_ecl_pos.size()):
		if world.distance_to(_ecl_pos[i]) <= ECL_RADIUS + 14.0:
			_ecl_hp[i] -= TAP_DAMAGE
			return
	var d: float = world.length()
	if d < PLANET_R * 0.35 or d > PLANET_R + 70.0:
		return
	var slot: int = posmod(roundi((world.angle() - rot) / TAU * float(SLOTS)), SLOTS)
	_panels[slot] = not _panels[slot]


func _rotate_input() -> float:
	var v: float = 0.0
	if _finger != -1 and _finger_moved:
		v = (_finger_pos.x - _finger_start.x) / DRAG_PX
	if Input.is_physical_key_pressed(KEY_A) or Input.is_physical_key_pressed(KEY_Q):
		v -= 1.0
	if Input.is_physical_key_pressed(KEY_D) or Input.is_physical_key_pressed(KEY_E):
		v += 1.0
	return clampf(v, -1.0, 1.0)


# ---- simulation -------------------------------------------------------------

func _process(delta: float) -> void:
	var dt: float = minf(delta, 0.05)
	var input_value: float = _rotate_input()
	if absf(input_value) > 0.01:
		rot_vel = move_toward(rot_vel, input_value * ROT_MAX, ROT_ACCEL * dt)
	else:
		rot_vel *= exp(-ROT_DAMPING * dt)
	rot = wrapf(rot + rot_vel * dt, -PI, PI)
	sun = wrapf(sun + deg_to_rad(drift_deg) * dt, -PI, PI)
	_step_eclipsers(dt)
	_update_light()
	_step_income(dt)
	queue_redraw()


func _sun_dir() -> Vector2:
	return Vector2.from_angle(sun)


## Signed distance of a surface point from the beam's centre line: R·sin(φ − σ).
func _slot_offset(slot: int) -> float:
	return PLANET_R * sin(TAU * float(slot) / float(SLOTS) + rot - sun)


## BRIEF §6 beam: in the beam iff cos(φ − σ) > 0 and |R·sin(φ − σ)| ≤ W/2.
func _slot_in_beam(slot: int) -> bool:
	var a: float = TAU * float(slot) / float(SLOTS) + rot - sun
	return cos(a) > 0.0 and absf(PLANET_R * sin(a)) <= beam_width * 0.5


## BRIEF §6 shadows: a caster at p casts iff dot(p, s) > R; its offset is
## o = s.x·p.y − s.y·p.x and it shades lit points with R·sin(φ − σ) in [o − r, o + r].
func _update_light() -> void:
	var s: Vector2 = _sun_dir()
	for i: int in range(SLOTS):
		_lit[i] = _slot_in_beam(i)
		_shaded[i] = false
		if not _lit[i]:
			continue
		var y: float = _slot_offset(i)
		for p: Vector2 in _ecl_pos:
			if p.dot(s) > PLANET_R and absf(y - s.cross(p)) <= ECL_RADIUS:
				_shaded[i] = true
				break


func _step_income(dt: float) -> void:
	var rate: float = panel_rate * light_mult  # panelRate × beamStrength
	income = 0.0
	for i: int in range(SLOTS):
		if not _panels[i]:
			continue
		if _lit[i] and not _shaded[i]:
			income += rate
		elif _lit[i]:
			_lost_shade += rate * dt
		else:
			_lost_unlit += rate * dt
	stardust += income * dt
	_earned += income * dt


## Where an Eclipser wants to sit across the beam: the offset whose shadow
## covers the most lit panels not already claimed by an earlier Eclipser.
func _best_offset(claimed: Array[bool], current: float) -> float:
	var ys: Array[float] = []
	for i: int in range(SLOTS):
		if _panels[i] and _slot_in_beam(i) and not claimed[i]:
			ys.append(_slot_offset(i))
	if ys.is_empty():
		return 0.0
	var best: float = 0.0
	var best_count: int = -1
	for y: float in ys:
		for candidate: float in [y, y - ECL_RADIUS + 1.0, y + ECL_RADIUS - 1.0]:
			var count: int = 0
			for other: float in ys:
				if absf(other - candidate) <= ECL_RADIUS:
					count += 1
			if count > best_count or (count == best_count and absf(candidate - current) < absf(best - current)):
				best_count = count
				best = candidate
	for i: int in range(SLOTS):
		if _panels[i] and _slot_in_beam(i) and absf(_slot_offset(i) - best) <= ECL_RADIUS:
			claimed[i] = true
	return best


func _step_eclipsers(dt: float) -> void:
	# Keep the requested number alive.
	while _ecl_pos.size() > eclipser_count:
		_remove_eclipser(_ecl_pos.size() - 1)
	if _ecl_pos.size() < eclipser_count:
		_respawn -= dt
		if _respawn <= 0.0:
			_respawn = ECL_RESPAWN_SECONDS
			var bearing: float = deg_to_rad(LANES_DEG[_spawned % LANES_DEG.size()] + randf_range(-6.0, 6.0))
			_spawned += 1
			_ecl_pos.append(Vector2.from_angle(bearing) * ECL_SPAWN_R)
			_ecl_hp.append(ECL_HP)
			_ecl_phase.append(Phase.FLY)
			_ecl_timer.append(ECL_HOVER_SECONDS)
			_ecl_radius.append(ECL_HOVER_R)

	var s: Vector2 = _sun_dir()
	var perp: Vector2 = Vector2(-s.y, s.x)
	var claimed: Array[bool] = []
	claimed.resize(SLOTS)
	claimed.fill(false)
	var i: int = 0
	while i < _ecl_pos.size():
		if _ecl_hp[i] <= 0.0:
			_killed += 1
			_remove_eclipser(i)
			continue
		var offset: float = _best_offset(claimed, s.cross(_ecl_pos[i]))
		var target: Vector2 = s * _ecl_radius[i] + perp * offset
		_ecl_pos[i] = _ecl_pos[i].move_toward(target, ECL_SPEED * dt)
		match _ecl_phase[i]:
			Phase.FLY:
				if _ecl_pos[i].distance_to(target) < 2.0:
					_ecl_phase[i] = Phase.HOVER
			Phase.HOVER:
				_ecl_timer[i] -= dt
				if _ecl_timer[i] <= 0.0:
					_ecl_phase[i] = Phase.DESCEND
			Phase.DESCEND:
				_ecl_radius[i] -= ECL_DESCEND_SPEED * dt
		if _ecl_pos[i].length() <= PLANET_R + 1.0:
			_landed += 1
			_remove_eclipser(i)
			continue
		i += 1


func _remove_eclipser(index: int) -> void:
	_ecl_pos.remove_at(index)
	_ecl_hp.remove_at(index)
	_ecl_phase.remove_at(index)
	_ecl_timer.remove_at(index)
	_ecl_radius.remove_at(index)


# ---- drawing --------------------------------------------------------------------

func _draw() -> void:
	var c: Vector2 = _center()
	var k: float = _world_scale()
	var s: Vector2 = _sun_dir()
	var perp: Vector2 = Vector2(-s.y, s.x)
	var half: float = beam_width * 0.5
	var far: float = 2000.0
	draw_set_transform(c, 0.0, Vector2(k, k))

	# Beam: a band of parallel rays arriving from the sun bearing.
	var beam: Color = COL_BEAM
	beam.a = 0.06 + 0.16 * light_mult
	draw_colored_polygon(PackedVector2Array([perp * half, perp * half + s * far, -perp * half + s * far, -perp * half]), beam)
	beam.a = 0.10 + 0.15 * light_mult
	var rays: int = 9
	for i: int in range(rays):
		var o: float = lerpf(-half, half, float(i) / float(rays - 1))
		draw_line(perp * o, perp * o + s * far, beam, 1.0)

	# Shadows: the strip of the beam behind each casting Eclipser.
	for p: Vector2 in _ecl_pos:
		var along: float = p.dot(s)
		if along <= PLANET_R:
			continue
		var o: float = s.cross(p)
		var lo: float = maxf(o - ECL_RADIUS, -half)
		var hi: float = minf(o + ECL_RADIUS, half)
		if lo < hi:
			draw_colored_polygon(PackedVector2Array([perp * lo + s * along, perp * hi + s * along, perp * hi, perp * lo]), Color(COL_BG, 0.8))

	draw_circle(Vector2.ZERO, PLANET_R, COL_PLANET)
	# Lit arc on the rim, 2·asin(W / 2R) wide, then the shaded parts over it.
	var lit_half: float = asin(clampf(half / PLANET_R, 0.0, 1.0))
	draw_arc(Vector2.ZERO, PLANET_R - 2.0, sun - lit_half, sun + lit_half, 48, Color(COL_BEAM, 0.4 + 0.5 * light_mult), 4.0)
	for p: Vector2 in _ecl_pos:
		if p.dot(s) <= PLANET_R:
			continue
		var o: float = s.cross(p)
		var lo: float = maxf(o - ECL_RADIUS, -half)
		var hi: float = minf(o + ECL_RADIUS, half)
		if lo < hi:
			var a0: float = sun + asin(clampf(lo / PLANET_R, -1.0, 1.0))
			var a1: float = sun + asin(clampf(hi / PLANET_R, -1.0, 1.0))
			draw_arc(Vector2.ZERO, PLANET_R - 2.0, a0, a1, 16, Color(0.0, 0.0, 0.0, 0.9), 6.0)

	# Slots and panels.
	for i: int in range(SLOTS):
		var a: float = TAU * float(i) / float(SLOTS) + rot
		var dir: Vector2 = Vector2.from_angle(a)
		if _panels[i]:
			var col: Color = COL_PANEL_DARK
			if _lit[i]:
				col = COL_PANEL_SHADED if _shaded[i] else COL_PANEL_LIT
			draw_set_transform(c + dir * (PLANET_R + 5.0) * k, a, Vector2(k, k))
			draw_rect(Rect2(-4.0, -6.5, 8.0, 13.0), col)
			draw_set_transform(c, 0.0, Vector2(k, k))
		else:
			var tick: Color = Color(1.0, 0.9, 0.6, 0.7) if (_lit[i] and not _shaded[i]) else Color(1, 1, 1, 0.18)
			draw_circle(dir * (PLANET_R + 5.0), 2.0, tick)
	draw_line(Vector2.ZERO, Vector2.from_angle(rot) * (PLANET_R - 12.0), Color(1, 1, 1, 0.2), 2.0)

	# Eclipsers: dark disc, outline, HP bar.
	for i: int in range(_ecl_pos.size()):
		var p: Vector2 = _ecl_pos[i]
		draw_circle(p, ECL_RADIUS, Color(0.05, 0.03, 0.08))
		var ring: Color = Color(0.8, 0.4, 1.0) if _ecl_phase[i] != Phase.DESCEND else Color(1.0, 0.35, 0.35)
		draw_arc(p, ECL_RADIUS, 0.0, TAU, 32, ring, 2.0)
		draw_rect(Rect2(p.x - 24.0, p.y - ECL_RADIUS - 10.0, 48.0, 5.0), Color(0, 0, 0, 0.6))
		draw_rect(Rect2(p.x - 24.0, p.y - ECL_RADIUS - 10.0, 48.0 * maxf(_ecl_hp[i], 0.0) / ECL_HP, 5.0), Color(0.9, 0.3, 0.3))
		if _ecl_phase[i] == Phase.HOVER:
			draw_arc(p, ECL_RADIUS + 5.0, -PI * 0.5, -PI * 0.5 + TAU * _ecl_timer[i] / ECL_HOVER_SECONDS, 32, Color(1, 1, 1, 0.5), 2.0)

	# Sun marker at the edge of the view.
	draw_circle(s * (VIEW_R - 14.0), 10.0, Color(COL_BEAM, 0.4 + 0.6 * light_mult))
	draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
	_draw_hud()


func _draw_hud() -> void:
	var total: int = _panels.count(true)
	var lit_count: int = 0
	var shaded_count: int = 0
	for i: int in range(SLOTS):
		if _panels[i] and _lit[i]:
			if _shaded[i]:
				shaded_count += 1
			else:
				lit_count += 1
	var lit_arc: float = rad_to_deg(2.0 * asin(clampf(beam_width / (2.0 * PLANET_R), 0.0, 1.0)))
	var possible: float = maxf(_earned + _lost_shade + _lost_unlit, 0.001)
	var lines: Array[String] = [
		"Stardust %d    income %.1f/s" % [int(stardust), income],
		"panels %d: %d earning, %d shaded, %d out of the beam" % [total, lit_count, shaded_count, total - lit_count - shaded_count],
		"lit arc 2·asin(W/2R) = %.0f°  (%.0f of %d slots)   sun at %.0f°" % [lit_arc, lit_arc / 360.0 * float(SLOTS), SLOTS, rad_to_deg(sun)],
		"of possible income: earned %.0f%%, lost to shade %.0f%%, out of beam %.0f%%" % [100.0 * _earned / possible, 100.0 * _lost_shade / possible, 100.0 * _lost_unlit / possible],
		"Eclipsers shot %d, landed %d" % [_killed, _landed],
	]
	for i: int in range(lines.size()):
		draw_string(_font, Vector2(14.0, 24.0 + 21.0 * float(i)), lines[i], HORIZONTAL_ALIGNMENT_LEFT, -1.0, 15, COL_TEXT)
