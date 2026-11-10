#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Manta's host tests: the generated files up to date (the splash, the FM6 patches), the groove engine strict about
# warnings and single precision and its own tests (tests/ma_groove_test.c), the screen and its layout lint
# (tests/ma_ui_test.sh).
#   sh tests/ma_tests.sh        (exit 0: all passed)
set -eu
mkdir -p build/host
python3 tools/ma_logo.py --check
python3 tools/ma_fm6.py --check
cc -std=c11 -O2 -Wall -Wextra -Wshadow -Wdouble-promotion -Werror -Ifirmware/src -c firmware/src/ma_groove.c \
   -o build/host/ma_groove.o
cc -std=c11 -O2 -Wall -Ifirmware/src -o build/host/ma_groove_test tests/ma_groove_test.c firmware/src/ma_groove.c -lm
build/host/ma_groove_test
sh tests/ma_ui_test.sh build/ma_ui
echo "ma_tests: all passed"
