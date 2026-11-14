# Table textures and lighting

Materials for the table, cue and stage, all CC0 (sources and authors in `THIRD_PARTY.md`). The
settings below are the ones `scenes/dev/materials_preview.tscn` renders with; build the game's
materials from them.

Two tables use them differently. Where the bought table model is in the project
(`game/licensed/`, never in the repo; see "The bought table" below) the game shows it, with its own
textures, and of the materials here only the cue's and the stage apply. Everywhere else (CI, and
TestFlight builds made there) `procedural_table.gd` draws the table from the cloth, rail, leather
and net materials below.

See them:

```
"D:\Tools\Godot\Godot_v4.7.1-stable_win64_console.exe" --path game --resolution 2868x1320 res://scenes/dev/materials_preview.tscn -- --mute --no-link --screenshot=<abs path> --quit-after=5
```

Left: cue level behind the cue ball. Top right: the far corner from above. Bottom right: the cue's
joint and butt. To try other values without editing the scene, point `OFS_MATERIALS_PREVIEW` at a
JSON file of overrides, for example `{"cloth": {"tile": 0.2}, "balls": {"mirror": true}}` (the
mirror adds a chrome ball that shows exactly what the balls reflect).

## Conventions

- **UVs in metres.** Each texture covers one tile of the size given below, so set
  `uv1_scale = Vector3(1 / tile, 1 / tile, 1)` on a mesh whose UVs are in metres.
- **Grain runs along U**: the wood grain, the leather and the cloth's nap. Give the cloth U along the
  table (baulk to top), the rails U along each rail, the cue U along its length (V around it, as arc
  length). `materials_preview.gd` has `_box` and `_lathe` helpers that build meshes this way.
- **Normal maps are OpenGL style** (green up), which is what Godot expects.
- **Albedo is sRGB, the rest is data.** Godot 4 has no sRGB switch on import: StandardMaterial3D reads
  `albedo_texture` as sRGB and normal and roughness maps as linear. Keep them in the slots below.
- **Roughness maps** are greyscale; set `roughness_texture_channel = TEXTURE_CHANNEL_RED`. The
  material's `roughness` multiplies the map.

## Materials

All maps are 1024 x 1024 JPG.

### Cloth (`cloth/`)

| | |
| --- | --- |
| Files | `cloth_albedo.jpg` (neutral grey, mean 0.8), `cloth_normal.jpg`, `cloth_roughness.jpg` |
| Source | ambientCG Fabric 034, a wool felt scan. The albedo is its luminance with the contrast cut to 35 %, so it only adds a faint mottle; the green is `albedo_color` |
| Tile | **0.12 m** square (the scan gives no size; chosen by eye: at 0.25 m and above it reads as carpet or felt, at 0.12 m as fine baize) |
| StandardMaterial3D | `albedo_color` sRGB (0.11, 0.42, 0.16) in the preview; the procedural table (`procedural_table.gd`) uses (0.10, 0.505, 0.145), with the cushions a shade darker through their vertex colour (0.93); `roughness` 0.85 (about 0.7 with the map); `normal_scale` 0.3; `rim_enabled`, `rim` 0.25, `rim_tint` 0.6; no clear-coat |

Under the stage below the cloth renders at about sRGB (39-52, 118-127, 46-56), a TV tournament green (the
colour was (0.09, 0.46, 0.13) under the stage's earlier, brighter key light, for the same result). The soft
sheen near the camera comes from the roughness picking up the canopy in the reflection probe; rim adds
very little. Anisotropy was tried for a sheen along the nap and looked wrong (dark or washed out), so it
is off. The cushions use the same material.

### Rails (`rail/`)

| | |
| --- | --- |
| Files | `rail_albedo.jpg`, `rail_normal.jpg`, `rail_roughness.jpg` |
| Source | Poly Haven Dark Wood (mahogany), its albedo contrast softened to 55 % so the figure reads as one polished board rather than rosewood |
| Tile | **2.0 m** (the scan's real size); grain along U |
| StandardMaterial3D | `roughness` 0.6 (about 0.38 with the map); `normal_scale` 0.3; `clearcoat_enabled`, `clearcoat` 0.35, `clearcoat_roughness` 0.3 (a satin lacquer: the mirror finish of 1.0 / 0.08 showed the canopy as a hard white streak) |
| On the procedural table | `albedo_color` sRGB (0.86, 0.78, 0.7), a slight warm darkening of the scan, which is already mahogany (mean sRGB 94, 44, 24); `clearcoat` 0.2, `metallic_specular` 0.3. Renders about sRGB (91, 52, 43) from overhead. The earlier tint (0.66, 0.5, 0.32) with the preview's sheen read mauve from above and near black from far down the table |

A tinted Sapele veneer was tried and looked like painted brown; the figure in Dark Wood is what makes it
read as wood. Each rail's grain runs along it, so it turns on the diagonal behind each corner pocket,
where two rails meet.

### Pocket leathers (`leather/`)

| | |
| --- | --- |
| Files | `leather_albedo.jpg`, `leather_normal.jpg`, `leather_roughness.jpg` |
| Source | ambientCG Leather 030, albedo darkened to 45 % (dark brown, nearly black under the canopy) |
| Tile | **0.45 m** (the source's "ca. 45 cm") |
| StandardMaterial3D | `roughness` 1.0 (the map averages 0.48, so it has a soft sheen); `normal_scale` 1.0 |
| Fittings on the procedural table | a 12 mm band standing on each pocket's facings and round its back: the scan's normal (`normal_scale` 0.6) and roughness (`roughness` 0.9) without its albedo, `albedo_color` cream sRGB (0.78, 0.7, 0.55), shaded through the vertex colour from 1.0 on top down to 0.55 on its wall inside the pocket; `clearcoat` 0.25, `clearcoat_roughness` 0.4. The dark albedo is only the materials preview's now: the holes are no longer lined |

### Pocket nets (generated, no file)

`procedural_table.gd` (`_net_maps`) draws the crochet net when the procedural table is first built, in about 4 ms on
this PC: a 64 x 64 albedo and normal map, two diamonds of round cord across a **0.025 m** tile (each
diamond about 12 mm), a knot where the cords cross and the cords swelling three times per diamond
side like crochet stitches. Cord sRGB (0.88, 0.86, 0.8), the gaps (0.3, 0.28, 0.25) for the dark
under the table, off-white on grey as the real nets read rather than white on black; the normal map (OpenGL style) comes from the cords' heights. Roughness is
`cloth/cloth_roughness.jpg` with `roughness` 1.0 and `metallic_specular` 0.2: matt cotton. The net
hangs as a bag from the cloth turned down round the hole, which is the physics' drop circle; its
vertex colour shades it 0.6 at the rim, up to 0.95 in its bowl, 0.3 low down and near black (0.012)
at the opening at its bottom. Its texture keeps a whole number of tiles round each hole and its
diamonds shrink down the net as fast as round it.

### Cue (`cue/`)

| | Shaft: ash | Butt: ebony |
| --- | --- | --- |
| Files | `ash_albedo.jpg`, `ash_normal.jpg`, `ash_roughness.jpg` | `ebony_albedo.jpg`, `ebony_normal.jpg`, `ebony_roughness.jpg` |
| Source | Poly Haven Ash Veneer, unchanged | Poly Haven Flamed Black Veneer, albedo darkened and warmed to ebony |
| Tile | **1.0 m** | **1.0 m** |
| StandardMaterial3D | `roughness` 0.7, `normal_scale` 0.3, `clearcoat` 0.6, `clearcoat_roughness` 0.2 | `roughness` 0.6, `normal_scale` 0.3, `clearcoat` 1.0, `clearcoat_roughness` 0.08 |

### Balls (no textures)

`albedo_color` white sRGB (0.93, 0.91, 0.86), red (0.62, 0.03, 0.03), black (0.02, 0.02, 0.02);
`roughness` 0.2; `clearcoat_enabled`, `clearcoat` 1.0, `clearcoat_roughness` 0.02. Put them on their
own visual layer (the preview uses layer 2) for the probe and the ball highlight below.

## Stage: environment, light and the canopy

TV snooker is a dark arena with a light over the table. The game keeps its reflections faint and soft
(KaiC found a crisp canopy rectangle on the balls unreal): the balls and rails show a soft glow from
the canopy, the balls the green cloth round their lower halves, and the dark arena elsewhere. This is
what `game/scripts/view/stage.gd` builds:

1. **WorldEnvironment**: `background_mode` Sky, a PanoramaSkyMaterial with `env/studio_small_04_1k.hdr`
   (a dark studio), `energy_multiplier` 0.35, Sky `radiance_size` 128; ambient source Sky, energy
   0.7; reflected light source Sky; tonemap Filmic, exposure 1.0. The studio lights the scene and is
   what the cue, the lamp and whatever is under the table reflect; the probe leaves it out.
2. **Each Camera3D** gets its own `environment`: a duplicate of the world's with `background_mode`
   Color, (0.02, 0.022, 0.025), the plain dark backdrop.
3. **Key light**: DirectionalLight3D straight down, `light_energy` 0.55, `light_angular_distance` 25
   (the canopy's size seen from the cloth: soft contact shadows), `light_specular` 0.32 (the faint
   highlight dot on the balls, as strong as it was at energy 0.7 and 0.25), shadows on, orthogonal,
   its range fitted to the camera's view of the table.
4. **Canopy**: an emissive plane, 1.0 m x 1.6 m, 1.0 m above the cloth, facing down, emission energy
   0.4 through a radial gradient that fades to black at its edges, on a visual layer only the probe
   sees (layer 20, left out of every camera's `cull_mask`). Above about 0.5 the probe clips it (the
   Mobile renderer stores probes with little range) and the glow turns into a flat shape with hard
   edges.
5. **ReflectionProbe** over the table: a box 0.2 m wider than the playing area on each side, from
   5 cm below the cloth to just above the canopy, `origin_offset` at ball-centre height, `update_mode`
   Once, `cull_mask` = the table's layer + the canopy's (not the balls: a ball in its own probe
   reflects itself as a dark blob), `reflection_mask` = the table's and the balls' layers (not the
   cue, which mirrored the cloth: green ebony, olive brass). Also:
   - `interior` on, so the capture leaves out the studio's bright panels, and project.godot's
     `rendering/environment/defaults/default_clear_color` set to the backdrop: an interior probe
     clears to it, and Godot's default grey made the rails milky.
   - `box_projection` off. On, it cut the canopy's glow into hard-edged shards on balls near the
     cushions.
   - `blend_distance` 0.02. The default 1 m is wider than the box is tall and fades the probe to
     almost nothing at ball height.
   - **Two captures**: one in the first frame drawn, and again once the renderer has gone a few
     frames without compiling a pipeline (Stage.SETTLED_FRAMES), since the first can miss meshes still
     compiling (in some runs the cloth came out black and the balls had black lower halves).
     Call `capture_reflections()` again after any material change.
   - With the probe, the game prints `7 RIDs of type "Texture" were leaked` when it quits (it
     doesn't without the probe), from the engine's reflection
     atlas teardown. It comes after the session log closes, so it never shows there.
6. **No ball highlight light.** An AreaLight3D the canopy's size lighting only the balls gave a
   crisp white rectangle; `materials_preview.gd` still has it (`ball_highlight`), the game doesn't.
7. **Floor**: a 16 m plane 0.8208 m below the cloth, where the bought table's feet are (the
   procedural table's frame comes down to it too), on the scenery layer (layer 4: the cameras see
   it, the probe neither captures nor lights it). Its own small shader: albedo sRGB (0.16, 0.15,
   0.14) under the key light, about sRGB (33, 30, 28) on screen, fading into the backdrop colour
   between 2.5 m and 7 m from the table's centre, so it has no edge and no horizon, with a grain
   under one step of the screen so the fade doesn't band. It draws the table's shadow itself, from
   the table's outline (`set_table_bounds`): 0.22 of its light under the table, the edge 0.36 m wide,
   the penumbra the canopy's 25 degrees gives at that depth. A shadow-map shadow there came out
   hard-edged (the Mobile renderer blurs by one fixed, narrow amount) and needed the shadow range
   stretched down to the floor.
8. **Lamp** (only with the bought model, `res://licensed/lamp.gltf`): on the scenery layer, no
   shadow, lighting nothing; the key light and canopy stand for it. It hangs from 0.79 m above the
   cloth and is drawn in full for a camera below 0.55 m, fades as the camera rises and is gone by
   0.70 m, so it is never between a camera and the table. Its materials are tuned after loading
   (see "The bought table").

The numbers in 1 and 3 were retuned for the bought table (before: ambient 0.6, key 0.7, key specular
0.25). The canopy and probe are as they were: three glows in the canopy's place, the shape of the
lamp's shades, made the balls' reflections blotchy and the cloth's sheen uneven, and with no canopy
the balls lose their soft glow and the cloth its sheen.

### What was tried for the canopy

| | Canopy on the balls | Cloth on the balls | Verdict |
| --- | --- | --- | --- |
| ReflectionProbe + emissive plane | yes (soft, with the gradient and low energy) | **yes** | use it |
| Canopy painted into the sky (sky shader, `stage.canopy = "sky"` in the preview) | yes | no: lower halves reflect the room | fallback if probes cost too much |
| AreaLight3D as the only light | yes, crisp | no | its shadows don't render in the Mobile renderer |
| HDRI on a dome only the probe sees | the canopy went missing from the capture | yes | dropped for step 2 |

### Phone vs these screenshots

The PC screenshots use the Mobile renderer but not the `.mobile` project-setting overrides, and two of
those matter here:

- `rendering/reflections/reflection_atlas/reflection_size.mobile` is 128 (PC 256): the probe is
  blurrier on the phone, which suits the soft glow and costs a quarter of the capture.
  `reflection_count` is 4 (Godot's 64 allocated a 268 MB cube array on the phone for one probe).
- `rendering/lights_and_shadows/directional_shadow/soft_shadow_filter_quality.mobile` is 2, as on the
  PC (Godot's mobile default, 0, made the angular-distance soft shadows grainy).

## The bought table

Studio-Lab's Snooker Table (`THIRD_PARTY.md`), fitted to the playing area by `tools/fit_table.py`
into the gitignored `game/licensed/`. `table_view.gd` shows `table.gltf` (Table, Racks, Chalk) and
`stage.gd` hangs `lamp.gltf`; both tune copies of the imported materials after loading. See it:

```
python scripts/background.py "D:\Tools\Godot\Godot_v4.7.1-stable_win64_console.exe" --path game --resolution 1434x660 res://scenes/dev/table_preview.tscn -- --mute --no-link --out=<abs folder> --views=aim,corner,middle,above
```

(`OFS_TABLE=procedural` in the environment shows the procedural table instead; the script's header
lists the views, and `OFS_TABLE_TUNE` tries other numbers without editing code. The first run after a
change to a shader can catch the reflections before they are captured: run it twice.)

| Material | As imported | What the game changes |
| --- | --- | --- |
| `SnookerTableShader`: cloth, wood and leather in one 2K set (colour, normal, roughness in green with metallic in blue) | double-sided; one specular for all three | Drawn by `TableView.TABLE_SHADER` with the same three textures, back faces culled. The cloth is told by its colour (green well above red and blue): `SPECULAR` 0.65 and a rim (0.25, tint 0.6) there, 0.3 on the wood and leather |
| `SnookerNetShader` | alpha blended, double-sided | alpha scissor at 0.12 (the cords are a texel or two wide: at 0.5 only the knots survive the smaller mipmaps), still double-sided |
| `SnookerPropsShader` (racks), `snookerTableProps` (chalk) | double-sided | back faces culled; the racks, under the table and in its shadow, cast none of their own |
| `PoolTableLightsShader` (lamp metal, bulbs) | fully metallic, emission 4 from its texture | `metallic` 0.3, so the brass takes the key light rather than mirroring the dark arena; emission energy 3 with (0.012, 0.011, 0.006) added, a faint glow that stands for the cloth lighting the lamp from below |
| `PoolTableLightsGlassShader` (shades) | the fit's opaque stand-in for glass, textured dark | no colour texture, `albedo_color` sRGB (0.86, 0.84, 0.78), `roughness` 0.35, emission (1.0, 0.93, 0.8) at 0.2: pale shades that glow a little |

Why back faces are culled: a double-sided surface comes out black in the reflection probe's capture,
so with the materials as imported every ball's lower half reflected black instead of cloth.

Why the table has its own shader: the model's cloth is one flat pure green, sRGB (1, 109, 10), and
its wood very dark. White light alone renders that cloth as sRGB (36, 135, 39), more neon than
baize; the neutral sheen that brings it to tournament green (specular 0.8 on everything) turned the
wood grey, (66, 65, 63). With the shader and the stage above:

| | Rendered sRGB |
| --- | --- |
| Cloth, from above and in the aim view | (47-52, 128-132, 49-54) |
| Wood, from above | (27, 24, 23), its red grain showing |
| Leathers, from above | (198, 191, 163) |
| Floor | (33, 30, 28) |

Our own baulk line, D and spots are drawn 0.3 mm above the model's bed (the fit paints the model's
out of its texture), as on the procedural table. The model's net is see-through: in the artist's
own pictures it reads cream because a bright studio shows through it, here the dark under the table
does and its cords read light on dark.

Cost on this PC (RTX 4070 laptop), 2868 x 1320, uncapped (`table_preview.tscn -- --time=<view>`):
the procedural table 1.22 ms a frame in the aim view and 1.38 ms from above, the model 1.50 ms in
both. Hiding the racks (34k of the model's 180k triangles) or stopping the table casting shadows
changes neither by more than run-to-run noise (0.02 ms).

## Import settings

Set in each `.import` file (Godot's VRAM compression for the phone is on in project.godot):

| Maps | Settings |
| --- | --- |
| all JPG | Compress Mode VRAM Compressed, High Quality on (BPTC on desktop, ASTC 4x4 on iOS), mipmaps on, Detect 3D off |
| `*_normal.jpg` | Normal Map: Enable |
| `*_roughness.jpg` | Channel Pack: Optimized; Roughness Mode: Red with Src Normal = the matching `*_normal.jpg` (Godot raises roughness in the mips where the normals vary, so the far cloth and wood don't sparkle) |
| `env/*.hdr` | VRAM Uncompressed, no mipmaps (a sky panorama: Godot builds its own radiance maps) |

The bought model's textures get the same settings from `.import` files that
`tools/licensed_imports.py` writes next to them (their folder is gitignored, so the files can't be
committed): colour maps as above, `*_N` as normal maps, the roughness maps with Roughness Mode Green
and Src Normal = their set's `*_N`.

Size: 6.9 MB in the repo. Each 1K map imports to 1.4 MB with mipmaps (8 bits a texel), so the 15 maps
add about 21 MB to an export, plus 2 MB for the HDRI.
