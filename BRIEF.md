# Orbitfall — Prototype Brief (complete build handoff)

> **This is the one document a building agent needs.** It holds the game design, the technical architecture, the maths, the milestone tasks and the working rules. `PLAN.md` (roadmap detail), `DESIGN.md` (open questions and toys) and `CLAUDE.md` (repo rules, auto-loaded by Claude Code) agree with it. Precedence: `docs/design/DECISIONS.md` (once it exists) > this brief > PLAN.md.
> Owner: Daniele. Last revised 2026-10-27. "Orbitfall" is a placeholder title. Sound and final art are out of scope: every visual is a placeholder.

---

## 0. The plan in plain words

1. **What we're making:** a tower defence on a small planet that you spin by hand. Towers fire in a cone, so spinning is aiming. Money comes only from solar panels sitting in a thin, slowly drifting sunbeam, and big aliens park in front of the sun to shade them. Every turn of the planet trades income against defence.
2. **How it gets built:** in milestones. **M0** sets up the project and an automatic link you open on your phone. **M1** is a small, complete 3 to 4 minute tower defence with the sun, shadows and two enemy lanes. After each milestone, **you play it** and decide whether it's fun before the next one starts.
3. **What comes after:** M2 adds depth (upgrades, weak points, shields, angled turrets and focus fire, drills, three home planets). M3 makes huge swarms run on a phone. M4 adds zooming out to enemy planets you must destroy to win. M5 adds your hand-fired weapon and level-up cards. M6 adds satellites. M7 is a full 20 to 30 minute run. M8 is polish and balance.
4. **How agents work:** one task per session, one pull request per task, each with tests and a playable preview link. Throwaway "toys" test risky ideas on your phone before the real version is built.
5. **What you need to do first:** create an empty GitHub repository for the game, then start a Claude Code session on it with the first task (§12).

---

## 1. The game

A mobile-first (landscape, two thumbs; PC later) **tower defence on a rotating planet**. Very little sunlight is left and aliens want to eclipse the rest. You place towers and **solar panels** on a ring of slots around your planet and **rotate the planet by hand**. Towers fire only within an arc around their aim axis, so rotating is aiming. Panels earn **stardust** (the one currency) only while inside a thin **sun beam that drifts** around the planet, and big enemies (**Eclipsers**) park between sun and planet and **cast shadows** that stop panel income. Enemies are Bloons-like: layered, readable at a glance, colour tier = health. Landing enemies subtract planet HP; at 0 you lose. The full game is won by destroying every enemy planet (each one dims the sun; killing it gives light back), then optional endless mode. Difficulty ramps Vampire-Survivors-style from a trickle to a screen-filling swarm.

**North star: it must feel like a tower defence first.** Place, shoot, earn, survive must be fun on its own before anything else is added. Anything that doesn't serve the loop yet is deferred, not built.

## 2. Pillars (every task protects these)

1. **Tower-defence loop first.**
2. **Chase the light.** The drifting beam and enemy shadows are the economy and the theme, from M1. Every system should touch light.
3. **Competing orientations.** Panels want the light, towers want their targets, shields want the falling threat, precision towers want weak points, focus points want to sit on the swarm. No single angle satisfies everything.
4. **Readable chaos.** Shape = movement class, colour tier = health, outline/badge = trait; HP bars only on large units. A player names the biggest threat within 2 seconds.
5. **Zoom is a mode switch:** Surface (build), Orbit (fight), System (plan), Sector (strategy).
6. **Scale is a feature,** so the sim is data-oriented from day one (scale *work* waits for M3).
7. **Content is data,** so new content never needs engine changes.

## 3. Hard design rules (decided by Daniele; do not reopen)

| Rule | Meaning for code |
|---|---|
| **No Autopilot, ever** | The player always rotates by hand. No auto-rotate, no auto-fire on player weapons |
| **Buildings are never damaged** | Landings and falling objects only subtract planet HP. No building HP, rubble or repair. Satellites are jammed, never destroyed (default, Q32) |
| **No hard counters** | Nothing immune or untargetable; every damage multiplier ≥ 0.25 (validated) |
| **Stardust only** | One currency. Sources: lit panels, drills (M2), a tiny kill bounty |
| **The forecast never lies** | Forecast and spawner read the same wave data |
| **≥ 3 answers per threat** | From different families: tower type; layout/rotation; weapon/ability; satellite/economy. Validated from M2, when enough content exists |
| **Light charges actives** | Buildings with a cooldown recharge `lightChargeMult` (default 2×) faster while lit and unshaded |
| **Sun is pluggable** | Sun movement goes through `SunPath`; prototype = constant drift |
| **Time** | Sim frozen between waves; during a wave time runs at `surfaceTimeScale` (0.75) while in the Surface band |
| **Win/lose** | Win: destroy all enemy planets (M4+; M1 = survive the last wave), then optional endless. Lose: planet HP 0 |

---

## 4. Game design reference

### 4.1 Planet, rotation and controls

- The planet sits at screen centre in landscape. A ring of `N` slots (36 on the M1 planet) holds buildings; M2 adds an elevated second layer (needs a foundation) and 1 to 3 slot footprints.
- **Home planets (M2):** small / medium / large. Size sets rotation speed (small = fast), HP, slot count and lit fraction (via fixed beam width, §6). No planet may be strictly best (bot check).
- **Rotation:** angular input in [-1, 1] accelerates toward `input × rotMaxSpeed` with `rotAccel`, decays with `rotDamping` when released. All from planet JSON.
- **Controls (landscape, two thumbs; defaults, toy D2 may change them):** left half: hold-and-drag horizontally to rotate, optional ⟲ ⟳ buttons. Right half: tap selects; at Surface zoom tap an empty slot → build menu → place; tap a building → info, aim, sell; long-press inspects. Pinch zooms anywhere; double-tap toggles Surface/Orbit. Start Wave button bottom right. Desktop: `A/D` or `Q/E` rotate, wheel zooms, click places, `Space` starts the wave, backtick toggles the debug overlay.

### 4.2 Light: beam, shadows, dimming

- **Sun beam:** a band of parallel rays of fixed world width from a sun bearing that drifts at constant speed (sim time only). Lit arc on the planet = `2·asin(W/2R)` (≈ 90° on the medium planet). Drawn as a soft bright cone in a dark sky; lit slots glow warm.
- **Shadows:** large enemies flagged `shadowCaster` (Eclipser from M1; Carriers and up, Swarmcloud clusters and enemy planets later) shade the surface points whose sun ray they block. Shaded slots tint dark and a dark wedge is drawn on the planet. Small enemies cast none.
- **Eclipser (M1):** large, slow dark disc with an HP bar. Flies to the sun line over your lit panels, tracks the drifting sun, hovers, then descends and lands if not killed. Killing it clears its shadow.
- **Dim rounds:** each wave has a `lightMult`. **Eclipse dimming (M4):** each living enemy planet multiplies beam strength down (floor above zero); destroying one restores part.
- **Light-charged actives (M2+):** cooldown buildings recharge 2× faster while lit. **Sun Lens** tower damage scales with light being channelled (§4.5).

### 4.3 Economy

- **Solar Panel:** earns `panelRate × beamStrength` per second while in the beam and unshaded.
- **Drill (M2):** steady income regardless of light; takes a slot; instability rises with drill count; past a threshold a drill overheats and shuts down for a cooldown. **Cooler** lowers instability. Nothing is ever damaged.
- **Kills:** a tiny stardust bounty (M1) and XP (M5). Panels are the economy.
- Spend on buildings, aim changes, upgrades (M2) and satellites (M6). Sell refunds 70%.
- Design targets: spend-limited early, slot-limited late; diminishing returns per building type if bots show snowballing.

### 4.4 Placement, aim and focus fire (building rotation)

- Each tower fires within ±`arcDeg` of its **aim axis**. By default the axis is the slot's outward normal, so rotating the planet aims it (Gatling ±40°, Cannon ±25°, Flak ±80°, Tesla 360°, Lance ±6°).
- **Bent aim (Daniele 2026-10-27, M2; exact form pending decision Q33).** Default proposal: towers flagged `aimMount` get a mount set at placement to one of `aimOffsetDeg` ∈ {−60, −30, 0, +30, +60}; the aim axis becomes slot normal + offset. Re-aiming costs 10% of the tower's price and is allowed only between waves. Shown at Surface zoom as a line along the axis. Purpose: a tower on the sunny side can lean toward a lane, and several towers can be made to converge. Limits: max ±60° (still cannot cover the far side), the arc stays the same width, and the Lance's weak-point rule ignores offsets (the Lance must still sit under its target).
- **Focus fire (combos).** Where the aim axes of two or more towers intersect within all their ranges, a **focus point** forms (planet-local, so it turns with the planet). A focus zone of radius `focusRadius` (30) is drawn as a small reticle. Each hit on an enemy inside a zone, from a tower whose axis feeds that zone, deals `+focusBonus` (25%) per additional feeding tower, capped at +50%. **Two Sun Lens beams** crossing in a zone merge into one beam beyond it with summed power and pierce. Because focus points rotate with the planet, **rotation sweeps the focus point across the sky**: one more reason to turn, not one fewer.
- **Adjacency (M2):** Amplifier and Radar buff towers within k slots; links drawn at Surface zoom.

### 4.5 Towers and buildings (prototype roster)

| Building | Role | Milestone |
|---|---|---|
| Gatling | fast single-target, pops layers | M1 |
| Cannon | splash, slow | M1 |
| Solar Panel | income when lit | M1 |
| Flak | wide arc, strong close, weak far | M2 |
| Railgun | pierce, constant damage, precision | M2 |
| Cryo | slow, freeze active | M2 |
| Radar | reveals camo nearby | M2 |
| Amplifier | adjacency buff | M2 |
| **Sun Lens** | continuous beam; damage × (0.2 + 0.8 × channel), channel = lit unshaded fraction of panel capacity; **Overcharge** active; replaces any Laser tower | M2 |
| **Lance** | ±6° arc, cheap, weak, no slot bonuses; built to exploit weak points by rotation | M2 |
| **Shield** | absorbs falling objects over a 60° arc; energy, recharge | M2 |
| Drill, Cooler | light-independent income and its relief | M2 |
| Tesla, EMP Spire, Mortar, Missile Silo | chain, anti-shield, far-strong, salvo active | M7 |
| Siege Cannon | damages enemy planets | M4 |

Every tower: 2 upgrade paths × 4 tiers (M2), targeting modes first/strong/close, falloff curve and gradient range ring.

### 4.6 Enemies, traits and readability

- **Colour tier = health** (red → blue → green → yellow → pink → black → white → ceramic); each layer popped steps down a colour. **Shape = movement class:** circle drifter, triangle fast, square tank, hexagon carrier, star boss. **Outline/badge = trait.** Small enemies never show HP bars; large units show a segmented bar. Trait legend one tap away.
- **Damage types:** Kinetic, Explosive, Energy, Cryo, EMP, Pierce. Counters are multipliers, never < 0.25.

| Trait | Cue | Soft effect | Easier with |
|---|---|---|---|
| Camo | dashed, translucent | ~half of undetected shots miss | Radar, Scanner sat, focus zones, splash |
| Armored | thick grey outline | Kinetic −50% | Explosive, Energy, Pierce |
| Shielded | glow ring | first X damage absorbed | EMP, Tesla, Overcharge |
| Fast | elongated, trail | outruns slow shots | Cryo, slow fields, hitscan |
| Regrow | badge | regains layers | burst, focus fire |
| Splitter | notched | splits into 2 to 4 | splash, chain |
| Heat-resistant | orange rim | Energy −50% | Kinetic, Cryo |
| Burrower | dips under | ~25% while buried | mines, timing |
| Orbiter | circles then dives | escapes fixed arcs | 360° towers, bent aim, timing |
| Healer | cross badge | buffs neighbours | priority targeting, actives |
| Bulwark | plate + bright wedge | body 0.25×, weak point 3–4× | Lance/Railgun lined up by rotation |
| Carrier | large ship + HP bar | spawns children; casts shadow | Siege, Railgun, actives |

- **Roster (~16):** Drifter, Dart, Eclipser (M1); Brick, Phantom, Bubble, Ember, Spore, Splitter, Bulwark, Bomber (M2); Mole, Moth, Medic, Warden (M7); Carrier, Dreadnought, Swarmcloud (500 units as one LOD cluster), one boss per enemy planet (M4).
- **Weak points:** a weak-point hit happens when the firing tower's world angle is within ±8° of the enemy's bearing (directly under it).

### 4.7 Shields and falling objects (M2)

Meteors, Bomber bombs and (M4) enemy-planet volleys fall on a telegraphed bearing (streak + edge indicator 3 s ahead). A **Shield** covers a 60° sky arc above its slot: it absorbs objects crossing it, losing energy equal to their damage; at zero it goes offline and recharges (2× faster while lit). Unshielded hits subtract planet HP only. Panels face the light, shields face the threat; the layout decides how well one angle can serve both. Optional stardust upkeep while online (Q23).

### 4.8 Forecast (from M1)

A HUD strip of upcoming waves. Next wave **exact**: light level (sun icon fill = `lightMult` × dimming), enemy icons with counts, bearing per lane, shadow-casters marked, and a **ghost shadow arc** on the planet where Eclipsers are expected to park. Waves +2 and +3: icons without counts. Beyond: light level only. Live shadow-casters get a strip marker and edge indicator. From M4 each entry also shows the **source enemy planet** with a spawn countdown, and scouting (Sector view, Scanner satellite) sharpens far waves.

### 4.9 Zoom bands and time

| Band | See | Do | From |
|---|---|---|---|
| Surface | slots, footprints, aim axes, focus reticles, lit/shaded slots | build, aim, sell, upgrade; time 0.75× | M1 |
| Orbit | planet, near space, all shots | main combat | M1 |
| System | incoming waves, long reach, high satellites | plan rotation ahead | M4 |
| Sector | enemy planets, lanes, favoured enemies, timeline | strategy | M4 |

Continuous zoom with band thresholds in data; UI cross-fades. Off-screen threats get edge indicators. Projectiles keep a minimum pixel size and turn into tracers when far.

### 4.10 Enemy planets and the run (M4)

Visible at Sector zoom, each with HP, shield, phases, a lane, a cadence and a **weighted spawn table** favouring a signature enemy (Forge ~50% armored, Veil ~50% camo, Bastion ~40% Bulwarks). Seven planets: Hive (spawn rate rises as damaged), Forge (armored shell phase), Veil (lane hidden until scanned), Garden (regrows), Shipyard (rare big launches), Comet (moves; lane changes), Bastion (moons first). Each living planet dims the sun and can cast a shadow. Damaged by Siege Cannon, max-tier Railgun, ultimates and Payload satellites. Destroying one removes its lane and pays a big reward. A run director ramps spawn budget over 20 to 30 minutes to 100k+ concurrent enemies.

### 4.11 Active play and progression (M5)

- **One manual weapon per run**, each with a skill tree (3 branches × 4 nodes; a point every second level): **Slingshot** (drag back from the core, piercing slug), **Target Missiles** (tap to mark, salvo after a delay, charges), **Orbital Gun** (satellite turning with the planet; altitude slider: low = fast, strong, short; high = slow, long, weak; light-charged). Backlog: Gravity Tractor, Rotation Railgun, Orbital Minefield. No weapon damages buildings.
- **Planet ability** picked per run (6): Ring Laser, Time Dilation, Magnetic Pulse, Spin Overdrive, Meteor Call, Terraform.
- **Tower actives** on cooldowns (Silo salvo, Lens Overcharge, Cryo freeze, Tesla storm), light-charged.
- **XP** → pick 1 of 3 cards (tower unlocks, path tiers, passives, rotation speed, beam width, shield arc, wider aim mounts, focus bonus); evolutions need two prerequisites. Minimal meta unlocks stored locally.

### 4.12 Satellites (M6)

Eight satellites (Slow Field, Proximity Mine, Scanner, Relay, Decoy, Gravity Well, Laser Sat, Shield Node) dragged onto orbit radii at Orbit/System zoom; geostationary, fixed or orbiting. Limit rule open (Q7, proposal: needs a ground uplink in range). Hunter enemies jam them.

---

## 5. Technical architecture

### 5.1 Stack (decided)

| Layer | Choice |
|---|---|
| Engine | **Godot 4**, latest stable 4.x at M0.1 (≥ 4.3), pinned in `project.godot` and CI |
| Simulation | **C++17** library in `core/`, no Godot includes, built with CMake, tested with doctest |
| Binding | **GDExtension** (`gdextension/`, godot-cpp, SCons): `SimServer` node, command API, snapshots → MultiMesh buffers |
| Glue | Statically typed **GDScript** (untyped declarations are errors): input, HUD, menus, flow. Nothing per entity |
| Rendering | `RenderingServer` **MultiMesh**, one batch per visual archetype, packed float buffer via `multimesh_set_buffer`; SDF `canvas_item` shaders; `INSTANCE_CUSTOM` = shape id, colour tier, trait flags. Planet, towers and UI may be nodes |
| Renderers | Mobile on phones, Forward+ desktop, Compatibility on web |
| Physics | None for entities; uniform spatial grid in `core/` |
| Content | JSON in `content/`, validated by the C++ loader |
| Tests | doctest, GdUnit4 headless, `core_bench`, band screenshots under `xvfb-run` |
| CI | GitHub Actions: build, tests, bench budget (from M3), web + Android exports, Pages deploy |

Not C#: iOS export is experimental and C# cannot export to web.

### 5.2 Folders

```
project.godot
core/            sim/, pools/, systems/, spatial/, content/, tests/, bench/
gdextension/     SimServer binding, SConstruct
content/         planets/ sun/ buildings/ enemies/ waves/ (later: falling/ satellites/ weapons/ abilities/ cards/)
game/            render/ input/ ui/ app/ tests/
toys/            throwaway GDScript design toys (no tests, no perf rules)
tools/sim-runner headless bot runs
docs/design/     DECISIONS.md, reviews/
docs/STATUS.md   task checklist
```

### 5.3 Simulation model

- **Fixed timestep 30 Hz** on its own thread; render interpolates the last two snapshots. A time-scale command (pause, Surface slow-down) changes `dt` deterministically.
- **Determinism:** seeded `xoshiro128**`; no wall clock or unseeded randomness in `core/`. A run = seed + command log; replay gives identical state hashes. Single-threaded mode (web) gives identical hashes.
- **Commands in:** `RotateInput(value)`, `PlaceBuilding(slot, id, aimOffset)`, `SellBuilding(slot)`, `SetAim(slot, offset)`, `SetTargeting(slot, mode)`, `StartWave`, `SetTimeScale(scale)`, later `FireWeapon(…)`, `ActivateAbility(…)`, `PlaceSatellite(…)`. Every command carries the tick it applies on.
- **Events out:** `LayerPopped`, `EnemyKilled`, `PlanetHit`, `StardustChanged`, `WaveStarted`, `WaveCleared`, `ShadowChanged`, `GameOver`, `Victory`, later `LevelUp`.
- **Pools:** fixed-capacity SoA arrays (pos, vel, hp/layers, type, trait bits, flags), reserved at load, swap-remove. No class per entity, no allocation in per-entity loops.
- **Spatial grid:** uniform grid rebuilt each tick by counting sort; a coarse grid for the sector later.
- **Buildings** are few: a plain array indexed by slot (type, level, aimOffset, cooldowns, lit/shaded flags). Focus points are recomputed only when a building or aim changes.
- **Parallelism (M3):** movement, targeting and broad-phase in chunks on `WorkerThreadPool`.

**Tick pipeline (full):** `applyCommands → planetRotation → sunlight (beam, shadows, lit flags, channel) → spawners → clusterLOD → enemyMovement → fallingObjects → spatialRebuild → towerTargeting → towerFire → projectileMovement → collisions → shields → damageResolve (incl. focus and weak-point multipliers) → landing → income → satelliteEffects → abilities → statusEffects → xp → cleanup → snapshot`. Each system is `void name(World&, float dt)` registered in order in `core/sim/pipeline.cpp`; M1 implements only what it needs.

### 5.4 Content schemas (examples; loader validates ids, references and multipliers)

```json
// content/buildings/cannon.json
{ "id": "cannon", "kind": "tower", "footprint": 1, "layer": "ground", "cost": 90,
  "arcDeg": 25, "range": 450, "fireRate": 0.8, "aimMount": true,
  "projectile": { "kind": "shell", "speed": 600, "damage": 3, "type": "explosive", "splash": 40 },
  "falloff": [[0, 1], [1, 1]], "targeting": ["first", "strong", "close"],
  "paths": [], "active": null, "visual": { "shape": "square", "color": "tower.cannon" } }

// content/enemies/drifter_green.json
{ "id": "drifter_green", "shape": "circle", "layers": 3, "speed": 40, "size": 8,
  "landDamagePerLayer": 1, "bounty": 1, "traits": [], "shadowCaster": false, "popsInto": "drifter_blue" }

// content/sun/default.json
{ "type": "constant_drift", "startDeg": 0, "speedDegPerSec": 1.5, "beamWidth": 141, "baseStrength": 1.0 }

// content/waves/m1.json (one entry)
{ "wave": 5, "lightMult": 0.5, "spawnSeconds": 20,
  "spawns": [ { "lane": "A", "enemy": "drifter_green", "count": 14 },
              { "lane": "B", "enemy": "dart", "count": 8 } ],
  "forecast": { "exactAhead": 1, "iconsAhead": 3 } }
```

Tunable numbers live in `content/`, never in systems. Engine code never names a content id.

### 5.5 Rendering rules

One packed buffer per archetype (enemy shapes, projectile dot, tracer, beam, ring, satellite). Zoom-aware size `clamp(worldSize × zoom, minPx, maxPx)` so shots stay visible zoomed out; far fast shots draw as tracers. Cull by grid cells in the camera rect. Shadows: dark wedges on the planet; beam: additive soft cone.

### 5.6 Performance budgets

| Platform | Live entities | Frame |
|---|---|---|
| Mid-range Android 2021–23 | **150k at 60 fps** (300k at 30) | sim ≤ 6 ms/tick, render ≤ 8 ms |
| Desktop iGPU | 500k–1M at 60 fps | |

About 48 bytes per instance (150k ≈ 7 MB/frame upload): pack tightly, cull, skip unchanged batches. "Millions" comes from simulation LOD: swarms as aggregate clusters far away, dense gunfire as volleys or analytic hitscan, still drawn as individuals. From M3, `core_bench` scenarios (`100k-enemies`, `500k-projectiles`, `mixed-late-game`) fail CI on > 10% regression.

### 5.7 Testing

Core unit tests on tiny worlds; determinism (3,000 ticks twice, both threading modes); content validation; GdUnit4 per gesture and UI flow; band screenshots (`--screenshot-bands out/`); bot runs from `tools/sim-runner` (M2+) for design questions: never-rotate, chase-light, face-threat, rotate-to-weak-point, focus-builder, balance, bank-and-drill.

### 5.8 Playtest builds

| Build | When | Link |
|---|---|---|
| Latest | every push to `main` | Pages `/latest/` |
| PR preview | every PR | `/pr-<n>/`, link + QR comment |
| Milestone | tag `m1`, `m2`… | `/m1/` + GitHub Release |
| Toys | pushes touching `toys/` | `/toys/<name>/` |
| Android debug APK | milestone tags, on demand | Release asset |

Web uses the **no-threads** web template with the wasm GDExtension (no cross-origin isolation needed on Pages); web is for feel, APK and desktop for performance. Every build shows its version, a debug overlay toggle and a **"Copy run log"** button (seed + commands) for exact replays. iOS arrives in M8.

### 5.9 Commands

- `scons -C gdextension` (debug) / `target=template_release`; `scons -C gdextension platform=web`
- `cmake -S core -B build && cmake --build build && ./build/core_tests`; `./build/core_bench`
- `godot --headless --path . -s addons/gdUnit4/bin/GdUnitCmdTool.gd`
- `xvfb-run godot --path . -- --screenshot-bands out/`
- `godot --headless --export-debug "Android" build/game.apk`; `--export-release "Web" build/web/index.html`

---

## 6. Maths

**Frames.** World 2D, planet centre at origin, radius `R`, rotation `ρ`. Slot `i` of `N` at local angle `θᵢ = 2πi/N`, world angle `θᵢ + ρ`. Enemies, sun and projectiles are in world space.

**Beam.** Sun bearing `σ(t)` from `SunPath`; `s = (cos σ, sin σ)`. A surface point at world angle `φ` is in the beam iff `cos(φ − σ) > 0` and `|R·sin(φ − σ)| ≤ W/2`. Lit arc `= 2·asin(W/2R)`; medium planet `W = 1.41R` → ~90°. `beamStrength = baseStrength × wave.lightMult × Π(planet dimming)`.

**Shadows.** Caster disc centre `p`, radius `r`. Casts iff `dot(p, s) > R`. Offset `o = s.x·p.y − s.y·p.x`. A lit point at `φ` is shaded iff `R·sin(φ − σ) ∈ [o − r, o + r]`; convert to an angular interval via `φ = σ + asin(clamp(y/R, −1, 1))`, merge intervals per tick, test slots. Must match a brute-force ray test; 1k casters < 0.2 ms.

**Channel (Sun Lens).** `channel = Σ(lit unshaded panels × beamStrength) / total panels` (0 if none); Lens damage × `(0.2 + 0.8·channel)`.

**Tower arc.** Aim axis world angle `α = θᵢ + ρ + aimOffset`. Tower at `T = R(cos(θᵢ+ρ), sin(θᵢ+ρ))` may target enemy `E` iff `|E − T| ≤ range` and the angle between `(E − T)` and `(cos α, sin α)` ≤ `arcDeg`.

**Focus points.** For towers A and B with positions `T_A, T_B` and axis directions `d_A, d_B`, solve `T_A + a·d_A = T_B + b·d_B`; a focus point exists iff `a, b > 0` and within both ranges. Compute in planet-local coordinates when buildings or aims change (O(n²) over aim-mounted towers, n ≤ ~40), merge points closer than `focusRadius`, rotate by `ρ` each tick for hit tests.

**Weak points.** Hit is weak-point iff `|wrap(θ_tower_world − bearing(E))| ≤ 8°`.

**Landing.** Enemy reaching `R` is removed and deals `landDamagePerLayer × remainingLayers` (flat value for non-layered units).

---

## 7. M0: Foundations (an empty project that builds, tests and publishes a link)

| ID | Task | Deps | Acceptance |
|---|---|---|---|
| M0.1 | Scaffold: Godot project, `core/` + doctest, `gdextension/` + godot-cpp, GdUnit4, GitHub Actions, **cloud environment setup script** (pinned Godot, export templates, SCons, Emscripten, Android SDK/JDK) | — | A scene loads the extension and shows a value from C++; tests green locally and in CI |
| M0.6 | Playtest pipeline (§5.8) | M0.1 | `/latest/` opens on Android and iPhone; PRs get preview links automatically. Do straight after M0.1 |
| M0.2 | Sim core: World, fixed step, time scale, RNG, command/event queues, state hash | M0.1 | 3,000 ticks twice → equal hashes; check bans `rand()`, `time()`, `<chrono>` clocks in `core/` |
| M0.3 | SoA pool library | M0.1 | Add/remove/iterate tests; no allocation after reserve |
| M0.4 | MultiMesh renderer: archetype batches, 7 SDF shapes, palette texture | M0.1 | 20k animated instances at 60 fps desktop; CI screenshot |
| M0.5 | `SimServer`: sim thread, double-buffered snapshots, command API, interpolation, single-threaded mode | M0.2, M0.4 | GdUnit4 round-trip; both modes give equal hashes |
| M0.7 | Camera: continuous zoom, min pixel sizes | M0.4 | 0.05× to 20× smooth |

## 8. M1: First playable tower defence (3 to 4 minutes, win and lose, in a phone browser)

### 8.1 Scope

- **In:** medium planet (36 slots, ground layer, 1-slot footprints); **Gatling, Cannon, Solar Panel**; **Drifter** (1–3 layers), **Dart**, **Eclipser**; **two lanes** at 30° and 170°; drifting beam; shadows; 8 waves with dim rounds; intermission; forecast strip; Surface and Orbit bands; Surface slow time; win/lose/restart; debug overlay and live tuning panel. Tower data already carries `aimMount`/`aimOffset` (default 0, no UI) so M2 needs no refactor.
- **Out:** everything else (§4 milestones).

### 8.2 Starter numbers (placeholders, all in `content/`)

Planet: `radius 100, slots 36, hp 100, rotMaxSpeed 90°/s, rotAccel 360°/s², rotDamping 4/s, startStardust 150`. Sun: `start 0°, speed 1.5°/s, beamWidth 141, baseStrength 1`.

| Building | Cost | Arc | Range | Fire | Projectile |
|---|---|---|---|---|---|
| Gatling | 50 | ±40° | 350 | 6/s | dot, speed 900, dmg 1 |
| Cannon | 90 | ±25° | 450 | 0.8/s | shell, speed 600, dmg 3, splash 40 |
| Solar Panel | 40 | — | — | `panelRate 3/s` lit and unshaded | — |

| Enemy | Shape | Layers / HP | Speed | Size | Land dmg | Bounty |
|---|---|---|---|---|---|---|
| Drifter | circle | 1 red, 2 blue, 3 green (1 HP per layer) | 40 | 8 | 1 per layer | 1 |
| Dart | triangle, trail | 1 red | 110 | 6 | 1 | 1 |
| Eclipser | dark disc, HP bar | 40 HP | 25 | r 28 | 15 | 15 |

Eclipser behaviour: fly to hover radius 260 on the sun line with offset over the most lit panels, track the sun, hover 15 s, descend at speed 10, land if alive. Spawns at radius 900 on lane bearing ±6°.

| Wave | lightMult | Lane A (30°) | Lane B (170°) |
|---|---|---|---|
| 1 | 1.0 | 8 red Drifters | — |
| 2 | 1.0 | 10 blue Drifters | 4 red Drifters |
| 3 | 1.0 | 6 Darts | 10 blue Drifters |
| 4 | 1.0 | 12 blue Drifters | 1 Eclipser |
| 5 | **0.5** | 14 green Drifters | 8 Darts |
| 6 | 1.0 | 1 Eclipser + 10 Darts | 16 blue Drifters |
| 7 | 0.7 | 20 green Drifters | 1 Eclipser + 12 green Drifters |
| 8 | 1.0 | 2 Eclipsers + 15 Darts | 25 green Drifters |

Spawns spread over 15 to 25 s per wave. Balance target: ignoring the sun loses by wave 6; only chasing the sun leaks heavily on one lane.

### 8.3 Flow

Intermission: sim frozen; rotate, build, sell, read the forecast; Start Wave resumes (auto-start timer in data, off). Wave: normal time, 0.75× in the Surface band (eased over 0.3 s, as a command). Win after wave 8 is cleared; lose at HP 0; end screens show seed, waves survived, Restart.

### 8.4 Tasks

| ID | Task | Deps | Acceptance |
|---|---|---|---|
| M1.1 | Planet entity, rotation physics from data | M0.2 | Accel, max speed and damping tests |
| M1.2 | Input → commands (left rotate, right tap/long-press, pinch, double-tap, keys) | M0.5, M1.1 | GdUnit4 test per gesture |
| M1.3 | Slot ring, placement, sell (70%) | M1.1 | No overlap; refund tests |
| M1.4 | Build menu, placement ghost with arc and range | M1.2, M1.3 | Ghost arc rotates with the planet |
| M1.5 | Grid, enemy pool, two lanes, movement, landing, GameOver | M0.3 | HP 0 emits GameOver; landing formula tested |
| M1.6 | Targeting (axis, arc, range, modes), projectiles, collisions, layer popping, splash | M1.3, M1.5 | Out-of-arc never targeted; `aimOffset` honoured in a unit test; splash and layer tests |
| M1.7 | `SunPath`, beam test, panel income, cone and slot tint, stardust HUD | M1.3 | Lit earns, unlit doesn't; second `SunPath` type swaps in; lit arc formula tested |
| M1.8 | Shadows + Eclipser, wedge and tint | M1.7, M1.5 | Brute-force match; kill clears shadow; 1k casters < 0.2 ms |
| M1.9 | Waves from JSON, `lightMult`, intermission, Start Wave, win/lose/restart, forecast strip (§4.8, lane as source) | M1.5, M1.6, M1.8 | Strip matches actual spawns for all 8 waves; dim wave weakens beam and income; replay deterministic |
| M1.10 | Surface/Orbit bands, cross-fade, slow time, double-tap | M0.7, M1.9 | Time eases; logged as a command |
| M1.11 | Debug overlay (fps, ms/tick, counts, band, sun bearing, shade intervals, income/s, taps/min, % time lit) + live tuning panel | M0.5 | Editing sun speed changes drift live |

**Feel gate M1** (Daniele plays `/m1/` and the APK): Does this feel like a tower defence? Does chasing the light and dodging shadows feel central? Is rotating by hand fun, not a chore?

## 9. Later milestones (one line per task; full criteria in PLAN.md §9)

**M2 Depth (rotation is aiming).** 2.1 elevated layer, footprints, adjacency · 2.2 upgrade paths 2×4 + UI · 2.3 damage types, traits, colour tiers, HP bars, codex · 2.4 weak points, Bulwark, Lance (never-rotating bot kills Bulwarks at < 25% the rate of a rotating bot) · 2.5 enemies batch 1 · 2.6 Flak, Railgun, Cryo, Radar, Amplifier, Sun Lens · 2.7 Drill, instability, Cooler · 2.8 three home planets · 2.9 sim-runner bots + "≥ 3 strategies beat each threat" check · 2.10 falling objects, Shield, Bomber · **2.11 bent aim and focus fire** (aim mounts, `SetAim`, focus-point solver, reticles, focus bonus, Lens beam merge; a focus-builder bot beats a no-focus bot on swarm waves by 10 to 40%, not more; after toy D7). Gate: is rotation a real decision without overload; is anything obviously best?

**M3 Scale.** bench + CI budget · projectile LOD · cluster LOD (Swarmcloud) · render pass (tracers, culling, packing; 200k+ desktop) · threading + zero-allocation audit · Android perf check · readability at 100k (name the biggest threat in 2 s). Gate: does a huge wave still feel good?

**M4 Wider battlefield.** System and Sector bands, edge indicators · enemy planets, lanes from planets, dimming and planet shadows · weighted spawn tables, threat timeline, scouting · Siege Cannon, win, endless · planets batch 1 and 2 · run pacing and enrage rules. Gate: does zooming out give real planning decisions?

**M5 Active play.** weapon framework + skill trees · Slingshot · Target Missiles · Orbital Gun · extra weapon(s) chosen at Q26 · tower actives (light-charged) · planet abilities · XP and cards · 25+ passive cards.

**M6 Space layer.** satellite pool, orbits, placement, limit rule · two satellite batches · hunters that jam satellites, Payload satellite.

**M7 Full run.** run director to 100k+ · towers batch 2 and 3 · enemies batch 2 · full game flow (title → planet → weapon → ability → run → summary → endless) · save, settings, meta unlocks, resume with equal hash.

**M8 Phones, desktop, balance.** telemetry + report · data-only balance pass · release Android/iOS · desktop exports · touch UX pass.

**Prototype done when** a full 20 to 30 minute run plays on a phone with all four zoom bands, 15 defence buildings, 3 economy buildings, 8 satellites, ~16 enemies, 7 enemy planets, 6 abilities, 3+ weapons with trees, 25+ cards, and 100k+ late-game entities at target frame rate.

## 10. Design track (alongside the build)

- **Toys** are single GDScript scenes in `toys/`, built in a day, published at `/toys/<name>/`, played by Daniele on a phone. **D2** rotation and control schemes (before M1.2 merges) · **D3** sun, shadows and economy tuning (before M1.7 merges) · **D4** zoom and slow time · **D5** weapons (before M5.1) · **D7** bent aim and focus fire (before M2.11): do focus points read clearly, does the sweep feel good, does bending kill the panels-vs-targets tension?
- **D1** seeds `docs/design/DECISIONS.md` from DESIGN.md with M0; **D6** picks backlog ideas at the M2 gate.
- **Bots** answer balance questions; **a design-critic agent** files `docs/design/reviews/MN.md` (5 problems, 5 opportunities, each with a test) at every gate.
- **Feel metrics** in the debug overlay: taps/min, time per band, % time with a target in arc, income lost to shade or drift, leaks per lane.

## 11. Working rules for agents

1. **One task per session**, one branch (`m1-6-targeting`) and one PR titled `[M1.6] …` per task. Implement exactly that task and its acceptance criteria; don't build ahead.
2. **Data-oriented sim in C++.** SoA pools; no object per entity; no heap allocation in per-entity loops.
3. **No scene-tree node per entity;** many-instance things only through MultiMesh. **No Godot physics** for entities.
4. **Determinism:** seeded RNG only, no wall clock in `core/`, every player action is a command.
5. **`core/` has no Godot includes** and builds and tests standalone.
6. **Content is data;** new content goes in `content/**` with a test in `core/tests/content/`; never hard-code content ids or tunable numbers in systems.
7. **Readability rules** (§4.6) and **placeholder visuals only** (SDF shapes, palette colours; no image or audio assets).
8. **Perf budgets are law** from M3: a > 10% `core_bench` regression fails unless the PR justifies a new baseline.
9. **Scenes are shared text:** small `.tscn` files, repeated UI built in code, never edit a scene another open task is editing.
10. **Conventions:** systems `void name(World&, float dt)`; pools iterated `for (uint32_t i = 0; i < pool.count; ++i)`; radians internally, degrees in JSON; towers stored in planet-local polar coords; typed GDScript; PRs under ~800 changed lines (split and note it in STATUS.md otherwise).
11. **Definition of done:** acceptance ticked in the PR; extension builds; core and Godot tests pass; bench within budget when sim/render touched; new systems have tiny-world unit tests; visual changes include band screenshots; the PR preview link loads and shows the change; `docs/STATUS.md` ticked.
12. **Parallelism:** tasks in a milestone run in parallel only when they touch different folders; engine-core tasks (M0, M1.5, M3.x) one at a time. The next milestone starts only after Daniele's feel gate.
13. **Design questions** an agent hits mid-task: pick the default in this brief or DESIGN.md, note it in the PR and in DECISIONS.md, and flag it for Daniele. Never reverse a §3 rule.

## 12. Open questions (defaults in force; none block M1)

| Q | Question | Default |
|---|---|---|
| Q20 | Reference games and feel targets | Bloons (enemies), Vampire Survivors (escalation); awaiting Daniele |
| — | Title | "Orbitfall" placeholder |
| Q30 | Forecast fidelity | Next wave exact, +2/+3 icons, beyond light only |
| Q32 | Can satellites be destroyed? | No, they are jammed |
| Q33 | Where bent aim comes from | Player sets a mount angle at placement (asked) |
| Q34 | Focus fire strength | +25% per extra feeding tower, cap +50%; Lens beams merge |
| Q17 | Kill bounty | Tiny (1 per kill) |
| Q7 | Satellite limit | Ground uplink in range |
| Q9 | Slingshot aim | Toy D5 picks |
| Q23 | Shield upkeep | Small upkeep while online, bot-tested |
| Q26/Q27 | Extra weapons; weapon skill points | Three weapons; a point every second level |

## 13. Getting started

1. **Daniele:** create an empty GitHub repository and enable GitHub Pages from Actions. Agents cannot create repositories.
2. A Claude Code cloud environment whose network policy allows downloading Godot, export templates, godot-cpp, Emscripten and the Android SDK (the setup script comes from M0.1).
3. Copy `BRIEF.md`, `PLAN.md`, `DESIGN.md` and `CLAUDE.md` to the repo root.
4. First prompt: *"Implement task M0.1 from BRIEF.md. Follow CLAUDE.md. Open a PR titled `[M0.1] …` with the acceptance criteria ticked."* Then M0.6, then the rest of M0 and the toys D2 to D4 in parallel.
