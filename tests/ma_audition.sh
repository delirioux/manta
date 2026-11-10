#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Manta's sounds side by side (tests/ma_audition.c): each one's loudness and the trim that evens it out.
#   tests/ma_audition.sh [OUTDIR]     (with OUTDIR: each phrase as a WAV too)
set -eu
mkdir -p build/host
[ -f build/gen/felucca_tables.h ] || python3 -c "import sys; sys.argv=['x']; sys.path.insert(0,'tools'); import build; build.generate()"
cc -O2 -w -DFELUCCA_SLICE=0 -Ibuild/gen -Ifirmware/src -o build/host/ma_audition tests/ma_audition.c firmware/src/ma_groove.c -lm
[ -n "${1:-}" ] && mkdir -p "$1"
build/host/ma_audition ${1:+"$1"}
