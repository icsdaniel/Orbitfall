class_name UiKit
extends RefCounted
## Small factory helpers so the HUD is built in code with consistent, thumb-sized
## placeholder widgets (touch targets >= 48 px).

const MIN_TOUCH: float = 56.0


static func panel_style(bg: Color = Palette.PANEL_BG) -> StyleBoxFlat:
	var style: StyleBoxFlat = StyleBoxFlat.new()
	style.bg_color = bg
	style.set_corner_radius_all(8)
	style.set_content_margin_all(10.0)
	style.border_color = Color(1.0, 1.0, 1.0, 0.12)
	style.set_border_width_all(1)
	return style


static func panel() -> PanelContainer:
	var p: PanelContainer = PanelContainer.new()
	p.add_theme_stylebox_override(&"panel", panel_style())
	return p


static func label(text: String, size: int = 22, color: Color = Palette.TEXT) -> Label:
	var l: Label = Label.new()
	l.text = text
	l.add_theme_font_size_override(&"font_size", size)
	l.add_theme_color_override(&"font_color", color)
	l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return l


static func button(text: String, min_size: Vector2, bg: Color = Color(0.20, 0.26, 0.40)) -> Button:
	var b: Button = Button.new()
	b.text = text
	b.custom_minimum_size = Vector2(maxf(min_size.x, MIN_TOUCH), maxf(min_size.y, MIN_TOUCH))
	b.focus_mode = Control.FOCUS_NONE
	b.add_theme_font_size_override(&"font_size", 24)
	for state: StringName in [&"normal", &"hover", &"pressed", &"disabled"]:
		var style: StyleBoxFlat = panel_style(bg)
		if state == &"hover":
			style.bg_color = bg.lightened(0.12)
		elif state == &"pressed":
			style.bg_color = bg.lightened(0.3)
		elif state == &"disabled":
			style.bg_color = Color(bg.darkened(0.5), 0.7)
		b.add_theme_stylebox_override(state, style)
	return b
