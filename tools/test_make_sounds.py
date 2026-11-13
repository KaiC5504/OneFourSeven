import math
import pathlib
import tempfile
import unittest

import make_sounds

# (shortest, longest) in ms for each family.
DURATIONS_MS = {
    "ball_click": (20.0, 60.0),
    "tip": (30.0, 100.0),
    "cushion": (100.0, 300.0),
    "jaw": (60.0, 200.0),
    "pocket": (250.0, 600.0),
    "miscue": (80.0, 250.0),
    "roll_loop": (2000.0, 4000.0),
}


def family(name):
    return name.rstrip("0123456789").rstrip("_")


class MakeSoundsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.built = make_sounds.build()
        cls.frames = {name: make_sounds.read_wav(data) for name, data in cls.built.items()}

    def test_every_family_has_its_variants(self):
        self.assertEqual(set(map(family, self.built)), set(DURATIONS_MS))
        self.assertEqual(sum(1 for n in self.built if family(n) == "ball_click"), 4)

    def test_deterministic(self):
        self.assertEqual(make_sounds.build(), self.built)

    def test_committed_files_match(self):
        self.assertEqual(make_sounds.check(), [])

    def test_check_reports_changed_missing_and_stray_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = pathlib.Path(tmp)
            for name, data in self.built.items():
                (folder / f"{name}.wav").write_bytes(data)
            self.assertEqual(make_sounds.check(folder), [])
            (folder / "tip_1.wav").write_bytes(self.built["tip_2"])
            (folder / "jaw_2.wav").unlink()
            (folder / "old.wav").write_bytes(self.built["tip_2"])
            problems = make_sounds.check(folder)
            self.assertEqual(len(problems), 3, problems)

    def test_format_and_durations(self):
        for name, frames in self.frames.items():
            low, high = DURATIONS_MS[family(name)]
            ms = len(frames) * 1000.0 / make_sounds.RATE
            self.assertTrue(low <= ms <= high, f"{name}: {ms:.1f} ms")

    def test_peaks_without_clipping(self):
        for name, frames in self.frames.items():
            peak = max(abs(v) for v in frames)
            self.assertLess(peak, 32767, f"{name} clips")
            self.assertLessEqual(peak / 32767.0, make_sounds.PEAK + 1e-4, name)
            self.assertGreater(peak / 32767.0, 10 ** (-6 / 20), f"{name} is too quiet")

    def test_no_dc_offset(self):
        for name, frames in self.frames.items():
            self.assertLess(abs(sum(frames) / len(frames)), 0.001 * 32767, name)

    def test_one_shots_fade_in_and_out(self):
        for name, frames in self.frames.items():
            if name in make_sounds.LOOPS:
                continue
            self.assertLessEqual(abs(frames[0]), 64, f"{name} starts with a step")
            self.assertLessEqual(abs(frames[-1]), 64, f"{name} ends with a step")

    def test_one_shots_have_died_away_before_the_fade(self):
        fade = make_sounds.samples(make_sounds.FADE_OUT_S)
        for name, frames in self.frames.items():
            if name in make_sounds.LOOPS:
                continue
            tail = max(abs(v) for v in frames[-2 * fade:-fade])
            self.assertLess(tail, 0.05 * 32767, f"{name} is cut off by its fade")

    def test_roll_loop_is_seamless(self):
        frames = self.frames["roll_loop"]
        steps = [abs(frames[i + 1] - frames[i]) for i in range(len(frames) - 1)]
        wrap = abs(frames[0] - frames[-1])
        typical = math.sqrt(sum(s * s for s in steps) / len(steps))
        self.assertLessEqual(wrap, max(steps), "wrap step larger than any step inside the loop")
        self.assertLess(wrap, 4.0 * typical, "wrap step stands out")
        window = make_sounds.samples(0.1)

        def rms(xs):
            return math.sqrt(sum(v * v for v in xs) / len(xs))
        level = 20.0 * math.log10(rms(frames[-window:]) / rms(frames[:window]))
        self.assertLess(abs(level), 2.0, f"loop level jumps {level:.1f} dB at the wrap")

    def test_spectra_follow_the_materials(self):
        centroid = {name: make_sounds.spectral_centroid(f) for name, f in self.frames.items()
                    if name in ("ball_click_1", "tip_1", "jaw_1", "cushion_1", "pocket_1", "roll_loop")}
        self.assertTrue(2000.0 < centroid["ball_click_1"] < 9000.0, centroid)
        self.assertGreater(centroid["ball_click_1"], centroid["tip_1"], "tip is duller than a ball click")
        self.assertGreater(centroid["jaw_1"], centroid["cushion_1"], "jaw is sharper than a cushion")
        self.assertLess(centroid["cushion_1"], 1000.0, centroid)
        self.assertLess(centroid["pocket_1"], 1000.0, centroid)
        self.assertLess(centroid["roll_loop"], 1000.0, centroid)


if __name__ == "__main__":
    unittest.main()
