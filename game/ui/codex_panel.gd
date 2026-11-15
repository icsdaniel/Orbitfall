class_name CodexPanel
extends PanelContainer
## M2.3 enemy codex and trait legend (PLAN §5.5: "a trait legend is one tap away").
## Everything shown is generated from SimServer content getters: get_enemy_defs(),
## get_trait_defs(), get_damage_types(), get_shield_mults() and get_building_defs().
## Nothing here names a content id; new enemies and traits appear by themselves.


## An enemy (or a bare trait sample) drawn with the same cues as the SDF shader:
## shape = movement class, colour tier = health, outline/badge = trait.
class CueIcon:
	extends Control
	var shape: int = 0
	var color: Color = Color.WHITE
	## Trait bit mask (BatchRenderer.TRAIT_*).
	var traits: int = 0

	func _init(icon_shape: int = 0, icon_color: Color = Color.WHITE, mask: int = 0) -> void:
		shape = icon_shape
		color = icon_color
		traits = mask
		custom_minimum_size = Vector2(56.0, 56.0)
		mouse_filter = Control.MOUSE_FILTER_IGNORE

	func _draw() -> void:
		var c: Vector2 = size * 0.5
		var r: float = minf(size.x, size.y) * 0.27
		var fill: Color = color
		if traits & BatchRenderer.TRAIT_CAMO != 0:
			fill.a = 0.4
		if traits & BatchRenderer.TRAIT_FAST != 0:
			for dx: float in [-0.5, 0.0, 0.5]:
				var tail: float = 1.9 if dx == 0.0 else 1.6
				draw_line(c + Vector2(dx * r, r * 0.8), c + Vector2(dx * r, r * tail),
					Color(color.lightened(0.35), 0.8), 2.0, true)
		ShapeDraw.draw(self, shape, c, r, 0.0, fill,
			0.0 if traits & BatchRenderer.TRAIT_CAMO != 0 else 1.5)
		var outline: PackedVector2Array = Transform2D(0.0, c) * ShapeDraw.points(shape, r)
		outline.append(outline[0])
		if traits & BatchRenderer.TRAIT_CAMO != 0:
			for i: int in 10:
				var a0: float = TAU * float(i) / 10.0
				draw_arc(c, r * 1.02, a0, a0 + TAU / 20.0, 4, color.lightened(0.3), 2.0, true)
		if traits & BatchRenderer.TRAIT_ARMORED != 0:
			draw_polyline(outline, Palette.ARMOR, 4.0, true)
		if traits & BatchRenderer.TRAIT_SPLITTER != 0:
			draw_colored_polygon(PackedVector2Array([
				c + Vector2(0.0, r * 0.05), c + Vector2(r * 0.6, r * 1.15),
				c + Vector2(-r * 0.6, r * 1.15)]), Color(Palette.PANEL_BG, 1.0))
		if traits & BatchRenderer.TRAIT_HEAT_RESISTANT != 0:
			draw_arc(c, r * 1.22, 0.0, TAU, 28, Palette.HEAT, 2.0, true)
		if traits & BatchRenderer.TRAIT_SHIELDED != 0:
			draw_arc(c, r * 1.48, 0.0, TAU, 28, Color(Palette.SHIELD, 0.9), 3.0, true)
		if traits & BatchRenderer.TRAIT_REGROW != 0:
			var b: Vector2 = c + Vector2(r * 1.15, -r * 1.15)
			draw_circle(b, r * 0.5, Color(0.03, 0.10, 0.05))
			draw_line(b - Vector2(r * 0.32, 0.0), b + Vector2(r * 0.32, 0.0), Palette.REGROW, 2.5)
			draw_line(b - Vector2(0.0, r * 0.32), b + Vector2(0.0, r * 0.32), Palette.REGROW, 2.5)


signal closed

## Row controls by content id, for tests and for scrolling to an entry later.
var enemy_rows: Dictionary[String, Control] = {}
var trait_rows: Dictionary[String, Control] = {}
var tower_rows: Dictionary[String, Control] = {}
## The text shown for each row (same keys), so tests can read what the player reads.
var enemy_text: Dictionary[String, String] = {}
var trait_text: Dictionary[String, String] = {}
var tower_text: Dictionary[String, String] = {}

var _list: VBoxContainer
var _scroll: ScrollContainer


func _init() -> void:
	add_theme_stylebox_override(&"panel", UiKit.panel_style(Color(Palette.PANEL_BG, 0.97)))
	visible = false
	var box: VBoxContainer = VBoxContainer.new()
	box.add_theme_constant_override(&"separation", 8)
	add_child(box)
	var head: HBoxContainer = HBoxContainer.new()
	box.add_child(head)
	var title: Label = UiKit.label("Codex", 30)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(title)
	var hint: Label = UiKit.label("shape = movement   colour = health   outline / badge = trait",
		16, Palette.TEXT_DIM)
	hint.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hint.clip_text = true
	head.add_child(hint)
	var close_button: Button = UiKit.button("X", Vector2(56.0, 56.0), Color(0.30, 0.16, 0.18))
	close_button.name = "Close"
	close_button.pressed.connect(close)
	head.add_child(close_button)
	_scroll = ScrollContainer.new()
	_scroll.custom_minimum_size = Vector2(900.0, 500.0)
	_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	box.add_child(_scroll)
	_list = VBoxContainer.new()
	_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_list.add_theme_constant_override(&"separation", 6)
	_scroll.add_child(_list)


## Reads every definition from the sim and rebuilds the rows.
func build_from(sim: Node) -> void:
	var types: PackedStringArray = PackedStringArray()
	var traits: Array[Dictionary] = []
	var shield: PackedFloat32Array = PackedFloat32Array()
	if sim.has_method(&"get_damage_types"):
		types = sim.get_damage_types()
		traits = sim.get_trait_defs()
		shield = sim.get_shield_mults()
	build(sim.get_enemy_defs(), traits, types, sim.get_building_defs(), shield)


func build(enemy_defs: Array[Dictionary], trait_defs: Array[Dictionary],
		damage_types: PackedStringArray, building_defs: Array[Dictionary],
		shield_mults: PackedFloat32Array = PackedFloat32Array()) -> void:
	for child: Node in _list.get_children():
		_list.remove_child(child)
		child.queue_free()
	enemy_rows.clear()
	trait_rows.clear()
	tower_rows.clear()
	enemy_text.clear()
	trait_text.clear()
	tower_text.clear()

	_section("Enemies")
	for def: Dictionary in enemy_defs:
		var id: String = str(def.get("id", "?"))
		var mask: int = int(def.get("traits", 0))
		var text: String = enemy_summary(def, enemy_defs, trait_defs, damage_types)
		var icon: CueIcon = CueIcon.new(Palette.shape_of(def.get("shape", 0)),
			Palette.color_of(def.get("color", Color.WHITE)), mask)
		enemy_rows[id] = _row(icon, str(def.get("name", id)), text)
		enemy_text[id] = text

	_section("Traits")
	for def: Dictionary in trait_defs:
		# Traits without a legend entry yet (later milestones) are left out.
		if str(def.get("cue", "")) == "":
			continue
		var id: String = str(def.get("id", "?"))
		var text: String = trait_summary(def, damage_types, shield_mults)
		var icon: CueIcon = CueIcon.new(BatchRenderer.Shape.CIRCLE, Palette.tier(1),
			int(def.get("mask", 0)))
		trait_rows[id] = _row(icon, str(def.get("name", id)), text)
		trait_text[id] = text

	_section("Towers")
	for def: Dictionary in building_defs:
		if int(def.get("damage_type", -1)) < 0:
			continue
		var id: String = str(def.get("id", "?"))
		var text: String = "%s damage   %s per shot" % [
			str(def.get("damage_type_name", "?")).capitalize(),
			_number(float(def.get("damage", 0.0)))]
		if float(def.get("splash", 0.0)) > 0.0:
			text += "   splash %s" % _number(float(def.get("splash", 0.0)))
		var icon: ShapeIcon = ShapeIcon.new(Palette.shape_of(def.get("shape", 0)),
			Palette.color_of(def.get("color", Color.WHITE)), 40.0)
		tower_rows[id] = _row(icon, str(def.get("name", id)), text)
		tower_text[id] = text


## Scroll area size for a viewport: never larger than the screen, whatever the
## aspect ratio (phone landscape, desktop window).
static func fit_size(viewport: Vector2) -> Vector2:
	return Vector2(clampf(viewport.x - 120.0, 320.0, 900.0), clampf(viewport.y - 170.0, 160.0, 500.0))


func open() -> void:
	if is_inside_tree():
		_scroll.custom_minimum_size = fit_size(get_viewport_rect().size)
	visible = true
	_scroll.scroll_vertical = 0


func close() -> void:
	if visible:
		visible = false
		closed.emit()


func toggle() -> void:
	if visible:
		close()
	else:
		open()


## Display names of the traits in `mask`, in bit order.
static func trait_names(mask: int, trait_defs: Array[Dictionary]) -> PackedStringArray:
	var out: PackedStringArray = PackedStringArray()
	for def: Dictionary in trait_defs:
		if mask & int(def.get("mask", 0)) != 0:
			out.append(str(def.get("name", def.get("id", "?"))))
	return out


## "Kinetic x0.5, Pierce x1.5" for every multiplier that is not 1; "" when all are 1.
static func multiplier_text(mults: PackedFloat32Array, damage_types: PackedStringArray) -> String:
	var parts: PackedStringArray = PackedStringArray()
	for t: int in mini(mults.size(), damage_types.size()):
		if absf(mults[t] - 1.0) > 0.001:
			parts.append("%s x%s" % [damage_types[t].capitalize(), _number(mults[t])])
	return ", ".join(parts)


## Two lines: stats, then traits and how each damage type does against it.
static func enemy_summary(def: Dictionary, enemy_defs: Array[Dictionary],
		trait_defs: Array[Dictionary], damage_types: PackedStringArray) -> String:
	var layers: int = int(def.get("layers", 1))
	var stats: PackedStringArray = PackedStringArray()
	stats.append("HP %s" % _number(float(def.get("hp", 1.0))))
	if layers > 1:
		stats.append("%d layers" % layers)
	if float(def.get("shield", 0.0)) > 0.0:
		stats.append("shield %s" % _number(float(def.get("shield", 0.0))))
	stats.append("speed %s" % _number(float(def.get("speed", 0.0))))
	stats.append("lands for %s" % _number(float(def.get("land_damage", 0.0))))
	var pops: int = int(def.get("pops_into", -1))
	if pops >= 0 and pops < enemy_defs.size():
		stats.append("pops into %s" % str(enemy_defs[pops].get("name", "?")))
	if bool(def.get("shadow_caster", false)):
		stats.append("casts a shadow")
	var names: PackedStringArray = trait_names(int(def.get("traits", 0)), trait_defs)
	var second: String = "Traits: %s" % (", ".join(names) if not names.is_empty() else "none")
	var raw: Variant = def.get("damage_mult", PackedFloat32Array())
	var mults: PackedFloat32Array = raw if raw is PackedFloat32Array else PackedFloat32Array()
	var counters: String = multiplier_text(mults, damage_types)
	if counters != "":
		second += "   Damage taken: %s" % counters
	return "%s\n%s" % ["   ".join(stats), second]


## Two lines: cue and effect, then what answers it and its matrix row.
static func trait_summary(def: Dictionary, damage_types: PackedStringArray,
		shield_mults: PackedFloat32Array = PackedFloat32Array()) -> String:
	var first: String = "Looks: %s.   %s" % [str(def.get("cue", "")), str(def.get("effect", ""))]
	var second: PackedStringArray = PackedStringArray()
	if str(def.get("easier_with", "")) != "":
		second.append("Easier with: %s" % str(def.get("easier_with", "")))
	var raw: Variant = def.get("mult", PackedFloat32Array())
	var mults: PackedFloat32Array = raw if raw is PackedFloat32Array else PackedFloat32Array()
	var row: String = multiplier_text(mults, damage_types)
	if row != "":
		second.append("Damage taken: %s" % row)
	if int(def.get("mask", 0)) == BatchRenderer.TRAIT_SHIELDED:
		var on_shield: String = multiplier_text(shield_mults, damage_types)
		if on_shield != "":
			second.append("Against the shield: %s" % on_shield)
	return first if second.is_empty() else "%s\n%s" % [first, "   ".join(second)]


static func _number(v: float) -> String:
	return "%d" % roundi(v) if absf(v - roundf(v)) < 0.005 else ("%.2f" % v).rstrip("0")


func _section(title: String) -> void:
	var l: Label = UiKit.label(title, 22, Palette.SUN_WARM)
	_list.add_child(l)


func _row(icon: Control, title: String, text: String) -> Control:
	var row: HBoxContainer = HBoxContainer.new()
	row.add_theme_constant_override(&"separation", 12)
	row.add_child(icon)
	var name_label: Label = UiKit.label(title, 22)
	name_label.custom_minimum_size = Vector2(170.0, 0.0)
	row.add_child(name_label)
	var body: Label = UiKit.label(text, 16, Palette.TEXT_DIM)
	body.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	row.add_child(body)
	_list.add_child(row)
	return row
