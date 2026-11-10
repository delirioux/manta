# Contributing to Manta

Manta is a small fork. Bugs and ideas: [Issues](https://github.com/delirioux/manta/issues) on this repository,
please; not [Felucca](https://github.com/hugelton/Felucca)'s, [Tuna](https://github.com/delirioux/tuna)'s or [Daurade](https://github.com/delirioux/daurade)'s trackers: Manta is maintained here.

With a bug, say the Manta version (the splash), the style and song, the steps to reproduce it, what you expected
and what happened. A change to the songs goes in `firmware/src/ma_groove.c`, to the sounds in `ma_sounds.h` (run
`sh tests/ma_audition.sh` and keep the trims even). The keys only ever play music. `sh tests/ma_tests.sh` must pass,
and the screenshots in `docs/screens/` change with the screen.
