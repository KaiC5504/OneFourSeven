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

## Check round, second pass (resumed)

Logging / silent failures:
1. High: bonjour_browser.mm:129-134, :111 — resolve failures other than PolicyDenied and
   non-IPv4 results log nothing and never retry (phone stuck on "searching…").
2. High: ofs_devlink.cpp:478-481, 529-531 — failed /health logs nothing.
3. High: ofs_devlink.cpp:487-489, 592-594 — failed /params logs nothing; devlink.py's 500
   reason only on its console.
4. Medium: last second before quit/crash never uploads (final flush on quit, plus spec 1).
5. Medium: perf.gd:86 static_memory_mb is 0 in release builds; report iOS phys_footprint
   via OfsDevice.
6. Medium: log.cpp:178,185,200 — FileSink open/fwrite failures are never surfaced.
7. Medium: 400-rejected lines lost on both sides; devlink.py should keep
   logs/<date>/rejected.jsonl; client should count suppressed rejections.
8. Medium: app.start settings always {}; launch_args loses engine flags; add
   display_server and max_fps.
9. Medium: devlink.py keeps no copy of served params; save logs/params/<hash>.json.
10. Medium: gate logs lane (and CI) pass sessions containing app.error lines.
11. Medium: if the extension fails to load there is no log at all; GDScript fallback writer.
12. Low: devlink.py startup prints not flushed. 13. Low: git_sha doesn't mark a dirty tree.

Windows run (all harness controls, link connect, live params and offline catch-up work):
1. No idle throttling: renders at the display rate (240 fps here, 15-31 % of a core)
   with nothing moving; project.godot sets no low-processor mode or max fps; perf.gd
   hard-codes idle true. Spec 13.4.
2. "lost" shows for one frame only (ofs_devlink.cpp ~232 turns Lost into Searching at once).
3. Losing a local link takes ~16 s (5 s timeouts x3) while the label says "connected".
4. Session tail never uploads (= logging 4). 5. Release static_memory_mb is 0 (= logging 5).
6. No way to turn the link off from settings or args ("off" unreachable outside tests).

Godot/GDScript:
1. High: run_tests.gd passes broken tests — scripts that fail to load vanish from the
   count, and runtime errors in a test count as pass. Fail on load()==null or
   !can_instantiate(); a runner Logger failing the test on ERROR_TYPE_SCRIPT; gate.py checks
   the expected test count.
2. (= Windows 1) idle throttling; Perf must count Engine.get_frames_drawn() deltas once
   low-processor mode is on.
3. Medium: headless --dump-state/--quit-after hang forever (frame_post_draw never fires
   headless), silently. Dump without waiting for a draw; screenshot only when not headless;
   timeout that logs.
4. Low-medium: perf.gd logs a false hitch after every resume; reset on RESUMED.
5. Low: Link is live during tests and gate runs (sessions land in logs/, /params can apply
   mid-suite). run_tests.gd disables Link, or a --no-link user arg.
6. Low: untyped_declaration warnings as errors in project.godot (test_log.gd:38, :67).
Verify on the first simulator/phone log: platform "ios-simulator" detection; perf target
from screen_get_refresh_rate() vs actual render rate.

Test gaps (sketch tests in scratchpad/checks/tests):
1. High: (= Godot 1) runner passes crashed and unparseable tests.
2. Medium: C++ and Python params hashes disagree for some full-precision doubles
   (nlohmann dump isn't always shortest; 11/20000 in [1e-3,1e3], 471/20000 in
   [1e15,1e16) where devlink.py's workaround is wrong). Define one number format both
   sides implement (e.g. printf "%.17g" in C++ and Python, or explicit shortest
   round-trip), update the protocol doc, add a cross-language test over many doubles.
3. Medium: (= logging 6) inert FileSink silent.
4. Medium: gate.py crashes with a traceback (no summary, stale gate-passed kept) on an
   unexpected exception; an all-skip gate exits 0.
5. Medium-low: check_logs.py exits 0 with a missing path beside a good file, passes empty
   logs, accepts ts 1e999.
6. (= spec 5) 413 and 404 on /logs retried forever.
7. Low: Dictionary keys 7 and "7" collide silently (ofs_log.cpp:167); harness
   `-DemoScreen --quit-after=3` swallows the flag, `--quit-after=1e400` = inf, repeated
   flags silently last-wins; perf frames past 512/s dropped from stats; from_jsonl accepts
   ts 1e300 that devlink rejects.
8. Weak tests: test_log.gd:177 can't fail; test_link.gd:214 passes for the wrong reason.
   Add tests for the paths the sketches proved: 503 loses link, 30 s backoff cap, /params
   200 without hash, /health ok:false, Variant edge cases, devlink routing edge cases.
9. tools/test_devlink.py copies the working-tree core/data, so concurrent live-params
   checks change what it tests: copy from git (HEAD) or a fixture instead.

## Fix round (started)
Contracts 02d6b68: GODOTCPP_DISABLE_EXCEPTIONS OFF, FileSink::error, OFS_ASSERT,
ofs_params_hash CLI, OfsDevice.memory_footprint_mb, low_processor_mode, MSAA 4x, typed
GDScript as errors, log schema v2, protocol (file-based upload, 413/404, archive, %.17g
number rule), spec/CLAUDE.md/skill doc fixes. Five fix agents: fix-core, fix-link,
fix-game, fix-tools, fix-scripts. If interrupted, re-run whichever hasn't merged.
Merged: fix-tools, fix-core (+72d504e), fix-scripts. Accepted limitation: OfsLog.content_hash
hashes an integer beyond int64/uint64 as a double where devlink/ofs_params_hash refuse it;
real params never contain such integers (move the overflow check into ofs::params if they
ever might). Waiting on fix-link, fix-game.

## Fix round merged (f310a47)
All five fix branches merged; `gate.py --quick` green (build, core, tools incl. 20,019-line
hash parity, godot 86+ tests with every file loading, workflows, logs). Repo created
(KaiC5504/OneFourSeven, public), testflight environment main-only, fork-PR approval, five
secrets set, ASC_APP_ID 6829495084. First iOS compile check compiled everything incl. the
.mm files; failed only at framework naming (fixed in 1630665). Running: seam review,
second Windows run, CI on dev. Then: ff main, push, full gate on main, TestFlight build 1
(`testflight.py start --create-signing`).

## CI on dev
Seam fixes 9491730 (test runs log to user://test-logs, smoke run off the link, footprint
in MB of 1e6 with a reported failure). Windows CI: the first Godot import on a fresh
runner can crash on exit; gate.godot_import retries once (772c1e9), green since. iOS: the
simulator libgodot.a holds only x86_64 objects, so the compile check builds x86_64 for
`generic/platform=iOS Simulator` (Xcode 26 lists no Rosetta destinations) and installs on
the booted arm64 simulator under Rosetta (52bf36e).
iOS run 41340675458: launches, table.loaded 20 s after launch under Rosetta, log schema-valid.
The simulator runs Godot's Compatibility renderer on Apple's software OpenGL (app.start
`renderer`), so its first frame lags the table by ~5 s and it warns about RGBAFloat
textures; ab788c4 screenshots after a perf.second with frames drawn and allows that one
GLES3 warning. The simulator screenshot is not the phone's look (Metal, Mobile renderer).
The check now collects every launch and log problem before failing (79d7ded).
TestFlight pre-flight review: nothing certain to fail in the workflow; order is ff main
(testflight.yml isn't on main yet), full gate on main, `start --dry-run --create-signing`
(first Release/clang/device path), then `start`. Owner side: the internal tester group
needs automatic distribution on.
iOS run 41343966322 on 8f98887: green. The first frame took 117.6 s on the simulator's
software GL under Rosetta (perf.hitch), then idle at 0 fps; footprint ~930 MB there (watch
it on the phone). Its screenshot is solid magenta: the simulator can't show the look, so
CLAUDE.md now checks screens with Windows screenshots. main fast-forwarded to 8f98887,
full gate passed on it; TestFlight dry run with --create-signing started (41346458241).
TestFlight dry run 41346458241: green first try (Release extension, export, new App Store
profile, IPA; build number 1). Upload started: 41347888441.
Follow-up (not blocking): the headless macOS editor that exports has no macOS build of the
extension, so --import/--export log parse errors for every script using Ofs* classes (and
"No GDExtension library found for macos.arm64"). Exported scripts still run (the simulator
log has no script errors), but the noise can hide real export errors. Fix: build the
extension for macos.arm64 editor on the runners (gdextension entry + build step), then make
the export step fail on SCRIPT ERROR.

## Shipped
TestFlight build 1 (run 41347888441, 8f98887): uploaded and processed by Apple. dev
(41e823a and later) green on both CI checks. Waiting on KaiC's phone check: install,
landscape both ways, allow local network, "PC link: connected" with devlink running,
Windows shortcut works. First phone session logs land in logs/<date>/ via devlink: check
app.start renderer (expect mobile/Metal), footprint_mb, perf.second fps and idle.
Phone (iPhone18,2, iOS 27.2, build 1): renderer mobile / Apple A19 Pro GPU (Apple9), 120 Hz,
no app.error, footprint 320-410 MB, thermal nominal. Bonjour found the PC in 40 ms and the
first session (local-network prompt) uploaded as backlog. Idle draws ~1 frame/s, but the
main loop still ticks at 120 Hz (frame_ms_avg 8.33): low-processor mode doesn't slow the
loop on iOS, where the display link drives it. M1 candidate: cap Engine.max_fps while idle.
First launch hitch 2.8 s (pipeline compile), none on the second.
Follow-ups done in M1 (m1-infra, run 41396740025): CI builds a macOS editor extension so import/export parse every script, check-godot-log fails the export on any script error (zero now), and idle caps Engine.max_fps at 30 (Windows idle 6.9 -> 33.3 ms).
