import contextlib
import io
import json
import math
import pathlib
import tempfile
import unittest
from unittest import mock

import make_table_shape


def run(shape, *argv):
    """main() on the file at `shape`: its exit code and what it printed to stderr."""
    stderr = io.StringIO()
    with mock.patch.object(make_table_shape, "SHAPE", shape), contextlib.redirect_stderr(stderr), \
            contextlib.redirect_stdout(io.StringIO()):
        return make_table_shape.main(list(argv)), stderr.getvalue()


def mirrored(point, sx, sy):
    return (round(sx * point[0], 12), round(sy * point[1], 12))


def arc_points(arc, sx, sy):
    """A mirrored arc sweeps the other way, so an arc is known by its ends, its middle and its centre."""
    def at(angle):
        return (arc["centre"][0] + arc["radius"] * math.cos(angle), arc["centre"][1] + arc["radius"] * math.sin(angle))

    ends = frozenset((mirrored(at(arc["angle_start"]), sx, sy), mirrored(at(arc["angle_end"]), sx, sy)))
    return ends, mirrored(at(0.5 * (arc["angle_start"] + arc["angle_end"])), sx, sy), mirrored(arc["centre"], sx, sy)


class MakeTableShapeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.committed = json.loads(make_table_shape.SHAPE.read_text(encoding="utf-8"))
        cls.built = make_table_shape.build(cls.committed["dimensions"])

    def copy(self, folder, **dimensions):
        shape = json.loads(json.dumps(self.committed))
        shape["dimensions"].update(dimensions)
        path = pathlib.Path(folder) / "table_shape.json"
        path.write_text(json.dumps(shape, indent=2) + "\n", encoding="utf-8")
        return path

    def test_check_passes_on_the_committed_file(self):
        self.assertEqual(run(make_table_shape.SHAPE, "--check"), (0, ""))

    def test_check_fails_when_a_dimension_changed_without_a_rebuild(self):
        cases = {"jaw_radius_m": ("lines", "arcs"), "corner_drop_centre_m": ("pockets", "lines"),
                 "middle_jaw_start_m": ("lines", "arcs"), "rail_width_m": ("rail_width_m",)}
        for name, stale in cases.items():
            with self.subTest(name), tempfile.TemporaryDirectory() as tmp:
                path = self.copy(tmp, **{name: self.committed["dimensions"][name] + 0.001})
                code, said = run(path, "--check")
                self.assertEqual(code, 1)
                for part in stale:
                    self.assertIn(part, said)
                self.assertIn("run tools/make_table_shape.py", said)

    def test_a_rebuild_makes_the_check_pass_and_keeps_the_rest_of_the_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self.copy(tmp, corner_jaw_start_m=0.09)
            self.assertEqual(run(path)[0], 0)
            self.assertEqual(run(path, "--check"), (0, ""))
            rebuilt = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(rebuilt["dimensions"]["corner_jaw_start_m"], 0.09)
            for key in ("schema_version", "_note", "docs"):
                self.assertEqual(rebuilt[key], self.committed[key])
            self.assertNotEqual(rebuilt["lines"], self.committed["lines"])

    def test_check_takes_no_other_argument(self):
        self.assertEqual(run(make_table_shape.SHAPE, "--check", "--force")[0], 2)

    def test_the_outline_is_symmetric_across_both_axes(self):
        segments = {frozenset((mirrored(line["p1"], 1, 1), mirrored(line["p2"], 1, 1))): mirrored(line["normal"], 1, 1)
                    for line in self.built["lines"]}
        holes = {(mirrored(pocket["drop_centre"], 1, 1), pocket["drop_radius"]) for pocket in self.built["pockets"]}
        arcs = {arc_points(arc, 1, 1) for arc in self.built["arcs"]}
        for sx, sy in ((-1, 1), (1, -1)):
            with self.subTest(mirror=(sx, sy)):
                for line in self.built["lines"]:
                    ends = frozenset((mirrored(line["p1"], sx, sy), mirrored(line["p2"], sx, sy)))
                    self.assertEqual(segments.get(ends), mirrored(line["normal"], sx, sy), line["name"])
                for pocket in self.built["pockets"]:
                    self.assertIn((mirrored(pocket["drop_centre"], sx, sy), pocket["drop_radius"]), holes, pocket["name"])
                for arc in self.built["arcs"]:
                    self.assertIn(arc_points(arc, sx, sy), arcs, arc["name"])

    def test_six_pockets_six_noses_and_a_jaw_and_a_facing_at_each_cushion_end(self):
        self.assertEqual([pocket["name"] for pocket in self.built["pockets"]], list(make_table_shape.POCKET_ORDER))
        kinds = [line["kind"] for line in self.built["lines"]]
        self.assertEqual((kinds.count("nose"), kinds.count("facing"), len(self.built["arcs"])), (6, 12, 12))


if __name__ == "__main__":
    unittest.main()
