#include "sim_server.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "content/content_loader.h"
#include "sim/mathx.h"
#include "systems/waves.h"
#include "sim/version.h"

using namespace godot;
using orbitfall::Command;
using orbitfall_binding::Snapshot;

namespace {

constexpr float kDegToRad = 0.017453292519943295f;
constexpr int kFloatsPerInstance = 16;  // BatchRenderer layout
constexpr uint32_t kMaxTicksPerAdvance = 8;
constexpr uint32_t kLogMagic = 0x31474C4Fu;  // "OLG1"
constexpr size_t kLogHeaderBytes = 20;       // magic, seed, wave set, end tick, count

// Indexed by orbitfall::EventType.
const char* const kEventNames[] = {
    "None",        "LayerPopped",   "EnemyKilled",  "PlanetHit",     "StardustChanged",
    "WaveStarted", "WaveCleared",   "ShadowChanged", "GameOver",     "Victory",
    "BuildingPlaced", "BuildingSold", "CommandRejected"};
constexpr int kEventNameCount = static_cast<int>(sizeof(kEventNames) / sizeof(kEventNames[0]));
static_assert(kEventNameCount == static_cast<int>(orbitfall::EventType::Count), "event name table out of date");
static_assert(static_cast<int>(orbitfall::EventType::CommandRejected) == 12, "event name table out of date");

String to_gd(const std::string& s) { return String::utf8(s.c_str(), static_cast<int64_t>(s.size())); }

// Debug system (set_debug_scenario): exercises events and the snapshot path on
// a world without content. Deterministic; changes the state hash.
void debugScenario(orbitfall::World& w, float dt) {
  if (w.tick == 0) {
    for (int i = 0; i < 3; ++i) {
      const uint32_t e = w.enemies.add();
      if (e == orbitfall::kInvalidIndex) break;
      w.enemies.posX[e] = 300.0f;
      w.enemies.posY[e] = 40.0f * static_cast<float>(i - 1);
      w.enemies.velX[e] = -30.0f;
      w.enemies.hp[e] = 1.0f;
      w.enemies.type[e] = static_cast<uint16_t>(i);
    }
    const uint32_t p = w.projectiles.add();
    if (p != orbitfall::kInvalidIndex) {
      w.projectiles.velY[p] = 60.0f;
      w.projectiles.life[p] = 1.0e9f;
    }
  }
  for (uint32_t i = 0; i < w.enemies.count; ++i) {
    w.enemies.posX[i] += w.enemies.velX[i] * dt;
    w.enemies.posY[i] += w.enemies.velY[i] * dt;
  }
  for (uint32_t i = 0; i < w.projectiles.count; ++i) {
    w.projectiles.posX[i] += w.projectiles.velX[i] * dt;
    w.projectiles.posY[i] += w.projectiles.velY[i] * dt;
  }
  w.emit(orbitfall::EventType::None, static_cast<int32_t>(w.commandsApplied),
         static_cast<int32_t>(w.enemies.count), 0.0f, 0.0f, w.timeScale);
}

void collect_json(const String& dir, const String& rel, std::map<std::string, std::string>& files,
                  PackedStringArray& errors) {
  const PackedStringArray names = DirAccess::get_files_at(dir);
  for (int64_t i = 0; i < names.size(); ++i) {
    const String name = names[i];
    if (!name.ends_with(".json")) continue;
    const String path = dir.path_join(name);
    const String relPath = rel.is_empty() ? name : rel + String("/") + name;
    const Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
    if (f.is_null()) {
      errors.push_back(relPath + String(": cannot open file"));
      continue;
    }
    files[std::string(relPath.utf8().get_data())] = std::string(f->get_as_text().utf8().get_data());
  }
  const PackedStringArray dirs = DirAccess::get_directories_at(dir);
  for (int64_t i = 0; i < dirs.size(); ++i) {
    const String name = dirs[i];
    collect_json(dir.path_join(name), rel.is_empty() ? name : rel + String("/") + name, files, errors);
  }
}

template <typename T>
PackedInt32Array to_packed_int32(const std::vector<T>& v) {
  PackedInt32Array out;
  out.resize(static_cast<int64_t>(v.size()));
  if (!v.empty()) std::memcpy(out.ptrw(), v.data(), v.size() * sizeof(int32_t));
  return out;
}

}  // namespace

// ----------------------------------------------------------------------------
// Binding
// ----------------------------------------------------------------------------

void SimServer::_bind_methods() {
  ClassDB::bind_method(D_METHOD("core_version"), &SimServer::core_version);
  ClassDB::bind_method(D_METHOD("core_answer"), &SimServer::core_answer);

  ClassDB::bind_method(D_METHOD("load_content", "dir"), &SimServer::load_content, DEFVAL(String("res://content")));
  ClassDB::bind_method(D_METHOD("new_run", "seed", "wave_set"), &SimServer::new_run, DEFVAL(0));
  ClassDB::bind_method(D_METHOD("set_threaded", "on"), &SimServer::set_threaded);
  ClassDB::bind_method(D_METHOD("is_threaded"), &SimServer::is_threaded);
  ClassDB::bind_method(D_METHOD("step_ticks", "n"), &SimServer::step_ticks);
  ClassDB::bind_method(D_METHOD("advance", "delta"), &SimServer::advance);
  ClassDB::bind_method(D_METHOD("get_alpha"), &SimServer::get_alpha);

  ClassDB::bind_method(D_METHOD("rotate_input", "v"), &SimServer::rotate_input);
  ClassDB::bind_method(D_METHOD("place_building", "slot", "building", "aim_offset"), &SimServer::place_building, DEFVAL(0.0f));
  ClassDB::bind_method(D_METHOD("sell_building", "slot"), &SimServer::sell_building);
  ClassDB::bind_method(D_METHOD("set_aim", "slot", "offset"), &SimServer::set_aim);
  ClassDB::bind_method(D_METHOD("set_targeting", "slot", "mode"), &SimServer::set_targeting);
  ClassDB::bind_method(D_METHOD("start_wave"), &SimServer::start_wave);
  ClassDB::bind_method(D_METHOD("set_time_scale", "scale", "ease_seconds"), &SimServer::set_time_scale, DEFVAL(0.0f));
  ClassDB::bind_method(D_METHOD("get_command_log"), &SimServer::get_command_log);
  ClassDB::bind_method(D_METHOD("replay", "seed", "log"), &SimServer::replay);

  ClassDB::bind_method(D_METHOD("get_tick"), &SimServer::get_tick);
  ClassDB::bind_method(D_METHOD("get_state_hash"), &SimServer::get_state_hash);
  ClassDB::bind_method(D_METHOD("get_phase"), &SimServer::get_phase);
  ClassDB::bind_method(D_METHOD("get_wave_index"), &SimServer::get_wave_index);
  ClassDB::bind_method(D_METHOD("get_wave_count"), &SimServer::get_wave_count);
  ClassDB::bind_method(D_METHOD("get_stardust"), &SimServer::get_stardust);
  ClassDB::bind_method(D_METHOD("get_planet_hp"), &SimServer::get_planet_hp);
  ClassDB::bind_method(D_METHOD("get_planet_hp_max"), &SimServer::get_planet_hp_max);
  ClassDB::bind_method(D_METHOD("get_time_scale"), &SimServer::get_time_scale);
  ClassDB::bind_method(D_METHOD("get_planet_radius"), &SimServer::get_planet_radius);
  ClassDB::bind_method(D_METHOD("get_planet_rotation"), &SimServer::get_planet_rotation);
  ClassDB::bind_method(D_METHOD("get_planet_angular_velocity"), &SimServer::get_planet_angular_velocity);
  ClassDB::bind_method(D_METHOD("get_slot_count"), &SimServer::get_slot_count);
  ClassDB::bind_method(D_METHOD("get_slot_buildings"), &SimServer::get_slot_buildings);
  ClassDB::bind_method(D_METHOD("get_slot_flags"), &SimServer::get_slot_flags);
  ClassDB::bind_method(D_METHOD("get_sun_bearing"), &SimServer::get_sun_bearing);
  ClassDB::bind_method(D_METHOD("get_beam_width"), &SimServer::get_beam_width);
  ClassDB::bind_method(D_METHOD("get_beam_strength"), &SimServer::get_beam_strength);
  ClassDB::bind_method(D_METHOD("get_shade_intervals"), &SimServer::get_shade_intervals);
  ClassDB::bind_method(D_METHOD("get_income_per_second"), &SimServer::get_income_per_second);
  ClassDB::bind_method(D_METHOD("get_enemy_buffer"), &SimServer::get_enemy_buffer);
  ClassDB::bind_method(D_METHOD("get_enemy_count"), &SimServer::get_enemy_count);
  ClassDB::bind_method(D_METHOD("get_projectile_buffer"), &SimServer::get_projectile_buffer);
  ClassDB::bind_method(D_METHOD("get_projectile_count"), &SimServer::get_projectile_count);
  ClassDB::bind_method(D_METHOD("get_large_units"), &SimServer::get_large_units);
  ClassDB::bind_method(D_METHOD("get_building_defs"), &SimServer::get_building_defs);
  ClassDB::bind_method(D_METHOD("get_enemy_defs"), &SimServer::get_enemy_defs);
  ClassDB::bind_method(D_METHOD("get_forecast", "count"), &SimServer::get_forecast);
  ClassDB::bind_method(D_METHOD("get_debug_stats"), &SimServer::get_debug_stats);
  ClassDB::bind_method(D_METHOD("set_tunable", "path", "value"), &SimServer::set_tunable);
  ClassDB::bind_method(D_METHOD("drain_events"), &SimServer::drain_events);
  ClassDB::bind_method(D_METHOD("get_surface_time_scale"), &SimServer::get_surface_time_scale);
  ClassDB::bind_method(D_METHOD("get_surface_time_ease_seconds"), &SimServer::get_surface_time_ease_seconds);

  ClassDB::bind_method(D_METHOD("set_palette", "colors"), &SimServer::set_palette);
  ClassDB::bind_method(D_METHOD("set_debug_scenario", "on"), &SimServer::set_debug_scenario);
}

String SimServer::core_version() const { return String(orbitfall::core_version()); }
int SimServer::core_answer() const { return orbitfall::core_answer(); }

SimServer::SimServer() {
  // A valid empty run from the start, so every getter is safe before new_run.
  sim_ = std::make_unique<orbitfall::Sim>(orbitfall::SimConfig());
  orbitfall_binding::fill_snapshot(sim_->world(), statics_, *curr_);
  orbitfall_binding::fill_snapshot(sim_->world(), statics_, *prev_);
}

SimServer::~SimServer() { stop_thread(); }

void SimServer::_notification(int what) {
  if (what == NOTIFICATION_READY) set_process(!Engine::get_singleton()->is_editor_hint());
}

void SimServer::_process(double delta) {
  if (!autoAdvance_ || Engine::get_singleton()->is_editor_hint()) return;
  advance(delta);
}

// ----------------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------------

PackedStringArray SimServer::load_content(const String& dir) {
  PackedStringArray out;
  if (!DirAccess::dir_exists_absolute(dir)) {
    out.push_back(dir + String(": content directory not found"));
    return out;
  }
  std::map<std::string, std::string> files;
  collect_json(dir, String(), files, out);
  if (files.empty() && out.is_empty()) out.push_back(dir + String(": no .json files found"));

  orbitfall::ContentDB db;
  std::vector<std::string> errors;
  const bool ok = orbitfall::load_content_from_strings(files, db, errors);
  for (const std::string& e : errors) out.push_back(to_gd(e));
  if (!ok && out.is_empty()) out.push_back(String("content failed to load"));
  if (!out.is_empty()) return out;

  // A world points into its ContentDB, so the run in progress ends here: the
  // server goes back to an empty world until new_run builds one from the new content.
  stop_thread();
  sim_ = std::make_unique<orbitfall::Sim>(orbitfall::SimConfig());
  content_ = std::move(db);
  contentLoaded_ = true;
  autoAdvance_ = false;
  rebuild_colors();
  rebuild_statics();
  reset_run_state();
  return out;
}

// Static copies for the cheap getters, taken from the world of the current run.
void SimServer::rebuild_statics() {
  statics_ = orbitfall_binding::RunStatics();
  const orbitfall::World& w = sim_->world();
  statics_.waveCount = static_cast<int32_t>(w.waveCount());
  statics_.planetRadius = w.planet ? w.planet->radius : 0.0f;
  statics_.planetHpMax = w.planetHpMax;
  statics_.slotCount = static_cast<int32_t>(w.slotCount());
  statics_.enemyColors = enemyColors_.empty() ? nullptr : enemyColors_.data();
  const auto shot = palette_.find("projectile");
  if (shot != palette_.end()) {
    statics_.projectileColor[0] = shot->second.r;
    statics_.projectileColor[1] = shot->second.g;
    statics_.projectileColor[2] = shot->second.b;
    statics_.projectileColor[3] = shot->second.a;
  }
}

// Clears everything that belongs to the previous run (sim thread stopped).
void SimServer::reset_run_state() {
  pending_.clear();
  events_.clear();
  readyFresh_ = false;
  targetTick_ = 0;
  doneTick_ = 0;
  msPerTick_ = 0.0f;
  log_.clear();
  accumulator_ = 0.0;
  alpha_ = 1.0f;
  hashCacheTick_ = 0xFFFFFFFFu;
  orbitfall_binding::fill_snapshot(sim_->world(), statics_, *curr_);
  orbitfall_binding::fill_snapshot(sim_->world(), statics_, *prev_);
}

void SimServer::new_run(int64_t seed, int64_t wave_set) {
  if (contentLoaded_) {
    ERR_FAIL_COND_MSG(wave_set < 0 || wave_set >= static_cast<int64_t>(content_.waveSets.size()),
                      "SimServer.new_run: wave_set out of range");
  }
  stop_thread();
  // The same fold as core's content-aware initWorld, so the logged seed replays.
  const uint64_t wide = static_cast<uint64_t>(seed);
  seed_ = static_cast<uint32_t>(wide ^ (wide >> 32));
  waveSet_ = static_cast<int>(wave_set);

  orbitfall::SimConfig config;
  config.seed = seed_;
  sim_ = std::make_unique<orbitfall::Sim>(config);
  if (contentLoaded_) {
    // The real game: planet, sun and waves of the wave set (starts in an intermission).
    orbitfall::initWorld(sim_->world(), content_, static_cast<uint32_t>(waveSet_), wide);
    // The binding keeps the replay log itself (get_command_log).
    sim_->world().recordCommands = false;
  }
  if (debugScenario_) sim_->pipeline().add("debugScenario", debugScenario);

  rebuild_statics();
  reset_run_state();
  autoAdvance_ = true;
  if (threadedWanted_) start_thread();
}

void SimServer::set_threaded(bool on) {
#ifdef ORBITFALL_SINGLE_THREADED
  (void)on;  // web: always stepped on the main thread
#else
  threadedWanted_ = on;
  if (on)
    start_thread();
  else
    stop_thread();
#endif
}

bool SimServer::is_threaded() const { return threadRunning_; }

void SimServer::step_ticks(int64_t n) {
  const uint32_t ticks = n > 0 ? static_cast<uint32_t>(n) : 0u;
  schedule(ticks, true);
  latch();
  accumulator_ = 0.0;
  alpha_ = 1.0f;  // reads give exactly the latest tick
}

int64_t SimServer::advance(double delta) {
  if (!(delta > 0.0)) return 0;  // also rejects NaN
  const double fixed = static_cast<double>(orbitfall::kFixedDt);
  accumulator_ += delta;
  uint32_t ticks = 0;
  while (accumulator_ >= fixed && ticks < kMaxTicksPerAdvance) {
    accumulator_ -= fixed;
    ++ticks;
  }
  if (accumulator_ >= fixed) accumulator_ = 0.0;  // fell too far behind: drop the backlog
  schedule(ticks, false);
  latch();
  alpha_ = static_cast<float>(accumulator_ / fixed);
  return ticks;
}

// ----------------------------------------------------------------------------
// Tick scheduling and the sim thread
// ----------------------------------------------------------------------------

void SimServer::schedule(uint32_t ticks, bool wait) {
  if (threadRunning_) {
    if (ticks > 0) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        targetTick_ += ticks;
      }
      workCv_.notify_one();
    }
    if (wait) wait_idle();
    return;
  }
  targetTick_ += ticks;
  for (const Command& c : pending_) sim_->push(c);
  pending_.clear();
  run_to(targetTick_);
}

void SimServer::run_to(uint32_t target) {
  using Clock = std::chrono::steady_clock;
  while (sim_->tick() < target) {
    const Clock::time_point t0 = Clock::now();
    sim_->step();
    orbitfall::World& world = sim_->world();
    orbitfall_binding::fill_snapshot(world, statics_, *back_);
    const float ms = std::chrono::duration<float, std::milli>(Clock::now() - t0).count();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      std::swap(back_, ready_);
      readyFresh_ = true;
      events_.insert(events_.end(), world.events.items.begin(), world.events.items.end());
      doneTick_ = world.tick;
      msPerTick_ = msPerTick_ <= 0.0f ? ms : msPerTick_ * 0.9f + ms * 0.1f;
    }
    world.events.clear();
    doneCv_.notify_all();
  }
}

void SimServer::latch() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!readyFresh_) return;
  // prev <- curr <- ready; the old prev becomes the buffer the sim overwrites next.
  std::swap(prev_, curr_);
  std::swap(curr_, ready_);
  readyFresh_ = false;
}

void SimServer::wait_idle() {
  if (!threadRunning_) return;
  std::unique_lock<std::mutex> lock(mutex_);
  doneCv_.wait(lock, [this] { return doneTick_ >= targetTick_; });
}

void SimServer::start_thread() {
#ifndef ORBITFALL_SINGLE_THREADED
  if (threadRunning_) return;
  quit_ = false;
  threadRunning_ = true;
  thread_ = std::thread(&SimServer::thread_main, this);
#endif
}

void SimServer::stop_thread() {
#ifndef ORBITFALL_SINGLE_THREADED
  if (!threadRunning_) return;
  wait_idle();  // finish the ticks already scheduled, so both modes stay in step
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
  }
  workCv_.notify_one();
  thread_.join();
  threadRunning_ = false;
#endif
}

void SimServer::thread_main() {
  std::vector<Command> commands;
  for (;;) {
    uint32_t target = 0;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      workCv_.wait(lock, [this] { return quit_ || targetTick_ > doneTick_; });
      if (quit_) return;
      // Target and commands are taken together: a command stamped with tick T
      // was queued before the target moved past T, so it is always in time.
      target = targetTick_;
      commands.swap(pending_);
    }
    for (const Command& c : commands) sim_->push(c);
    commands.clear();
    run_to(target);
  }
}

// ----------------------------------------------------------------------------
// Commands
// ----------------------------------------------------------------------------

void SimServer::queue(Command c) {
  c.tick = targetTick_;  // the next tick to be scheduled; only this thread writes it
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back(c);
  }
  log_.push_back(c);
}

void SimServer::rotate_input(float v) { queue(orbitfall::cmd::rotateInput(0, v)); }
void SimServer::place_building(int64_t slot, int64_t building, float aim_offset) {
  queue(orbitfall::cmd::placeBuilding(0, static_cast<int32_t>(slot), static_cast<int32_t>(building), aim_offset));
}
void SimServer::sell_building(int64_t slot) { queue(orbitfall::cmd::sellBuilding(0, static_cast<int32_t>(slot))); }
void SimServer::set_aim(int64_t slot, float offset) {
  queue(orbitfall::cmd::setAim(0, static_cast<int32_t>(slot), offset));
}
void SimServer::set_targeting(int64_t slot, int64_t mode) {
  const uint8_t m = static_cast<uint8_t>(mode < 0 ? 0 : (mode > 255 ? 255 : mode));
  queue(orbitfall::cmd::setTargeting(0, static_cast<int32_t>(slot), m));
}
void SimServer::start_wave() { queue(orbitfall::cmd::startWave(0)); }
void SimServer::set_time_scale(float scale, float ease_seconds) {
  queue(orbitfall::cmd::setTimeScale(0, scale, ease_seconds));
}

// Log layout (little-endian): magic "OLG1", seed u32, wave set i32, end tick u32,
// command count u32, then count x 24-byte orbitfall::Command.
PackedByteArray SimServer::get_command_log() const {
  PackedByteArray out;
  out.resize(static_cast<int64_t>(kLogHeaderBytes + log_.size() * sizeof(Command)));
  uint8_t* p = out.ptrw();
  const uint32_t header[5] = {kLogMagic, seed_, static_cast<uint32_t>(waveSet_), targetTick_,
                              static_cast<uint32_t>(log_.size())};
  std::memcpy(p, header, kLogHeaderBytes);
  if (!log_.empty()) std::memcpy(p + kLogHeaderBytes, log_.data(), log_.size() * sizeof(Command));
  return out;
}

bool SimServer::replay(int64_t seed, const PackedByteArray& log) {
  const size_t size = static_cast<size_t>(log.size());
  ERR_FAIL_COND_V_MSG(size < kLogHeaderBytes, false, "SimServer.replay: log too short");
  uint32_t header[5];
  std::memcpy(header, log.ptr(), kLogHeaderBytes);
  ERR_FAIL_COND_V_MSG(header[0] != kLogMagic, false, "SimServer.replay: not a run log");
  const size_t count = header[4];
  ERR_FAIL_COND_V_MSG(size != kLogHeaderBytes + count * sizeof(Command), false,
                      "SimServer.replay: log size does not match its command count");
  std::vector<Command> commands(count);
  if (count) std::memcpy(commands.data(), log.ptr() + kLogHeaderBytes, count * sizeof(Command));
  for (const Command& c : commands) {
    ERR_FAIL_COND_V_MSG(c.tick > header[3] || c.type >= orbitfall::CommandType::Count, false,
                        "SimServer.replay: corrupt command in log");
  }

  new_run(seed, static_cast<int32_t>(header[2]));
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = commands;  // already stamped with their original ticks
  }
  log_ = std::move(commands);
  step_ticks(header[3]);
  return true;
}

// ----------------------------------------------------------------------------
// Snapshot getters
// ----------------------------------------------------------------------------

int64_t SimServer::get_state_hash() {
  // Hashing walks every pool, so it is done on demand (and cached per tick),
  // not per snapshot. Waits for the sim thread to finish its scheduled ticks.
  wait_idle();
  const uint32_t tick = sim_->tick();
  if (tick != hashCacheTick_) {
    hashCache_ = sim_->hash();
    hashCacheTick_ = tick;
  }
  return static_cast<int64_t>(hashCache_);
}

float SimServer::get_planet_rotation() const {
  if (curr_->tick == prev_->tick + 1) {
    // Rotation is kept in [0, 2*pi): interpolate along the short way round.
    float d = curr_->planetRotation - prev_->planetRotation;
    if (d > orbitfall::kPi) d -= orbitfall::kTau;
    if (d < -orbitfall::kPi) d += orbitfall::kTau;
    return curr_->planetRotation - d * (1.0f - alpha_);
  }
  return curr_->planetRotation;
}

PackedInt32Array SimServer::get_slot_buildings() const { return to_packed_int32(curr_->slotBuildings); }
PackedInt32Array SimServer::get_slot_flags() const { return to_packed_int32(curr_->slotFlags); }

PackedFloat32Array SimServer::get_shade_intervals() const {
  PackedFloat32Array out;
  const std::vector<float>& v = curr_->shadeIntervals;
  out.resize(static_cast<int64_t>(v.size()));
  if (!v.empty()) std::memcpy(out.ptrw(), v.data(), v.size() * sizeof(float));
  return out;
}

namespace {
// Entities are swap-removed, so indices do not match between two snapshots.
// Positions are therefore interpolated inside the latest tick: the buffer core
// filled for that tick, with each origin moved back along its velocity by the
// part of the tick not yet shown.
PackedFloat32Array interpolated(const std::vector<float>& render, const std::vector<float>& velX,
                                const std::vector<float>& velY, uint32_t count, float back) {
  PackedFloat32Array out;
  out.resize(static_cast<int64_t>(count) * kFloatsPerInstance);
  if (count == 0) return out;
  float* f = out.ptrw();
  std::memcpy(f, render.data(), static_cast<size_t>(count) * kFloatsPerInstance * sizeof(float));
  if (back > 0.0f) {
    for (uint32_t i = 0; i < count; ++i, f += kFloatsPerInstance) {
      f[3] -= velX[i] * back;
      f[7] -= velY[i] * back;
    }
  }
  return out;
}
}  // namespace

PackedFloat32Array SimServer::get_enemy_buffer() {
  const Snapshot& s = *curr_;
  return interpolated(s.enemyRender, s.enemyVelX, s.enemyVelY, s.enemyCount, back_seconds());
}

PackedFloat32Array SimServer::get_projectile_buffer() {
  const Snapshot& s = *curr_;
  return interpolated(s.projRender, s.projVelX, s.projVelY, s.projectileCount, back_seconds());
}

TypedArray<Dictionary> SimServer::get_large_units() const {
  TypedArray<Dictionary> out;
  const float back = back_seconds();
  for (const orbitfall_binding::LargeUnit& u : curr_->largeUnits) {
    Dictionary d;
    d["pos"] = Vector2(u.x - u.vx * back, u.y - u.vy * back);
    d["radius"] = u.radius;
    d["hp_frac"] = u.hpFrac;
    d["shadow"] = u.shadow;
    out.push_back(d);
  }
  return out;
}

float SimServer::get_surface_time_scale() const {
  const orbitfall::PlanetDef* planet = sim_->world().planet;  // fixed for the run
  return planet ? planet->surfaceTimeScale : 1.0f;
}

float SimServer::get_surface_time_ease_seconds() const {
  return contentLoaded_ ? content_.rules.surfaceTimeEaseSeconds : 0.0f;
}

// ----------------------------------------------------------------------------
// Content info
// ----------------------------------------------------------------------------

// Instance colours per enemy definition from the client palette: white for
// colour tiers (the shader tints those from its palette texture).
void SimServer::rebuild_colors() {
  enemyColors_.assign(content_.enemies.size() * 4, 1.0f);
  for (size_t i = 0; i < content_.enemies.size(); ++i) {
    const orbitfall::EnemyDef& e = content_.enemies[i];
    if (e.visual.colorTier >= 0) continue;
    const auto it = palette_.find(e.visual.color);
    if (it == palette_.end()) continue;
    enemyColors_[i * 4 + 0] = it->second.r;
    enemyColors_[i * 4 + 1] = it->second.g;
    enemyColors_[i * 4 + 2] = it->second.b;
    enemyColors_[i * 4 + 3] = it->second.a;
  }
}

void SimServer::set_palette(const Dictionary& colors) {
  wait_idle();  // the sim thread reads the colours while it fills snapshots
  palette_.clear();
  const Array keys = colors.keys();
  for (int64_t i = 0; i < keys.size(); ++i) {
    const String key = keys[i];
    palette_[std::string(key.utf8().get_data())] = colors[keys[i]];
  }
  rebuild_colors();
  rebuild_statics();
  // Show the new colours without waiting for the next tick.
  orbitfall_binding::fill_snapshot(sim_->world(), statics_, *curr_);
}

void SimServer::set_debug_scenario(bool on) { debugScenario_ = on; }

TypedArray<Dictionary> SimServer::get_building_defs() const {
  TypedArray<Dictionary> out;
  for (size_t i = 0; i < content_.buildings.size(); ++i) {
    const orbitfall::BuildingDef& b = content_.buildings[i];
    Dictionary d;
    d["index"] = static_cast<int64_t>(i);
    d["id"] = to_gd(b.id);
    d["name"] = to_gd(b.id).capitalize();
    d["cost"] = b.cost;
    d["arc"] = b.arc;  // rad, half-angle
    d["range"] = b.range;
    d["kind"] = b.kind == orbitfall::BuildingKind::Panel ? String("panel") : String("tower");
    d["shape"] = static_cast<int64_t>(b.visual.shape);
    d["color"] = to_gd(b.visual.color);  // palette key
    d["footprint"] = b.footprint;
    d["aim_mount"] = b.aimMount;
    d["aim_offset"] = b.aimOffset;
    d["fire_rate"] = b.fireRate;
    d["panel_rate"] = b.panelRate;
    out.push_back(d);
  }
  return out;
}

TypedArray<Dictionary> SimServer::get_enemy_defs() const {
  TypedArray<Dictionary> out;
  for (size_t i = 0; i < content_.enemies.size(); ++i) {
    const orbitfall::EnemyDef& e = content_.enemies[i];
    Dictionary d;
    d["index"] = static_cast<int64_t>(i);
    d["id"] = to_gd(e.id);
    d["name"] = to_gd(e.id).capitalize();
    d["shape"] = static_cast<int64_t>(e.visual.shape);
    d["color"] = to_gd(e.visual.color);  // palette key
    d["tier"] = static_cast<int64_t>(e.visual.colorTier);  // -1 when not a colour tier
    d["layers"] = e.layers;
    d["hp"] = e.hp;
    d["speed"] = e.speed;
    d["size"] = e.size;
    d["bounty"] = e.bounty;
    d["traits"] = static_cast<int64_t>(e.traits);
    d["shadow_caster"] = e.shadowCaster;
    d["hp_bar"] = e.hpBar;
    d["pops_into"] = e.popsInto;
    out.push_back(d);
  }
  return out;
}

namespace {
Dictionary forecast_to_dict(const orbitfall::WaveForecast& f) {
  Dictionary d;
  d["wave"] = f.wave;
  d["light_mult"] = f.lightMult;
  d["exact"] = f.exact;
  Array entries;
  for (const orbitfall::ForecastEntry& e : f.entries) {
    Dictionary entry;
    entry["lane"] = e.lane;
    entry["lane_bearing"] = e.laneBearing;
    entry["enemy"] = e.enemy;
    entry["count"] = e.count;
    entry["shadow_caster"] = e.shadowCaster;
    entries.push_back(entry);
  }
  d["entries"] = entries;
  PackedFloat32Array ghosts;
  ghosts.resize(static_cast<int64_t>(f.eclipserGhostArcs.size()));
  if (!f.eclipserGhostArcs.empty()) {
    std::memcpy(ghosts.ptrw(), f.eclipserGhostArcs.data(), f.eclipserGhostArcs.size() * sizeof(float));
  }
  d["eclipser_ghost_arcs"] = ghosts;
  return d;
}
}  // namespace

// Core's forecast(): the same WaveDef data the spawner reads (the forecast
// never lies). During a wave the wave in flight comes first (exact, straight
// from its WaveDef, no ghost arcs: its shadow casters are live), then the
// upcoming ones.
TypedArray<Dictionary> SimServer::get_forecast(int64_t count) {
  TypedArray<Dictionary> out;
  if (!contentLoaded_ || count <= 0) return out;
  wait_idle();  // reads the World
  const orbitfall::World& w = sim_->world();
  if (w.waveSet == nullptr || w.planet == nullptr) return out;
  int upcoming = static_cast<int>(count > 64 ? 64 : count);
  if (w.phase == orbitfall::Phase::Wave && w.waveIndex < w.waveCount()) {
    const orbitfall::WaveDef& wave = w.waveSet->waves[w.waveIndex];
    orbitfall::WaveForecast now;
    now.wave = wave.wave;
    now.lightMult = wave.lightMult;
    now.exact = true;
    for (const orbitfall::SpawnDef& sp : wave.spawns) {
      orbitfall::ForecastEntry e;
      e.lane = sp.lane;
      e.laneBearing = w.planet->lanes[static_cast<size_t>(sp.lane)].bearing;
      e.enemy = sp.enemy;
      e.count = sp.count;
      e.shadowCaster = w.content->enemies[static_cast<size_t>(sp.enemy)].shadowCaster;
      now.entries.push_back(e);
    }
    out.push_back(forecast_to_dict(now));
    --upcoming;
  }
  for (const orbitfall::WaveForecast& f : orbitfall::forecast(w, upcoming)) out.push_back(forecast_to_dict(f));
  return out;
}

Dictionary SimServer::get_debug_stats() const {
  Dictionary d;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    d["ms_per_tick"] = msPerTick_;
    d["sim_tick"] = static_cast<int64_t>(doneTick_);
    d["pending_events"] = static_cast<int64_t>(events_.size());
  }
  d["tick"] = static_cast<int64_t>(curr_->tick);
  d["enemies"] = static_cast<int64_t>(curr_->enemyCount);
  d["projectiles"] = static_cast<int64_t>(curr_->projectileCount);
  d["enemy_overflow"] = static_cast<int64_t>(curr_->enemyOverflow);
  d["projectile_overflow"] = static_cast<int64_t>(curr_->projectileOverflow);
  d["lit_fraction"] = curr_->litFraction;
  d["shade_intervals"] = static_cast<int64_t>(curr_->shadeIntervals.size() / 2);
  d["wave_time"] = curr_->waveTime;
  d["time_scale"] = curr_->timeScale;
  d["sim_time"] = curr_->simTime;
  d["threaded"] = threadRunning_;
  d["seed"] = static_cast<int64_t>(seed_);
  return d;
}

// Live tuning (debug panel). Paths are "<group>.<json key>" for the current
// run's sun and planet and for rules; values use the JSON units (degrees).
// The World reads its ContentDB every tick, so mutating the ContentDB between
// ticks (sim idle) is all it takes. Tuned runs do not replay from their log:
// the log does not record tunables.
bool SimServer::set_tunable(const String& path, float value) {
  if (!contentLoaded_ || waveSet_ < 0 || waveSet_ >= static_cast<int>(content_.waveSets.size())) return false;
  if (!(value - value == 0.0f)) return false;  // NaN or infinity
  const orbitfall::WaveSetDef& ws = content_.waveSets[static_cast<size_t>(waveSet_)];
  orbitfall::SunDef* sun = ws.sun >= 0 && ws.sun < static_cast<int>(content_.suns.size())
                               ? &content_.suns[static_cast<size_t>(ws.sun)]
                               : nullptr;
  orbitfall::PlanetDef* planet = ws.planet >= 0 && ws.planet < static_cast<int>(content_.planets.size())
                                     ? &content_.planets[static_cast<size_t>(ws.planet)]
                                     : nullptr;
  orbitfall::RulesDef& rules = content_.rules;

  struct Entry {
    const char* path;
    float* field;
    float scale;
  };
  const Entry table[] = {
      {"sun.startDeg", sun ? &sun->start : nullptr, kDegToRad},
      {"sun.speedDegPerSec", sun ? &sun->speed : nullptr, kDegToRad},
      {"sun.beamWidth", sun ? &sun->beamWidth : nullptr, 1.0f},
      {"sun.baseStrength", sun ? &sun->baseStrength : nullptr, 1.0f},
      {"planet.rotMaxSpeed", planet ? &planet->rotMaxSpeed : nullptr, kDegToRad},
      {"planet.rotAccel", planet ? &planet->rotAccel : nullptr, kDegToRad},
      {"planet.rotDamping", planet ? &planet->rotDamping : nullptr, 1.0f},
      {"planet.surfaceTimeScale", planet ? &planet->surfaceTimeScale : nullptr, 1.0f},
      {"planet.sellRefund", planet ? &planet->sellRefund : nullptr, 1.0f},
      {"rules.killBounty", &rules.killBounty, 1.0f},
      {"rules.lightChargeMult", &rules.lightChargeMult, 1.0f},
      {"rules.spawnRadius", &rules.spawnRadius, 1.0f},
      {"rules.spawnJitterDeg", &rules.spawnJitter, kDegToRad},
      {"rules.surfaceTimeEaseSeconds", &rules.surfaceTimeEaseSeconds, 1.0f},
  };
  const CharString key = path.utf8();
  for (const Entry& e : table) {
    if (std::strcmp(e.path, key.get_data()) != 0) continue;
    if (e.field == nullptr) return false;
    wait_idle();  // the sim may read content and statics while it runs ticks
    *e.field = value * e.scale;
    return true;
  }
  return false;
}

// ----------------------------------------------------------------------------
// Events
// ----------------------------------------------------------------------------

TypedArray<Dictionary> SimServer::drain_events() {
  std::vector<orbitfall::Event> events;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    events.swap(events_);
  }
  TypedArray<Dictionary> out;
  for (const orbitfall::Event& e : events) {
    const int type = static_cast<int>(e.type);
    Dictionary d;
    d["type"] = String(type < kEventNameCount ? kEventNames[type] : "Unknown");
    d["tick"] = static_cast<int64_t>(e.tick);
    d["a"] = e.a;
    d["b"] = e.b;
    d["x"] = e.x;
    d["y"] = e.y;
    d["value"] = e.value;
    out.push_back(d);
  }
  return out;
}
