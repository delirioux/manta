/* SPDX-License-Identifier: GPL-3.0-only */
/* MANTA's sounds on Felucca's engines, and how the cards set a track: shared by the firmware (ma_fw.c) and the host
 * render (tests/ma_felucca.c), so both play the same. Needs core.h (track_t, P_*, the song) and the matrix's
 * MS_* / MD_* (mod.c) first.
 *
 * Tracks: 1 STABS, 2 BASS, 3 HOOK, 4 DRUMS. Each music part has a list of ten sounds (the SOUND page): an engine and
 * its factory preset, or a sound of Manta's own on that engine (mas_own), or one of the FM6 patches (ma_fm6.inc).
 * The PIANO is the SAMPLE engine's set 1: six notes of a Kawai grand (VCSL, CC0) made bright (tools/ma_piano.py).
 * The drums are a kit: EURO, Manta's own one-shots and its choir (the SAMPLE engine, set DRUMS: tools/ma_drums.py at
 * 44.1 kHz, the choir's OH and AH tools/ma_choir.py's, pitched zones above the kit), or the DRUM engine's TIGHT or 80
 * kit (no choir there).
 *
 * Two of the matrix's slots carry the groove's controls, set every block by the glue:
 *   slot 1  MODW -> the sound's brightness (ANALOG and TRIO CUT, FM6 MLVL, ...) by -32: the LIFT (t->mw is
 *           127 - the groove's cut, so an open filter leaves the sound as its preset has it, and a closed one takes
 *           half the range off)
 *   slot 2  EXPR -> AMP by 64: the PUMP (t->ex_off is the groove's duck: the gain is 1 - duck)
 *   slot 4  VEL -> AMP: the drums' velocities on the SAMPLE engine (it has none of its own) */
#ifndef MA_SOUNDS_H
#define MA_SOUNDS_H

#include "ma_groove.h"

enum { MAS_ANALOG = 0, MAS_PHASE = 2, MAS_LOFI = 3, MAS_SAMPLE = 4, MAS_VOICE = 5, MAS_TRIO = 6, MAS_WHEEL = 7,
       MAS_PHYS = 9, MAS_DRUM = 10, MAS_FM6 = 12 };
enum { MAS_KEEP = -1 };                   /* dist, glide, voice: as the preset leaves them */
enum { MAS_POLY = 0, MAS_MONO = 1, MAS_LEGATO = 2, MAS_UNISON = 3 };

typedef struct {
    const char *name;
    uint8_t engine, preset;
    int8_t dist, glide, voice;
    const preset_t *own;
    const uint8_t *fm6;
    int8_t bright;                        /* the E parameter the LIFT takes down (0..7), -1: none */
    int8_t trim;                          /* added to the level (F_DB steps), to even the sounds out: each part's
                                           * sounds at one loudness (tests/ma_audition.sh measures them) */
} mas_sound;

/* ---- Manta's own sounds: Felucca's preset layout (core.h preset_t): {name, E0..E7, ATK DEC SUS REL, ENV->FLT,
 * mono, FX(dist, chorus, delay, reverb)}. The ANALOG E values: WAVE DTN MIX NOIS CUT RES DRV KTR; VOICE: VOWL VOWL2
 * TALK SHIFT BUZZ BRTH Q RAND. SAW LEAD is Tuna's (tuna_sounds_own.inc), OCTAVE Daurade's DISCO BASS ---- */
/* the stab of the anthem: two saws far apart, bright, a short pluck of the filter, wide in the chorus */
static const preset_t MAS_SUPER = {"SUPER", {0, 42, 64, 0, 110, 22, 12, 64}, {0, 64, 84, 26}, 12, 0, FX(0, 60, 25, 25)};
/* the hook's lead: the same saws on every free voice (UNISON), a touch of glide */
static const preset_t MAS_SUPERLEAD = {"SUPER LEAD", {0, 30, 64, 0, 112, 18, 16, 64}, {1, 60, 104, 30}, 8, 1,
                                       FX(0, 50, 35, 30)};
/* the octave bass of the offbeat: a saw with its sub-octave, plucked by the filter envelope (Daurade's) */
static const preset_t MAS_OCTAVE = {"OCTAVE", {6, 0, 40, 0, 62, 35, 20, 64}, {0, 52, 60, 14}, 30, 1, FX(5, 0, 5, 10)};
/* the reese: two saws beating slowly against each other, low and dark */
static const preset_t MAS_REESE = {"REESE", {0, 22, 64, 0, 58, 20, 15, 64}, {0, 80, 110, 18}, 6, 1, FX(0, 0, 0, 8)};
/* the hoover: a wide pulse, detuned, a growl of drive, gliding (the hook's) */
static const preset_t MAS_HOOVER = {"HOOVER", {4, 38, 64, 0, 96, 30, 20, 64}, {0, 70, 100, 30}, 16, 1, FX(6, 40, 20, 25)};
/* the sung hook: the VOICE engine breathy and round, its vowels and their glide the VOX page's */
static const preset_t MAS_OOH = {"OOH", {127, 0, 40, 0, 70, 35, 80, 8}, {10, 70, 110, 40}, 0, 1, FX(0, 40, 30, 40)};
/* ---- 0.4's own: the sounds a style can vary between (ma_groove.c's pools) ---- */
/* STABS: a deep house chord of two squares a little apart, a short pluck of the filter */
static const preset_t MAS_DEEPCHORD = {"DEEP CHORD", {1, 20, 64, 0, 86, 32, 10, 64}, {0, 58, 46, 20}, 22, 0, FX(0, 40, 30, 30)};
/* STABS: a short trance pluck, the saws far apart, the filter snapping shut */
static const preset_t MAS_TRANCESTAB = {"TRANCE", {0, 38, 64, 0, 110, 24, 12, 64}, {0, 58, 30, 18}, 36, 0, FX(0, 50, 40, 30)};
/* STABS and HOOK: a choir on the VOICE engine, quicker than its factory CHOIR AAH (the VOX page's vowels) */
static const preset_t MAS_CHOIR = {"CHOIR", {0, 0, 40, 0, 40, 26, 64, 6}, {14, 90, 112, 60}, 0, 0, FX(0, 70, 15, 70)};
static const preset_t MAS_CHOIRLEAD = {"CHOIR", {0, 0, 40, 0, 50, 22, 70, 6}, {10, 80, 112, 50}, 0, 1, FX(0, 50, 30, 60)};
/* BASS: a punchy analogue saw, short and dry */
static const preset_t MAS_SYNTHBASS = {"SYNTH BASS", {0, 0, 0, 0, 70, 40, 12, 64}, {0, 46, 30, 10}, 34, 1, FX(0, 0, 0, 6)};
/* BASS: the trance bass, a bright saw with a little detune, rolling */
static const preset_t MAS_TRANCEBASS = {"TRANCE BASS", {0, 10, 64, 0, 84, 22, 8, 64}, {0, 62, 82, 12}, 20, 1, FX(0, 0, 0, 8)};
/* BASS: a deep sine with a click on top */
static const preset_t MAS_SINEBASS = {"SINE BASS", {3, 0, 0, 0, 127, 0, 25, 0}, {0, 70, 100, 12}, 0, 1, FX(0, 0, 0, 4)};
/* HOOK: REEDS, the accordion's musette: two pulses beating against each other, as its reeds do */
static const preset_t MAS_MUSETTE = {"MUSETTE", {4, 16, 64, 0, 96, 12, 18, 64}, {3, 70, 112, 28}, 0, 1, FX(0, 30, 30, 35)};
/* HOOK: the pop lead of the bubblegum songs: a square, plucked by the filter */
static const preset_t MAS_POP = {"POP", {1, 6, 64, 0, 100, 20, 5, 64}, {0, 55, 60, 20}, 25, 1, FX(0, 20, 35, 30)};
/* HOOK: the anthem lead: saws wide and driven, a slow glide */
static const preset_t MAS_ANTHEM = {"ANTHEM", {0, 26, 64, 0, 108, 15, 10, 64}, {4, 70, 110, 35}, 10, 1, FX(0, 40, 40, 40)};
/* HOOK: the whistle: a sine with a breath of noise */
static const preset_t MAS_WHISTLE = {"WHISTLE", {3, 0, 0, 6, 127, 0, 0, 64}, {6, 70, 110, 25}, 0, 1, FX(0, 20, 40, 45)};
/* HOOK: a trance pluck lead, snappy */
static const preset_t MAS_TRANCELEAD = {"TRANCE", {0, 22, 64, 0, 100, 32, 12, 64}, {0, 46, 30, 20}, 45, 1, FX(0, 30, 50, 35)};
/* HOOK: the robot (Daurade's): the VOICE engine at full buzz and a high Q, a vocoder */
/* HOOK: CORNET, a solo trumpet, light: one saw, a breath of noise, the filter swelling open on each note (the lip), mono */
static const preset_t MAS_TRUMPET = {"CORNET", {0, 0, 0, 6, 50, 12, 6, 64}, {12, 60, 96, 26}, 42, 1, FX(0, 20, 25, 35)};
static const preset_t MAS_SAWLEAD = {"SAW LEAD", {0, 0, 12, 12, 0, 96, 25, 64}, {2, 70, 100, 35}, 20, 1,
                                     FX(0, 10, 35, 25)};

#include "ma_fm6.inc"

#define S(n, e, p, b, tr) {(n), (uint8_t)(e), (uint8_t)(p), MAS_KEEP, MAS_KEEP, MAS_KEEP, 0, 0, (int8_t)(b), (int8_t)(tr)}
#define SX(n, e, p, d, gl, v, b, tr) {(n), (uint8_t)(e), (uint8_t)(p), (int8_t)(d), (int8_t)(gl), (int8_t)(v), 0, 0, (int8_t)(b), (int8_t)(tr)}
#define SO(n, e, p, own, d, gl, v, b, tr) {(n), (uint8_t)(e), (uint8_t)(p), (int8_t)(d), (int8_t)(gl), (int8_t)(v), &(own), 0, (int8_t)(b), (int8_t)(tr)}
#define SF(n, patch, gl, v, tr) {(n), (uint8_t)MAS_FM6, 0, MAS_KEEP, (int8_t)(gl), (int8_t)(v), 0, (patch), 2, (int8_t)(tr)}

/* the three music parts' lists, in the SOUND card's order (ma_groove.c names them in the styles) */
static const mas_sound MAS_SOUND[3][MA_SOUNDS] = {
    {   /* STABS */
        S("PIANO", MAS_SAMPLE, 1, 4, -18),               /* the club piano (tools/ma_piano.py), pushed back */
        S("ORGAN", MAS_WHEEL, 1, 3, 0),                  /* JAZZ PERC: the house organ's click */
        SO("SUPER", MAS_ANALOG, 0, MAS_SUPER, MAS_KEEP, MAS_KEEP, MAS_POLY, 4, 0),
        S("STRINGS", MAS_ANALOG, 11, 4, -6),
        S("PWM", MAS_ANALOG, 3, 4, -5),                   /* PWM STR: pulse-width strings, wide and soft */
        S("PLUCK", MAS_ANALOG, 8, 4, 4),
        S("HOOVER", MAS_ANALOG, 6, 4, -5),               /* RAVE */
        SF("ORCH", MA_FM6_ORCHHIT, MAS_KEEP, MAS_POLY, 6),
        S("EPIANO", MAS_FM6, 0, 2, -2),                  /* TINE EP: the electric piano of the ballads */
        S("PAD", MAS_FM6, 4, 2, 16),
        S("HOUSE", MAS_WHEEL, 0, 3, 3),                  /* FULL ORGAN, short: the house organ chord */
        SO("DEEP", MAS_ANALOG, 0, MAS_DEEPCHORD, MAS_KEEP, MAS_KEEP, MAS_POLY, 4, 10),
        S("KEYS", MAS_ANALOG, 5, 4, 3),                  /* SINE KEY: a soft keyboard chord */
        SO("CHOIR", MAS_VOICE, 0, MAS_CHOIR, MAS_KEEP, MAS_KEEP, MAS_POLY, -1, -1),
        S("VIOLINS", MAS_PHASE, 2, 2, -3),                /* PHASE's STRING: wide, orchestral */
        SO("TRANCE", MAS_ANALOG, 0, MAS_TRANCESTAB, MAS_KEEP, MAS_KEEP, MAS_POLY, 4, 9),
    },
    {   /* BASS */
        S("FM BASS", MAS_FM6, 2, 2, -4),
        S("SQUARE", MAS_ANALOG, 2, 4, -1),
        S("SUB", MAS_ANALOG, 7, 4, -13),
        S("FAT", MAS_TRIO, 0, 5, 2),
        SX("ACID", MAS_ANALOG, 4, MAS_KEEP, 18, MAS_LEGATO, 4, 6),
        SO("REESE", MAS_ANALOG, 0, MAS_REESE, MAS_KEEP, MAS_KEEP, MAS_MONO, 4, -8),
        SF("SLAP", MA_FM6_SLAPBASS, MAS_KEEP, MAS_POLY, 0),
        SF("SLIDE", MA_FM6_FRETLESS, 22, MAS_LEGATO, -5),
        SO("OCTAVE", MAS_ANALOG, 0, MAS_OCTAVE, MAS_KEEP, MAS_KEEP, MAS_MONO, 4, -1),
        SX("WOW", MAS_VOICE, 2, MAS_KEEP, 24, MAS_LEGATO, -1, -3),
        SX("ORGAN", MAS_WHEEL, 1, MAS_KEEP, MAS_KEEP, MAS_MONO, 3, -4),   /* the 90s organ bass */
        SO("SYNTH", MAS_ANALOG, 0, MAS_SYNTHBASS, MAS_KEEP, MAS_KEEP, MAS_MONO, 4, 2),
        SO("SINE", MAS_ANALOG, 0, MAS_SINEBASS, MAS_KEEP, MAS_KEEP, MAS_MONO, -1, -14),
        SO("TRANCE", MAS_ANALOG, 0, MAS_TRANCEBASS, MAS_KEEP, MAS_KEEP, MAS_MONO, 4, -6),
        SX("DX", MAS_FM6, 7, MAS_KEEP, MAS_KEEP, MAS_MONO, 2, -1),        /* FM6's PLUCK, low and hard */
        SX("GROWL", MAS_PHASE, 0, 6, MAS_KEEP, MAS_MONO, 2, -9),          /* PHASE's BRASS in the low end */
    },
    {   /* HOOK */
        SO("SUPER", MAS_ANALOG, 0, MAS_SUPERLEAD, MAS_KEEP, 6, MAS_UNISON, 4, -8),
        SO("SAW", MAS_TRIO, 0, MAS_SAWLEAD, MAS_KEEP, 10, MAS_KEEP, 5, -8),
        S("PLUCK", MAS_ANALOG, 8, 4, 2),
        SO("POP", MAS_ANALOG, 0, MAS_POP, MAS_KEEP, 4, MAS_MONO, 4, -3),  /* the bubblegum lead */
        S("PIANO", MAS_SAMPLE, 1, 4, -18),
        SO("HOOVER", MAS_ANALOG, 0, MAS_HOOVER, MAS_KEEP, 14, MAS_LEGATO, 4, -14),
        SO("OOH", MAS_VOICE, 0, MAS_OOH, MAS_KEEP, MAS_KEEP, MAS_LEGATO, -1, -12),
        S("SYNC", MAS_TRIO, 2, 5, -6),
        S("FLUTE", MAS_WHEEL, 3, 3, -7),                 /* SOFT FLUTE */
        S("PAD", MAS_ANALOG, 1, 4, -11),                 /* SOFT PAD: HOOK SPEED PAD holds it on the chords */
        SO("REEDS", MAS_ANALOG, 0, MAS_MUSETTE, MAS_KEEP, 8, MAS_LEGATO, 4, -14),
        SO("ANTHEM", MAS_ANALOG, 0, MAS_ANTHEM, MAS_KEEP, 14, MAS_LEGATO, 4, -12),
        SO("CHOIR", MAS_VOICE, 0, MAS_CHOIRLEAD, MAS_KEEP, 10, MAS_LEGATO, -1, -12),
        SO("WHISTLE", MAS_ANALOG, 0, MAS_WHISTLE, MAS_KEEP, 16, MAS_LEGATO, -1, -16),
        SO("TRANCE", MAS_ANALOG, 0, MAS_TRANCELEAD, MAS_KEEP, MAS_KEEP, MAS_MONO, 4, 4),
        SO("CORNET", MAS_ANALOG, 0, MAS_TRUMPET, MAS_KEEP, 8, MAS_LEGATO, 4, -17),   /* (2 dB under the leads: light) */
    },
};
#undef S
#undef SX
#undef SO
#undef SF

static const char *const MAS_KIT_NAMES[MA_KIT_COUNT] = {"EURO", "SESSION", "MACHINE"};
static const int8_t MAS_KIT_TRIM[MA_KIT_COUNT] = {-14, 0, -1};   /* (tests/ma_audition.sh, as the sounds' trims) */
static inline const char *mas_name(int part, int i)
{
    if (part == MA_DRUMS)
        return MAS_KIT_NAMES[(unsigned)i % MA_KIT_COUNT];
    return MAS_SOUND[part % 3][(unsigned)i % MA_SOUNDS].name;
}

/* the sound a part plays now: its card's */
static inline const mas_sound *mas_of(const ma_groove *g, int part)
{
    return &MAS_SOUND[part % 3][(unsigned)g->s.snd[part] % MA_SOUNDS];
}

/* ---- the drums: the kit's engine and each drum's note ---- */
#define MAS_KIT_BASE 53                   /* the DRUMS set's first key (tools/gen_samples.py KIT_BASE) */
static inline uint32_t mas_kit_engine(int kit) { return kit == MA_KIT_EURO ? MAS_SAMPLE : MAS_DRUM; }
/* the choir on the DRUMS set: a vowel's note n at key n + MAS_CHOIR_KEY[vowel] (tools/gen_samples.py CHOIR_KEY) */
static const uint8_t MAS_CHOIR_KEY[2] = {16, 40};
/* the kicks again, pitched, below the kit: PUNCH at its own pitch on key 26 (20..32), ROUND on 42 (36..48)
 * (tools/gen_samples.py KICK_KEY); a kick shift semitones up at key + shift */
static const uint8_t MAS_KICK_KEY[2] = {26, 42};
/* the note a drum (or a choir note, MA_CHOIR_NOTE) plays on the kit; -1: this kit has none (the choir is EURO's) */
static inline int mas_drum_note(const ma_settings *s, int drum)
{
    if (ma_is_choir(drum))
        return s->snd[3] == MA_KIT_EURO ? ma_choir_pitch(drum) + MAS_CHOIR_KEY[ma_choir_vowel(drum) & 1] : -1;
    if (ma_is_kick_tuned(drum)) {         /* a tuned kick: EURO's kick zones below the kit (MAS_KICK_KEY), else GM's */
        if (s->snd[3] != MA_KIT_EURO)
            return 36;
        return MAS_KICK_KEY[s->kick == MA_KICK_ROUND] + ma_kick_shift(drum);
    }
    if (s->snd[3] == MA_KIT_EURO) {       /* the DRUMS set: KICK PUNCH, KICK ROUND, SNARE, CLAP, HAT, OPEN HAT, CRASH */
        static const uint8_t K[MA_DR_OH] = {0, 2, 3, 4, 5, 6};
        int k = K[drum % MA_DR_OH];
        if (drum == MA_DR_KICK && s->kick == MA_KICK_ROUND)
            k = 1;
        return MAS_KIT_BASE + k;
    }
    static const uint8_t GM[MA_DR_OH] = {36, 38, 39, 42, 46, 49};   /* General MIDI */
    return GM[drum % MA_DR_OH];
}
/* the DRUM engine's eight (E0 KIT, E1 TUNE, E2 TONE, E3 DECY, E4 SNAP, E5 ACC, E6 KICK, E7 DRV) for a kit */
static inline void mas_kit_params(const ma_settings *s, int16_t *e)
{
    e[0] = s->snd[3] == MA_KIT_SESSION ? 10 : 4;    /* TIGHT, 80 (eng_drum.c DK_*) */
    e[1] = 64;
    e[2] = 64;
    e[3] = 60;
    e[4] = (int16_t)(30 + s->snap * 97 / 100);
    e[5] = 100;
    e[6] = (int16_t)(s->kick == MA_KICK_ROUND ? 1 : 0);
    e[7] = 10;
}

static inline int16_t mas_clamp(int v, int lo, int hi) { return (int16_t)(v < lo ? lo : v > hi ? hi : v); }
#define MAS_PIANO_CUT 55                  /* the piano's CUT (eng_sample.c: a gentle one-pole, 55: about 5 kHz) */
#define MAS_PIANO_ROOM 35                 /* the piano's reverb send over SPACE's (0..127) */

/* the TIME card in Felucca's delay times (params.c N_DTIME): 1/16, 1/8, 1/8D, 1/4 */
static const uint8_t MAS_DTIME[MA_DTIME_COUNT] = {2, 1, 10, 0};
static const char *const MAS_DTIME_NAMES[MA_DTIME_COUNT] = {"1/16", "1/8", "3/16", "1/4"};

/* the OOH's vowel glide: an eighth of the tempo, as the VOICE engine's TALK time (gen_tables.py: a value v is
 * 10000^(v/127) ms) */
static inline int16_t mas_talk(const ma_settings *s)
{
    float ms = 60000.0f / (float)(s->bpm ? s->bpm : 120) * 0.5f;
    /* v = 127 log10(ms) / 4, log10 without libm: by the powers of ten and a few steps of the series */
    int dec = 0;
    float x = ms;
    while (x >= 10.0f) { x *= 0.1f; dec++; }
    float y = (x - 1.0f) / (x + 1.0f), y2 = y * y;   /* ln(x) = 2 atanh(y) */
    float ln = 2.0f * y * (1.0f + y2 / 3.0f + y2 * y2 / 5.0f + y2 * y2 * y2 / 7.0f);
    float v = 127.0f * ((float)dec + ln * 0.4342945f) / 4.0f;
    return mas_clamp((int)(v + 0.5f), 0, 127);
}
/* the VOWELS card: VOWL and VOWL2 (A 0, E 32, I 64, O 95, U 127: eng_formant.c's five) */
static const uint8_t MAS_VOWELS[MA_VOWELS_COUNT][2] = {{127, 0}, {0, 64}, {95, 0}, {127, 64}, {0, 95}};
static const char *const MAS_VOWEL_NAMES[MA_VOWELS_COUNT] = {"OO-AH", "AH-EE", "OH-AH", "OO-EE", "AH-OH"};
static const char *const MAS_CHOIR_NAMES[MA_CHOIR_COUNT] = {"OH", "AH", "MIX"};

/* a part's track from the cards, over its sound's preset: the level, the place, the sends, TONE on the parts the
 * lift filters, ENSEMBLE on the stabs and the hook, the VOX page on a VOICE sound, and the matrix's slots for the
 * groove (above). Main loop, the audio held off (or the host) */
static inline void mas_track(track_t *t, const ma_groove *g, int part)
{
    const ma_settings *s = &g->s;
    const ma_style *st = &MA_STYLE[s->style];
    const mas_sound *snd = part == MA_DRUMS ? 0 : mas_of(g, part);
    bool lifted = (st->lift_parts >> part) & 1u;
    /* MIX: 100 is -2 dB (headroom: the master limiter stays out of it), each step down 0.3 dB (MIX 50: -17 dB), 0 off;
     * then the sound's trim (F_DB: half-dB steps); the stabs sit 2 dB lower, under the hook */
    int lvl = 108 - (100 - s->lvl[part]) * 6 / 10 + (snd ? snd->trim : MAS_KIT_TRIM[s->snd[3] % MA_KIT_COUNT]) +
              (part == MA_STABS ? -4 : 0);
    t->p[P_LEVEL] = mas_clamp(s->lvl[part] ? lvl : 0, 1, 127);
    /* the place: the stabs a little left and spread, the hook a little right, the bass and drums centred */
    int w = s->width;
    t->p[P_PAN] = (int16_t)(part == MA_STABS ? -w * 18 / 100 : part == MA_HOOK ? w * 18 / 100 : 0);
    t->p[P_SPRD] = mas_clamp(part == MA_STABS || (part == MA_HOOK && snd->voice == MAS_UNISON) ? w * 90 / 100 : 0, 0, 127);
    /* the sends */
    static const uint8_t REV[MA_PARTS] = {100, 30, 90, 35}, DLY[MA_PARTS] = {60, 0, 100, 0};
    t->p[P_REV] = mas_clamp(s->reverb * 127 / 100 * REV[part] / 100, 0, 127);
    t->p[P_DLY] = mas_clamp(s->delay * 127 / 100 * DLY[part] / 100, 0, 127);
    /* the matrix: the lift and the pump */
    t->p[P_M1SRC] = MS_MODW;
    t->p[P_M1DST] = (int16_t)(snd && snd->bright >= 0 ? MD_E1 + snd->bright : MD_OFF);
    t->p[P_M1AMT] = -32;
    t->p[P_M2SRC] = part == MA_DRUMS ? MS_OFF : MS_EXPR;
    t->p[P_M2DST] = part == MA_DRUMS ? MD_OFF : MD_AMP;
    t->p[P_M2AMT] = 64;
    if (part == MA_DRUMS) {
        t->p[P_VOICE] = V_POLY;
        if (s->snd[3] == MA_KIT_EURO) {           /* the samples: their velocity, SNAP as a low-pass (100: open) */
            t->p[P_M4SRC] = MS_VEL;
            t->p[P_M4DST] = MD_AMP;
            t->p[P_M4AMT] = 64;
            int lpf = 60 + s->snap * 68 / 100;
            if (lpf >= 127) {
                t->p[P_ITYPE] = 0;
            } else {
                t->p[P_ITYPE] = 5;                /* IT_CRUSH at 16 bits and the full rate: its low-pass alone */
                t->p[P_IA] = 127;
                t->p[P_IB] = 127;
                t->p[P_IC] = mas_clamp(lpf, 0, 127);
                t->p[P_IMIX] = 127;
            }
        } else {
            t->p[P_M4SRC] = MS_OFF;
            t->p[P_ITYPE] = 0;
            mas_kit_params(s, &t->p[P_E0]);
        }
        t->p[P_DIST] = 0;
        return;
    }
    if (snd->engine == MAS_SAMPLE) {             /* the piano: its velocity (the SAMPLE engine has none of its own), */
        t->p[P_M4SRC] = MS_VEL;                  /* a low-pass at about 5 kHz to take its edge off (the LIFT closes it */
        t->p[P_M4DST] = MD_AMP;                  /* further), and a room of its own over SPACE's REVERB: a piano */
        t->p[P_M4AMT] = 64;                      /* in a club, pushed back, not one in the listener's ear */
        t->p[P_E4] = MAS_PIANO_CUT;
        t->p[P_REV] = mas_clamp(t->p[P_REV] + MAS_PIANO_ROOM, 0, 127);
    } else {
        t->p[P_M4SRC] = MS_OFF;
    }
    if (snd->glide >= 0)
        t->p[P_GLIDE] = snd->glide;
    if (snd->voice >= 0)
        t->p[P_VOICE] = (int16_t)(snd->voice == MAS_UNISON ? V_UNISON : snd->voice == MAS_LEGATO ? V_LEGATO
                                  : snd->voice == MAS_MONO ? V_MONO : V_POLY);
    if (snd->engine == MAS_VOICE) {              /* the VOX page */
        t->p[P_E0] = MAS_VOWELS[s->vowels % MA_VOWELS_COUNT][0];
        t->p[P_E1] = MAS_VOWELS[s->vowels % MA_VOWELS_COUNT][1];
        t->p[P_E2] = mas_talk(s);
        t->p[P_E3] = s->shift;
        if (part == MA_HOOK && s->glide)
            t->p[P_GLIDE] = mas_clamp(s->glide * 60 / 100, 0, 127);
    }
    /* ENSEMBLE: the chorus on the stabs and the hook, over what the sound has of its own */
    if (part != MA_BASS)
        t->p[P_CHOR] = mas_clamp(s->ensemble * 127 / 100, 0, 127);
    /* TONE on the lifted parts: RESO, DRIVE, CRUSH. A lifted ANALOG or TRIO sound starts bright (CUT at least 100):
     * the lift brings the darkness, a dark preset under it would go silent */
    if (lifted) {
        if ((snd->engine == MAS_ANALOG || snd->engine == MAS_TRIO) && snd->bright >= 0 && t->p[P_E0 + snd->bright] < 100)
            t->p[P_E0 + snd->bright] = 100;
        if (snd->engine == MAS_ANALOG)
            t->p[P_E5] = mas_clamp(s->reso * 120 / 100, 0, 127);
        else if (snd->engine == MAS_TRIO)
            t->p[P_E6] = mas_clamp(s->reso * 120 / 100, 0, 127);
        t->p[P_DIST] = mas_clamp((snd->dist >= 0 ? snd->dist : 0) + s->drive * 50 / 100, 0, 127);   /* (DRIVE 100: 50) */
        if (s->crush) {
            t->p[P_ITYPE] = 5;                    /* IT_CRUSH: the bits fall as CRUSH rises, the rate with them */
            t->p[P_IA] = mas_clamp(127 - s->crush * 100 / 100, 16, 127);
            t->p[P_IB] = mas_clamp(127 - s->crush * 70 / 100, 0, 127);
            t->p[P_IC] = 110;
            t->p[P_IMIX] = 127;
        } else {
            t->p[P_ITYPE] = 0;
        }
    } else {
        if (snd->dist >= 0)
            t->p[P_DIST] = snd->dist;
        t->p[P_ITYPE] = 0;
    }
}

/* the song's own: the tempo for the delay and the LFOs, the delay, the reverb's room */
static inline void mas_song(const ma_settings *s)
{
    song.g[G_BPM] = s->bpm;
    song.g[G_DTIME] = MAS_DTIME[s->dtime % MA_DTIME_COUNT];
    song.g[G_DFDBK] = 45;
    song.g[G_DMIX] = 100;
    song.g[G_RSIZE] = (int16_t)(60 + s->reverb * 30 / 100);
    song.g[G_RDAMP] = 50;
    song.g[G_RTYPE] = 0;                          /* (Felucca's ROOM model) */
}

/* the block's controls into the tracks (the audio ISR, before the mix): the lift and the pump */
static inline void mas_block(const ma_groove *g)
{
    for (int p = 0; p < MA_PARTS; ++p) {
        trk[p].mw = (uint8_t)(127 - g->cut[p]);
        trk[p].ex_off = g->duck[p];
    }
}

#endif
