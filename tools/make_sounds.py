#!/usr/bin/env python3
"""Generates the table sounds (spec 13.2) as 16-bit mono 44.1 kHz WAVs in game/audio/.

    python tools/make_sounds.py            # writes game/audio/*.wav
    python tools/make_sounds.py --check    # exit 1 unless the committed files are exactly these
    python tools/make_sounds.py --report   # peak, length, RMS and spectral centroid of each file

Plain Python with no third-party packages, so the check runs wherever the tools' tests do.
Seeded per file, so the output is the same on every run.

Every impact is modelled the same way: a smooth contact-force pulse whose length sets how bright
the hit is (hard phenolic on phenolic is a fraction of a millisecond, a rubber cushion a few
milliseconds), radiating directly through its rate of change (rigid-body acceleration noise)
and ringing the struck bodies' modes, each a two-pole resonator driven by that force. A shorter
pulse excites higher modes, which is why a ball click is bright and a cushion dull.
"""

from __future__ import annotations

import argparse
import cmath
import io
import math
import random
import sys
import wave
import zlib
from array import array
from pathlib import Path

RATE = 44100
OUT = Path(__file__).resolve().parent.parent / "game" / "audio"
SEED = 147
PEAK = 10.0 ** (-1.0 / 20.0)
FADE_IN_S = 0.0001
FADE_OUT_S = 0.004
DC_CUTOFF_HZ = 20.0
# The centroid is measured on at most this many samples (the loop's first 1.5 s).
ANALYSIS_SAMPLES = 65536

# (frequency Hz, amplitude decay time s, relative amplitude) of each body's modes.
# A 52.5 mm phenolic resin ball rings in the low kilohertz up past 8 kHz and dies in milliseconds.
BALL_MODES = [(2850.0, 0.0050, 1.0), (4100.0, 0.0036, 0.8), (5450.0, 0.0028, 0.65),
              (6900.0, 0.0021, 0.5), (8600.0, 0.0016, 0.35)]
# An ash shaft about 1.45 m long: first longitudinal mode near c / 2L, damped by the hand and
# bridge, plus a low flexural mode for body.
CUE_MODES = [(300.0, 0.012, 0.25), (1700.0, 0.006, 1.0), (3400.0, 0.004, 0.5)]
RUBBER_MODES = [(175.0, 0.028, 0.7), (240.0, 0.022, 1.0), (330.0, 0.016, 0.8), (460.0, 0.012, 0.5)]
RAIL_MODES = [(780.0, 0.010, 1.0), (1150.0, 0.008, 0.7), (1720.0, 0.006, 0.45)]
# The jaw: short rubber over the wooden fall, struck sharper than a cushion nose.
JAW_MODES = [(380.0, 0.014, 0.6), (610.0, 0.010, 0.7), (1050.0, 0.012, 1.0), (1600.0, 0.009, 0.85),
             (2350.0, 0.006, 0.6), (3100.0, 0.0045, 0.4)]
LEATHER_MODES = [(140.0, 0.040, 1.0), (210.0, 0.030, 0.8), (320.0, 0.022, 0.5)]

Modes = list[tuple[float, float, float]]


def samples(seconds: float) -> int:
    return int(round(seconds * RATE))


def zeros(n: int) -> list[float]:
    return [0.0] * n


def noise(rng: random.Random, n: int) -> list[float]:
    # Uniform rather than Gaussian: no libm in the noise itself, and the filters make it Gaussian.
    return [rng.uniform(-1.0, 1.0) for _ in range(n)]


def pulse(contact_s: float) -> list[float]:
    """A smooth (sin^2) contact force lasting contact_s, normalised to unit area."""
    n = max(2, math.ceil(contact_s * RATE))
    p = []
    for i in range(n):
        t = (i + 0.5) / RATE
        p.append(math.sin(math.pi * t / contact_s) ** 2 if t < contact_s else 0.0)
    total = sum(p)
    return [v / total for v in p]


def radiation(force: list[float]) -> list[float]:
    """Far-field pressure of a rigid body pushed by `force`: its rate of change."""
    padded = force + [0.0]
    return [padded[i] - (padded[i - 1] if i > 0 else 0.0) for i in range(len(padded))]


def ring(drive: list[float], n: int, modes: Modes) -> list[float]:
    out = zeros(n)
    for freq, tau, amp in modes:
        if freq >= 0.45 * RATE:
            continue
        w = 2.0 * math.pi * freq / RATE
        r = math.exp(-1.0 / (tau * RATE))
        a1 = 2.0 * r * math.cos(w)
        a2 = -r * r
        # sin(w) makes the impulse response amp * r^n * sin(w (n + 1)).
        g = amp * math.sin(w)
        y1 = y2 = 0.0
        m = len(drive)
        for i in range(n):
            y = a1 * y1 + a2 * y2 + (g * drive[i] if i < m else 0.0)
            out[i] += y
            y2 = y1
            y1 = y
    return out


def jitter(rng: random.Random, modes: Modes, freq: float, tau: float, amp: float) -> Modes:
    return [(f * (1.0 + rng.uniform(-freq, freq)), t * (1.0 + rng.uniform(-tau, tau)),
             a * (1.0 + rng.uniform(-amp, amp))) for f, t, a in modes]


def biquad(x: list[float], kind: str, f0: float, q: float = 0.7071) -> list[float]:
    """RBJ cookbook lowpass, highpass or bandpass (0 dB peak)."""
    w = 2.0 * math.pi * f0 / RATE
    cw = math.cos(w)
    alpha = math.sin(w) / (2.0 * q)
    if kind == "lowpass":
        b0, b1, b2 = (1.0 - cw) / 2.0, 1.0 - cw, (1.0 - cw) / 2.0
    elif kind == "highpass":
        b0, b1, b2 = (1.0 + cw) / 2.0, -(1.0 + cw), (1.0 + cw) / 2.0
    elif kind == "bandpass":
        b0, b1, b2 = alpha, 0.0, -alpha
    else:
        raise ValueError(kind)
    a0 = 1.0 + alpha
    b0, b1, b2, a1, a2 = b0 / a0, b1 / a0, b2 / a0, -2.0 * cw / a0, (1.0 - alpha) / a0
    out = []
    x1 = x2 = y1 = y2 = 0.0
    for v in x:
        y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        out.append(y)
        x2, x1, y2, y1 = x1, v, y1, y
    return out


def lowpass(x: list[float], f0: float, stages: int) -> list[float]:
    """Cascaded lowpasses: a bandpass alone leaves 6 dB/octave skirts that read as hiss."""
    for _ in range(stages):
        x = biquad(x, "lowpass", f0)
    return x


def sweep_bandpass(x: list[float], f_start: float, f_end: float, q: float, block: int = 16) -> list[float]:
    """A bandpass whose centre glides exponentially from f_start to f_end over x."""
    out = []
    s1 = s2 = 0.0
    n = len(x)
    for start in range(0, n, block):
        f0 = f_start * (f_end / f_start) ** (start / max(n - 1, 1))
        w = 2.0 * math.pi * f0 / RATE
        alpha = math.sin(w) / (2.0 * q)
        a0 = 1.0 + alpha
        b0, b2 = alpha / a0, -alpha / a0
        a1, a2 = -2.0 * math.cos(w) / a0, (1.0 - alpha) / a0
        for v in x[start:start + block]:
            # Transposed direct form II, which tolerates coefficients changing between blocks.
            y = b0 * v + s1
            s1 = -a1 * y + s2
            s2 = b2 * v - a2 * y
            out.append(y)
    return out


def envelope(n: int, attack_s: float, decay_s: float) -> list[float]:
    return [(1.0 - math.exp(-(i / RATE) / attack_s)) * math.exp(-(i / RATE) / decay_s) for i in range(n)]


def times(a: list[float], b: list[float]) -> list[float]:
    return [u * v for u, v in zip(a, b)]


def norm(x: list[float]) -> list[float]:
    peak = max((abs(v) for v in x), default=0.0)
    return [v / peak for v in x] if peak > 0.0 else x


def mix(dst: list[float], src: list[float], gain: float, offset: int = 0) -> None:
    for i, v in enumerate(src):
        if offset + i >= len(dst):
            break
        dst[offset + i] += gain * v


def finish(x: list[float]) -> list[float]:
    """DC blocker, raised-cosine fades, the mean removed exactly, peak at -1 dBFS."""
    r = math.exp(-2.0 * math.pi * DC_CUTOFF_HZ / RATE)
    y = []
    prev_x = prev_y = 0.0
    for v in x:
        prev_y = v - prev_x + r * prev_y
        prev_x = v
        y.append(prev_y)
    n = len(y)
    fade_in = max(1, samples(FADE_IN_S))
    fade_out = max(1, samples(FADE_OUT_S))
    for i in range(fade_in):
        y[i] *= 0.5 - 0.5 * math.cos(math.pi * i / fade_in)
    for i in range(fade_out):
        y[n - 1 - i] *= 0.5 - 0.5 * math.cos(math.pi * i / fade_out)
    # A sin^2 bump carries the correction, so the ends stay at zero.
    mean = sum(y) / n
    bump = [math.sin(math.pi * i / (n - 1)) ** 2 for i in range(n)]
    bump_mean = sum(bump) / n
    y = [v - mean * b / bump_mean for v, b in zip(y, bump)]
    return [v * PEAK for v in norm(y)]


def ball_click(rng: random.Random) -> list[float]:
    n = samples(0.040)
    force = pulse(rng.uniform(0.00011, 0.00016))
    first = jitter(rng, BALL_MODES, 0.03, 0.15, 0.2)
    # No two balls are identical, so each mode rings twice, a little apart, and beats.
    second = [(f * (1.0 + rng.uniform(0.004, 0.015)), t, a * rng.uniform(0.6, 1.0)) for f, t, a in first]
    out = zeros(n)
    mix(out, norm(ring(force, n, first + second)), 1.0)
    mix(out, norm(radiation(force)), 0.6)
    burst_n = samples(0.004)
    burst = times(noise(rng, burst_n), envelope(burst_n, 0.00005, 0.0006))
    mix(out, norm(biquad(biquad(burst, "highpass", 2000.0), "lowpass", 12000.0)), 0.25)
    return finish(out)


def tip(rng: random.Random) -> list[float]:
    n = samples(0.060)
    # Leather is soft: the contact lasts most of a millisecond, so little above 3 kHz is excited.
    force = pulse(rng.uniform(0.00055, 0.00075))
    out = zeros(n)
    mix(out, norm(radiation(force)), 1.0)
    mix(out, norm(ring(force, n, jitter(rng, CUE_MODES, 0.04, 0.15, 0.2))), 0.7)
    mix(out, norm(ring(force, n, jitter(rng, BALL_MODES, 0.03, 0.15, 0.2))), 0.15)
    burst_n = samples(0.006)
    burst = times(noise(rng, burst_n), envelope(burst_n, 0.0001, 0.0015))
    mix(out, norm(biquad(burst, "lowpass", 3000.0)), 0.15)
    return finish(biquad(out, "lowpass", 5000.0))


def cushion(rng: random.Random) -> list[float]:
    n = samples(0.200)
    contact = rng.uniform(0.0025, 0.0035)
    force = pulse(contact)
    out = zeros(n)
    mix(out, norm(radiation(force)), 1.0)
    mix(out, norm(ring(force, n, jitter(rng, RUBBER_MODES, 0.05, 0.15, 0.2))), 0.8)
    # The wooden rail behind the rubber takes a weaker, sharper blow.
    mix(out, norm(ring(pulse(contact * 0.4), n, jitter(rng, RAIL_MODES, 0.05, 0.15, 0.2))), 0.25)
    rub_n = samples(0.050)
    rub = times(noise(rng, rub_n), envelope(rub_n, 0.0003, 0.008))
    mix(out, norm(biquad(rub, "lowpass", 1200.0)), 0.12)
    return finish(out)


def jaw(rng: random.Random) -> list[float]:
    n = samples(0.140)
    force = pulse(rng.uniform(0.0009, 0.0012))
    out = zeros(n)
    mix(out, norm(radiation(force)), 0.8)
    mix(out, norm(ring(force, n, jitter(rng, JAW_MODES, 0.05, 0.15, 0.2))), 1.0)
    mix(out, norm(ring(pulse(0.0004), n, jitter(rng, BALL_MODES, 0.03, 0.15, 0.2))), 0.12)
    knock_n = samples(0.020)
    knock = times(noise(rng, knock_n), envelope(knock_n, 0.0002, 0.003))
    mix(out, norm(biquad(knock, "bandpass", 1500.0, 0.8)), 0.15)
    return finish(out)


def thud(rng: random.Random, n: int, contact_s: float) -> list[float]:
    """The ball landing on the pocket's leather."""
    force = pulse(contact_s)
    out = zeros(n)
    mix(out, norm(radiation(force)), 1.0)
    mix(out, norm(ring(force, n, jitter(rng, LEATHER_MODES, 0.05, 0.15, 0.2))), 0.8)
    slap_n = min(n, samples(0.060))
    slap = times(noise(rng, slap_n), envelope(slap_n, 0.0005, 0.012))
    mix(out, norm(biquad(slap, "lowpass", 900.0)), 0.35)
    return norm(out)


def pocket(rng: random.Random) -> list[float]:
    n = samples(0.450)
    out = zeros(n)
    # It settles like a ball bouncing on leather: each bounce slower by the restitution, and
    # sooner by it too.
    restitution = rng.uniform(0.35, 0.45)
    gap = rng.uniform(0.060, 0.090)
    start, level, contact = 0.0, 1.0, rng.uniform(0.007, 0.010)
    while level > 0.03 and samples(start) < n:
        offset = samples(start)
        mix(out, thud(rng, n - offset, contact), level, offset)
        start += gap
        gap *= restitution
        level *= restitution
        contact = rng.uniform(0.005, 0.007)
    return finish(out)


def miscue(rng: random.Random) -> list[float]:
    n = samples(0.180)
    out = zeros(n)
    force = pulse(rng.uniform(0.0004, 0.0005))
    mix(out, norm(radiation(force)), 0.7)
    mix(out, norm(ring(force, n, jitter(rng, CUE_MODES, 0.04, 0.15, 0.2))), 0.4)
    # The tip skids off: it grabs and slips a few hundred times a second, and the scrape's
    # pitch falls as it slows.
    scrape_n = samples(rng.uniform(0.060, 0.080)) + samples(0.060)
    grabs = zeros(scrape_n)
    t = 0.0
    interval = rng.uniform(0.0022, 0.0030)
    while samples(t) < scrape_n:
        k = samples(t)
        for i in range(k, min(scrape_n, k + samples(0.004))):
            grabs[i] += math.exp(-((i - k) / RATE) / 0.0008)
        interval *= rng.uniform(1.0, 1.08)
        t += interval
    env = envelope(scrape_n, 0.003, 0.030)
    gritty = [e * (0.45 + 0.55 * min(g, 1.0)) for e, g in zip(env, grabs)]
    scrape = lowpass(sweep_bandpass(times(noise(rng, scrape_n), gritty), 3800.0, 2000.0, 1.4), 6000.0, 2)
    scrape = [s + 0.15 * r for s, r in zip(norm(scrape), norm(ring(scrape, scrape_n, BALL_MODES)))]
    mix(out, norm(scrape), 0.9, samples(0.001))
    # The tip leaving the ball's edge.
    mix(out, norm(radiation(pulse(0.0003))), 0.3, samples(rng.uniform(0.025, 0.035)))
    return finish(out)


def roll_loop(rng: random.Random) -> list[float]:
    """A ball rolling on cloth: low noise, swelling with each turn of the ball. Loops seamlessly."""
    n = samples(3.0)
    fade = samples(0.150)
    warmup = samples(0.250)
    total = warmup + n + fade
    rumble = norm(lowpass(biquad(noise(rng, total), "bandpass", 260.0, 0.7), 600.0, 2))
    body = norm(lowpass(biquad(noise(rng, total), "bandpass", 600.0, 1.0), 1200.0, 2))
    hiss = norm(lowpass(biquad(noise(rng, total), "highpass", 1500.0), 3000.0, 2))
    phase_turn = rng.uniform(0.0, 2.0 * math.pi)
    phase_slow = rng.uniform(0.0, 2.0 * math.pi)
    x = []
    for i in range(n + fade):
        j = warmup + i
        # Whole cycles in n samples (6 Hz and 1 Hz over 3 s), so the swell loops too.
        swell = (1.0 + 0.12 * math.sin(2.0 * math.pi * 6.0 * i / RATE + phase_turn)
                 + 0.06 * math.sin(2.0 * math.pi * 1.0 * i / RATE + phase_slow))
        x.append(swell * (rumble[j] + 0.35 * body[j] + 0.04 * hiss[j]))
    # Equal-power crossfade of the run-on into the start: the last sample flows into the first.
    y = x[:n]
    for i in range(fade):
        a = (i + 0.5) / fade
        y[i] = x[i] * math.sin(a * math.pi / 2.0) + x[n + i] * math.cos(a * math.pi / 2.0)
    mean = sum(y) / n
    return [v * PEAK for v in norm([v - mean for v in y])]


RECIPES = {
    **{f"ball_click_{k}": ball_click for k in range(1, 5)},
    **{f"cushion_{k}": cushion for k in range(1, 4)},
    **{f"jaw_{k}": jaw for k in range(1, 3)},
    **{f"pocket_{k}": pocket for k in range(1, 4)},
    **{f"tip_{k}": tip for k in range(1, 4)},
    "miscue": miscue,
    "roll_loop": roll_loop,
}
LOOPS = {"roll_loop"}


def pcm(x: list[float]) -> array:
    return array("h", (max(-32768, min(32767, math.floor(v * 32767.0 + 0.5))) for v in x))


def wav_bytes(data: array) -> bytes:
    if sys.byteorder != "little":
        data = array("h", data)
        data.byteswap()
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(data.tobytes())
    return buffer.getvalue()


def read_wav(data: bytes) -> array:
    with wave.open(io.BytesIO(data), "rb") as w:
        if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != (1, 2, RATE):
            raise ValueError("not 16-bit mono 44.1 kHz")
        frames = array("h", w.readframes(w.getnframes()))
    if sys.byteorder != "little":
        frames.byteswap()
    return frames


def generate(name: str) -> bytes:
    rng = random.Random(SEED * 1_000_003 + zlib.crc32(name.encode()))
    return wav_bytes(pcm(RECIPES[name](rng)))


def build() -> dict[str, bytes]:
    return {name: generate(name) for name in RECIPES}


def check(folder: Path = OUT) -> list[str]:
    """What differs between the generated sounds and the WAVs in `folder`; empty when they match."""
    problems = []
    for name, data in build().items():
        path = folder / f"{name}.wav"
        if not path.exists():
            problems.append(f"{path.name}: missing")
        elif path.read_bytes() != data:
            problems.append(f"{path.name}: differs from what make_sounds.py generates")
    stray = sorted(p.name for p in folder.glob("*.wav") if p.stem not in RECIPES)
    problems += [f"{name}: not generated by make_sounds.py" for name in stray]
    return problems


def fft(x: list[float]) -> list[complex]:
    n = 1
    while n < len(x):
        n *= 2
    a = [complex(v) for v in x] + [0j] * (n - len(x))
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    size = 2
    while size <= n:
        half = size // 2
        twiddles = [cmath.exp(-2j * math.pi * k / size) for k in range(half)]
        for start in range(0, n, size):
            for k in range(half):
                u = a[start + k]
                v = a[start + k + half] * twiddles[k]
                a[start + k] = u + v
                a[start + k + half] = u - v
        size *= 2
    return a


def spectral_centroid(frames: array) -> float:
    """Magnitude-weighted mean frequency, Hz."""
    spectrum = fft([v / 32768.0 for v in frames[:ANALYSIS_SAMPLES]])
    n = len(spectrum)
    mags = [abs(spectrum[k]) for k in range(n // 2 + 1)]
    total = sum(mags)
    return sum(k * RATE / n * m for k, m in enumerate(mags)) / total if total > 0.0 else 0.0


def analyse(frames: array) -> dict[str, float]:
    peak = max(abs(v) for v in frames) / 32767.0
    rms = math.sqrt(sum(v * v for v in frames) / len(frames)) / 32767.0
    return {
        "length_ms": len(frames) * 1000.0 / RATE,
        "peak_dbfs": 20.0 * math.log10(peak),
        "rms_dbfs": 20.0 * math.log10(rms),
        "centroid_hz": spectral_centroid(frames),
        "mean": sum(frames) / len(frames) / 32767.0,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="verify the committed WAVs match")
    parser.add_argument("--report", action="store_true", help="measure the committed WAVs")
    args = parser.parse_args()
    if args.check:
        problems = check()
        for problem in problems:
            print(problem)
        print("sounds match" if not problems else f"{len(problems)} sound(s) out of date: run tools/make_sounds.py")
        return 1 if problems else 0
    if args.report:
        print(f"{'sound':<14}{'length ms':>10}{'peak dBFS':>11}{'rms dBFS':>10}{'centroid Hz':>13}")
        for name in RECIPES:
            m = analyse(read_wav((OUT / f"{name}.wav").read_bytes()))
            print(f"{name:<14}{m['length_ms']:>10.1f}{m['peak_dbfs']:>11.2f}{m['rms_dbfs']:>10.1f}{m['centroid_hz']:>13.0f}")
        return 0
    OUT.mkdir(parents=True, exist_ok=True)
    for name, data in build().items():
        (OUT / f"{name}.wav").write_bytes(data)
    print(f"wrote {len(RECIPES)} sounds to {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
