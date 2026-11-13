class_name ForecastStrip
extends PanelContainer
## M1.9 / BRIEF §4.8: strip of upcoming waves from SimServer.get_forecast().
## Next wave exact (icons with counts, lane bearing, shadow-casters marked, light
## level as a sun icon fill); +2/+3 icons without counts; beyond: light level only.
## It shows exactly what get_forecast returns; it never guesses.


## One enemy group in a wave card: shape icon, count, lane bearing, caster mark.
class EntryIcon:
	extends Control
	var shape: int = 0
	var color: Color = Color.WHITE
	var count: int = -1
	var bearing: float = 0.0
	var shadow_caster: bool = false
	## Far waves: icon and lane arrow only (no count, no degrees).
	var compact: bool = false

	func _init() -> void:
		custom_minimum_size = Vector2(70.0, 56.0)
		mouse_filter = Control.MOUSE_FILTER_IGNORE

	func _draw() -> void:
		var font: Font = ThemeDB.fallback_font
		var centre: Vector2 = Vector2(18.0, 18.0)
		if shadow_caster:
			# Shadow-casters: violet halo and a dark shadow wedge under the icon.
			draw_circle(centre, 17.0, Color(Palette.SHADED, 0.55))
			draw_colored_polygon(PackedVector2Array([
				centre + Vector2(-9.0, 10.0), centre + Vector2(9.0, 10.0),
				centre + Vector2(14.0, 30.0), centre + Vector2(-14.0, 30.0)]),
				Color(0.0, 0.0, 0.0, 0.75))
		ShapeDraw.draw(self, shape, centre, 12.0, 0.0, color, 1.5)
		if count >= 0:
			draw_string(font, Vector2(36.0, 25.0), "x%d" % count,
				HORIZONTAL_ALIGNMENT_LEFT, -1.0, 20, Palette.TEXT)
		# Lane bearing: arrow flying toward the planet from that bearing, plus degrees.
		var dir: Vector2 = Vector2.from_angle(bearing)
		var c: Vector2 = Vector2(14.0, 45.0)
		draw_line(c + dir * 9.0, c - dir * 5.0, Palette.THREAT, 2.0, true)
		draw_colored_polygon(PackedVector2Array([
			c - dir * 9.0, c - dir * 2.0 + dir.orthogonal() * 4.5,
			c - dir * 2.0 - dir.orthogonal() * 4.5]), Palette.THREAT)
		if not compact:
			draw_string(font, Vector2(30.0, 51.0),
				"%d°" % roundi(wrapf(rad_to_deg(bearing), 0.0, 360.0)),
				HORIZONTAL_ALIGNMENT_LEFT, -1.0, 15, Palette.TEXT_DIM)


## Sun icon whose fill is the wave's light level.
class SunIcon:
	extends Control
	var light: float = 1.0

	func _init() -> void:
		custom_minimum_size = Vector2(30.0, 30.0)
		mouse_filter = Control.MOUSE_FILTER_IGNORE

	func _draw() -> void:
		var c: Vector2 = size * 0.5
		var r: float = 9.0
		for i: int in 8:
			var d: Vector2 = Vector2.from_angle(TAU * float(i) / 8.0)
			draw_line(c + d * (r + 2.0), c + d * (r + 5.5), Color(Palette.SUN_WARM, 0.8), 1.5, true)
		draw_circle(c, r, Color(0.16, 0.14, 0.10))
		var fill: float = clampf(light, 0.0, 1.0)
		if fill >= 0.999:
			draw_circle(c, r, Palette.SUN_WARM)
		elif fill > 0.0:
			var pts: PackedVector2Array = PackedVector2Array([c])
			for i: int in 25:
				pts.append(c + Vector2.from_angle(-PI * 0.5 + TAU * fill * float(i) / 24.0) * r)
			draw_colored_polygon(pts, Palette.SUN_WARM)
		draw_arc(c, r, 0.0, TAU, 24, Palette.SUN_WARM, 1.5, true)


var _row: HBoxContainer
var _signature: String = ""
var _enemy_defs: Array[Dictionary] = []
var _cards: int = 0
var _entries: int = 0


func _init() -> void:
	add_theme_stylebox_override(&"panel", UiKit.panel_style())
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	_row = HBoxContainer.new()
	_row.add_theme_constant_override(&"separation", 8)
	_row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_row)


func set_enemy_defs(defs: Array[Dictionary]) -> void:
	_enemy_defs = defs
	_signature = ""


## Number of wave cards currently shown.
func card_count() -> int:
	return _cards


## Number of enemy entry icons currently shown, over all cards.
func entry_count() -> int:
	return _entries


## Entry icons of one card (for tests).
func entries_of(card: int) -> Array[EntryIcon]:
	var out: Array[EntryIcon] = []
	if card < 0 or card >= _row.get_child_count():
		return out
	for node: Node in _row.get_child(card).find_children("*", "Control", true, false):
		if node is EntryIcon:
			out.append(node as EntryIcon)
	return out


## Rebuilds the strip when the forecast content changed.
func show_forecast(forecast: Array[Dictionary], in_wave: bool = false) -> void:
	var signature: String = _sign(forecast) + ("|now" if in_wave else "")
	if signature == _signature:
		return
	_signature = signature
	for child: Node in _row.get_children():
		_row.remove_child(child)
		child.queue_free()
	_cards = 0
	_entries = 0
	visible = not forecast.is_empty()
	for i: int in forecast.size():
		_row.add_child(_make_card(forecast[i], i == 0, in_wave))
		_cards += 1


func _make_card(wave: Dictionary, is_next: bool, in_wave: bool) -> Control:
	var card: PanelContainer = PanelContainer.new()
	card.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var bg: Color = Color(0.16, 0.20, 0.32, 0.95) if is_next else Color(0.10, 0.12, 0.20, 0.9)
	var style: StyleBoxFlat = UiKit.panel_style(bg)
	style.set_content_margin_all(6.0)
	if is_next:
		style.border_color = Color(Palette.THREAT, 0.8)
		style.set_border_width_all(2)
	card.add_theme_stylebox_override(&"panel", style)
	var box: VBoxContainer = VBoxContainer.new()
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	box.add_theme_constant_override(&"separation", 2)
	card.add_child(box)
	var head: HBoxContainer = HBoxContainer.new()
	head.mouse_filter = Control.MOUSE_FILTER_IGNORE
	box.add_child(head)
	var number: int = int(wave.get("wave", 0))  # 1-based, as in the wave data
	var title: String = "W%d" % number
	if is_next:
		title = ("NOW  W%d" if in_wave else "NEXT  W%d") % number
	head.add_child(UiKit.label(title, 17,
		Palette.TEXT if is_next else Palette.TEXT_DIM))
	var sun: SunIcon = SunIcon.new()
	sun.light = float(wave.get("light_mult", 1.0))
	head.add_child(sun)
	if sun.light < 0.999:
		head.add_child(UiKit.label("%d%%" % roundi(sun.light * 100.0), 17, Palette.SUN_WARM))
	var entries: Array = wave.get("entries", [])
	if entries.is_empty():
		return card
	var icons: HBoxContainer = HBoxContainer.new()
	icons.mouse_filter = Control.MOUSE_FILTER_IGNORE
	icons.add_theme_constant_override(&"separation", 2)
	box.add_child(icons)
	for entry: Dictionary in entries:
		var icon: EntryIcon = EntryIcon.new()
		var def: Dictionary = _enemy_def(entry.get("enemy"))
		icon.shape = Palette.shape_of(def.get("shape", 0))
		icon.color = Palette.color_of(def.get("color", Palette.tier(0)))
		icon.count = int(entry.get("count", -1))
		icon.bearing = Palette.angle_of(entry.get("lane_bearing", 0.0))
		icon.shadow_caster = bool(entry.get("shadow_caster", false))
		icon.compact = not bool(wave.get("exact", is_next))
		if icon.compact:
			icon.custom_minimum_size = Vector2(38.0, 56.0)
		elif icon.count >= 100:
			icon.custom_minimum_size = Vector2(84.0, 56.0)  # room for three digits
		icons.add_child(icon)
		_entries += 1
	return card


func _enemy_def(key: Variant) -> Dictionary:
	for def: Dictionary in _enemy_defs:
		if key is String or key is StringName:
			if str(def.get("id", "")) == str(key):
				return def
		elif int(def.get("index", -1)) == int(key):
			return def
	return {}


func _sign(forecast: Array[Dictionary]) -> String:
	var parts: PackedStringArray = PackedStringArray()
	for wave: Dictionary in forecast:
		parts.append("%s:%s:%s" % [wave.get("wave"), wave.get("light_mult"), wave.get("exact")])
		var entries: Array = wave.get("entries", [])
		for entry: Dictionary in entries:
			parts.append("%s,%s,%s,%s" % [
				entry.get("lane_bearing"), entry.get("enemy"), entry.get("count"),
				entry.get("shadow_caster")])
	return "|".join(parts)
