#pragma once
// SimServer: the only bridge between GDScript and core/ (CONTRACT.md, BRIEF §5.3).
// GDScript sends commands and reads snapshots; it never simulates.
//
// Threading model
//   * The Sim is touched by exactly one thread at a time: the sim thread while
//     it has ticks to run, the main thread only while the sim thread is idle
//     (wait_idle) or stopped. The core stays single-threaded.
//   * The main thread decides how many ticks exist (target tick) in both modes
//     and stamps every command with the target tick at the moment it is sent,
//     so threaded and single-threaded runs apply the same commands on the same
//     ticks and give equal state hashes.
//   * After each tick the sim fills its back Snapshot and swaps it with the
//     `ready` one under a short lock; the main thread latches `ready` into
//     prev/curr once per frame (no copies, four buffers in rotation).
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#ifndef ORBITFALL_SINGLE_THREADED
#include <thread>
#endif

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "content/content.h"
#include "sim/sim.h"
#include "snapshot.h"

namespace godot {

class SimServer : public Node {
  GDCLASS(SimServer, Node)

 public:
  SimServer();
  ~SimServer() override;

  void _process(double delta) override;

  // M0.1 smoke values.
  String core_version() const;
  int core_answer() const;

  // --- lifecycle ---
  PackedStringArray load_content(const String& dir);
  // M2.8: `planet_id` picks the home planet ("" = the wave set's own, medium).
  // An unknown id, or a planet without the wave set's lanes, is an error and
  // falls back to the wave set's planet. Old two-argument calls are unchanged.
  void new_run(int64_t seed, int64_t wave_set, const String& planet_id);
  void set_threaded(bool on);
  bool is_threaded() const;
  void step_ticks(int64_t n);
  // What _process calls: real seconds in, whole 30 Hz ticks out. Returns the ticks scheduled.
  int64_t advance(double delta);
  float get_alpha() const { return alpha_; }

  // --- commands ---
  void rotate_input(float v);
  // `layer` (M2.1): 0 ground (default), 1 elevated. The command carries the
  // site index slot + layer * get_slot_count(); sell/aim/targeting take that
  // index too (so M1 slot numbers still mean the ground ring).
  void place_building(int64_t slot, int64_t building, float aim_offset, int64_t layer);
  void sell_building(int64_t slot);
  void set_aim(int64_t slot, float offset);
  void set_targeting(int64_t slot, int64_t mode);
  void start_wave();
  void set_time_scale(float scale, float ease_seconds);
  PackedByteArray get_command_log() const;
  bool replay(int64_t seed, const PackedByteArray& log);

  // --- snapshot getters ---
  int64_t get_tick() const { return curr_->tick; }
  int64_t get_state_hash();
  int64_t get_phase() const { return curr_->phase; }
  int64_t get_wave_index() const { return curr_->waveIndex; }
  int64_t get_wave_count() const { return statics_.waveCount; }
  float get_stardust() const { return curr_->stardust; }
  float get_planet_hp() const { return curr_->planetHp; }
  float get_planet_hp_max() const { return statics_.planetHpMax; }
  float get_time_scale() const { return curr_->timeScale; }
  float get_planet_radius() const { return statics_.planetRadius; }
  float get_planet_rotation() const;
  float get_planet_angular_velocity() const { return curr_->planetAngVel; }
  int64_t get_slot_count() const { return statics_.slotCount; }
  PackedInt32Array get_slot_buildings() const;
  PackedInt32Array get_slot_flags() const;
  float get_sun_bearing() const { return curr_->sunBearing; }
  float get_beam_width() const { return curr_->beamWidth; }
  float get_beam_strength() const { return curr_->beamStrength; }
  PackedFloat32Array get_shade_intervals() const;
  float get_income_per_second() const { return curr_->incomePerSecond; }
  PackedFloat32Array get_enemy_buffer();
  int64_t get_enemy_count() const { return curr_->enemyCount; }
  PackedFloat32Array get_projectile_buffer();
  int64_t get_projectile_count() const { return curr_->projectileCount; }
  TypedArray<Dictionary> get_large_units() const;
  TypedArray<Dictionary> get_building_defs() const;
  // ---- M2.1 placement depth -------------------------------------------------
  // Sites: one index space over both rings, site = slot + layer * slot_count.
  // The M1 slot getters keep returning the ground ring only.
  int64_t get_layer_count() const { return statics_.layerCount; }  // 1, or 2 with an elevated ring
  float get_elevated_height() const { return statics_.elevatedHeight; }  // world units above the surface
  float get_elevated_range_bonus() const { return content_.rules.elevatedRangeBonus; }  // 0.15 = +15%
  PackedInt32Array get_site_buildings() const;   // per site: building index on the anchor slot, else -1
  PackedInt32Array get_site_flags() const;       // per site: same bits as get_slot_flags
  PackedInt32Array get_site_owners() const;      // per site: anchor site of the covering building, -1 = free
  PackedFloat32Array get_site_modifiers() const; // 4 per site: range, fire rate, damage, income multipliers
  PackedInt32Array get_adjacency_links() const;  // triples: source site, target site, stat (0 range, 1 fire rate, 2 damage, 3 income)
  // ---- end M2.1 -------------------------------------------------------------
  TypedArray<Dictionary> get_enemy_defs() const;
  TypedArray<Dictionary> get_forecast(int64_t count);
  // --- M2.3 damage types and traits ---
  // Damage type names in engine order ("kinetic", "explosive", ...).
  PackedStringArray get_damage_types() const;
  // Trait legend and soft-counter matrix from content/damage.json, one entry
  // per trait bit: {bit, mask, id, name, cue, effect, easier_with,
  // mult: PackedFloat32Array per damage type}.
  TypedArray<Dictionary> get_trait_defs() const;
  // Multiplier on damage of each type dealt to a shield.
  PackedFloat32Array get_shield_mults() const;
  // --- end M2.3 ---
  // --- M2.8 home planets ---
  // Every planet in content order, with the stats the picker shows:
  // {index, id, name, radius, slots, hp, rot_max_speed (rad/s), rot_accel
  // (rad/s^2), lit_arc (rad, under wave set 0's sun), lit_fraction (0..1),
  // color (palette key), default (true for wave set 0's own planet), fits
  // (can host wave set 0)}.
  TypedArray<Dictionary> get_planet_defs() const;
  // Id of the current run's planet ("" before the first new_run).
  String get_planet_id() const;
  // --- end M2.8 ---
  // --- M2.10 falling objects and shields ---
  // Falling objects (meteors, bombs) in the BatchRenderer layout, interpolated
  // like the enemies: custom = shape id, colour tier, 0, 0.
  PackedFloat32Array get_falling_buffer();
  int64_t get_falling_count() const { return curr_->fallingCount; }
  // Objects within get_falling_warn_seconds() of impact: {pos: Vector2 (now),
  // impact: Vector2 (on the surface), bearing (world rad), eta (s), damage,
  // falling (def index), shield_slot (the online shield covering that bearing
  // now, or -1)}.
  TypedArray<Dictionary> get_falling_warnings() const;
  float get_falling_warn_seconds() const;
  // Placed shields: {slot, angle (world rad, interpolated), half_width (rad),
  // dome_radius, energy, energy_max, energy_frac, online, charge_frac
  // (recharge progress while offline), lit (charging lightChargeMult x faster)}.
  TypedArray<Dictionary> get_shields() const;
  // {index, id, name, damage, speed, size, spawn_radius, shape, color}.
  TypedArray<Dictionary> get_falling_defs() const;
  // Wave set ids in new_run(seed, wave_set) order (m1 is 0; m2_storm is the demo).
  PackedStringArray get_wave_sets() const;
  // --- end M2.10 ---
  Dictionary get_debug_stats() const;
  bool set_tunable(const String& path, float value);
  TypedArray<Dictionary> drain_events();
  // Slow time in the Surface band (planet surfaceTimeScale, rules
  // surfaceTimeEaseSeconds): the client sends them back as set_time_scale.
  float get_surface_time_scale() const;
  float get_surface_time_ease_seconds() const;

  // --- M2.7 drills, instability and coolers (begin) ---
  // Global instability: {level, threshold, load (summed weight of the running
  // drills), cooling (coolers right now, light-aware), drills, running,
  // overheated, coolers, drill_income (stardust/s), overheat_seconds}.
  // level > threshold means running drills heat up and, at heat 1, overheat.
  Dictionary get_instability() const;
  // One entry per drill, site order: {site, building, heat (0..1),
  // overheated (bool), cooldown (seconds left), cooldown_max}.
  TypedArray<Dictionary> get_drill_states() const;
  // Stardust per second from running drills (panels: get_income_per_second).
  float get_drill_income_per_second() const { return curr_->drillIncomePerSecond; }
  // --- M2.7 drills, instability and coolers (end) ---

  // --- M2.2 upgrade paths (begin) ---
  // Command: buys the next tier of `path` (0 or 1) for the building on `slot`.
  // Emits BuildingUpgraded {a: slot, b: path, x: new tier, value: cost} or
  // CommandRejected {a: slot, b: reason (4 Unaffordable with value = cost,
  // 11 UpgradeBlocked), x: 8}.
  void upgrade_building(int64_t slot, int64_t path);
  // Bought tier per path of the building on `slot` ([0, 0] when empty or invalid).
  PackedInt32Array get_slot_tiers(int64_t slot) const;
  // Next-tier preview for one path of the building on `slot`; empty when the slot
  // or path does not exist. Keys: path, id, name, tier, max_tier, cap (highest
  // tier reachable given the other path), has_next, next_name, cost, affordable,
  // can_upgrade, locked (capped by the other path), before / after (stats
  // dictionaries: damage, fire_rate, range, arc (rad), splash, income) and
  // changes ([{stat, before, after}] for the stats the tier changes).
  Dictionary get_upgrade_preview(int64_t slot, int64_t path) const;
  // Everything the upgrade UI needs for `slot` in one call; empty when the slot
  // is empty. Keys: slot, building, tiers, stats, spent (on upgrades),
  // sell_refund, paths (one get_upgrade_preview dictionary per path).
  Dictionary get_slot_upgrades(int64_t slot) const;
  // --- M2.2 upgrade paths (end) ---

  // --- M2.6 towers batch 1 (begin) ---
  // Command: fires the active ability (Sun Lens Overcharge) of the building on
  // `slot` (a site). Emits BuildingActivated {a: anchor site, b: building,
  // value: cooldown s} or CommandRejected {b: 5 EmptySlot, 6 NotAllowed (no
  // active), 12 ActiveNotReady; x: 9}.
  void activate_building(int64_t slot);
  // Lit, unshaded fraction of the solar panel capacity (0..1): what the Sun Lens channels.
  float get_channel() const { return curr_->channel; }
  // Sun Lens beams this tick, 7 floats each: x0, y0 (tower), x1, y1 (target),
  // power (ramp x channel x overcharge; 1 = full beam), overcharged (0/1), site.
  PackedFloat32Array get_beams() const;
  // 2 floats per site: active cooldown left (s), active time left (s); zeros
  // for buildings without an active.
  PackedFloat32Array get_site_actives() const;
  // --- M2.6 towers batch 1 (end) ---

  // --- M2.11 bent aim and focus fire (begin) ---
  // set_aim(slot, offset) (above) snaps to get_aim_rules().offsets, costs
  // reaim_cost_frac of the building's cost and is refused mid-wave: emits
  // AimChanged {a: site, b: building, x: offset rad, value: cost} or
  // CommandRejected {b: 4 Unaffordable / 6 NotAllowed / 7 WrongPhase, x: 3}.
  // Per site: the aim offset (rad) added to the slot normal.
  PackedFloat32Array get_site_aims() const;
  // Mount steps and focus numbers from rules.json: {offsets: PackedFloat32Array
  // (rad), reaim_cost_frac, between_waves_only, focus_bonus, focus_cap,
  // focus_radius, lens_merge}.
  Dictionary get_aim_rules() const;
  // Stardust a re-aim of the building on `slot` costs; -1 when there is none
  // or it has no aim mount.
  float get_reaim_cost(int64_t slot) const;
  // Focus points: [{local: Vector2 (planet frame), pos: Vector2 (world, at the
  // interpolated rotation), radius, feeders: PackedInt32Array (sites),
  // lenses: int, mult: float (damage multiplier inside)}].
  Array get_focus_points() const;
  // get_beams(): the overcharged float gains +2 for a merged Lens beam (from
  // the focus point outward).
  // --- M2.11 bent aim and focus fire (end) ---

  // --- extras (not in CONTRACT.md) ---
  // Colours for palette keys that are not colour tiers ("enemy.eclipser"); default white.
  void set_palette(const Dictionary& colors);
  // Tests and demos before M1: from the next new_run, a debug system emits one
  // event per tick and flies a few enemies and a projectile in straight lines.
  void set_debug_scenario(bool on);

 protected:
  static void _bind_methods();
  void _notification(int what);

 private:
  void queue(orbitfall::Command c);
  void schedule(uint32_t ticks, bool wait);
  void run_to(uint32_t target);  // sim thread, or main thread when not threaded
  void latch();
  void wait_idle();
  void start_thread();
  void stop_thread();
  void thread_main();
  void rebuild_colors();
  void reset_run_state();
  void rebuild_statics();
  // Seconds the drawn frame lies behind the latest tick; 0 while the sim is frozen.
  float back_seconds() const { return curr_->running ? curr_->dt * (1.0f - alpha_) : 0.0f; }

  // --- main thread only ---
  orbitfall::ContentDB content_;
  bool contentLoaded_ = false;
  int waveSet_ = 0;
  int planet_ = orbitfall::kNoIndex;  // M2.8: planet override of the current run
  uint32_t seed_ = 1;
  bool threadedWanted_ = false;
  bool debugScenario_ = false;
  bool autoAdvance_ = false;  // _process ticks only after the first new_run
  double accumulator_ = 0.0;
  float alpha_ = 1.0f;
  std::vector<orbitfall::Command> log_;
  orbitfall_binding::Snapshot buffers_[4];
  orbitfall_binding::Snapshot* prev_ = &buffers_[0];
  orbitfall_binding::Snapshot* curr_ = &buffers_[1];
  std::vector<float> enemyColors_;  // RGBA per enemy def; read by the sim thread, written while idle
  std::vector<float> fallingColors_;  // M2.10: RGBA per falling def, same rules
  std::unordered_map<std::string, Color> palette_;
  uint64_t hashCache_ = 0;
  uint32_t hashCacheTick_ = 0xFFFFFFFFu;

  // --- owned by whoever runs ticks (see the threading model above) ---
  std::unique_ptr<orbitfall::Sim> sim_;
  orbitfall_binding::RunStatics statics_;
  orbitfall_binding::Snapshot* back_ = &buffers_[2];

  // --- shared, guarded by mutex_ ---
  mutable std::mutex mutex_;
  std::condition_variable workCv_;
  std::condition_variable doneCv_;
  std::vector<orbitfall::Command> pending_;
  std::vector<orbitfall::Event> events_;
  orbitfall_binding::Snapshot* ready_ = &buffers_[3];
  bool readyFresh_ = false;
  uint32_t targetTick_ = 0;  // written by the main thread only
  uint32_t doneTick_ = 0;
  bool quit_ = false;
  float msPerTick_ = 0.0f;
#ifndef ORBITFALL_SINGLE_THREADED
  std::thread thread_;
#endif
  bool threadRunning_ = false;
};

}  // namespace godot
