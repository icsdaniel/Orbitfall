class_name WeakPointAssist
extends Node2D
## M2.4 aim assist for weak points (PLAN §5.5). While a weak-point enemy (Bulwark)
## is out and a precision tower (Lance) is built, it shows how far and which way
## to turn the planet to put that tower right under it: a bright tick on the rim
## at the enemy's bearing, an arc with an arrowhead from the tower to that tick,
## and a label next to the planet, "< 23°" or "23° >", matching the rotate
## buttons. Inside the weak-point window it turns green and says "WEAK". Always
## drawn (never hover-only), constant size on screen, in world space so it turns
## with the planet. Weak-point hits flash a small ring where they land.
## Reads SimServer.get_weak_point_hint() and WeakPointHit events only.

const WEAK: Color = Color(1.0, 0.93, 0.35)
const LINED_UP: Color = Color(0.40, 1.0, 0.55)
const FLASH_SECONDS: float = 0.45
const LABEL_PX: int = 22

var sim: Node
## Camera zoom (screen px per world unit), for constant-pixel-size drawing.
var view_zoom: float = 1.0
var radius: float = 100.0
## Latest hint (get_weak_point_hint); {"active": false} when there is none.
var hint: Dictionary = {"active": false}

var _font: Font
var _flashes: Array[Dictionary] = []  # {pos: Vector2, t: float}


func _ready() -> void:
	_font = ThemeDB.fallback_font


func _process(delta: float) -> void:
	sync()
	for f: Dictionary in _flashes:
		f["t"] = float(f["t"]) - delta
	_flashes = _flashes.filter(func(f: Dictionary) -> bool: return float(f["t"]) > 0.0)
	queue_redraw()


func sync() -> void:
	if sim == null:
		return
	radius = float(sim.get_planet_radius())
	if sim.has_method(&"get_weak_point_hint"):
		hint = sim.get_weak_point_hint()
	else:
		hint = {"active": false}


## Called by the game for each WeakPointHit event.
func flash(pos: Vector2) -> void:
	_flashes.append({"pos": pos, "t": FLASH_SECONDS})


func is_active() -> bool:
	return bool(hint.get("active", false))


## Signed degrees to turn (+ = the ">" button), rounded for the label.
func turn_degrees() -> int:
	return roundi(rad_to_deg(float(hint.get("turn", 0.0))))


func is_lined_up() -> bool:
	return is_active() and absf(float(hint.get("turn", 0.0))) <= float(hint.get("window", 0.0))


## The label text: which button and how many degrees, or WEAK when lined up.
func label_text() -> String:
	if not is_active():
		return ""
	if is_lined_up():
		return "WEAK"
	var deg: int = turn_degrees()
	return "%d° >" % deg if deg > 0 else "< %d°" % -deg


func _draw() -> void:
	var px: float = 1.0 / maxf(view_zoom, 0.01)
	for f: Dictionary in _flashes:
		var k: float = float(f["t"]) / FLASH_SECONDS
		draw_arc(f["pos"], (10.0 + 18.0 * (1.0 - k)) * px, 0.0, TAU, 20, Color(WEAK, k), 3.0 * px, true)
	if not is_active():
		return
	var bearing: float = float(hint.get("bearing", 0.0))
	var turn: float = float(hint.get("turn", 0.0))
	var tower: float = bearing - turn  # the tower's world angle now
	var lined: bool = is_lined_up()
	var col: Color = LINED_UP if lined else WEAK
	var rim: float = radius + 34.0 * px + 8.0
	# Target tick on the rim at the enemy's bearing, and a faint line up to it.
	var dir: Vector2 = Vector2.from_angle(bearing)
	draw_line(dir * (radius - 4.0), dir * (rim + 16.0 * px), col, 3.0 * px, true)
	var pos: Vector2 = hint.get("pos", dir * rim)
	draw_line(dir * (rim + 16.0 * px), pos, Color(col, 0.18), 1.5 * px, true)
	if not lined:
		# Arc from the tower to the target with an arrowhead at the target end.
		var steps: int = maxi(4, ceili(absf(turn) / deg_to_rad(4.0)))
		var pts: PackedVector2Array = PackedVector2Array()
		for i: int in steps + 1:
			pts.append(Vector2.from_angle(lerpf(tower, bearing, float(i) / float(steps))) * rim)
		draw_polyline(pts, col, 4.0 * px, true)
		var tip: Vector2 = dir * rim
		var along: Vector2 = dir.orthogonal() * signf(turn)  # direction of travel at the tip
		var size: float = 12.0 * px
		draw_colored_polygon(PackedVector2Array([
			tip + along * size, tip - along * size * 0.6 + dir * size * 0.8,
			tip - along * size * 0.6 - dir * size * 0.8]), col)
		# Where the tower is now.
		var tdir: Vector2 = Vector2.from_angle(tower)
		draw_circle(tdir * rim, 5.0 * px, col)
	# Label just outside the arc's middle, upright, constant size on screen.
	var mid: float = bearing - 0.5 * turn
	var at: Vector2 = Vector2.from_angle(mid) * (rim + 30.0 * px)
	var text: String = label_text()
	var size_px: Vector2 = _font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, LABEL_PX)
	draw_set_transform(at, 0.0, Vector2(px, px))
	var origin: Vector2 = Vector2(-0.5 * size_px.x, 0.35 * LABEL_PX)
	var back: Rect2 = Rect2(origin + Vector2(-8.0, -LABEL_PX - 2.0), size_px + Vector2(16.0, 12.0))
	draw_rect(back, Color(0.0, 0.0, 0.0, 0.62))
	draw_string(_font, origin, text, HORIZONTAL_ALIGNMENT_LEFT, -1, LABEL_PX, col)
	draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
