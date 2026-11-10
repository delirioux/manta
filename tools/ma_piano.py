#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Manta's PIANO: six notes of a Yamaha upright from the Versilian Community Sample Library (VCSL, CC0 1.0,
github.com/sgossner/VCSL, "Chordophones/Zithers/Upright Piano, Yamaha/Sustains"), its loudest layer (vl3), made into
the percussive dance-hall upright of the montuno: summed to mono, cut short (a montuno does not need the long tail), a
high-pass at 110 Hz, a little high shelf and bite, a touch less body, a light firmness on the attack, normalised;
written as 16-bit mono WAVs at 44.1 kHz to assets/samples-cc0/PIANO/ (tools/gen_samples.py stores them at 22.05 kHz).

    python3 tools/ma_piano.py VCSL_DIR      VCSL_DIR: a checkout of VCSL with that folder (a sparse checkout of it,
                                            or the six files, is enough)

The same source gives the same bytes. The results stay CC0 (assets/samples-cc0/ATTRIBUTION.txt).
"""
import struct
import sys
import wave
from pathlib import Path

import numpy as np

SR = 44100
OUT = Path(__file__).resolve().parents[1] / "assets" / "samples-cc0" / "PIANO"
FOLDER = "Chordophones/Zithers/Upright Piano, Yamaha/Sustains"
# the notes (VCSL's names, C3 = middle C) and how long each is kept (s): the montuno's and the solo's range
NOTES = [("G1", 1.0), ("C2", 1.0), ("G2", 0.95), ("C3", 0.9), ("G3", 0.85), ("C4", 0.8)]


def read(path):
    with wave.open(str(path)) as w:
        n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    assert sw in (2, 3) and sr == SR, (path, sw, sr)
    if sw == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    else:                                           # 24-bit (the upright's files)
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | b[:, 1] << 8 | b[:, 2] << 16
        x = np.where(v >= 1 << 23, v - (1 << 24), v).astype(np.float64) / float(1 << 23)
    x = x.reshape(-1, ch).mean(axis=1)
    return x


def biquad(x, b, a):
    y = np.zeros_like(x)
    x1 = x2 = y1 = y2 = 0.0
    for i, v in enumerate(x):
        o = b[0] * v + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2
        x2, x1, y2, y1 = x1, v, y1, o
        y[i] = o
    return y


def shelf_high(x, fc, db):
    """RBJ high shelf, slope 1"""
    A = 10 ** (db / 40)
    w = 2 * np.pi * fc / SR
    c, s = np.cos(w), np.sin(w)
    al = s / 2 * np.sqrt(2)
    b = [A * ((A + 1) + (A - 1) * c + 2 * np.sqrt(A) * al), -2 * A * ((A - 1) + (A + 1) * c),
         A * ((A + 1) + (A - 1) * c - 2 * np.sqrt(A) * al)]
    a = [(A + 1) - (A - 1) * c + 2 * np.sqrt(A) * al, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - 2 * np.sqrt(A) * al]
    return biquad(x, [v / a[0] for v in b], [1.0, a[1] / a[0], a[2] / a[0]])


def peak_eq(x, fc, q, db):
    A = 10 ** (db / 40)
    w = 2 * np.pi * fc / SR
    al = np.sin(w) / (2 * q)
    c = np.cos(w)
    b = [1 + al * A, -2 * c, 1 - al * A]
    a = [1 + al / A, -2 * c, 1 - al / A]
    return biquad(x, [v / a[0] for v in b], [1.0, a[1] / a[0], a[2] / a[0]])


def high_pass(x, fc, q):
    """RBJ high-pass"""
    w = 2 * np.pi * fc / SR
    al = np.sin(w) / (2 * q)
    c = np.cos(w)
    b = [(1 + c) / 2, -(1 + c), (1 + c) / 2]
    a = [1 + al, -2 * c, 1 - al]
    return biquad(x, [v / a[0] for v in b], [1.0, a[1] / a[0], a[2] / a[0]])


def make(src, keep):
    x = read(src)
    on = int(np.argmax(np.abs(x) > 0.02 * np.max(np.abs(x))))   # the hammer: start just before it
    x = x[max(0, on - 32):]
    x = x[:int(keep * SR)]
    x = x / np.max(np.abs(x))
    x = shelf_high(x, 2500, 3.0)                    # a little brighter: the montuno has to cut through the horns
    x = peak_eq(x, 4000, 1.0, 1.5)                  # the hammer's bite
    x = high_pass(x, 110, 0.7)                      # the low end the tumbao's
    x = peak_eq(x, 250, 0.9, -1.5)                  # a touch less body: percussive, a dance-hall upright
    x = np.tanh(x * 1.2) / np.tanh(1.2)             # just a little firmness on the attack
    n = len(x)
    fade = int(0.12 * SR)                           # a raised-cosine fade over the last 120 ms
    x[n - fade:] *= 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, fade))
    return x / np.max(np.abs(x)) * 0.98


def write_wav(path, x):
    data = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2").tobytes()
    fmt = struct.pack("<HHIIHH", 1, 1, SR, SR * 2, 2, 16)
    body = b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
    path.write_bytes(b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WAVE" + body)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    folder = Path(sys.argv[1]) / FOLDER
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("*.wav"):
        old.unlink()
    for k, (note, keep) in enumerate(NOTES):
        src = folder / f"Upright1_Sus_{note}_vl3_rr1.wav"
        x = make(src, keep)
        write_wav(OUT / f"{k:02d}_Upright_{note}_vl3.wav", x)
        print(f"ma_piano: {k:02d}_Upright_{note}_vl3.wav  {len(x) / SR * 1000:.0f} ms  <-  VCSL {src.name}")


if __name__ == "__main__":
    main()
