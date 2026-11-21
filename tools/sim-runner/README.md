# sim-runner (M2.9)

Headless balance runner: plays N seeded runs per strategy bot on one wave set and writes one CSV row per run. Plain C++ on top of `core/` (no Godot). The bots live in `core/bots/` and are the same code the doctest balance suite (`core/tests/test_balance.cpp`, `core/tests/test_threats.cpp`) uses.

## Build and run

```sh
cmake -S core -B build && cmake --build build -j4
./build/sim-runner                      # 100 seeds x every bot on the first wave set -> sim-runner.csv
./build/sim-runner --runs 10 --bots sensible,balance --out out.csv
./build/sim-runner --list-bots
./build/sim-runner --help
```

The CMake default build type is RelWithDebInfo; a Debug build is several times slower.

Options:

| Option | Meaning |
|---|---|
| `--content DIR` | content folder (default: the repo's `content/`) |
| `--waves ID` | wave set id (default: the first one); the wave set names its planet and sun |
| `--runs N` | seeds per bot (default 100); seeds are `S .. S+N-1` |
| `--seed S` | first seed (default 1) |
| `--bots a,b` | bot ids or `all` (default) |
| `--max-waves N` | stop every run before 0-based wave N |
| `--out FILE` | CSV path (default `sim-runner.csv`), `-` for stdout (summary tables are then skipped) |
| `--beaten-hp F`, `--beaten-seeds F`, `--answers N` | threat coverage thresholds (defaults 0.25, 0.5, 3) |
| `--strict` | exit with status 2 when a wave has fewer answers than `--answers` |
| `--trace` | print each run's state at every wave start (use with `--runs 1`) |
| `--quiet` | no summary tables on stdout |

The run is deterministic: the same content, seeds and bots give the same CSV byte for byte (the `hash` column is the final world hash). Wall-clock timing goes to stderr only.

## Bots

| Id | Family | Plays |
|---|---|---|
| `never-build` | control | builds nothing; never counted as an answer |
| `towers-only` | tower | towers under both lanes, no panels, never rotates (M1) |
| `sun-chaser` | light | panels plus towers beside them, keeps the panels in the beam (M1) |
| `sensible` | mixed | some panels, lane towers, small turns for light (M1) |
| `never-rotate` | layout | the sensible build, never rotates |
| `rotate-to-lane` | rotation | one tower battery, turned to the lane carrying the most threat |
| `rotate-to-weak-point` | rotation | same battery, turned to the nearest enemy (weak points arrive with M2.4) |
| `chase-the-light` | light | a fixed panel field kept under the sun; guns wherever the lanes are |
| `face-the-threat` | rotation | same battery, turned to cover the most incoming landing damage |
| `balance` | mixed | lane towers and panels; weighs lit panels against covered threat every moment |
| `bank-and-drill` | economy | sensible build; before a dim wave buys drills on the night side (up to four, plus a Cooler when the fourth would overheat) instead of panels, banking the rest until they stand (M2.7) |
| `upgrader` | mixed | sensible, but spends what is left on upgrade tiers (cheapest first, one path per tower) instead of extra towers (M2.2) |
| `focus-builder` | layout | never-rotate's plan, but each lane's towers stand on the slots whose bent mounts cross on the lane, forming focus zones (M2.11; try `--waves m2_swarm`) |

## CSV columns

`wave_set, planet, seed, bot, family, outcome (won/lost/unfinished), waves_survived, waves_total, hp_left, hp_max, stardust_earned, stardust_from_panels, stardust_spent, stardust_left, run_seconds (sim time in waves), ticks, kills, leaks, leak_damage, towers, panels, upgrades (tiers bought), drills, coolers (standing at the end, M2.7), overheats (drill shutdowns over the run), rejected (commands the sim refused), hash`, then `leaks_<enemy id>` per enemy definition and `wave<n>_hp_lost` per wave (empty when the run never reached that wave).

## Threat coverage (PLAN §5.1b)

After the CSV the runner prints a summary per bot and a table per wave: a strategy *beats* a wave when it clears it losing at most 25% of max HP, in more than half the seeds; every wave should be beaten by at least three strategies. The control bot does not count. The same check runs from doctest as an opt-in report that warns rather than fails:

```sh
./build/core_tests --no-skip -tc="threat coverage report"
# ORBITFALL_THREAT_SEEDS=<n> (default 5), ORBITFALL_BALANCE_CONTENT=<dir>
```

## Acceptance timing

`./build/sim-runner --runs 100` (11 bots x 100 seeds = 1100 full M1 runs) takes about 82 s wall on the dev container (one core, RelWithDebInfo), well inside the "100 seeded runs in < 10 minutes" target.
