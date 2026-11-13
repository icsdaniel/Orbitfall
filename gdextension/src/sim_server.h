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
  void new_run(int64_t seed, int64_t wave_set);
  void set_threaded(bool on);
  bool is_threaded() const;
  void step_ticks(int64_t n);
  // What _process calls: real seconds in, whole 30 Hz ticks out. Returns the ticks scheduled.
  int64_t advance(double delta);
  float get_alpha() const { return alpha_; }

  // --- commands ---
  void rotate_input(float v);
  void place_building(int64_t slot, int64_t building, float aim_offset);
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
  TypedArray<Dictionary> get_enemy_defs() const;
  TypedArray<Dictionary> get_forecast(int64_t count);
  Dictionary get_debug_stats() const;
  bool set_tunable(const String& path, float value);
  TypedArray<Dictionary> drain_events();
  // Slow time in the Surface band (planet surfaceTimeScale, rules
  // surfaceTimeEaseSeconds): the client sends them back as set_time_scale.
  float get_surface_time_scale() const;
  float get_surface_time_ease_seconds() const;

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
