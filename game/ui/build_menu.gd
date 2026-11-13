class_name BuildMenu
extends PanelContainer
## M1.4: build menu for the selected slot. Empty slot: one big button per building
## (tap = preview the placement ghost, tap again or "Build" = place). Occupied slot:
## name, light state and Sell. The menu only emits intents; Game sends the commands.

signal preview_changed(building: int)
signal build_requested(building: int)
signal sell_requested
signal closed

var slot: int = -1
var previewed: int = -1

var _box: VBoxContainer
var _title: Label
var _info: Label
var _list: VBoxContainer
var _build: Button
var _sell: Button
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
	_list = VBoxContainer.new()
	_list.add_theme_constant_override(&"separation", 6)
	_box.add_child(_list)
	_build = UiKit.button("Build", Vector2(230.0, 68.0), Color(0.14, 0.42, 0.24))
	_build.name = "Build"
	_build.pressed.connect(_on_build_pressed)
	_box.add_child(_build)
	_sell = UiKit.button("Sell", Vector2(230.0, 68.0), Color(0.45, 0.20, 0.16))
	_sell.name = "Sell"
	_sell.pressed.connect(_on_sell_pressed)
	_box.add_child(_sell)


func is_open() -> bool:
	return visible


## Opens the menu for an empty slot. `preferred` is previewed first (last used).
func open_empty(for_slot: int, defs: Array[Dictionary], stardust: float, preferred: int = -1) -> void:
	slot = for_slot
	_stardust = stardust
	_title.text = "Build on slot %d" % for_slot
	_info.text = "Tap to preview, tap again to build"
	_clear_list()
	for def: Dictionary in defs:
		var index: int = int(def.get("index", _buttons.size()))
		var cost: float = float(def.get("cost", 0.0))
		var b: Button = UiKit.button(
			"%s   %d" % [str(def.get("name", def.get("id", "?"))), roundi(cost)],
			Vector2(230.0, 62.0))
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
	_build.visible = true
	_sell.visible = false
	visible = true
	previewed = -1
	if _buttons.has(preferred):
		_set_preview(preferred)
	elif not _buttons.is_empty():
		_set_preview(_buttons.keys()[0])
	_refresh()


## Opens the info/sell view for an occupied slot.
func open_building(for_slot: int, def: Dictionary, lit: bool, shaded: bool) -> void:
	slot = for_slot
	previewed = -1
	_clear_list()
	_title.text = str(def.get("name", def.get("id", "Building")))
	var light: String = "shaded" if shaded else ("in the beam" if lit else "in the dark")
	_info.text = "Slot %d, %s\nCost %d" % [for_slot, light, roundi(float(def.get("cost", 0.0)))]
	_list.visible = false
	_build.visible = false
	_sell.visible = true
	visible = true


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
