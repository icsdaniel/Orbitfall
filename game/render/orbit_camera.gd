class_name OrbitCamera
extends Camera2D
## M0.7 camera: continuous zoom from min_zoom to max_zoom, eased exponentially in
## log space toward a target, around a screen focus point (mouse or pinch centre).
## Input code drives it through zoom_by() and set_zoom_target(); wheel, magnify
## gesture and two-finger pinch are handled here when handle_input is on.

signal zoom_changed(z: float)

@export var min_zoom: float = 0.05
@export var max_zoom: float = 20.0
## Exponential easing rate (1/s) in log-zoom space. Higher is snappier.
@export var zoom_rate: float = 12.0
## Zoom factor per mouse wheel notch.
@export var wheel_step: float = 1.2
## Handle wheel, magnify gesture and two-finger pinch in _unhandled_input.
@export var handle_input: bool = true

const _SNAP_LOG_EPSILON: float = 0.0005

var _zoom_value: float = 1.0
var _zoom_target: float = 1.0
var _has_focus: bool = false
var _focus_screen: Vector2 = Vector2.ZERO
var _touches: Dictionary[int, Vector2] = {}


func _ready() -> void:
	_zoom_value = clampf(zoom.x, min_zoom, max_zoom)
	_zoom_target = _zoom_value
	zoom = Vector2(_zoom_value, _zoom_value)


func _process(delta: float) -> void:
	advance(delta)


## Sets the zoom to ease toward (clamped), zooming around the screen centre.
func set_zoom_target(z: float) -> void:
	_zoom_target = clampf(z, min_zoom, max_zoom)
	_has_focus = false


## Multiplies the target zoom by `factor`, keeping the world point under
## `screen_focus` (viewport coordinates) fixed while the zoom eases.
func zoom_by(factor: float, screen_focus: Vector2) -> void:
	if factor <= 0.0:
		return
	_zoom_target = clampf(_zoom_target * factor, min_zoom, max_zoom)
	_has_focus = true
	_focus_screen = screen_focus


## Jumps straight to a zoom (clamped) with no easing.
func snap_zoom(z: float) -> void:
	set_zoom_target(z)
	_apply_zoom(_zoom_target)


func get_zoom_target() -> float:
	return _zoom_target


func get_zoom_value() -> float:
	return _zoom_value


func is_zooming() -> bool:
	return _zoom_value != _zoom_target


## World position under a viewport position at the current zoom.
func screen_to_world(screen: Vector2) -> Vector2:
	return global_position + (screen - _viewport_centre()) / _zoom_value


## One easing step; called from _process, public so tests and replays can step it.
func advance(delta: float) -> void:
	if _zoom_value == _zoom_target:
		return
	var log_target: float = log(_zoom_target)
	var t: float = 1.0 - exp(-zoom_rate * maxf(delta, 0.0))
	var log_zoom: float = lerpf(log(_zoom_value), log_target, t)
	if absf(log_zoom - log_target) < _SNAP_LOG_EPSILON:
		_apply_zoom(_zoom_target)
	else:
		_apply_zoom(exp(log_zoom))


func _unhandled_input(event: InputEvent) -> void:
	if not handle_input:
		return
	if event is InputEventMouseButton:
		var button: InputEventMouseButton = event
		if button.pressed and button.button_index == MOUSE_BUTTON_WHEEL_UP:
			zoom_by(pow(wheel_step, _wheel_amount(button)), button.position)
		elif button.pressed and button.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			zoom_by(pow(wheel_step, -_wheel_amount(button)), button.position)
	elif event is InputEventMagnifyGesture:
		var magnify: InputEventMagnifyGesture = event
		zoom_by(magnify.factor, magnify.position)
	elif event is InputEventScreenTouch:
		var touch: InputEventScreenTouch = event
		if touch.pressed:
			_touches[touch.index] = touch.position
		else:
			_touches.erase(touch.index)
	elif event is InputEventScreenDrag:
		var drag: InputEventScreenDrag = event
		_on_drag(drag.index, drag.position)


func _on_drag(index: int, pos: Vector2) -> void:
	if not _touches.has(index):
		return
	if _touches.size() != 2:
		_touches[index] = pos
		return
	var points: Array[Vector2] = _touches.values()
	var old_centre: Vector2 = (points[0] + points[1]) * 0.5
	var old_distance: float = points[0].distance_to(points[1])
	_touches[index] = pos
	points = _touches.values()
	var new_centre: Vector2 = (points[0] + points[1]) * 0.5
	var new_distance: float = points[0].distance_to(points[1])
	# Two-finger pan, then pinch around the new centre. Pinch follows the fingers
	# directly, so it skips the easing.
	global_position -= (new_centre - old_centre) / _zoom_value
	if old_distance > 1.0 and new_distance > 1.0:
		zoom_by(new_distance / old_distance, new_centre)
		_apply_zoom(_zoom_target)


func _wheel_amount(button: InputEventMouseButton) -> float:
	return button.factor if button.factor > 0.0 else 1.0


func _viewport_centre() -> Vector2:
	if not is_inside_tree():
		return Vector2.ZERO
	return get_viewport_rect().size * 0.5


func _apply_zoom(z: float) -> void:
	if z == _zoom_value:
		return
	if _has_focus:
		var offset: Vector2 = _focus_screen - _viewport_centre()
		global_position += offset / _zoom_value - offset / z
	_zoom_value = z
	zoom = Vector2(z, z)
	zoom_changed.emit(z)
