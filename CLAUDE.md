# CLAUDE.md — Orbitfall (planet tower defence prototype)

**North star: it must feel like a tower defence first.** The core foundation is **chasing the light**: a thin drifting sun beam, solar panels, and shadows cast on the planet by big enemies (PLAN §2, §5.4); every system should interact with it. Read `BRIEF.md` (self-contained spec for M0 and M1), `PLAN.md` (§1 north star, §9 tasks) and `docs/design/DECISIONS.md` before any task; DECISIONS.md overrides BRIEF.md, which overrides PLAN.md. Design questions and toys are in `DESIGN.md`; toys live in `toys/`, are throwaway and exempt from tests and perf rules. Don't build ahead of the current milestone: anything not needed by the task or its milestone gate is deferred. Each task has an ID (e.g. `M2.4`); implement exactly that task and its acceptance criteria.

## Stack
Godot 4 (version pinned in `project.godot` and CI). Simulation core in C++ (`core/`, no Godot includes) exposed through a GDExtension binding (`gdextension/`, godot-cpp). GDScript for UI, input and game flow. Rendering through `RenderingServer` MultiMesh batches with SDF `canvas_item` shaders. Content in JSON under `content/`. Tests: doctest (core), GdUnit4 (Godot, headless).

## Commands
- `scons -C gdextension` — build the extension (debug); `scons -C gdextension target=template_release` for release
- `cmake -S core -B build && cmake --build build && ./build/core_tests` — core unit, determinism and content tests (no Godot needed)
- `./build/core_bench` — perf scenarios vs `core/bench/baseline.json`
- `godot --headless --path . -s res://addons/gdUnit4/bin/GdUnitCmdTool.gd -a res://game/tests --ignoreHeadlessMode -c` — Godot tests
- `xvfb-run godot --path . -- --screenshot-bands out/` — screenshots of the four zoom bands
- `godot --headless --export-debug "Android" build/game.apk` (and "Linux", "Windows") — exports
- `scons -C gdextension platform=web` then `godot --headless --export-release "Web" build/web/index.html` — web build (CI publishes it to GitHub Pages)

## Hard rules
1. **Data-oriented sim in C++.** Enemies, projectiles, effects and satellites live in SoA pools in `core/`. No class or object per entity, no heap allocation in per-entity loops.
2. **No scene-tree node per entity.** Many-instance things are drawn only through MultiMesh batches. Nodes are fine for the planet, towers, UI.
3. **No Godot physics** for entities; use the core spatial grid.
4. **Determinism.** No unseeded randomness or wall clock in `core/`. Use the world's seeded RNG. All player actions go through commands to `SimServer`.
5. **`core/` has no Godot includes.** It must build and test standalone.
6. **Content is data, and the sun is pluggable.** Sun movement goes through the `SunPath` interface (prototype: constant drift). New towers/enemies/satellites/planets/upgrades/abilities go in `content/**` as JSON with a test in `core/tests/content/`. Don't hard-code content ids in engine code.
7. **Readability rules** (PLAN §5.5): shape = movement class, color tier = health, outline/badge = trait, HP bars only on carrier-size and up.
8. **Placeholder visuals only.** SDF shapes and palette colors. No image assets, no audio.
9. **Perf budgets are law.** If `core_bench` regresses > 10%, fix it or explain in the PR why the baseline should change.
10. **Scenes are shared text.** Keep `.tscn` files small; build repeated UI in code. Don't edit a scene another open task is editing.

## Definition of Done (every task)
- AC from PLAN.md met and checked off in the PR description.
- Extension builds; core tests and Godot headless tests pass; bench within budget if sim/render touched.
- New systems have unit tests on tiny worlds; new mechanics have a counter/behavior test.
- Visual changes include band screenshots (Surface, Orbit, System, Sector where relevant).
- The PR's preview link (`/pr-<n>/`, posted by CI) loads and shows the change; if it doesn't, the task isn't done.
- `docs/STATUS.md` updated with the task ticked.
- One task per branch/PR, branch name `m2.4-projectiles` style, PR title `[M2.4] …`.

## Conventions
- Systems are `void name(World&, float dt)`, registered in order in `core/sim/pipeline.cpp`.
- Pools expose arrays by field name; iterate `for (uint32_t i = 0; i < pool.count; ++i)`.
- Angles in radians; towers stored in planet-local polar coords; world angle = local + planet rotation.
- Tunable numbers live in `content/` JSON, not in systems.
- GDScript is statically typed (`var x: float`), with warnings as errors for untyped declarations.
- Keep PRs small. If a task grows beyond ~800 changed lines, split it and note the split in STATUS.md.
