class_name EndScreen
extends ColorRect
## Win / lose screen: seed, waves survived, Restart (BRIEF §8.3).

signal restart_requested

var _title: Label
var _detail: Label
var _restart: Button


func _init() -> void:
	color = Color(0.0, 0.0, 0.03, 0.72)
	visible = false
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var centre: CenterContainer = CenterContainer.new()
	centre.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(centre)
	var panel: PanelContainer = UiKit.panel()
	centre.add_child(panel)
	var box: VBoxContainer = VBoxContainer.new()
	box.add_theme_constant_override(&"separation", 14)
	box.alignment = BoxContainer.ALIGNMENT_CENTER
	panel.add_child(box)
	_title = UiKit.label("", 54)
	_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(_title)
	_detail = UiKit.label("", 26, Palette.TEXT_DIM)
	_detail.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(_detail)
	_restart = UiKit.button("Restart", Vector2(320.0, 88.0), Color(0.14, 0.42, 0.24))
	_restart.name = "Restart"
	_restart.pressed.connect(func() -> void: restart_requested.emit())
	box.add_child(_restart)


func show_result(won: bool, run_seed: int, waves_survived: int, wave_count: int) -> void:
	_title.text = "VICTORY" if won else "PLANET LOST"
	_title.add_theme_color_override(&"font_color", Palette.GOOD if won else Palette.THREAT)
	_detail.text = "Waves survived: %d / %d\nSeed: %d" % [waves_survived, wave_count, run_seed]
	visible = true


func title_text() -> String:
	return _title.text


func detail_text() -> String:
	return _detail.text


func restart_button() -> Button:
	return _restart
