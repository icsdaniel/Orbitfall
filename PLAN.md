# Orbitfall — Gameplay Prototype Plan

> Working title "Orbitfall" (placeholder). Mobile-first tower defence on a rotating planet, PC later. Engine: **Godot 4**.
> Scope: the full gameplay prototype. Sound and final art are out of scope; every visual is a placeholder (flat SDF shapes, colors, text labels).
> **Build handoff: start from `BRIEF.md`** (self-contained spec for M0 and M1 with starter numbers and the light/shadow maths; it wins over this file where they differ).
> For Claude Code agents. Pair with `CLAUDE.md` (standing rules) and `DESIGN.md` (decisions log, open questions, design toys). Each task in §9 is sized for one agent session and has acceptance criteria.
> Decisions made by Daniele are marked **(decided)**. Anything in `docs/design/DECISIONS.md` overrides this file.

---

## 1. North star: it must feel like a tower defence first

The first thing built is a small, complete tower defence you can win and lose in your phone browser: enemies walk in, you place towers, you spend currency, waves get harder. Everything else (zoom bands, satellites, enemy planets, abilities, scale tech) is layered on top of that loop, **never in front of it**. Each milestone ends with a **feel gate**: Daniele plays the build and answers "does this feel like a tower defence, and is it more fun than the last one?" before the next milestone starts.

Rules that follow from this:

- A playable loop (place, shoot, earn, survive) exists at M1, before any scale work, sector view or progression.
- Every milestone ships a playable link (§8). No milestone is "infrastructure only" after M0.
- Anything that doesn't serve the loop yet is **deferred** (see §11), not dropped.

## 2. The game

**Core foundation (decided): chase the light.** There is very little sunshine left, and hostile aliens want to eclipse what remains. You gather the little light that is available with solar panels on your planet, and you clean the sky of the threat to give the sun back. Two things make this the heart of the game: the sun beam is thin and always moving, and **enemies cast shadows on the planet**. Big dark enemies drift between the sun and your planet and put your panels in shade; enemy planets slowly dim the sun itself; destroying them restores it. Placing panels, rotating, and killing the shadow-casters are all one problem: where is the light, and who is blocking it. The placeholder visuals lean on this: a thin bright beam in a dark sky, visible shadow wedges falling across the planet, and enemy planets drawn as dark discs biting into the sun.

You own a planet in the middle of a landscape screen. You **rotate it** and **zoom** from the surface (where you arrange buildings) out to the sector (where enemy planets that launch waves are visible). Towers on the surface fire into space within a limited arc, so rotating the planet aims them. **Solar panels** earn stardust only while they sit in a drifting beam of sunlight, so turning the planet to fight pulls your income out of the light. **Shields** are the second thing to rotate into place: objects fall from space onto the planet, and shields only protect the arc they face. So panels must face the light while shields must face the threat, and the layout you build decides how well you can satisfy both. Enemies are Bloons-like: layered, readable at a glance, with no hard counters but towers that make some of them easier. Some enemies have **weak points** that only precisely aimed shots exploit, which is what makes rotation matter. Difficulty ramps Vampire-Survivors style from a trickle to a screen-filling mess. You win by **destroying every enemy planet**, then may continue in optional **endless mode**; you lose when planet HP reaches zero. Landing enemies simply subtract planet HP.

### Decisions so far (Daniele)

| Area | Decision |
|---|---|
| Engine | Godot 4; C++ GDExtension simulation core, typed GDScript for UI and flow |
| Screen | Landscape, two thumbs: left thumb rotates, right thumb places and acts |
| Target phone | Mid-range Android, 2021 to 2023: 150k live objects at 60 fps |
| Run | Win by destroying all enemy planets, then optional endless mode |
| Landing | Landing enemies only subtract planet HP |
| Counters | No hard counters or immunities; every tower does at least 25% damage to everything |
| Rotation | Meaningful through weak points: Bulwark enemies plus the placement-neutral Lance tower. **No Autopilot (decided): the player always rotates by hand** |
| Buildings | **Buildings are never damaged or destroyed (decided).** Falling objects and landings only subtract planet HP |
| Light-charged actives | Buildings with an active ability and cooldown (shields first) recharge faster while their slot is in sunlight (decided) |
| Economy | One currency, stardust, from solar panels in a drifting sun beam plus drills limited by rising instability. No falling motes, no collectors |
| Planet | Size sets rotation speed, HP, slots and lit fraction |
| Light | **Chase the light (core foundation):** thin drifting sun beam, shadows cast on the planet by large enemies, enemy planets dim the sun and killing them restores it. Present from M1 |
| Time | Pause between waves (sim frozen), and time runs at 75% speed (`surfaceTimeScale` 0.75, tunable) while zoomed in on the surface |
| Enemy planets | Weighted spawn tables: each is more likely to send its signature enemy, shown in the Sector view |
| Scale | The feeling of millions is enough, as long as it delivers |
| Demos | Every push and PR is playable from a browser link; Android APK on milestone tags |

## 3. Design pillars (every task protects these)

1. **Tower-defence loop first.** Place, shoot, earn, survive must always be fun on its own.
2. **Chase the light.** The thin moving sun beam and the shadows enemies cast on the planet are the foundation of the economy and of the theme; they exist from M1 and every system should interact with them.
3. **Three orientations compete.** Panels want to face the light, shields want to face the falling threat, towers want their targets (and weak points). Turning the planet is always a real decision, and no single angle satisfies everything.
4. **Readable chaos.** At 100k+ objects the player still knows what each enemy is, how hurt it is, and what makes it easier. Shape = movement class, color tier = health, outline/badge = trait.
5. **Zoom is a mode switch,** not just a camera.
6. **Scale is a feature,** so the simulation is data-oriented from day one.
7. **Content is data,** so agents can add content without touching engine code.

---

## 4. Tech stack and architecture

| Layer | Choice | Why |
|---|---|---|
| Engine | **Godot 4** (latest stable 4.x, pinned in `project.godot` and CI) | Open source, text scenes/resources, exports to Android, iOS, Windows, macOS, Linux and web, runs headless for agents |
| Simulation core | **C++ via GDExtension** (godot-cpp), structure-of-arrays, fixed timestep, own thread with `WorkerThreadPool` jobs | Native speed incl. iOS. A plain C++ library with a thin binding, so it builds and tests **without Godot** |
| Glue, UI, flow | Typed **GDScript** | Menus, HUD, build UI, run state. Nothing per entity |
| Rendering | `RenderingServer` + **MultiMesh**, one batch per visual archetype, filled each frame from a packed float buffer; SDF `canvas_item` shaders; palette tier via `INSTANCE_CUSTOM` | No node per entity; one upload call per archetype |
| Heavy projectiles (later) | RenderingDevice compute | Stretch goal only |
| Renderers | Mobile on phones, Forward+ on desktop, Compatibility on web/fallback | |
| Content | **JSON** in `content/`, validated by the C++ loader | Agents edit text; testable without Godot |
| Physics | None from Godot for entities; custom spatial grid | Physics bodies don't scale to 100k+ |
| Tests | doctest (core), GdUnit4 (headless Godot), bench executable, band screenshots under `xvfb-run` | Runs in CI and cloud containers |
| CI | GitHub Actions: build extension, doctest, bench budget, Godot tests, web + Android exports, Pages deploy | Perf regressions fail the build |

**Why not C#:** Godot 4's C# export to iOS is still experimental and C# can't export to web. C++ for the core and GDScript for glue is the safer mobile-first choice.
**Agent environment:** cloud containers need the Godot binary, export templates, SCons and Emscripten installed by the environment's setup script (and network access to fetch them). Task M0.1 writes it.

### Scale targets

| Platform | Live entities (enemies + projectiles + effects) | Frame |
|---|---|---|
| Mid-range Android phone (2021–2023) | **150k** at 60 fps (300k at 30 fps) | sim ≤ 6 ms/tick, render ≤ 8 ms |
| Desktop, integrated GPU | **500k to 1M** at 60 fps | |
| Desktop, compute projectiles | 2M to 5M projectiles | stretch |

Instance upload is the tightest Godot budget: about 48 bytes per instance, so 150k instances is about 7 MB per frame. Pack tightly, cull by grid cell, skip unchanged batches. "Millions on screen" comes from **simulation LOD**: distant swarms run as aggregate clusters, dense gunfire runs as volleys or analytic hitscan, and both are still drawn as individuals.

### Folders

```
project.godot
core/            # C++ library, no Godot includes: sim, pools, systems, spatial, content loaders, tests, bench
gdextension/     # thin Godot binding: SimServer node, command API, snapshot -> MultiMesh buffers
content/         # DATA ONLY JSON: towers, enemies, buildings, planets, sun, satellites, upgrades, abilities
game/            # Godot project files: render, input, ui, app, tests
toys/            # throwaway design prototypes (GDScript scenes), exempt from tests
tools/sim-runner # headless bot runs for balance and design questions
docs/design/     # DECISIONS.md, reviews/
docs/STATUS.md
```

### Simulation model

- **Fixed timestep** 30 Hz (configurable) on its own thread; render interpolates between the last two snapshots. A **time scale** command lets zoom and pause slow or stop the sim deterministically.
- **Determinism:** seeded RNG (`xoshiro128**`), no wall clock or unseeded randomness in `core/`. A run is a seed plus a command log; replays reproduce identical state hashes.
- **Commands in, events out.** GDScript input produces commands (`RotatePlanet`, `PlaceBuilding`, `FireSlingshot`, …); the core emits events (`EnemyKilled`, `PlanetHit`, `LevelUp`).
- **Pools:** fixed-capacity SoA arrays (position, velocity, hp, type, trait bitmask, flags), reserved at load, swap-remove deletion.
- **Frames:** world space is 2D, planet at origin. Buildings live in planet-local polar coordinates; world angle = slot angle + planet rotation.
- **Spatial hash:** uniform grid rebuilt every tick with counting sort (linear, no allocations), plus a coarse grid for the sector.
- **Parallelism:** movement, targeting and collision broad-phase run in chunks on `WorkerThreadPool`; a **single-threaded mode** (needed by web builds without cross-origin isolation) must give identical state hashes.

**Tick pipeline:** `applyCommands` → `planetRotation` → `sunlight` → `spawners` → `clusterLOD` → `enemyMovement` → `spatialRebuild` → `towerTargeting` → `towerFire` → `income` → `satelliteEffects` → `abilities` → `projectileMovement` → `collisions` → `damageResolve` → `landing` → `statusEffects` → `xp` → `cleanup` → `snapshot`. Each system is `void system(World&, float dt)` over SoA arrays with no heap allocation in per-entity loops.

### Rendering

- One packed float buffer per visual archetype (enemy shapes, projectile dot, tracer, beam, ring, satellite, tower, planet), uploaded with `multimesh_set_buffer`.
- SDF placeholder shapes (circle, triangle, square, diamond, hexagon, ring, star); `INSTANCE_CUSTOM` carries color tier, trait flags (outline style) and shape id.
- Culling by grid cells in the camera rect; **zoom-aware sizing** `screenSize = clamp(worldSize * zoom, minPx, maxPx)` so bullets stay visible zoomed out; fast bullets draw as tracer lines when far.
- Planet, towers and UI may be normal nodes since they are few.

### Content as data

```json
{ "id": "cannon", "footprint": 1, "layer": "ground", "cost": 120,
  "arcDeg": 25, "range": 420, "fireRate": 0.8,
  "projectile": { "kind": "shell", "speed": 600, "damage": 12, "type": "explosive", "splash": 40 },
  "falloff": [[0, 1], [1, 1]], "targeting": ["first", "strong", "close"],
  "paths": [], "active": null, "visual": { "shape": "square", "color": "tower.cannon" } }
```

Content tasks touch only `content/**` plus a test in `core/tests/content/`. Validation: all JSON loads, ids unique, references exist, every trait lists easier answers from at least three different families (tower, layout/rotation, weapon/ability, satellite/economy), no damage multiplier below 0.25.

---

## 5. Game systems

### 5.1 Rotation and controls

- **Landscape, two thumbs (decided).** Left thumb zone rotates the planet: hold to spin, horizontal drag for fine control with inertia. Right thumb zone taps, places, fires actives and the slingshot. Pinch zooms. Optional on-screen rotate buttons. PC: `A/D` or `Q/E`, mouse drag, wheel zoom.
- Max angular speed and acceleration are data (and depend on the home planet); optional snap to slot boundaries at Surface zoom.
- **No Autopilot (decided).** The player always rotates by hand; the design leans on clear telegraphing (sun beam, threat streaks, edge indicators, visible arcs) instead of automation.
- **Gesture budget** (to be written from the toy results): drag in the left zone rotates, tap selects/targets, long-press places or inspects, drag from the core fires the slingshot, pinch zooms.

### 5.1b Wave forecast and multiple answers (decided, Daniele 2026-10-26)

The player always knows what is coming and can prepare with more than one valid solution.

- **Forecast strip (from M1).** A HUD strip shows the next waves: each entry has a **light level** (a dim or normal round, shown as a sun icon with a fill, driven by the run's `lightMult` per wave plus eclipse dimming), the **threat types** (enemy icons with counts, shadow-casters and falling-object volleys marked) and the **bearing** each threat comes from. The next wave is exact; further waves are shown with decreasing precision (icons without counts, then only the light level) unless scouted (Sector view, Scanner satellite) which sharpens them. The pause between waves is the time to act on it.
- **Dim rounds.** Some waves are dim: the sun beam is weaker for that wave (planned eclipse, an enemy planet nearby, a heavy Eclipser wave). Answers are several: bank stardust beforehand, build drills (light-independent income), build more panels, lean on Sun Lens-free towers, or kill the shadow-casters first.
- **Multiple answers per threat (content rule).** For every threat the player can name **at least three answers from different families**: *tower type* (e.g. splash vs armored), *layout and rotation* (e.g. put a shield or tower on the right bearing), *active ability or weapon*, *satellite or economy preparation*. Nothing is a hard counter (damage floor 25%), but a prepared defence of any family should hold. This is enforced as a content validation check and by bots: each threat must be beaten by at least three different scripted strategies.
- **Source and light conditions (Daniele 2026-10-26).** Each forecast entry also says **where it comes from and what limits the light**: the **source** (which enemy planet is about to spawn this wave, with its bearing and a countdown shown on its sector marker and as an edge indicator) and the **light conditions** (dimming by living enemy planets, planned eclipses, and where Eclipser shadows are expected to fall, drawn as a ghost shadow arc on the planet). Enemies already on the field that limit light (Eclipsers, shadow-casters) are marked live in the strip and on the planet. The player can therefore tell "this wave dims the sun by 40% and the Eclipsers arrive from the east, shadowing the east panels".
- Data: `content/waves/*.json` carries per-wave `lightMult`, spawn lists, bearings and forecast reveal rules; the same data feeds the strip and the simulation, so the forecast cannot lie.

### 5.2 Zoom bands

Continuous zoom with four semantic bands (thresholds in data), cross-fading UI:

| Band | What you see | What you do |
|---|---|---|
| **Surface** | Slot grid, footprints, adjacency links, tower arcs, lit/unlit slots | Place, sell, upgrade, plan layout; **time slows about 25% here (decided)** |
| **Orbit** | Planet and near space, all shots, low satellites | Main combat view |
| **System** | Incoming waves, long-range reach, high satellites | Plan rotation ahead of waves |
| **Sector** | Enemy planets, lanes, favored enemies, threat timeline | Strategy: pick targets, read upcoming waves |

The game also **pauses between waves (decided)**; the player can build and tap Start Wave early. Double-tap toggles Surface/Orbit. Off-screen threats get edge indicators. M1 ships Surface and Orbit only; System and Sector arrive in M4.

### 5.3 Surface placement

- A ring of **N angular slots** (e.g. 72), one layer in M1; M2 adds a second elevated layer (needs a foundation) and 1 to 3 slot footprints.
- Each tower has a **firing arc** around its outward normal (±25° cannon, ±80° flak, 360° tesla), so rotation is aiming.
- **Adjacency** buffs (Amplifier, Radar) within k slots, links drawn at Surface zoom.
- **Bent aim and focus fire (Daniele 2026-10-27, M2.11; form of the bend is DESIGN Q33).** Aim-mounted towers can be set to fire along slot normal + `aimOffsetDeg` (default steps 0, ±30°, ±60°; re-aim costs 10% between waves). Where two or more aim axes cross within range, a **focus point** forms in the planet frame: hits inside its zone gain +25% per extra feeding tower (cap +50%) and crossing Sun Lens beams merge. Focus points turn with the planet, so rotation sweeps them across the sky. Details and maths in BRIEF §4.4 and §6.
- **Buildings are never damaged (decided).** Landing enemies and falling objects only subtract planet HP. There is no rubble and nothing to repair.

### 5.4 Economy: sunlight and drills (decided)

Stardust is the one build currency. Spend it on buildings, upgrades and satellites. Kills give XP and a very small stardust trickle.

- **Sun beam and Solar Panels.** The beam is a band of parallel rays of world width `beamWidth`; it lights an arc of `2·asin(W/2R)` (about 90° on the medium planet, so bigger planets get a smaller lit fraction), drawn as a soft cone. Formulas in BRIEF §5. Solar Panels produce stardust at a steady rate only while their slot is inside the lit arc. Rotating to aim towers swings panels out of the light, so you balance income against defence.
- **Shadows (decided, core).** Sunlight is modeled as parallel rays from the sun bearing. Any **shadow-caster** (large enemies such as the M1 **Eclipser**, Carriers and up, Swarmclouds as clusters, and later enemy planets) that sits between the sun and the planet shades every surface point whose ray to the sun passes through it. Each caster yields a shaded interval on the planet's surface per tick (cheap: one interval per caster, merged); a slot earns only if it is inside the beam arc **and** outside every shade interval. Shadows are drawn as dark wedges on the planet and slots tint when shaded. Small enemies cast none (keeps it cheap and readable). Killing or shooting a caster moves or removes its shadow, so towers aimed at casters protect income.
- **Eclipse dimming (decided).** Each living enemy planet reduces beam strength (a data multiplier, bottoming out above zero); destroying a planet restores part of it.
- **The sun drifts (decided).** The bearing is a function of run time behind one `SunPath` interface; the prototype is `bearing(t) = start + speed * t` from `content/sun/*.json` (start, speed, arc width). Oscillating suns, eclipses or several suns are later data or new path types, not engine changes. Drifting also stops players parking panels in light forever.
- **Drills.** Extract stardust at a steady rate regardless of light. They take slots and build **instability** (rising with drill count). Past a threshold a drill **overheats and shuts down** for a cooldown; a Cooler building or pausing the drills relieves it. Nothing is ever damaged: drills are safe-in-the-dark income that costs space and uptime.
- **Light-charged actives (decided).** Any building with an active ability and a cooldown (Shield recharge, Silo salvo, Sun Lens overcharge, Cryo flash-freeze, Tesla storm) charges faster while its slot is lit and not shaded; the multiplier is data (`lightChargeMult`, default 2×). So light matters beyond panels: a shield that faces the threat **and** sits in the beam is back online twice as fast, and a shaded shield or tower is slow. This is the second reason to chase the light.
- **Home planets (decided).** Size sets rotation speed (small fast, large slow), HP, slot count and lit fraction. Stored in `content/planets/*.json`, picked before a run; no planet may be strictly best.

### 5.4b Shields and falling objects (core rotation mechanic; Daniele, 2026-10-26)

Objects fall from space onto the planet: meteors, debris, bombs dropped by Bombers, and later volleys thrown by enemy planets. They are not shot down by default; they arrive on a bearing and hit the surface.

- **Shield buildings** project a barrier over an arc of the sky above their slot (default about 60°). A falling object that crosses a shielded arc is absorbed: the shield loses energy equal to the object's damage, and a shield at zero energy goes offline and recharges (cooldown in data). Unshielded objects hit the planet.
- **What a hit does (decided):** an unshielded falling object subtracts planet HP only. Buildings are never damaged, so shields protect the HP bar and the tension is about uptime, not about saving particular slots.
- **Light-charged recharge (decided).** A shield recharges faster while its slot is lit (see §5.4), so a shield placed where the threat arc and the sun beam overlap is strongest.
- **The two-orientation puzzle.** Panels need the sun beam (bearing drifts); shields need the threat bearing (set by lanes, which change by wave and by enemy planet). The angle between your panel cluster and your shield cluster is fixed by where you built them, so you choose a layout that can serve both most of the time and rotate to favor whichever matters now. Drift and wave changes keep the best angle moving.
- **Shield upkeep (proposal, DESIGN Q23):** shields draw stardust over time while online, so over-shielding costs light.
- **Warnings:** falling objects telegraph their bearing and arrival time (a thin streak and an edge indicator) so the player can rotate in time; shields show their arc and energy.
- Data: `content/buildings/shield.json`, `content/falling/*.json` (meteor, bomb, volley). Core systems `fallingObjects` and `shields` run after `projectileMovement`. Falling objects are pooled SoA entities and count toward the scale budget.

### 5.5 Damage, traits and weak points

Damage types: Kinetic, Explosive, Energy, Cryo, EMP, Pierce.

**No hard counters (decided).** Nothing is immune or untargetable; every tower deals at least 25% of normal damage to every enemy. Counters are multipliers, so a wrong tower is slower, not useless.

**Weak points (decided).** Some enemies have a weak side that only precisely aimed shots reach: a hit counts as a weak-point hit when the firing tower's world angle is within ±`weakWindowDeg` (default 8°) of the enemy's bearing, i.e. the tower sits directly under it. Weak-point hits deal 3 to 4 times damage. Precision towers (Lance, Railgun) exploit it, but only if the planet is rotated to line them up. (Reviewed 2026-10-26: a weak side facing an arbitrary world bearing is unreachable, because shots from the planet arrive almost radially at long range.)

| Trait | Cue | Soft effect | Easier with |
|---|---|---|---|
| Camo | dashed, translucent | about half of undetected shots miss | Radar, Scanner satellite |
| Armored | thick grey outline | Kinetic −50% | Explosive, Energy, Pierce |
| Shielded | glow ring | first X damage absorbed | EMP, Tesla |
| Fast | elongated, trail | outruns slow shots | Cryo, slow fields, hitscan |
| Layered | color tier changes | none | high fire rate |
| Regrow | badge | regains layers | burst, burn |
| Splitter | notched | splits into 2 to 4 | splash, chain |
| Heat-resistant | orange rim | Energy −50% | Kinetic, Cryo |
| Burrower | dips underground | takes about 25% while buried | Seismic effects, mines |
| Orbiter | circles before diving | outside fixed arcs | 360° towers, timing |
| Healer / Shield-caster | cross badge, aura | buffs neighbors | priority targeting |
| Bulwark | plate with bright weak-side wedge | body about 25%, weak side 3 to 4× | Lance, Railgun; rotate to line up |
| Carrier | large ship + HP bar | spawns children | Siege, Railgun, actives |

**Readability:** small enemies never show HP bars; **color tier is health** (red → blue → green → yellow → pink → black → white → ceramic). Large units show a segmented HP bar with the count of children. A trait legend is one tap away. Shape = movement class (circle drifter, triangle fast, square tank, hexagon carrier, star boss).

**Projectile clarity across zoom:** minimum pixel size per archetype, tracer lines when far, damage falloff curves per tower with a gradient range ring (Flak strong close, Mortar stronger far, Railgun constant).

### 5.6 Roster targets

- **Enemies (about 16):** Drifter, Dart, Brick, Phantom, Bubble, Spore, Splitter, Ember, Mole, Moth, Medic, Warden, Bulwark, Carrier, Dreadnought, Swarmcloud (500 tiny units as one LOD cluster), plus one boss per enemy planet. Movement archetypes: straight, spiral-in, orbit-then-dive, zig-zag, formation, swarm.
- **Defence buildings (15, each with 2 paths × 4 tiers):** Gatling, Cannon, Flak, Railgun, **Sun Lens**, Tesla, Cryo, EMP Spire, Mortar, Missile Silo, Radar, Amplifier, **Shield**, Siege Cannon (reaches enemy planets), **Lance** (decided: narrow arc ±6°, deliberately placement-neutral: no slot bonuses, cheap, weak against everything else, so its only skill is rotating the planet to line it up).
- **Sun Lens (decided, Daniele 2026-10-26):** a turret that shoots **channeled light**. Its beam power scales with how much light you are channeling right now: the lit, unshaded fraction of your solar panel capacity (a `channel` value from 0 to 1, computed by the `sunlight` system each tick). Panels in the beam means a strong Lens; panels in shade or out of the beam means a weak one (floor of about 20% so it is never useless). So the Lens is a mirror of your economy: the same good rotation that earns stardust also makes it hit hard, and an Eclipser's shadow weakens both. Medium arc (about ±35°), continuous beam that ramps up on a target, active **Overcharge** (spend stored light for a burst, light-charged cooldown). It replaces the old Laser tower.
- **Economy buildings (3):** Solar Panel, Drill, Cooler.
- **Satellites (8):** Slow Field, Proximity Mine, Scanner, Relay, Decoy, Gravity Well, Laser Sat, Shield Node. Dragged onto an orbit radius at Orbit/System zoom; modes geostationary, fixed or orbiting. Following the never-damaged rule, satellites are never destroyed: enemies that hunt them **jam** them (offline for a while) instead (reviewed 2026-10-26; DESIGN Q32).
- **Several towers have an active** on cooldown (Silo salvo, Sun Lens overcharge, Cryo flash-freeze, Tesla storm).

### 5.7 Active play

- **Active weapon with its own skill tree (decided direction, Daniele 2026-10-26).** Before each run the player picks **one active weapon archetype**; each archetype has its own skill tree (about 3 branches of 4 nodes), so the choice changes how the run plays. All weapons are manual: you aim and fire them, there is no auto-fire and no Autopilot, and none can damage buildings (nothing can).
  - **Slingshot (decided):** drag back from the planet core and release to launch a heavy piercing slug; power scales with drag. Branches such as multi-shot, ricochet, and charged shots.
  - **Target Missiles (decided):** tap points in the sky to mark targets, release a salvo that flies to them after a short delay; charges regenerate. Branches such as more marks, splash, homing.
  - **Orbital Gun (decided):** a satellite that rotates with the planet at an altitude the player adjusts with a slider. Low altitude: fast sweep over nearby sky, strong shots, short reach. High altitude: slow sweep, long reach, weaker shots. It fires when you tap a target inside its arc. The satellite sits in the sun beam or in shadow depending on its bearing, so it is also light-charged. Branches such as rate of fire, altitude range, beam width.
  - **Candidates to include (Daniele picks; DESIGN Q26):** **Gravity Tractor**, a placed well that drags swarms into a clump for splash damage; **Rotation Railgun**, a charged shot fired along the planet's "up" vector, aimed only by rotating the planet (the open Slingshot question, Q9, in weapon form); **Orbital Minefield**, drag a path in orbit to lay mines. The Sun Lens is no longer a weapon candidate: it is a turret (see §5.6).
  - **Weapon skill points** come from every second level-up, separate from the pick-1-of-3 cards (DESIGN Q27).
- **Planet ability**, picked before each run (one of 6): Ring Laser, Time Dilation, Magnetic Pulse, Spin Overdrive, Meteor Call, Terraform.

### 5.8 Enemy planets (decided win condition)

Visible at Sector zoom, each with HP, shield, phases, wave cadence, a lane and a **weighted spawn table**: it is *more likely* to send its signature enemy (Forge about 50% armored, Veil about 50% camo, Bastion about 40% Bulwarks), the rest mixed. The Sector view shows each planet's favored enemy icons, so the player can prepare the right towers per lane.

| Planet | Favors | Gimmick |
|---|---|---|
| Hive | swarms, Darts | spawn rate rises as it's damaged |
| Forge | Bricks, Carriers | armored shell phase |
| Veil | Phantoms | lane hidden until scanned |
| Garden | Spores, Medics | regrows its own HP |
| Shipyard | Carriers → Dreadnoughts | rare big launches |
| Comet | Darts, Moths | moves on an orbit, lane changes |
| Bastion | Wardens, Bulwarks | moons must fall first |

Some planets also launch volleys of falling objects at the shield side. Planets are damaged by the Siege Cannon, max-tier Railgun, ability ultimates, and Payload satellites sent along a lane. Destroying one removes its lane and drops a big reward. Win: all destroyed, then optional endless mode. Lose: planet HP 0.

### 5.9 Progression

- **XP** fills a level bar; each level offers a pick of 1 from 3 cards: tower unlocks, path tiers, global passives, satellites, ability upgrades, Rotation-related cards (faster rotation, wider sun beam, longer shield arc). Some cards are **evolutions** needing two prerequisites.
- A **run director** ramps spawn budget over a 20 to 30 minute run, from a trickle to over 100k concurrent enemies, with scripted spikes.
- **Meta progression** (minimal): unlock planets, abilities and starting towers between runs, stored locally.

---

## 6. Performance strategy

SoA arrays reserved at load; no allocation in systems (allocation-counting test build); counting-sort grid; sim on its own thread; no scene-tree node per entity; one MultiMesh per archetype with tight packing, skip-unchanged and cell culling; **cluster LOD** (swarms merge into aggregate entities beyond a distance or density and split near the Orbit band); **projectile LOD** (volleys and analytic hitscan). Budgets enforced by `core/bench` scenarios (`100k-enemies`, `500k-projectiles`, `mixed-late-game`): fail on >10% regression. Later: SIMD, compute-shader cosmetic projectiles.

## 7. Testing

Core unit tests on tiny worlds (doctest); determinism (3,000 ticks twice, equal hashes, both threading modes); content validation; bench budgets; GdUnit4 for binding, commands and UI; `--screenshot-bands` boot flag saving PNGs of each zoom band under `xvfb-run`; bot runs from `tools/sim-runner`.

## 8. Playtest builds: how Daniele tries every demo

| Build | Made when | Link | Use |
|---|---|---|---|
| Latest web build | every push to `main` | GitHub Pages `/latest/` | newest build on any phone or PC browser |
| PR preview | every PR | `/pr-<n>/`, link and QR code posted on the PR | try a change before merging |
| Milestone build | tag `m1`, `m2`, … | `/m1/` etc. plus a GitHub Release | feel gates, side-by-side comparison |
| Design toys | every push touching `toys/` | `/toys/<name>/` | try design toys the day they exist |
| Android debug APK | milestone tags or on demand | GitHub Release asset | real touch feel and performance |

The web build uses Godot's Compatibility renderer (C++ core built for WebAssembly with Emscripten), so it is for feel; **performance is judged on the APK and desktop only**. GitHub Pages can't set the cross-origin isolation headers threaded web builds need, so the web export uses Godot's **no-threads** web template (Godot 4.3+) and the core runs in its single-threaded mode there. Every build shows its version, has the debug overlay one tap away, and a **"Copy run log" button** (seed plus command log) so any bug can be replayed exactly. Native iOS needs a Mac or macOS runner and arrives in M8.

---

## 9. Milestones and agent tasks

Conventions: one branch and one PR per task. **Deps** must be merged first; tasks without mutual deps can run in parallel. Each milestone ends in a **feel gate**: tag `mN`, Daniele plays `/mN/` (and the APK where noted), and a design-critic agent files `docs/design/reviews/MN.md` (top 5 problems and 5 opportunities, each with a proposed test).

### M0: Foundations (goal: an empty project that builds, tests and publishes a link)

| ID | Task | Deps | Acceptance criteria |
|---|---|---|---|
| M0.1 | Scaffold: Godot project, `core/` C++ library, `gdextension/` with godot-cpp, doctest, GdUnit4, GitHub Actions, **environment setup script** (pinned Godot, export templates, SCons, Emscripten) | — | Editor opens a scene that loads the extension; build and all tests green locally and in CI |
| M0.2 | Sim core: World, fixed-step loop, time scale, seeded RNG, command and event queues, state hash | M0.1 | Determinism test passes; check bans unseeded randomness and wall clock in `core/` |
| M0.3 | SoA pool library with swap-remove and typed field schemas | M0.1 | Add/remove/iterate tests; no allocation after reserve (1M-entity timing moves to M3.1) |
| M0.4 | MultiMesh renderer: archetype batches, SDF shapes (circle, triangle, square, diamond, hexagon, ring, star), palette texture | M0.1 | Demo draws 20k animated instances at 60 fps desktop (200k target moves to M3.4); screenshot saved in CI |
| M0.5 | `SimServer` binding: sim thread, double-buffered snapshots, command API, interpolation, single-threaded fallback | M0.2, M0.4 | GdUnit4 sends commands and reads events; both modes give identical hashes |
| M0.6 | **Playtest pipeline (§8):** web export with wasm GDExtension, PWA isolation, Pages deploy for `/latest/`, `/pr-<n>/` (comment with link and QR), `/mN/` on tags, `/toys/<name>/`, Android debug APK on tags, version label, "Copy run log" | M0.1 | `/latest/` opens on an Android phone and an iPhone; a PR gets a working preview link automatically; tag `m0` publishes `/m0/` and an APK |
| M0.7 | Camera: continuous zoom (pinch, wheel), zoom-aware minimum pixel sizes | M0.4 | Zoom 0.01× to 20× is smooth; small objects respect minPx |

### M1: First playable tower defence (goal: a 3-minute TD you can win and lose in the browser)

Content: Gatling, Cannon, Solar Panel, Drifter, Dart, Eclipser. **Two lanes** at fixed bearings (so rotation and the forecast's bearing column mean something), ground layer only, 8 waves, win by surviving the last scripted wave (replaced by planet-killing in M4). Starter numbers in BRIEF §7.2.

| ID | Task | Deps | Acceptance criteria |
|---|---|---|---|
| M1.1 | Planet entity and rotation system (accel, max speed, inertia) from data | M0.2 | Rotation physics tests; values in content |
| M1.2 | Input: left-zone rotate (hold, drag), right-zone tap/long-press, pinch zoom, desktop keys, mapped to commands | M0.5, M1.1 | GdUnit4 test per gesture via synthetic events; works on touch emulation and desktop |
| M1.3 | Slot ring, placement validation, sell (ground layer, 1-slot footprints) | M1.1 | No overlap; tests |
| M1.4 | Build menu and placement ghost with arc and range preview | M1.2, M1.3 | Place a tower at Surface zoom; arc preview rotates with the planet |
| M1.5 | Spatial grid, enemy pool, straight-in movement from one lane, landing subtracts planet HP, GameOver | M0.3 | 200k-point grid rebuild < 3 ms desktop; planet HP 0 emits GameOver |
| M1.6 | Tower targeting (arc, range, priority modes), projectile pool, collisions, layered damage with color-tier popping (Drifter red → blue) | M1.3, M1.5 | Out-of-arc enemies never targeted; pierce and splash tests |
| M1.7 | **Sun beam and Solar Panel:** drifting `SunPath`, lit test per slot, panels earn only when lit, light cone and lit/unlit slot tint, stardust HUD, spending | M1.3 | Lit slot earns at data rate, unlit nothing; rotating moves panels through light; a unit test swaps in a second `SunPath` type |
| M1.8 | **Shadows:** `ShadowCaster` flag, per-caster shade interval on the planet surface, merged shade test per slot, Eclipser enemy (large, slow, dark disc, drifts between sun and planet), dark wedge on the planet and shaded-slot tint | M1.7, M1.5 | Panel in shade earns nothing; shade interval matches a ray test against a brute-force reference; killing the Eclipser clears its shadow; shadows computed for 1k casters in under 0.2 ms |
| M1.9 | Wave script from `content/waves/*.json` with per-wave `lightMult` (dim rounds), **pause between waves**, Start Wave, win/lose screens, restart, and the **forecast strip** (light level, threat icons with counts, lane bearing, light-limiting conditions and ghost shadow arcs; exact for the next wave, fading after; the source becomes an enemy planet with countdown in M4.3) | M1.5, M1.6, M1.8 | Full loop playable; replay deterministic; the strip matches what actually spawns in a test of 20 waves; a dim wave visibly weakens the beam and income |
| M1.10 | Surface and Orbit zoom bands, **slow time at Surface (`surfaceTimeScale` default 0.75)**, cross-fade, double-tap toggle | M0.7, M1.9 | Time eases down and back; time scale logged as a command |
| M1.11 | Debug overlay (fps, ms/tick, counts, band, sun bearing, shade intervals), toggle with backtick or 3-finger tap | M0.5 | Shows live numbers |

**Feel gate M1 (APK too):** "Does this feel like a tower defence? Does chasing the light and dodging shadows feel like a real, central choice?"

### M2: Depth of defence (goal: choices that matter; rotation is aiming)

| ID | Task | Deps | Acceptance criteria |
|---|---|---|---|
| M2.1 | Placement depth: elevated layer with foundations, 1 to 3 slot footprints, adjacency framework | M1.3 | Elevated needs a foundation; adjacency buffs apply within k slots |
| M2.2 | Upgrade path framework (2 paths × 4 tiers) and upgrade UI at Surface zoom | M1.9 | Stats preview diff; costs in stardust |
| M2.3 | Damage types and soft-counter matrix, trait bitmask and visuals, color tiers, segmented HP bars for large units, enemy codex | M1.6 | No multiplier below 0.25; traits render distinctly; codex generated from data |
| M2.4 | **Weak points:** weak-side mechanic (bearing, wedge width, multiplier), Bulwark enemy, wedge marker, **Lance** tower, aim-assist showing how far to turn | M2.3 | Body hits about 25%, weak-side 3 to 4×, none ever 0; a never-rotating bot kills Bulwarks at under 25% the rate of a rotating bot |
| M2.5 | Enemies batch 1: Brick, Phantom (camo), Bubble, Ember, Spore, Splitter | M2.3 | Behavior test per mechanic |
| M2.6 | Towers batch 1: Flak, Railgun, Cryo, Radar, Amplifier, **Sun Lens** (beam power scales with the channel value; Overcharge active), plus falloff curves and gradient range ring | M2.2, M2.1 | Content and tests per tower; each beats its trait ≥2× over a non-listed tower; Sun Lens damage is about 20% when channel is 0 and 100% when all panels are lit, measured in a unit test; an Eclipser shading the panels visibly weakens the beam |
| M2.7 | **Drill, instability, Cooler** | M1.7, M2.1 | Drill output independent of light; instability past threshold makes the drill overheat and shut down for a cooldown, with no damage to any building; Cooler lowers it |
| M2.8 | **Home planets:** 3 planets (small, medium, large) with size-derived rotation speed, HP, slots and lit fraction, plus a pre-run picker | M1.7 | Each planet changes all four stats in play; bot comparison shows none is strictly best |
| M2.10 | **Falling objects and Shield:** falling-object pool (meteor, bomb) with telegraphed bearing and arrival, Shield building with arc, energy, offline/recharge that is faster while lit (`lightChargeMult`) and optional stardust upkeep, Bomber enemy that drops bombs, impact damage to planet HP only, shield arc and energy drawn | M1.9, M1.3 | A meteor crossing a shielded arc is absorbed and drains the shield; an unshielded hit damages planet HP only (no building is ever damaged); offline shield recharges on schedule and about 2× faster while its slot is lit; warning streak shows 3 seconds before impact; a bot facing only the sun loses to a meteor storm, a bot facing only the threat starves, a bot balancing does best |
| M2.11 | **Bent aim and focus fire:** aim mounts and `SetAim` command, focus-point solver (planet-local, recomputed on build/aim change), reticles and axis lines at Surface zoom, focus bonus in `damageResolve`, Sun Lens beam merge | M2.6, M2.9, toy D7 | Unit tests for axis targeting with offsets and for the focus solver; a focus-builder bot beats a no-focus bot on swarm waves by 10 to 40% (not more) |
| M2.9 | Headless `tools/sim-runner` with scripted bots (never-rotate, rotate-to-lane, rotate-to-weak-point, chase-the-light, face-the-threat, balance, bank-and-drill) and a check that each threat is beaten by at least three different strategies | M1.9 | 100 seeded runs in < 10 minutes with a CSV of results |

**Feel gate M2 (APK):** "Does rotation feel like a real three-way decision (light, shields, targets) without being overwhelming? Is any one tower, layout or planet obviously best?"

### M3: Scale (goal: it keeps feeling good when the screen fills)

| ID | Task | Deps | Acceptance criteria |
|---|---|---|---|
| M3.1 | `core/bench` harness, CI budget check, stress scenarios `100k-enemies`, `500k-projectiles`, `mixed-late-game`, in-engine stress scene | M2.9 | Baselines recorded; CI fails on >10% regression |
| M3.2 | Projectile LOD: volleys and analytic hitscan | M3.1 | Same DPS within 2%; ≥5× fewer entities |
| M3.3 | Cluster LOD for swarms (Swarmcloud) | M3.1 | 50k-unit swarm costs ≤10% of individual sim; split is seamless |
| M3.4 | Render pass: tracer lines at far zoom, cell culling, tight packing, skip unchanged batches | M3.1 | 500k+ instances desktop 60 fps; upload time logged; bullets visible zoomed out |
| M3.5 | Parallel systems on `WorkerThreadPool` and allocation audit | M3.1 | Zero heap allocations per tick in the mixed scene; tick time scales with cores |
| M3.6 | Android perf check on a mid-range device or profile | M3.2–M3.4 | Meets the phone target or a report with the top 3 bottlenecks |
| M3.7 | **Readability at scale:** 100k-enemy scene with swarm clouds and screenshots | M3.3 | A reviewer can name the biggest threat within 2 seconds |

**Feel gate M3 (APK, real phone):** "Does a huge wave still feel good, not just big?"

### M4: The wider battlefield (goal: zoom out, plan, and win by destroying planets)

| ID | Task | Deps | Acceptance criteria |
|---|---|---|---|
| M4.1 | Four zoom bands (add System and Sector), band thresholds in data, edge indicators | M1.10 | Four bands detected; indicators point to off-screen threats |
| M4.2 | Sector generation and enemy planet entity (HP, shield, phases, lane); waves now come from planets; **each living planet dims the sun, destroying one restores it** | M4.1, M3.1, M1.8 | Deterministic layouts; waves originate on lanes; beam strength falls with each living planet and recovers when one dies; planets also cast shadow wedges when between sun and planet |
| M4.3 | **Weighted spawn tables, favored-enemy icons and threat timeline** in the Sector view; scouting sharpens forecast precision further out | M4.2, M1.9 | Spawn statistics match weights over 10k samples; shows next 3 waves per planet; scouted waves show counts, unscouted show icons only |
| M4.4 | Siege Cannon, planet damage, win condition, **endless mode after victory** | M4.2 | Destroying all planets wins; endless continues with the same director |
| M4.5 | Planets batch 1: Hive, Forge, Veil, Garden, with Carrier and boss template | M4.2, M2.5 | Gimmicks tested; children spawn counts correct |
| M4.6 | Planets batch 2: Shipyard, Comet (moving), Bastion (moons), Dreadnought | M4.5, M2.4 | Gimmicks tested |
| M4.7 | **Run pacing decision (D5):** bot run-length simulation, enrage rules for surviving planets | M4.4, M2.9 | Target run length and enrage rules recorded in DECISIONS.md |

**Feel gate M4 (APK):** "Does zooming out give me real planning decisions?"

### M5: Active play and progression (goal: always something to do; runs build up)

| ID | Task | Deps | Acceptance criteria |
|---|---|---|---|
| M5.1 | **Active weapon framework:** archetype data, per-weapon skill tree data and UI (3 branches × 4 nodes), skill points every second level, pre-run weapon pick, manual-fire rule | M1.2, M1.6 | Picked weapon persists; nodes unlock and change stats in a unit test; no weapon fires without a player command |
| M5.2 | **Slingshot** weapon and tree (drag from core, power by drag, trajectory preview) | M5.1 | Cooldown; pierce test; good on touch |
| M5.8 | **Target Missiles** weapon and tree (tap to mark, salvo after delay, charges) | M5.1 | Works at Orbit and System bands; marks cap and regen tested |
| M5.9 | **Orbital Gun** weapon and tree: satellite bound to planet rotation, altitude slider changing sweep speed, reach and damage, fire on tap inside its arc, light-charged cooldown | M5.1, M1.7 | Altitude changes the three stats as data says; satellite in shade recharges slower than in the beam |
| M5.10 | Extra weapon(s) chosen at DESIGN Q26 (Gravity Tractor, Rotation Railgun or Minefield) | M5.1 | Content, tree and tests per chosen weapon |
| M5.3 | Tower actives with cooldowns and quick-bar; cooldowns shorten while the slot is lit | M2.6 | Tap tower to trigger; cooldown UI; lit slot recharges `lightChargeMult` times faster |
| M5.4 | Planet ability framework, pre-run pick, abilities Ring Laser, Time Dilation, Magnetic Pulse | M1.9 | Selected ability persists for the run |
| M5.5 | Planet abilities Spin Overdrive, Meteor Call, Terraform | M5.4 | Content and tests |
| M5.6 | XP and level-up pick-1-of-3 card system with weights and evolutions | M2.2 | Deterministic draws from the seed; evolutions unlock |
| M5.7 | Passive cards (25 or more) including rotation speed, sun-beam width and shield-arc boosts | M5.6 | Content validation passes; each card changes its stat in a unit test |

### M6: Space layer (goal: gameplay outside the planet)

| ID | Task | Deps | Acceptance criteria |
|---|---|---|---|
| M6.1 | Satellite pool, orbit modes, drag placement onto orbit radii, optional uplink rule (DESIGN.md Q7) | M4.1 | Modes tested; placement in Orbit and System bands |
| M6.2 | Satellites batch 1: Slow Field, Proximity Mine, Scanner, Relay | M6.1 | Content and tests |
| M6.3 | Satellites batch 2: Decoy, Gravity Well, Laser Sat, Shield Node | M6.1 | Content and tests |
| M6.4 | Enemies that hunt satellites by jamming them; Payload satellite for planet damage | M6.2, M4.4 | Jammed satellites go offline for a data duration and recover; no satellite is ever destroyed |

### M7: The full run (goal: 20 to 30 minutes from start to win, with all content)

| ID | Task | Deps | Acceptance criteria |
|---|---|---|---|
| M7.1 | Run director: spawn budget curve, spikes, density ramp to 100k+ | M4.3, M3.3 | Bot run shows the ramp; late game hits target density |
| M7.2 | Towers batch 2: Tesla, EMP Spire | M2.6 | Content and tests |
| M7.3 | Towers batch 3: Mortar, Missile Silo | M2.6 | Content and tests |
| M7.4 | Enemies batch 2: Mole, Moth, Medic, Warden, remaining movement archetypes | M2.5 | Behavior tests |
| M7.5 | Game flow: title → planet pick → weapon pick → ability pick → run → win/lose → summary → endless | M5.4, M4.4 | Full loop start to finish |
| M7.6 | Save and settings, meta unlocks, pause/resume mid-run | M7.5 | Resume restores an identical state hash |

### M8: Phones, desktop and balance

| ID | Task | Deps | Acceptance criteria |
|---|---|---|---|
| M8.1 | Telemetry per run (DPS by tower, leaks by enemy, card picks) and HTML report from bot runs | M2.9, M7.5 | Report generated from CSV |
| M8.2 | Balance pass using reports; tune data only | M8.1 | Each tower used in ≥10% of winning bot runs; no planet or tower dominant |
| M8.3 | Release-grade Android and iOS presets, arm64 GDExtension builds, safe areas | M7.5 | Signed release APK/AAB in CI; iOS build documented |
| M8.4 | Desktop exports and mouse/keyboard polish | M7.5 | Windows, macOS, Linux builds as CI artifacts |
| M8.5 | Touch UX pass: thumb zones, hit targets ≥44 px, two-thumb landscape play | M8.3 | Checklist and screenshots on 16:9, 19.5:9 and 4:3 |

**Prototype done when:** a full 20 to 30 minute run plays on a phone from planet pick to win/lose, with 15 defence buildings, 3 economy buildings, 8 satellites, about 16 enemies, 7 enemy planets, 6 planet abilities, 3 or more active weapons with skill trees, 25+ cards and all four zoom bands, with late-game density of 100k+ entities at target frame rate.

### Design track (runs alongside; see DESIGN.md)

| ID | Task | When | Output |
|---|---|---|---|
| D1 | Seed `docs/design/DECISIONS.md` from DESIGN.md | with M0 | Every decision and open question has a home |
| D2 | **Rotation toy:** control schemes with the left-thumb zone, weak-point aiming, **panels vs shields orientation tension** | after M0.6, before M1.2 merges | Pick the default scheme; write the gesture budget |
| D3 | **Sun and economy toy:** arc width, drift speed, panel vs drill income, planet sizes | after M0.6, before M1.7 merges (include shadows) | Tuned defaults in `content/` |
| D4 | **Zoom and slow-time toy:** slow-time percentage, band thresholds | after M0.7 | Tuned defaults |
| D5 | Weapon toy: slingshot aiming (Q9), Orbital Gun altitude feel, candidate weapons (Q26) | before M5.1 | Decisions recorded |
| D7 | **Bent aim and focus toy:** angled mounts, focus reticles, sweeping a focus point by rotating | before M2.11 | Pick Q33 form and Q34 numbers |
| D6 | Choose which backlog ideas enter the prototype | at the M2 gate | Chosen ideas added as tasks |

Toys are GDScript scenes under `toys/`, published at `/toys/<name>/`.

---

## 10. Orchestrating with Claude Code

- Put `CLAUDE.md`, `PLAN.md` and `DESIGN.md` at the repo root; keep `docs/STATUS.md` as a checklist of task IDs that each PR ticks.
- **One task per agent session.** Prompt template: *"Implement task M1.6 from PLAN.md. Follow CLAUDE.md and docs/design/DECISIONS.md. Open a PR titled `[M1.6] …` with acceptance criteria checked off, test output and screenshots or bench numbers."*
- Run tasks in the same milestone in parallel only when they touch different folders (content batches). Engine-core tasks (M0, M1.5, M3.x) run one at a time. Do M0.6 straight after M0.1 so every later task and toy ships with a preview link. Avoid parallel edits to the same `.tscn` file.
- At each milestone gate: tag, Daniele plays `/mN/`, the design-critic agent files its review, and only then does the next milestone start.

## 11. Deferred backlog (not in the prototype unless chosen at the M2 gate)

Projectile gravity (shells curving around the planet), spin momentum resource, debris rings, capturing enemy planets, a player moon platform, scouting rewards, run mutators, combo shots between satellites and towers, a second resource (starshards), a tap-on-core stardust boost, compute-shader projectiles, native iOS before M8, monetization, sound, final art.

## 12. Open items

Reference games and feel targets (asked), working title, forecast fidelity (Q30), satellite limit rule (Q7), slingshot aiming (Q9), weapon skill points (Q27), shield upkeep (Q23), extra weapons (Q26), sun beam width and drift speed (toy D3), whether kills pay stardust beyond a trickle (Q17), which backlog ideas to include (D6). Satellites jammed rather than destroyed (Q32) is a reviewer default awaiting Daniele's confirmation.
