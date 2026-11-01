"""Draws the app icon: a glossy black ball on tournament green, 1024x1024 RGB, no alpha.

    python tools/make_icon.py            # writes game/icons/icon.png

Needs Pillow and numpy (`python -m pip install --user pillow numpy`). App Store icons
must not have an alpha channel, so the image is saved as plain RGB.
"""

from pathlib import Path

import numpy as np
from PIL import Image

SIZE = 1024
SUPERSAMPLE = 2
OUT = Path(__file__).resolve().parent.parent / "game" / "icons" / "icon.png"

CLOTH = np.array([0.012, 0.15, 0.045])
BALL_ALBEDO = np.array([0.004, 0.004, 0.005])
BALL_CENTRE = (0.5, 0.47)
BALL_RADIUS = 0.31


def normalize(v):
    return v / np.linalg.norm(v, axis=-1, keepdims=True)


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def render(n: int) -> np.ndarray:
    ys, xs = np.mgrid[0:n, 0:n].astype(np.float64)
    u = (xs + 0.5) / n
    v = (ys + 0.5) / n

    # Cloth lit by a lamp above and slightly behind the ball, falling off to the corners.
    glow = np.exp(-(((u - 0.5) / 0.55) ** 2 + ((v - 0.38) / 0.6) ** 2))
    rng = np.random.default_rng(147)
    weave = 1.0 + 0.025 * (rng.standard_normal((n, n)) * 0.6 + np.sin(v * n * 0.9) * 0.4)
    image = CLOTH[None, None, :] * (0.45 + 0.75 * glow)[..., None] * weave[..., None]

    cx, cy = BALL_CENTRE
    r = BALL_RADIUS
    shadow_d = ((u - cx - 0.02) / (r * 1.05)) ** 2 + ((v - cy - r * 0.78) / (r * 0.42)) ** 2
    image *= (1.0 - 0.85 * np.exp(-shadow_d * 1.6))[..., None]

    dx = (u - cx) / r
    dy = (v - cy) / r
    d2 = dx * dx + dy * dy
    inside = d2 < 1.0
    nz = np.sqrt(np.clip(1.0 - d2, 0.0, 1.0))
    normal = np.stack([dx, dy, nz], axis=-1)
    view = np.array([0.0, 0.0, 1.0])
    ndv = np.clip(normal[..., 2], 0.0, 1.0)
    reflected = 2.0 * ndv[..., None] * normal - view

    # Environment seen in the clear coat: the green cloth below the horizon, darkness above
    # it, and the long canopy lamp over a snooker table.
    below = smoothstep(-0.25, 0.7, reflected[..., 1])
    env = CLOTH[None, None, :] * 1.4 * below[..., None]
    lamp = np.exp(-(((reflected[..., 0] + 0.32) / 0.26) ** 2 + ((reflected[..., 1] + 0.56) / 0.14) ** 2) ** 2)
    lamp *= smoothstep(0.0, 0.2, reflected[..., 2])
    env += np.array([1.0, 0.98, 0.94])[None, None, :] * (lamp * 1.8)[..., None]

    fresnel = 0.05 + 0.95 * (1.0 - ndv) ** 5
    light = normalize(np.array([-0.35, -0.7, 0.62]))
    diffuse = np.clip((normal * light).sum(-1), 0.0, 1.0)
    half = normalize(light + view)
    spec = np.clip((normal * half).sum(-1), 0.0, 1.0) ** 600 * 2.5

    ball = BALL_ALBEDO[None, None, :] * (0.2 + diffuse)[..., None]
    ball += fresnel[..., None] * env + spec[..., None]
    # A soft edge band keeps the silhouette anti-aliased before the downsample.
    edge = 1.0 - smoothstep(0.985, 1.0, np.sqrt(d2))
    image = np.where(inside[..., None], ball * edge[..., None] + image * (1.0 - edge[..., None]), image)

    corner = np.sqrt((u - 0.5) ** 2 + (v - 0.5) ** 2)
    image *= (1.0 - 0.35 * smoothstep(0.35, 0.75, corner))[..., None]
    return image


def main() -> None:
    linear = np.clip(render(SIZE * SUPERSAMPLE), 0.0, None)
    # Simple filmic-style shoulder, then sRGB-ish gamma.
    mapped = linear / (1.0 + linear * 0.35) * 1.35
    srgb = np.clip(mapped, 0.0, 1.0) ** (1.0 / 2.2)
    big = Image.fromarray((srgb * 255.0 + 0.5).astype(np.uint8), "RGB")
    icon = big.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    icon.save(OUT, "PNG", optimize=True)
    print(f"wrote {OUT} {icon.size} {icon.mode}")


if __name__ == "__main__":
    main()
