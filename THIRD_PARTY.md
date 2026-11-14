# Third-party components

| Component | Version | Licence | Where | Source |
| --- | --- | --- | --- | --- |
| Godot Engine | 4.7.1-stable | MIT | engine and export templates (not in the repo) | https://godotengine.org |
| godot-cpp | 10.0.0-stable | MIT | `thirdparty/godot-cpp` (submodule) | https://github.com/godotengine/godot-cpp |
| doctest | 2.5.3 | MIT | `thirdparty/doctest/doctest.h` | https://github.com/doctest/doctest |
| nlohmann/json | 3.12.0 | MIT | `thirdparty/nlohmann/json.hpp` | https://github.com/nlohmann/json |
| App icon | generated | ours | `game/icons/icon.png` | `tools/make_icon.py` (Pillow, numpy), no third-party artwork |
| Table sounds | generated | ours, no licence needed | `game/audio/*.wav` | `tools/make_sounds.py` (plain Python modal synthesis), no recordings or third-party samples |
| Pocket nets (procedural table) | generated | ours | drawn when the procedural table is built, no file | `game/scripts/view/procedural_table.gd` (`_net_maps`: crochet albedo and normal map), no third-party artwork |
| pooltool | main, 2026 | Apache-2.0 | physics in `core/src/physics/` and `core/src/math/` ported from or checked against its models (each file names the module); reference numbers in `core/tests/fixtures/reference/` | https://github.com/ekiefl/pooltool (Evan Kiefl) |

## Textures and lighting (CC0)

Each asset's own page states CC0 1.0 (public domain, no attribution required). Downloaded at 1K; how
each file was derived is in `game/textures/README.md`.

| Asset | Source | Author | Licence | Used for |
| --- | --- | --- | --- | --- |
| Fabric 034 (felt) | https://ambientcg.com/view?id=Fabric034 | ambientCG | CC0 1.0 | `game/textures/cloth/`: the cloth's grain (albedo made neutral grey, normal, roughness) |
| Dark Wood | https://polyhaven.com/a/dark_wood | Dario Barresi, Dimitrios Savva, Rico Cilliers | CC0 1.0 | `game/textures/rail/`: the mahogany rails |
| Leather 030 | https://ambientcg.com/view?id=Leather030 | ambientCG | CC0 1.0 | `game/textures/leather/`: the pocket fittings' grain (normal, roughness; albedo darkened, in the materials preview) |
| Ash Veneer | https://polyhaven.com/a/ash_veneer | Jenelle van Heerden | CC0 1.0 | `game/textures/cue/ash_*`: the cue's shaft |
| Flamed Black Veneer | https://polyhaven.com/a/flamed_black_veneer | Jenelle van Heerden | CC0 1.0 | `game/textures/cue/ebony_*`: the cue's butt (albedo darkened to ebony) |
| Studio Small 04 (HDRI) | https://polyhaven.com/a/studio_small_04 | Greg Zaal | CC0 1.0 | `game/env/studio_small_04_1k.hdr`: ambient light and the dim room the balls reflect |

## Bought asset (not in the repo)

One asset is bought rather than CC0, with the owner's agreement. Its licence forbids sharing the
files, so none of them is in this repository, in CI or in a TestFlight build made by CI: they live
only in the gitignored `licensed/` (as bought) and `game/licensed/` (fitted for the game) on the
owner's machines. A build without them draws its own table (`game/scripts/view/procedural_table.gd`).

| | |
| --- | --- |
| Asset | Snooker Table (table with cushions, pockets, nets, legs and frame; cue and rest racks; chalk; hanging lamp; balls; cue; rest, spare cue and triangle) |
| Author | Studio-Lab |
| Source | Fab (fab.com) |
| Licence | Fab Standard Licence: use in a product is allowed, redistributing the files is not |
| Bought | 2026-11-15, by the owner |
| Files | `licensed/snookertable.blend` and its textures as bought; `game/licensed/*.gltf`, `*.bin`, `*.jpg`, `*.png` and `fit.json` as fitted. All gitignored |
| Fitted by | `tools/fit_table.py` (Blender): lengthens the model's straight runs to the full-size playing area, draws the corner cushions back to the regulation gap, paints the baulk line, D and spots out of the bed (the game draws its own), shrinks the textures and exports glTF with the cloth at height 0 in the game's frame. `tools/licensed_imports.py` writes the textures' Godot import settings |
| Used for | `table.gltf`: the table the game shows (`game/scripts/view/table_view.gd`: Table, Racks and Chalk, its own materials and textures; the physics outline in `core/data/table_shape.json` was measured from it). `lamp.gltf`: the lamp over the table (`game/scripts/view/stage.gd`). `balls.gltf` and `cue.gltf`: the balls and the cue. `extras.gltf` (loose rest, spare cue, triangle) is exported and not used |
