# M2 progress (the feel build)

Plan approved 2026-11-13 with every recommendation: the app opens into the free table; a cue ball
with six small red dots, colours plain; finger stroke by default; spin and elevation reset after
each shot (a setting keeps them); sounds generated in code. KaiC added: nothing we run on the Mac
may make a sound (`scripts/mac.py` stops unless the Mac is muted; agent-driven game runs start
muted).

## Contracts
Binding `OfsPhysics` / `OfsShotRecord` (gdext/src/ofs_physics.*) stubbed; core
`record_query.hpp` (impact speeds, path sampling) and a `log_shot` overload with the game's
context, stubbed; GDScript skeletons with fixed node paths in `game/scenes/main.tscn`; `ShotSetup`
and `ShotPlayback` real; `Settings` and `Physics` autoloads; `Params.current_text`,
`pending_hash`, `commit_pending` stubs; harness hooks on main.gd; keyboard test aids (F3 overlay,
Space shoot, R retake, Ctrl+Z / Ctrl+Y); 16 new log events; named positions synced into
`game/data/positions/`.

## Wave 1 (9 builders, launched 2026-11-13 on 32f4be7)
Addenda before launch: 8497fc2 (OfsPhysics.opening_rack, outline names and kinds), 32f4be7 (test slot
test_record_query_impl.cpp for the binding agent).
A binding, B look, C playback + sound + haptics, D gestures + aim + fine-tune + camera,
E spin + elevation + stroke + power bar, F guides, G harness channel + overlay + params + perf,
H free table + settings, I independent tests.

## Wave 2
J integrator: main.gd / main.tscn / hud.gd, the shot flow, layout, edit-mode interaction, demo
screens. After G merges: run_debug.py passes --mute unless --sound.
