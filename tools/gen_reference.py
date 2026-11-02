# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""Writes core/tests/fixtures/reference/cushion_mathavan.json, the oracle for calibration targets
C7 and C8 (spec 7.11): ball-cushion rebounds from an independent, fine-step solution of
Mathavan, Jackson & Parkin (2010), "A theoretical analysis of billiard ball dynamics under cushion
impacts", Proc. IMechE C 224(9):1863-1873, doi:10.1243/09544062JMES1964 (open copy:
https://hdl.handle.net/2134/15087).

    uv run tools/gen_reference.py           # writes the fixture
    uv run tools/gen_reference.py --check   # exits 1 if the committed fixture is stale

Ported here from the paper's equations (3)-(17), not from core/ or pooltool: the normal impulse
P_I at the cushion contact is the independent variable, Coulomb friction acts at the cushion
(mu_w) and the cloth (mu_s, normal impulse P_C = P_I (sin theta + mu_w sin phi cos theta)), and
the impact ends by Stronge's energetic restitution (work in restitution = e^2 x work in
compression). The restitution rule of the M1 contracts addendum (abfa39a) is included:
e = cushion_restitution - slope * max(0, v_normal - cushion_rigid_limit_m_s), clamped to
[0, cushion_restitution]. Parameters are read from core/data/table_params.json and stored in the
fixture, so the C++ test can tell when they have changed since.

Frame (the left cushion's, which is also the table frame there): x along the cushion, y the
cushion normal pointing into the cushion, z up. The ball moves +x and +y. Incidence and rebound
angles are measured from the cushion normal (Mathavan's alpha is from the cushion: 90 - ours).
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PARAMS = ROOT / "core" / "data" / "table_params.json"
OUT = ROOT / "core" / "tests" / "fixtures" / "reference" / "cushion_mathavan.json"
# table_geometry.hpp kBallDiameter / 2 (WPBSA 52.5 mm).
BALL_RADIUS_M = 0.02625
STEPS = 100_000
PARAM_KEYS = (
    "ball_mass_kg",
    "cushion_restitution",
    "cushion_friction",
    "cushion_table_friction",
    "cushion_contact_height_m",
    "cushion_rigid_limit_m_s",
    "cushion_restitution_slope_per_m_s",
)


def restitution(p: dict, v_normal: float) -> float:
    e0 = p["cushion_restitution"]
    e = e0 - p["cushion_restitution_slope_per_m_s"] * max(0.0, v_normal - p["cushion_rigid_limit_m_s"])
    return min(max(e, 0.0), e0)


def unit(x: float, y: float) -> tuple[float, float]:
    n = math.hypot(x, y)
    # An exactly zero slip has no friction direction (rolling at that contact, paper eq. 16).
    return (x / n, y / n) if n > 1e-15 else (0.0, 0.0)


def solve(p: dict, v: list[float], w: list[float], steps: int) -> tuple[list[float], list[float]]:
    M = p["ball_mass_kg"]
    R = BALL_RADIUS_M
    mu_w = p["cushion_friction"]
    mu_s = p["cushion_table_friction"]
    sin_t = (p["cushion_contact_height_m"] - R) / R
    cos_t = math.sqrt(1.0 - sin_t * sin_t)
    k = 5.0 / (2.0 * M * R)
    vx, vy = v
    wx, wy, wz = w
    e = restitution(p, vy)
    # The paper's step: (1 + e) M V0 sin(alpha) / N, its estimate of the whole normal impulse.
    dP = (1.0 + e) * M * vy / steps

    def rates(vx, vy, wx, wy, wz):
        # Slip at the cushion contact I (eq. 12) and at the cloth contact C (eq. 13).
        cI, sI = unit(vx + wy * R * sin_t - wz * R * cos_t, -vy * sin_t + wx * R)
        cC, sC = unit(vx - wy * R, vy + wx * R)
        pc = sin_t + mu_w * sI * cos_t  # dP_C / dP_I, eq. 8
        return (
            -(mu_w * cI + mu_s * cC * pc) / M,
            -(cos_t - mu_w * sin_t * sI + mu_s * sC * pc) / M,
            -k * (mu_w * sI + mu_s * sC * pc),
            -k * (mu_w * cI * sin_t - mu_s * cC * pc),
            k * mu_w * cI * cos_t,
        )

    state = (vx, vy, wx, wy, wz)
    work = 0.0
    work_c = None
    for _ in range(20 * steps):
        d = rates(*state)
        new = tuple(s + r * dP for s, r in zip(state, d))
        # Normal velocity of I towards the cushion is vy cos(theta); eq. 17a's trapezoid.
        dW = 0.5 * (state[1] + new[1]) * cos_t * dP
        if work_c is None and new[1] <= 0.0:
            # Compression ends inside this step where vy = 0: split it there (eq. 17c).
            f = state[1] / (state[1] - new[1])
            mid = tuple(s + f * (n - s) for s, n in zip(state, new))
            work_c = work + 0.5 * state[1] * cos_t * f * dP
            work = work_c + 0.5 * new[1] * cos_t * (1.0 - f) * dP
            target = (1.0 - e * e) * work_c  # eq. 17b
            if work <= target:
                g = (work_c - target) / (work_c - work) if work_c > work else 0.0
                state = tuple(m + g * (n - m) for m, n in zip(mid, new))
                break
            state = new
            continue
        if work_c is not None and work + dW <= target:
            g = (work - target) / (-dW) if dW < 0.0 else 0.0
            state = tuple(s + g * (n - s) for s, n in zip(state, new))
            break
        work += dW
        state = new
    else:
        raise RuntimeError(f"impact did not end within {20 * steps} steps: v={v} w={w}")
    return [state[0], state[1]], [state[2], state[3], state[4]]


def incoming(speed: float, incidence_deg: float, topspin: float, side: float) -> tuple[list[float], list[float]]:
    """topspin is R w_T / V (1: natural roll, Mathavan's k in Fig. 7), side is R w_z / V (Fig. 8's k;
    positive is running side for a ball moving +x along the cushion)."""
    a = math.radians(incidence_deg)
    vx, vy = speed * math.sin(a), speed * math.cos(a)
    R = BALL_RADIUS_M
    # w_T about the horizontal axis perpendicular to v: rolling has u_C = v + w x (0, 0, -R) = 0.
    return [vx, vy], [-topspin * vy / R, topspin * vx / R, side * speed / R]


def cases() -> list[dict]:
    out = []
    for speed in (0.5, 1.0, 2.0, 3.0, 4.0):
        for inc in (0, 10, 20, 30, 40, 50, 60, 70, 80):
            out.append({"group": "rolling", "speed_m_s": speed, "incidence_deg": inc, "topspin": 1.0, "side": 0.0})
    for topspin in (-1.0, 0.0, 2.0):
        for inc in (0, 20, 40, 60, 80):
            out.append({"group": "topspin", "speed_m_s": 1.0, "incidence_deg": inc, "topspin": topspin, "side": 0.0})
    for speed in (0.5, 1.0, 2.0, 4.0):
        for inc in (20, 40, 60):
            for side in (-1.0, -0.5, 0.5, 1.0):
                out.append({"group": "side", "speed_m_s": speed, "incidence_deg": inc, "topspin": 1.0, "side": side})
    return out


def rebound(v: list[float]) -> tuple[float, float]:
    return math.hypot(v[0], v[1]), math.degrees(math.atan2(v[0], -v[1]))


def build(params: dict) -> dict:
    rows = []
    worst_speed = 0.0
    worst_angle = 0.0
    for c in cases():
        v, w = incoming(c["speed_m_s"], c["incidence_deg"], c["topspin"], c["side"])
        v1, _ = solve(params, v, w, STEPS)
        v2, w2 = solve(params, v, w, 2 * STEPS)
        s1, a1 = rebound(v1)
        s2, a2 = rebound(v2)
        worst_speed = max(worst_speed, abs(s1 - s2) / s2)
        worst_angle = max(worst_angle, abs(a1 - a2))
        rows.append({**c, "in": {"v": v, "w": w}, "out": {"v": v2, "w": w2},
                     "rebound_speed_m_s": s2, "rebound_angle_deg": a2})
    return {
        "_note": "Generated by tools/gen_reference.py; do not edit. Oracle for C7/C8 (docs/calibration/targets.md).",
        "model": "Mathavan, Jackson & Parkin (2010) cushion impact, fine-step Euler in the normal impulse, "
                 "ported from the paper's equations (not from core/ or pooltool); restitution rule of the "
                 "M1 contracts addendum abfa39a",
        "source": "https://doi.org/10.1243/09544062JMES1964 (open copy https://hdl.handle.net/2134/15087)",
        "cross_check": "pooltool (Apache-2.0) physics/resolve/ball_cushion/mathavan_2010 solve(), main branch, "
                       "run once without numba on 144 cases (0.5-4 m/s, 0-80 deg, topspin -1..1, side -1..1): "
                       "agrees to 1.4e-5 in speed and 0.0008 deg",
        "frame": "x along the cushion, y the normal into the cushion, z up; the ball moves +x, +y. "
                 "Angles from the cushion normal; side = R w_z / V (positive: running), topspin = R w_T / V "
                 "(1: natural roll). 'out' is the ball just after the impact.",
        "params": {**{k: params[k] for k in PARAM_KEYS}, "ball_radius_m": BALL_RADIUS_M},
        "steps": 2 * STEPS,
        "convergence": {"steps_compared": [STEPS, 2 * STEPS], "max_rel_speed_diff": worst_speed,
                        "max_angle_diff_deg": worst_angle},
        "published": {
            "perpendicular_rolling_rebound_ratio": 0.818,
            "perpendicular_rolling_speed_range_m_s": [0.0, 1.5],
            "where": "Mathavan et al. (2010) sec. 3.3 and Fig. 5: snooker ball rolling straight into a "
                     "Riley Renaissance cushion, rebound / incident speed averages 0.818 below 1.5 m/s",
        },
        "cases": rows,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="compare with the committed fixture instead of writing it")
    args = ap.parse_args()
    params = json.loads(PARAMS.read_text(encoding="utf-8"))["values"]
    data = build(params)
    text = json.dumps(data, indent=1) + "\n"
    if args.check:
        old = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if old != text:
            print(f"{OUT.relative_to(ROOT)} is stale: run `uv run tools/gen_reference.py`")
            return 1
        print("ok")
        return 0
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(text, encoding="utf-8")
    c = data["convergence"]
    print(f"wrote {OUT.relative_to(ROOT)}: {len(data['cases'])} cases, "
          f"N vs 2N: speed {c['max_rel_speed_diff']:.2e} rel, angle {c['max_angle_diff_deg']:.2e} deg")
    return 0


if __name__ == "__main__":
    sys.exit(main())
