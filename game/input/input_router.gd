class_name InputRouter
extends Node
## M1.2: turns touches, mouse and keys into SimServer commands and UI intents
## (BRIEF §4.1, landscape, two thumbs).
##  - left half: hold and drag horizontally -> rotate_input(v), released -> 0
##  - tap (anywhere, no drag) -> `tapped`; long-press -> `long_pressed`
##  - double-tap -> `double_tapped` (band toggle); pinch / wheel -> `zoom_requested`
##  - keys: A/Q and D/E rotate, Space start_wave, backtick -> `debug_toggled`
## Commands go straight to the sim; everything else is a signal for Game.

signal tapped(screen_pos: Vector2)
signal long_pressed(screen_pos: Vector2)
signal double_tapped(screen_pos: Vector2)
signal zoom_requested(factor: float, screen_pos: Vector2)
signal debug_toggled

const PHASE_INTERMISSION: int = 0

var sim: Node
## Returns true when a screen position is over interactive HUD (touch is ignored).
var ui_blocker: Callable = Callable()
## Used instead of the viewport size when set (tests).
var viewport_size_override: Vector2 = Vector2.ZERO

var drag_range_px: float = 90.0
var drag_dead_px: float = 10.0
var tap_max_px: float = 18.0
var double_tap_seconds: float = 0.3
var double_tap_px: float = 56.0
var long_press_seconds: float = 0.5
var wheel_step: float = 1.2

## Taps since the run started (debug overlay: taps/min).
var tap_count: int = 0

var _time: float = 0.0
var _touches: Dictionary[int, Vector2] = {}
var _primary: int = -1
var _start: Vector2 = Vector2.ZERO
var _start_time: float = 0.0
var _moved: bool = false
var _left_zone: bool = false
var _long_fired: bool = false
var _pinch_distance: float = 0.0
var _last_tap_time: float = -100.0
var _last_tap_pos: Vector2 = Vector2.ZERO
var _drag_value: float = 0.0
var _button_value: float = 0.0
var _keys: Dictionary[int, bool] = {}
var _sent_rotate: float = 0.0


func configure(config: ClientConfig) -> void:
	drag_range_px = config.num("input", "dragRangePx", drag_range_px)
	drag_dead_px = config.num("input", "dragDeadPx", drag_dead_px)
	tap_max_px = config.num("input", "tapMaxPx", tap_max_px)
	double_tap_seconds = config.num("input", "doubleTapSeconds", double_tap_seconds)
	double_tap_px = config.num("input", "doubleTapPx", double_tap_px)
	long_press_seconds = config.num("input", "longPressSeconds", long_press_seconds)
	wheel_step = config.num("input", "wheelStep", wheel_step)


func _process(delta: float) -> void:
	advance(delta)


func _unhandled_input(event: InputEvent) -> void:
	handle_event(event)


## Advances the gesture clock (long-press detection). Public so tests can step it.
func advance(delta: float) -> void:
	_time += delta
	if (_primary >= 0 and not _moved and not _long_fired and _touches.size() == 1
			and _time - _start_time >= long_press_seconds):
		_long_fired = true
		long_pressed.emit(_start)


## Entry point for every input event. Public so tests can feed events directly.
func handle_event(event: InputEvent) -> void:
	if event is InputEventScreenTouch:
		var touch: InputEventScreenTouch = event
		if touch.pressed:
			_touch_down(touch.index, touch.position)
		else:
			_touch_up(touch.index, touch.position)
	elif event is InputEventScreenDrag:
		var drag: InputEventScreenDrag = event
		_touch_move(drag.index, drag.position)
	elif event is InputEventMouseButton:
		var button: InputEventMouseButton = event
		if button.pressed and button.button_index == MOUSE_BUTTON_WHEEL_UP:
			zoom_requested.emit(wheel_step, button.position)
		elif button.pressed and button.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			zoom_requested.emit(1.0 / wheel_step, button.position)
	elif event is InputEventMagnifyGesture:
		var magnify: InputEventMagnifyGesture = event
		zoom_requested.emit(magnify.factor, magnify.position)
	elif event is InputEventKey:
		_key(event as InputEventKey)


## Held rotate buttons (HUD) feed the same rotate_input command. v in [-1, 1].
func set_button_rotation(v: float) -> void:
	_button_value = clampf(v, -1.0, 1.0)
	_send_rotate()


func taps_per_minute() -> float:
	return float(tap_count) * 60.0 / maxf(_time, 1.0)


func reset_stats() -> void:
	tap_count = 0
	_time = 0.0
	_start_time = 0.0
	_last_tap_time = -100.0


func _touch_down(index: int, pos: Vector2) -> void:
	if ui_blocker.is_valid() and bool(ui_blocker.call(pos)):
		return
	_touches[index] = pos
	if _touches.size() == 1:
		_primary = index
		_start = pos
		_start_time = _time
		_moved = false
		_long_fired = false
		_left_zone = pos.x < _viewport_size().x * 0.5
	else:
		# A second finger turns the gesture into a pinch.
		_primary = -1
		_set_drag(0.0)
		_pinch_distance = _pinch_span()


func _touch_move(index: int, pos: Vector2) -> void:
	if not _touches.has(index):
		return
	_touches[index] = pos
	if _touches.size() >= 2:
		var span: float = _pinch_span()
		if _pinch_distance > 1.0 and span > 1.0:
			zoom_requested.emit(span / _pinch_distance, _pinch_centre())
		_pinch_distance = span
		return
	if index != _primary:
		return
	var offset: Vector2 = pos - _start
	if offset.length() > tap_max_px:
		_moved = true
	if _left_zone and _moved:
		var magnitude: float = clampf(
			(absf(offset.x) - drag_dead_px) / maxf(drag_range_px, 1.0), 0.0, 1.0)
		_set_drag(magnitude * signf(offset.x))


func _touch_up(index: int, pos: Vector2) -> void:
	if not _touches.has(index):
		return
	_touches.erase(index)
	if index != _primary:
		if _touches.size() < 2:
			_pinch_distance = 0.0
		return
	_primary = -1
	_set_drag(0.0)
	if not _moved and not _long_fired:
		_tap(pos)


func _tap(pos: Vector2) -> void:
	tap_count += 1
	if (_time - _last_tap_time <= double_tap_seconds
			and pos.distance_to(_last_tap_pos) <= double_tap_px):
		_last_tap_time = -100.0
		double_tapped.emit(pos)
		return
	_last_tap_time = _time
	_last_tap_pos = pos
	tapped.emit(pos)


func _key(key: InputEventKey) -> void:
	if key.echo:
		return
	var code: int = key.physical_keycode if key.physical_keycode != KEY_NONE else key.keycode
	match code:
		KEY_A, KEY_Q, KEY_D, KEY_E:
			_keys[code] = key.pressed
			_send_rotate()
		KEY_SPACE:
			if key.pressed and sim != null and int(sim.get_phase()) == PHASE_INTERMISSION:
				sim.start_wave()
		KEY_QUOTELEFT:
			if key.pressed:
				debug_toggled.emit()


func _set_drag(v: float) -> void:
	_drag_value = v
	_send_rotate()


func _send_rotate() -> void:
	var keys: float = 0.0
	if _keys.get(KEY_D, false) or _keys.get(KEY_E, false):
		keys += 1.0
	if _keys.get(KEY_A, false) or _keys.get(KEY_Q, false):
		keys -= 1.0
	var v: float = clampf(_drag_value + _button_value + keys, -1.0, 1.0)
	if is_equal_approx(v, _sent_rotate):
		return
	_sent_rotate = v
	if sim != null:
		sim.rotate_input(v)


func _pinch_span() -> float:
	var points: Array[Vector2] = _touches.values()
	return points[0].distance_to(points[1]) if points.size() >= 2 else 0.0


func _pinch_centre() -> Vector2:
	var points: Array[Vector2] = _touches.values()
	return (points[0] + points[1]) * 0.5 if points.size() >= 2 else Vector2.ZERO


func _viewport_size() -> Vector2:
	if viewport_size_override != Vector2.ZERO:
		return viewport_size_override
	if is_inside_tree():
		return get_viewport().get_visible_rect().size
	return Vector2(1280.0, 720.0)
