# /// script
# dependencies = ["numpy", "scipy"]
# ///
"""Fit a filmed shot: the cue speed and tip offset that best reproduce its measured cushions and cannon.

The measurements are docs/calibration/films/shot_<n>.json (docs/calibration/targets.md, the filmed
shots). Needs the Release ofs_sim_cli (build/windows-physics). Working files go to build/film-fit.

usage: uv run tools/film/fitshot.py docs/calibration/films/shot_7.json [--params=FILE] [--max=6.5]
           [--tip-x=lo,hi] [--tip-y=lo,hi]
"""
import json, math, subprocess, sys
from pathlib import Path

import numpy as np
from scipy.optimize import minimize

REPO = Path(__file__).resolve().parents[2]
FILMS = REPO / "docs/calibration/films"
HERE = REPO / "build/film-fit"
HERE.mkdir(parents=True, exist_ok=True)
CLI = REPO / "build/windows-physics/tools/sim_cli/Release/ofs_sim_cli.exe"
R = 0.02625
HALF_L, HALF_W = 1.7845, 0.889
CUSHIONS = {"right": (1, -(HALF_W - R)), "left": (1, HALF_W - R), "baulk": (0, -(HALF_L - R)), "top": (0, HALF_L - R)}


class Shot:
    def __init__(self, path, tag="s"):
        self.d = json.loads(Path(path).read_text(encoding="utf-8"))
        self.tag = tag
        balls = self.d["balls"]
        cue = next(b for b in balls if b["ball"] == "cue")
        rest = [b for b in balls if b["ball"] != "cue"]
        self.balls = [cue] + rest
        counts = {}
        self.start = []
        for b in self.balls:
            i = counts.get(b["ball"], 0)
            counts[b["ball"]] = i + 1
            self.start.append({"ball": b["ball"], "index": i, "r": [float(b["r"][0]), float(b["r"][1])]})
        self.cue = self.start[0]["r"]
        ob = self.d["object_ball"]
        self.object = self.index_of(ob["ball"], ob.get("r"))
        self.object_r = self.start[self.object]["r"]
        self.potted = bool(ob.get("potted"))
        self.pocket = ob.get("pocket")
        ev = self.d["events"]
        balls_hit = [e for e in ev if e["kind"] == "ball"]
        t_cannon = balls_hit[-1]["t"] if len(balls_hit) > 1 else 1e9

        def turns(e):
            a, b = e.get("heading_in_deg"), e.get("heading_out_deg")
            return a is None or b is None or abs((b - a + 180.0) % 360.0 - 180.0) >= 5.0

        # cushions before the cannon; a ball running along a cushion and touching it again is not one
        self.cushions = [(e["with"], e["r"], e["t"]) for e in ev
                         if e["kind"] == "cushion" and e["t"] < t_cannon and turns(e)]
        self.t_first = balls_hit[0]["t"] if balls_hit else 0.0
        self.cannon = balls_hit[-1] if len(balls_hit) > 1 else None
        self.cannon_i = self.index_of(self.cannon["with"], None, near=self.cannon["r"]) if self.cannon else None
        self.allowed = {self.index_of(e["with"], None, near=e["r"]) for e in balls_hit[1:]}
        # balls lying against the cannon ball count as reaching it
        self.pack = set()
        if self.cannon_i is not None:
            c = self.start[self.cannon_i]["r"]
            self.pack = {i for i, b in enumerate(self.start)
                         if i > 0 and math.hypot(b["r"][0] - c[0], b["r"][1] - c[1]) < 0.12}
        self.object_heading = ob.get("heading_out_deg")

    def index_of(self, name, r=None, near=None):
        name = name.split()[0].lower()
        best, best_d = None, 1e9
        for i, b in enumerate(self.start):
            if b["ball"] != name or i == 0:
                continue
            ref = r or near
            d = math.hypot(b["r"][0] - ref[0], b["r"][1] - ref[1]) if ref else 0.0
            if d < best_d:
                best, best_d = i, d
        return best

    def sim(self, aim, tip_x, tip_y, speed, params=None):
        pos = {"cue_ball": 0, "start": {"balls": self.start},
               "strike": {"aim_rad": aim, "tip_x": tip_x, "tip_y": tip_y, "elevation_rad": 0.0, "speed_m_s": speed}}
        pf, rf = HERE / f"pos_{self.tag}.json", HERE / f"rec_{self.tag}.json"
        pf.write_text(json.dumps(pos))
        cmd = [str(CLI), "simulate", str(pf), "--out", str(rf)]
        if params:
            cmd += ["--params", str(params)]
        out = subprocess.run(cmd, capture_output=True, text=True)
        if out.returncode != 0:
            return None
        return json.loads(rf.read_text())

    def object_heading_of(self, rec):
        seg = next((s for s in rec["segments"][self.object] if s["start"]["motion"] != "stationary"), None)
        if seg is None:
            return None
        v = seg["start"]["v"]
        return math.degrees(math.atan2(v[1], v[0]))

    def shoot(self, params, speed, tip_x, tip_y):
        """Aim so the object ball leaves on its measured line (to the pocket when potted)."""
        aim_heading = self.d["object_ball"].get("aim_heading_deg")
        if self.potted and self.pocket and aim_heading is not None and math.hypot(
                self.pocket[0] - self.object_r[0], self.pocket[1] - self.object_r[1]) < 0.15:
            # a ball hanging in the jaws drops however it is hit: play the filmed line
            rec = self.sim(math.radians(aim_heading), tip_x, tip_y, speed, params)
            if rec is not None:
                rec["aim"] = math.radians(aim_heading)
            return rec
        if self.potted and self.pocket:
            want = math.degrees(math.atan2(self.pocket[1] - self.object_r[1], self.pocket[0] - self.object_r[0]))
        else:
            want = self.object_heading
        # ghost-ball aim for that departure
        gx = self.object_r[0] - 2 * R * math.cos(math.radians(want))
        gy = self.object_r[1] - 2 * R * math.sin(math.radians(want))
        a1 = math.atan2(gy - self.cue[1], gx - self.cue[0])
        a0 = a1 + math.radians(0.4)

        def err(rec):
            h = self.object_heading_of(rec) if rec else None
            return None if h is None else (h - want + 180.0) % 360.0 - 180.0

        rec = self.sim(a1, tip_x, tip_y, speed, params)
        f1 = err(rec)
        if f1 is None:
            return None
        f0 = err(self.sim(a0, tip_x, tip_y, speed, params))
        for _ in range(8):
            if abs(f1) < 0.03 or f0 is None or f1 == f0:
                break
            a0, a1, f0 = a1, a1 - f1 * (a1 - a0) / (f1 - f0), f1
            rec = self.sim(a1, tip_x, tip_y, speed, params)
            f1 = err(rec)
            if f1 is None:
                return None
        rec["aim"] = a1
        return rec

    def measure(self, rec):
        segs = rec["segments"][0]
        ev = [e for e in rec["events"] if 0 in (e.get("a"), e.get("b"))]
        hit = next((e for e in ev if e["kind"] == "ball_ball"), None)
        if hit is None:
            return None
        first = hit["b"] if hit["a"] == 0 else hit["a"]
        out = {"t0": hit["t"], "first": first, "cushions": [], "balls": [], "cue_potted": False}
        for e in ev:
            if e["t"] <= hit["t"] + 1e-9:
                continue
            if e["kind"] in ("ball_line", "ball_arc"):
                after = next(s for s in segs if abs(s["t0"] - e["t"]) < 1e-9)
                roll = next((s for s in segs if s["t0"] > e["t"] + 1e-9 and s["start"]["motion"] == "rolling"), after)
                out["cushions"].append({"t": e["t"] - hit["t"], "r": after["start"]["r"], "kind": e["kind"],
                                        "v_roll": roll["start"]["v"], "roll_r": roll["start"]["r"],
                                        "roll_t": roll["t0"] - hit["t"]})
            elif e["kind"] == "ball_ball":
                other = e["b"] if e["a"] == 0 else e["a"]
                at = next(s for s in segs if abs(s["t0"] - e["t"]) < 1e-9)
                out["balls"].append({"t": e["t"] - hit["t"], "ball": other, "r": at["start"]["r"]})
            elif e["kind"] == "pocket" and e.get("a") == 0:
                out["cue_potted"] = True
        out["end"] = rec["final"]["balls"][0]["r"]
        out["object_potted"] = rec["final"]["balls"][self.object]["pocket"] >= 0
        return out

    def score(self, rec, roll_friction):
        if rec is None:
            return 1e3, None
        m = self.measure(rec)
        if m is None or m["first"] != self.object:
            return 1e3, None
        info = {"m": m}
        terms = []
        if self.potted and not m["object_potted"]:
            terms.append(3.0)
        if m["cue_potted"]:
            terms.append(6.0)
        t_shift = self.t_first
        k_cush = 0
        for k, (name, r, t) in enumerate(self.cushions):
            axis, val = CUSHIONS[name]
            if len(m["cushions"]) <= k:
                return 60.0 * (len(self.cushions) - k) + sum(x * x for x in terms), info
            c = m["cushions"][k]
            if abs(c["r"][axis] - val) > 0.03:
                return 60.0 * (len(self.cushions) - k) + 5.0 * abs(c["r"][axis] - val) + sum(x * x for x in terms), info
            # a ball contact before this cushion that the film does not have
            early = [b for b in m["balls"] if b["t"] < c["t"] and b["ball"] not in self.allowed]
            if early:
                return 60.0 * (len(self.cushions) - k) + sum(x * x for x in terms), info
            terms.append((c["r"][1 - axis] - r[1 - axis]) / 0.05)
            terms.append((c["t"] - (t - t_shift)) / 0.15)
            k_cush = k + 1
        if self.cannon:
            want_r, want_t = self.cannon["r"], self.cannon["t"] - t_shift
            last_t = m["cushions"][k_cush - 1]["t"] if k_cush else 0.0
            later = [b for b in m["balls"] if b["t"] > last_t and b["ball"] not in self.allowed - self.pack]
            hit = next((b for b in later if b["ball"] in self.pack), None)
            wrong = next((b for b in later if b["ball"] not in self.pack), None)
            if hit and (wrong is None or wrong["t"] > hit["t"]):
                terms.append(math.hypot(hit["r"][0] - want_r[0], hit["r"][1] - want_r[1]) / 0.05)
                terms.append((hit["t"] - want_t) / 0.4)
                info["cannon"] = "hit at %.2f s" % hit["t"]
            else:
                # how far the cue ball's line passes from the filmed contact, and whether it gets there
                if k_cush:
                    c = m["cushions"][k_cush - 1]
                    r0, v = c["roll_r"], c["v_roll"]
                else:
                    roll = next((s for s in rec["segments"][0] if s["t0"] > m["t0"] and s["start"]["motion"] == "rolling"), None)
                    if roll is None:
                        return 40.0 + sum(x * x for x in terms), info
                    r0, v = roll["start"]["r"], roll["start"]["v"]
                sp = math.hypot(v[0], v[1])
                if sp < 1e-6:
                    return 40.0 + sum(x * x for x in terms), info
                ux, uy = v[0] / sp, v[1] / sp
                dx, dy = want_r[0] - r0[0], want_r[1] - r0[1]
                along, across = dx * ux + dy * uy, -dx * uy + dy * ux
                reach = sp * sp / (2 * roll_friction * 9.81)
                terms.append(2.0 + abs(across) / 0.05)
                if along < 0:
                    terms.append(5.0)
                elif reach < along:
                    terms.append((along - reach) / 0.15)
                info["cannon"] = "missed by %.3f m across, reach %.2f of %.2f m%s" % (
                    across, reach, along, ", hit ball %d first" % wrong["ball"] if wrong else "")
        info["terms"] = [round(t, 2) for t in terms]
        return float(sum(x * x for x in terms)), info

    def fit(self, params, roll_friction, max_speed, tip_x_range, tip_y_range, speeds=None):
        def f(x):
            speed, tx, ty = x
            if not (0.3 <= speed <= max_speed) or math.hypot(tx, ty) > 0.53:
                return 1e4
            if not (tip_x_range[0] <= tx <= tip_x_range[1]) or not (tip_y_range[0] <= ty <= tip_y_range[1]):
                return 1e4
            return self.score(self.shoot(params, speed, tx, ty), roll_friction)[0]

        grid = []
        speeds = speeds or [1.5, 2.2, 3.0, 4.0, 5.0, 6.0]
        for sp in speeds:
            if sp > max_speed:
                continue
            for tx in np.linspace(tip_x_range[0], tip_x_range[1], 5):
                for ty in np.linspace(tip_y_range[0], tip_y_range[1], 5):
                    grid.append((f((sp, tx, ty)), (sp, float(tx), float(ty))))
        grid.sort()
        best = (1e9, None)
        for _, x0 in grid[:3]:
            res = minimize(f, x0, method="Nelder-Mead",
                           options={"xatol": 0.004, "fatol": 0.02, "maxfev": 140,
                                    "initial_simplex": [x0, (x0[0] * 0.9, x0[1], x0[2]), (x0[0], x0[1] + 0.05, x0[2]),
                                                        (x0[0], x0[1], x0[2] + 0.05)]})
            if res.fun < best[0]:
                best = (res.fun, res.x)
        rec = self.shoot(params, *best[1])
        s, info = self.score(rec, roll_friction)
        return s, best[1], info, rec

    def line(self, m):
        txt = ""
        for c in m["cushions"][:4]:
            v = c["v_roll"]
            txt += " | %s (%.2f,%.2f) t%.2f out %.2f m/s %.0f deg" % (
                "L" if c["kind"] == "ball_line" else "J", c["r"][0], c["r"][1], c["t"], math.hypot(v[0], v[1]),
                math.degrees(math.atan2(v[1], v[0])))
        txt += " | balls %s | end (%.2f,%.2f)" % ([(round(b["t"], 2), b["ball"]) for b in m["balls"][:4]], m["end"][0], m["end"][1])
        return txt


def real_line(shot):
    txt = "film:"
    for name, r, t in shot.cushions:
        txt += " | %s (%.2f,%.2f) t%.2f" % (name, r[0], r[1], t - shot.t_first)
    if shot.cannon:
        txt += " | cannon ball %d at (%.2f,%.2f) t%.2f" % (shot.cannon_i, shot.cannon["r"][0], shot.cannon["r"][1],
                                                           shot.cannon["t"] - shot.t_first)
    return txt


if __name__ == "__main__":
    path = sys.argv[1]
    params, max_speed = None, 6.5
    tip_x, tip_y = (-0.45, 0.45), (-0.45, 0.45)
    for a in sys.argv[2:]:
        if a.startswith("--params="):
            params = a[9:]
        elif a.startswith("--max="):
            max_speed = float(a[6:])
        elif a.startswith("--tip-x="):
            tip_x = tuple(float(v) for v in a[8:].split(","))
        elif a.startswith("--tip-y="):
            tip_y = tuple(float(v) for v in a[8:].split(","))
    values = json.loads(Path(params or REPO / "core/data/table_params.json").read_text())["values"]
    shot = Shot(path, tag=Path(path).stem)
    print(real_line(shot), flush=True)
    s, x, info, rec = shot.fit(params, values["roll_friction"], max_speed, tip_x, tip_y)
    print("loss %.2f speed %.2f tip (%.3f, %.3f) aim %.4f terms %s cannon: %s" % (
        s, x[0], x[1], x[2], rec["aim"] if rec else float("nan"), info.get("terms") if info else None,
        info.get("cannon") if info else None))
    if info and info.get("m"):
        print("ours:" + shot.line(info["m"]))
