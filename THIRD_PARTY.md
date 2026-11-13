# Third-party components

| Component | Version | Licence | Where | Source |
| --- | --- | --- | --- | --- |
| Godot Engine | 4.7.1-stable | MIT | engine and export templates (not in the repo) | https://godotengine.org |
| godot-cpp | 10.0.0-stable | MIT | `thirdparty/godot-cpp` (submodule) | https://github.com/godotengine/godot-cpp |
| doctest | 2.5.3 | MIT | `thirdparty/doctest/doctest.h` | https://github.com/doctest/doctest |
| nlohmann/json | 3.12.0 | MIT | `thirdparty/nlohmann/json.hpp` | https://github.com/nlohmann/json |
| App icon | generated | ours | `game/icons/icon.png` | `tools/make_icon.py` (Pillow, numpy), no third-party artwork |
| Table sounds | generated | ours, no licence needed | `game/audio/*.wav` | `tools/make_sounds.py` (plain Python modal synthesis), no recordings or third-party samples |
| pooltool | main, 2026 | Apache-2.0 | physics in `core/src/physics/` and `core/src/math/` ported from or checked against its models (each file names the module); reference numbers in `core/tests/fixtures/reference/` | https://github.com/ekiefl/pooltool (Evan Kiefl) |

## Textures and lighting (CC0)

Each asset's own page states CC0 1.0 (public domain, no attribution required). Downloaded at 1K; how
each file was derived is in `game/textures/README.md`.

| Asset | Source | Author | Licence | Used for |
| --- | --- | --- | --- | --- |
| Fabric 034 (felt) | https://ambientcg.com/view?id=Fabric034 | ambientCG | CC0 1.0 | `game/textures/cloth/`: the cloth's grain (albedo made neutral grey, normal, roughness) |
| Dark Wood | https://polyhaven.com/a/dark_wood | Dario Barresi, Dimitrios Savva, Rico Cilliers | CC0 1.0 | `game/textures/rail/`: the mahogany rails |
| Leather 030 | https://ambientcg.com/view?id=Leather030 | ambientCG | CC0 1.0 | `game/textures/leather/`: pocket leathers (albedo darkened) |
| Ash Veneer | https://polyhaven.com/a/ash_veneer | Jenelle van Heerden | CC0 1.0 | `game/textures/cue/ash_*`: the cue's shaft |
| Flamed Black Veneer | https://polyhaven.com/a/flamed_black_veneer | Jenelle van Heerden | CC0 1.0 | `game/textures/cue/ebony_*`: the cue's butt (albedo darkened to ebony) |
| Studio Small 04 (HDRI) | https://polyhaven.com/a/studio_small_04 | Greg Zaal | CC0 1.0 | `game/env/studio_small_04_1k.hdr`: ambient light and the dim room the balls reflect |
