# Orbitfall — Decision log

> **Read this before every task.** Precedence: this file > `BRIEF.md` > `PLAN.md`.
> Seeded by task D1 from `DESIGN.md` §1–2 and `BRIEF.md` §3 and §12 (state as of 2026-10-27). It adds nothing new: where it and those files agree today, that is intended.
> To change something: add or edit a row with the options considered, the evidence (toy or bot result), the decision, who made it and the date. Rules in §1 are Daniele's and are never reversed by an agent (BRIEF §11.13).

## 1. Decided rules (do not reopen)

| # | Rule | Meaning for design and code | By, date |
|---|---|---|---|
| North star | **Tower defence first** | Place, shoot, earn, survive must be fun alone; anything not serving the loop yet is deferred | Daniele |
| Q21 | **Chase the light is the core foundation** | Drifting sun beam, panels, enemy shadows from M1; enemy planets dim the sun (M4) and killing them restores it | Daniele, 2026-10-25 |
| Q1 | **Rotation aims at weak points** | Bulwark: body 0.25×, weak point 3–4×. A hit is a weak-point hit when the firing tower is within ±8° of the enemy's bearing. The Lance exploits it; lanes are a secondary driver | Daniele, 2026-10-25; geometry fixed in review 2026-10-26 |
| Q2 | **No Autopilot, ever** | The player always rotates by hand. No auto-rotate, no auto-fire on player weapons. Control scheme tuned in toy D2 | Daniele, 2026-10-25 |
| Q8, Q22 | **Buildings are never damaged** | Landings and falling objects subtract planet HP only. No building HP, rubble, repair, surface walkers or slot attacks | Daniele, 2026-10-25 |
| Q10 | **No hard counters** | Nothing immune or untargetable. Every damage multiplier ≥ 0.25, validated by the content loader. Enemy planets favour a signature enemy, shown in Sector view | Daniele, 2026-10-25 |
| Q14 | **Stardust only** | One currency. Sources: lit panels, drills (M2), a tiny kill bounty | Daniele, 2026-10-25 |
| Q18 | **Sunlight is the economy** | Panels earn only inside the beam and unshaded; drills are the second source. No falling motes, collectors or tap-to-collect | Daniele, 2026-10-25 |
| Q18 | **Sun is pluggable** | Sun movement goes through `SunPath`; prototype = constant slow drift | Daniele, 2026-10-25 |
| Q29 | **The forecast never lies** | Forecast and spawner read the same wave data. The player always knows the light level and the threat types and bearings | Daniele, 2026-10-26 |
| Q31 | **Forecast scope** | Also shows which enemy planet spawns next and where (M4), what is limiting the light (dimming, eclipses, expected Eclipser shadow arcs) and live shadow-casters | Daniele, 2026-10-26 |
| Q29 | **≥ 3 answers per threat** | From different families (tower type; layout/rotation; weapon/ability; satellite/economy). Validated from M2 | Daniele, 2026-10-26 |
| Q25 | **Light charges actives** | Buildings with a cooldown recharge `lightChargeMult` (2×) faster while lit and unshaded; shields first | Daniele, 2026-10-25 |
| Q22b | **Shields are a core rotation mechanic** | Objects fall from space; a shield protects only the arc it faces. Panels face the light, shields face the threat (M2) | Daniele, 2026-10-26 |
| Q13 | **Drills are limited by instability** | Past a threshold a drill overheats and shuts down for a cooldown; relieved by a Cooler or pausing. Nothing is damaged | Daniele, 2026-10-25 |
| Q4 | **Time** | Sim frozen between waves; during a wave time runs at `surfaceTimeScale` (0.75) while in the Surface band, eased over 0.3 s | Daniele, 2026-10-25 |
| Q6 | **Win / lose** | Win: destroy all enemy planets, then optional endless (M1: survive the last wave). Lose: planet HP 0 | Daniele, 2026-10-25 |
| Q5 | **The feeling of millions is enough** | Swarm clouds and simulation LOD; a player must name the biggest threat within 2 seconds | Daniele, 2026-10-25 |
| G1/G2 | **Day/night side; planets as characters** | The sun beam; home planets whose size sets rotation speed, HP, slots and lit fraction (M2) | Daniele, 2026-10-25 |
| Q33 | **Explore bent aim and focus fire** | Some turrets fire at a fixed angle off their slot normal; several towers can combine into focused fire (M2). The form of the bend is open, see §2 | Daniele, 2026-10-27 |
| Q26 | **Sun Lens is a turret, not a weapon** | Continuous beam, damage × (0.2 + 0.8 × channel). Player weapons: Slingshot, Target Missiles, Orbital Gun | Daniele, 2026-10-26 |
| — | **Device and orientation** | Landscape, two thumbs; mid-range Android 2021–2023; PC later | Daniele, 2026-10-25 |

**Dropped:** falling motes, collector buildings and tap-to-collect; surface-walking landed enemies; hard immunities (camo untargetable, burrower invulnerable, Bulwark immune); a separate Laser tower (replaced by the Sun Lens).

## 2. Open questions (the default is in force until decided)

None of these block M1. An agent that hits one uses the default, notes it in the PR and flags it for Daniele.

| # | Question | Current default | Decided by / test | Status |
|---|---|---|---|---|
| Q3 | Gesture budget on landscape two-thumb | Left zone drag rotates (optional ⟲ ⟳ buttons); right zone tap selects/places, long-press inspects; pinch zooms; double-tap toggles Surface/Orbit | Toy D2: 5-minute sessions per scheme, taps/min and fatigue | Open, toy built (`toys/d2_rotation`) |
| Q12 | Is solar panel placement a real decision? | ~90° lit arc (beam width 141 on radius 100), drift 1.5°/s, panel 3/s, panels moderate rather than the whole economy | Toy D3; bots "keep panels lit" vs "ignore the sun" within 25% | Open, toy built (`toys/d3_sun`) |
| Q30 | Forecast range and precision | Next wave exact, +2/+3 as icons, beyond that light level only (`exactAhead 1`, `iconsAhead 3`) | Bots exact vs fuzzy; humans at the M1 gate | Open |
| Q17 | Do kills pay? | Tiny bounty (1 per kill, Eclipser 15); XP from M5 | Confirm after the M1 feel gate | Open |
| Q20 | Reference games and feel targets | Bloons (enemies), Vampire Survivors (escalation) | Daniele | Asked, awaiting answer |
| Q33 | Where bent aim comes from | Player sets a mount angle at placement on `aimMount` towers: −60, −30, 0, +30, +60°; re-aim costs 10%, between waves only; the Lance's weak-point rule ignores offsets | Toy D7 and bots: mounts help by under 25% | Asked 2026-10-27 |
| Q34 | Focus fire strength | +25% per extra feeding tower inside a zone of radius 30, cap +50%; two Sun Lens beams merge | Focus bot 10–40% better, not more; toy D7 | Open |
| Q32 | Can satellites be destroyed? | No: hunters jam them (offline for a while) | Daniele to confirm | Reviewer default |
| Q7 | What limits satellites? | A ground uplink building within range | Bots with and without the rule | Open |
| Q9 | Slingshot aim | Along the planet's up vector, so rotation aims it; alternative free aim | Toy D5, Daniele picks | Open |
| Q23 | Shield cost | 60° arc, energy pool, offline and recharge when empty, small stardust upkeep while online | Bots with and without upkeep | Open |
| Q24 | Does three-way rotation overload the player? | Introduce in order (sun M1, shields M2); the best angle is always visible | Balance bot vs single-goal bots; M2 feel gate | Open |
| Q26 | Extra weapons beyond the three | Gravity Tractor, Rotation Railgun, Orbital Minefield stay in the backlog | Toy D5 | Open |
| Q27 | Weapon skill points | A tree per weapon, one point every second level-up | Bot runs per weapon within 25% of each other | Open |
| Q28 | Does Orbital Gun altitude matter? | Adjustable altitude kept | Adaptive bot must clearly beat fixed altitudes | Open |
| Q15 | Does the economy snowball or stall? | Diminishing returns per building type and a soft income cap, only if bots show snowballing | Income vs spend plots | Open |
| Q19 | Planet balance | Three home planets; none may be strictly best | Bot comparison (M2.8): small earns most, large forgives most, medium keeps most HP; see §3 | Open, bots pass; humans at the M2 gate |
| — | Title | "Orbitfall" (placeholder) | Daniele | Open |

## 3. Defaults picked by agents (flagged for Daniele)

Small choices an agent had to make where the brief was silent. Each is data, not code, unless noted.

| Date | Task | Choice | Where |
|---|---|---|---|
| 2026-11-13 | Content loader | `spawnSeconds` per wave: 15, 16, 17, 18, 20, 22, 24, 25 (brief: "15 to 25 s"; wave 5 = 20 given) | `content/waves/m1.json` |
| 2026-11-13 | Content loader | Lane enemies spawn at radius 900 with no bearing jitter; the Eclipser keeps its own 900 and ±6° (brief gives numbers only for the Eclipser) | `content/rules.json`, `content/enemies/eclipser.json` |
| 2026-11-13 | Content loader | `killBounty` in rules is a multiplier (1) on each enemy's own `bounty` | `content/rules.json` |
| 2026-11-13 | Content loader | The 0.25 damage floor is tunable upward in `rules.json` (`minDamageMult`) but can never go below 0.25 (fixed in the loader) | `core/content/content.h` |
| 2026-11-13 | Content loader | Dart carries the `fast` trait (its "triangle, trail" cue); it has no gameplay effect in M1 | `content/enemies/dart.json` |
| 2026-11-13 | M1 balance | First balance pass, data only; replaces the BRIEF §8.2 starter numbers where listed. Before it, three Gatlings bought from the starting 150 won every wave with no panels. Costs: Gatling 50 → 60, Cannon 90 → 110, Panel 40 → 50 (`panelRate` stays 3). Gatling 6 → 5 shots/s; Cannon splash 40 → 28 | `content/buildings/*.json` |
| 2026-11-13 | M1 balance | Waves grow much larger from wave 4 (wave 8: 240 green Drifters on lane B; 160 Darts + 65 greens + 2 Eclipsers on lane A) so that tower count, and therefore income, decides the late waves; waves 1–3 are sized so that never building loses in wave 3. `spawnSeconds` 14–24 | `content/waves/m1.json` |
| 2026-11-13 | M1 balance | `killBounty` 1 → 0.4 (bounties stay tiny next to panel income); lane spawn radius 900 → 600 and lane jitter 0 → ±8° (shorter approach, a stream instead of a single file) | `content/rules.json` |
| 2026-11-13 | M1 balance | Eclipser: HP 40 → 80, speed 25 → 30, spawn radius 900 → 600, hover 15 → 12 s, descend speed 10 → 14 (a full run is 3–4 min of wave time) | `content/enemies/eclipser.json` |
| 2026-11-13 | M1 balance | The four strategy bots in `core/bots/bots.h` are the balance regression suite (never-build, towers-only, sun-chaser, sensible); outcome table: `./build/core_tests --no-skip -tc="balance report"` | `core/tests/test_balance.cpp` |
| 2026-11-13 | M1 sim | Rotation between waves uses the real tick dt (the sim clock is frozen in an intermission, the planet still turns at full speed, whatever the time scale) | `core/systems/planet.cpp` |
| 2026-11-13 | M1 sim | Unit k of n of a spawn entry appears at `spawnSeconds · k / n` into the wave; the forecast uses the same formula | `core/systems/waves.h` |
| 2026-11-13 | M1 sim | Splash hits every enemy disc it touches at full damage (no falloff inside the splash radius) | `core/systems/combat.cpp` |
| 2026-11-13 | M1 sim | `SetAim` is free in M1 (M2.11 prices re-aiming) | `core/systems/buildings.cpp` |
| 2026-11-18 | M2.8 | Home planets (medium = M1 planet). Small: radius 80, 30 slots, HP 75, 135°/s (accel 540), lit 34%. Large: radius 125, 48 slots, HP 200, 60°/s (accel 240), lit 19%. Same lanes, start stardust, damping, refund and slow time on all three | `content/planets/*.json` |
| 2026-11-18 | M2.8 | Bot comparison over seeds 1–6: sensible wins 6/6 on every planet; small has the most panel income (2181 vs 1807 medium, 1789 large), medium keeps the most HP (95% vs 72% small, 48% large), large lets towers-only and the sun-chaser survive a wave longer (6 and 4 waves vs 5 and 3). No planet is ≥ on every measure | `core/tests/test_balance.cpp` |
| 2026-11-18 | M2.8 | Bots choose lane tower slots by angle (within 35° of the lane, at most 6, nearest first) instead of fixed slot indices; on medium these are the M1 lists, so M1 results are unchanged | `core/tests/balance_bots.h` |
| 2026-11-18 | M2.8 | A planet can host a wave set only if it has the wave set's lane ids in the same order (spawns hold lane indices); otherwise `new_run` reports an error and plays the wave set's planet. The run log stores the planet in the high 16 bits of its wave-set word (0 = default), so old logs replay | `core/content/content_loader.cpp`, `gdextension/src/sim_server.cpp` |
| 2026-11-18 | M2.8 | Build view fit: the slot rings, ground and elevated (radius + 30), takes the same share of the screen height on every planet; on radius 100 the planet is `surfacePlanetFrac` (0.24) as in M1.13 | `game/app/band_controller.gd`, `game/app/client_config.json` |
| 2026-11-13 | M1 sim | Shadows lag one tick: `sunlight` runs before `enemyMovement`, so shade is computed from the casters' positions of the previous tick | `core/sim/pipeline.cpp` |
| 2026-11-13 | M1 sim | `StardustChanged` from panel income is emitted once per whole unit earned, not every tick | `core/systems/sun.cpp` |
| 2026-11-13 | M1 sim | The disc is shape id 6 (after circle, triangle, square, hexagon, star, diamond); the client's `BatchRenderer.Shape` uses the same ids | `core/content/content.h`, `game/render/batch_renderer.gd` |
| 2026-11-13 | M1 sim | Live tuning mutates the `ContentDB` between ticks (the World reads it every tick); tuned runs do not replay from their log | `gdextension/src/sim_server.cpp` |
| 2026-11-13 | M1 integration | One render convention: instance basis = entity radius, batch shape of radius 1 (shape size 2), the shape's "up" along the velocity; the Dart's triangle is drawn narrowed so its heading reads | `core/sim/render_snapshot.h`, `game/view/entity_view.gd`, `game/render/shaders/sdf_shapes.gdshader` |
| 2026-11-13 | M1 integration | `get_forecast` during a wave lists the wave in flight first (exact, no ghost arcs), then the upcoming ones from core `forecast()` | `gdextension/src/sim_server.cpp` |
| 2026-11-13 | M1 integration | Slow-time numbers (`surfaceTimeScale`, `surfaceTimeEaseSeconds`) are read from content through `SimServer.get_surface_time_scale()` / `get_surface_time_ease_seconds()`; removed from `client_config.json` | `game/app/band_controller.gd` |
| 2026-11-13 | M1 integration | `load_content` ends the run in progress (a World points into its `ContentDB`); call `new_run` after it | `gdextension/src/sim_server.cpp` |
| 2026-11-15 | M2.3 | The soft-counter matrix is one file, `content/damage.json`: a row of damage-type multipliers per trait plus the legend texts the codex shows, and `shieldMult` (damage dealt to a shield). An enemy's final multiplier = its own `damageMult` x the row of every trait it has, floored at `minDamageMult`; the loader rejects any single entry below the floor | `content/damage.json`, `core/systems/combat.cpp` |
| 2026-11-15 | M2.3 | Starter numbers: Armored Kinetic x0.5, Pierce x1.5; Heat-resistant Energy x0.5, Cryo x1.5; EMP x2 against shields (PLAN gives only the two halvings) | `content/damage.json` |
| 2026-11-15 | M2.3 | Shielded: `shield` on the enemy is the damage absorbed first; overflow goes on to the hit points; the shield never regenerates, carries over a popped layer, and the glow ring disappears when it breaks. The 0.25 floor applies to the multipliers, not to the absorb (PLAN 5.5 lists it as a soft effect) | `core/systems/combat.cpp` |
| 2026-11-15 | M2.3 | `layered` is a trait bit (bit 12) derived by the loader from `layers > 1`, not authored; its cue is the colour tier only | `core/content/content_loader.cpp` |
| 2026-11-15 | M2.3 | HP bar segments are data (`hpBarSegments`, Eclipser 4); the codex opens with the `?` button (touch/mouse) or C, Escape closes it; it does not pause the sim | `content/enemies/eclipser.json`, `game/ui/codex_panel.gd` |
| 2026-11-21 | M2.10 | Falling objects fall straight down a fixed world bearing (planet rotation moves shields under them, not the other way); warned when the ETA is within `rules.fallingWarnSeconds` (3 s). A shield absorbs a whole object (energy -= damage, no overflow); the check runs every tick inside the dome (`radius + height`), so turning a shield under an object late still catches it; with overlapping shields the one with most energy takes it | `core/systems/falling.cpp`, `content/rules.json` |
| 2026-11-21 | M2.10 | Shield numbers: cost 50, 60° wide, dome 40 above the surface, 24 energy, offline at 0 then back to full after `rechargeSeconds` 12 (charge accrues `lightChargeMult` = 2x while the site is lit and unshaded), `regenPerSec` 1 while online (also light-multiplied), `upkeepPerSec` 0 (Q23; paid as far as stardust allows) | `content/buildings/shield.json` |
| 2026-11-21 | M2.10 | Meteor: damage 2, speed 70, from radius 650. Bomb: damage 5, speed 45. Bomber: hp 12, orbits at 260 (12°/s), drops a bomb every 2.5 s (first after half that), 4 bombs, then dives | `content/falling/*.json`, `content/enemies/bomber.json` |
| 2026-11-21 | M2.10 | Impacts emit `FallingImpact` (not `PlanetHit`, which stays for landed enemies); a third system `fallingImpact` removes landed/absorbed objects after `shields`. A wave ends only when its volleys are spent and no falling object is left | `core/sim/pipeline.cpp`, `core/systems/waves.cpp` |
| 2026-11-21 | M2.10 | Wave sets may override the planet's start stardust (`startStardust`); the demo set `m2_storm` (400, five waves of meteor volleys plus bombers, not in M1 waves) is played with `--waves=m2_storm` (web: `?waves=m2_storm`), which skips the planet picker | `content/waves/m2_storm.json`, `game/app/game.gd` |
| 2026-11-21 | M2.10 | Three-bot doctest on `m2_storm`, seeds 1-3: sun-only loses in W3, threat-only wins with ~38 HP on ~7 panel income, balance wins with ~76 HP on ~660 income | `core/tests/test_shield_bots.cpp` |

## 4. Entry template

```
### Qnn. Question
- Options: a) … b) …
- Evidence: toy/bot result, link
- Decision: … (who, date)
- Changes: files or content values touched
```
