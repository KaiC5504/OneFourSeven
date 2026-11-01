# One Four Seven

A single-player, realistic snooker game for KaiC's iPhone 17 Pro Max, built and tested on
Windows first. The design is `docs/superpowers/specs/2026-11-01-one-four-seven-design.md`
(the spec): read the sections a task touches before starting it. KaiC plays and judges;
he does not read code.

Every milestone (spec section 21) runs through the project skill `running-a-milestone`:
one plan KaiC approves, then parallel agents build, check, fix and ship.

## Rules

- Public repo: no secrets in code, logs or CI output. Never add analytics, ads or SDKs
  that phone home.
- `core/` is plain C++17 with no Godot types. Doubles and SI units (metres, seconds,
  kilograms, radians). No fast-math; `-ffp-contract=off` / `/fp:precise` stay on.
- Core coordinates (`core/include/ofs/table_geometry.hpp`): origin at the table centre,
  +x towards the black, +y to the left seen from baulk. Godot maps core (x, y) to
  `Vector3(-y, height, -x)`; `game/scripts/table_space.gd` owns that mapping.
- GDScript is statically typed. Logging goes through the `Log` autoload, C++ through
  `ofs::log`; every event and field is in `core/data/log_schema.json`.
- Every new failure path logs enough to diagnose it from one occurrence (spec 16.10).
- Tests never touch the network (devlink tests use a local server on 127.0.0.1).
- Assets are generated or CC0, each listed in `THIRD_PARTY.md`.
- Comments: sparse, explaining why, never narrating. Match the file around you.
- Never reformat a whole existing file.
- Build files (`CMakeLists.txt` everywhere, `project.godot`, `export_presets.cfg`) change
  only in a milestone's contracts commit or by their named owner.

## Layout

| Path | What |
| --- | --- |
| `core/` | C++ library: logging, parameters, table geometry (later physics, rules, AI); `core/tests/` doctest suite; `core/data/` parameter and schema files |
| `gdext/` | godot-cpp binding (`OfsLog`, `OfsTable`, `OfsDevlink`, `OfsEngineLogger`); `gdext/ios/` iOS-only Objective-C++ |
| `game/` | Godot 4.7.1 project; `game/tests/` headless GDScript tests (`run_tests.gd`) |
| `tools/` | PC tools: `devlink.py` (the phone link server) |
| `scripts/` | `gate.py` (local checks), `build_windows.py`, `testflight.py`, CI helpers |
| `thirdparty/` | godot-cpp 10.0.0 (submodule), doctest, nlohmann/json |
| `docs/` | spec, `log-schema.md`, `devlink-protocol.md` |
| `logs/` | session logs from the game and the phone (gitignored) |

## Building

- Godot: `D:\Tools\Godot\Godot_v4.7.1-stable_win64_console.exe`.
- Native build: `cmake -S . -B build/windows -G "Visual Studio 18 2026" -A x64`, then
  `cmake --build build/windows --config Debug`. The extension lands in `game/bin/`.
- Core tests: `ctest --test-dir build/windows -C Debug`.
- Godot tests: `godot --headless --path game -s res://tests/run_tests.gd`.
- Everything at once: `python scripts/gate.py` (`--quick` for the edit loop).

## Building and shipping

iOS app, developed on Windows with no Mac. GitHub Actions does everything:
`ios-compile.yml` builds the iOS extension, exports the Godot project, compiles and
screenshots on every push, and `testflight.yml` signs and uploads when
`scripts/testflight.py start` dispatches it. The full playbook, including every manual
step and known failures, is the global `ios-testflight` skill. Load it before touching
signing, `testflight.yml`, the export presets or the compile workflow.

Identity: Godot project `game/`, exported Xcode project `OneFourSeven`, bundle id
`com.kaichuan.onefourseven` (extensions: none). Repo `KaiC5504/OneFourSeven` (public).
App Store Connect name: `One Four Seven`, home-screen label `147`, Apple ID: not yet.

1. Work on `dev`. Push. Wait for the compile check and look at the
   `simulator-screenshots` artifact for every screen you touched.
2. `git checkout main && git merge --ff-only dev && git push && git checkout dev`
3. `python scripts/testflight.py start`. Green means Apple has processed the build. After
   any change to `testflight.yml`, run `start --dry-run` first.
4. Report the TestFlight build number, what changed and test steps for the phone.

The owner has authorised commit, push and Actions runs without asking. Never start a
signed build before the compile check is green.

## Status

- M0 paused (2026-11-02): all branches merged, full gate green, check round half done. Resume from `docs/m0-progress.md`.
