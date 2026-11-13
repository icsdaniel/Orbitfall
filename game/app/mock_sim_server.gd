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
const SHAPES: Dictionary[String, int] = {
	"circle": 0, "disc": 0, "triangle": 1, "square": 2, "hexagon": 3, "star": 4,
	"diamond": 5, "ring": 6,
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

var _buildings: Array[Dictionary] = []
var _enemies: Array[Dictionary] = []
var _waves: Array[Dictionary] = []
var _slot_building: PackedInt32Array = PackedInt32Array()
var _slot_flags: PackedInt32Array = PackedInt32Array()
var _slot_cooldown: PackedFloat32Array = PackedFloat32Array()
var _slot_aim: PackedFloat32Array = PackedFloat32Array()

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


func place_building(slot: int, building: int, aim_offset: float = 0.0) -> void:
	_record(&"place_building", [slot, building, aim_offset])
	if slot < 0 or slot >= _slot_count or building < 0 or building >= _buildings.size():
		_emit(&"CommandRejected", slot, building)
		return
	var cost: float = _buildings[building]["cost"]
	if _slot_building[slot] != -1 or cost > _stardust or _phase >= PHASE_WON:
		_emit(&"CommandRejected", slot, building)
		return
	_stardust -= cost
	_slot_building[slot] = building
	_slot_aim[slot] = aim_offset
	_slot_cooldown[slot] = 0.0
	_emit(&"BuildingPlaced", slot, building)
	_emit(&"StardustChanged", int(_stardust), 0)
	_update_light()


func sell_building(slot: int) -> void:
	_record(&"sell_building", [slot])
	if slot < 0 or slot >= _slot_count or _slot_building[slot] == -1:
		_emit(&"CommandRejected", slot, -1)
		return
	var cost: float = _buildings[_slot_building[slot]]["cost"]
	_stardust += floorf(cost * _sell_refund)
	_emit(&"BuildingSold", slot, _slot_building[slot])
	_slot_building[slot] = -1
	_emit(&"StardustChanged", int(_stardust), 0)
	_update_light()


func set_aim(slot: int, offset: float) -> void:
	_record(&"set_aim", [slot, offset])
	if slot >= 0 and slot < _slot_count:
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
	return _slot_building


func get_slot_flags() -> PackedInt32Array:
	return _slot_flags


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
		var size: float = float(def["size"]) * 2.0
		var tier: int = int(def["tier"])
		if int(def["layers"]) > 1 or not bool(def["hp_bar"]):
			tier = clampi(int(ceilf(_e_hp[i] / float(def["hp_per_layer"]))) - 1, 0, 7)
		var xform: Transform2D = Transform2D(
			(-pos).angle() + PI * 0.5, Vector2(size, size), 0.0, pos)
		BatchRenderer.write_instance(
			_enemy_buffer, i, xform, Color.WHITE,
			BatchRenderer.make_custom(int(def["shape"]), tier, 0))
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
			})
	return out


func get_building_defs() -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	for b: Dictionary in _buildings:
		out.append({
			"index": b["index"], "id": b["id"], "name": b["name"], "cost": b["cost"],
			"arc": b["arc"], "range": b["range"], "kind": b["kind"], "shape": b["shape"],
			"color": b["color"],
		})
	return out


func get_enemy_defs() -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	for e: Dictionary in _enemies:
		out.append({
			"index": e["index"], "id": e["id"], "name": e["name"], "shape": e["shape"],
			"tier": e["tier"], "color": e["color"], "size": e["size"],
			"shadow_caster": e["shadow_caster"],
		})
	return out


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
	_slot_building.resize(_slot_count)
	_slot_building.fill(-1)
	_slot_flags.resize(_slot_count)
	_slot_flags.fill(0)
	_slot_cooldown.resize(_slot_count)
	_slot_cooldown.fill(0.0)
	_slot_aim.resize(_slot_count)
	_slot_aim.fill(0.0)
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
	for slot: int in _slot_count:
		var flags: int = _slot_flags[slot] & FLAG_TARGET
		var phi: float = TAU * float(slot) / float(_slot_count) + _rot
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
				_income += float(_buildings[b]["panel_rate"]) * strength
	_lit_fraction = float(lit_panels) / float(panels) if panels > 0 else 0.0


func _interval_before(a: Vector2, b: Vector2) -> bool:
	return a.x < b.x


func _step_towers() -> void:
	for slot: int in _slot_count:
		var b: int = _slot_building[slot]
		_slot_flags[slot] &= ~FLAG_TARGET
		if b < 0:
			continue
		var def: Dictionary = _buildings[b]
		var fire_rate: float = def["fire_rate"]
		if fire_rate <= 0.0:
			continue
		var angle: float = TAU * float(slot) / float(_slot_count) + _rot
		var origin: Vector2 = Vector2.from_angle(angle) * _radius
		var axis: Vector2 = Vector2.from_angle(angle + _slot_aim[slot])
		var arc: float = def["arc"]
		var tower_range: float = def["range"]
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
		var splash: float = def["splash"]
		if splash > 0.0:
			for i: int in _e_pos.size():
				if _e_pos[i].distance_to(target) <= splash:
					_damage(i, float(def["damage"]))
		else:
			_damage(best, float(def["damage"]))
		_p_from.append(origin)
		_p_to.append(target)
		_p_t.append(0.0)
		_p_dur.append(maxf(origin.distance_to(target) / float(def["shot_speed"]), TICK))
		_p_size.append(1.8 if splash > 0.0 else 1.0)


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
	_spawn_radius = float(rules.get("spawnRadius", 900.0))
	if not planet.is_empty():
		_radius = float(planet.get("radius", 100.0))
		_slot_count = int(planet.get("slots", 36))
		_hp_max = float(planet.get("hp", 100.0))
		_rot_max = deg_to_rad(float(planet.get("rotMaxSpeed", 90.0)))
		_rot_accel = deg_to_rad(float(planet.get("rotAccel", 360.0)))
		_rot_damping = float(planet.get("rotDamping", 4.0))
		_start_stardust = float(planet.get("startStardust", 150.0))
		_sell_refund = float(planet.get("sellRefund", 0.7))
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
			_enemies.append(e)
	for e: Dictionary in _enemies:
		enemy_ids[str(e["id"])] = int(e["index"])
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
