# Developing Manta

The working notes behind Manta: the agreements, how the work flows, why the code is shaped as it is, where it
stands and what comes next. What Manta is and how to play it is in the [README](../README.md) and the
[manual](MANUAL.md).

Status: 10 November 2026. Version 0.1, a test build on `dev`: the groove engine, the twelve styles, the percussion kit,
the upright and the pages, tested on the host and in the browser emulator; the FM-1 build comes from CI.

## Agreements

| What | Agreed |
| --- | --- |
| Home | GitHub, `github.com/delirioux/manta`, branch `main`; unfinished work on `dev` until it has run on the FM-1. |
| Upstream | A fork of [Sill](https://github.com/delirioux/sill) 1.0.4 (itself on [Daurade](https://github.com/delirioux/daurade), [Tuna](https://github.com/delirioux/tuna) and [Felucca](https://github.com/hugelton/Felucca)). Never push to Tuna or Felucca. Felucca's five engines are a submodule, `firmware/engines`. |
| Name | Ocean animals, as Tuna, Flyfish, Makohai, Daurade, Snapper, Sill and Sardine: the manta. The logo: a manta over a ring of the clave's beat, a knob box with claves, a sign-style wordmark in mango orange and palm green. |
| Firmware ID | `FM-1_47626820` (MANTA0 on a phone's keys), in `tools/build.py`. |
| What it is | Generative Latin grooves: a band on the clave, the player leading it and taking the solo. Its sounds and styles are its own; the repository names no artist or record as a model, in code, docs or history. |
| Description | "Generative Latin grooves for the FM-1. Let the band play, ¡tú al solo!" |
| Melodies | Only from the public domain: traditional (anonymous) songs, or composers dead before 1926, transcribed from public-domain scores, never from MIDI files; and phrases Manta makes in the style. |
| Language | English throughout: code, comments, commits, docs. The screen is ASCII (MONA for moña). |
| Licence | GPL-3.0-only. The samples are CC0: VCSL's and VSCO 2 CE's recordings and what `tools/ma_perc.py` synthesizes. |
| Commits | Author `delirioux <340923392+delirioux@users.noreply.github.com>`. Work done with Claude ends with the `Co-Authored-By` and `Claude-Session` lines. |
| Repository contents | The firmware, its tests and tools, its docs and its plan (this file). Chat discussions stay out. Docs and screenshots change with the code. |
| Controls | The keys only ever play music; settings, pages and sets are on knobs and buttons. |
| Changes | A new style, song, hit or mambo waits for the next bar line; the knobs act at once. |
| Colours | HAVANA is the default palette; Felucca's palettes stay on MENU. |

## How changes reach GitHub

Claude pushes to the repository directly (the Claude GitHub App). Work goes to `dev`; a push there builds a test
package (Actions > the run > Artifacts > `manta-firmware`) and the test site under `dev/`, installed with the
installer's "Install a test build". It goes to `main` once it has run on the FM-1. A push to `main` with a version
(`ma_version.h`) not yet tagged tags it `vX.Y` and makes a GitHub release with the package and `docs/RELEASE.md`.
The README links `dev`'s pages (`/manta/dev/try/`, `/manta/dev/`) until the first release; from then on `main`'s
(`/manta/try/`, `/manta/`) first, `dev`'s test build under them.

## Where Manta lives

Manta's files are `firmware/src/ma_*`, `tests/ma_*`, `tools/ma_*`, `web/ma_*` and `web/emu/ma_*`. From Tuna:
`tuna_libm.c` and `tuna_sys.h`. Everything else is Felucca's, touched only where Manta hooks in (`felucca.c`,
`main.c`, `audio.c`, `panel.c`, `gfx.c`, `eng_sample.c`: `FELUCCA_MANTA`; `tools/build.py`: `MA_MODULES`;
`tools/gen_samples.py`: the kit and the upright, the dry sounds at 32 kHz; `tools/gen_ui_palettes.py`: HAVANA).

| File | Does |
| --- | --- |
| `ma_groove.h/.c` | the engine: twelve styles, the chord loops, the clave and the two-bar patterns of each part and each drum, the form, BAND HIT and the descarga, the mambo, the solo (its phrases, its mute, PAN 3D), swing and human timing. Plain C, no Felucca: it runs on the host as it does on the FM-1 |
| `ma_sounds.h` | the sounds on Felucca's engines and their trims, the kit's notes and their General MIDI notes, and how the cards set a part's track (level, place, sends, the matrix's tone, gain and place slots) |
| `ma_ui.h/.c` | the twelve pages, the cards, the styles list, the sets, on Felucca's `gfx.c` |
| `ma_fw.c` | the glue: the groove on the tracks and MIDI out, loading a part's sound when it changes, the panel and its LEDs, the settings and the sets in flash, the splash |
| `ma_logo.inc` | the splash logo, made by `tools/ma_logo.py` from the same drawing as `assets/logo.svg` (`tools/draw_logo.py`) |
| `tools/ma_perc.py` | the thirty one-shots into `assets/samples-cc0/DRUMS/`: VCSL's recordings, trimmed and levelled; the timbales' heads, the kick and the washboard synthesized, seeded. `--fetch DIR` takes VCSL from a clone |
| `tools/ma_horns.py` | the sampled horns, CC0, into `assets/samples-cc0/`: BRASS (eleven notes of VSCO 2 CE's tenor trombone, F1 to C3, and trumpet, D#3 to A4), SAX (five of VCSL's tenor sax) and VIOLINS (four of VSCO 2 CE's violin section): the swell evened, an accent on the attack, the sustain levelled |
| `tools/ma_piano.py` | the UPRIGHT: six notes of VCSL's Yamaha upright (CC0) into `assets/samples-cc0/PIANO/` |
| `tests/ma_groove_test.c` | the engine: every style plays and ends its notes, in key; the clave's sides; the form's order; a song replays exactly; the style and song on the bar line; BAND HIT tapped and held; the mambo; the keys; the mutes and PAN 3D; a bad record |
| `tests/ma_ui_test.c` | the screen: the controls, every page in every palette, the layout lint, PNGs and a GIF (`docs/screens/`) |
| `tests/ma_felucca.c` | a song through Felucca's real engines to a WAV: voices, steals, peaks, the cost |
| `tests/ma_audition.c` | every sound's loudness, for the trims in `ma_sounds.h` |
| `web/emu/ma_web.c` | the browser build, on Felucca's emulator |

## Decisions and why

- **Sill's machine, a new engine.** The frame (the four tracks, the matrix slots, the cards, the sets, the flash, the
  browser build) is Sill's, proven on the FM-1. The groove engine, the styles, the sounds, the pages and the kit are
  Manta's.
- **Everything on the clave.** Every pattern is two bars of sixteenths, written 3-2; 2-3 plays it a bar on. So the
  piano's anticipations, the bass's tumbao, the bells and the horns always agree with the clave, and CLAVE never puts
  one part against another.
- **The percussion is sampled.** Synthesized congas and bells sounded thin; the kit is recordings where CC0 ones were
  good (VCSL), synthesis only where they were not (the timbales' heads under a real rope-snare stroke, the kick, the
  washboard). The dry sounds (the bells, the shakers, the güiro, the washboard) play at 32 kHz for their air, the
  skins at 22,050 Hz. 34 zones with the upright, about 210 KB, under Sill's footprint.
- **A sampled upright and sampled brass, the rest synthesized.** The montuno lives on the piano's attack; six notes of
  an upright, bright but not hard. A salsa section lives on real brass: eleven notes of a trumpet and a trombone in
  one set (BRASS, the trombone below D#3), and its trumpet zones alone as a second set (TRUMPET, no bytes of its own).
  Each is cut to 0.55 s and looped over whole periods of its pitch, so a held note holds; about 67 KB. A tenor sax
  (VCSL) and a violin section (VSCO 2 CE) the same way, at 16 kHz to fit (about 35 KB; the violins loop over a
  vibrato cycle); the image about 570 KB of the 581 KB slot, so the flash is now full. With TPT+TBN the section's
  second horn goes an octave down, onto the trombone. The pan pipes are synthesized: a triangle filtered to little but
  its fundamental, breath through the same resonant filter.
  The bass, the other horns and the other keyboards are Felucca's engines: bytes, not kilobytes.
- **The mutes are filters.** Four mutes as recordings would cost four times the trumpet. On the sampled brass the
  SAMPLE engine runs a state-variable filter after CUT (Manta's, `eng_sample.c`, its E5 set by the glue each block
  while the solo plays): STRAIGHT the lows gone, thin and buzzy; CUP a low-pass, round, no top; HARMON a narrow
  band-pass, the nasal honk; WAH that band swept by the TONE. Measured on the solo: the open horn's centroid 2.3 kHz,
  the cup's 1.2 kHz, the straight's lows 19 dB down. On the synthesized horns the mutes are the TONE's darkness.
- **PAN 3D in the matrix, for now.** The solo shares the HORNS track (four tracks only), so PAN 3D moves the horns'
  place, tone and level by the matrix's third slot (AT to PAN): behind you duller, far off softer. Sardine's
  binaural filters need a track of their own.
- **The washboard is a colour.** One scrape a bar, a tap of the thimbles now and then when hot; the merengue's
  güira plays on the güiro.
- **One mute for the HORNS track.** The section and the solo share it; BOLERO's horns are muted and sit back
  (its GAIN down, about 9 dB under the mix).
- **BAND HIT is the clave's figure.** A cierre is the whole band on the clave's strokes of the next bar, the cymbal on
  the last; held, the descarga leaves the percussion alone and a timbal roll brings the band back on the bar line.
- **A small, dry room.** Latin records of the era are close and punchy; ROOM is short, PLATE adds to the horns and the
  piano only, no delay but a short slap on the horns.
- **The guitar is a string model, and it strums.** No CC0 guitar recording was to be had and the flash is full, so
  GUITAR (the PIANO part) and REQUINT (the HORNS) are Felucca's PHYS string (Karplus-Strong). With the GUITAR the
  PIANO part leaves its montuno and strums its own patterns (`GUITAR_PAT`, two a style): down-strums 10 ms a string
  low to high, up-strums high to low, the muted chuck, the thumb's root and fifth, the fingers' pluck, arpeggios.
  PHYS has three voices a part, so a strum is three strings; the root is the thumb's, alone. A MUTE is a trumpet's:
  on the other HORNS sounds it does nothing, and BOLERO's pull-back is its trumpets'.
- **Eight voices for everything.** Felucca shares eight voices across the four tracks; the percussion's one-shots ring
  short and the oldest released note is stolen first. Measured through Felucca's engines, SALSA's busiest moments steal
  released tails, not held notes.
- **No libm, no doubles**: floats on the FPU (`-mcpu=r3 -mfprev1`), `sinf` and friends from `tuna_libm.c`.
- **Settings save themselves while stopped**, in Felucca's first project object ("MAN1"); the eight sets in the
  second ("MST1"). The record only grows at its end, so older settings still load; a bad record is fixed field by field.
- **HAVANA**: dark tobacco, cream values, mango orange for the one active thing, palm green; its own colour tag, so
  another firmware's choice does not carry over.

## Measured on the host

SALSA with the arc through Felucca's engines (`tests/ma_felucca.sh`): peak -4.0 dBFS, nothing clipped. The real cost
comes from the FM-1 (MENU shows the CPU).

## Plan

1. **On the FM-1.** It installs, boots and plays; the settings and sets survive a power cycle; MIDI out; the CPU at
   SALSA and MAMBO over a long session. Confirm the firmware ID with the Hub.
2. **PAN 3D in full.** Sardine's binaural filters on the solo, on a track of its own.
3. **TRAD.** Phrases from traditional songs of the Latin countries, transcribed from public-domain scores, on PHRASE's
   TRAD and MIX.
4. **The sound by ear.** The kit's levels, the upright against the bass, the brass and its mutes, the trims.
