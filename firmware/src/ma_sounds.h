/* SPDX-License-Identifier: GPL-3.0-only */
/* MANTA's sounds on Felucca's engines, and how the cards set a track: shared by the firmware (ma_fw.c) and the host
 * render (tests/ma_felucca.c), so both play the same. Needs core.h (track_t, P_*, the song) and the matrix's MS_* /
 * MD_* (mod.c) first.
 *
 * Tracks: 1 PIANO, 2 BASS, 3 HORNS (the section and the solo), 4 PERC. Each music part has a list of eight sounds (the
 * SOUND page): an engine and its factory preset, or a sound of Manta's own on that engine, or one of the FM6 patches
 * (ma_fm6.inc). The UPRIGHT is the SAMPLE engine's set 1: six notes of VCSL's Yamaha upright (CC0, tools/ma_piano.py).
 * TPT+TBN and TPT are its sets 2 and 3: VSCO 2 CE's trumpet and tenor trombone (CC0, tools/ma_brass.py).
 * The percussion is the SAMPLE engine's set DRUMS: thirty one-shots, a key each from 53 (tools/ma_perc.py).
 *
 * The matrix's slots carry the groove's controls, set every block by the glue:
 *   slot 1  MODW -> the sound's brightness by -32: the TONE (t->mw is 127 - the groove's tone: MUTE, BELL, PAN 3D)
 *   slot 2  EXPR -> AMP by 64: the GAIN (t->ex_off is the groove's gain: PAN 3D's DIST)
 *   slot 3  AT -> PAN by 64 on the horns, their PAN at -64: PAN 3D's place (t->at is the groove's pan_horns)
 *   slot 4  VEL -> AMP: the velocities on the SAMPLE engine (it has none of its own) */
#ifndef MA_SOUNDS_H
#define MA_SOUNDS_H

#include "ma_groove.h"

enum { MAS_ANALOG = 0, MAS_PHASE = 2, MAS_LOFI = 3, MAS_SAMPLE = 4, MAS_VOICE = 5, MAS_TRIO = 6, MAS_WHEEL = 7,
       MAS_PHYS = 9, MAS_DRUM = 10, MAS_FM6 = 12 };
enum { MAS_KEEP = -1 };
enum { MAS_POLY = 0, MAS_MONO = 1, MAS_LEGATO = 2, MAS_UNISON = 3 };

typedef struct {
    const char *name;
    uint8_t engine, preset;
    int8_t dist, glide, voice;
    const preset_t *own;
    const uint8_t *fm6;
    int8_t bright;                        /* the E parameter the TONE takes down (0..7), -1: none */
    int8_t trim;                          /* added to the level (F_DB steps), to even the sounds out */
} mas_sound;

/* ---- Manta's own sounds: Felucca's preset layout (core.h preset_t): {name, E0..E7, ATK DEC SUS REL, ENV->FLT, mono,
 * FX(dist, chorus, delay, reverb)}. The ANALOG E values: WAVE DTN MIX NOIS CUT RES DRV KTR ---- */
/* the accordion (cumbia's, the vallenato's): two pulses beating against each other, as its reeds do (Sill's MUSETTE) */
static const preset_t MAS_ACCORDION = {"ACCORDION", {4, 16, 64, 0, 96, 12, 18, 64}, {3, 70, 112, 28}, 0, 0, FX(0, 30, 20, 30)};
/* the bandoneon: the tango's, darker, fewer beats, a slower breath */
static const preset_t MAS_BANDONEON = {"BANDONEON", {4, 7, 64, 0, 76, 14, 16, 64}, {9, 80, 110, 30}, 0, 0, FX(0, 10, 15, 35)};
/* the sampled brass (tools/ma_brass.py, VSCO 2 CE): the SAMPLE engine's set BRASS (the trombone below the trumpet)
 * and TRUMPET (its trumpet alone), looped; tongued at once, held, let go short as a section does. The SAMPLE E
 * values: SET TUNE BITS LOOP CUT MUTE DRV - (MUTE: Manta's, set by the glue every block) */
enum { MAS_SET_BRASS = 2, MAS_SET_TRUMPET = 3, MAS_SET_SAX = 5, MAS_SET_VIOLINS = 6 };   /* (gen_samples.py CC0_SETS) */
static const preset_t MAS_BRASS = {"BRASS", {2, 0, 0, 1, 118, 0, 0, 0}, {0, 70, 127, 20}, 0, 0, FX(0, 0, 0, 30)};
static const preset_t MAS_TPT = {"TRUMPET", {3, 0, 0, 1, 118, 0, 0, 0}, {0, 70, 127, 24}, 0, 0, FX(0, 0, 0, 34)};
/* the tenor sax (VCSL, tools/ma_horns.py): set SAX, the reed's buzz; the violin section (VSCO 2 CE): set VIOLINS,
 * bowed in a little softer, let go longer */
static const preset_t MAS_SAXS = {"SAX", {5, 0, 0, 1, 118, 0, 0, 0}, {0, 70, 127, 22}, 0, 0, FX(0, 0, 0, 30)};
static const preset_t MAS_VLNS = {"VIOLINS", {6, 0, 0, 1, 112, 0, 0, 0}, {6, 70, 127, 40}, 0, 0, FX(0, 0, 0, 40)};
/* the pan flute (the Andes' zampona, the cumbia's flutes): a triangle through a low-pass that keeps little but the
 * fundamental, a lot of breath through the same resonant filter (the noise sings at the pipe's pitch), a chiff of the
 * filter's envelope on each note */
static const preset_t MAS_PANFLUTE = {"PANFLUTE", {2, 0, 0, 44, 62, 40, 0, 100}, {6, 45, 105, 30}, 30, 0, FX(0, 0, 30, 50)};
/* the guitars, on PHYS's string (Karplus-Strong; three voices): the nylon guitar of the son, the bolero and the bossa,
 * warm, plucked near the middle; the requinto, the bolero trio's lead guitar, small, steel-bright, plucked near the
 * bridge. The PHYS E values: MODEL STRC BRIT DAMP POS ACC BOW EXC */
static const preset_t MAS_GUITAR = {"GUITAR", {1, 36, 58, 100, 30, 110, 0, 0}, {0, 100, 127, 60}, 0, 0, FX(0, 10, 15, 30)};
static const preset_t MAS_REQUINTO = {"REQUINTO", {1, 30, 86, 116, 30, 127, 0, 0}, {0, 100, 127, 50}, 0, 0, FX(0, 15, 25, 35)};
/* a solo trumpet, light: one saw, a breath of noise, the filter swelling open on each note (the lip) (Sill's CORNET) */
static const preset_t MAS_TRUMPET = {"TRUMPET", {0, 0, 0, 6, 50, 12, 6, 64}, {12, 60, 96, 26}, 42, 0, FX(0, 15, 20, 30)};
/* the upright bass: a sine with a pluck of noise, round and short */
static const preset_t MAS_UPRIGHTB = {"UPRIGHT", {3, 0, 0, 5, 90, 10, 22, 0}, {0, 56, 40, 14}, 22, 1, FX(0, 0, 0, 10)};
/* a round sine bass, the bolero's and the bossa's */
static const preset_t MAS_ROUND = {"ROUND", {3, 0, 0, 0, 127, 0, 25, 0}, {0, 70, 100, 12}, 0, 1, FX(0, 0, 0, 6)};

#include "ma_fm6.inc"

#define S(n, e, p, b, tr) {(n), (uint8_t)(e), (uint8_t)(p), MAS_KEEP, MAS_KEEP, MAS_KEEP, 0, 0, (int8_t)(b), (int8_t)(tr)}
#define SX(n, e, p, d, gl, v, b, tr) {(n), (uint8_t)(e), (uint8_t)(p), (int8_t)(d), (int8_t)(gl), (int8_t)(v), 0, 0, (int8_t)(b), (int8_t)(tr)}
#define SO(n, e, p, own, d, gl, v, b, tr) {(n), (uint8_t)(e), (uint8_t)(p), (int8_t)(d), (int8_t)(gl), (int8_t)(v), &(own), 0, (int8_t)(b), (int8_t)(tr)}
#define SF(n, patch, gl, v, tr) {(n), (uint8_t)MAS_FM6, 0, MAS_KEEP, (int8_t)(gl), (int8_t)(v), 0, (patch), 2, (int8_t)(tr)}

/* the three music parts' lists, in the SOUND card's order (ma_groove.c names them in the styles) */
static const mas_sound MAS_SOUND[3][MA_SOUNDS] = {
    {   /* PIANO */
        S("UPRIGHT", MAS_SAMPLE, 1, 4, -1),             /* the montuno's upright (tools/ma_piano.py) */
        S("EPIANO", MAS_FM6, 0, 2, -1),                  /* TINE EP: the bolero's, the bossa's */
        S("ORGAN", MAS_WHEEL, 1, 3, -1),                 /* JAZZ PERC: the cha-cha's and the boogaloo's organ */
        SX("TRES", MAS_PHYS, 2, MAS_KEEP, MAS_KEEP, MAS_POLY, 2, 13),   /* PHYS's PLUCK: the son's tres */
        S("HARP", MAS_PHYS, 8, 2, 2),                    /* the Paraguayan and the jarocho harp */
        SO("ACCORD", MAS_ANALOG, 0, MAS_ACCORDION, MAS_KEEP, MAS_KEEP, MAS_POLY, 4, 3),
        SO("BANDO", MAS_ANALOG, 0, MAS_BANDONEON, MAS_KEEP, MAS_KEEP, MAS_POLY, 4, 0),
        SO("GUITAR", MAS_PHYS, 0, MAS_GUITAR, MAS_KEEP, MAS_KEEP, MAS_POLY, 2, 5),   /* strums (ma_groove.c GUITAR_PAT) */
    },
    {   /* BASS */
        SF("BABY", MA_FM6_FRETLESS, MAS_KEEP, MAS_MONO, -6),   /* the baby bass: round, a soft attack */
        SO("UPRIGHT", MAS_ANALOG, 0, MAS_UPRIGHTB, MAS_KEEP, MAS_KEEP, MAS_MONO, -1, -4),
        S("FM BASS", MAS_FM6, 2, 2, -4),
        S("SUB", MAS_ANALOG, 7, 4, -13),
        SF("SLAP", MA_FM6_SLAPBASS, MAS_KEEP, MAS_POLY, 0),
        SF("SLIDE", MA_FM6_FRETLESS, 22, MAS_LEGATO, -6),
        SO("ROUND", MAS_ANALOG, 0, MAS_ROUND, MAS_KEEP, MAS_KEEP, MAS_MONO, -1, -14),
        SX("TUBA", MAS_PHASE, 0, 4, MAS_KEEP, MAS_MONO, 2, -9),   /* PHASE's BRASS in the low end */
    },
    {   /* HORNS: the section and the solo (the solo's mute, bell and place on the TONE, GAIN and PAN slots) */
        SO("TPT+TBN", MAS_SAMPLE, 2, MAS_BRASS, MAS_KEEP, MAS_KEEP, MAS_POLY, 4, 2),   /* the sampled brass: set BRASS */
        SO("TPT", MAS_SAMPLE, 3, MAS_TPT, MAS_KEEP, MAS_KEEP, MAS_POLY, 4, 2),         /* its trumpet alone: TRUMPET */
        SO("REQUINT", MAS_PHYS, 0, MAS_REQUINTO, MAS_KEEP, MAS_KEEP, MAS_POLY, 2, 12),   /* the lead guitar's riffs */
        SO("SAX", MAS_SAMPLE, MAS_SET_SAX, MAS_SAXS, MAS_KEEP, MAS_KEEP, MAS_POLY, 4, 1),   /* the sampled tenor */
        SO("PANPIPE", MAS_ANALOG, 0, MAS_PANFLUTE, MAS_KEEP, MAS_KEEP, MAS_POLY, 4, -3),
        SO("VIOLINS", MAS_SAMPLE, MAS_SET_VIOLINS, MAS_VLNS, MAS_KEEP, MAS_KEEP, MAS_POLY, 4, 1),   /* the sampled section */
        SO("ACCORD", MAS_ANALOG, 0, MAS_ACCORDION, MAS_KEEP, MAS_KEEP, MAS_POLY, 4, -3),
        SO("BANDO", MAS_ANALOG, 0, MAS_BANDONEON, MAS_KEEP, MAS_KEEP, MAS_POLY, 4, -1),
    },
};
#undef S
#undef SX
#undef SO
#undef SF

static inline const char *mas_name(int part, int i)
{
    if (part == MA_PERC) {               /* (the PERC card: a style's percussion, its name short enough for a card) */
        static const char *const SHORT[MA_STYLES] = {"SALSA", "MAMBO", "MERENG", "CUMBIA", "SAMBA", "SON", "CHACHA",
                                                     "BOLERO", "BOSSA", "TANGO", "GUARAC",
                                                     "DANZON"};
        return SHORT[(unsigned)i % MA_STYLES];
    }
    return MAS_SOUND[part % 3][(unsigned)i % MA_SOUNDS].name;
}

static inline const mas_sound *mas_of(const ma_groove *g, int part)
{
    return &MAS_SOUND[part % 3][(unsigned)g->s.snd[part] % MA_SOUNDS];
}

/* ---- the percussion: the kit's keys ---- */
#define MAS_KIT_BASE 53                   /* the DRUMS set's first key (tools/gen_samples.py KIT_BASE) */
#define MAS_KIT_TRIM 4                    /* (tests/ma_audition.sh, as the sounds' trims) */
static inline uint32_t mas_kit_engine(void) { return MAS_SAMPLE; }
static inline int mas_drum_note(int drum) { return MAS_KIT_BASE + drum; }
/* the General MIDI percussion a drum is sent as (MIDI out, channel 10) */
static inline int mas_gm_note(int drum)
{
    static const uint8_t GM[MA_DR_COUNT] = {63, 64, 62, 64, 60, 61, 60, 65, 66, 37, 37, 56, 56, 56, 67, 68, 75, 76,
                                            73, 74, 70, 70, 69, 81, 80, 50, 45, 36, 73, 75};
    return GM[(unsigned)drum % MA_DR_COUNT];
}

static inline int16_t mas_clamp(int v, int lo, int hi) { return (int16_t)(v < lo ? lo : v > hi ? hi : v); }
#define MAS_PIANO_CUT 92                  /* the upright's CUT: a little edge off (eng_sample.c's one-pole) */

/* a part's track from the cards, over its sound's preset: the level, the place, the sends, WARMTH, and the matrix's
 * slots for the groove (above). Main loop, the audio held off (or the host) */
static inline void mas_track(track_t *t, const ma_groove *g, int part)
{
    const ma_settings *s = &g->s;
    const mas_sound *snd = part == MA_PERC ? 0 : mas_of(g, part);
    /* MIX: 100 is -2 dB (headroom: the master limiter stays out of it), each step down 0.3 dB, 0 off; then the
     * sound's trim (F_DB: half-dB steps) */
    int lvl = 108 - (100 - s->lvl[part]) * 6 / 10 + (snd ? snd->trim : MAS_KIT_TRIM);
    t->p[P_LEVEL] = mas_clamp(s->lvl[part] ? lvl : 0, 1, 127);
    /* the place: the piano a little left, the horns a little right (PAN 3D moves the solo), bass and percussion
     * centred, the percussion spread */
    int w = s->width;
    t->p[P_PAN] = (int16_t)(part == MA_PIANO ? -w * 16 / 100 : part == MA_HORNS ? -64 : 0);
    t->p[P_SPRD] = mas_clamp(part == MA_PERC ? w * 70 / 100 : part == MA_PIANO ? w * 40 / 100 : 0, 0, 127);
    /* the sends: ROOM on everything, PLATE more on the piano and the horns */
    static const uint8_t ROOM[MA_PARTS] = {70, 20, 80, 60};
    int rev = s->room * 127 / 100 * ROOM[part] / 100 + (part == MA_PIANO || part == MA_HORNS ? s->plate * 50 / 100 : 0);
    t->p[P_REV] = mas_clamp(rev, 0, 127);
    t->p[P_DLY] = mas_clamp(part == MA_HORNS ? 18 : 0, 0, 127);   /* (a short slap on the horns) */
    /* the matrix: TONE, GAIN, the horns' place */
    t->p[P_M1SRC] = MS_MODW;
    t->p[P_M1DST] = (int16_t)(snd && snd->bright >= 0 ? MD_E1 + snd->bright : MD_OFF);
    t->p[P_M1AMT] = -32;
    t->p[P_M2SRC] = MS_EXPR;
    t->p[P_M2DST] = MD_AMP;
    t->p[P_M2AMT] = 64;
    t->p[P_M3SRC] = part == MA_HORNS ? MS_AT : MS_OFF;
    t->p[P_M3DST] = part == MA_HORNS ? MD_PAN : MD_OFF;
    t->p[P_M3AMT] = 64;
    if (part == MA_PERC) {
        t->p[P_VOICE] = V_POLY;
        t->p[P_M4SRC] = MS_VEL;          /* the one-shots' velocity */
        t->p[P_M4DST] = MD_AMP;
        t->p[P_M4AMT] = 64;
        t->p[P_ITYPE] = 0;
        t->p[P_DIST] = 0;
        return;
    }
    if (snd->engine == MAS_SAMPLE) {     /* the upright and the brass: their velocity; the upright a little edge off */
        t->p[P_M4SRC] = MS_VEL;
        t->p[P_M4DST] = MD_AMP;
        t->p[P_M4AMT] = 64;
        if (part == MA_PIANO)
            t->p[P_E4] = MAS_PIANO_CUT;
    } else {
        t->p[P_M4SRC] = MS_OFF;
    }
    if (snd->glide >= 0)
        t->p[P_GLIDE] = snd->glide;
    if (snd->voice >= 0)
        t->p[P_VOICE] = (int16_t)(snd->voice == MAS_UNISON ? V_UNISON : snd->voice == MAS_LEGATO ? V_LEGATO
                                  : snd->voice == MAS_MONO ? V_MONO : V_POLY);
    /* WARMTH: the tape's saturation, a little on every part */
    t->p[P_DIST] = mas_clamp((snd->dist >= 0 ? snd->dist : 0) + s->warmth * 30 / 100, 0, 127);
    t->p[P_ITYPE] = 0;
}

/* the song's own: the tempo, the delay (a short slap), the reverb's room */
static inline void mas_song(const ma_settings *s)
{
    song.g[G_BPM] = s->bpm > 200 ? 200 : s->bpm;
    song.g[G_DTIME] = 2;                  /* (params.c N_DTIME: 1/16) */
    song.g[G_DFDBK] = 15;
    song.g[G_DMIX] = 100;
    song.g[G_RSIZE] = (int16_t)(45 + s->room * 30 / 100);
    song.g[G_RDAMP] = 55;
    song.g[G_RTYPE] = 0;                  /* (Felucca's ROOM model) */
}

/* the block's controls into the tracks (the audio ISR, before the mix): the tone, the gain, the horns' place */
static inline void mas_block(const ma_groove *g)
{
    for (int p = 0; p < MA_PARTS; ++p) {
        trk[p].mw = (uint8_t)(127 - g->tone[p]);
        trk[p].ex_off = g->gain[p];
    }
    trk[MA_HORNS].at = g->pan_horns;
    trk[MA_HORNS].p[P_E5] = g->brass ? g->mute_live : 0;   /* the sampled brass's mute filter (eng_sample.c) */
}

/* the groove told what the sounds are: the HORNS the sampled brass (its mutes) with the trombone (an octave down);
 * the PIANO part the GUITAR (it strums) */
static inline void mas_flags(ma_groove *g)
{
    g->guitar = mas_of(g, MA_PIANO)->own == &MAS_GUITAR;
    const mas_sound *h = mas_of(g, MA_HORNS);
    int set = h->engine == MAS_SAMPLE && h->own ? h->own->e[0] : -1;
    g->brass = set == MAS_SET_BRASS || set == MAS_SET_TRUMPET;
    g->trombone = set == MAS_SET_BRASS;
}

#endif
