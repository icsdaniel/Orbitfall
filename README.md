# Orbitfall (working title)

A tower defence on a small planet you spin by hand. Towers fire in a cone, so spinning is aiming; money comes only from solar panels in a thin drifting sunbeam, and big aliens park in front of the sun to shade them.

Prototype. Godot 4.5.1 + C++ GDExtension sim core. Start with `BRIEF.md` and `CLAUDE.md`; progress in `docs/STATUS.md`.

```
tools/setup_env.sh                       # toolchain (Godot, templates, SCons)
cmake -S core -B build && cmake --build build && ./build/core_tests
scons -C gdextension                     # build the extension
godot --path .                           # run
```
