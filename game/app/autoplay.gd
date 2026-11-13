class_name Autoplay
extends RefCounted
## Playtest bot for screenshots and smoke tests (`--play=sensible|towers|none`).
## It plays through the SimServer contract only, like a player: builds in the
## intermissions, then starts the wave. A GDScript cousin of the "sensible" bot in
## core/tests/balance_bots.h (without its small turns for light); the balance
## numbers are judged by the core bots, not by this one.

const PHASE_INTERMISSION: int = 0
const LANE_SPREAD: Array[int] = [0, 1, -1, 2, -2, 3]
const WANT_A: Array[int] = [1, 1, 1, 1, 2, 2, 3, 3]
const WANT_B: Array[int] = [0, 1, 1, 1, 1, 2, 2, 3]
const WANT_PANELS: Array[int] = [1, 2, 4, 5, 5, 6, 6, 4]

var sim: Node
var mode: String = "sensible"
var _gatling: Dictionary = {}
var _cannon: Dictionary = {}
var _panel: Dictionary = {}
var _lanes: Array[Array] = []
var _reserved: Dictionary[int, bool] = {}
var _budget: float = 0.0
var _taken: Dictionary[int, bool] = {}
var _towers_bought: int = 0
var _planned_wave: int = -1


func setup(sim_node: Node, play_mode: String) -> void:
	sim = sim_node
	mode = play_mode
	var towers: Array[Dictionary] = []
	for def: Dictionary in sim.get_building_defs():
		if str(def.get("kind", "")) == "panel":
			_panel = def
		else:
			towers.append(def)
	towers.sort_custom(func(x: Dictionary, y: Dictionary) -> bool:
		return float(x.get("cost", 0.0)) < float(y.get("cost", 0.0)))
	# The cheap tower is the bread and butter; the dearer one alternates with it.
	if not towers.is_empty():
		_gatling = towers[0]
	if towers.size() > 1:
		_cannon = towers[1]
	# Tower slots under each lane, from the forecast's lane bearings (rotation 0).
	var n: int = sim.get_slot_count()
	var bearings: Array[float] = []
	for wave: Dictionary in sim.get_forecast(64):
		var entries: Array = wave.get("entries", [])
		for entry: Dictionary in entries:
			var bearing: float = float(entry.get("lane_bearing", 0.0))
			if not bearings.has(bearing):
				bearings.append(bearing)
	bearings.sort()
	for bearing: float in bearings:
		var centre: int = roundi(bearing / TAU * float(n))
		var slots: Array[int] = []
		for offset: int in LANE_SPREAD:
			var slot: int = posmod(centre + offset, n)
			slots.append(slot)
			_reserved[slot] = true
		_lanes.append(slots)


## Call every step: plans once per intermission and starts the wave.
func act() -> void:
	if int(sim.get_phase()) != PHASE_INTERMISSION:
		return
	var wave: int = sim.get_wave_index()
	if wave == _planned_wave:
		return
	_planned_wave = wave
	_budget = sim.get_stardust()
	_taken.clear()
	var buildings: PackedInt32Array = sim.get_slot_buildings()
	for slot: int in buildings.size():
		if buildings[slot] >= 0:
			_taken[slot] = true
	if mode == "sensible":
		_plan_sensible(wave, buildings)
	elif mode == "towers":
		_plan_towers()
	sim.start_wave()


func _plan_towers() -> void:
	if _lanes.size() < 2:
		return
	for i: int in 16:
		var a: bool = _towers_on(_lanes[0]) <= _towers_on(_lanes[1])
		if not _buy_tower(_lanes[0] if a else _lanes[1]):
			break


func _plan_sensible(wave: int, buildings: PackedInt32Array) -> void:
	if _lanes.size() < 2 or _panel.is_empty():
		return
	var n: int = buildings.size()
	var k: int = mini(wave, WANT_A.size() - 1)
	var sun: float = sim.get_sun_bearing()
	var radius: float = maxf(sim.get_planet_radius(), 1.0)
	var half: float = asin(clampf(float(sim.get_beam_width()) / (2.0 * radius), 0.0, 1.0))
	var panel_index: int = int(_panel["index"])
	var gatling_index: int = int(_gatling["index"])
	# Panels and sky guns the sun has left behind are sold.
	for slot: int in n:
		var behind: float = angle_difference(TAU * float(slot) / float(n), sun)
		var stale_panel: bool = buildings[slot] == panel_index and behind > half + deg_to_rad(20.0)
		var stale_gun: bool = (buildings[slot] == gatling_index and not _reserved.has(slot)
			and behind > half)
		if stale_panel or stale_gun:
			sim.sell_building(slot)
			_taken.erase(slot)
	while _towers_on(_lanes[0]) < WANT_A[k] and _buy_tower(_lanes[0]):
		pass
	while _towers_on(_lanes[1]) < WANT_B[k] and _buy_tower(_lanes[1]):
		pass
	# A shadow caster is forecast: a Gatling under the sun shoots it down.
	var forecast: Array[Dictionary] = sim.get_forecast(1)
	var caster: bool = false
	if not forecast.is_empty():
		var entries: Array = forecast[0].get("entries", [])
		for entry: Dictionary in entries:
			caster = caster or bool(entry.get("shadow_caster", false))
	if caster:
		var gun_slot: int = _nearest_free(sun + deg_to_rad(20.0), deg_to_rad(25.0), n)
		if gun_slot >= 0:
			_buy(gun_slot, _gatling)
	# Light: new panels toward the leading edge of the lit arc.
	var lit: int = 0
	for slot: int in n:
		if buildings[slot] == panel_index and _taken.has(slot):
			if angle_difference(sun, TAU * float(slot) / float(n)) > -half + deg_to_rad(30.0):
				lit += 1
	while lit < WANT_PANELS[k]:
		var slot: int = _nearest_free(sun + half - deg_to_rad(8.0), half, n)
		if slot < 0 or not _buy(slot, _panel):
			break
		lit += 1
	for i: int in 8:
		var a: bool = _towers_on(_lanes[0]) <= _towers_on(_lanes[1])
		if not _buy_tower(_lanes[0] if a else _lanes[1]) and not _buy_tower(_lanes[1] if a else _lanes[0]):
			break


func _towers_on(slots: Array) -> int:
	var count: int = 0
	for slot: int in slots:
		if _taken.has(slot):
			count += 1
	return count


func _buy(slot: int, def: Dictionary) -> bool:
	var cost: float = float(def.get("cost", 0.0))
	if def.is_empty() or _taken.has(slot) or cost > _budget:
		return false
	_budget -= cost
	_taken[slot] = true
	sim.place_building(slot, int(def["index"]), 0.0)
	return true


func _buy_tower(slots: Array) -> bool:
	for slot: int in slots:
		if _taken.has(slot):
			continue
		var want: Dictionary = _cannon if (_towers_bought % 2 == 1 and not _cannon.is_empty()) else _gatling
		if _buy(slot, want) or _buy(slot, _gatling):
			_towers_bought += 1
			return true
		return false
	return false


func _nearest_free(local: float, max_off: float, n: int) -> int:
	var best: int = -1
	var best_off: float = max_off
	for slot: int in n:
		if _taken.has(slot) or _reserved.has(slot):
			continue
		var off: float = absf(angle_difference(local, TAU * float(slot) / float(n)))
		if off < best_off:
			best_off = off
			best = slot
	return best
