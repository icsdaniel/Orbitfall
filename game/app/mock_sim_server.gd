class_name MockSimServer
extends Node
## Stand-in for the GDExtension `SimServer` with the full CONTRACT.md method surface
## and a crude fake simulation. It lets the client be built, tested and screenshotted
## before the real binding exists. NOT the game rules: numbers and behaviour are only
## close enough to exercise every client feature. Every call is recorded in `calls`
## so tests can assert which commands the client sent.

const TICK: float = 1.0 / 30.0
const F: int = 16
const PHASE_INTERMISSION: int = 0
const PHASE_WAVE: int = 1
const PHASE_WON: int = 2
const PHASE_LOST: int = 3
const FLAG_LIT: int = 1
const FLAG_SHADED: int = 2
const FLAG_TARGET: int = 4
## M2.3: damage types and trait bits, in engine order (core/content/content.h).
const DAMAGE_TYPES: PackedStringArray = [
	"kinetic", "explosive", "energy", "cryo", "emp", "pierce"]
const TRAIT_IDS: PackedStringArray = [
	"camo", "armored", "shielded", "fast", "regrow", "splitter", "heat_resistant",
	"burrower", "orbiter", "healer", "bulwark", "carrier", "layered"]
const TRAIT_LAYERED: int = 1 << 12
# CommandRejected reasons (Event b), as core/sim/events.h.
const REJECT_INVALID_SLOT: int = 1
const REJECT_INVALID_BUILDING: int = 2
const REJECT_OCCUPIED: int = 3
const REJECT_UNAFFORDABLE: int = 4
const REJECT_EMPTY_SLOT: int = 5
const REJECT_WRONG_PHASE: int = 7
const REJECT_NO_FOUNDATION: int = 8
const REJECT_WRONG_LAYER: int = 9
const REJECT_SUPPORTS_ELEVATED: int = 10
const LAYER_GROUND: int = 0
const LAYER_ELEVATED: int = 1
const LAYER_ANY: int = 2
const LAYER_NAMES: Dictionary[String, int] = {"ground": 0, "elevated": 1, "any": 2}
const KIND_BITS: Dictionary[String, int] = {"tower": 1, "panel": 2, "support": 4}
const AURA_STATS: Dictionary[String, int] = {"range": 0, "fireRate": 1, "damage": 2, "income": 3}
const REJECT_UPGRADE_BLOCKED: int = 11  # M2.2
## M2.2: paths per building, and the stats an upgrade tier may modify (JSON name -> key).
const UPGRADE_PATHS: int = 2
const UPGRADE_STATS: Dictionary[String, String] = {
	"damage": "damage", "fireRate": "fire_rate", "range": "range", "arc": "arc",
	"splash": "splash", "income": "income",
}
const SHAPES: Dictionary[String, int] = {
	"circle": 0, "triangle": 1, "square": 2, "hexagon": 3, "star": 4,
	"diamond": 5, "disc": 6,
}
const TIERS: Dictionary[String, int] = {
	"tier.red": 0, "tier.blue": 1, "tier.green": 2, "tier.yellow": 3, "tier.pink": 4,
	"tier.black": 5, "tier.white": 6, "tier.ceramic": 7, "enemy.eclipser": 5,
}

## Every contract call in order: {method: String, args: Array}.
var calls: Array[Dictionary] = []
## When false the node does not advance itself in _process (tests use step_ticks).
var auto_advance: bool = true

var _content_dir: String = "res://content"
var _seed: int = 0
var _rng: RandomNumberGenerator = RandomNumberGenerator.new()
var _tick: int = 0
var _phase: int = PHASE_INTERMISSION
var _wave_index: int = 0
var _stardust: float = 150.0
var _hp: float = 100.0
var _hp_max: float = 100.0
var _time_scale: float = 1.0
var _scale_from: float = 1.0
var _scale_target: float = 1.0
var _scale_ease: float = 0.0
var _scale_t: float = 0.0
var _accumulator: float = 0.0
var _threaded: bool = false

var _radius: float = 100.0
var _slot_count: int = 36
var _rot: float = 0.0
var _rot_prev: float = 0.0
var _rot_vel: float = 0.0
var _rot_input: float = 0.0
var _rot_max: float = deg_to_rad(90.0)
var _rot_accel: float = deg_to_rad(360.0)
var _rot_damping: float = 4.0
var _start_stardust: float = 150.0
var _sell_refund: float = 0.7
var _lanes: Dictionary[String, float] = {"A": deg_to_rad(30.0), "B": deg_to_rad(170.0)}

var _sun: float = 0.0
var _sun_start: float = 0.0
var _sun_speed: float = deg_to_rad(1.5)
var _beam_width: float = 141.0
var _base_strength: float = 1.0
var _income: float = 0.0
var _shade: PackedFloat32Array = PackedFloat32Array()
var _shade_offsets: PackedFloat32Array = PackedFloat32Array()
var _spawn_radius: float = 900.0
var _surface_time_scale: float = 0.75
var _surface_ease: float = 0.3

var _buildings: Array[Dictionary] = []
var _enemies: Array[Dictionary] = []
## M2.3: trait legend and soft-counter matrix (content/damage.json).
var _trait_defs: Array[Dictionary] = []
var _shield_mults: PackedFloat32Array = PackedFloat32Array([1.0, 1.0, 1.0, 1.0, 1.0, 1.0])
var _min_damage_mult: float = 0.25
var _weak_window_deg: float = 8.0  # M2.4: rules.weakWindowDeg
var _waves: Array[Dictionary] = []
var _slot_building: PackedInt32Array = PackedInt32Array()
var _slot_flags: PackedInt32Array = PackedInt32Array()
var _slot_cooldown: PackedFloat32Array = PackedFloat32Array()
var _slot_aim: PackedFloat32Array = PackedFloat32Array()
# M2.1. The _slot_* arrays above are indexed by site (ground ring, then the
# elevated ring); site = slot + layer * _slot_count.
var _layer_count: int = 2
var _elevated_height: float = 10.0
var _elevated_bonus: float = 0.15
var _site_owner: PackedInt32Array = PackedInt32Array()
var _site_mods: PackedFloat32Array = PackedFloat32Array()
var _links: PackedInt32Array = PackedInt32Array()
## M2.2: bought tiers, UPGRADE_PATHS entries per site.
var _slot_tiers: PackedInt32Array = PackedInt32Array()
var _exclusive_tier: int = 2

var _e_pos: PackedVector2Array = PackedVector2Array()
var _e_prev: PackedVector2Array = PackedVector2Array()
var _e_def: PackedInt32Array = PackedInt32Array()
var _e_hp: PackedFloat32Array = PackedFloat32Array()
var _e_state: PackedInt32Array = PackedInt32Array()
var _e_timer: PackedFloat32Array = PackedFloat32Array()
var _p_from: PackedVector2Array = PackedVector2Array()
var _p_to: PackedVector2Array = PackedVector2Array()
var _p_t: PackedFloat32Array = PackedFloat32Array()
var _p_dur: PackedFloat32Array = PackedFloat32Array()
var _p_size: PackedFloat32Array = PackedFloat32Array()
var _spawn_queue: Array[Dictionary] = []
var _wave_time: float = 0.0
var _events: Array[Dictionary] = []
var _lit_fraction: float = 0.0
var _ms_per_tick: float = 0.0
var _enemy_buffer: PackedFloat32Array = PackedFloat32Array()
var _projectile_buffer: PackedFloat32Array = PackedFloat32Array()


func _init() -> void:
	_load_fallback_content()
	_reset_run()


func _process(delta: float) -> void:
	if not auto_advance:
		return
	if _scale_t < _scale_ease:
		_scale_t = minf(_scale_t + delta, _scale_ease)
		_time_scale = lerpf(_scale_from, _scale_target, _scale_t / _scale_ease)
	else:
		_time_scale = _scale_target
	_accumulator += minf(delta, 0.25) * _time_scale
	while _accumulator >= TICK:
		_accumulator -= TICK
		_step()


# ---------------------------------------------------------------- lifecycle

func load_content(dir: String = "res://content") -> PackedStringArray:
	_record(&"load_content", [dir])
	_content_dir = dir
	var errors: PackedStringArray = PackedStringArray()
	_load_fallback_content()
	_load_damage_rules()
	_load_json_content(errors)
	_reset_run()
	return errors


func new_run(run_seed: int, wave_set: int = 0) -> void:
	_record(&"new_run", [run_seed, wave_set])
	_seed = run_seed
	_reset_run()


func set_threaded(on: bool) -> void:
	_record(&"set_threaded", [on])
	_threaded = on


func step_ticks(n: int) -> void:
	for i: int in n:
		_step()


func get_alpha() -> float:
	return clampf(_accumulator / TICK, 0.0, 1.0) if auto_advance else 1.0


# ---------------------------------------------------------------- commands

func rotate_input(v: float) -> void:
	_record(&"rotate_input", [v])
	_rot_input = clampf(v, -1.0, 1.0)


func place_building(slot: int, building: int, aim_offset: float = 0.0, layer: int = 0) -> void:
	# The M1 three-argument form is recorded as it was called.
	_record(&"place_building",
		[slot, building, aim_offset] if layer == 0 else [slot, building, aim_offset, layer])
	var site: int = slot
	if layer != 0:
		site = slot + _slot_count if (layer == 1 and slot >= 0 and slot < _slot_count) else -1
	if site < 0 or site >= _slot_building.size():
		_emit(&"CommandRejected", site, REJECT_INVALID_SLOT)
		return
	if building < 0 or building >= _buildings.size():
		_emit(&"CommandRejected", site, REJECT_INVALID_BUILDING)
		return
	if _phase >= PHASE_WON:
		_emit(&"CommandRejected", site, REJECT_WRONG_PHASE)
		return
	var def: Dictionary = _buildings[building]
	var elevated: bool = site >= _slot_count
	var base: int = _slot_count if elevated else 0
	var first: int = site - base
	var width: int = clampi(int(def["footprint"]), 1, 3)
	for k: int in width:
		if _site_owner[base + (first + k) % _slot_count] >= 0:
			_emit(&"CommandRejected", site, REJECT_OCCUPIED)
			return
	var allowed: int = int(def["layer"])
	if allowed != LAYER_ANY and (allowed == LAYER_ELEVATED) != elevated:
		_emit(&"CommandRejected", site, REJECT_WRONG_LAYER)
		return
	if elevated:
		for k: int in width:
			if _site_owner[(first + k) % _slot_count] < 0:
				_emit(&"CommandRejected", site, REJECT_NO_FOUNDATION)
				return
	var cost: float = def["cost"]
	if cost > _stardust:
		_emit(&"CommandRejected", site, REJECT_UNAFFORDABLE)
		return
	_stardust -= cost
	_slot_building[site] = building
	_slot_aim[site] = aim_offset
	_slot_cooldown[site] = 0.0
	for k: int in width:
		_site_owner[base + (first + k) % _slot_count] = site
	_set_tiers(site, [0, 0])  # M2.2
	_recompute_layout()
	_emit(&"BuildingPlaced", site, building)
	_emit(&"StardustChanged", int(_stardust), 0)
	_update_light()


## `slot` is a site index and may be any slot the building covers.
func sell_building(slot: int) -> void:
	_record(&"sell_building", [slot])
	if slot < 0 or slot >= _slot_building.size():
		_emit(&"CommandRejected", slot, REJECT_INVALID_SLOT)
		return
	var site: int = _site_owner[slot]
	if site < 0:
		_emit(&"CommandRejected", slot, REJECT_EMPTY_SLOT)
		return
	var building: int = _slot_building[site]
	var base: int = _slot_count if site >= _slot_count else 0
	var first: int = site - base
	var width: int = clampi(int(_buildings[building]["footprint"]), 1, 3)
	if base == 0 and _layer_count > 1:
		for k: int in width:
			if _site_owner[_slot_count + (first + k) % _slot_count] >= 0:
				_emit(&"CommandRejected", slot, REJECT_SUPPORTS_ELEVATED)
				return
	# M2.2: the refund covers the upgrades too.
	var cost: float = float(_buildings[building]["cost"]) + _upgrade_spent(_buildings[building], _tiers(site))
	_stardust += floorf(cost * _sell_refund)
	_emit(&"BuildingSold", site, building)
	_slot_building[site] = -1
	_set_tiers(site, [0, 0])
	for k: int in width:
		_site_owner[base + (first + k) % _slot_count] = -1
	_recompute_layout()
	_emit(&"StardustChanged", int(_stardust), 0)
	_update_light()


# --- M2.2 upgrade paths (begin) ---

func upgrade_building(slot: int, path: int) -> void:
	_record(&"upgrade_building", [slot, path])
	var preview: Dictionary = get_upgrade_preview(slot, path)
	if preview.is_empty() or _phase >= PHASE_WON or not bool(preview["can_upgrade"]):
		# b as core RejectReason: Unaffordable or UpgradeBlocked (empty: EmptySlot).
		var unaffordable: bool = not preview.is_empty() and bool(preview["has_next"]) \
			and not bool(preview["locked"]) and not bool(preview["affordable"])
		var reason: int = REJECT_UPGRADE_BLOCKED
		if _anchor(slot) < 0:
			reason = REJECT_EMPTY_SLOT if slot >= 0 and slot < _slot_building.size() else REJECT_INVALID_SLOT
		elif unaffordable and _phase < PHASE_WON:
			reason = REJECT_UNAFFORDABLE
		_emit(&"CommandRejected", slot, reason)
		return
	var site: int = _anchor(slot)
	var tiers: Array[int] = _tiers(site)
	tiers[path] += 1
	_set_tiers(site, tiers)
	_stardust -= float(preview["cost"])
	_emit(&"BuildingUpgraded", site, path, Vector2(float(tiers[path]), 0.0))
	_emit(&"StardustChanged", int(_stardust), 0)
	_update_light()


func get_slot_tiers(slot: int) -> PackedInt32Array:
	var site: int = _anchor(slot)
	return PackedInt32Array(_tiers(site) if site >= 0 else [0, 0])


func get_upgrade_preview(slot: int, path: int) -> Dictionary:
	slot = _anchor(slot)
	if slot < 0:
		return {}
	var def: Dictionary = _buildings[_slot_building[slot]]
	var paths: Array = def.get("paths", [])
	if path < 0 or path >= mini(paths.size(), UPGRADE_PATHS):
		return {}
	var tiers: Array[int] = _tiers(slot)
	var pd: Dictionary = paths[path]
	var tier_defs: Array = pd["tiers"]
	var tier: int = tiers[path]
	var cap: int = tier_defs.size()
	for other: int in mini(paths.size(), UPGRADE_PATHS):
		if other != path and tiers[other] > _exclusive_tier:
			cap = mini(cap, _exclusive_tier)
	var has_next: bool = tier < tier_defs.size()
	var before: Dictionary = _site_stats(def, tiers, slot)
	var after: Dictionary = before
	var cost: float = 0.0
	var next_name: String = ""
	if has_next:
		var next: Array[int] = tiers.duplicate()
		next[path] += 1
		after = _site_stats(def, next, slot)
		cost = float(tier_defs[tier]["cost"])
		next_name = str(tier_defs[tier]["name"])
	var changes: Array[Dictionary] = []
	for key: String in before:
		if not is_equal_approx(float(before[key]), float(after[key])):
			changes.append({"stat": key, "before": before[key], "after": after[key]})
	var affordable: bool = has_next and _stardust >= cost
	return {
		"path": path, "id": pd["id"], "name": pd["name"], "tier": tier,
		"max_tier": tier_defs.size(), "cap": cap, "has_next": has_next, "next_name": next_name,
		"cost": cost, "affordable": affordable, "can_upgrade": affordable and tier < cap,
		"locked": has_next and tier >= cap, "before": before, "after": after, "changes": changes,
	}


func get_slot_upgrades(slot: int) -> Dictionary:
	slot = _anchor(slot)
	if slot < 0:
		return {}
	var def: Dictionary = _buildings[_slot_building[slot]]
	var tiers: Array[int] = _tiers(slot)
	var spent: float = _upgrade_spent(def, tiers)
	var paths: Array[Dictionary] = []
	var path_defs: Array = def.get("paths", [])
	for path: int in mini(path_defs.size(), UPGRADE_PATHS):
		paths.append(get_upgrade_preview(slot, path))
	return {
		"slot": slot, "building": _slot_building[slot], "tiers": PackedInt32Array(tiers),
		"stats": _site_stats(def, tiers, slot), "spent": spent,
		"sell_refund": (float(def["cost"]) + spent) * _sell_refund, "paths": paths,
	}


## M2.1: the anchor site of the building covering `slot` (either ring), or -1.
func _anchor(slot: int) -> int:
	if slot < 0 or slot >= _site_owner.size():
		return -1
	var site: int = _site_owner[slot]
	return site if site >= 0 and _slot_building[site] >= 0 else -1


## Upgraded stats times the site's M2.1 multipliers (elevated bonus, auras).
func _site_stats(def: Dictionary, tiers: Array[int], site: int) -> Dictionary:
	var stats: Dictionary = _stats(def, tiers)
	if 4 * site + 3 < _site_mods.size():
		stats["range"] = float(stats["range"]) * _site_mods[4 * site]
		stats["fire_rate"] = float(stats["fire_rate"]) * _site_mods[4 * site + 1]
		stats["damage"] = float(stats["damage"]) * _site_mods[4 * site + 2]
		stats["income"] = float(stats["income"]) * _site_mods[4 * site + 3]
	return stats


func _tiers(slot: int) -> Array[int]:
	var out: Array[int] = []
	for path: int in UPGRADE_PATHS:
		var i: int = slot * UPGRADE_PATHS + path
		out.append(_slot_tiers[i] if slot >= 0 and i < _slot_tiers.size() else 0)
	return out


func _set_tiers(slot: int, tiers: Array) -> void:
	for path: int in UPGRADE_PATHS:
		_slot_tiers[slot * UPGRADE_PATHS + path] = int(tiers[path])


## As core: effective = (base + adds) * muls over every bought tier of both paths.
func _stats(def: Dictionary, tiers: Array[int]) -> Dictionary:
	var stats: Dictionary = {
		"damage": float(def["damage"]), "fire_rate": float(def["fire_rate"]),
		"range": float(def["range"]), "arc": float(def["arc"]),
		"splash": float(def["splash"]), "income": float(def["panel_rate"]),
	}
	var mul: Dictionary = {}
	var paths: Array = def.get("paths", [])
	for path: int in mini(paths.size(), UPGRADE_PATHS):
		var tier_defs: Array = paths[path]["tiers"]
		for t: int in mini(tiers[path], tier_defs.size()):
			var mods: Array = tier_defs[t]["mods"]
			for mod: Dictionary in mods:
				var key: String = mod["stat"]
				if str(mod["op"]) == "mul":
					mul[key] = float(mul.get(key, 1.0)) * float(mod["value"])
				else:
					stats[key] = float(stats[key]) + float(mod["value"])
	for key: String in mul:
		stats[key] = float(stats[key]) * float(mul[key])
	for key: String in stats:
		stats[key] = maxf(float(stats[key]), 0.0)
	stats["arc"] = minf(float(stats["arc"]), PI)
	return stats


func _upgrade_spent(def: Dictionary, tiers: Array[int]) -> float:
	var spent: float = 0.0
	var paths: Array = def.get("paths", [])
	for path: int in mini(paths.size(), UPGRADE_PATHS):
		var tier_defs: Array = paths[path]["tiers"]
		for t: int in mini(tiers[path], tier_defs.size()):
			spent += float(tier_defs[t]["cost"])
	return spent


## JSON "paths" -> [{id, name, tiers: [{name, cost, mods: [{stat, op, value}]}]}] with
## stat keys as in the stats dictionaries and arc adds in radians.
func _read_paths(raw: Variant) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	if not raw is Array:
		return out
	for p: Variant in raw as Array:
		if not p is Dictionary or out.size() >= UPGRADE_PATHS:
			continue
		var pd: Dictionary = p
		var tiers: Array[Dictionary] = []
		var raw_tiers: Array = pd.get("tiers", [])
		for t: Variant in raw_tiers:
			if not t is Dictionary:
				continue
			var td: Dictionary = t
			var mods: Array[Dictionary] = []
			var raw_mods: Array = td.get("mods", [])
			for m: Variant in raw_mods:
				if not m is Dictionary:
					continue
				var md: Dictionary = m
				var stat: String = str(md.get("stat", ""))
				if not UPGRADE_STATS.has(stat):
					continue
				var op: String = str(md.get("op", "mul"))
				var value: float = float(md.get("value", 1.0))
				if stat == "arc" and op == "add":
					value = deg_to_rad(value)
				mods.append({"stat": UPGRADE_STATS[stat], "op": op, "value": value})
			tiers.append({
				"name": str(td.get("name", "Tier %d" % (tiers.size() + 1))),
				"cost": float(td.get("cost", 0.0)), "mods": mods,
			})
		var id: String = str(pd.get("id", "path%d" % out.size()))
		out.append({"id": id, "name": str(pd.get("name", id)), "tiers": tiers})
	return out

# --- M2.2 upgrade paths (end) ---


func set_aim(slot: int, offset: float) -> void:
	_record(&"set_aim", [slot, offset])
	if slot >= 0 and slot < _slot_aim.size():
		_slot_aim[slot] = offset


func set_targeting(slot: int, mode: int) -> void:
	_record(&"set_targeting", [slot, mode])


func start_wave() -> void:
	_record(&"start_wave", [])
	if _phase != PHASE_INTERMISSION or _wave_index >= _waves.size():
		_emit(&"CommandRejected", -1, -1)
		return
	_phase = PHASE_WAVE
	_wave_time = 0.0
	_build_spawn_queue()
	_emit(&"WaveStarted", _wave_index, 0)


func set_time_scale(scale: float, ease_seconds: float = 0.0) -> void:
	_record(&"set_time_scale", [scale, ease_seconds])
	_scale_from = _time_scale
	_scale_target = maxf(scale, 0.0)
	_scale_ease = maxf(ease_seconds, 0.0)
	_scale_t = 0.0
	if _scale_ease <= 0.0 or not auto_advance:
		_time_scale = _scale_target
		_scale_t = _scale_ease


func get_command_log() -> PackedByteArray:
	var commands: Array[Dictionary] = []
	for c: Dictionary in calls:
		commands.append(c)
	return var_to_bytes({"seed": _seed, "calls": commands})


func replay(run_seed: int, _log: PackedByteArray) -> void:
	_record(&"replay", [run_seed])
	_seed = run_seed
	_reset_run()


# ---------------------------------------------------------------- snapshot

func get_tick() -> int:
	return _tick


func get_state_hash() -> int:
	return hash([_tick, _stardust, _hp, _rot, _sun, _e_pos.size(), _slot_building])


func get_phase() -> int:
	return _phase


func get_wave_index() -> int:
	return _wave_index


func get_wave_count() -> int:
	return _waves.size()


func get_stardust() -> float:
	return _stardust


func get_planet_hp() -> float:
	return _hp


func get_planet_hp_max() -> float:
	return _hp_max


func get_time_scale() -> float:
	return _time_scale


func get_planet_radius() -> float:
	return _radius


func get_planet_rotation() -> float:
	return lerp_angle(_rot_prev, _rot, get_alpha())


func get_planet_angular_velocity() -> float:
	return _rot_vel


func get_slot_count() -> int:
	return _slot_count


func get_slot_buildings() -> PackedInt32Array:
	return _slot_building.slice(0, _slot_count)


func get_slot_flags() -> PackedInt32Array:
	return _slot_flags.slice(0, _slot_count)


# ---- M2.1 placement depth ----

func get_layer_count() -> int:
	return _layer_count


func get_elevated_height() -> float:
	return _elevated_height


func get_elevated_range_bonus() -> float:
	return _elevated_bonus


func get_site_buildings() -> PackedInt32Array:
	return _slot_building


func get_site_flags() -> PackedInt32Array:
	return _slot_flags


func get_site_owners() -> PackedInt32Array:
	return _site_owner


func get_site_modifiers() -> PackedFloat32Array:
	return _site_mods


func get_adjacency_links() -> PackedInt32Array:
	return _links

# ---- end M2.1 ----


func get_sun_bearing() -> float:
	return _sun


func get_beam_width() -> float:
	return _beam_width


func get_beam_strength() -> float:
	return _base_strength * _light_mult(_wave_index)


func get_shade_intervals() -> PackedFloat32Array:
	return _shade


func get_income_per_second() -> float:
	return _income


func get_enemy_count() -> int:
	return _e_pos.size()


func get_enemy_buffer() -> PackedFloat32Array:
	var n: int = _e_pos.size()
	_enemy_buffer.resize(n * F)
	var alpha: float = get_alpha()
	for i: int in n:
		var def: Dictionary = _enemies[_e_def[i]]
		var pos: Vector2 = _e_prev[i].lerp(_e_pos[i], alpha)
		var size: float = float(def["size"])  # instance scale = radius
		var tier: int = int(def["tier"])
		if int(def["layers"]) > 1 or not bool(def["hp_bar"]):
			tier = clampi(int(ceilf(_e_hp[i] / float(def["hp_per_layer"]))) - 1, 0, 7)
		var xform: Transform2D = Transform2D(
			(-pos).angle() + PI * 0.5, Vector2(size, size), 0.0, pos)
		BatchRenderer.write_instance(
			_enemy_buffer, i, xform, Color.WHITE,
			BatchRenderer.make_custom(int(def["shape"]), tier, int(def["traits"])))
	return _enemy_buffer


func get_projectile_count() -> int:
	return _p_t.size()


func get_projectile_buffer() -> PackedFloat32Array:
	var n: int = _p_t.size()
	_projectile_buffer.resize(n * F)
	for i: int in n:
		var pos: Vector2 = _p_from[i].lerp(_p_to[i], clampf(_p_t[i] / _p_dur[i], 0.0, 1.0))
		BatchRenderer.write_instance(
			_projectile_buffer, i,
			Transform2D(0.0, Vector2(_p_size[i], _p_size[i]), 0.0, pos),
			Color(1.0, 0.92, 0.55), BatchRenderer.make_custom(BatchRenderer.Shape.CIRCLE, -1))
	return _projectile_buffer


func get_large_units() -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	var s: Vector2 = Vector2.from_angle(_sun)
	for i: int in _e_pos.size():
		var def: Dictionary = _enemies[_e_def[i]]
		if bool(def["hp_bar"]):
			out.append({
				"pos": _e_pos[i], "radius": float(def["size"]),
				"hp_frac": _e_hp[i] / float(def["hp"]),
				"shadow": bool(def["shadow_caster"]) and _e_pos[i].dot(s) > _radius,
				# M2.3 (the mock does not simulate shields: up until the unit dies).
				"segments": int(def["hp_bar_segments"]),
				"shield_frac": 1.0 if float(def["shield"]) > 0.0 else 0.0,
			})
	return out


func get_building_defs() -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	for b: Dictionary in _buildings:
		out.append({
			"index": b["index"], "id": b["id"], "name": b["name"], "cost": b["cost"],
			"arc": b["arc"], "range": b["range"], "kind": b["kind"], "shape": b["shape"],
			"color": b["color"], "footprint": b["footprint"], "layer": b["layer"],
			"auras": b["auras"], "fire_rate": b["fire_rate"], "panel_rate": b["panel_rate"],
			# M2.3
			"damage_type": b["damage_type"], "damage_type_name": b["damage_type_name"],
			"damage": b["damage"], "splash": b["splash"],
			"precision": b["precision"],  # M2.4
		})
	return out


func get_enemy_defs() -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	for e: Dictionary in _enemies:
		out.append({
			"index": e["index"], "id": e["id"], "name": e["name"], "shape": e["shape"],
			"tier": e["tier"], "color": e["color"], "size": e["size"],
			"shadow_caster": e["shadow_caster"],
			# M2.3 (codex)
			"layers": e["layers"], "hp": e["hp"], "speed": e["speed"], "bounty": e["bounty"],
			"traits": e["traits"], "hp_bar": e["hp_bar"], "pops_into": e["pops_into"],
			"shield": e["shield"], "hp_bar_segments": e["hp_bar_segments"],
			"land_damage": float(e["land_damage"]) * float(e["layers"]), "damage_mult": _enemy_mults(e),
			"weak_point": (e["weak_point"] as Dictionary).duplicate(),  # M2.4
		})
	return out


# --- M2.3 damage types and traits ---
func get_damage_types() -> PackedStringArray:
	return DAMAGE_TYPES


func get_trait_defs() -> Array[Dictionary]:
	return _trait_defs.duplicate(true)


func get_shield_mults() -> PackedFloat32Array:
	return _shield_mults


## Own damageMult x the row of every trait carried, floored (as core does).
func _enemy_mults(e: Dictionary) -> PackedFloat32Array:
	var own: PackedFloat32Array = e["damage_mult"]
	var out: PackedFloat32Array = own.duplicate()
	var traits: int = int(e["traits"])
	for def: Dictionary in _trait_defs:
		if traits & int(def["mask"]) != 0:
			var row: PackedFloat32Array = def["mult"]
			for t: int in out.size():
				out[t] *= row[t]
	for t: int in out.size():
		out[t] = maxf(out[t], _min_damage_mult)
	return out


func _type_mults(raw: Variant) -> PackedFloat32Array:
	var out: PackedFloat32Array = PackedFloat32Array([1.0, 1.0, 1.0, 1.0, 1.0, 1.0])
	if raw is Dictionary:
		var d: Dictionary = raw
		for key: Variant in d:
			var t: int = DAMAGE_TYPES.find(str(key))
			if t >= 0:
				out[t] = float(d[key])
	return out


func _load_damage_rules() -> void:
	var doc: Dictionary = _read_json(_content_dir.path_join("damage.json"))
	var rows: Dictionary = doc.get("traits", {})
	_shield_mults = _type_mults(doc.get("shieldMult"))
	for t: int in _shield_mults.size():
		_shield_mults[t] = maxf(_shield_mults[t], _min_damage_mult)
	_trait_defs = []
	for bit: int in TRAIT_IDS.size():
		var id: String = TRAIT_IDS[bit]
		var row: Dictionary = rows.get(id, {})
		_trait_defs.append({
			"bit": bit, "mask": 1 << bit, "id": id, "name": str(row.get("name", id)),
			"cue": str(row.get("cue", "")), "effect": str(row.get("effect", "")),
			"easier_with": str(row.get("easierWith", "")), "mult": _type_mults(row.get("mult")),
		})
# --- end M2.3 ---


# --- M2.4 weak points ---
## Same answer as core weakPointHint: the weak-point enemy nearest the planet
## and the precision tower that needs the smallest turn to sit under it.
func get_weak_point_hint() -> Dictionary:
	var best: int = -1
	var best_d: float = 0.0
	for i: int in _e_pos.size():
		var wp: Dictionary = _enemies[_e_def[i]].get("weak_point", {})
		if wp.is_empty():
			continue
		var d: float = _e_pos[i].length()
		if best < 0 or d < best_d:
			best = i
			best_d = d
	if best < 0:
		return {"active": false}
	var pos: Vector2 = _e_prev[best].lerp(_e_pos[best], get_alpha()) if best < _e_prev.size() else _e_pos[best]
	var bearing: float = pos.angle()
	var rotation_now: float = get_planet_rotation()
	var site: int = -1
	var turn: float = 0.0
	for s: int in _slot_building.size():
		var b: int = _slot_building[s]
		if b < 0 or not bool(_buildings[b].get("precision", false)):
			continue
		var t: float = wrapf(bearing - (_site_angle(s) + rotation_now), -PI, PI)
		if site < 0 or absf(t) < absf(turn):
			site = s
			turn = t
	if site < 0:
		return {"active": false}
	var window: float = float((_enemies[_e_def[best]]["weak_point"] as Dictionary).get("window", 0.0))
	return {
		"active": true, "site": site, "site_angle": _site_angle(site), "bearing": bearing,
		"pos": pos, "distance": pos.length(), "turn": turn, "window": window,
	}


func get_wave_sets() -> PackedStringArray:
	var out: PackedStringArray = PackedStringArray()
	for d: Dictionary in _read_dir("waves"):
		out.append(str(d.get("id", "?")))
	if out.is_empty():
		out.append("sample")
	return out
# --- end M2.4 ---


func get_forecast(count: int) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	for k: int in count:
		var w: int = _wave_index + k  # next wave, or the one in flight
		if w >= _waves.size():
			break
		var wave: Dictionary = _waves[w]
		var exact: bool = k < int(wave["exact_ahead"])
		var icons: bool = k < int(wave["icons_ahead"])
		var entries: Array[Dictionary] = []
		var ghost: PackedFloat32Array = PackedFloat32Array()
		if icons:
			var spawns: Array = wave["spawns"]
			for spawn: Dictionary in spawns:
				var def: Dictionary = _enemies[int(spawn["enemy"])]
				entries.append({
					"lane_bearing": spawn["bearing"], "enemy": spawn["enemy"],
					"count": int(spawn["count"]) if exact else -1,
					"shadow_caster": def["shadow_caster"],
				})
				if exact and bool(def["shadow_caster"]):
					# Crude guess: the Eclipser parks on the sun line where the sun
					# will be once it has flown in.
					var eta: float = (_spawn_radius - 260.0) / maxf(float(def["speed"]), 1.0)
					var centre: float = _sun + _sun_speed * eta
					var half: float = asin(clampf(float(def["size"]) / _radius, 0.0, 1.0))
					ghost.append(centre - half)
					ghost.append(centre + half)
		out.append({
			"wave": w + 1, "light_mult": wave["light_mult"], "exact": exact,
			"entries": entries, "eclipser_ghost_arcs": ghost,
		})
	return out


func get_debug_stats() -> Dictionary:
	return {
		"ms_per_tick": _ms_per_tick, "enemies": _e_pos.size(),
		"projectiles": _p_t.size(), "lit_fraction": _lit_fraction, "mock": true,
		"weak_hits": 0, "body_hits": 0,  # M2.4 (not simulated by the mock)
	}


func set_tunable(path: String, value: float) -> void:
	_record(&"set_tunable", [path, value])
	match path:
		"sun.speedDegPerSec":
			_sun_speed = deg_to_rad(value)
		"sun.beamWidth":
			_beam_width = value
		"sun.baseStrength":
			_base_strength = value
		"planet.rotMaxSpeed":
			_rot_max = deg_to_rad(value)
		"planet.rotAccel":
			_rot_accel = deg_to_rad(value)
		"planet.rotDamping":
			_rot_damping = value
	_update_light()


func get_surface_time_scale() -> float:
	return _surface_time_scale


func get_surface_time_ease_seconds() -> float:
	return _surface_ease


func drain_events() -> Array[Dictionary]:
	var out: Array[Dictionary] = _events
	_events = []
	return out


# ---------------------------------------------------------------- test helpers

## Calls recorded for one method, oldest first (each entry is the args array).
func calls_to(method: StringName) -> Array[Array]:
	var out: Array[Array] = []
	for c: Dictionary in calls:
		if c["method"] == method:
			var args: Array = c["args"]
			out.append(args)
	return out


func clear_calls() -> void:
	calls.clear()


## Test helper (M2.1): adds a building definition for this run, e.g. an aura
## building that M1 content does not have. `extra` overrides fields of a plain
## 1-slot support building. Returns its index.
func debug_add_building(id: String, extra: Dictionary) -> int:
	var b: Dictionary = _building(
		_buildings.size(), id, "support", 10.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, "hexagon")
	b["layer"] = LAYER_ANY
	b.merge(extra, true)
	_buildings.append(b)
	return _buildings.size() - 1


## Test/screenshot helper: drop an enemy of the given def id at a world position.
func debug_spawn(enemy_id: String, pos: Vector2) -> void:
	for e: Dictionary in _enemies:
		if e["id"] == enemy_id:
			_add_enemy(int(e["index"]), pos)
			_e_state[_e_state.size() - 1] = 1 if bool(e["shadow_caster"]) else 0
	_update_light()


## Test helper: jump to the next intermission (or to victory after the last wave).
func debug_skip_wave() -> void:
	_e_hp.fill(-1.0)
	_compact_enemies()
	_spawn_queue.clear()
	_phase = PHASE_WAVE
	_check_wave_end()


func debug_set_stardust(value: float) -> void:
	_stardust = value


func debug_set_phase(phase: int) -> void:
	_phase = phase


func debug_set_planet_hp(value: float) -> void:
	_hp = value


# ---------------------------------------------------------------- fake sim

func _record(method: StringName, args: Array) -> void:
	calls.append({"method": method, "args": args})


func _emit(type: StringName, a: int, b: int, pos: Vector2 = Vector2.ZERO) -> void:
	_events.append({"type": String(type), "tick": _tick, "a": a, "b": b, "x": pos.x, "y": pos.y})
	if _events.size() > 4096:
		_events = _events.slice(2048)


func _reset_run() -> void:
	_rng.seed = _seed
	_tick = 0
	_phase = PHASE_INTERMISSION
	_wave_index = 0
	_stardust = _start_stardust
	_hp = _hp_max
	_rot = 0.0
	_rot_prev = 0.0
	_rot_vel = 0.0
	_rot_input = 0.0
	_sun = _sun_start
	_income = 0.0
	_accumulator = 0.0
	var sites: int = _slot_count * _layer_count
	_slot_building.resize(sites)
	_slot_building.fill(-1)
	_slot_flags.resize(sites)
	_slot_flags.fill(0)
	_slot_cooldown.resize(sites)
	_slot_cooldown.fill(0.0)
	_slot_aim.resize(sites)
	_slot_aim.fill(0.0)
	_slot_tiers.resize(sites * UPGRADE_PATHS)
	_slot_tiers.fill(0)
	_site_owner.resize(sites)
	_site_owner.fill(-1)
	_recompute_layout()
	_e_pos.clear()
	_e_prev.clear()
	_e_def.clear()
	_e_hp.clear()
	_e_state.clear()
	_e_timer.clear()
	_p_from.clear()
	_p_to.clear()
	_p_t.clear()
	_p_dur.clear()
	_p_size.clear()
	_spawn_queue.clear()
	_events.clear()
	_update_light()


## Local angle of a site: the centre of its footprint for an anchor.
func _site_angle(site: int) -> float:
	var extra: float = 0.0
	var b: int = _slot_building[site]
	if b >= 0:
		extra = 0.5 * float(clampi(int(_buildings[b]["footprint"]), 1, 3) - 1)
	return TAU * (float(site % _slot_count) + extra) / float(_slot_count)


func _site_radius(site: int) -> float:
	return _radius + (_elevated_height if site >= _slot_count else 0.0)


func _footprint_gap(a: int, wa: int, b: int, wb: int) -> int:
	var best: int = _slot_count
	for i: int in wa:
		for j: int in wb:
			var d: int = posmod(a + i - b - j, _slot_count)
			best = mini(best, mini(d, _slot_count - d))
	return best


## M2.1: elevated range bonus and adjacency auras, on layout changes only.
func _recompute_layout() -> void:
	var sites: int = _slot_building.size()
	_site_mods.resize(sites * 4)
	_site_mods.fill(0.0)
	_links = PackedInt32Array()
	for s: int in sites:
		var sb: int = _slot_building[s]
		if sb < 0:
			continue
		var auras: Array = _buildings[sb]["auras"]
		var sw: int = int(_buildings[sb]["footprint"])
		for aura: Dictionary in auras:
			for t: int in sites:
				var tb: int = _slot_building[t]
				if t == s or tb < 0:
					continue
				var target: Dictionary = _buildings[tb]
				if (int(aura["kinds"]) & int(KIND_BITS.get(str(target["kind"]), 0))) == 0:
					continue
				if _footprint_gap(s % _slot_count, sw, t % _slot_count,
						int(target["footprint"])) > int(aura["radius"]):
					continue
				_site_mods[4 * t + int(aura["stat"])] += float(aura["amount"])
				_links.append_array(PackedInt32Array([s, t, int(aura["stat"])]))
	for s: int in sites:
		for k: int in 4:
			_site_mods[4 * s + k] = maxf(1.0 + _site_mods[4 * s + k], 0.1)
		if s >= _slot_count:
			_site_mods[4 * s] *= 1.0 + _elevated_bonus


func _light_mult(wave: int) -> float:
	if wave < 0 or wave >= _waves.size():
		return 1.0
	return float(_waves[wave]["light_mult"])


func _step() -> void:
	var t0: int = Time.get_ticks_usec()
	_tick += 1
	_step_rotation()
	if _phase == PHASE_WAVE:
		_sun = wrapf(_sun + _sun_speed * TICK, -PI, PI)
		_wave_time += TICK
		_step_spawns()
		_step_enemies()
		_update_light()
		_step_towers()
		_step_projectiles()
		_compact_enemies()
		_stardust += _income * TICK
		_check_wave_end()
	else:
		_e_prev = _e_pos.duplicate()
		_update_light()
	_ms_per_tick = lerpf(_ms_per_tick, float(Time.get_ticks_usec() - t0) / 1000.0, 0.1)


func _step_rotation() -> void:
	_rot_prev = _rot
	if absf(_rot_input) > 0.001:
		_rot_vel = move_toward(_rot_vel, _rot_input * _rot_max, _rot_accel * TICK)
	else:
		_rot_vel *= maxf(1.0 - _rot_damping * TICK, 0.0)
		if absf(_rot_vel) < 0.0005:
			_rot_vel = 0.0
	_rot = wrapf(_rot + _rot_vel * TICK, -PI, PI)


func _build_spawn_queue() -> void:
	_spawn_queue.clear()
	var wave: Dictionary = _waves[_wave_index]
	var seconds: float = wave["spawn_seconds"]
	var spawns: Array = wave["spawns"]
	for spawn: Dictionary in spawns:
		var n: int = spawn["count"]
		for i: int in n:
			_spawn_queue.append({
				"t": seconds * (float(i) + 0.5) / float(n),
				"enemy": spawn["enemy"], "bearing": spawn["bearing"],
			})
	_spawn_queue.sort_custom(_spawn_before)


func _spawn_before(a: Dictionary, b: Dictionary) -> bool:
	return float(a["t"]) < float(b["t"])


func _step_spawns() -> void:
	while not _spawn_queue.is_empty() and float(_spawn_queue[0]["t"]) <= _wave_time:
		var spawn: Dictionary = _spawn_queue.pop_front()
		var def: Dictionary = _enemies[int(spawn["enemy"])]
		var jitter: float = deg_to_rad(_rng.randf_range(-6.0, 6.0)) if bool(def["shadow_caster"]) else 0.0
		_add_enemy(int(spawn["enemy"]), Vector2.from_angle(float(spawn["bearing"]) + jitter) * _spawn_radius)


func _add_enemy(def_index: int, pos: Vector2) -> void:
	_e_pos.append(pos)
	_e_prev.append(pos)
	_e_def.append(def_index)
	_e_hp.append(float(_enemies[def_index]["hp"]))
	_e_state.append(0)
	_e_timer.append(0.0)


func _step_enemies() -> void:
	var s: Vector2 = Vector2.from_angle(_sun)
	for i: int in _e_pos.size():
		var def: Dictionary = _enemies[_e_def[i]]
		var pos: Vector2 = _e_pos[i]
		_e_prev[i] = pos
		var speed: float = def["speed"]
		if bool(def["shadow_caster"]) and _e_state[i] < 2:
			var hover: Vector2 = s * 260.0
			if _e_state[i] == 0:
				pos = pos.move_toward(hover, speed * TICK)
				if pos.distance_to(hover) < 1.0:
					_e_state[i] = 1
			else:
				pos = hover
				_e_timer[i] += TICK
				if _e_timer[i] >= 15.0:
					_e_state[i] = 2
		elif bool(def["shadow_caster"]):
			pos = pos.move_toward(Vector2.ZERO, 10.0 * TICK)
		else:
			pos = pos.move_toward(Vector2.ZERO, speed * TICK)
		_e_pos[i] = pos
		if pos.length() <= _radius:
			var damage: float = float(def["land_damage"])
			if int(def["layers"]) > 1:
				damage *= ceilf(_e_hp[i] / float(def["hp_per_layer"]))
			_hp = maxf(_hp - damage, 0.0)
			_e_hp[i] = -1.0e6
			_emit(&"PlanetHit", int(damage), int(_hp), pos)
			if _hp <= 0.0 and _phase == PHASE_WAVE:
				_phase = PHASE_LOST
				_emit(&"GameOver", _wave_index, 0)


func _update_light() -> void:
	var s: Vector2 = Vector2.from_angle(_sun)
	var half_w: float = minf(_beam_width * 0.5, _radius)
	# Shade intervals in beam-offset space, then merged.
	var raw: Array[Vector2] = []
	for i: int in _e_pos.size():
		var def: Dictionary = _enemies[_e_def[i]]
		if not bool(def["shadow_caster"]) or _e_hp[i] <= 0.0:
			continue
		var p: Vector2 = _e_pos[i]
		if p.dot(s) <= _radius:
			continue
		var o: float = s.x * p.y - s.y * p.x
		var r: float = def["size"]
		var lo: float = maxf(o - r, -half_w)
		var hi: float = minf(o + r, half_w)
		if lo < hi:
			raw.append(Vector2(lo, hi))
	raw.sort_custom(_interval_before)
	var merged: Array[Vector2] = []
	for iv: Vector2 in raw:
		if not merged.is_empty() and iv.x <= merged[-1].y:
			merged[-1] = Vector2(merged[-1].x, maxf(merged[-1].y, iv.y))
		else:
			merged.append(iv)
	var had_shade: bool = not _shade.is_empty()
	_shade = PackedFloat32Array()
	_shade_offsets = PackedFloat32Array()
	for iv: Vector2 in merged:
		_shade_offsets.append(iv.x)
		_shade_offsets.append(iv.y)
		_shade.append(_sun + asin(clampf(iv.x / _radius, -1.0, 1.0)))
		_shade.append(_sun + asin(clampf(iv.y / _radius, -1.0, 1.0)))
	if had_shade != (not _shade.is_empty()):
		_emit(&"ShadowChanged", merged.size(), 0)
	var strength: float = get_beam_strength()
	var panels: int = 0
	var lit_panels: int = 0
	_income = 0.0
	for slot: int in _slot_building.size():
		var flags: int = _slot_flags[slot] & FLAG_TARGET
		var phi: float = _site_angle(slot) + _rot
		var d: float = phi - _sun
		var y: float = _radius * sin(d)
		if cos(d) > 0.0 and absf(y) <= half_w:
			flags |= FLAG_LIT
			var k: int = 0
			while k + 1 < _shade_offsets.size():
				if y >= _shade_offsets[k] and y <= _shade_offsets[k + 1]:
					flags |= FLAG_SHADED
				k += 2
		_slot_flags[slot] = flags
		var b: int = _slot_building[slot]
		if b >= 0 and float(_buildings[b]["panel_rate"]) > 0.0:
			panels += 1
			if flags & FLAG_LIT and not flags & FLAG_SHADED:
				lit_panels += 1
				_income += float(_stats(_buildings[b], _tiers(slot))["income"]) * strength * _site_mods[4 * slot + 3]
	_lit_fraction = float(lit_panels) / float(panels) if panels > 0 else 0.0


func _interval_before(a: Vector2, b: Vector2) -> bool:
	return a.x < b.x


func _step_towers() -> void:
	for slot: int in _slot_building.size():
		var b: int = _slot_building[slot]
		_slot_flags[slot] &= ~FLAG_TARGET
		if b < 0:
			continue
		var def: Dictionary = _buildings[b]
		var stats: Dictionary = _stats(def, _tiers(slot))  # M2.2: with upgrades
		var fire_rate: float = float(stats["fire_rate"]) * _site_mods[4 * slot + 1]
		if fire_rate <= 0.0:
			continue
		var angle: float = _site_angle(slot) + _rot
		var origin: Vector2 = Vector2.from_angle(angle) * _site_radius(slot)
		var axis: Vector2 = Vector2.from_angle(angle + _slot_aim[slot])
		var arc: float = stats["arc"]
		var tower_range: float = float(stats["range"]) * _site_mods[4 * slot]
		var hit: float = float(stats["damage"]) * _site_mods[4 * slot + 2]
		var best: int = -1
		var best_dist: float = INF
		for i: int in _e_pos.size():
			if _e_hp[i] <= 0.0:
				continue
			var to: Vector2 = _e_pos[i] - origin
			if to.length() > tower_range or absf(axis.angle_to(to)) > arc:
				continue
			var dist: float = _e_pos[i].length()
			if dist < best_dist:
				best_dist = dist
				best = i
		_slot_cooldown[slot] = maxf(_slot_cooldown[slot] - TICK, 0.0)
		if best < 0:
			continue
		_slot_flags[slot] |= FLAG_TARGET
		if _slot_cooldown[slot] > 0.0:
			continue
		_slot_cooldown[slot] = 1.0 / fire_rate
		var target: Vector2 = _e_pos[best]
		var splash: float = stats["splash"]
		if splash > 0.0:
			for i: int in _e_pos.size():
				if _e_pos[i].distance_to(target) <= splash:
					_damage(i, hit)
		else:
			_damage(best, hit)
		_p_from.append(origin)
		_p_to.append(target)
		_p_t.append(0.0)
		_p_dur.append(maxf(origin.distance_to(target) / float(def["shot_speed"]), TICK))
		_p_size.append(4.0 if splash > 0.0 else 2.0)  # radius, as core


func _damage(i: int, amount: float) -> void:
	if _e_hp[i] <= 0.0:
		return
	_e_hp[i] -= amount
	if _e_hp[i] <= 0.0:
		var def: Dictionary = _enemies[_e_def[i]]
		_stardust += float(def["bounty"])
		_emit(&"EnemyKilled", _e_def[i], 0, _e_pos[i])
	else:
		_emit(&"LayerPopped", _e_def[i], 0, _e_pos[i])


func _step_projectiles() -> void:
	var i: int = _p_t.size() - 1
	while i >= 0:
		_p_t[i] += TICK
		if _p_t[i] >= _p_dur[i]:
			var last: int = _p_t.size() - 1
			_p_from[i] = _p_from[last]
			_p_to[i] = _p_to[last]
			_p_t[i] = _p_t[last]
			_p_dur[i] = _p_dur[last]
			_p_size[i] = _p_size[last]
			_p_from.resize(last)
			_p_to.resize(last)
			_p_t.resize(last)
			_p_dur.resize(last)
			_p_size.resize(last)
		i -= 1


func _compact_enemies() -> void:
	var i: int = _e_pos.size() - 1
	var removed: bool = false
	while i >= 0:
		if _e_hp[i] <= 0.0:
			var last: int = _e_pos.size() - 1
			_e_pos[i] = _e_pos[last]
			_e_prev[i] = _e_prev[last]
			_e_def[i] = _e_def[last]
			_e_hp[i] = _e_hp[last]
			_e_state[i] = _e_state[last]
			_e_timer[i] = _e_timer[last]
			_e_pos.resize(last)
			_e_prev.resize(last)
			_e_def.resize(last)
			_e_hp.resize(last)
			_e_state.resize(last)
			_e_timer.resize(last)
			removed = true
		i -= 1
	if removed:
		_update_light()


func _check_wave_end() -> void:
	if _phase != PHASE_WAVE or not _spawn_queue.is_empty() or not _e_pos.is_empty():
		return
	_emit(&"WaveCleared", _wave_index, 0)
	_wave_index += 1
	_p_t.clear()
	_p_from.clear()
	_p_to.clear()
	_p_dur.clear()
	_p_size.clear()
	if _wave_index >= _waves.size():
		_phase = PHASE_WON
		_emit(&"Victory", _wave_index, 0)
	else:
		_phase = PHASE_INTERMISSION
	_update_light()


# ---------------------------------------------------------------- content

func _load_fallback_content() -> void:
	_buildings = [
		_building(0, "cannon", "tower", 90.0, 25.0, 450.0, 0.8, 3.0, 40.0, 600.0, 0.0, "square"),
		_building(1, "gatling", "tower", 50.0, 40.0, 350.0, 6.0, 1.0, 0.0, 900.0, 0.0, "triangle"),
		_building(2, "solar_panel", "panel", 40.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 3.0, "diamond"),
	]
	_enemies = [
		_enemy(0, "dart", "triangle", "tier.red", 1, 1.0, 110.0, 6.0, 1.0, 1.0, false),
		_enemy(1, "drifter_red", "circle", "tier.red", 1, 1.0, 40.0, 8.0, 1.0, 1.0, false),
		_enemy(2, "eclipser", "disc", "enemy.eclipser", 1, 40.0, 25.0, 28.0, 15.0, 15.0, true),
	]
	_lanes = {"A": deg_to_rad(30.0), "B": deg_to_rad(170.0)}
	var a: float = _lanes["A"]
	var b: float = _lanes["B"]
	_waves = [
		{"light_mult": 1.0, "spawn_seconds": 15.0, "exact_ahead": 1, "icons_ahead": 3,
			"spawns": [{"enemy": 1, "count": 8, "bearing": a}]},
		{"light_mult": 1.0, "spawn_seconds": 16.0, "exact_ahead": 1, "icons_ahead": 3,
			"spawns": [{"enemy": 1, "count": 10, "bearing": a}, {"enemy": 0, "count": 4, "bearing": b}]},
		{"light_mult": 0.5, "spawn_seconds": 18.0, "exact_ahead": 1, "icons_ahead": 3,
			"spawns": [{"enemy": 1, "count": 12, "bearing": a}, {"enemy": 2, "count": 1, "bearing": b}]},
	]


func _building(
		index: int, id: String, kind: String, cost: float, arc_deg: float, tower_range: float,
		fire_rate: float, damage: float, splash: float, shot_speed: float, panel_rate: float,
		shape: String
) -> Dictionary:
	return {
		"index": index, "id": id, "name": id.capitalize(), "kind": kind, "cost": cost,
		"arc": deg_to_rad(arc_deg), "range": tower_range, "fire_rate": fire_rate,
		"damage": damage, "splash": splash, "shot_speed": maxf(shot_speed, 1.0),
		"panel_rate": panel_rate, "shape": int(SHAPES.get(shape, 0)),
		"color": "building." + id if kind == "panel" else "tower." + id,
		# M2.3: towers declare a damage type (default Kinetic), -1 for non-shooters.
		"damage_type": 0 if kind == "tower" else -1,
		"damage_type_name": "kinetic" if kind == "tower" else "",
		# M2.1: slots covered, where it may stand, adjacency auras.
		"footprint": 1, "layer": LAYER_ANY if kind == "tower" else LAYER_GROUND, "auras": [],
		"precision": false,  # M2.4
	}


func _enemy(
		index: int, id: String, shape: String, color: String, layers: int, hp: float,
		speed: float, size: float, land_damage: float, bounty: float, shadow_caster: bool
) -> Dictionary:
	var tier: int = int(TIERS.get(color, 0))
	return {
		"index": index, "id": id, "name": id.capitalize(), "shape": int(SHAPES.get(shape, 0)),
		"tier": tier, "color": color, "layers": layers, "hp": hp,
		"hp_per_layer": hp / float(maxi(layers, 1)), "speed": speed, "size": size,
		"land_damage": land_damage, "bounty": bounty, "shadow_caster": shadow_caster,
		"hp_bar": shadow_caster,
		# M2.3
		"traits": TRAIT_LAYERED if layers > 1 else 0, "shield": 0.0, "pops_into": -1,
		"hp_bar_segments": 4 if shadow_caster else 1,
		"damage_mult": PackedFloat32Array([1.0, 1.0, 1.0, 1.0, 1.0, 1.0]),
		"weak_point": {},  # M2.4: {mult, body_mult, window} when it has one
	}


func _read_json(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if parsed is Dictionary:
		return parsed as Dictionary
	return {}


func _read_dir(sub: String) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	var dir_path: String = _content_dir.path_join(sub)
	var names: PackedStringArray = DirAccess.get_files_at(dir_path)
	names.sort()
	for file_name: String in names:
		if file_name.ends_with(".json"):
			var d: Dictionary = _read_json(dir_path.path_join(file_name))
			if not d.is_empty():
				out.append(d)
	return out


func _load_json_content(errors: PackedStringArray) -> void:
	var waves_list: Array[Dictionary] = _read_dir("waves")
	if waves_list.is_empty():
		errors.append("mock: no wave set under %s, using the built-in sample" % _content_dir)
		return
	var wave_set: Dictionary = waves_list[0]
	var planet: Dictionary = _read_json(
		_content_dir.path_join("planets/%s.json" % str(wave_set.get("planet", "medium"))))
	var sun: Dictionary = _read_json(
		_content_dir.path_join("sun/%s.json" % str(wave_set.get("sun", "default"))))
	var rules: Dictionary = _read_json(_content_dir.path_join("rules.json"))
	_min_damage_mult = maxf(float(rules.get("minDamageMult", 0.25)), 0.25)
	_load_damage_rules()
	_spawn_radius = float(rules.get("spawnRadius", 900.0))
	_exclusive_tier = int(rules.get("upgradeExclusiveTier", 2))  # M2.2
	_surface_ease = float(rules.get("surfaceTimeEaseSeconds", 0.3))
	_elevated_bonus = float(rules.get("elevatedRangeBonus", 0.15))
	_weak_window_deg = float(rules.get("weakWindowDeg", 8.0))  # M2.4
	if not planet.is_empty():
		_radius = float(planet.get("radius", 100.0))
		_slot_count = int(planet.get("slots", 36))
		_hp_max = float(planet.get("hp", 100.0))
		_rot_max = deg_to_rad(float(planet.get("rotMaxSpeed", 90.0)))
		_rot_accel = deg_to_rad(float(planet.get("rotAccel", 360.0)))
		_rot_damping = float(planet.get("rotDamping", 4.0))
		_start_stardust = float(planet.get("startStardust", 150.0))
		_sell_refund = float(planet.get("sellRefund", 0.7))
		_surface_time_scale = float(planet.get("surfaceTimeScale", 0.75))
		_layer_count = 2 if bool(planet.get("elevatedLayer", true)) else 1
		_elevated_height = float(planet.get("elevatedHeight", 0.1 * _radius))
		_lanes = {}
		var lanes: Array = planet.get("lanes", [])
		for lane: Dictionary in lanes:
			_lanes[str(lane.get("id", ""))] = deg_to_rad(float(lane.get("bearingDeg", 0.0)))
	if not sun.is_empty():
		_sun_start = deg_to_rad(float(sun.get("startDeg", 0.0)))
		_sun_speed = deg_to_rad(float(sun.get("speedDegPerSec", 1.5)))
		_beam_width = float(sun.get("beamWidth", 141.0))
		_base_strength = float(sun.get("baseStrength", 1.0))
	var building_list: Array[Dictionary] = _read_dir("buildings")
	if not building_list.is_empty():
		_buildings = []
		for d: Dictionary in building_list:
			var shot: Dictionary = {}
			if d.get("projectile") is Dictionary:
				shot = d["projectile"]
			var visual: Dictionary = d.get("visual", {})
			var b: Dictionary = _building(
				_buildings.size(), str(d.get("id", "?")), str(d.get("kind", "tower")),
				float(d.get("cost", 0.0)), float(d.get("arcDeg", 0.0)), float(d.get("range", 0.0)),
				float(d.get("fireRate", 0.0)), float(shot.get("damage", 0.0)),
				float(shot.get("splash", 0.0)), float(shot.get("speed", 0.0)),
				float(d.get("panelRate", 0.0)), str(visual.get("shape", "square")))
			b["color"] = str(visual.get("color", ""))
			if b["kind"] == "tower":
				var type_name: String = str(shot.get("type", "kinetic"))
				b["damage_type"] = maxi(DAMAGE_TYPES.find(type_name), 0)
				b["damage_type_name"] = DAMAGE_TYPES[int(b["damage_type"])]
			b["footprint"] = clampi(int(d.get("footprint", 1)), 1, 3)
			b["precision"] = bool(d.get("precision", false))  # M2.4
			b["layer"] = int(LAYER_NAMES.get(str(d.get("layer", "ground")), LAYER_GROUND))
			var auras: Array[Dictionary] = []
			var raw_auras: Array = d.get("auras", [])
			for raw: Dictionary in raw_auras:
				var kinds: int = 0
				var affects: Array = raw.get("affects", ["tower", "panel", "support"])
				for kind_name: Variant in affects:
					kinds |= int(KIND_BITS.get(str(kind_name), 0))
				auras.append({
					"stat": int(AURA_STATS.get(str(raw.get("stat", "range")), 0)),
					"amount": float(raw.get("amount", 0.0)),
					"radius": int(raw.get("radius", 1)), "kinds": kinds,
				})
			b["auras"] = auras
			b["paths"] = _read_paths(d.get("paths", []))  # M2.2
			_buildings.append(b)
	var enemy_ids: Dictionary[String, int] = {}
	var enemy_list: Array[Dictionary] = _read_dir("enemies")
	if not enemy_list.is_empty():
		_enemies = []
		for d: Dictionary in enemy_list:
			var visual: Dictionary = d.get("visual", {})
			var layers: int = int(d.get("layers", 1))
			var hp: float = float(d.get("hp", float(layers) * float(d.get("hpPerLayer", 1.0))))
			var e: Dictionary = _enemy(
				_enemies.size(), str(d.get("id", "?")), str(d.get("shape", "circle")),
				str(visual.get("color", "tier.red")), layers, hp, float(d.get("speed", 40.0)),
				float(d.get("size", 8.0)),
				float(d.get("landDamagePerLayer", d.get("landDamage", 1.0))),
				float(d.get("bounty", 1.0)), bool(d.get("shadowCaster", false)))
			e["hp_bar"] = bool(d.get("hpBar", false))
			# M2.3
			var trait_names: Array = d.get("traits", [])
			for trait_name: Variant in trait_names:
				var bit: int = TRAIT_IDS.find(str(trait_name))
				if bit >= 0:
					e["traits"] = int(e["traits"]) | (1 << bit)
			e["shield"] = float(d.get("shield", 0.0))
			if float(e["shield"]) > 0.0:
				e["traits"] = int(e["traits"]) | (1 << TRAIT_IDS.find("shielded"))
			e["hp_bar_segments"] = int(d.get("hpBarSegments", 1))
			e["damage_mult"] = _type_mults(d.get("damageMult"))
			# M2.4 (the mock does not simulate weak-point damage, only the hint).
			if d.get("weakPoint") is Dictionary:
				var wp: Dictionary = d["weakPoint"]
				e["weak_point"] = {
					"mult": float(wp.get("mult", 3.5)), "body_mult": float(wp.get("bodyMult", 0.25)),
					"window": deg_to_rad(float(wp.get("windowDeg", _weak_window_deg))),
				}
				e["traits"] = int(e["traits"]) | (1 << TRAIT_IDS.find("bulwark"))
			e["_pops_into_id"] = str(d.get("popsInto", "")) if d.get("popsInto") != null else ""
			_enemies.append(e)
	for e: Dictionary in _enemies:
		enemy_ids[str(e["id"])] = int(e["index"])
	for e: Dictionary in _enemies:
		e["pops_into"] = int(enemy_ids.get(str(e.get("_pops_into_id", "")), -1))
	_waves = []
	var waves: Array = wave_set.get("waves", [])
	for w: Dictionary in waves:
		var forecast: Dictionary = w.get("forecast", {})
		var spawns: Array[Dictionary] = []
		var raw_spawns: Array = w.get("spawns", [])
		for sp: Dictionary in raw_spawns:
			var enemy_id: String = str(sp.get("enemy", ""))
			if not enemy_ids.has(enemy_id):
				errors.append("mock: unknown enemy '%s'" % enemy_id)
				continue
			spawns.append({
				"enemy": enemy_ids[enemy_id], "count": int(sp.get("count", 1)),
				"bearing": float(_lanes.get(str(sp.get("lane", "")), 0.0)),
			})
		_waves.append({
			"light_mult": float(w.get("lightMult", 1.0)),
			"spawn_seconds": float(w.get("spawnSeconds", 15.0)),
			"exact_ahead": int(forecast.get("exactAhead", 1)),
			"icons_ahead": int(forecast.get("iconsAhead", 3)),
			"spawns": spawns,
		})
