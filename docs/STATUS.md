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
- [ ] M1.3 Slots, placement, sell (sim and slot view done; wiring to the real sim remains)
- [x] M1.4 Build menu and ghost
- [x] M1.5 Enemies, lanes, landing
- [x] M1.6 Targeting, projectiles, splash
- [ ] M1.7 Sun, beam, income (sim and client done; wiring remains)
- [ ] M1.8 Shadows and Eclipser (sim and client done; wiring remains)
- [ ] M1.9 Waves, flow, forecast (sim and client done; wiring remains)
- [x] M1.10 Bands and slow time (client; sim honours set_time_scale)
- [x] M1.11 Debug overlay and tuning (client; tunables applied by SimServer)

## Design track
- [ ] D1 DECISIONS.md
- [ ] D2 rotation toy
- [ ] D3 sun/shadow toy

M1 client note: the client parts of M1.3/M1.7/M1.8/M1.9 (slot ring, beam cone, slot tint, shadow wedges, HUD, forecast strip, end screens) are in `game/view/` and `game/ui/`; those tasks get ticked when their sim halves land. `--mock` runs the client on `MockSimServer`.
