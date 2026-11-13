class_name DebugOverlay
extends PanelContainer
## M1.11 debug overlay: fps, ms/tick, counts, band, sun bearing, shade intervals,
## income/s, taps/min, % time lit. Toggled with backtick or the HUD "DBG" button.

const PHASE_WAVE: int = 1
const REFRESH_SECONDS: float = 0.2

var sim: Node
var bands: BandController
var router: InputRouter

var _label: Label
var _copy: Button
var _timer: float = 0.0
var _wave_seconds: float = 0.0
var _lit_seconds: float = 0.0


func _init() -> void:
	add_theme_stylebox_override(&"panel", UiKit.panel_style(Color(0.0, 0.0, 0.0, 0.72)))
	visible = false
	var box: VBoxContainer = VBoxContainer.new()
	add_child(box)
	_label = UiKit.label("", 16, Color(0.75, 1.0, 0.8))
	box.add_child(_label)
	_copy = UiKit.button("Copy run log", Vector2(180.0, 52.0))
	_copy.add_theme_font_size_override(&"font_size", 18)
	_copy.pressed.connect(_on_copy)
	box.add_child(_copy)


func _process(delta: float) -> void:
	if sim == null:
		return
	# % time lit is integrated even while hidden so it covers the whole run.
	if int(sim.get_phase()) == PHASE_WAVE:
		var stats: Dictionary = sim.get_debug_stats()
		var scaled: float = delta * float(sim.get_time_scale())
		_wave_seconds += scaled
		_lit_seconds += scaled * float(stats.get("lit_fraction", 0.0))
	if not visible:
		return
	_timer -= delta
	if _timer <= 0.0:
		_timer = REFRESH_SECONDS
		_label.text = build_text()


func reset_stats() -> void:
	_wave_seconds = 0.0
	_lit_seconds = 0.0


func percent_time_lit() -> float:
	return 100.0 * _lit_seconds / _wave_seconds if _wave_seconds > 0.0 else 0.0


func build_text() -> String:
	var stats: Dictionary = sim.get_debug_stats()
	var shade: PackedFloat32Array = sim.get_shade_intervals()
	var shade_text: PackedStringArray = PackedStringArray()
	var i: int = 0
	while i + 1 < shade.size():
		shade_text.append("[%d..%d]" % [
			roundi(rad_to_deg(shade[i])), roundi(rad_to_deg(shade[i + 1]))])
		i += 2
	var lines: PackedStringArray = PackedStringArray([
		"%s%s" % [BuildInfo.label(), "  MOCK SIM" if bool(stats.get("mock", false)) else ""],
		"fps %d   ms/tick %.3f   tick %d" % [
			int(Engine.get_frames_per_second()), float(stats.get("ms_per_tick", 0.0)),
			int(sim.get_tick())],
		"enemies %d   projectiles %d" % [
			int(stats.get("enemies", sim.get_enemy_count())),
			int(stats.get("projectiles", sim.get_projectile_count()))],
		"band %s   time x%.2f" % [
			bands.band_name() if bands != null else "?", float(sim.get_time_scale())],
		"sun %d deg   beam x%.2f" % [
			roundi(wrapf(rad_to_deg(float(sim.get_sun_bearing())), 0.0, 360.0)),
			float(sim.get_beam_strength())],
		"shade %s" % (" ".join(shade_text) if not shade_text.is_empty() else "none"),
		"income %.1f/s   lit now %d%%" % [
			float(sim.get_income_per_second()), roundi(float(stats.get("lit_fraction", 0.0)) * 100.0)],
		"taps/min %.1f   time lit %d%%" % [
			router.taps_per_minute() if router != null else 0.0, roundi(percent_time_lit())],
	])
	return "\n".join(lines)


func _on_copy() -> void:
	var log_bytes: PackedByteArray = sim.get_command_log()
	DisplayServer.clipboard_set(Marshalls.raw_to_base64(log_bytes))
