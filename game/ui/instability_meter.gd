class_name InstabilityMeter
extends Control
## M2.7: the drills' instability at a glance, under the HP bar (no hover needed).
## A bar whose fill is the global instability level, with a tick at the overheat
## threshold (the bar's middle): blue while calm, warm near the threshold, red
## above it (drills heating). The line under it says how many drills run, how
## many are overheated and what they earn. Hidden until a drill or cooler stands.

const BAR_HEIGHT: float = 14.0
const WIDTH: float = 330.0

var level: float = 0.0
var threshold: float = 1.0
var drills: int = 0
var running: int = 0
var coolers: int = 0
var drill_income: float = 0.0

var _label: Label


func _init() -> void:
	custom_minimum_size = Vector2(WIDTH, BAR_HEIGHT + 26.0)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	visible = false
	_label = UiKit.label("", 18, Palette.TEXT_DIM)
	_label.position = Vector2(0.0, BAR_HEIGHT + 2.0)
	add_child(_label)


## `info` is SimServer.get_instability().
func show_info(info: Dictionary) -> void:
	level = float(info.get("level", 0.0))
	threshold = maxf(float(info.get("threshold", 1.0)), 0.001)
	drills = int(info.get("drills", 0))
	running = int(info.get("running", drills))
	coolers = int(info.get("coolers", 0))
	drill_income = float(info.get("drill_income", 0.0))
	visible = drills > 0 or coolers > 0
	var text: String = "Drills %d" % drills
	if running < drills:
		text += " (%d overheated)" % (drills - running)
	text += "   +%.1f/s" % drill_income
	if coolers > 0:
		text += "   Coolers %d" % coolers
	if level > threshold:
		text += "   HOT"
	if _label.text != text:
		_label.text = text
	_label.add_theme_color_override(&"font_color", Palette.DRILL_HOT if level > threshold else Palette.TEXT_DIM)
	queue_redraw()


## 0..1 fill of the bar: the threshold sits at the middle.
func fill_fraction() -> float:
	return clampf(level / (2.0 * threshold), 0.0, 1.0)


func bar_color() -> Color:
	if level > threshold:
		return Palette.DRILL_HOT
	if level > 0.75 * threshold:
		return Palette.SUN_WARM
	return Palette.COOL


func _draw() -> void:
	var w: float = size.x if size.x > 0.0 else WIDTH
	draw_rect(Rect2(0.0, 0.0, w, BAR_HEIGHT), Color(0.10, 0.12, 0.18), true)
	draw_rect(Rect2(0.0, 0.0, w * fill_fraction(), BAR_HEIGHT), bar_color(), true)
	var x: float = w * 0.5
	draw_line(Vector2(x, -2.0), Vector2(x, BAR_HEIGHT + 2.0), Palette.TEXT, 2.0)
