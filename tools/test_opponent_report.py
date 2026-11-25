"""tools/opponent_report.py's stage 2 marks on handmade tournament reports: one that meets every
mark, and the same with each mark missed in turn."""
import contextlib
import copy
import io
import json
import pathlib
import re
import tempfile
import unittest

import opponent_report

# (level, medium made per 100, long made per 100, each frame's highest break by either player as
# {break: frames}); each player's own highs are half of those, as self-play has them.
GOOD_LEVELS = {"club": (60, 25, {15: 1000}),
               "amateur": (80, 45, {30: 1000}),
               "pro": (90, 65, {55: 1000}),
               "world": (95, 75, {80: 850, 120: 150})}
LADDER = (("club", "beginner"), ("amateur", "club"), ("pro", "amateur"), ("world", "pro"))


def highs_json(highs):
    """`frame_highs` or `frame_best` as the tournament writes them, from {break: frames}."""
    counts = sorted([points, n] for points, n in highs.items())
    values = sorted(points for points, n in highs.items() for _ in range(n))
    typical = values[len(values) // 2] if len(values) % 2 else (values[len(values) // 2 - 1] + values[len(values) // 2]) / 2
    return {"frames": len(values), "typical": typical, "fifty_plus": sum(v >= 50 for v in values),
            "century": sum(v >= 100 for v in values), "counts": counts}


def halved(highs):
    return {points // 2: n for points, n in highs.items()}


def side(frames, medium=60, long_=25, highs=None, won=0, us_max=200_000.0, fallbacks=0):
    """One level's counts as `ofs_sim_cli tournament --report` writes them; `highs` its own."""
    highs = highs or {10: frames}
    bands = {"short": {"attempts": 100, "made": 90}, "medium": {"attempts": 100, "made": medium},
             "long": {"attempts": 100, "made": long_}, "unknown": {"attempts": 0, "made": 0}}
    return {
        "frames_won": won, "strokes": 5000,
        "pots": {"attempts": 300, "made": 90 + medium + long_, "bands": bands},
        "breaks": {"count": 900, "mean": 12.0, "median": 9.0, "highest": max(highs), "fifty_plus": 1,
                   "hundred_plus": 0, "maximums": 0},
        "safeties": 400, "escapes": 50, "safeties_with_pot": 100, "no_pot": 350, "fouls": 200, "points": 9000,
        "maximum": {"visits_on": 10, "completed": 0},
        "think": {"sims_mean": 100.0, "sims_max": 400, "steps_mean": 30.0, "us_mean": 5000.0, "us_p99": 20000.0,
                  "us_max": us_max, "stopped": {"done": 5000, "sims": 0, "steps": 0, "time": 0}},
        "fallbacks": fallbacks,
        "tactics": {"safeties": {"played": 400, "then_pot_attempt": 150, "then_potted": 60, "then_no_attempt": 250,
                                 "snooker_kind": 20},
                    "snookers": {"laid": 30, "called": 15, "then_fouled": 10, "then_foul_points": 45},
                    "escapes": {"played": 50, "hit": 40, "by_cushions": {"1": {"played": 30, "hit": 27},
                                                                         "unknown": {"played": 20, "hit": 13}}},
                    "break_offs": {"played": 500, "then_pot": 100, "then_no_pot": 400}},
        "frame_highs": highs_json(highs),
        "concessions": 0, "claims": 3,
    }


def report(a, b, frames, a_stats, b_stats, failures=(), best=None):
    return {"frames": frames, "seed": "1", "params_hash": "p", "table_hash": "t",
            "levels": {"a": {"id": a, "name": opponent_report.NAMES[a]}, "b": {"id": b, "name": opponent_report.NAMES[b]}},
            "level_hashes": {"a": "h", "b": "h"}, "a": a_stats, "b": b_stats,
            "frame_best": highs_json(best or {10: frames}), "exhibitions": [],
            "failures": list(failures), "timing": {"wall_s": 1.0, "threads": 4, "frames_per_s": 1.0}}


def good_reports():
    """Every report stage 2 reads, each meeting its marks: name -> report."""
    out = {}
    for level, (medium, long_, best) in GOOD_LEVELS.items():
        frames = opponent_report.STAGE2_FRAMES
        out[f"{level}.json"] = report(level, level, frames,
                                      side(frames, medium, long_, halved(best), won=frames // 2),
                                      side(frames, medium, long_, halved(best), won=frames - frames // 2),
                                      best=best)
    frames = opponent_report.LADDER_FRAMES
    for i, (upper, lower) in enumerate(LADDER):
        won = int(0.8 * frames)
        up, down = side(frames, won=won), side(frames, won=frames - won)
        # The higher level sits at B in one pairing, as a report may have it either way.
        out[f"{upper}-{lower}.json"] = (report(lower, upper, frames, down, up) if i == 1
                                        else report(upper, lower, frames, up, down))
    return out


def set_best(r, highs):
    r["frame_best"] = highs_json(highs)


def mark_names():
    names = []
    for level, *_ in opponent_report.STAGE2_LEVELS:
        name = opponent_report.NAMES[level]
        names += [f"{name}'s frames against itself", f"{name}'s medium pots", f"{name}'s long pots",
                  f"{name}'s typical break"]
    names.append("World class's frames with a century")
    for upper, lower in LADDER:
        label = f"{opponent_report.NAMES[upper]} over {opponent_report.NAMES[lower]}"
        names += [f"{label}: frames", f"{label}: frames won"]
    return names + ["No stroke refused by the engine", "No fallback", "World class's slowest decision"]


def both(r, fn):
    for key in ("a", "b"):
        fn(r[key])


def upper_side(r, upper):
    return r["a"] if r["levels"]["a"]["id"] == upper else r["b"]


def set_won(r, upper, won):
    upper_side(r, upper)["frames_won"] = won


# Each mark with a change to the good reports that misses it alone.
MISSES = {
    "Club's frames against itself": lambda rs: rs["club.json"].update(frames=999),
    "Club's medium pots": lambda rs: both(rs["club.json"], lambda s: s["pots"]["bands"]["medium"].update(made=54)),
    "Club's long pots": lambda rs: both(rs["club.json"], lambda s: s["pots"]["bands"]["long"].update(made=31)),
    "Club's typical break": lambda rs: set_best(rs["club.json"], {9: 1000}),
    "Amateur's frames against itself": lambda rs: rs["amateur.json"].update(frames=10),
    "Amateur's medium pots": lambda rs: both(rs["amateur.json"],
                                             lambda s: s["pots"]["bands"]["medium"].update(made=86)),
    "Amateur's long pots": lambda rs: both(rs["amateur.json"], lambda s: s["pots"]["bands"]["long"].update(made=39)),
    "Amateur's typical break": lambda rs: set_best(rs["amateur.json"], {41: 1000}),
    "Pro's frames against itself": lambda rs: rs["pro.json"].update(frames=500),
    "Pro's medium pots": lambda rs: both(rs["pro.json"], lambda s: s["pots"]["bands"]["medium"].update(made=85)),
    "Pro's long pots": lambda rs: both(rs["pro.json"], lambda s: s["pots"]["bands"]["long"].update(made=71)),
    "Pro's typical break": lambda rs: set_best(rs["pro.json"], {39: 1000}),
    "World class's frames against itself": lambda rs: rs["world.json"].update(frames=1),
    "World class's medium pots": lambda rs: both(rs["world.json"],
                                                 lambda s: s["pots"]["bands"]["medium"].update(made=99)),
    "World class's long pots": lambda rs: both(rs["world.json"], lambda s: s["pots"]["bands"]["long"].update(made=69)),
    # These two change only their own part of the counts, so both can be missed at once.
    "World class's typical break": lambda rs: rs["world.json"]["frame_best"].update(
        counts=[[59 if points == 80 else points, n] for points, n in rs["world.json"]["frame_best"]["counts"]]),
    "World class's frames with a century": lambda rs: rs["world.json"]["frame_best"].update(
        counts=[[points, 99 if points >= 100 else 901] for points, _ in rs["world.json"]["frame_best"]["counts"]]),
    "Club over Beginner: frames": lambda rs: rs["club-beginner.json"].update(frames=299),
    "Club over Beginner: frames won": lambda rs: set_won(rs["club-beginner.json"], "club", 209),
    "Amateur over Club: frames": lambda rs: rs["amateur-club.json"].update(frames=100),
    "Amateur over Club: frames won": lambda rs: set_won(rs["amateur-club.json"], "amateur", 69),
    "Pro over Amateur: frames": lambda rs: rs["pro-amateur.json"].update(frames=1),
    "Pro over Amateur: frames won": lambda rs: set_won(rs["pro-amateur.json"], "pro", 0),
    "World class over Pro: frames": lambda rs: rs["world-pro.json"].update(frames=200),
    "World class over Pro: frames won": lambda rs: set_won(rs["world-pro.json"], "world", 139),
    "No stroke refused by the engine": lambda rs: rs["pro-amateur.json"]["failures"].append(
        {"where": "frame 3", "stroke": 7, "player": 0, "level": "pro", "kind": "opponent",
         "detail": "it left the cue ball in hand unplaced", "repro": ""}),
    "No fallback": lambda rs: rs["amateur.json"]["b"].update(fallbacks=1),
    "World class's slowest decision": lambda rs: rs["world-pro.json"]["a"]["think"].update(us_max=1_600_000.0),
}


class Stage2Marks(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = pathlib.Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def check(self, reports, *checks):
        paths = []
        for name, r in reports.items():
            path = self.tmp / name
            path.write_text(json.dumps(r), encoding="utf-8")
            paths.append(path)
        out = io.StringIO()
        args = [*map(str, paths), "--out", str(self.tmp / "report.md")]
        for stage in checks or ("stage2",):
            args += ["--check", stage]
        with contextlib.redirect_stdout(out):
            code = opponent_report.main(args)
        return code, out.getvalue()

    def statuses(self, output, names=None):
        """{mark: status} as printed, for the marks named (stage 2's by default)."""
        found = {}
        for name in names or mark_names():
            m = re.search(rf"(?m)^(PASS|FAIL|MISSING)\s+{re.escape(name)}: ", output)
            if m:
                found[name] = m.group(1)
        return found

    def test_every_mark_is_listed(self):
        self.assertEqual(sorted(MISSES), sorted(mark_names()))

    def test_reports_that_meet_every_mark_pass(self):
        code, output = self.check(good_reports())
        self.assertEqual(code, 0, output)
        self.assertEqual(self.statuses(output), {name: "PASS" for name in mark_names()})
        self.assertIn(f"{len(mark_names())} of {len(mark_names())} marks met", output)
        text = (self.tmp / "report.md").read_text(encoding="utf-8")
        for heading in ("## Stage 2's marks", "## The ladder", "## Tactics per level", "## Thinking per level"):
            self.assertIn(heading, text)
        self.assertIn("| Amateur over Club | 300 | 240/300 (80.0%) |", text)
        self.assertIn("| typical break | 15 | 30 | 55 | 80 |", text)
        self.assertIn("| - each player's own | 7 | 15 | 27 | 40 |", text)
        self.assertIn("| frames with a century | " + "0/1000 (0.0%) | " * 3 + "150/1000 (15.0%) |", text)
        self.assertRegex(output, r"(?m)^PASS\s+Club's typical break: 15 \(each player's own: 7\) \(wanted 10 to 20\)$")

    def test_each_mark_missed_alone_fails_by_name(self):
        for name, miss in MISSES.items():
            with self.subTest(mark=name):
                reports = good_reports()
                miss(reports)
                code, output = self.check(reports)
                self.assertEqual(code, 1, output)
                statuses = self.statuses(output)
                self.assertEqual(statuses.pop(name), "FAIL", output)
                self.assertEqual(set(statuses.values()), {"PASS"}, output)
                self.assertIn("not met: 1", output)

    def test_every_mark_missed_at_once_lists_them_all(self):
        reports = good_reports()
        for miss in MISSES.values():
            miss(reports)
        code, output = self.check(reports)
        self.assertEqual(code, 1)
        self.assertEqual(self.statuses(output), {name: "FAIL" for name in mark_names()})
        self.assertIn(f"0 of {len(mark_names())} marks met; not met: {len(mark_names())}", output)

    def test_a_report_not_given_is_missing_not_passed(self):
        reports = good_reports()
        del reports["pro.json"], reports["world-pro.json"]
        code, output = self.check(reports)
        self.assertEqual(code, 1)
        statuses = self.statuses(output)
        for name in ("Pro's frames against itself", "Pro's medium pots", "Pro's long pots", "Pro's typical break",
                     "World class over Pro: frames", "World class over Pro: frames won"):
            self.assertEqual(statuses[name], "MISSING", name)
        self.assertEqual(statuses["World class's slowest decision"], "PASS")

    def test_the_typical_break_is_the_frames_best_not_each_players_own(self):
        # Each player's own highs would pass by the old pooled median; the frames' best misses.
        reports = good_reports()
        amateur = reports["amateur.json"]
        both(amateur, lambda s: s.update(frame_highs=highs_json({30: 1000})))
        set_best(amateur, {50: 1000})
        code, output = self.check(reports)
        self.assertEqual(code, 1, output)
        self.assertRegex(output, r"(?m)^FAIL\s+Amateur's typical break: 50 \(each player's own: 30\) ")
        # And the reverse: their own highs would fail, the frames' best passes.
        reports = good_reports()
        amateur = reports["amateur.json"]
        both(amateur, lambda s: s.update(frame_highs=highs_json({10: 1000})))
        set_best(amateur, {30: 1000})
        code, output = self.check(reports)
        self.assertEqual(code, 0, output)
        self.assertRegex(output, r"(?m)^PASS\s+Amateur's typical break: 30 \(each player's own: 10\) ")

    def test_the_median_is_over_the_frames(self):
        reports = good_reports()
        set_best(reports["amateur.json"], {10: 500, 30: 500})
        code, output = self.check(reports)
        self.assertEqual(code, 0, output)
        self.assertRegex(output, r"(?m)^PASS\s+Amateur's typical break: 20 ")

    def test_a_report_without_frame_best_is_missing_not_judged_the_old_way(self):
        reports = good_reports()
        del reports["pro.json"]["frame_best"], reports["world.json"]["frame_best"]
        code, output = self.check(reports)
        self.assertEqual(code, 1, output)
        statuses = self.statuses(output)
        for name in ("Pro's typical break", "World class's typical break", "World class's frames with a century"):
            self.assertEqual(statuses.pop(name), "MISSING", name)
            self.assertRegex(output, rf"(?m)^MISSING\s+{re.escape(name)}: no frame_best \(\w+\.json\) "
                                     r"\(wanted a report from a build that writes frame_best; this one predates it\)$")
        self.assertEqual(set(statuses.values()), {"PASS"}, output)
        text = (self.tmp / "report.md").read_text(encoding="utf-8")
        self.assertIn("| typical break | 15 | 30 | - | - |", text)

    def test_stage1_and_stage2_together(self):
        code, output = self.check(good_reports(), "stage1", "stage2")
        self.assertEqual(code, 1)
        self.assertEqual(self.statuses(output, ["Beginner's medium pots"]), {"Beginner's medium pots": "MISSING"})
        self.assertEqual(self.statuses(output), {name: "PASS" for name in mark_names()})
        text = (self.tmp / "report.md").read_text(encoding="utf-8")
        self.assertLess(text.index("## Stage 1's marks"), text.index("## Stage 2's marks"))


if __name__ == "__main__":
    unittest.main()
