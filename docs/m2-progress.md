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
- D aim + camera (cbeb725): slop 8, tap < 0.5 s, pinch/tilt thresholds 16/12 with hysteresis 40;
  dragging right turns the aim clockwise (negative aim_rad); camera pitch default 16°, zoom
  0.35-1.6, FOV 40. Integrator: FineTuneWheel mouse_filter STOP, right edge ~120×380 in the safe
  area; gestures.pinched → rig.pinch, two_finger_dragged → rig.tilt(relative.y), setup.changed →
  rig.follow, hold around the shot, wheel.stepped → haptics tick; AimControl off during shots/edit.
- F guides (c4f78c6): level 3 shows paths + pace only; pocket mouth → kind "pocket"; throttle 5/s
  with a deferred last prediction; predict_slow over 4000 µs. Integrator: call show_for on
  change (cheap if unchanged), hide_all during shots, last_predict_us → overlay.
  Screens check: spheres near the frame edges look stretched (wide horizontal FOV at 2.17:1).
- B look (ea56e3e): table as one mesh of 4 surfaces swept from the outline, sky-shader canopy for
  reflections, cue ball with 6 dots, drop animation; ~0.9 ms/frame uncapped at 2564×1320 on the PC.
  Integrator: Stage.setup(); TableView.build(Physics.table_shape(), OfsTable.dimensions());
  stage.set_table_bounds(table.bounds()); balls.set_pockets(shape["pockets"]) after each build;
  stage.fit_shadows(camera) on rig.moved; replace main.gd's CSG table and test_main's pocket test.
- A binding (5a7e334, e6d9223): replay of a break-off from the game's line: 171 events, 0 diverged.
  Debug: break-off 3.0 ms through the binding (2.8 core), state_at 22 balls 8.9 µs, path 194 µs.
  Ball lists may carry "pocket" so final_balls() feeds the next shot. Release extension timing
  not yet measured.
- G harness (f9e022a): --mute, --demo-position, --harness-port channel (docs/harness.md,
  tools/harness_client.py), run_debug mutes by default (--sound opts out), Params pending until
  commit_pending(shot_id), Perf.last_summary, DebugOverlay (F3). Integrator: fill main.gd's
  harness_* hooks, three-finger tap → Overlay.toggle, set_shot_times, commit_pending at the strike.
  Unverified: -DemoPosition on iOS needs data/positions in the export filter.
- C playback + sound + haptics (98286a1): sounds synthesized in plain Python (contact pulse
  into modal resonators), PCM imports, roll loop capped at -24 dB in game. Godot 4.7 iOS haptics
  are Core Haptics *continuous* events (no sharpness): if KaiC finds them mushy, the transient-event
  plugin is the fallback. Integrator: frame → TableAudio.on_frame; event_reached → on_event and
  Haptics.on_event unless ShotPlayer.is_skipping(); begin_shot(strike_outcome) before play;
  stop_all on undo/retake.
- H free table + settings (daca28d, e1a2f65): history semantics in its report (setups undoable,
  edit sessions one entry, retake = undo of a shot). Integrator: shot_started(snapshot, shot_id)
  before simulate, shot_finished(final_balls) after playback, ShotSetup.restore on returned
  snapshots, bar ↔ FreeTable wiring, edit-mode gestures. Saved custom setups are M5.
- I spec tests (56cd2d5): all pass against the merged builders. Notes: the miscue ring isn't
  exposed for a direct test; neither spec nor contract fixes the sideways-drag sign (D chose
  right = clockwise).
- Lead: run_tests.gd points Settings at user://test_settings.json so tests never touch the
  player's file.

## Wave 2 (launched 2026-11-13 on the merged wave 1)
J integrator works in the main checkout (the only builder left; saves a godot-cpp build).
The CC0 asset agent runs in parallel (textures, env, its preview); J wires its materials when it lands.
- J integrator (8b393f9..3d61753): free table playable; CC0 materials wired; balls with a soft
  highlight (KaiC disliked the hard canopy reflection: area light removed); mobile reflection
  atlas 256, soft-low shadows. 390 Godot tests; 8/8 shots replay. Open: leaked-RID warning at exit,
  level 3 pace label overlapping balls, -DemoPosition export filter on iOS, cut_half_ball pot window
  0.0015 rad (feel check).

## Check round (launched on 3d61753, dev pushed: CI 43396383322 Windows, 43396383307 iOS)
C++ binding, GDScript/flow, logging + replay, test gaps, spec, comments, screens, Windows run.
- Checks all in (findings in the lead's scratchpad m2-findings.md). Windows CI 43396383322 green;
  iOS 43396383307 failed only on the session-log check (simulator ASTC conversion warning).
- KaiC: builds for him go to the iPhone through the Mac (mac.py phone), not TestFlight. First
  phone build 2611132018 (8cd24d5): starts clean over the link; one 3.4 s hitch in the first seconds.

## Fix round (contracts e.g. below)
Contracts: cancel_press() stubs on FineTuneWheel, ElevationControl, PowerBar; CI allows the
simulator's ASTC warning; export filters include data/positions and exclude dev scenes.
Agents: binding (C++), controls (input/*), flow (main, hud, guides, free table, play), autoloads
(harness, params, physics, perf, settings), look (views, camera, the cue wedge, reflections).
