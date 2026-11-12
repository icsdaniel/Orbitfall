# Orbitfall — Design Exploration

> Companion to `PLAN.md`; the build handoff is `BRIEF.md`. Design work runs alongside the build: question the riskiest mechanics, test them with throwaway toys and bots, and record every decision in `docs/design/DECISIONS.md` (seeded from this file by task D1). Decisions there override `PLAN.md`.
> North star: **it must feel like a tower defence first** (PLAN §1). Every idea below is judged by whether it makes the core loop (place, shoot, earn, survive) better.

---

## 1. Decided (Daniele, 2026-10-25)

| # | Question | Decision |
|---|---|---|
| Q1 | Does rotation stay meaningful? | Rotation aims shots at enemy **weak points**: Bulwark enemies take about 25% on the body and 3 to 4× on a weak-point hit (firing tower within ±8° of the enemy's bearing, i.e. directly under it; geometry fixed in review 2026-10-26); the placement-neutral **Lance** tower exploits it. Lanes from enemy planets are a secondary driver |
| Q2 | Is rotation a chore? | **No Autopilot (decided).** The player always rotates by hand; control scheme tuned in toy D2 |
| Q4 | Attention across zoom bands | **Both** pause between waves and slow time while zoomed in on the surface (default 75% speed, `surfaceTimeScale`, tuned in toy D4) |
| Q5 | Millions vs the feeling of millions | The feeling is enough as long as it delivers; swarm clouds and a 2-second "name the biggest threat" test |
| Q6 | Win condition and pacing | **Win by destroying all enemy planets, then optional endless mode** |
| Q8 | What does landing do? | **Only subtracts planet HP**; no surface walkers, no slot attacks |
| Q10 | Counters | **No hard counters.** Nothing immune; every tower does at least 25% damage; counters are multipliers. Enemy planets are more likely to spawn a signature enemy, shown in the Sector view |
| Q13 | What limits drills | **Rising instability**: past a threshold a drill overheats and shuts down for a cooldown; relieved by a Cooler or pausing. Nothing is damaged |
| Q22 | What does a falling object hit? | **Planet HP only. Buildings are never damaged** |
| Q25 | Light and cooldowns | Buildings with an active ability and cooldown (shields first) recharge faster while their slot is lit |
| Q14 | Resource types | **Stardust only** |
| Q18 | Economy source | **Sunlight replaces falling motes.** Solar panels earn only inside a drifting sun beam; drills as second source. Sun drifts at constant slow speed behind an extensible `SunPath` |
| Q22b | Shields (Daniele, 2026-10-26) | **Shields are a core rotation mechanic:** objects fall from space and shields protect only the arc they face; panels face the light, shields face the threat. Details Q22 to Q24 |
| Q29 | Forecast and answers (Daniele, 2026-10-26) | **The player always knows what is coming:** light level per wave (dim rounds) and threat types and bearings, shown in a forecast strip. Every threat has at least three answers from different families; nothing has a single counter |
| Q31 | Forecast scope (Daniele, 2026-10-26) | The forecast also shows **which enemy planet is about to spawn and where**, whether **enemies or conditions are limiting the light** (planet dimming, eclipses, expected Eclipser shadow arcs), and live shadow-casters already on the field |
| Q21 | Theme | **Chase the light is the core foundation:** little sunshine left, aliens want to eclipse it. Large enemies **cast shadows on the planet**, enemy planets **dim the sun** and killing them restores it; present from M1 |
| G1/G2 | Day/night side, planets as characters | In: the sun beam, and home planets whose size sets rotation speed, HP, slots and lit fraction |
| Q33 | Building rotation (Daniele, 2026-10-27) | **Explore bent aim and focus fire:** some turrets fire at a fixed angle off their slot normal, and several towers can combine into focused fire. Form of the bend open (Q33 below) |
| — | Orientation, target device | Landscape two-thumb play; mid-range Android 2021 to 2023 |

Dropped along the way: falling motes, collector buildings and tap-to-collect; surface-walking landed enemies; hard immunities (camo untargetable, burrower invulnerable, Bulwark immune).

---

## 2. Open questions (each has a proposal and a test)

### Q3. Gesture budget on landscape two-thumb
Left zone rotates, right zone taps, places and fires actives; pinch zooms. Open details: does a drag on the planet itself rotate too, and how does tap-to-select avoid colliding with placement?
- **Proposal:** drag in the left zone rotates; tap selects/targets; long-press places or inspects; drag from the core fires the slingshot; pinch zooms.
- **Test (toy D2):** 5-minute sessions per scheme on a phone; measure taps per minute and self-reported fatigue.

### Q7. What limits satellites?
Without a limit, satellites make the surface puzzle irrelevant.
- **Proposal:** satellites need a ground **uplink** building within range, tying space to the surface layout and to rotation. Alternatives: orbital capacity per ring, shared upkeep.
- **Test:** bot runs with and without the rule; check surface decisions still matter.

### Q9. Slingshot and rotation
- **Proposal:** the slingshot fires along the planet's up vector, so rotation aims it (one control, two jobs). Alternative: free aim from the core.
- **Test (toy, before M5.1):** both variants on a phone; Daniele picks.

### Q26. Which active weapons enter the prototype?
Decided: Slingshot, Target Missiles and an Orbital Gun satellite with adjustable altitude, each with its own skill tree. The **Sun Lens became a turret** (decided 2026-10-26, see PLAN §5.6) rather than a weapon. Remaining candidates: **Gravity Tractor** (pulls swarms into a clump), **Rotation Railgun** (charged shot along the planet's up vector, aimed purely by rotation) and **Orbital Minefield**.
- **Proposal:** build the three named; keep the others in the backlog. Every weapon must be manually fired and can never damage a building.
- **Test (weapon toy D5):** one scene per weapon, 5 minutes each; Daniele ranks fun and clarity.

### Q30. How far ahead does the forecast see, and how precise is it?
- **Proposal:** the next wave exact (types, counts, bearings, light); two more waves as icons only; the rest as light level only; scouting (Sector view, Scanner satellite) sharpens further waves. Risk: too much foresight makes the game solved; too little feels unfair.
- **Test:** bots with exact versus fuzzy forecasts; humans at the M1 gate. Tune reveal rules in `content/waves/`.

### Q27. How do weapon skill points work?
- **Proposal:** a separate tree per weapon, points earned every second level-up so the pick-1-of-3 cards stay for towers and passives. Alternative: weapon nodes appear as cards in the same pick (simpler UI, but they crowd out tower cards).
- **Risk:** one weapon dominating by being too good; bot runs for each weapon must land within 25% of each other.

### Q28. Does the Orbital Gun's altitude matter enough?
Low altitude: fast sweep, strong, short reach. High altitude: slow sweep, long reach, weak. If one setting is always best, the slider is decoration.
- **Test:** bot at fixed altitudes versus an adaptive bot; adaptive should win clearly.

### Q12. Is the solar panel placement a real decision?
Panels in the sun are good; panels out of it are idle. Risk: players never rotate to protect income, or never risk leaving the light.
- **Proposal:** about 90° lit arc, panels moderate rather than the whole economy; drills pay when the beam is elsewhere.
- **Test (toy D3):** bots with "keep panels lit" vs "ignore the sun"; neither should dominate by more than 25%. Tune arc width and drift speed.

### Q15. Does the economy snowball or stall?
- **Proposal:** diminishing returns per building type and a soft income cap that rises with run stage.
- **Test:** bot runs plotting income against spend; target is spend-limited early, slot-limited late.

### Q17. Do kills still pay?
- **Proposal:** kills give XP and only a very small stardust trickle, so stardust is the sun/drill economy and XP is the combat reward. Confirm after the M1 feel gate: if the loop feels flat, kills may pay more.

### Q19. Planet balance
Bigger planets could simply be better (more HP and slots) unless slow rotation and low lit fraction truly hurt.
- **Test:** bot comparison of the three home planets (task M2.8); none may be strictly best.

### Q20. Reference feel
Which games should this feel like, and what exactly do you like about them (rotation and zoom feel, screen-full-of-enemies look, economy)? Needed so agents can calibrate placeholder feel. **Asked, awaiting answer.**

---

### Q33. Where does bent aim come from? (asked 2026-10-27)
Options: (a) the player sets a mount angle at placement (0, ±30°, ±60°; re-aim costs 10%, between waves only); (b) fixed per tower type (e.g. a Flanker always at 45°); (c) a Mirror/Deflector building bends neighbours' shots.
- **Proposal:** (a), on aim-mountable towers only; the Lance's weak-point rule ignores offsets.
- **Risk:** bending lets towers on the sunny side cover lanes, which could dissolve the panels-vs-targets tension. Limit ±60° keeps the far side unreachable.
- **Test (toy D7, bots):** chase-light bot with and without mounts; the tension must survive (mounts help by under 25%).

### Q34. How strong is focus fire?
Where aim axes cross within range, a focus point forms in the planet frame and turns with the planet.
- **Proposal:** +25% damage per extra feeding tower on enemies inside the zone (radius 30), cap +50%; two Sun Lens beams crossing merge into one with summed power and pierce.
- **Risk:** one giant focus point becomes the only strategy. Zones are small and fixed relative to the planet, so swarms on two lanes can't both be in one.
- **Test:** focus-builder bot vs no-focus bot on swarm waves: 10 to 40% better, not more; Daniele judges readability in toy D7.

### Q32. Can satellites be destroyed? (reviewer default, confirm)
The never-damaged rule covers buildings; satellites were planned with HP and hunters. **Default:** satellites are never destroyed either; hunters **jam** them (offline for a while). Keeps one consistent rule and avoids a repair loop.

### Q23. What does a shield cost?
- **Proposal:** arc about 60°, an energy pool that drains when it absorbs, offline and recharge when empty, plus a small stardust upkeep while online. Risk: the upkeep makes shields a tax rather than a choice.
- **Test:** bot runs with and without upkeep.

### Q24. Does three-way rotation overload the player?
Panels face the light, shields face the threat, towers face targets. If the best angle is unclear or constantly shifting, players give up and park.
- **Mitigations:** introduce in order (sun in M1, shields in M2), clear telegraphing (sun beam, threat streaks, edge indicators), a rule that the best angle is always visible (sun beam, threat bearing and weak-point bearing drawn together), and a layout that lets one angle serve two goals most of the time.
- **Test:** the "balance" bot versus single-goal bots; Daniele's feel gate at M2.

## 3. Backlog of ideas (deferred unless chosen at the M2 gate, task D6)

| Idea | Why it might be great | Risk |
|---|---|---|
| Projectile gravity: slow shells curve around the planet, lobbed shots can orbit once | Distinctive look of fire leaving the planet | Harder to aim |
| Spin momentum: fast spinning charges a dynamo that powers actives | Rotation becomes a skill | Encourages spam-spinning |
| Debris rings: kills leave orbiting debris that shields or hits satellites | The "mess" becomes a mechanic | Readability |
| Capture instead of destroy: convert a broken enemy planet into an outpost | Strategic choice per planet | Scope; one planet first |
| Moon platform: a player moon carrying towers | Moving placement | Complexity |
| Scouting: tapping an enemy planet at Sector zoom scans it for XP | Makes zooming out a deliberate action | Must not become a chore |
| Run mutators (all camo, double spin, no satellites) | Cheap replayability | None |
| Combo shots: projectiles crossing a satellite field change type | Emergent synergy | Combinatorial balance |
| Tap on the planet core for a small stardust pulse | Active income feel | Clashes with slingshot-from-core |
| Second resource: starshards for upgrades | A spend-now-or-save decision | UI and balance work |

---

## 4. How the design work runs

- **Toys first.** Each toy is a single GDScript scene in `toys/`, built in a day, no tests required, published at `/toys/<name>/` for Daniele to try on the phone. Toys: **D2 rotation and control schemes** (including weak-point aiming and panels versus shields), **D3 sun and economy** (arc width, drift speed, panels vs drills, planet sizes), **D4 zoom and slow time**.
- **Bots answer design questions.** The deterministic sim lets scripted bots (`tools/sim-runner`) detect dominant strategies and measure run length, income curves and leaks, cheaply.
- **Decision log.** `docs/design/DECISIONS.md`: one entry per question with options, evidence (toy or bot result), decision and date. Agents read it before every task.
- **Design-critic agent at every milestone gate.** Prompt: *"Play the build through the band-screenshot run, the Android debug build and the bot runner. Against the pillars in PLAN.md §3 and DECISIONS.md, list the 5 biggest design problems and 5 opportunities, each with a proposed test."* Output goes to `docs/design/reviews/MN.md` for Daniele to triage.
- **Live tuning.** A dev-only panel edits content values at runtime (rotation speed, arcs, sun speed, spawn budget) so questions are answered by feel in minutes.
- **Feel metrics** in the debug overlay: taps per minute, time per zoom band, fraction of time with a target in arc, income lost to being out of the sun, leaks per lane.
