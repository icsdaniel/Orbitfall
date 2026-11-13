class_name Palette
extends RefCounted
## Placeholder palette and small helpers that normalise loosely typed values coming
## from SimServer content dictionaries (colour as Color or key, shape as int or name,
## angles in radians or degrees).

const TIER_COLORS: Array[Color] = [
	Color(0.90, 0.22, 0.20), Color(0.22, 0.48, 0.95), Color(0.25, 0.78, 0.33),
	Color(0.98, 0.84, 0.22), Color(0.98, 0.50, 0.75), Color(0.10, 0.10, 0.12),
	Color(0.96, 0.96, 0.96), Color(0.80, 0.62, 0.42),
]
const TIER_NAMES: Array[String] = [
	"red", "blue", "green", "yellow", "pink", "black", "white", "ceramic"]
const NAMED: Dictionary[String, Color] = {
	"tower.cannon": Color(0.93, 0.55, 0.25),
	"tower.gatling": Color(0.45, 0.85, 0.95),
	"building.solar_panel": Color(0.35, 0.55, 1.0),
	"planet.medium": Color(0.17, 0.22, 0.33),
	"enemy.eclipser": Color(0.10, 0.10, 0.12),
	"projectile": Color(1.0, 0.92, 0.55),
}
const SHAPE_NAMES: Dictionary[String, int] = {
	"circle": 0, "triangle": 1, "square": 2, "hexagon": 3, "star": 4,
	"diamond": 5, "disc": 6,
}

const SKY: Color = Color(0.02, 0.02, 0.05)
const PLANET: Color = Color(0.17, 0.22, 0.33)
const PLANET_RIM: Color = Color(0.36, 0.44, 0.60)
const SUN_WARM: Color = Color(1.0, 0.86, 0.45)
const LIT: Color = Color(1.0, 0.80, 0.30)
const UNLIT: Color = Color(0.36, 0.42, 0.55)
const SHADED: Color = Color(0.45, 0.25, 0.75)
const SHADOW: Color = Color(0.0, 0.0, 0.02)
const THREAT: Color = Color(1.0, 0.35, 0.30)
const GOOD: Color = Color(0.35, 0.90, 0.50)
const TEXT: Color = Color(0.93, 0.95, 1.0)
const TEXT_DIM: Color = Color(0.62, 0.68, 0.80)
const PANEL_BG: Color = Color(0.06, 0.08, 0.14, 0.88)


static func tier(index: int) -> Color:
	return TIER_COLORS[clampi(index, 0, TIER_COLORS.size() - 1)]


static func named(key: String) -> Color:
	if NAMED.has(key):
		return NAMED[key]
	if key.begins_with("tier."):
		var i: int = TIER_NAMES.find(key.trim_prefix("tier."))
		if i >= 0:
			return TIER_COLORS[i]
	# Unknown key: a stable placeholder hue so new content is still distinguishable.
	return Color.from_hsv(float(absi(key.hash()) % 360) / 360.0, 0.55, 0.95)


## Colour from a Color, a palette key or anything else (white).
static func color_of(value: Variant) -> Color:
	if value is Color:
		return value as Color
	if value is String or value is StringName:
		return named(str(value))
	return Color.WHITE


## BatchRenderer.Shape id from an int or a shape name.
static func shape_of(value: Variant) -> int:
	if value is int or value is float:
		return int(value)
	if value is String or value is StringName:
		return int(SHAPE_NAMES.get(str(value), 0))
	return 0


## Angles from the sim are radians; anything above a full turn must be degrees.
static func angle_of(value: Variant) -> float:
	var a: float = float(value) if (value is float or value is int) else 0.0
	return deg_to_rad(a) if absf(a) > TAU + 0.001 else a
