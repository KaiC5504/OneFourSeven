"""Godot import settings for the fitted table model's textures (game/licensed/).

That folder is never in git, so its .import files can't be committed the way game/textures/'s
are, and Godot's defaults for a new image are wrong for a phone: lossless and without mipmaps
(about 130 MB of GPU memory against about 30 MB, and a cloth that shimmers at a distance).
tools/fit_table.py writes these stubs beside the textures it exports and scripts/mac.py sends
the same ones to the Mac; Godot fills in the rest of each file on its next import.

The settings are the ones game/textures/README.md gives the game's own maps: VRAM compressed,
high quality, mipmaps, no 3D detection; normal maps flagged; a roughness map limited by its
set's normal map (glTF packs roughness in green).
"""

from __future__ import annotations

from pathlib import Path

TEXTURES = (".jpg", ".jpeg", ".png")

TEMPLATE = """[remap]

importer="texture"
type="CompressedTexture2D"

[params]

compress/mode=2
compress/high_quality=true
compress/lossy_quality=0.7
compress/uastc_level=0
compress/rdo_quality_loss=0.0
compress/hdr_compression=1
compress/normal_map={normal}
compress/channel_pack=0
mipmaps/generate=true
mipmaps/limit=-1
roughness/mode={roughness}
roughness/src_normal="{src_normal}"
process/channel_remap/red=0
process/channel_remap/green=1
process/channel_remap/blue=2
process/channel_remap/alpha=3
process/fix_alpha_border=true
process/premult_alpha=false
process/normal_map_invert_y=false
process/hdr_as_srgb=false
process/hdr_clamp_exposure=false
process/size_limit=0
detect_3d/compress_to=0
"""


def stub(name: str, names: set[str]) -> str:
    """The .import text for texture `name`, one of the folder's file `names`."""
    stem = name.rsplit(".", 1)[0]
    normal, roughness, src = 2, 1, ""
    if stem.endswith("_N"):
        normal = 1
    elif stem.endswith("_R"):
        # "SnookerTable_M-SnookerTable_R" and "SnookerNet_R" both belong to the set before _M or _R.
        base = stem.split("_M-")[0] if "_M-" in stem else stem[:-2]
        if base + "_N.jpg" in names:
            roughness, src = 3, f"res://licensed/{base}_N.jpg"
    return TEMPLATE.format(normal=normal, roughness=roughness, src_normal=src)


def stubs(folder: Path) -> dict[str, str]:
    """File name -> .import text for every texture in the folder."""
    names = {f.name for f in folder.iterdir() if f.suffix.lower() in TEXTURES}
    return {name + ".import": stub(name, names) for name in sorted(names)}


def write(folder: Path) -> int:
    made = stubs(folder)
    for name, text in made.items():
        (folder / name).write_text(text, encoding="utf-8", newline="\n")
    # Godot doesn't import a glTF scene again when only its images changed; without its .import
    # file it must.
    for scene in folder.glob("*.gltf.import"):
        scene.unlink()
    return len(made)
