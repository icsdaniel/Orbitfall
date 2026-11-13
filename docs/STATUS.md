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

## Design track
- [ ] D1 DECISIONS.md
- [ ] D2 rotation toy
- [ ] D3 sun/shadow toy

M1 integration (branch `m1-integration`): the game runs on the real `SimServer` by default (sim thread on desktop and Android, stepped on the main thread on web); `--mock` still runs the client on `MockSimServer`. First balance pass done (data only, see DECISIONS §3); the strategy bots in `core/tests/test_balance.cpp` are the balance regression suite (`./build/core_tests --no-skip -tc="balance report"` prints the outcome table). Playtested headless: screenshots in `docs/screenshots/m1-integration/` (taken with the playtest bot, `--play=sensible --ff-wave=N --screenshot PATH`), web export smoke test passes in headless Chromium. Open for the M1 feel gate: Daniele plays `/m1/` and the APK (BRIEF §8.4).
