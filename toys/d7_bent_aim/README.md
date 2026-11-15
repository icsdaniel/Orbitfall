# Toy D7: bent aim and focus fire

Throwaway toy for DESIGN Q33 (where does the bend come from?) and Q34 (how strong is focus fire?). Pure GDScript, one scene, published by CI at `/toys/d7_bent_aim/`. Run locally with `godot --path . res://toys/d7_bent_aim/d7_bent_aim.tscn`.

## What to do
- **Drag anywhere** to spin the planet (same grab-to-spin as the game). Desktop: mouse, or A/D.
- **Tap a slot** to select it; the bar at the bottom then offers build, lean, re-aim or sell.
- Enemies arrive in clumps on two lanes that sway and move every wave. Sweep a focus point onto a clump by rotating.
- **Start wave** (or Space) begins the next wave; the red bands show where it will come from.

## The forms (buttons top right, or keys 1 to 4)
1. **Straight**: baseline, no bend.
2. **Fixed mount**: pick 0, ±30° or ±60° when you build; sell to change it.
3. **Re-aim**: same, and you can re-aim between waves for 10% of the price.
4. **Twins**: towers within 4 slots pair up and converge on a point at the "twin focus distance". An odd tower fires straight.

Switching form keeps the towers, so the same layout can be compared under each form. The top-left text keeps the last wave result per form (killed, leaked, share of damage that came from focus).

## What you see
- Blue line and faint wedge: a tower's aim axis and firing arc (±25°, range 300).
- Yellow reticle: a focus point, where two or more axes cross within both ranges. Its label is the bonus. It turns with the planet. Yellow thick tracers are boosted hits.
- Panel: DPS with focus and without, and sliders for bonus per extra tower (25%), cap (50%), zone radius (30), mount step (30°), twin distance and enemy rate.

## Simplifications
- 18 slots (every other slot of the medium planet) so each is a thumb-sized target.
- One tower type, instant hits, full refund on sale. Towers prefer targets inside their own focus zone, then the one closest to the planet.
- No sun or panels here, so the panels-vs-targets tension (Q33 risk) is not tested; that needs the bots.

## Questions for Daniele
- Which form feels best, and is choosing a lean at build time fun or a chore?
- Do reticles and axis lines read at a glance?
- Is sweeping a focus point onto a clump satisfying? Is zone radius 30 too small to hit?
