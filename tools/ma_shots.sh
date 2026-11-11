#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The README's animation and the new pages' screenshots, from the firmware itself in the browser emulator (the scopes
# show the real sound). Needs the emulator built (web/emu/ma_build.sh), Node and Python with Pillow.
#   tools/ma_shots.sh [build/ma_emu/manta.wasm]
set -eu
WASM=${1:-build/ma_emu/manta.wasm}
OUT=build/ma_shots
rm -rf "$OUT"
node web/emu/ma_shots.mjs "$WASM" "$OUT"
python3 - "$OUT" <<'PY'
import glob, sys
from PIL import Image
out = sys.argv[1]
fr = [Image.open(p).convert("RGB") for p in sorted(glob.glob(out + "/anim_*.ppm"))]
pal = fr[0].quantize(colors=96, method=Image.Quantize.MEDIANCUT)
fr = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in fr]
fr[0].save("docs/manta-play.gif", save_all=True, append_images=fr[1:], duration=83, loop=0, optimize=True)
for n in ("scopes", "piano", "piano_edit", "bass", "horns", "perc"):
    Image.open(f"{out}/{n}.ppm").save(f"docs/screens/{n}.png")
print(f"ma_shots: docs/manta-play.gif ({len(fr)} frames), docs/screens: scopes piano piano_edit bass horns perc")
PY
