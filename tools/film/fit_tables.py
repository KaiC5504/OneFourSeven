# /// script
# dependencies = ["numpy", "scipy"]
# ///
"""Every filmed shot fitted on each candidate table: which cloth and cushion values the films prefer.

Prints each shot's misfit per table and the totals; results accumulate in build/film-fit/results.json.
Shot 3 (the cue ball flies) cannot be played: leave it out with --only.

usage: uv run tools/film/fit_tables.py --only=1,2,4,5,6,7,8,9,10 [--jobs=4] [--variants=now,g1_e90_r09]
"""
import json, sys
from multiprocessing import Pool
from pathlib import Path

import fitshot

HERE = fitshot.HERE
# Changes to the committed table_params.json values.
VARIANTS = {
    "now": {},
    "grip2": {"cushion_side_grip": 2.0},
    "grip4": {"cushion_side_grip": 4.0},
    "e87": {"cushion_restitution": 0.87},
    "e93": {"cushion_restitution": 0.93},
    "r08": {"roll_friction": 0.008},
    "r10": {"roll_friction": 0.010},
    "paper": {"cushion_restitution": 0.98, "roll_friction": 0.010},
    "feel": {"cushion_restitution": 0.85, "roll_friction": 0.015},
}


# What each film names or plainly shows: (tip_x range, tip_y range, cue speeds to start from).
PRIORS = {
    "shot_1": ((-0.45, 0.0), (0.0, 0.45), [1.5, 2.0, 2.5, 3.0, 3.5, 4.0]),
    "shot_2": ((-0.2, 0.2), (-0.2, 0.35), [3.5, 4.0, 4.5, 5.0, 5.5, 6.0]),
    "shot_4": ((0.0, 0.45), (0.0, 0.45), [5.0, 5.5, 6.0, 6.5]),
    "shot_5": ((-0.2, 0.2), (0.0, 0.45), [4.5, 5.0, 5.5, 6.0, 6.5]),
    "shot_6": ((0.0, 0.45), (-0.45, 0.0), [3.5, 4.0, 4.5, 5.0, 5.5, 6.0]),
    "shot_7": ((-0.45, 0.0), (-0.45, 0.0), [4.25, 4.75, 5.25, 5.75, 6.25]),
    "shot_8": ((-0.2, 0.2), (0.0, 0.45), [4.5, 5.0, 5.5, 6.0, 6.5]),
    "shot_9": ((-0.3, 0.3), (0.0, 0.45), [3.0, 3.5, 4.0, 4.5, 5.0]),
    "shot_10": ((-0.45, 0.0), (0.0, 0.45), [1.5, 2.0, 2.5, 3.0, 3.5]),
}


def variant_file(name):
    p = json.loads((fitshot.REPO / "core/data/table_params.json").read_text())
    p["values"].update(VARIANTS[name])
    path = HERE / f"bv_{name}.json"
    path.write_text(json.dumps(p))
    return path, p["values"]


def work(job):
    name, shot_path = job
    path, values = variant_file(name)
    shot = fitshot.Shot(shot_path, tag=f"{Path(shot_path).stem}_{name}")
    try:
        tx, ty, speeds = PRIORS.get(Path(shot_path).stem, ((-0.45, 0.45), (-0.45, 0.45), None))
        s, x, info, rec = shot.fit(str(path), values["roll_friction"], values["max_cue_speed_m_s"], tx, ty, speeds)
    except Exception as e:  # one bad shot file must not stop the batch
        return name, Path(shot_path).stem, {"error": repr(e)}
    return name, Path(shot_path).stem, {
        "loss": s, "speed": float(x[0]), "tip": [float(x[1]), float(x[2])], "terms": (info or {}).get("terms"),
        "cannon": (info or {}).get("cannon"), "ours": shot.line(info["m"]) if info and info.get("m") else None,
        "film": fitshot.real_line(shot)}


if __name__ == "__main__":
    jobs_n, only, names = 4, None, list(VARIANTS)
    for a in sys.argv[1:]:
        if a.startswith("--jobs="):
            jobs_n = int(a[7:])
        elif a.startswith("--only="):
            only = {f"shot_{n}" for n in a[7:].split(",")}
        elif a.startswith("--variants="):
            names = a[11:].split(",")
    shots = sorted(p for p in fitshot.FILMS.glob("shot_*.json") if only is None or p.stem in only)
    jobs = [(n, str(p)) for n in names for p in shots]
    out_path = HERE / "results.json"
    results = json.loads(out_path.read_text()) if out_path.exists() else {}
    with Pool(jobs_n) as pool:
        for name, stem, res in pool.imap_unordered(work, jobs):
            results.setdefault(name, {})[stem] = res
            out_path.write_text(json.dumps(results, indent=1))
            print(name, stem, "loss %.2f" % res["loss"] if "loss" in res else res, flush=True)
    stems = sorted({s for v in results.values() for s in v}, key=lambda s: int(s.split("_")[1]))
    print("variant   " + " ".join(f"{s[5:]:>7}" for s in stems) + "   total")
    for name in results:
        row = [results[name].get(s, {}).get("loss") for s in stems]
        print(f"{name:9} " + " ".join("%7.1f" % v if v is not None else "      -" for v in row)
              + "  %6.1f" % sum(min(v, 60.0) for v in row if v is not None))
