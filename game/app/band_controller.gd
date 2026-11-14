class_name BandController
extends Node
## M1.10: Surface and Orbit zoom bands on the continuous OrbitCamera zoom, the
## cross-fade weight for surface-only detail, the double-tap toggle, and slow time:
## while a wave runs and the view is in the Surface band the sim is asked (as a
## command) for the planet's `surfaceTimeScale`, eased over the rules'
## `surfaceTimeEaseSeconds` (both read from the sim's content); otherwise normal speed.
## M1.12 (feel gate): the zoom is two views, not a free range. Surface is shown to
## the player as the Build view, Orbit as the Watch view (wide enough to see enemies
## from their spawn ring). The camera glides between them in `transitionSeconds`;
## a pinch follows the fingers and then settles on one of the two.

signal band_changed(band: int)

enum Band { SURFACE, ORBIT }
const PHASE_WAVE: int = 1
const BAND_NAMES: PackedStringArray = ["Surface", "Orbit"]
## What the player sees on the toggle button and in the hint.
const VIEW_NAMES: PackedStringArray = ["Build", "Watch"]

var sim: Node
var camera: OrbitCamera
## Used instead of the viewport size when set (tests).
var viewport_size_override: Vector2 = Vector2.ZERO

var surface_planet_frac: float = 0.36
var orbit_view_radius: float = 400.0
## The planet sits this many pixels below the screen centre in Surface (HUD room).
var surface_offset_px: float = 22.0
var fade_width: float = 0.35
var surface_time_scale: float = 0.75
var normal_time_scale: float = 1.0
var ease_seconds: float = 0.3
var transition_seconds: float = 0.45
## A pinch must change the zoom by this factor to settle on the other view.
var pinch_switch_factor: float = 1.15
## Go to Watch when a wave starts and back to Build when it is cleared.
var auto_switch: bool = true

var band: int = Band.SURFACE
## 1 in Surface, 0 in Orbit, smooth across the threshold.
var surface_weight: float = 1.0
var _sent_scale: float = -1.0
var _pinch_from: int = -1


func configure(config: ClientConfig) -> void:
	surface_planet_frac = config.num("bands", "surfacePlanetFrac", surface_planet_frac)
	orbit_view_radius = config.num("bands", "orbitViewRadius", orbit_view_radius)
	fade_width = config.num("bands", "fadeWidth", fade_width)
	surface_offset_px = config.num("bands", "surfaceOffsetPx", surface_offset_px)
	transition_seconds = config.num("bands", "transitionSeconds", transition_seconds)
	pinch_switch_factor = config.num("bands", "pinchSwitchFactor", pinch_switch_factor)
	auto_switch = config.num("bands", "autoSwitch", 1.0) > 0.5
	normal_time_scale = config.num("time", "normalTimeScale", normal_time_scale)
	refresh_time_settings()


## Slow-time numbers live in content/ and come through the sim (after new_run).
func refresh_time_settings() -> void:
	if sim != null and sim.has_method(&"get_surface_time_scale"):
		surface_time_scale = float(sim.get_surface_time_scale())
		ease_seconds = float(sim.get_surface_time_ease_seconds())


func _ready() -> void:
	if is_inside_tree():
		get_viewport().size_changed.connect(_on_viewport_resized)


func _process(_delta: float) -> void:
	update()


## Zoom at which the planet radius takes `surfacePlanetFrac` of the screen height.
func surface_zoom() -> float:
	var r: float = float(sim.get_planet_radius()) if sim != null else 100.0
	return _viewport_size().y * surface_planet_frac / maxf(r, 1.0)


## Zoom at which `orbitViewRadius` world units fit in half the screen height.
func orbit_zoom() -> float:
	return _viewport_size().y * 0.5 / maxf(orbit_view_radius, 1.0)


func threshold_zoom() -> float:
	return sqrt(surface_zoom() * orbit_zoom())


func band_name() -> String:
	return BAND_NAMES[band]


func view_name(target: int = -1) -> String:
	return VIEW_NAMES[band if target < 0 else target]


## Jumps (no easing) to a band's default zoom.
func snap_to(target: int) -> void:
	_apply_limits()
	camera.snap_zoom(surface_zoom() if target == Band.SURFACE else orbit_zoom())
	update()


func go_to(target: int) -> void:
	_apply_limits()
	_pinch_from = -1
	var z: float = surface_zoom() if target == Band.SURFACE else orbit_zoom()
	if not is_equal_approx(camera.get_zoom_target(), z):
		camera.glide_to(z, transition_seconds)
	update()


## Wheel notch / trackpad magnify: +1 goes to Build, -1 to Watch.
func step(direction: int) -> void:
	go_to(Band.SURFACE if direction > 0 else Band.ORBIT)


func toggle() -> void:
	go_to(Band.ORBIT if band == Band.SURFACE else Band.SURFACE)


## Live pinch around the planet (the camera stays centred on it); settle() ends it.
func zoom_by(factor: float) -> void:
	_apply_limits()
	if _pinch_from < 0:
		_pinch_from = band
	camera.set_zoom_now(camera.get_zoom_value() * factor)
	update()


## Pinch released: go to the other view if the pinch moved far enough toward it,
## otherwise glide back.
func settle() -> void:
	if _pinch_from < 0:
		return
	var z: float = camera.get_zoom_value()
	var target: int = _pinch_from
	if _pinch_from == Band.SURFACE and z < surface_zoom() / pinch_switch_factor:
		target = Band.ORBIT
	elif _pinch_from == Band.ORBIT and z > orbit_zoom() * pinch_switch_factor:
		target = Band.SURFACE
	go_to(target)


## Recomputes the band and fade and sends set_time_scale when the wanted scale
## changes. Called every frame; public so tests can drive it.
func update() -> void:
	if camera == null:
		return
	var threshold: float = threshold_zoom()
	var new_band: int = Band.SURFACE if camera.get_zoom_target() >= threshold else Band.ORBIT
	var t: float = log(maxf(camera.get_zoom_value(), 0.0001) / threshold) / maxf(fade_width, 0.01)
	surface_weight = smoothstep(-1.0, 1.0, t)
	camera.global_position = Vector2(
		0.0, -surface_offset_px * surface_weight / maxf(camera.get_zoom_value(), 0.0001))
	if new_band != band:
		band = new_band
		band_changed.emit(band)
	if sim == null:
		return
	var wanted: float = normal_time_scale
	if int(sim.get_phase()) == PHASE_WAVE and band == Band.SURFACE:
		wanted = surface_time_scale
	if not is_equal_approx(wanted, _sent_scale):
		_sent_scale = wanted
		sim.set_time_scale(wanted, ease_seconds)


## Forget what was sent (after new_run the sim is back at scale 1).
func reset_time_scale() -> void:
	_sent_scale = -1.0
	refresh_time_settings()


func _apply_limits() -> void:
	camera.min_zoom = orbit_zoom() * 0.8
	camera.max_zoom = surface_zoom() * 1.25


func _on_viewport_resized() -> void:
	if camera != null and sim != null:
		snap_to(band)


func _viewport_size() -> Vector2:
	if viewport_size_override != Vector2.ZERO:
		return viewport_size_override
	if is_inside_tree():
		return get_viewport().get_visible_rect().size
	return Vector2(1280.0, 720.0)
