class_name PlanetPicker
extends ColorRect
## M2.8 pre-run home planet picker: three big cards (smallest planet first) with
## the four stats size drives: rotation speed, HP, slots and lit fraction.
## Touch / click: tap a card to select it, tap it again or press Start to play.
## Keyboard: 1 / 2 / 3 (or Left / Right) select, Enter / Space starts.
## Built in code; shown by Game before a run and after Restart.

signal planet_chosen(planet_id: String)

const CARD_SIZE: Vector2 = Vector2(300.0, 380.0)
const SELECTED_BORDER: Color = Color(1.0, 0.86, 0.45)

var _defs: Array[Dictionary] = []
var _cards: Array[Button] = []
var _selected: int = -1
var _start: Button
var _row: HBoxContainer
var _max: Dictionary = {}


func _init() -> void:
	name = "PlanetPicker"
	color = Color(0.0, 0.0, 0.03, 0.78)
	visible = false
	mouse_filter = Control.MOUSE_FILTER_STOP
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var centre: CenterContainer = CenterContainer.new()
	centre.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	centre.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(centre)
	var box: VBoxContainer = VBoxContainer.new()
	box.alignment = BoxContainer.ALIGNMENT_CENTER
	box.add_theme_constant_override(&"separation", 18)
	centre.add_child(box)
	var title: Label = UiKit.label("Choose your home planet", 40)
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(title)
	_row = HBoxContainer.new()
	_row.alignment = BoxContainer.ALIGNMENT_CENTER
	_row.add_theme_constant_override(&"separation", 22)
	box.add_child(_row)
	var hint: Label = UiKit.label("Tap a planet, or press 1 / 2 / 3 then Enter", 20, Palette.TEXT_DIM)
	hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(hint)
	_start = UiKit.button("START", Vector2(300.0, 80.0), Color(0.14, 0.46, 0.26))
	_start.name = "StartRun"
	_start.add_theme_font_size_override(&"font_size", 30)
	_start.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	_start.pressed.connect(confirm)
	box.add_child(_start)


## Builds the cards from SimServer.get_planet_defs(); planets that cannot host the
## run are left out. `current` (an id) is preselected, else the default planet.
func setup(defs: Array[Dictionary], current: String = "") -> void:
	_defs = []
	for def: Dictionary in defs:
		if bool(def.get("fits", true)):
			_defs.append(def)
	_defs.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		return float(a.get("radius", 0.0)) < float(b.get("radius", 0.0)))
	_max = {"rot": 0.0, "hp": 0.0, "slots": 0.0, "lit": 0.0}
	for def: Dictionary in _defs:
		_max["rot"] = maxf(float(_max["rot"]), float(def.get("rot_max_speed", 0.0)))
		_max["hp"] = maxf(float(_max["hp"]), float(def.get("hp", 0.0)))
		_max["slots"] = maxf(float(_max["slots"]), float(def.get("slots", 0)))
		_max["lit"] = maxf(float(_max["lit"]), float(def.get("lit_fraction", 0.0)))
	for card: Button in _cards:
		card.queue_free()
	_cards = []
	var max_radius: float = 1.0
	for def: Dictionary in _defs:
		max_radius = maxf(max_radius, float(def.get("radius", 1.0)))
	for i: int in _defs.size():
		var card: Button = _make_card(i, _defs[i], max_radius)
		_row.add_child(card)
		_cards.append(card)
	var pick: int = -1
	for i: int in _defs.size():
		var id: String = str(_defs[i].get("id", ""))
		if (current != "" and id == current) or (pick < 0 and current == "" and bool(_defs[i].get("default", false))):
			pick = i
	select(maxi(pick, 0) if not _defs.is_empty() else -1)


func open(defs: Array[Dictionary], current: String = "") -> void:
	setup(defs, current)
	visible = true


func close() -> void:
	visible = false


func planet_count() -> int:
	return _defs.size()


func selected_index() -> int:
	return _selected


func selected_id() -> String:
	return str(_defs[_selected].get("id", "")) if _selected >= 0 and _selected < _defs.size() else ""


func card(index: int) -> Button:
	return _cards[index]


func start_button() -> Button:
	return _start


## Card i in display order (smallest first).
func select(index: int) -> void:
	if index < 0 or index >= _cards.size():
		return
	_selected = index
	for i: int in _cards.size():
		_style_card(_cards[i], i == _selected)


## Plays the selected planet.
func confirm() -> void:
	if not visible or _selected < 0:
		return
	var id: String = selected_id()
	close()
	planet_chosen.emit(id)


func _input(event: InputEvent) -> void:
	if not visible:
		return
	var key: InputEventKey = event as InputEventKey
	if key == null or not key.pressed or key.echo:
		return
	var code: int = key.physical_keycode if key.physical_keycode != KEY_NONE else key.keycode
	var handled: bool = true
	match code:
		KEY_1, KEY_KP_1:
			select(0)
		KEY_2, KEY_KP_2:
			select(1)
		KEY_3, KEY_KP_3:
			select(2)
		KEY_LEFT:
			select(maxi(_selected - 1, 0))
		KEY_RIGHT:
			select(mini(_selected + 1, _cards.size() - 1))
		KEY_ENTER, KEY_KP_ENTER, KEY_SPACE:
			confirm()
		_:
			handled = false
	if handled and is_inside_tree():
		get_viewport().set_input_as_handled()


func _on_card_pressed(index: int) -> void:
	# First tap selects, a second tap on the selected card plays it.
	if index == _selected:
		confirm()
	else:
		select(index)


func _make_card(index: int, def: Dictionary, max_radius: float) -> Button:
	var b: Button = Button.new()
	b.name = "Planet_%s" % str(def.get("id", index))
	b.custom_minimum_size = CARD_SIZE
	b.focus_mode = Control.FOCUS_NONE
	b.pressed.connect(_on_card_pressed.bind(index))
	var box: VBoxContainer = VBoxContainer.new()
	box.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	box.offset_left = 16.0
	box.offset_right = -16.0
	box.offset_top = 12.0
	box.offset_bottom = -12.0
	box.add_theme_constant_override(&"separation", 6)
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	b.add_child(box)
	var head: Label = UiKit.label("%d  %s" % [index + 1, str(def.get("name", def.get("id", "?")))], 30)
	head.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(head)
	var disc: PlanetDisc = PlanetDisc.new()
	disc.custom_minimum_size = Vector2(0.0, 130.0)
	disc.radius_frac = float(def.get("radius", 1.0)) / max_radius
	disc.lit_arc = float(def.get("lit_arc", 0.0))
	disc.slots = int(def.get("slots", 0))
	disc.body = Palette.NAMED.get(str(def.get("color", "")), Palette.PLANET)
	box.add_child(disc)
	_add_stat(box, "Rotation", "%d°/s" % roundi(rad_to_deg(float(def.get("rot_max_speed", 0.0)))),
		float(def.get("rot_max_speed", 0.0)) / maxf(float(_max["rot"]), 0.001))
	_add_stat(box, "HP", "%d" % roundi(float(def.get("hp", 0.0))),
		float(def.get("hp", 0.0)) / maxf(float(_max["hp"]), 0.001))
	_add_stat(box, "Slots", "%d" % int(def.get("slots", 0)),
		float(def.get("slots", 0)) / maxf(float(_max["slots"]), 0.001))
	_add_stat(box, "Lit", "%d%%" % roundi(100.0 * float(def.get("lit_fraction", 0.0))),
		float(def.get("lit_fraction", 0.0)) / maxf(float(_max["lit"]), 0.001))
	return b


func _add_stat(box: VBoxContainer, label: String, value: String, frac: float) -> void:
	var row: HBoxContainer = HBoxContainer.new()
	row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var name_label: Label = UiKit.label(label, 20, Palette.TEXT_DIM)
	name_label.custom_minimum_size = Vector2(92.0, 0.0)
	row.add_child(name_label)
	var bar: StatBar = StatBar.new()
	bar.frac = frac
	bar.custom_minimum_size = Vector2(0.0, 22.0)
	bar.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	row.add_child(bar)
	var value_label: Label = UiKit.label(value, 22)
	value_label.custom_minimum_size = Vector2(70.0, 0.0)
	value_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	row.add_child(value_label)
	box.add_child(row)


func _style_card(b: Button, selected: bool) -> void:
	for state: StringName in [&"normal", &"hover", &"pressed", &"focus"]:
		var style: StyleBoxFlat = UiKit.panel_style(Color(0.10, 0.13, 0.22) if not selected else Color(0.16, 0.20, 0.32))
		if state == &"hover":
			style.bg_color = style.bg_color.lightened(0.08)
		if selected:
			style.border_color = SELECTED_BORDER
			style.set_border_width_all(4)
		b.add_theme_stylebox_override(state, style)


## A planet preview: disc scaled by size, the lit arc in the sun's colour, slot ticks.
class PlanetDisc:
	extends Control
	var radius_frac: float = 1.0
	var lit_arc: float = 0.0
	var slots: int = 0
	var body: Color = Palette.PLANET

	func _init() -> void:
		mouse_filter = Control.MOUSE_FILTER_IGNORE

	func _draw() -> void:
		var c: Vector2 = size * 0.5
		var r: float = minf(size.x, size.y) * 0.42 * radius_frac
		draw_circle(c, r, body)
		# The sun shines from the right: its lit arc is centred on angle 0.
		draw_arc(c, r - 3.0, -lit_arc * 0.5, lit_arc * 0.5, 32, Palette.LIT, 6.0, true)
		draw_arc(c, r, 0.0, TAU, 64, Palette.PLANET_RIM, 1.5, true)
		for i: int in slots:
			var dir: Vector2 = Vector2.from_angle(TAU * float(i) / float(slots))
			draw_line(c + dir * (r + 3.0), c + dir * (r + 8.0), Palette.UNLIT, 1.5, true)


## Relative bar for one stat (1 = the best planet on that stat).
class StatBar:
	extends Control
	var frac: float = 0.0

	func _init() -> void:
		mouse_filter = Control.MOUSE_FILTER_IGNORE

	func _draw() -> void:
		var h: float = minf(size.y, 12.0)
		var y: float = (size.y - h) * 0.5
		draw_rect(Rect2(0.0, y, size.x, h), Color(1.0, 1.0, 1.0, 0.08))
		draw_rect(Rect2(0.0, y, size.x * clampf(frac, 0.0, 1.0), h), Palette.TEXT_DIM)
