#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Manta's percussion kit: one-shots from the Versilian Community Sample Library (VCSL, CC0 1.0,
github.com/sgossner/VCSL), each summed to mono, started at its onset, cut to the length the music needs with a short
raised-cosine fade, tuned where a drum stands in for another (the timbales: a rope-tension snare with its snares off,
tuned up), normalised; written as 16-bit mono WAVs at 44.1 kHz to assets/samples-cc0/DRUMS/ in the kit's key order
(tools/gen_samples.py stores the skins at 22,050 Hz and the metal, wood and shakers, whose air lives higher, at
32 kHz: a name ending in _air). The kick (soft and round) and the washboard (scraped and tapped) are Manta's own, synthesized.

    python3 tools/ma_perc.py VCSL_DIR       VCSL_DIR: a checkout of VCSL with the folders below (a sparse checkout
                                            of them is enough)
    python3 tools/ma_perc.py --fetch DIR    fetch only the files used, into DIR (git, partial clone)

The same source gives the same bytes. The results stay CC0 (assets/samples-cc0/ATTRIBUTION.txt).
"""
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

SR = 44100
OUT = Path(__file__).resolve().parents[1] / "assets" / "samples-cc0" / "DRUMS"
M = "Membranophones/Struck Membranophones/"
I = "Idiophones/Struck Idiophones/"
# the kit, in key order (firmware/src/ma_sounds.h MAS_KIT_*): name, VCSL file (or None: synthesized), seconds kept,
# semitones of tuning (the timbales: the head's pitch in Hz, see timbal), gain (dB) before normalising the kit
KIT = [
    ("quinto", M + "Conga/Quinto_HitN_v2_rr1_Sum.wav", 0.40, 0, 0),
    ("conga_open", M + "Conga/Conga_HitN_v2_rr1_Sum.wav", 0.45, 0, 0),
    ("conga_mute", M + "Conga/Conga_HitFM_v2_rr1_Sum.wav", 0.22, 0, -1),
    ("tumba", M + "Conga/Tumba_HitN_v3_rr1_Sum.wav", 0.60, 0, 0),
    ("bongo_hi", M + "Bongos/BongoH_Hit1_v2_rr1_Mid.wav", 0.28, 0, -1),
    ("bongo_lo", M + "Bongos/BongoL_Hit1_v2_rr1_Mid.wav", 0.32, 0, -1),
    ("bongo_mute", M + "Bongos/BongoH_HitMuted1_v2_rr1_Mid.wav", 0.16, 0, -2),
    ("timbal_hi", M + "Snare Drum, Rope Tension/Low/RopeSnare_low_ns_Main_vl3_rr1.wav", 0.45, 410, 0),
    ("timbal_lo", M + "Snare Drum, Rope Tension/Low/RopeSnare_low_ns_Main_vl3_rr2.wav", 0.50, 300, 0),
    ("timbal_rim", M + "Tom 1/TomH_rimS_v4_rr1_Mid.wav", 0.30, 2, 0),
    ("paila_air", M + "Snare Drum, Rope Tension/RopeSnare_stick_Main_vl1_rr1.wav", 0.12, 3, -3),
    ("bell_mambo_air", I + "Cowbells/Cowbell1_Normal_v3_rr1_Mid.wav", 0.45, 0, -2),
    ("bell_bongo_air", I + "Cowbells/Cowbell2_Normal_v3_rr1_Mid.wav", 0.35, 0, -3),
    ("bell_mute_air", I + "Cowbells/Cowbell1_Muted_v3_rr1_Mid.wav", 0.14, 0, -3),
    ("agogo_hi_air", I + "Agogo Bells/Agogo_High_v2_rr1_Mid.wav", 0.45, 0, -4),
    ("agogo_lo_air", I + "Agogo Bells/Agogo_Low_v2_rr1_Mid.wav", 0.45, 0, -4),
    ("clave_air", I + "Claves/Claves1_Hit_v2_rr1_Mid.wav", 0.22, 0, -1),
    ("woodblock_air", I + "Woodblock/wood_click_f_rr1.wav", 0.14, 0, -3),
    ("guiro_short_air", I + "Guiro/Guiro_Hit_rr1_Mid.wav", 0.22, 0, -4),
    ("guiro_long_air", I + "Guiro/Guiro_Med_rr1_Mid.wav", 0.55, 0, -4),
    ("shaker_fwd_air", I + "Shaker, Small/Mid_ShakerHighFaster_Down_rr1.wav", 0.18, 0, -6),
    ("shaker_back_air", I + "Shaker, Small/Mid_ShakerHighFaster_Up_rr1.wav", 0.18, 0, -7),
    ("cabasa_air", I + "Cabasa/Cabasa1_Hit_rr1_Mid.wav", 0.15, 0, -6),
    ("triangle_air", I + "Triangles/Triangle1_Hit_v1_rr1_Mid.wav", 0.45, 0, -8),
    ("triangle_mute_air", I + "Triangles/Triangle1_HitM_v1_rr2_Mid.wav", 0.18, 0, -8),
    ("tom_hi", M + "Tom 1/Stick/TomH_HitS_v3_rr1_Mid.wav", 0.55, 0, -1),
    ("tom_lo", M + "Tom 2/Stick/TomL_HitS_v3_rr1_Mid.wav", 0.65, 0, -1),
    ("kick", None, 0.45, 0, -1),
    ("wash_scrape_air", None, 0.30, 0, -4),        # the washboard (and the merengue's guira): thimbles scraped down
    ("wash_tap_air", None, 0.10, 0, -4),           # its ridges, and a single tap of a thimble on them
]


def read(path):
    with wave.open(str(path)) as w:
        n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    if sw == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif sw == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | b[:, 1] << 8 | b[:, 2] << 16
        x = np.where(v >= 1 << 23, v - (1 << 24), v).astype(np.float64) / float(1 << 23)
    else:
        raise SystemExit(f"{path}: {sw * 8}-bit samples")
    return sr, x.reshape(-1, ch).mean(axis=1)


def resample(x, sr_in, ratio_up):
    """x at sr_in to SR, `ratio_up` semitones higher (read faster): linear interpolation on a lightly
    band-limited signal (the one-shots are short; this keeps the script free of scipy)"""
    step = sr_in / SR * 2.0 ** (ratio_up / 12.0)
    if step > 1.0:                                   # reading faster: soften the top first
        k = int(np.ceil(step)) * 2 + 1
        w = np.hanning(k + 2)[1:-1]
        x = np.convolve(x, w / w.sum(), mode="same")
    t = np.arange(0.0, len(x) - 1, step)
    i = t.astype(np.int64)
    f = t - i
    return x[i] * (1 - f) + x[i + 1] * f


def onset(x, thr=0.02):
    a = np.abs(x)
    hit = np.nonzero(a > thr * a.max())[0]
    return max(0, int(hit[0]) - 8) if len(hit) else 0


def kick(secs):
    """a soft, round kick: a sine falling from 110 to 52 Hz, a short click, a smooth decay (seeded: always the same)"""
    n = int(secs * SR)
    t = np.arange(n) / SR
    f = 52.0 + 58.0 * np.exp(-t / 0.035)
    ph = 2 * np.pi * np.cumsum(f) / SR
    body = np.sin(ph) * np.exp(-t / 0.16)
    rng = np.random.default_rng(1)
    click = rng.standard_normal(n) * np.exp(-t / 0.003) * 0.25
    return np.tanh(1.4 * (body + click))


def timbal(stick, f0, secs):
    """a timbal: VCSL has none, so the stick's attack of a rope-tension snare with its snares off (the crack of
    stick on head, high-passed) over a tuned head ringing in its first modes (f0 and the membrane's 1.59, 2.14,
    2.30, 2.65 f0) and the thin metal shell's ring around 2.9 kHz, each mode with its own decay"""
    n = int(secs * SR)
    t = np.arange(n) / SR
    hp = stick[:n] - np.convolve(stick[:n], np.ones(24) / 24, mode="same")
    att = np.zeros(n)
    att[:len(hp)] = hp[:n] * np.exp(-t[:len(hp)] / 0.012)
    head = sum(a * np.sin(2 * np.pi * f0 * r * t + r) * np.exp(-t / d)
               for r, a, d in ((1.0, 1.0, 0.16), (1.59, 0.55, 0.10), (2.14, 0.35, 0.08), (2.30, 0.30, 0.07),
                               (2.65, 0.18, 0.05)))
    bend = np.exp(-t / 0.02) * 0.06                      # the head's pitch drops a touch just after the hit
    head *= 1.0 + bend
    shell = sum(np.sin(2 * np.pi * f * t) for f in (2870.0, 3130.0, 4410.0)) * np.exp(-t / 0.09) * 0.12
    x = head / np.abs(head).max() + 1.6 * att / max(1e-9, np.abs(att).max()) * 0.5 + shell
    return np.tanh(1.2 * x)


def washboard(secs, scrape):
    """a washboard, synthesized (VCSL has none): a metal sheet of ridges struck by thimbles. A scrape is a train of
    ticks, one per ridge (about 55 a second, a little uneven as a hand is), each a burst of noise through the sheet's
    bright resonances; a tap is one tick with the sheet ringing a moment longer. Seeded: always the same"""
    n = int(secs * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(7 if scrape else 8)
    ticks = np.zeros(n)
    if scrape:
        pos, k = 0.004, 0
        while pos < secs - 0.02:
            i = int(pos * SR)
            ticks[i] = (0.55 + 0.45 * rng.random()) * (1.0 - 0.5 * pos / secs)
            pos += (1.0 / 55.0) * (0.85 + 0.3 * rng.random())
            k += 1
    else:
        ticks[int(0.002 * SR)] = 1.0
    noise = rng.standard_normal(n)
    burst = np.convolve(ticks, np.exp(-np.arange(int(0.004 * SR)) / (0.0012 * SR)), mode="full")[:n]
    x = noise * burst
    ring = sum(np.sin(2 * np.pi * f * t + rng.random() * 6.28) * a for f, a in
               ((3150.0, 0.5), (4870.0, 0.4), (6420.0, 0.35), (8930.0, 0.25)))
    env = np.convolve(ticks, np.exp(-np.arange(int(0.03 * SR)) / ((0.008 if scrape else 0.02) * SR)), mode="full")[:n]
    x = x + 0.35 * ring * env
    x = x - np.convolve(x, np.ones(6) / 6, mode="same")        # the sheet's thin, bright sound: no low end
    return x / max(1e-9, np.abs(x).max())


def shape(x, secs):
    n = min(len(x), int(secs * SR))
    x = x[:n].copy()
    fade = min(n // 3, int(0.03 * SR))
    if fade > 1:
        x[n - fade:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(1, fade + 1) / fade)
    return x


def write(path, x):
    y = np.clip(np.round(x * 32767.0), -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(y.tobytes())


def fetch(dest):
    dest = Path(dest)
    repo = dest / "VCSL"
    if not (repo / ".git").exists():
        subprocess.run(["git", "clone", "--depth", "1", "--filter=blob:none", "--no-checkout",
                        "https://github.com/sgossner/VCSL", str(repo)], check=True)
    for _, rel, *_ in KIT:
        if rel:
            out = repo / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            data = subprocess.run(["git", "-C", str(repo), "show", "HEAD:" + rel], check=True,
                                  capture_output=True).stdout
            out.write_bytes(data)
    print("fetched into", repo)


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--fetch":
        fetch(sys.argv[2])
        return
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    src = Path(sys.argv[1])
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("*.wav"):
        old.unlink()
    shots = []
    for name, rel, secs, tune, gain in KIT:
        if rel is None and name.startswith("wash_"):
            x = washboard(secs, "scrape" in name)
        elif rel is None:
            x = kick(secs)
        else:
            sr, x = read(src / rel)
            x = x - x.mean()
            x = x[onset(x):]
            if name.startswith("timbal_") and tune > 24:
                x = timbal(resample(x, sr, 0), float(tune), secs)
            else:
                x = resample(x, sr, tune)
        x = shape(x, secs)
        x = x / max(1e-9, np.abs(x).max()) * 10 ** (gain / 20.0)
        shots.append((name, x))
    pk = max(np.abs(x).max() for _, x in shots)
    total = 0.0
    for k, (name, x) in enumerate(shots):
        write(OUT / f"{k:02d}_{name}.wav", x / pk * 0.89)
        total += len(x) / SR
    print(f"ma_perc: {len(shots)} one-shots, {total:.2f} s -> {OUT}")


if __name__ == "__main__":
    main()
