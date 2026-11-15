# /// script
# dependencies = ["numpy", "scipy"]
# ///
"""Write each filmed shot as a position fixture: the layout, the fitted strike on the committed
table, and what the film shows.

usage: uv run tools/film/write_fixtures.py now    (after fit_tables.py --variants=now)
"""
import json, math, sys
from pathlib import Path

import fitshot

HERE = fitshot.HERE
OUT = fitshot.REPO / "core/tests/fixtures/positions"
SHOTS = {
    10: ("film_allen_two_cushions", "Mark Allen v John Higgins: a thin blue to the right middle pocket with top and left-hand side, the cue ball off the baulk and right side cushions and back up the table into the pink and its reds."),
    9: ("film_osullivan_baulk_cushion", "Ronnie O'Sullivan v Michael Georgiou: the brown to a baulk corner with follow, the cue ball off the baulk cushion and the length of the table to the pink and a red."),
    8: ("film_lee_hugs_cushion", "Stephen Lee v Ding Junhui: a black hanging in a top corner struck near full with heavy top; the cue ball leaves the top cushion and the top spin pulls it back on to the cushion and along it into a red."),
    7: ("film_trump_screw_left", "Judd Trump v John Higgins: the black off its spot with deep screw and left-hand side, the cue ball the length of the table to the baulk and right side cushions and back to the reds by the top cushion."),
    6: ("film_trump_screw_check", "Judd Trump v Ding Junhui: the black off its spot with screw and right-hand side, check side on the left side and baulk cushions, the cue ball back up the left cushion to a red lying on it."),
    4: ("selby_blue", "Mark Selby v Ronnie O'Sullivan, the last frame of the 2014 World Championship final: the blue to the right middle pocket almost straight, struck very hard with top and right-hand side, the cue ball round the right side, baulk and left side cushions to the two reds on the pink."),
    5: ("film_selby_one_cushion", "Mark Selby v Ding Junhui: a near-full blue to a top corner struck very hard with follow, the cue ball off the left side cushion to the reds by the top cushion."),
    2: ("film_osullivan_stun_back", "Ronnie O'Sullivan v Mark Williams: a near-full black with a little follow, the cue ball off the top cushion and straight back to clip the red on the pink."),
    1: ("film_higgins_left_side", "Alex Higgins: a ball to the right middle pocket with follow and left-hand side, the cue ball off the baulk cushion wide of the mirror line into a ball by the right side cushion. Old footage: places good to 3 to 5 cm only, and the potted ball's colour is a guess."),
}


def main(variant):
    results = json.loads((HERE / "results.json").read_text())[variant]
    values = json.loads((fitshot.REPO / "core/data/table_params.json").read_text())["values"]
    for n, (name, what) in SHOTS.items():
        res = results.get(f"shot_{n}")
        if not res or "loss" not in res:
            print(n, "no fit"); continue
        shot = fitshot.Shot(fitshot.FILMS / f"shot_{n}.json", tag=f"gen{n}")
        rec = shot.shoot(None, res["speed"], res["tip"][0], res["tip"][1])
        score, info = shot.score(rec, values["roll_friction"])
        m = info["m"]
        measured = {"cushions": [{"cushion": c, "r": [round(r[0], 3), round(r[1], 3)], "t_s": round(t - shot.t_first, 2)}
                                 for c, r, t in shot.cushions]}
        if shot.cannon:
            measured["cannon"] = {"ball": shot.start[shot.cannon_i]["ball"], "index": shot.cannon_i,
                                  "pack": sorted(shot.pack),
                                  "r": [round(shot.cannon["r"][0], 3), round(shot.cannon["r"][1], 3)],
                                  "t_s": round(shot.cannon["t"] - shot.t_first, 2)}
        measured["object_ball"] = shot.object
        measured["allowed_on_the_way"] = sorted(shot.allowed - shot.pack)
        fx = {
            "name": name,
            "note": what + " Measured from the broadcast reel KaiC sent on 2026-11-15 (docs/calibration/targets.md, the filmed shots): `measured` holds each cushion's place and time and the cannon, with times counted from the first ball contact. The strike is the pace and tip offset that fit the film best on the committed table.",
            "cue_ball": 0,
            "start": {"balls": shot.start},
            "strike": {"aim_rad": rec["aim"], "tip_x": round(res["tip"][0], 4), "tip_y": round(res["tip"][1], 4),
                       "elevation_rad": 0.0, "speed_m_s": round(res["speed"], 3)},
            "measured": measured,
        }
        # the rounded strike must still play the shot
        rec2 = shot.sim(fx["strike"]["aim_rad"], fx["strike"]["tip_x"], fx["strike"]["tip_y"], fx["strike"]["speed_m_s"])
        score2, info2 = shot.score(rec2, values["roll_friction"])
        (OUT / f"{name}.json").write_text(json.dumps(fx, indent=2) + "\n", encoding="utf-8", newline="\n")
        print(f"{n:2d} {name}: loss {score:.2f} (rounded strike {score2:.2f}) terms {info2.get('terms')} cannon {info2.get('cannon')}")
        print("    " + fitshot.real_line(shot))
        print("    ours:" + shot.line(info2["m"])[:300])


if __name__ == "__main__":
    main(sys.argv[1])
