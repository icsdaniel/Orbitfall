class_name UpgradePanel
extends VBoxContainer
## M2.2: the upgrade part of the build menu for a built tower. One row per upgrade
## path: path name and current tier, the stats diff of the next tier (before ->
## after, always visible: nothing relies on hover) and one big button with the
## tier's name and its stardust cost. Tap or click the button, or press 1 / 2.
## It only shows what `SimServer.get_slot_upgrades()` returned and emits intents.

signal upgrade_requested(path: int)

const STAT_NAMES: Dictionary[String, String] = {
	"damage": "Damage", "fire_rate": "Fire rate", "range": "Range", "arc": "Arc",
	"splash": "Blast", "income": "Income",
}
const STAT_ORDER: Array[String] = ["damage", "fire_rate", "range", "arc", "splash", "income"]
const PATH_KEYS: Array[Key] = [KEY_1, KEY_2]

var _stats: Label
var _rows: Array[VBoxContainer] = []
var _titles: Array[Label] = []
var _diffs: Array[Label] = []
var _buttons: Array[Button] = []
var _enabled: Array[bool] = []


func _init() -> void:
	add_theme_constant_override(&"separation", 6)
	_stats = UiKit.label("", 18, Palette.TEXT_DIM)
	_stats.name = "Stats"
	_stats.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_stats.custom_minimum_size.x = 300.0  # the button width: wraps instead of widening the menu
	add_child(_stats)


## Shows the upgrade info of one slot (`get_slot_upgrades`); an empty dictionary or a
## building without paths leaves only the stats line.
func show_upgrades(info: Dictionary) -> void:
	var paths: Array = info.get("paths", [])
	_stats.text = stats_text(info.get("stats", {}))
	_stats.visible = _stats.text != ""
	while _rows.size() < paths.size():
		_add_row()
	_enabled.resize(_rows.size())
	for i: int in _rows.size():
		_rows[i].visible = i < paths.size()
		_enabled[i] = false
		if i < paths.size():
			_show_path(i, paths[i])


func path_count() -> int:
	var n: int = 0
	for row: VBoxContainer in _rows:
		if row.visible:
			n += 1
	return n


## Widgets of one path (tests).
func upgrade_button(path: int) -> Button:
	return _buttons[path] if path >= 0 and path < _buttons.size() else null


func title_label(path: int) -> Label:
	return _titles[path] if path >= 0 and path < _titles.size() else null


func diff_label(path: int) -> Label:
	return _diffs[path] if path >= 0 and path < _diffs.size() else null


func stats_label() -> Label:
	return _stats


## "Damage 1   Fire rate 5/s   Range 350   Arc ±40°": the stats that are not zero.
static func stats_text(stats: Dictionary) -> String:
	var parts: PackedStringArray = PackedStringArray()
	for key: String in STAT_ORDER:
		if float(stats.get(key, 0.0)) > 0.0:
			parts.append("%s %s" % [STAT_NAMES[key], format_stat(key, float(stats[key]))])
	return "   ".join(parts)


## One line per changed stat: "Fire rate 5/s -> 6.5/s".
static func diff_text(changes: Array) -> String:
	var lines: PackedStringArray = PackedStringArray()
	for change: Dictionary in changes:
		var key: String = str(change.get("stat", ""))
		lines.append("%s %s -> %s" % [
			STAT_NAMES.get(key, key.capitalize()),
			format_stat(key, float(change.get("before", 0.0))),
			format_stat(key, float(change.get("after", 0.0)))])
	return "\n".join(lines)


static func format_stat(key: String, value: float) -> String:
	if key == "arc":
		return "±%s°" % _number(rad_to_deg(value))
	if key == "fire_rate" or key == "income":
		return "%s/s" % _number(value)
	return _number(value)


## Up to two decimals, without trailing zeros.
static func _number(value: float) -> String:
	var text: String = "%.2f" % value
	if text.contains("."):
		text = text.rstrip("0").rstrip(".")
	return text


func _unhandled_key_input(event: InputEvent) -> void:
	var key: InputEventKey = event as InputEventKey
	if key == null or not key.pressed or key.echo or not is_visible_in_tree():
		return
	var code: Key = key.physical_keycode if key.physical_keycode != KEY_NONE else key.keycode
	var path: int = PATH_KEYS.find(code)
	if path >= 0 and path < _enabled.size() and _enabled[path]:
		upgrade_requested.emit(path)
		get_viewport().set_input_as_handled()


func _add_row() -> void:
	var path: int = _rows.size()
	var row: VBoxContainer = VBoxContainer.new()
	row.name = "Path%d" % path
	row.add_theme_constant_override(&"separation", 2)
	var title: Label = UiKit.label("", 20)
	title.name = "Title"
	row.add_child(title)
	var diff: Label = UiKit.label("", 18, Palette.GOOD)
	diff.name = "Diff"
	row.add_child(diff)
	var button: Button = UiKit.button("", Vector2(300.0, 56.0), Color(0.16, 0.30, 0.44))
	button.name = "Upgrade"
	button.alignment = HORIZONTAL_ALIGNMENT_LEFT
	button.pressed.connect(_on_pressed.bind(path))
	row.add_child(button)
	add_child(row)
	_rows.append(row)
	_titles.append(title)
	_diffs.append(diff)
	_buttons.append(button)


func _show_path(i: int, path: Dictionary) -> void:
	var tier: int = int(path.get("tier", 0))
	var max_tier: int = int(path.get("max_tier", 0))
	var cost: int = roundi(float(path.get("cost", 0.0)))
	_titles[i].text = "%s   tier %d/%d" % [str(path.get("name", "Path")), tier, max_tier]
	var button: Button = _buttons[i]
	var diff: Label = _diffs[i]
	var can: bool = bool(path.get("can_upgrade", false))
	_enabled[i] = can
	button.disabled = not can
	diff.add_theme_color_override(&"font_color", Palette.GOOD if can else Palette.TEXT_DIM)
	button.add_theme_color_override(&"font_color", Palette.TEXT)
	button.add_theme_color_override(&"font_disabled_color", Palette.TEXT_DIM)
	if not bool(path.get("has_next", false)):
		diff.text = ""
		diff.visible = false
		button.text = "Top tier"
		return
	diff.text = diff_text(path.get("changes", []))
	diff.visible = diff.text != ""
	var next_name: String = str(path.get("next_name", "Upgrade"))
	if bool(path.get("locked", false)):
		button.text = "%s   locked: other path chosen" % next_name
	elif can:
		button.text = "[%d] %s   %d" % [i + 1, next_name, cost]
	else:
		button.text = "%s   need %d" % [next_name, cost]
		button.add_theme_color_override(&"font_disabled_color", Palette.THREAT)


func _on_pressed(path: int) -> void:
	if path < _enabled.size() and _enabled[path]:
		upgrade_requested.emit(path)
