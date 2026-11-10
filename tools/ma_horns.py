#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Manta's sampled horns, three sets, each its loudest (or fullest) layer made into a Latin band's horn: summed to
mono, the floor's rumble cut, a little presence for the bite, the swell of the player's ease-in evened and an accent on
the attack (the tongue, the bow), the sustain levelled, cut to the attack and enough of the steady tone for
tools/gen_samples.py to loop (whole periods of the pitch; the violins' over their vibrato). Written as 16-bit mono WAVs
at the source's rate to assets/samples-cc0/SET/ (gen_samples.py stores BRASS at 22.05 kHz, SAX and VIOLINS at 16 kHz).

  BRASS    the trumpet and the trombone of the Versilian Studios Chamber Orchestra 2 Community Edition (VSCO 2 CE,
           CC0 1.0, github.com/sgossner/VSCO-2-CE, "Brass/Trumpet/sus", "Brass/Tenor Trombone/sus"): one set, the
           trombone below the trumpet, so a section's lower voice an octave down is the trombone
  SAX      the tenor saxophone of the Versilian Community Sample Library (VCSL, CC0 1.0, github.com/sgossner/VCSL,
           "Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato"): the merengue's and the bossa's
  VIOLINS  VSCO 2 CE's violin section ("Strings/Violin Section/susVib"): the charanga's and the tango's

    python3 tools/ma_horns.py SRC_DIR      SRC_DIR: checkouts of VSCO 2 CE and VCSL (or any folder holding the files)

VSCO and VCSL name their notes with C3 as middle C. The same source gives the same bytes. The results stay CC0
(assets/samples-cc0/ATTRIBUTION.txt).
"""
import re
import struct
import sys
import wave
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ma_piano  # noqa: E402

CC0 = Path(__file__).resolve().parents[1] / "assets" / "samples-cc0"
# set -> (how long from the attack (s), the notes: (prefix, file, high-pass Hz, accent dB, the voice))
# voice: "tpt" bright brass, "tbn" the trombone's blare, "sax" the reed's buzz, "vln" the section's sheen
SETS = {
    "BRASS": (0.62, [("TBN", "tenortbn_sus_F1_v3_1.wav", 45, 3.0, "tbn"),
                     ("TBN", "tenortbn_sus_A#1_v3_1.wav", 50, 3.0, "tbn"),
                     ("TBN", "tenortbn_sus_D2_v3_1.wav", 60, 3.0, "tbn"),
                     ("TBN", "tenortbn_sus_F2_v3_1.wav", 70, 2.5, "tbn"),
                     ("TBN", "tenortbn_sus_C3_v3_1.wav", 90, 2.5, "tbn"),
                     ("TPT", "Sum_SHTrumpet_sus_D#3_v3_rr1.wav", 140, 2.5, "tpt"),
                     ("TPT", "Sum_SHTrumpet_sus_G3_v3_rr1.wav", 160, 2.0, "tpt"),
                     ("TPT", "Sum_SHTrumpet_sus_A#3_v3_rr1.wav", 180, 2.0, "tpt"),
                     ("TPT", "Sum_SHTrumpet_sus_D4_v3_rr1.wav", 220, 1.5, "tpt"),
                     ("TPT", "Sum_SHTrumpet_sus_F4_v3_rr1.wav", 260, 1.5, "tpt"),
                     ("TPT", "Sum_SHTrumpet_sus_A4_v3_rr1.wav", 300, 1.0, "tpt")]),
    "SAX": (0.52, [("SAX", "BrettTenor_NV_Main_D3_vl3_rr1.wav", 110, 3.0, "sax"),
                   ("SAX", "BrettTenor_NV_Main_F#3_vl3_rr1.wav", 130, 3.0, "sax"),
                   ("SAX", "BrettTenor_NV_Main_A#3_vl3_rr1.wav", 150, 2.5, "sax"),
                   ("SAX", "BrettTenor_NV_Main_E4_vl3_rr1.wav", 180, 2.0, "sax"),
                   ("SAX", "BrettTenor_NV_Main_G#4_vl3_rr1.wav", 200, 2.0, "sax")]),
    "VIOLINS": (0.58, [("VLN", "VlnEns_susVib_D3_v2.wav", 180, 2.0, "vln"),
                       ("VLN", "VlnEns_susVib_A3_v2.wav", 220, 2.0, "vln"),
                       ("VLN", "VlnEns_susVib_E4_v2.wav", 260, 1.5, "vln"),
                       ("VLN", "VlnEns_susVib_B4_v2.wav", 300, 1.5, "vln")]),
}


def note_name(fname):
    """'..._sus_A#3_...' / '..._Main_A#3_...' / '..._susVib_A#3_...' -> 'A#3'"""
    m = re.search(r"_([A-G]#?\d)_", fname)
    return m.group(1)


def find(root, name):
    hit = next(iter(sorted(Path(root).rglob(name))), None)
    if not hit:
        sys.exit(f"ma_horns: {name} not found under {root}")
    return hit


def read(path):
    """-> (mono float samples, rate): 16- or 24-bit"""
    with wave.open(str(path)) as w:
        n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    if sw == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    else:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | b[:, 1] << 8 | b[:, 2] << 16
        x = np.where(v >= 1 << 23, v - (1 << 24), v).astype(np.float64) / float(1 << 23)
    return x.reshape(-1, ch).mean(axis=1), sr


def write_wav(path, x, sr):
    data = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2").tobytes()
    fmt = struct.pack("<HHIIHH", 1, 1, sr, sr * 2, 2, 16)
    body = b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
    path.write_bytes(b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WAVE" + body)


def make(src, keep, hp, accent_db, voice):
    x, sr = read(src)
    ma_piano.SR = sr                                # (the filters are ma_piano's, at this file's rate)
    on = int(np.argmax(np.abs(x) > 0.03 * np.max(np.abs(x))))   # the tongue, the bow: start just before it
    x = x[max(0, on - 64):]
    x = x[:int(keep * sr)]
    x = ma_piano.high_pass(x, hp, 0.7)              # the room's rumble and the breath's thump
    x = ma_piano.high_pass(x, hp, 0.7)
    if voice == "tpt":
        x = ma_piano.peak_eq(x, 2800, 0.9, 2.0)     # the bite a salsa trumpet section has
        x = ma_piano.shelf_high(x, 7000, 1.0)
    elif voice == "tbn":
        x = ma_piano.peak_eq(x, 1400, 0.9, 2.5)     # the trombone's blare, so it speaks under the trumpets
        x = ma_piano.peak_eq(x, 250, 1.0, -1.5)     # less of its woolly body
    elif voice == "sax":
        x = ma_piano.peak_eq(x, 1800, 0.8, 3.0)     # the reed's edge: the merengue's sax cuts
        x = ma_piano.peak_eq(x, 300, 1.0, -1.0)
    else:
        x = ma_piano.peak_eq(x, 3200, 0.8, 1.5)     # the bows' sheen
        x = ma_piano.peak_eq(x, 500, 1.0, -1.5)     # less of the section's boxiness
    # the swell evened: a note that grows over its first 200 ms (the players ease in) brought up to its sustain at
    # once, as a section tongues or bows it; a 10 ms envelope, at most +8 dB, from the first 15 ms (the transient's own)
    lvl = np.sqrt(np.mean(x[int(0.25 * sr):int(0.5 * sr)] ** 2))
    win = int(0.010 * sr)
    env = np.sqrt(np.convolve(x ** 2, np.ones(win) / win, mode="same")) + 1e-6
    g = np.clip(lvl / env, 1.0, 2.5)
    t = np.arange(len(x)) / sr
    reach = np.clip((t - 0.015) / 0.02, 0, 1) * np.clip((0.30 - t) / 0.08, 0, 1)   # 15..35 ms in, 220..300 ms out
    g = np.convolve(g, np.ones(win) / win, mode="same")
    x = x * (1 + (g - 1) * reach)
    x = x * 10 ** (accent_db * np.exp(-t / 0.035) / 20)    # the accent: a few dB on the attack, gone in ~70 ms
    steady = x[int(0.18 * sr):int(0.5 * sr)]        # the sustain levelled to one loudness
    x = x / np.sqrt(np.mean(steady ** 2)) * 0.25
    x = np.tanh(x * 1.1) / np.tanh(1.1)             # the accent's peaks a little rounded
    fade = int(0.004 * sr)                          # (the start's 4 ms in)
    x[:fade] *= np.linspace(0, 1, fade)
    return x / np.max(np.abs(x)) * 0.98, sr


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    for setname, (keep, notes) in SETS.items():
        out_dir = CC0 / setname
        out_dir.mkdir(parents=True, exist_ok=True)
        for old in out_dir.glob("*.wav"):
            old.unlink()
        for k, (pre, fname, hp, accent, voice) in enumerate(notes):
            src = find(sys.argv[1], fname)
            x, sr = make(src, keep, hp, accent, voice)
            out = f"{k:02d}_{pre}_{note_name(fname)}.wav"
            write_wav(out_dir / out, x, sr)
            print(f"ma_horns: {setname}/{out}  {len(x) / sr * 1000:.0f} ms  <-  {src.name}")


if __name__ == "__main__":
    main()
