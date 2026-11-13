class_name TuningPanel
extends PanelContainer
## M1.11 live tuning: one slider per entry of `tunables` in client_config.json;
## moving a slider calls SimServer.set_tunable(path, value).

var sim: Node
var _sliders: Dictionary[String, HSlider] = {}
var _values: Dictionary[String, Label] = {}


func _init() -> void:
	add_theme_stylebox_override(&"panel", UiKit.panel_style())
	visible = false


func build(tunables: Array[Dictionary]) -> void:
	var box: VBoxContainer = VBoxContainer.new()
	box.add_theme_constant_override(&"separation", 0)
	add_child(box)
	box.add_child(UiKit.label("Live tuning", 18, Palette.TEXT_DIM))
	for entry: Dictionary in tunables:
		var path: String = str(entry.get("path", ""))
		if path == "":
			continue
		var head: HBoxContainer = HBoxContainer.new()
		box.add_child(head)
		var caption: Label = UiKit.label(str(entry.get("label", path)), 16)
		caption.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		head.add_child(caption)
		var value_label: Label = UiKit.label("", 16, Palette.SUN_WARM)
		head.add_child(value_label)
		var slider: HSlider = HSlider.new()
		slider.name = path.replace(".", "_")
		slider.min_value = float(entry.get("min", 0.0))
		slider.max_value = float(entry.get("max", 1.0))
		slider.step = float(entry.get("step", 0.01))
		slider.custom_minimum_size = Vector2(250.0, 48.0)
		slider.focus_mode = Control.FOCUS_NONE
		slider.set_value_no_signal(float(entry.get("default", slider.min_value)))
		value_label.text = _format(slider.value)
		slider.value_changed.connect(_on_changed.bind(path))
		box.add_child(slider)
		_sliders[path] = slider
		_values[path] = value_label


func slider(path: String) -> HSlider:
	return _sliders.get(path)


func _on_changed(value: float, path: String) -> void:
	_values[path].text = _format(value)
	if sim != null:
		sim.set_tunable(path, value)


func _format(value: float) -> String:
	return String.num(value, 2)
