# Table textures and lighting

Materials for the table, cue and stage, all CC0 (sources and authors in `THIRD_PARTY.md`). The
settings below are the ones `scenes/dev/materials_preview.tscn` renders with; build the game's
materials from them.

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
| StandardMaterial3D | `albedo_color` sRGB (0.11, 0.42, 0.16); `roughness` 0.85 (about 0.7 with the map); `normal_scale` 0.3; `rim_enabled`, `rim` 0.25, `rim_tint` 0.6; no clear-coat |

Under the stage below the cloth renders at about sRGB (42-66, 120-129, 52-73), a TV tournament green. The soft
sheen near the camera comes from the roughness picking up the canopy in the reflection probe; rim adds
very little. Anisotropy was tried for a sheen along the nap and looked wrong (dark or washed out), so it
is off. The cushions use the same material.

### Rails (`rail/`)

| | |
| --- | --- |
| Files | `rail_albedo.jpg`, `rail_normal.jpg`, `rail_roughness.jpg` |
| Source | Poly Haven Dark Wood (mahogany), its albedo contrast softened to 55 % so the figure reads as one polished board rather than rosewood |
| Tile | **2.0 m** (the scan's real size); grain along U |
| StandardMaterial3D | `roughness` 0.6 (about 0.38 with the map); `normal_scale` 0.3; `clearcoat_enabled`, `clearcoat` 1.0, `clearcoat_roughness` 0.08 |

A tinted Sapele veneer was tried and looked like painted brown; the figure in Dark Wood is what makes it
read as wood.

### Pocket leathers (`leather/`)

| | |
| --- | --- |
| Files | `leather_albedo.jpg`, `leather_normal.jpg`, `leather_roughness.jpg` |
| Source | ambientCG Leather 030, albedo darkened to 45 % (dark brown, nearly black under the canopy) |
| Tile | **0.45 m** (the source's "ca. 45 cm") |
| StandardMaterial3D | `roughness` 1.0 (the map averages 0.48, so it has a soft sheen); `normal_scale` 1.0 |

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

TV snooker is a dark arena with a bright rectangular canopy over the table. The balls show three
things: the canopy as a crisp white rectangle near the top, the green cloth around their lower half,
and a dim room elsewhere. This is the setup that gets all three in the Mobile renderer:

1. **WorldEnvironment**: `background_mode` Sky, a PanoramaSkyMaterial with `env/studio_small_04_1k.hdr`
   (a dark studio), `energy_multiplier` 0.35, Sky `radiance_size` 256; ambient source Sky, energy
   0.6; reflected light source Sky; tonemap Filmic, exposure 1.0.
2. **Each Camera3D** gets its own `environment`: a duplicate of the world's with `background_mode`
   Color, (0.02, 0.022, 0.025). The camera sees the plain dark backdrop; the reflection probe renders
   with the world's environment and so captures the dim room.
3. **Key light**: DirectionalLight3D straight down, `light_energy` 0.7, `light_angular_distance` 25
   (the canopy's size seen from the cloth: soft contact shadows), `light_specular` 0 (the highlights
   come from the canopy), shadows on, orthogonal, `directional_shadow_max_distance` 6.
4. **Canopy**: an emissive plane, 1.0 m x 2.4 m, 1.0 m above the cloth, facing down, emission energy
   6, on a visual layer only the probe sees (the preview uses layer 20 and leaves it out of every
   camera's `cull_mask`).
5. **ReflectionProbe** over the table: a box 0.2 m wider than the playing area on each side, from
   5 cm below the cloth to just above the canopy, `origin_offset` at ball-centre height,
   `box_projection` on, `update_mode` Once, `cull_mask` = the table's layer + the canopy's (not the
   balls: a ball in its own probe reflects itself as a dark blob). Two things are needed for it to work:
   - `blend_distance` 0.02. The default 1 m is wider than the box is tall and fades the probe to
     almost nothing at ball height.
   - **Capture again** about 0.5 s after the table is built (set `update_mode` to Once again), and after
     any material change. The first capture can miss meshes whose shaders are still compiling: in some
     runs the cloth came out black in it and the balls had black lower halves.
   - With the probe, the preview prints `7 RIDs of type "Texture" were leaked` when it quits (it
     doesn't without the probe). It comes after the session log closes, so it never shows there.
6. **Ball highlight**: an AreaLight3D the canopy's size at the canopy, facing down, `light_cull_mask` =
   the balls' layer only, `area_normalize_energy` off, `light_energy` 0.05, `light_specular` 100, no
   shadow. The probe alone leaves the canopy grey on a black ball (the Mobile renderer stores probes with
   too little range for a 4 % reflection of a lamp to reach white, even at emission 25); the area
   light's specular is analytic and gives the crisp white rectangle. Its diffuse is negligible.

### What was tried for the canopy

| | Canopy on the balls | Cloth on the balls | Verdict |
| --- | --- | --- | --- |
| ReflectionProbe + emissive plane | yes (grey on black, hence step 6) | **yes** | use it |
| Canopy painted into the sky (sky shader, `stage.canopy = "sky"` in the preview) | yes | no: lower halves reflect the room | fallback if probes cost too much |
| AreaLight3D as the only light | yes, crisp | no | its shadows don't render in the Mobile renderer |
| HDRI on a dome only the probe sees | the canopy went missing from the capture | yes | dropped for step 2 |

### Phone vs these screenshots

The PC screenshots use the Mobile renderer but not the `.mobile` project-setting overrides, and two of
those matter here (project.godot, not changed by this work):

- `rendering/reflections/reflection_atlas/reflection_size.mobile` is 128 (PC 256): the probe is
  blurrier on the phone. The canopy rectangle comes from the area light, so mostly the cloth's
  reflection softens. 256 is worth trying.
- `rendering/lights_and_shadows/directional_shadow/soft_shadow_filter_quality.mobile` is 0, Hard (PC
  2): the angular-distance soft shadows still work but are grainier. Check on the phone; 1 or 2 if it
  shows.

## Import settings

Set in each `.import` file (Godot's VRAM compression for the phone is on in project.godot):

| Maps | Settings |
| --- | --- |
| all JPG | Compress Mode VRAM Compressed, High Quality on (BPTC on desktop, ASTC 4x4 on iOS), mipmaps on, Detect 3D off |
| `*_normal.jpg` | Normal Map: Enable |
| `*_roughness.jpg` | Channel Pack: Optimized; Roughness Mode: Red with Src Normal = the matching `*_normal.jpg` (Godot raises roughness in the mips where the normals vary, so the far cloth and wood don't sparkle) |
| `env/*.hdr` | VRAM Uncompressed, no mipmaps (a sky panorama: Godot builds its own radiance maps) |

Size: 6.9 MB in the repo. Each 1K map imports to 1.4 MB with mipmaps (8 bits a texel), so the 15 maps
add about 21 MB to an export, plus 2 MB for the HDRI.
