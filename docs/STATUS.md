# Status

Task checklist from BRIEF.md. Tick a task when its PR merges.

## M0 Foundations
- [x] M0.1 Scaffold (Godot 4.5.1, core + doctest, gdextension + godot-cpp, GdUnit4, CI, setup script)
- [ ] M0.6 Playtest pipeline (Pages /latest/, PR previews)
- [x] M0.2 Sim core (World, fixed step, RNG, commands/events, hash)
- [x] M0.3 SoA pools
- [x] M0.4 MultiMesh renderer
- [x] M0.5 SimServer (sim thread, snapshots, command API; M1 getters stubbed with TODO(M1.x) in gdextension/src/snapshot.h)
- [x] M0.7 Camera

## M1 First playable
- [x] M1.1 Planet rotation
- [x] M1.2 Input → commands
- [x] M1.3 Slots, placement, sell
- [x] M1.4 Build menu and ghost
- [x] M1.5 Enemies, lanes, landing
- [x] M1.6 Targeting, projectiles, splash
- [x] M1.7 Sun, beam, income
- [x] M1.8 Shadows and Eclipser
- [x] M1.9 Waves, flow, forecast
- [x] M1.10 Bands and slow time (client; sim honours set_time_scale)
- [x] M1.11 Debug overlay and tuning (client; tunables applied by SimServer)
- [x] M1.12 Feel-gate fixes (Daniele, 2026-11-14): grab-to-spin rotation, bigger rotate buttons, two views Build/Watch with a timed glide, toggle button, auto-switch on wave start/end
- [x] M1.13 Build view shows the whole planet and slot ring clear of the HUD (surfacePlanetFrac 0.32 -> 0.24)

## M2 Depth of defence
- [x] M2.1 Placement depth: elevated ring (slot i elevated needs a building on ground slot i; elevated towers +15% range, `rules.elevatedRangeBonus`), 1-3 slot footprints, adjacency auras (stat, amount, radius k, kinds) recomputed on layout change. Build view draws the elevated ring, footprints and aura links; tap a ground building -> "Build above" (keys W/Up, S/Down back to the foundation); selling a foundation in use toasts "Sell the building above first". `place_building(slot, building, aim, layer = 0)` plus the M2.1 site getters. Screenshots in `docs/screenshots/m2.1/`
- [x] M2.2 Upgrade path framework: 2 paths x 4 tiers per building in content (`paths`, add/mul stat modifiers, exclusive past `rules.upgradeExclusiveTier` = 2), `upgrade_building(slot, path)` command, sell refund includes upgrades, real trees for Gatling, Cannon and Solar Panel; upgrade rows with before -> after diff in the Build view menu (tap/click or keys 1/2). `upgrader` strategy bot in `core/bots` (balance suite and sim-runner); upgrades work on elevated sites and multiply with M2.1 bonuses. Screenshots in `docs/screenshots/m2.2/`
- [x] M2.3 Damage types and soft-counter matrix (`content/damage.json`), trait bitmask and shader cues, colour tiers, segmented HP bars with shield strip, enemy codex (`?` button or C). Screenshots in `docs/screenshots/m2.3/`
- [x] M2.9 Headless `tools/sim-runner` (seeded bot runs to CSV, see its README) with the shared strategy bots in `core/bots/` (M1 four plus never-rotate, rotate-to-lane, rotate-to-weak-point, chase-the-light, face-the-threat, balance, bank-and-drill) and the opt-in threat coverage report (`./build/core_tests --no-skip -tc="threat coverage report"`). 1100 runs (11 bots x 100 seeds) in about 82 s; all 8 M1 waves have at least 3 answers
- [x] M2.8 Home planets: `content/planets/{small,medium,large}.json` (medium unchanged); size drives rotation speed, HP, slots and lit fraction. `new_run(seed, wave_set, planet_id := "")`, `get_planet_defs()`, `get_planet_id()`; the run log carries the planet. Pre-run picker (tap / click / 1-2-3 + Enter) on start and Restart, skipped by `--planet=`; the Build view fits each planet's slot ring. Bot comparison (`./build/core_tests --no-skip -tc="planet comparison report"`): none strictly best. Screenshots in `docs/screenshots/m2.8/`
- [x] M2.10 Falling objects and Shield: falling-object pool (`content/falling/meteor.json`, `bomb.json`) spawned by wave volleys and by the Bomber (`content/enemies/bomber.json`); systems `fallingObjects`, `shields`, `fallingImpact` after collisions; impacts hit planet HP only. Shield building (`content/buildings/shield.json`): 60° dome, energy, offline at 0, recharge 2x faster lit, upkeep 0. Events FallingWarning/FallingImpact/ShieldAbsorbed/ShieldOffline/ShieldOnline; `get_falling_buffer/count/warnings`, `get_shields`, `get_falling_defs`, `get_wave_sets`; forecast volley entries. Client: shield dome + energy gauge (dashed with recharge progress when offline, sun dot when lit), warning streaks with ETA rings (red hits, cyan covered) 3 s before impact, off-screen edge diamonds, impact/absorb flashes, volley landing zones and strip icons. Demo set `--waves=m2_storm`; three-bot doctest (sun-only loses, threat-only starves, balance best). Screenshots in `docs/screenshots/m2.10/`. Not split although ~2.6k lines changed: about 1.1k are tests, and the pool, shields, binding and client only work as a whole
- [x] M2.7 Drill, instability, Cooler: Drill (2/s whatever the light, `content/buildings/drill.json`), global instability from a data curve over the running drills (`rules.json` `instability`), past the threshold drills heat and overheat for 10 s (DrillOverheated / DrillRestarted; nothing is damaged), Cooler lowers it (global term, half strength in the beam). Upgrade paths for both (new stats `instability`, `cooling`). `get_instability()`, `get_drill_states()`, `get_drill_income_per_second()`. Client: drills redden with heat (ring), overheated ones dim with a cooldown ring and `!`, coolers wear a halo, HUD instability meter under the HP bar. bank-and-drill builds night-side drills and a Cooler before dim waves (20 seeds: 20/20 won, HP 98 mean vs 96 before). Screenshots in `docs/screenshots/m2.7/`
- [x] M2.6 Towers batch 1 as content (`content/buildings/{flak,railgun,cryo,radar,amplifier,sun_lens}.json`, 2 x 4 paths each): Flak (Explosive, 70° arc, falloff 1.6 close -> 0.5 far), Railgun (Pierce, 650 range, flat falloff, shots pierce 2), Cryo (Cryo type, 40% slow for 1.5 s), Radar (support: `detect` aura reveals camo, +8% range), Amplifier (support: +15% damage, x2 damage to shields), Sun Lens (continuous beam, ramps 30% -> 100% over 1.5 s on a target, x (0.2 + 0.8 x channel), Overcharge active x2.5 for 4 s, 24 s light-charged cooldown). Engine: `channel` in `sunlight`, systems `beamFire` and `abilities`, `ActivateBuilding` command, camo misses (deterministic, `missChance` 0.5 in damage.json), pierce, slow, new aura and upgrade stats. Doctests: each tower beats its trait >= 2x over a non-listed tower; Sun Lens 20% at channel 0 and 100% with all panels lit; an Eclipser shading the panels drops it to 20%. Client: beam drawing, gradient range ring from the falloff curve, scrolling build menu (touch drag, wheel, Left/Right or [ ] + Enter), Overcharge button (tap, or F). Bots unchanged (they build Gatling/Cannon/Panel). Screenshots in `docs/screenshots/m2.6/`

## Design track
- [ ] D1 DECISIONS.md
- [ ] D2 rotation toy
- [ ] D3 sun/shadow toy
- [ ] D7 bent aim and focus toy (built in `toys/d7_bent_aim`; waiting for Daniele to pick the Q33 form and Q34 numbers)

M1 integration (branch `m1-integration`): the game runs on the real `SimServer` by default (sim thread on desktop and Android, stepped on the main thread on web); `--mock` still runs the client on `MockSimServer`. First balance pass done (data only, see DECISIONS §3); the strategy bots in `core/tests/test_balance.cpp` are the balance regression suite (`./build/core_tests --no-skip -tc="balance report"` prints the outcome table). Playtested headless: screenshots in `docs/screenshots/m1-integration/` (taken with the playtest bot, `--play=sensible --ff-wave=N --screenshot PATH`), web export smoke test passes in headless Chromium. Open for the M1 feel gate: Daniele plays `/m1/` and the APK (BRIEF §8.4).
