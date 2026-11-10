#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Manta's brass: the trumpet and the trombone of the HORNS, from the Versilian Studios Chamber Orchestra 2 Community
Edition (VSCO 2 CE, CC0 1.0, github.com/sgossner/VSCO-2-CE, "Brass/Trumpet/sus" and "Brass/Tenor Trombone/sus"),
their loudest layer, made into a Latin section's bright, punchy brass: summed to mono, the floor's rumble cut, a
little presence for the bite, a short accent on the attack (the tongue), the sustain levelled, and cut to the attack
and enough of the steady tone for tools/gen_samples.py to loop (it loops each note over whole periods of its pitch).
Written as 16-bit mono WAVs at 44.1 kHz to assets/samples-cc0/BRASS/ (stored at 22.05 kHz): one sample set, the
trombone below the trumpet, so a section's lower voice an octave down is the trombone.

    python3 tools/ma_brass.py VSCO_DIR      VSCO_DIR: a checkout of VSCO 2 CE (or any folder holding the eleven files)

VSCO names its notes with C3 as middle C. The same source gives the same bytes. The results stay CC0
(assets/samples-cc0/ATTRIBUTION.txt).
"""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ma_piano import SR, high_pass, peak_eq, read, shelf_high, write_wav  # noqa: E402

OUT = Path(__file__).resolve().parents[1] / "assets" / "samples-cc0" / "BRASS"
KEEP = 0.62                                         # s from the attack (gen_samples.py keeps 0.55 of it)
# (prefix, VSCO file, high-pass Hz, the accent dB): the trombone below, the trumpet above (VSCO's names, C3 = middle C)
NOTES = [("TBN", "tenortbn_sus_F1_v3_1.wav", 45, 3.0),
         ("TBN", "tenortbn_sus_A#1_v3_1.wav", 50, 3.0),
         ("TBN", "tenortbn_sus_D2_v3_1.wav", 60, 3.0),
         ("TBN", "tenortbn_sus_F2_v3_1.wav", 70, 2.5),
         ("TBN", "tenortbn_sus_C3_v3_1.wav", 90, 2.5),
         ("TPT", "Sum_SHTrumpet_sus_D#3_v3_rr1.wav", 140, 2.5),
         ("TPT", "Sum_SHTrumpet_sus_G3_v3_rr1.wav", 160, 2.0),
         ("TPT", "Sum_SHTrumpet_sus_A#3_v3_rr1.wav", 180, 2.0),
         ("TPT", "Sum_SHTrumpet_sus_D4_v3_rr1.wav", 220, 1.5),
         ("TPT", "Sum_SHTrumpet_sus_F4_v3_rr1.wav", 260, 1.5),
         ("TPT", "Sum_SHTrumpet_sus_A4_v3_rr1.wav", 300, 1.0)]


def note_name(fname):
    """'..._sus_A#3_...' -> 'A#3'"""
    return fname.split("_sus_")[1].split("_")[0]


def find(root, name):
    hit = next(iter(sorted(Path(root).rglob(name))), None)
    if not hit:
        sys.exit(f"ma_brass: {name} not found under {root}")
    return hit


def make(src, hp, accent_db, trumpet):
    x = read(src)
    on = int(np.argmax(np.abs(x) > 0.03 * np.max(np.abs(x))))   # the tongue: start just before it
    x = x[max(0, on - 64):]
    x = x[:int(KEEP * SR)]
    x = high_pass(x, hp, 0.7)                       # the room's rumble and the breath's thump
    x = high_pass(x, hp, 0.7)
    if trumpet:
        x = peak_eq(x, 2800, 0.9, 2.0)              # the bite a salsa trumpet section has
        x = shelf_high(x, 7000, 1.0)
    else:
        x = peak_eq(x, 1400, 0.9, 2.5)              # the trombone's blare, so it speaks under the trumpets
        x = peak_eq(x, 250, 1.0, -1.5)              # less of its woolly body
    # the swell evened: a note that grows over its first 200 ms (VSCO's players ease in) brought up to its sustain
    # at once, as a section tongues it; a 10 ms envelope, at most +8 dB, from the first 15 ms (the transient's own)
    lvl = np.sqrt(np.mean(x[int(0.25 * SR):int(0.55 * SR)] ** 2))
    win = int(0.010 * SR)
    env = np.sqrt(np.convolve(x ** 2, np.ones(win) / win, mode="same")) + 1e-6
    g = np.clip(lvl / env, 1.0, 2.5)
    t = np.arange(len(x)) / SR
    reach = np.clip((t - 0.015) / 0.02, 0, 1) * np.clip((0.30 - t) / 0.08, 0, 1)   # 15..35 ms in, 220..300 ms out
    g = np.convolve(g, np.ones(win) / win, mode="same")
    x = x * (1 + (g - 1) * reach)
    # the accent: a few dB on the attack, gone in ~70 ms
    x = x * 10 ** (accent_db * np.exp(-t / 0.035) / 20)
    steady = x[int(0.18 * SR):int(0.55 * SR)]       # the sustain levelled to one loudness
    x = x / np.sqrt(np.mean(steady ** 2)) * 0.25
    x = np.tanh(x * 1.1) / np.tanh(1.1)             # the accent's peaks a little rounded
    fade = int(0.004 * SR)                          # (the start's 4 ms in)
    x[:fade] *= np.linspace(0, 1, fade)
    return x / np.max(np.abs(x)) * 0.98


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("*.wav"):
        old.unlink()
    for k, (pre, fname, hp, accent) in enumerate(NOTES):
        src = find(sys.argv[1], fname)
        x = make(src, hp, accent, pre == "TPT")
        out = f"{k:02d}_{pre}_{note_name(fname)}.wav"
        write_wav(OUT / out, x)
        print(f"ma_brass: {out}  {len(x) / SR * 1000:.0f} ms  <-  VSCO 2 CE {src.name}")


if __name__ == "__main__":
    main()
