class_name BuildMenu
extends PanelContainer
## M1.4: build menu for the selected slot. Empty slot: one big button per building
## (tap = preview the placement ghost, tap again or "Build" = place). Occupied slot:
## name, light state and Sell. The menu only emits intents; Game sends the commands.

signal preview_changed(building: int)
signal build_requested(building: int)
signal sell_requested
## M2.1: "Build above" on a ground building whose elevated slot is free.
signal build_above_requested
## M2.2: buy the next tier of an upgrade path of the selected building.
signal upgrade_requested(path: int)
## M2.6: fire the active ability (Sun Lens Overcharge) of the selected building.
signal activate_requested
## M2.11: lean the mount one step (-1 left / +1 right): the ghost's mount on an
## empty slot (free), or a re-aim of the selected tower (priced, between waves).
signal aim_step_requested(direction: int)
signal closed

## One column keeps the menu as narrow as in M1, so the Build view still shows
## the whole planet ring beside it; the list scrolls instead.
const LIST_COLUMNS: int = 1
const BUTTON_WIDTH: float = 230.0
const BUTTON_HEIGHT: float = 60.0
## Height of the building list before it scrolls (canvas px of the 1280x720 base).
const LIST_MAX_HEIGHT: float = 320.0

var slot: int = -1
## Layer the menu is open for: 0 ground, 1 elevated (M2.1).
var layer: int = 0
var previewed: int = -1

var _box: VBoxContainer
var _title: Label
var _info: Label
## M2.6: the building buttons in a scroll box, so a long roster
## still fits a landscape phone (touch drag, mouse wheel; Left/Right or [ ]
## pick, Enter builds).
var _list: GridContainer
var _scroll: ScrollContainer
## M2.6: active ability button (Overcharge), shown for buildings that have one.
var _active: Button
var _active_name: String = ""
var _active_key_text: String = ""
var _build: Button
## M2.11 mount row: < "Aim +30°  re-aim 6" >
var _aim_row: HBoxContainer
var _aim_left: Button
var _aim_right: Button
var _aim_label: Label
var _sell: Button
var _above: Button
## M2.2: upgrade paths of a built tower (Build view only).
var upgrades: UpgradePanel
var _upgrades_allowed: bool = true
var _buttons: Dictionary[int, Button] = {}
var _costs: Dictionary[int, float] = {}
var _names: Dictionary[int, String] = {}
var _stardust: float = 0.0


func _init() -> void:
	add_theme_stylebox_override(&"panel", UiKit.panel_style())
	visible = false
	_box = VBoxContainer.new()
	_box.add_theme_constant_override(&"separation", 8)
	add_child(_box)
	var head: HBoxContainer = HBoxContainer.new()
	_box.add_child(head)
	_title = UiKit.label("Slot", 24)
	_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(_title)
	var close_button: Button = UiKit.button("X", Vector2(56.0, 56.0), Color(0.30, 0.16, 0.18))
	close_button.name = "Close"
	close_button.pressed.connect(close)
	head.add_child(close_button)
	_info = UiKit.label("", 18, Palette.TEXT_DIM)
	_box.add_child(_info)
	_scroll = ScrollContainer.new()
	_scroll.name = "BuildingScroll"
	_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
	_box.add_child(_scroll)
	_list = GridContainer.new()
	_list.name = "BuildingList"
	_list.columns = LIST_COLUMNS
	_list.add_theme_constant_override(&"h_separation", 6)
	_list.add_theme_constant_override(&"v_separation", 6)
	_scroll.add_child(_list)
	# M2.11: touch the arrows, or , and . with a keyboard.
	_aim_row = HBoxContainer.new()
	_aim_row.name = "AimRow"
	_aim_row.visible = false
	_aim_row.add_theme_constant_override(&"separation", 6)
	_aim_left = UiKit.button("<", Vector2(56.0, 56.0), Color(0.20, 0.22, 0.34))
	_aim_left.name = "AimLeft"
	_aim_left.pressed.connect(func() -> void: aim_step_requested.emit(-1))
	_aim_row.add_child(_aim_left)
	_aim_label = UiKit.label("Aim 0°", 18)
	_aim_label.name = "AimLabel"
	_aim_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_aim_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_aim_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_aim_row.add_child(_aim_label)
	_aim_right = UiKit.button(">", Vector2(56.0, 56.0), Color(0.20, 0.22, 0.34))
	_aim_right.name = "AimRight"
	_aim_right.pressed.connect(func() -> void: aim_step_requested.emit(1))
	_aim_row.add_child(_aim_right)
	_box.add_child(_aim_row)
	_build = UiKit.button("Build", Vector2(230.0, 68.0), Color(0.14, 0.42, 0.24))
	_build.name = "Build"
	_build.pressed.connect(_on_build_pressed)
	_box.add_child(_build)
	# Keyboard path: W/Up (InputRouter); the key is named where there is a keyboard.
	var phone: bool = OS.has_feature("mobile") or OS.has_feature("web_android") \
		or OS.has_feature("web_ios")
	var above_text: String = "Build above" if phone else "Build above (W)"
	_above = UiKit.button(above_text, Vector2(150.0, 62.0), Color(0.14, 0.36, 0.42))
	_above.name = "BuildAbove"
	_above.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_above.pressed.connect(_on_above_pressed)
	_active_key_text = "" if phone else "  (F)"
	_active = UiKit.button("Overcharge", Vector2(230.0, 62.0), Color(0.55, 0.42, 0.10))
	_active.name = "Active"
	_active.visible = false
	_active.pressed.connect(func() -> void: activate_requested.emit())
	_box.add_child(_active)
	upgrades = UpgradePanel.new()
	upgrades.name = "Upgrades"
	upgrades.visible = false
	upgrades.upgrade_requested.connect(func(path: int) -> void: upgrade_requested.emit(path))
	_box.add_child(upgrades)
	_sell = UiKit.button("Sell", Vector2(150.0, 62.0), Color(0.45, 0.20, 0.16))
	_sell.name = "Sell"
	_sell.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_sell.pressed.connect(_on_sell_pressed)
	# M2.1 + M2.2: "Build above" and "Sell" share one row under the upgrade paths,
	# so a built tower's menu fits a landscape phone.
	var actions: HBoxContainer = HBoxContainer.new()
	actions.name = "Actions"
	actions.add_theme_constant_override(&"separation", 8)
	actions.add_child(_above)
	actions.add_child(_sell)
	_box.add_child(actions)


## M2.10: info line of a shield: its live energy (from get_shields, when given)
## and how it recharges.
static func shield_note(def: Dictionary, state: Dictionary = {}) -> String:
	var energy: float = float(def.get("shield_energy", 0.0))
	var line: String = "Shield %d° wide, %d energy" % [
		roundi(rad_to_deg(float(def.get("shield_width", 0.0)))), roundi(energy)]
	if not state.is_empty():
		if bool(state.get("online", true)):
			line = "Shield up: %d / %d energy" % [roundi(float(state.get("energy", 0.0))), roundi(energy)]
		else:
			line = "Shield DOWN: recharging %d%%" % roundi(float(state.get("charge_frac", 0.0)) * 100.0)
	return line + "\nRecharges %ds after a break, faster while lit" % roundi(
		float(def.get("recharge_seconds", 0.0)))


func is_open() -> bool:
	return visible


## Opens the menu for an empty slot. `preferred` is previewed first (last used).
## M2.1: `for_layer` 1 lists only what may stand on the elevated ring and shows
## its range bonus; buildings wider than one slot say so.
func open_empty(
		for_slot: int, defs: Array[Dictionary], stardust: float, preferred: int = -1,
		for_layer: int = 0, range_bonus: float = 0.0
) -> void:
	slot = for_slot
	layer = for_layer
	_stardust = stardust
	_title.text = ("Build above slot %d" if for_layer > 0 else "Build on slot %d") % for_slot
	_info.text = "Tap to preview, tap again to build"
	if for_layer > 0:
		_info.text = "Elevated: %+d%% range\n%s" % [roundi(range_bonus * 100.0), _info.text]
	_clear_list()
	for def: Dictionary in defs:
		# Def "layer": 0 ground only, 1 elevated only, 2 either (absent = ground).
		var allowed: int = int(def.get("layer", 0))
		if allowed != 2 and allowed != for_layer:
			continue
		var index: int = int(def.get("index", _buttons.size()))
		var cost: float = float(def.get("cost", 0.0))
		var label: String = "%s   %d" % [str(def.get("name", def.get("id", "?"))), roundi(cost)]
		var footprint: int = int(def.get("footprint", 1))
		if footprint > 1:
			label += "  (%d slots)" % footprint
		var b: Button = UiKit.button(label, Vector2(BUTTON_WIDTH, BUTTON_HEIGHT))
		b.name = "Building%d" % index
		b.alignment = HORIZONTAL_ALIGNMENT_LEFT
		b.icon_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		var icon: ShapeIcon = ShapeIcon.new(
			Palette.shape_of(def.get("shape", 0)), Palette.color_of(def.get("color", Color.WHITE)), 34.0)
		b.add_child(icon)
		icon.anchor_left = 1.0
		icon.anchor_right = 1.0
		icon.anchor_top = 0.5
		icon.anchor_bottom = 0.5
		icon.offset_left = -48.0
		icon.offset_right = -14.0
		icon.offset_top = -17.0
		icon.offset_bottom = 17.0
		b.pressed.connect(_on_building_pressed.bind(index))
		_list.add_child(b)
		_buttons[index] = b
		_costs[index] = cost
		_names[index] = str(def.get("name", def.get("id", "?")))
	_list.visible = true
	_scroll.visible = true
	_fit_scroll()
	_build.visible = true
	_aim_row.visible = false  # M2.11: Game shows it for aim-mounted buildings
	_active.visible = false
	_sell.visible = false
	_above.visible = false
	upgrades.visible = false
	visible = true
	refit()
	previewed = -1
	if _buttons.has(preferred):
		_set_preview(preferred)
	elif not _buttons.is_empty():
		# M2.6: with nothing used yet, preview the first tower (not a support building).
		var first: int = _buttons.keys()[0]
		for def: Dictionary in defs:
			var index: int = int(def.get("index", -1))
			if _buttons.has(index) and str(def.get("kind", "")) == "tower":
				first = index
				break
		_set_preview(first)
	_refresh()


## Opens the info/sell view for an occupied slot.
## M2.1 `extra` (all optional): "layer" (1 = elevated), "can_build_above" (shows
## the Build above button), "carrying" (a building stands on it: Sell is off),
## "notes" (lines such as "+20% range (linked)").
func open_building(
		for_slot: int, def: Dictionary, lit: bool, shaded: bool, extra: Dictionary = {}
) -> void:
	slot = for_slot
	layer = int(extra.get("layer", 0))
	previewed = -1
	_clear_list()
	_title.text = str(def.get("name", def.get("id", "Building")))
	var light: String = "shaded" if shaded else ("in the beam" if lit else "in the dark")
	var where: String = "Above slot %d" if layer > 0 else "Slot %d"
	_info.text = "%s, %s   Cost %d" % [
		where % for_slot, light, roundi(float(def.get("cost", 0.0)))]
	var notes: Array = extra.get("notes", [])
	for note: Variant in notes:
		_info.text += "\n" + str(note)
	var carrying: bool = bool(extra.get("carrying", false))
	if carrying:
		_info.text += "\nCarries a building: sell that one first"
	_list.visible = false
	_scroll.visible = false
	_build.visible = false
	_aim_row.visible = false  # M2.11
	# M2.6: the active ability (tap the button, or F with a keyboard).
	var active: Dictionary = def.get("active", {})
	_active_name = str(active.get("name", ""))
	_active.visible = not active.is_empty()
	set_active_state(0.0, 0.0)
	_sell.visible = true
	# A foundation in use stays pressable: the sim refuses the sale and the HUD
	# toasts why; the button is only dimmed.
	_sell.disabled = false
	_sell.modulate = Color(1.0, 1.0, 1.0, 0.55) if carrying else Color.WHITE
	_above.visible = bool(extra.get("can_build_above", false))
	_sell.text = "Sell"
	upgrades.show_upgrades({})
	upgrades.visible = false
	visible = true
	refit()


## M2.2: fills the upgrade paths of the open building from `get_slot_upgrades`.
## `build_view` false (Watch view) hides them: upgrading is a Build-view action.
func show_upgrades(info: Dictionary, build_view: bool) -> void:
	if not visible or not _sell.visible:
		return
	_upgrades_allowed = build_view
	upgrades.show_upgrades(info)
	upgrades.visible = build_view and not info.is_empty()
	if info.has("sell_refund"):
		_sell.text = "Sell  +%d" % floori(float(info["sell_refund"]))
	refit()


## Shrinks the menu back to its content around the point the HUD pinned it to, so
## it grows away from the screen edge it hugs (`reset_size` would grow right/down).
func refit() -> void:
	var x: float = _pin_point(offset_left, offset_right, grow_horizontal)
	var y: float = _pin_point(offset_top, offset_bottom, grow_vertical)
	offset_left = x
	offset_right = x
	offset_top = y
	offset_bottom = y


static func _pin_point(begin: float, end: float, grow: Control.GrowDirection) -> float:
	match grow:
		Control.GROW_DIRECTION_BEGIN:
			return end
		Control.GROW_DIRECTION_END:
			return begin
	return (begin + end) * 0.5


func close() -> void:
	if not visible:
		return
	visible = false
	slot = -1
	previewed = -1
	closed.emit()


## Keeps the affordability state current while stardust ticks.
func set_stardust(value: float) -> void:
	if is_equal_approx(value, _stardust):
		return
	_stardust = value
	if visible:
		_refresh()


func can_afford(building: int) -> bool:
	return _costs.has(building) and _costs[building] <= _stardust + 0.001


## Button for a building index (tests).
func building_button(building: int) -> Button:
	return _buttons.get(building)


func build_button() -> Button:
	return _build


func sell_button() -> Button:
	return _sell


func above_button() -> Button:
	return _above


func info_text() -> String:
	return _info.text


## M2.6: refreshes the active button: `cooldown` seconds left before it can
## fire again, `running` seconds left of the ability itself.
func set_active_state(cooldown: float, running: float) -> void:
	if not _active.visible:
		return
	if running > 0.0:
		_active.text = "%s  %.0fs" % [_active_name, ceilf(running)]
		_active.disabled = true
	elif cooldown > 0.0:
		_active.text = "%s in %.0fs" % [_active_name, ceilf(cooldown)]
		_active.disabled = true
	else:
		_active.text = _active_name + _active_key_text
		_active.disabled = false


func active_button() -> Button:
	return _active


## M2.11: shows the mount row with `text`; `enabled` false greys the arrows
## (re-aiming mid-wave). Hidden when `text` is empty.
func show_aim(text: String, enabled: bool) -> void:
	_aim_row.visible = text != ""
	_aim_label.text = text
	_aim_left.disabled = not enabled
	_aim_right.disabled = not enabled
	refit()


func aim_row_visible() -> bool:
	return _aim_row.visible


func aim_text() -> String:
	return _aim_label.text


func aim_button(direction: int) -> Button:
	return _aim_right if direction > 0 else _aim_left


## M2.6: previews the next (+1) or previous (-1) building of the list.
func step_preview(direction: int) -> void:
	var keys: Array[int] = []
	for child: Node in _list.get_children():
		for index: int in _buttons:
			if _buttons[index] == child:
				keys.append(index)
	if keys.is_empty():
		return
	var at: int = keys.find(previewed)
	var next: int = keys[posmod(at + direction, keys.size())] if at >= 0 else keys[0]
	_set_preview(next)
	_scroll.ensure_control_visible(_buttons[next])


## M2.6 keyboard path: Left/Right or [ ] pick a building, Enter builds; F fires
## the selected building's active.
func _unhandled_key_input(event: InputEvent) -> void:
	if not visible:
		return
	var key: InputEventKey = event as InputEventKey
	if key == null or not key.pressed or key.echo:
		return
	var code: int = key.physical_keycode if key.physical_keycode != KEY_NONE else key.keycode
	# M2.11 keyboard path for the mount row: , leans left, . leans right.
	if _aim_row.visible and (code == KEY_COMMA or code == KEY_PERIOD):
		if not _aim_left.disabled:
			aim_step_requested.emit(-1 if code == KEY_COMMA else 1)
		get_viewport().set_input_as_handled()
		return
	if _scroll.visible:
		match code:
			KEY_RIGHT, KEY_BRACKETRIGHT, KEY_TAB:
				step_preview(-1 if key.shift_pressed else 1)
				get_viewport().set_input_as_handled()
			KEY_LEFT, KEY_BRACKETLEFT:
				step_preview(-1)
				get_viewport().set_input_as_handled()
			KEY_ENTER, KEY_KP_ENTER:
				_on_build_pressed()
				get_viewport().set_input_as_handled()
	elif code == KEY_F and _active.visible and not _active.disabled:
		activate_requested.emit()
		get_viewport().set_input_as_handled()


## Caps the list's height so the menu fits a landscape phone; the rest scrolls.
func _fit_scroll() -> void:
	var rows: int = ceili(float(_buttons.size()) / float(LIST_COLUMNS))
	var full: float = rows * (BUTTON_HEIGHT + 6.0)
	_scroll.custom_minimum_size = Vector2(
		LIST_COLUMNS * (BUTTON_WIDTH + 6.0), minf(full, LIST_MAX_HEIGHT))


func _clear_list() -> void:
	for child: Node in _list.get_children():
		_list.remove_child(child)
		child.queue_free()
	_buttons.clear()
	_costs.clear()
	_names.clear()


func _set_preview(building: int) -> void:
	previewed = building
	preview_changed.emit(building)
	_refresh()


func _refresh() -> void:
	for index: int in _buttons:
		var b: Button = _buttons[index]
		var affordable: bool = can_afford(index)
		b.add_theme_color_override(&"font_color", Palette.TEXT if affordable else Palette.THREAT)
		b.modulate = Color.WHITE if index == previewed else Color(0.72, 0.72, 0.78)
	if _build.visible:
		var ok: bool = can_afford(previewed)
		_build.disabled = not ok
		if previewed < 0:
			_build.text = "Build"
		elif ok:
			_build.text = "Build %s" % _names.get(previewed, "")
		else:
			_build.text = "Need %d" % roundi(float(_costs.get(previewed, 0.0)))


func _on_building_pressed(building: int) -> void:
	if building == previewed:
		_on_build_pressed()
	else:
		_set_preview(building)


func _on_build_pressed() -> void:
	if previewed >= 0 and can_afford(previewed):
		build_requested.emit(previewed)


func _on_sell_pressed() -> void:
	sell_requested.emit()


func _on_above_pressed() -> void:
	build_above_requested.emit()
