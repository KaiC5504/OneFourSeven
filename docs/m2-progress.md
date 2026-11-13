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

## Merged
- E cue controls (714678f): stroke speed max·min(push/6000, 1)^1.5 (push = finger travel over the
  last 80 ms), backswing 0.25 m per 880 units pulled, wander -0.1·sideways/forward clamped ±1.5°,
  elevation max·t^2 (half travel = 20°), power max·p². For the integrator: anchor SpinWidget to its
  corner with grow away from it (150×150 collapsed, 420×480 expanded); elevation ~120×420, stroke
  area ~200 wide full height, power bar ≥140×420; wire backswing_changed → CueView.set_pose,
  struck / shoot_pressed → the shot; collapse() / cancel_stroke() when a shot starts. KaiC's feel
  check: 6000 units/s fastest push, exponent 1.5, wander gain 0.1.
