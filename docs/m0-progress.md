# M0 progress (paused 2026-11-02)

Resume with the `running-a-milestone` skill at step 5 (check round), then 6 and 7.

## Done
- Contracts and all 9 builder branches merged on `dev`.
- Fixes after merge: static release CRT (a6894c2); Bonjour type `_ofs-devlink._tcp`
  (7505af0); `config/name.ios="147"` and `import_etc2_astc=true` in project.godot.
- The fresh-import exit crash agent G saw only happened with the contract stubs; three
  fresh imports with the real code exit 0.
- `python scripts/gate.py` (full) passed on a clean tree: build, core, godot 66/66, tools,
  workflows, logs (real session log validated), windows-export; 140 s.

## Check round
Finished: spec, comments, CI/secrets, screens. Stopped before reporting (re-run them):
C++ review, Godot/GDScript review, silent failures + logging (incl. devlink end to end),
test gaps, Windows run (build_windows.py, idle CPU, link, live params, harness controls).

### Findings to fix
Spec:
1. Earlier session log files in user://logs are never uploaded; only the live ring
   buffer is (gdext/src/ofs_log.cpp ~197, core/src/log.cpp FileSink). Spec 16.10 needs
   crash/offline sessions to upload when the link next connects. Highest priority.
2. Windows logs go to %APPDATA%/Godot/app_userdata/One Four Seven/logs unless devlink runs:
   update spec 16.10 and CLAUDE.md to say so.
3. testflight.py start never checks gate.py's `gate-passed` record (also CI finding).
4. windows-ci.yml runs `gate.py --quick`, skipping the export lane spec 16.8 asks for.
5. Client treats a 413 as retryable and can stall on one oversized line
   (ofs_devlink.cpp ~310, ~564); document 413/411/404 in docs/devlink-protocol.md and
   drop + log 413 batches like 400.
6. Nothing emits app.error kind `assert`; table_geometry.cpp uses plain assert.
7. log.dropped is only checked inside OfsDevlink::tick, so it never fires with the link off.
8. No one-command debug build + run script (spec 17.1).
9. Spec 4 / M0 entry and the skill still mention SCons and a "4.7 branch"; reality is
   CMake only and godot-cpp 10.0.0-stable with GODOTCPP_API_VERSION 4.7.
10. CLAUDE.md: Godot test command needs sync_data + --import first; add devlink and
    build_windows --shortcut commands; layout misses test_ci/, check_logs.py, OfsDevice,
    game/data/; compile check is path-filtered; stale status line.
11. iOS 26 SDK is never asserted in the workflows.

CI/secrets:
1. scripts/testflight.py SECRETS omits SIGNING_NAME, so an unset secret would print the
   legal name unmasked; add it plus a test that SECRETS equals the workflow's secrets.
   The global ios-testflight skill's testflight.py has the same gap (owner's call).
2. (= spec 3) gate-passed not checked.
3. ios-plist should assert CODE_SIGN_STYLE = Automatic.
After the repo exists: verify the environment's main-only lock, secrets, fork-PR approval.

Screens (all three renders FAIL on looks; positions and framing pass):
1. No anti-aliasing (set MSAA/FXAA for the Mobile renderer).
2. Contact shadows barely visible; sawtooth shadow artefact under the near rail.
3. Pockets are crude black discs spilling over rails/cloth (real geometry is M1; make the
   placeholder clean).
4. PC link label is 9 pt at 3x, under iOS's 11 pt minimum.
5. hud.gd re-places the label only on resize; safe-area insets reported late would leave
   it under the Dynamic Island. Check on the simulator.
6. 2868x1320 renders need a borderless override.cfg on a 2560x1440 display.

Comments: remove "Agent X owns this file" lines (log.gd, params.gd, perf.gd, harness.gd,
main.gd); harness.gd says "compiled out" (it's inert); run_tests.gd claims a 10 s timeout
it doesn't have; remove the stub-era skip guards in test_link.gd, test_params.gd,
test_log.gd, test_main.gd, test_table_binding.gd and tools/test_check_logs.py (they'd let
a broken binding pass silently); gate.py Skip scaffolding and docstring; log_schema.hpp
reasons list; CMakeLists /utf-8 under the FP comment; ofs_devlink.h "all network work";
bonjour_browser.mm retry wording; testflight.yml manual-steps references.

## Waiting on the owner
- Create the public repo KaiC5504/OneFourSeven and push main + dev (blocked for Claude).
- Apple setup: register com.kaichuan.onefourseven, App Store Connect record "One Four
  Seven" (send its Apple ID for ASC_APP_ID), internal tester group, `testflight`
  environment + secrets (ios-testflight skill manual-steps §2–§5).

## Then
Fix round (agents grouped by file), seam review, second Windows run, push, iOS compile
check, signed build with `testflight.py start --create-signing`, TestFlight build 1.
