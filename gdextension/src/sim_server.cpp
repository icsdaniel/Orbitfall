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

// Indexed by orbitfall::EventType. The last three are in CONTRACT.md but not in
// core yet; they resolve by themselves if core appends them in this order.
// TODO(M1.3): confirm the order once core adds BuildingPlaced/BuildingSold/CommandRejected.
const char* const kEventNames[] = {
    "None",        "LayerPopped",   "EnemyKilled",  "PlanetHit",     "StardustChanged",
    "WaveStarted", "WaveCleared",   "ShadowChanged", "GameOver",     "Victory",
    "BuildingPlaced", "BuildingSold", "CommandRejected"};
constexpr int kEventNameCount = static_cast<int>(sizeof(kEventNames) / sizeof(kEventNames[0]));
static_assert(static_cast<int>(orbitfall::EventType::Victory) == 9, "event name table out of date");

// Palette order of BatchRenderer.Tier; "tier.<name>" colour keys map to these.
const char* const kTierNames[] = {"red", "blue", "green", "yellow", "pink", "black", "white", "ceramic"};

float tier_of(const std::string& colorKey) {
  if (colorKey.compare(0, 5, "tier.") != 0) return -1.0f;
  for (int i = 0; i < 8; ++i)
    if (colorKey.compare(5, std::string::npos, kTierNames[i]) == 0) return static_cast<float>(i);
  return -1.0f;
}

String to_gd(const std::string& s) { return String::utf8(s.c_str(), static_cast<int64_t>(s.size())); }

// Writes one 2D multimesh instance: rotated + scaled transform, colour, custom.
inline void write_instance(float* f, float x, float y, float dirX, float dirY, float scale,
                           const Color& c, float c0, float c1, float c2, float c3) {
  float len = std::sqrt(dirX * dirX + dirY * dirY);
  float cs = 1.0f, sn = 0.0f;
  if (len > 1e-6f) {
    cs = dirX / len;
    sn = dirY / len;
  }
  f[0] = cs * scale;   // x.x
  f[1] = -sn * scale;  // y.x
  f[2] = 0.0f;
  f[3] = x;
  f[4] = sn * scale;   // x.y
  f[5] = cs * scale;   // y.y
  f[6] = 0.0f;
  f[7] = y;
  f[8] = c.r;
  f[9] = c.g;
  f[10] = c.b;
  f[11] = c.a;
  f[12] = c0;
  f[13] = c1;
  f[14] = c2;
  f[15] = c3;
}

// Debug system (set_debug_scenario): exercises events and the render buffers
// before the M1 systems exist. Deterministic; changes the state hash.
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

  wait_idle();
  content_ = std::move(db);
  contentLoaded_ = true;
  rebuild_visuals();
  // The run in progress keeps its world; call new_run to build one from the new content.
  return out;
}

void SimServer::rebuild_statics() {
  statics_ = orbitfall_binding::RunStatics();
  if (!contentLoaded_ || waveSet_ < 0 || waveSet_ >= static_cast<int>(content_.waveSets.size())) return;
  const orbitfall::WaveSetDef& ws = content_.waveSets[static_cast<size_t>(waveSet_)];
  statics_.waveCount = static_cast<int32_t>(ws.waves.size());
  if (ws.planet >= 0 && ws.planet < static_cast<int>(content_.planets.size())) {
    const orbitfall::PlanetDef& p = content_.planets[static_cast<size_t>(ws.planet)];
    statics_.planetRadius = p.radius;
    statics_.planetHpMax = p.hp;
    statics_.slotCount = p.slots;
    statics_.startStardust = p.startStardust;
  }
  if (ws.sun >= 0 && ws.sun < static_cast<int>(content_.suns.size())) {
    const orbitfall::SunDef& s = content_.suns[static_cast<size_t>(ws.sun)];
    statics_.sunStart = s.start;
    statics_.beamWidth = s.beamWidth;
    statics_.beamStrength = s.baseStrength;
  }
}

void SimServer::new_run(int64_t seed, int64_t wave_set) {
  if (contentLoaded_) {
    ERR_FAIL_COND_MSG(wave_set < 0 || wave_set >= static_cast<int64_t>(content_.waveSets.size()),
                      "SimServer.new_run: wave_set out of range");
  }
  stop_thread();
  seed_ = static_cast<uint32_t>(seed);
  waveSet_ = static_cast<int>(wave_set);
  rebuild_statics();

  orbitfall::SimConfig config;
  config.seed = seed_;
  // TODO(M1.x): build the World from content_ (planet, sun, waves of waveSet_)
  // once core has a content-aware init; today the World only takes a seed.
  sim_ = std::make_unique<orbitfall::Sim>(config);
  if (debugScenario_) sim_->pipeline().add("debugScenario", debugScenario);

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
    return prev_->planetRotation + (curr_->planetRotation - prev_->planetRotation) * alpha_;
  }
  return curr_->planetRotation - curr_->planetAngVel * back_seconds();
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

// Entities are swap-removed, so indices do not match between two snapshots.
// Positions are therefore interpolated inside the latest tick: its position
// moved back along its velocity by the part of the tick not yet shown.
PackedFloat32Array SimServer::get_enemy_buffer() {
  const Snapshot& s = *curr_;
  PackedFloat32Array out;
  out.resize(static_cast<int64_t>(s.enemyCount) * kFloatsPerInstance);
  float* f = out.ptrw();
  const float back = back_seconds();
  const EnemyVisual fallback;
  const size_t defs = enemyVisuals_.size();
  for (uint32_t i = 0; i < s.enemyCount; ++i, f += kFloatsPerInstance) {
    const EnemyVisual& v = s.enemyType[i] < defs ? enemyVisuals_[s.enemyType[i]] : fallback;
    const float vx = s.enemyVelX[i], vy = s.enemyVelY[i];
    // Scale = diameter in world units: define the archetype with shape_mesh_size 1.
    write_instance(f, s.enemyPosX[i] - vx * back, s.enemyPosY[i] - vy * back, vx, vy, v.diameter,
                   v.color, v.shape, v.tier, static_cast<float>(s.enemyTraits[i]), 0.0f);
  }
  return out;
}

PackedFloat32Array SimServer::get_projectile_buffer() {
  const Snapshot& s = *curr_;
  PackedFloat32Array out;
  out.resize(static_cast<int64_t>(s.projectileCount) * kFloatsPerInstance);
  float* f = out.ptrw();
  const float back = back_seconds();
  const Color white(1, 1, 1, 1);
  for (uint32_t i = 0; i < s.projectileCount; ++i, f += kFloatsPerInstance) {
    const float vx = s.projVelX[i], vy = s.projVelY[i];
    // Scale 1: the archetype's shape_mesh_size is the projectile size. Custom: circle,
    // instance colour, no traits, extra = projectile type index.
    // TODO(M1.6): per-type size/colour once projectile types carry visuals.
    write_instance(f, s.projPosX[i] - vx * back, s.projPosY[i] - vy * back, vx, vy, 1.0f, white,
                   0.0f, -1.0f, 0.0f, static_cast<float>(s.projType[i]));
  }
  return out;
}

TypedArray<Dictionary> SimServer::get_large_units() const {
  TypedArray<Dictionary> out;
  const Snapshot& s = *curr_;
  const float back = back_seconds();
  const size_t defs = enemyVisuals_.size();
  for (uint32_t i = 0; i < s.enemyCount; ++i) {
    if (s.enemyType[i] >= defs) continue;
    const EnemyVisual& v = enemyVisuals_[s.enemyType[i]];
    if (!v.hpBar) continue;
    Dictionary d;
    d["pos"] = Vector2(s.enemyPosX[i] - s.enemyVelX[i] * back, s.enemyPosY[i] - s.enemyVelY[i] * back);
    d["radius"] = v.diameter * 0.5f;
    const float frac = v.hpMax > 0.0f ? s.enemyHp[i] / v.hpMax : 0.0f;
    d["hp_frac"] = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
    d["shadow"] = v.shadow;  // TODO(M1.8): true only while it actually casts (dot(p, s) > R)
    out.push_back(d);
  }
  return out;
}

// ----------------------------------------------------------------------------
// Content info
// ----------------------------------------------------------------------------

void SimServer::rebuild_visuals() {
  enemyVisuals_.clear();
  enemyVisuals_.reserve(content_.enemies.size());
  for (const orbitfall::EnemyDef& e : content_.enemies) {
    EnemyVisual v;
    v.shape = static_cast<float>(static_cast<int>(e.shape));
    v.tier = tier_of(e.visual.color);
    const auto it = palette_.find(e.visual.color);
    if (it != palette_.end()) v.color = it->second;
    v.diameter = e.size * 2.0f;
    v.hpMax = e.hp;
    v.hpBar = e.hpBar;
    v.shadow = e.shadowCaster;
    enemyVisuals_.push_back(v);
  }
}

void SimServer::set_palette(const Dictionary& colors) {
  palette_.clear();
  const Array keys = colors.keys();
  for (int64_t i = 0; i < keys.size(); ++i) {
    const String key = keys[i];
    palette_[std::string(key.utf8().get_data())] = colors[keys[i]];
  }
  rebuild_visuals();
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
    d["shape"] = static_cast<int64_t>(e.shape);
    d["color"] = to_gd(e.visual.color);  // palette key
    d["tier"] = static_cast<int64_t>(tier_of(e.visual.color));  // -1 when not a colour tier
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

// Reads the same WaveDef data the spawner will read (the forecast never lies).
TypedArray<Dictionary> SimServer::get_forecast(int64_t count) const {
  TypedArray<Dictionary> out;
  if (!contentLoaded_ || waveSet_ < 0 || waveSet_ >= static_cast<int>(content_.waveSets.size())) return out;
  const orbitfall::WaveSetDef& ws = content_.waveSets[static_cast<size_t>(waveSet_)];
  const orbitfall::PlanetDef* planet =
      ws.planet >= 0 && ws.planet < static_cast<int>(content_.planets.size())
          ? &content_.planets[static_cast<size_t>(ws.planet)]
          : nullptr;
  const int64_t first = curr_->waveIndex;
  const int64_t total = static_cast<int64_t>(ws.waves.size());
  if (first < 0 || first >= total) return out;
  // The wave the player is looking from decides how far ahead is exact / icons.
  const orbitfall::ForecastDef& range = ws.waves[static_cast<size_t>(first)].forecast;
  for (int64_t i = first; i < total && i < first + count; ++i) {
    const orbitfall::WaveDef& wave = ws.waves[static_cast<size_t>(i)];
    const int64_t ahead = i - first + 1;  // 1 = next (or current) wave
    const bool exact = ahead <= range.exactAhead;
    const bool icons = ahead <= range.iconsAhead;
    Dictionary d;
    d["wave"] = wave.wave;
    d["light_mult"] = wave.lightMult;
    d["exact"] = exact;
    Array entries;
    PackedFloat32Array ghosts;
    if (exact || icons) {
      for (const orbitfall::SpawnDef& sp : wave.spawns) {
        if (sp.enemy < 0 || sp.enemy >= static_cast<int>(content_.enemies.size())) continue;
        const orbitfall::EnemyDef& enemy = content_.enemies[static_cast<size_t>(sp.enemy)];
        float bearing = 0.0f;
        if (planet && sp.lane >= 0 && sp.lane < static_cast<int>(planet->lanes.size())) {
          bearing = planet->lanes[static_cast<size_t>(sp.lane)].bearing;
        }
        Dictionary e;
        e["lane_bearing"] = bearing;
        e["enemy"] = sp.enemy;
        e["count"] = exact ? sp.count : -1;
        e["shadow_caster"] = enemy.shadowCaster;
        entries.push_back(e);
        if (exact && enemy.shadowCaster && enemy.hasHover && planet && planet->radius > 0.0f) {
          // Where it is expected to park: its lane bearing +- spawn jitter, widened by
          // the disc's own angular size seen on the surface.
          // TODO(M1.8/M1.9): replace with the shadow system's projected arc if it differs.
          const float ratio = enemy.size / planet->radius;
          const float half = enemy.hover.spawnJitter + std::asin(ratio > 1.0f ? 1.0f : ratio);
          ghosts.push_back(bearing - half);
          ghosts.push_back(bearing + half);
        }
      }
    }
    d["entries"] = entries;
    d["eclipser_ghost_arcs"] = ghosts;
    out.push_back(d);
  }
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
  d["time_scale"] = curr_->timeScale;
  d["sim_time"] = curr_->simTime;
  d["threaded"] = threadRunning_;
  d["seed"] = static_cast<int64_t>(seed_);
  return d;
}

// Live tuning (debug panel). Paths are "<group>.<json key>" for the current
// run's sun and planet and for rules; values use the JSON units (degrees).
// Tuned runs do not replay from their log: the log does not record tunables.
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
  };
  const CharString key = path.utf8();
  for (const Entry& e : table) {
    if (std::strcmp(e.path, key.get_data()) != 0) continue;
    if (e.field == nullptr) return false;
    wait_idle();  // the sim may read content and statics while it runs ticks
    *e.field = value * e.scale;
    // Keep the binding's static copies in step (planet size and HP are not tunable).
    if (sun) {
      statics_.sunStart = sun->start;
      statics_.beamWidth = sun->beamWidth;
      statics_.beamStrength = sun->baseStrength;
    }
    // TODO(M1.11): if the World copies these values at new_run, forward the change to it here.
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
