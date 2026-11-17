import contextlib
import io
import json
import math
import pathlib
import tempfile
import unittest
from unittest import mock

import make_exhibitions

TABLE = make_exhibitions.Table()


def report(*breaks, failures=()):
    """A tournament report of exhibitions with these breaks, as `ofs_sim_cli tournament --report` writes it."""
    return {"frames": 0, "exhibitions": [{"name": f"Open table {i + 1}", "break_points": b, "maximum": b >= 147,
                                          "strokes": 36 if b >= 147 else 5} for i, b in enumerate(breaks)],
            "failures": list(failures)}


def run(*argv, canned=None):
    """main() with the simulator replaced by `canned` (a report): its exit code and what it printed."""
    out = io.StringIO()
    fake = mock.Mock(return_value=(canned, []))
    with mock.patch.object(make_exhibitions, "run_tournament", fake), contextlib.redirect_stdout(out), \
            contextlib.redirect_stderr(out):
        return make_exhibitions.main(list(argv)), out.getvalue()


class Generation(unittest.TestCase):
    def test_the_same_seed_gives_the_same_tables(self):
        for n in range(5):
            self.assertEqual(make_exhibitions.candidate(3, n, TABLE), make_exhibitions.candidate(3, n, TABLE))
        self.assertNotEqual(make_exhibitions.candidate(3, 0, TABLE)["balls"],
                            make_exhibitions.candidate(4, 0, TABLE)["balls"])

    def test_every_table_keeps_its_balls_apart_and_its_reds_off_the_cushions(self):
        r = TABLE.radius
        self.assertAlmostEqual(r, 0.02625)
        half_l, half_w = TABLE.length / 2.0, TABLE.width / 2.0
        for seed in (1, 2):
            for n in range(25):
                lay = make_exhibitions.candidate(seed, n, TABLE)
                self.assertIsNotNone(lay)
                with self.subTest(seed=seed, n=n):
                    balls = lay["balls"]
                    self.assertEqual(len(balls), 22)
                    self.assertEqual(balls[lay["cue_ball"]]["ball"], "cue")
                    self.assertEqual(sorted(b["index"] for b in balls if b["ball"] == "red"), list(range(15)))
                    for i, a in enumerate(balls):
                        for b in balls[i + 1:]:
                            self.assertGreaterEqual(math.hypot(a["x"] - b["x"], a["y"] - b["y"]),
                                                    2 * r + 0.015 - 1e-9)
                    for b in balls:
                        if b["ball"] == "red":
                            self.assertLessEqual(abs(b["x"]), half_l - 0.15)
                            self.assertLessEqual(abs(b["y"]), half_w - 0.15)
                            for spot in TABLE.spots.values():
                                self.assertGreaterEqual(math.hypot(b["x"] - spot[0], b["y"] - spot[1]), 2.5 * r)
                        elif b["ball"] != "cue":
                            self.assertEqual((b["x"], b["y"]), TABLE.spots[b["ball"]])
                    self.assertEqual(make_exhibitions.problems(lay, TABLE), [])

    def test_the_black_has_a_clear_run_to_both_top_corners(self):
        black = TABLE.spots["black"]
        for n in range(25):
            lay = make_exhibitions.candidate(5, n, TABLE)
            for b in lay["balls"]:
                if b["ball"] == "red":
                    for corner in TABLE.top_corners:
                        gap, _ = make_exhibitions.to_segment((b["x"], b["y"]), black, corner)
                        self.assertGreaterEqual(gap, 2 * TABLE.radius)

    def test_most_reds_lie_about_the_pink_and_the_black(self):
        pink_x = TABLE.spots["pink"][0]
        for n in range(10):
            reds = [b for b in make_exhibitions.candidate(1, n, TABLE)["balls"] if b["ball"] == "red"]
            self.assertGreaterEqual(sum(1 for b in reds if b["x"] > pink_x - 0.4), 10)


class Files(unittest.TestCase):
    def test_the_written_file_loads_back(self):
        layouts = make_exhibitions.renamed([make_exhibitions.candidate(1, n, TABLE) for n in range(3)])
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / "exhibitions.json"
            make_exhibitions.write_file(path, layouts)
            data = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(data["schema_version"], 1)
            self.assertIn("_note", data)
            self.assertEqual(make_exhibitions.load(path), layouts)
        self.assertEqual([lay["name"] for lay in layouts], ["Open table 1", "Open table 2", "Open table 3"])

    def test_the_committed_file_has_its_tables_in_order_laid_by_the_rules(self):
        layouts = make_exhibitions.load(make_exhibitions.EXHIBITIONS)
        self.assertGreaterEqual(len(layouts), 8)
        self.assertEqual([lay["name"] for lay in layouts], [f"Open table {i}" for i in range(1, len(layouts) + 1)])
        for lay in layouts:
            with self.subTest(lay["name"]):
                self.assertEqual(make_exhibitions.problems(lay, TABLE), [])


class Check(unittest.TestCase):
    def setUp(self):
        self._folder = tempfile.TemporaryDirectory()
        self.folder = pathlib.Path(self._folder.name)
        self.file = self.folder / "exhibitions.json"
        make_exhibitions.write_file(self.file, make_exhibitions.renamed(
            [make_exhibitions.candidate(1, n, TABLE) for n in range(2)]))
        self.cli = self.folder / "ofs_sim_cli.exe"
        self.cli.write_text("")

    def tearDown(self):
        self._folder.cleanup()

    def check(self, canned):
        return run("--check", "--file", str(self.file), "--cli", str(self.cli), canned=canned)

    def test_a_table_short_of_147_fails_the_check(self):
        code, out = self.check(report(147, 57))
        self.assertEqual(code, 1)
        self.assertIn("Open table 2", out)
        self.assertIn("break  57", out)
        self.assertIn("NOT 147: needed the black", out)
        self.assertIn("1 of 2 tables make 147", out)
        self.assertNotIn("Open table 1 ", out)

    def test_fifty_tables_print_only_the_failing_ones_and_a_count(self):
        make_exhibitions.write_file(self.file, make_exhibitions.renamed(
            [make_exhibitions.candidate(1, n % 2, TABLE) for n in range(50)]))
        breaks = [147] * 50
        breaks[6], breaks[41] = 33, 140
        code, out = self.check(report(*breaks))
        self.assertEqual(code, 1)
        self.assertEqual(out.splitlines()[-1].split(" (")[0], "48 of 50 tables make 147")
        self.assertEqual(len(out.splitlines()), 3, out)
        self.assertRegex(out, r"(?m)^Open table 7 +break  33 .*NOT 147")
        self.assertRegex(out, r"(?m)^Open table 42 +break 140 .*NOT 147")
        code, out = run("--check", "--verbose", "--file", str(self.file), "--cli", str(self.cli),
                        canned=report(*breaks))
        self.assertEqual(code, 1)
        self.assertEqual(len(out.splitlines()), 51, out)
        self.assertRegex(out, r"(?m)^Open table 50 +break 147 .* 147$")

    def test_a_planned_pot_that_stayed_out_fails_the_check_even_at_147(self):
        code, _ = self.check(report(147, 147, failures=[{"where": "exhibition Open table 1"}]))
        self.assertEqual(code, 1)

    def test_every_table_at_147_passes(self):
        code, out = self.check(report(147, 147))
        self.assertEqual(code, 0, out)
        self.assertIn("2 of 2 tables make 147", out)

    def test_a_report_missing_a_table_is_an_error(self):
        code, out = self.check(report(147))
        self.assertEqual(code, 2)
        self.assertIn("1 exhibitions for 2 tables", out)

    def test_no_cli_is_an_error(self):
        code, out = run("--check", "--file", str(self.file), "--cli", str(self.folder / "missing.exe"))
        self.assertEqual(code, 2)
        self.assertIn("no ofs_sim_cli", out)


def made_by_some(cli, file, threads, folder):
    """A stand-in for the simulator: about two tables in three make 147, the same ones whatever their place."""
    layouts = json.loads(pathlib.Path(file).read_text(encoding="utf-8"))["layouts"]
    breaks = [147 if round(lay["balls"][0]["x"] * 1e4) % 3 else 57 for lay in layouts]
    out = report(*breaks)
    for entry, lay in zip(out["exhibitions"], layouts):
        entry["name"] = lay["name"]
    return out, []


class Search(unittest.TestCase):
    def setUp(self):
        self._folder = tempfile.TemporaryDirectory()
        self.folder = pathlib.Path(self._folder.name)
        self.file = self.folder / "exhibitions.json"
        self.cli = self.folder / "ofs_sim_cli.exe"
        self.cli.write_text("")

    def tearDown(self):
        self._folder.cleanup()

    def search(self, *argv):
        out = io.StringIO()
        results = []

        def recorded(*args):
            results.append(made_by_some(*args))
            return results[-1]

        recorded.results = results
        fake = mock.Mock(side_effect=recorded)
        with mock.patch.object(make_exhibitions, "run_tournament", fake), contextlib.redirect_stdout(out), \
                contextlib.redirect_stderr(out):
            code = make_exhibitions.main(["--file", str(self.file), "--cli", str(self.cli), *argv])
        return code, out.getvalue(), fake

    def test_fifty_tables_are_found_in_batches_and_named_in_order(self):
        code, out, fake = self.search("--count", "50")
        self.assertEqual(code, 0, out)
        layouts = make_exhibitions.load(self.file)
        self.assertEqual([lay["name"] for lay in layouts], [f"Open table {i}" for i in range(1, 51)])
        for lay in layouts:
            self.assertEqual(make_exhibitions.problems(lay, TABLE), [])
        self.assertEqual(len({json.dumps(lay["balls"]) for lay in layouts}), 50)
        played = [len(report["exhibitions"]) for report, _ in fake.side_effect.results]
        self.assertEqual(played[-1], 50, "the file is played once more as written")
        self.assertTrue(all(n <= 12 for n in played[:-1]), played)
        self.assertIn("wrote 50 tables", out)
        self.assertNotIn("candidate 1-", out)
        self.assertRegex(out, r"(?m)^  \d+ of 12 candidates made 147; \d+ of 50 tables$")

    def test_a_limit_too_small_for_the_count_is_an_error(self):
        code, out, _ = self.search("--count", "50", "--limit", "30")
        self.assertEqual(code, 2)
        self.assertRegex(out, r"30 candidates gave \d+ of 50 tables")
        self.assertFalse(self.file.exists())


class Needed(unittest.TestCase):
    def test_the_ball_a_maximum_needs_after_each_break(self):
        cases = {0: "red", 1: "black", 8: "red", 57: "black", 112: "red", 113: "black", 120: "yellow",
                 122: "green", 125: "brown", 129: "blue", 134: "pink", 140: "black"}
        for points, ball in cases.items():
            self.assertEqual(make_exhibitions.needed(points), ball, points)


if __name__ == "__main__":
    unittest.main()
