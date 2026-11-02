# Third-party components

| Component | Version | Licence | Where | Source |
| --- | --- | --- | --- | --- |
| Godot Engine | 4.7.1-stable | MIT | engine and export templates (not in the repo) | https://godotengine.org |
| godot-cpp | 10.0.0-stable | MIT | `thirdparty/godot-cpp` (submodule) | https://github.com/godotengine/godot-cpp |
| doctest | 2.5.3 | MIT | `thirdparty/doctest/doctest.h` | https://github.com/doctest/doctest |
| nlohmann/json | 3.12.0 | MIT | `thirdparty/nlohmann/json.hpp` | https://github.com/nlohmann/json |
| App icon | generated | ours | `game/icons/icon.png` | `tools/make_icon.py` (Pillow, numpy), no third-party artwork |
| pooltool | main, 2026 | Apache-2.0 | physics in `core/src/physics/` and `core/src/math/` ported from or checked against its models (each file names the module); reference numbers in `core/tests/fixtures/reference/` | https://github.com/ekiefl/pooltool (Evan Kiefl) |
