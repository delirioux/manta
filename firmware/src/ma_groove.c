/* SPDX-License-Identifier: GPL-3.0-only */
/* MANTA groove engine (see ma_groove.h). The styles, the chord loops, the parts' patterns on the clave, the solo's
 * phrase and the horns' riff, the form; in time, sixteenth by sixteenth, with swing and a human feel; the horns' tone,
 * gain and place every block. The frame (the clock, the notes held and pending, the keys) is Sill's. */
#include "ma_groove.h"

#include "tuna_sys.h"   /* memcpy and memset: the FM-1 has no C library (TUNA_FREESTANDING) */

/* ---- small helpers ---------------------------------------------------------------------------------------- */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static uint32_t xs(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}
static int rnd_n(uint32_t *s, int n) { return n > 0 ? (int)(xs(s) % (uint32_t)n) : 0; }
static float rnd_f(uint32_t *s) { return (float)(xs(s) >> 8) * (1.0f / 16777216.0f); }   /* 0..1 */

static void cpy(char *d, const char *s, int n)
{
    int i = 0;
    for (; s[i] && i < n - 1; ++i)
        d[i] = s[i];
    d[i] = 0;
}

/* sin and cos without libm: a parabola refined (abs error under 0.001), x in turns (1.0 a whole circle) */
static float sin_turns(float x)
{
    x -= (float)(int)x;
    if (x < 0.0f)
        x += 1.0f;
    float y = x < 0.5f ? x * 2.0f : (x - 0.5f) * 2.0f;   /* 0..1 over a half circle */
    float s = 4.0f * y * (1.0f - y);
    s = s * (0.775f + 0.225f * s);
    return x < 0.5f ? s : -s;
}
static float cos_turns(float x) { return sin_turns(x + 0.25f); }

/* ---- names ------------------------------------------------------------------------------------------------ */
static const char *const NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char *ma_note_name(int pc) { return NOTE_NAMES[((pc % 12) + 12) % 12]; }
static const char *const SCALE_NAMES[MA_SCALES] = {"MIN", "MAJ", "DOR", "HARM"};
const char *ma_scale_name(int i) { return SCALE_NAMES[clampi(i, 0, MA_SCALES - 1)]; }
/* (the screen's font is ASCII: the moña is written MONA) */
static const char *const SEC_NAMES[MA_SECS] = {"INTRO", "TEMA", "VERSO", "CORTE", "FRASE", "PIANO", "MONTUNO", "MAMBO",
                                               "MONA", "CIERRE"};
static const char *const SEC_SHORT[MA_SECS] = {"IN", "TE", "VE", "CO", "FR", "PI", "MO", "MB", "MN", "FI"};
const char *ma_sec_name(int s) { return SEC_NAMES[clampi(s, 0, MA_SECS - 1)]; }
const char *ma_sec_short(int s) { return SEC_SHORT[clampi(s, 0, MA_SECS - 1)]; }
static const char *const DR_NAMES[MA_DR_COUNT] = {
    "QUINTO", "CONGA", "CONGA M", "TUMBA", "BONGO H", "BONGO L", "BONGO M", "TIMBAL H", "TIMBAL L", "RIM", "PAILA",
    "BELL", "BONGO BELL", "BELL M", "AGOGO H", "AGOGO L", "CLAVE", "WOOD", "GUIRO", "GUIRO L", "SHAKER", "SHAKER B",
    "CABASA", "TRIANGLE", "TRI M", "TOM H", "TOM L", "KICK", "WASHBOARD", "WASH TAP"};
const char *ma_drum_name(int d) { return DR_NAMES[clampi(d, 0, MA_DR_COUNT - 1)]; }
static const char *const DR_SHORT[MA_DR_COUNT] = {
    "QUINTO", "CONGA", "CNG M", "TUMBA", "BNG H", "BNG L", "BNG M", "TMB H", "TMB L", "RIM", "PAILA", "BELL", "B BELL",
    "BELL M", "AGO H", "AGO L", "CLAVE", "WOOD", "GUIRO", "GUI L", "SHAKE", "SHK B", "CABASA", "TRI", "TRI M", "TOM H",
    "TOM L", "KICK", "WASH", "W TAP"};
const char *ma_drum_short(int d) { return DR_SHORT[clampi(d, 0, MA_DR_COUNT - 1)]; }
/* PERC's keys, low to high: the low drums at the bottom (27 keys, the 27 drums worth a key) */
static const uint8_t KEY_DRUM[27] = {
    MA_DR_KICK, MA_DR_TOM_LO, MA_DR_TOM_HI, MA_DR_TUMBA, MA_DR_CONGA, MA_DR_CONGA_MUTE, MA_DR_QUINTO, MA_DR_BONGO_LO,
    MA_DR_BONGO_HI, MA_DR_BONGO_MUTE, MA_DR_TIMBAL_LO, MA_DR_TIMBAL_HI, MA_DR_TIMBAL_RIM, MA_DR_PAILA, MA_DR_CLAVE,
    MA_DR_WOOD, MA_DR_BELL_MAMBO, MA_DR_BELL_BONGO, MA_DR_BELL_MUTE, MA_DR_AGOGO_LO, MA_DR_AGOGO_HI, MA_DR_GUIRO_SHORT,
    MA_DR_GUIRO_LONG, MA_DR_SHAKER_FWD, MA_DR_CABASA, MA_DR_TRIANGLE, MA_DR_WASH};
int ma_key_drum(int k) { return KEY_DRUM[clampi(k, 0, 26)]; }

/* EDIT's memory: the patterns playing, the sets', the edit's copy, and what each part played over the last two bars
 * (the instrument pages). Off the interrupts' RAM: the FM-1's pool (one groove: as the pending notes) */
#if TUNA_FREESTANDING
#define MA_POOL __attribute__((section(".pool")))
#else
#define MA_POOL
#endif
static ma_pats pats_now MA_POOL, pats_edit MA_POOL, pats_sets[MA_SETS] MA_POOL;
static uint8_t cap_n[3][MA_PAT_STEPS][MA_PAT_NOTES] MA_POOL;   /* MIDI, 0 none */
static uint8_t cap_len[3][MA_PAT_STEPS] MA_POOL;
static uint32_t cap_dr[MA_PAT_STEPS] MA_POOL;
ma_pats *ma_pats_now(void) { return &pats_now; }
ma_pats *ma_pats_set(int i) { return &pats_sets[clampi(i, 0, MA_SETS - 1)]; }
const ma_pats *ma_pats_edit(void) { return &pats_edit; }
static void pats_clear(ma_pats *p)
{
    memset(p, 0, sizeof *p);
    memset(p->n, MA_PAT_REST, sizeof p->n);
}

/* the scales, as semitones from the key */
static const uint8_t SCALE[MA_SCALES][7] = {
    {0, 2, 3, 5, 7, 8, 10},   /* MINOR (aeolian) */
    {0, 2, 4, 5, 7, 9, 11},   /* MAJOR */
    {0, 2, 3, 5, 7, 9, 10},   /* DORIAN: the son montuno's minor */
    {0, 2, 3, 5, 7, 8, 11},   /* HARMONIC MINOR: the bolero's and the tango's */
};
static int step_note(int sc, int base, int k)
{
    int o = k >= 0 ? k / 7 : -((-k + 6) / 7);
    int d = k - o * 7;
    return base + o * 12 + SCALE[sc][d];
}

int ma_snap(const ma_settings *s, int note)
{
    int sc = clampi(s->scale, 0, MA_SCALES - 1), pc = ((note - s->key) % 12 + 12) % 12, best = 0, bd = 99;
    for (int i = 0; i < 7; ++i) {
        int d = SCALE[sc][i] - pc;
        int ad = d < 0 ? -d : d;
        if (ad < bd || (ad == bd && d < 0)) {
            bd = ad;
            best = d;
        }
    }
    return note + best;
}

/* ---- the chord loops: scale degrees, a flavour (3 triads, 7 sevenths, 9 with the ninth), the dominant's major
 * third in minor. The Latin turnarounds: the son's I IV V, the montuno's ii V I, the minor descent of the bolero and
 * the tango, the bossa's sevenths ---- */
typedef struct { uint8_t n, deg[4], flavour, dom; } ma_loopdef;
static const ma_loopdef MA_LOOP[MA_LOOPS] = {
    {4, {0, 3, 4, 3}, 3, 1},   /* 1  I IV V IV: the son's guajeo */
    {4, {1, 4, 0, 0}, 7, 1},   /* 2  ii7 V7 I I: the montuno's turn */
    {4, {0, 4, 0, 4}, 3, 1},   /* 3  I V I V: merengue, cumbia */
    {4, {0, 3, 4, 0}, 3, 1},   /* 4  i iv V i: the minor salsa */
    {4, {0, 5, 3, 4}, 7, 1},   /* 5  i VI iv V7: the bolero's drama */
    {4, {0, 6, 5, 4}, 3, 1},   /* 6  i VII VI V: the Andalusian descent (tango, son) */
    {4, {0, 3, 6, 2}, 7, 0},   /* 7  i7 iv7 VII7 IIImaj7: the montuno in minor */
    {4, {0, 1, 4, 0}, 7, 1},   /* 8  Imaj7 ii7 V7 Imaj7: the bossa's */
    {4, {1, 4, 2, 5}, 9, 1},   /* 9  ii9 V9 iii vi: the bossa's sevenths walking */
    {4, {0, 4, 4, 0}, 3, 1},   /* 10 I V V I: the cumbia's two chords */
    {4, {0, 3, 0, 4}, 7, 1},   /* 11 i iv i V7: the tango's */
    {4, {0, 2, 3, 4}, 3, 1},   /* 12 I iii IV V: the cha-cha's climb */
    {4, {0, 2, 3, 4}, 3, 1},   /* 13 i III iv V: the old son's turn in minor, the guitar trio's */
};
void ma_loop_name(const ma_settings *s, int loop, char *out, int max)
{
    (void)s;
    char b[12] = "LOOP ";
    int v = clampi(loop, 0, MA_LOOPS - 1) + 1, k = 5;
    if (v >= 10) b[k++] = (char)('0' + v / 10);
    b[k++] = (char)('0' + v % 10);
    b[k] = 0;
    cpy(out, b, max);
}

/* ---- the families: each style's piano, bass, horns and percussion -------------------------------------------- */
enum { PF_MONTUNO, PF_CHACHA, PF_MERENGUE, PF_CUMBIA, PF_SAMBA, PF_GUAJEO, PF_BOLERO, PF_BOSSA, PF_TANGO, PF_COUNT };
enum { BF_TUMBAO, BF_CHACHA, BF_MERENGUE, BF_CUMBIA, BF_SAMBA, BF_BOLERO, BF_BOSSA, BF_TANGO, BF_COUNT };
enum { HF_SALSA, HF_MAMBO, HF_MERENGUE, HF_CUMBIA, HF_SAMBA, HF_SON, HF_CHACHA, HF_BOLERO, HF_BOSSA, HF_TANGO, HF_COUNT };
/* the solo's speed */
enum { SR_FAST, SR_MID, SR_SLOW };

/* name side bpm key scale | heat clave hits | snd[3] | loops | bars swing | piano bass horns perc | claves on |
 * room plate warmth | mute | the three more sounds of PIANO, BASS and HORNS a song picks from.
 * Sounds (ma_sounds.h): PIANO 0 UPRIGHT 1 MALLETS 2 ORGAN 3 TRES 4 HARP 5 ACCORDION 6 BANDONEON 7 GUITAR; BASS 0 BABY
 * 1 UPRIGHT 2 FM BASS 3 SUB 4 SLAP 5 FRETLESS 6 ROUND 7 TUBA; HORNS 0 TPT+TBN 1 TRUMPET 2 REQUINTO 3 SAX 4 PANPIPE 5 VIOLINS
 * 6 ACCORDION 7 BANDONEON. The percussion's set is the style's own (snd[3] = the style) */
const ma_style MA_STYLE[MA_STYLES] = {
    {"SALSA", MA_SIDE_CALIENTE, 130, 9, MA_SC_MINOR, 70, MA_CLAVE_32, MA_HITS_8, {0, 0, 0}, {3, 1, 6, 0}, 1, 0,
     PF_MONTUNO, BF_TUMBAO, HF_SALSA, MA_ST_SALSA, 1, 30, 35, 20, MA_MUTE_AUTO, {{7, 3, 0}, {1, 2, 5}, {1, 3, 0}}},
    {"MAMBO", MA_SIDE_CALIENTE, 150, 7, MA_SC_MAJOR, 85, MA_CLAVE_23, MA_HITS_8, {0, 1, 0}, {1, 0, 11, 2}, 1, 0,
     PF_MONTUNO, BF_TUMBAO, HF_MAMBO, MA_ST_MAMBO, 1, 35, 40, 25, MA_MUTE_AUTO, {{2, 0, 2}, {0, 2, 6}, {1, 0, 3}}},
    {"MERENGUE", MA_SIDE_CALIENTE, 138, 2, MA_SC_MAJOR, 80, MA_CLAVE_32, MA_HITS_8, {0, 2, 3}, {2, 0, 9, 3}, 1, 0,
     PF_MERENGUE, BF_MERENGUE, HF_MERENGUE, MA_ST_MERENGUE, 0, 25, 25, 25, MA_MUTE_AUTO,
     {{5, 7, 0}, {0, 6, 4}, {6, 0, 2}}},
    {"CUMBIA", MA_SIDE_CALIENTE, 98, 7, MA_SC_MINOR, 60, MA_CLAVE_32, MA_HITS_16, {5, 6, 6}, {9, 2, 3, 0}, 1, 10,
     PF_CUMBIA, BF_CUMBIA, HF_CUMBIA, MA_ST_CUMBIA, 0, 30, 20, 30, MA_MUTE_AUTO, {{7, 0, 2}, {0, 2, 3}, {4, 3, 2}}},
    {"SAMBA", MA_SIDE_CALIENTE, 102, 0, MA_SC_MAJOR, 85, MA_CLAVE_32, MA_HITS_8, {7, 1, 2}, {7, 0, 8, 1}, 1, 0,
     PF_SAMBA, BF_SAMBA, HF_SAMBA, MA_ST_SAMBA, 0, 30, 30, 20, MA_MUTE_AUTO, {{3, 0, 7}, {0, 5, 6}, {0, 4, 3}}},
    /* the son of old Havana: the upright's montuno, the acoustic bass, one trumpet, bongo, maracas and claves; laid
     * back, a warm room (a song now and then brings the guitar, the upright beside it) */
    {"SON", MA_SIDE_NOCHE, 100, 2, MA_SC_MINOR, 50, MA_CLAVE_23, MA_HITS_16, {0, 1, 1}, {12, 0, 3, 1}, 1, 0,
     PF_GUAJEO, BF_TUMBAO, HF_SON, MA_ST_SON, 1, 50, 25, 55, MA_MUTE_AUTO, {{0, 7, 0}, {1, 6, 1}, {1, 2, 1}}},
    {"CHA-CHA", MA_SIDE_NOCHE, 120, 5, MA_SC_MAJOR, 55, MA_CLAVE_23, MA_HITS_16, {0, 0, 5}, {11, 0, 1, 2}, 1, 0,
     PF_CHACHA, BF_CHACHA, HF_CHACHA, MA_ST_CHACHA, 0, 35, 35, 20, MA_MUTE_AUTO,
     {{0, 7, 2}, {1, 6, 2}, {4, 1, 3}}},
    {"BOLERO", MA_SIDE_NOCHE, 80, 2, MA_SC_HARMONIC, 35, MA_CLAVE_32, MA_HITS_OFF, {0, 6, 1}, {4, 5, 3, 1}, 1, 0,
     PF_BOLERO, BF_BOLERO, HF_BOLERO, MA_ST_BOLERO, 1, 50, 45, 25, MA_MUTE_AUTO, {{0, 0, 0}, {1, 5, 0}, {2, 3, 2}}},
    {"BOSSA", MA_SIDE_NOCHE, 136, 4, MA_SC_MAJOR, 40, MA_CLAVE_32, MA_HITS_OFF, {7, 5, 3}, {7, 8, 1, 6}, 1, 0,
     PF_BOSSA, BF_BOSSA, HF_BOSSA, MA_ST_BOSSA, 0, 40, 35, 30, MA_MUTE_AUTO, {{0, 7, 0}, {1, 6, 0}, {1, 4, 5}}},
    {"TANGO", MA_SIDE_NOCHE, 118, 9, MA_SC_HARMONIC, 70, MA_CLAVE_32, MA_HITS_8, {0, 1, 7}, {10, 5, 3, 4}, 1, 0,
     PF_TANGO, BF_TANGO, HF_TANGO, MA_ST_TANGO, 0, 30, 30, 35, MA_MUTE_AUTO, {{6, 7, 0}, {0, 2, 6}, {0, 7, 5}}},
    /* the guaracha: the son fast and cheeky, a conjunto's (the tres, the bongo, the maracas, a trumpet) */
    {"GUARACHA", MA_SIDE_CALIENTE, 140, 2, MA_SC_MAJOR, 80, MA_CLAVE_23, MA_HITS_8, {0, 1, 1}, {0, 1, 2, 11}, 1, 0,
     PF_GUAJEO, BF_TUMBAO, HF_SON, MA_ST_GUARACHA, 1, 30, 30, 30, MA_MUTE_AUTO, {{7, 0, 0}, {0, 6, 5}, {0, 1, 2}}},
    /* the danzon: Cuba's ballroom, a charanga's violins over the piano, the pailas' cinquillo, the guiro */
    {"DANZON", MA_SIDE_NOCHE, 112, 5, MA_SC_MAJOR, 45, MA_CLAVE_32, MA_HITS_16, {0, 1, 5}, {2, 11, 0, 7}, 2, 0,
     PF_CHACHA, BF_CHACHA, HF_CHACHA, MA_ST_DANZON, 0, 40, 35, 35, MA_MUTE_AUTO, {{0, 2, 7}, {0, 6, 5}, {4, 5, 3}}},
};
const char *ma_style_name(int i) { return MA_STYLE[clampi(i, 0, MA_STYLES - 1)].name; }

static const uint8_t SOLO_SPEED[HF_COUNT] = {SR_FAST, SR_FAST, SR_FAST, SR_MID, SR_FAST, SR_MID, SR_MID, SR_SLOW,
                                             SR_SLOW, SR_MID};

int ma_bars_per_chord(const ma_settings *s) { return s->bars >= 4 ? 4 : s->bars >= 2 ? 2 : 1; }

/* ---- settings --------------------------------------------------------------------------------------------- */
void ma_style_apply(ma_settings *s, int style)
{
    const ma_style *st = &MA_STYLE[clampi(style, 0, MA_STYLES - 1)];
    s->style = (uint8_t)clampi(style, 0, MA_STYLES - 1);
    s->heat = st->heat;
    s->clave = st->clave;
    s->hits = st->hits;
    s->solo = MA_SOLO_PHRASE;
    for (int i = 0; i < 3; ++i)
        s->snd[i] = st->snd[i];
    s->snd[3] = s->style;
    s->mute = st->mute;
    s->phrase = MA_PHR_GEN;
    s->bell = 70;
    s->key = st->key;
    s->scale = st->scale;
    s->chords = st->loops[0];
    s->bars = st->bars;
    s->swing = st->swing;
    s->montuno = 0;
    s->tumbao = 0;
    s->fills = MA_FILLS_8;
    s->conga = s->bongo = s->timbal = s->extra = 1;   /* AUTO: the style's own */
    s->room = st->room;
    s->plate = st->plate;
    s->warmth = st->warmth;
    s->human = 12;
    s->mine = 0;                                   /* (a new style: its own patterns; the parts' own kept for EDIT) */
    s->bpm = st->bpm;
    s->song = 0;
}

void ma_song_sounds(ma_settings *s)
{
    const ma_style *st = &MA_STYLE[clampi(s->style, 0, MA_STYLES - 1)];
    uint32_t r = (uint32_t)s->song * 2654435761u + (uint32_t)s->style * 40503u + 7u;
    for (int p = 0; p < 3; ++p) {
        int k = s->song ? rnd_n(&r, 4) : 0;
        s->snd[p] = k ? st->alt[p][k - 1] : st->snd[p];
    }
}

void ma_defaults(ma_settings *s)
{
    memset(s, 0, sizeof *s);
    ma_style_apply(s, MA_ST_SALSA);
    s->lvl[MA_PIANO] = 85;
    s->lvl[MA_BASS] = 90;
    s->lvl[MA_HORNS] = 85;
    s->lvl[MA_PERC] = 85;
    s->width = 70;
    s->p3mode = MA_P3_OFF;
    s->p3angle = 3;                                /* (30 degrees to the right) */
    s->p3height = 50;
    s->p3dist = 20;
    s->arc = 1;
    s->verse = 8;
    s->mbars = 8;
    s->mambo = 1;
    s->palette = 10;                               /* HAVANA (ui_palettes.h UI_HAVANA_INDEX) */
    s->look = 0;
    s->keysnap = 0;
    for (int i = 0; i < 4; ++i)
        s->bright[i] = 100;
}

void ma_settings_fix(ma_settings *s)
{
    s->style = (uint8_t)clampi(s->style, 0, MA_STYLES - 1);
    s->heat = (uint8_t)clampi(s->heat, 0, 100);
    s->clave = (uint8_t)clampi(s->clave, 0, MA_CLAVE_COUNT - 1);
    s->solo = (uint8_t)clampi(s->solo, 0, MA_SOLO_COUNT - 1);
    s->hits = (uint8_t)clampi(s->hits, 0, MA_HITS_COUNT - 1);
    for (int i = 0; i < 3; ++i)
        s->snd[i] = (uint8_t)clampi(s->snd[i], 0, MA_SOUNDS - 1);
    s->snd[3] = (uint8_t)clampi(s->snd[3], 0, MA_STYLES - 1);
    s->mute = (uint8_t)clampi(s->mute, 0, MA_MUTE_COUNT - 1);
    s->phrase = (uint8_t)clampi(s->phrase, 0, MA_PHR_COUNT - 1);
    s->bell = (uint8_t)clampi(s->bell, 0, 100);
    s->p3mode = (uint8_t)clampi(s->p3mode, 0, MA_P3_COUNT - 1);
    s->p3angle = (uint8_t)clampi(s->p3angle, 0, 35);
    s->p3height = (uint8_t)clampi(s->p3height, 0, 100);
    s->p3dist = (uint8_t)clampi(s->p3dist, 0, 100);
    s->key = (uint8_t)clampi(s->key, 0, 11);
    s->scale = (uint8_t)clampi(s->scale, 0, MA_SCALES - 1);
    s->chords = (uint8_t)clampi(s->chords, 0, MA_LOOPS - 1);
    s->bars = (uint8_t)(s->bars >= 4 ? 4 : s->bars >= 2 ? 2 : 1);
    s->swing = (uint8_t)clampi(s->swing, 0, 60);
    s->montuno = (uint8_t)clampi(s->montuno, 0, 3);
    s->tumbao = (uint8_t)clampi(s->tumbao, 0, 3);
    s->fills = (uint8_t)clampi(s->fills, 0, MA_FILLS_COUNT - 1);
    s->conga = (uint8_t)clampi(s->conga, 0, MA_CONGA_COUNT - 1);
    s->bongo = (uint8_t)clampi(s->bongo, 0, MA_BONGO_COUNT - 1);
    s->timbal = (uint8_t)clampi(s->timbal, 0, MA_TIMBAL_COUNT - 1);
    s->extra = (uint8_t)clampi(s->extra, 0, MA_EXTRA_COUNT - 1);
    for (int i = 0; i < 4; ++i)
        s->lvl[i] = (uint8_t)clampi(s->lvl[i], 0, 100);
    s->room = (uint8_t)clampi(s->room, 0, 100);
    s->plate = (uint8_t)clampi(s->plate, 0, 100);
    s->warmth = (uint8_t)clampi(s->warmth, 0, 100);
    s->width = (uint8_t)clampi(s->width, 0, 100);
    s->arc = (uint8_t)(s->arc ? 1 : 0);
    s->verse = (uint8_t)(s->verse >= 16 ? 16 : 8);
    s->mbars = (uint8_t)(s->mbars >= 16 ? 16 : 8);
    s->mambo = (uint8_t)(s->mambo ? 1 : 0);
    s->bpm = (uint8_t)clampi(s->bpm ? s->bpm : 184, 60, 220);
    s->look = (uint8_t)(s->look ? 1 : 0);
    s->keysnap = (uint8_t)(s->keysnap == 1);
    s->human = (uint8_t)clampi(s->human, 0, 100);
    if (s->song > 999)
        s->song = 0;
    s->mine &= 15u;
    for (int i = 0; i < 4; ++i)
        s->bright[i] = (uint8_t)clampi(s->bright[i], 0, 100);
}

/* ---- the clave: (bar, step) of the pattern, the sides by CLAVE ----------------------------------------------- */
/* the son clave 3-2 on two bars of sixteenths: 1, 2-and, 4 | 2, 3. BOSSA's: its second side 2, 3-and; the rumba's
 * third stroke late (the CUMBIA and SAMBA play none: their sticks rest) */
static const uint8_t CLAVE_SON[2][16] = {{1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0},
                                         {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}};
static const uint8_t CLAVE_BOSSA[2][16] = {{1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0},
                                           {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0}};
/* the bar of the 3-2 pattern bar b plays (0 the three side): 2-3 starts on the two side */
static int cbar(const ma_groove *g, int b) { return ((b < 0 ? 0 : b) + (g->s.clave == MA_CLAVE_23 ? 1 : 0)) & 1; }
int ma_clave_side(const ma_groove *g, int bar) { return cbar(g, bar) == 0 ? 3 : 2; }
bool ma_clave_hit(const ma_groove *g, int bar, int step)
{
    const uint8_t (*c)[16] = g->s.style == MA_ST_BOSSA ? CLAVE_BOSSA : CLAVE_SON;
    return c[cbar(g, bar)][step & 15] != 0;
}

/* ---- the song: the chords, the phrase, the riff -------------------------------------------------------------- */
static void chord_name(ma_chord *c, int third, int fifth, int seventh, bool ninth)
{
    const char *q;
    if (third == 3 && fifth == 6)
        q = seventh ? "M7B5" : "DIM";
    else if (third == 3)
        q = !seventh ? "MIN" : ninth ? "MIN9" : "MIN7";
    else if (seventh == 10)
        q = ninth ? "9" : "7";
    else if (seventh == 11)
        q = ninth ? "MAJ9" : "MAJ7";
    else
        q = "";
    char b[10];
    cpy(b, NOTE_NAMES[c->root % 12], sizeof b);
    int k = 0;
    while (b[k]) k++;
    for (int i = 0; q[i] && k < 7; ++i)
        b[k++] = q[i];
    b[k] = 0;
    cpy(c->name, b, sizeof c->name);
}

/* the chord on scale degree deg: its tones and a four-note voicing near center, of its inversions the one whose middle
 * is closest (a pianist's hand leading the voices) */
static void build_chord(ma_chord *c, const ma_settings *s, int deg, int flavour, bool dom, int *center)
{
    int sc = clampi(s->scale, 0, MA_SCALES - 1);
    int r = SCALE[sc][deg % 7];
    int iv[5];
    for (int k = 0; k < 5; ++k) {
        int d = deg + 2 * k;
        iv[k] = SCALE[sc][d % 7] + 12 * (d / 7) - r;
    }
    if (dom && deg % 7 == 4) {                     /* the dominant: a major third, a minor seventh (in minor too) */
        iv[1] = 4;
        iv[3] = 10;
    }
    int third = iv[1], fifth = iv[2], seventh = flavour >= 7 ? iv[3] : (dom && deg % 7 == 4 ? 10 : 0);
    bool ninth = flavour == 9 && (iv[4] == 14) && seventh;
    c->root = (uint8_t)((s->key + r) % 12);
    c->tone[0] = c->root;
    c->tone[1] = (uint8_t)((c->root + third) % 12);
    c->tone[2] = (uint8_t)((c->root + fifth) % 12);
    c->tone[3] = (uint8_t)((c->root + (seventh ? seventh : 12)) % 12);
    chord_name(c, third, fifth, seventh, ninth);
    int pcs[4] = {ninth ? (c->root + 14) % 12 : c->root, c->tone[1], c->tone[2], seventh ? c->tone[3] : (c->root + 12) % 12};
    int best_d = 1 << 20;
    int bestv[4] = {60, 64, 67, 72};
    for (int inv = 0; inv < 4; ++inv) {
        for (int base = 52; base <= 68; ++base) {
            if ((base % 12) != pcs[inv])
                continue;
            int v[4], p = base;
            v[0] = p;
            for (int j = 1; j < 4; ++j) {
                int pc = pcs[(inv + j) % 4];
                int x = p + 1;
                while (x % 12 != pc)
                    ++x;
                v[j] = x;
                p = x;
            }
            int mid = (v[0] + v[3]) / 2;
            int d = (mid - *center) * (mid - *center) + (v[3] > 79 ? 400 : 0);
            if (d < best_d) {
                best_d = d;
                memcpy(bestv, v, sizeof bestv);
            }
        }
    }
    for (int j = 0; j < 4; ++j)
        c->note[j] = (uint8_t)bestv[j];
    c->note[4] = 0;
    c->n = 4;
    *center = (bestv[0] + bestv[3]) / 2;
}

/* the solo's one-bar rhythms, by speed: per step the length in sixteenths of the note starting there (0 none). The
 * fast ones syncopated round the clave, the slow ones sung */
static const uint8_t SOLO_RHY[3][4][16] = {
    /* (FAST, the fast styles' (two bars to the clave, an eighth their pulse): broad, sung lines on the eighths, four or
     * five notes a bar, held; MID syncopated on the sixteenths too, the tresillo; a Havana trumpet's line) */
    [SR_FAST] = {{4, 0, 0, 0, 2, 0, 2, 0, 4, 0, 0, 0, 4, 0, 0, 0},
                 {2, 0, 4, 0, 0, 0, 2, 0, 6, 0, 0, 0, 0, 0, 2, 0},
                 {6, 0, 0, 0, 0, 0, 2, 0, 4, 0, 0, 0, 4, 0, 0, 0},
                 {0, 0, 4, 0, 0, 0, 2, 0, 8, 0, 0, 0, 0, 0, 0, 0}},
    [SR_MID] = {{3, 0, 0, 3, 0, 0, 2, 0, 0, 2, 0, 1, 4, 0, 0, 0},
                {0, 0, 2, 0, 1, 3, 0, 0, 2, 0, 0, 1, 4, 0, 0, 0},
                {2, 0, 1, 2, 0, 1, 2, 0, 0, 0, 2, 0, 4, 0, 0, 0},
                {4, 0, 0, 0, 2, 0, 0, 2, 0, 1, 2, 0, 4, 0, 0, 0}},
    [SR_SLOW] = {{6, 0, 0, 0, 0, 0, 2, 0, 8, 0, 0, 0, 0, 0, 0, 0},
                 {4, 0, 0, 0, 4, 0, 0, 0, 6, 0, 0, 0, 0, 0, 2, 0},
                 {3, 0, 0, 3, 0, 0, 2, 0, 8, 0, 0, 0, 0, 0, 0, 0},
                 {8, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 4, 0, 0, 0}},
};

/* a bar of the phrase into dst from rhythm rhy, its pitches a walk from *note within lo..hi: repeats, steps, now and
 * then a leap (a singer's line: mostly steps) */
static void phrase_bar(int8_t *dst, uint8_t *len, const uint8_t *rhy, uint32_t *r, int *note, int lo, int hi)
{
    for (int i = 0; i < 16; ++i) {
        dst[i] = -128;
        len[i] = 0;
        if (!rhy[i])
            continue;
        dst[i] = (int8_t)*note;
        len[i] = rhy[i];
        int roll = rnd_n(r, 10), mv;
        if (roll < 2) mv = 0;
        else if (roll < 7) mv = rnd_n(r, 2) ? 1 : -1;
        else if (roll < 9) mv = rnd_n(r, 2) ? 2 : -2;
        else mv = rnd_n(r, 2) ? 4 : -3;
        int nn = *note + mv;
        if (nn > hi || nn < lo)
            nn = *note - mv;
        *note = clampi(nn, lo, hi);
    }
}

/* the phrase: four bars, A A' A A'' (a motif and its answers, the last landing home), in the style's speed. Scale
 * steps from the tonic. (TRAD: the old songs' phrases come with their transcriptions; until then GEN's) */
static void make_phrase(ma_groove *g, uint32_t seed)
{
    const ma_style *st = &MA_STYLE[g->s.style];
    uint32_t r = seed * 2654435761u + 0x51ED27u;
    int sp = SOLO_SPEED[st->horns];
    int lo = -2, hi = 9;
    int note = rnd_n(&r, 3) * 2;
    const uint8_t *rhy = SOLO_RHY[sp][rnd_n(&r, 4)];
    int8_t a[16], b[16], c[16];
    uint8_t al[16], bl[16], cl[16];
    phrase_bar(a, al, rhy, &r, &note, lo, hi);
    memcpy(b, a, sizeof b);
    memcpy(bl, al, sizeof bl);
    memcpy(c, a, sizeof c);
    memcpy(cl, al, sizeof cl);
    int8_t t[16];
    uint8_t tl[16];
    note = a[0] != -128 ? a[0] : 2;
    phrase_bar(t, tl, rhy, &r, &note, lo, hi);
    for (int i = 8; i < 16; ++i) {
        b[i] = t[i];
        bl[i] = tl[i];
    }
    phrase_bar(t, tl, SOLO_RHY[sp][rnd_n(&r, 4)], &r, &note, lo, hi);
    for (int i = 6; i < 16; ++i) {
        c[i] = t[i];
        cl[i] = tl[i];
    }
    for (int i = 15; i >= 8; --i)
        if (b[i] != -128) {
            b[i] = (int8_t)(rnd_n(&r, 2) ? 4 : 2);  /* the answer rests on the fifth or the third */
            break;
        }
    for (int i = 15; i >= 0; --i)
        if (c[i] != -128) {
            c[i] = 0;                               /* the last lands home */
            cl[i] = (uint8_t)(16 - i);
            break;
        }
    for (int i = 0; i < 16; ++i) {
        g->phr[i] = a[i];
        g->phr_len[i] = al[i];
        g->phr[16 + i] = b[i];
        g->phr_len[16 + i] = bl[i];
        g->phr[32 + i] = a[i];
        g->phr_len[32 + i] = al[i];
        g->phr[48 + i] = c[i];
        g->phr_len[48 + i] = cl[i];
    }
}

/* the horns' riff rhythms: two bars (the clave's), per step a hit (1 short, 2 long, 3 held). Written on 3-2 */
static const char *const RIFF_RHY[HF_COUNT][2] = {
    [HF_SALSA] = {"1..1..2...1.2.......1.1.2..1.3..", "2.....1..1..1...1..1..2.....3..."},
    [HF_MAMBO] = {"1.1..1.1..2.1...1..1..1.1.1.3...", "2..2..2...1.2...1.1.1.1.2...3..."},
    [HF_MERENGUE] = {"1.11.11.1.11.11.1.11.11.2...2...", "11.11.11.1.1.1..11.11.11.2...3.."},
    [HF_CUMBIA] = {"2...1.1.2...1.1.2...1.1.3.......", "1.1.2...1.1.2...1.1.1.1.3......."},
    [HF_SAMBA] = {"1..1..1...1..1..1..1..2...3.....", "..1..1.1..1..1.1..1..1.12...3..."},
    [HF_SON] = {"2...1.1.2...2...1.1.2...3.......", "....2...1.1.2...2...1.1.3......."},
    [HF_CHACHA] = {"2.......1.1.2...2.......1.1.3...", "1.1.2...1.1.2...2.......1.1.3..."},
    [HF_BOLERO] = {"3...............2.......2.......", "2.......2.......3..............."},
    [HF_BOSSA] = {"2.....2.....2...3...............", "..2...2...2.....3..............."},
    [HF_TANGO] = {"1...1...1...1...1.1.1.1.3.......", "1..1..1.2.......1...1...3......."},
};

/* the riff: the rhythm of the family (one of two), its notes a walk over the chord's tones (0 root .. 5 the fifth an
 * octave up), each hit voiced as two horns a chord tone apart (ma_groove.c play_horns) */
static void make_riff(ma_groove *g, uint32_t seed)
{
    const ma_style *st = &MA_STYLE[g->s.style];
    uint32_t r = seed * 2246822519u + 0x2C1B3C6Du;
    const char *rhy = RIFF_RHY[st->horns][rnd_n(&r, 2)];
    int t = 3 + rnd_n(&r, 2);
    for (int i = 0; i < MA_RIFF_LEN; ++i) {
        g->riff[i] = -128;
        g->riff_len[i] = 0;
        char ch = rhy[i];
        if (ch < '1' || ch > '3')
            continue;
        g->riff[i] = (int8_t)t;
        g->riff_len[i] = (uint8_t)(ch == '1' ? 1 : ch == '2' ? 2 : 6);
        int mv = rnd_n(&r, 5) - 2;                  /* a step or two along the chord, or the same */
        t = clampi(t + mv, 2, 5);
    }
}

void ma_rebuild(ma_groove *g)
{
    ma_settings *s = &g->s;
    const ma_loopdef *L = &MA_LOOP[clampi(s->chords, 0, MA_LOOPS - 1)];
    int center = 64;
    g->nloop = L->n;
    for (int i = 0; i < L->n; ++i)
        build_chord(&g->loop[i], s, L->deg[i], L->flavour, L->dom != 0, &center);
    uint32_t seed = (uint32_t)s->style * 7919u + (uint32_t)s->song * 104729u + 17u;
    uint32_t r = seed;
    g->pat = (uint8_t)rnd_n(&r, 4);
    make_phrase(g, seed + (uint32_t)g->phr_var * 31u);
    make_riff(g, seed + (uint32_t)g->riff_var * 57u);
}

/* ---- the patterns: two bars (a clave, written 3-2), per step a character ------------------------------------- */
/* the piano: '.' rest, 'o' a chord tone in octaves, 't' the triad, 'n' the next chord's triad (the anticipation on the
 * last offbeat), 'a' an arpeggio's note, 'b' the low root (the left hand), 'c' the chord low (the left hand's);
 * upper case accented. Four of each family (GROOVE's MONTUNO) */
static const char *const PIANO_PAT[PF_COUNT][4] = {
    [PF_MONTUNO] = {"o...t.o...t.o.n.o.t...t.o.t...n.", "o..to..to..to.n.o..to..to.t.o.n.",
                    "O...t.o...t.o.t.o.t.o...o.t.o.n.", "o.t...t.o.t.o.n.o.t.o.t...t.o.n."},
    [PF_CHACHA] = {"t...t...t.t.t...t...t...t.t.n...", "o.t.o.t.o.t.t...o.t.o.t.o.t.n...",
                   "T.....t...t.t...T.....t...t.n...", "t.t.t...t.t.t...t.t.t...t.t.n..."},
    [PF_MERENGUE] = {"o.t.o.t.o.t.o.t.o.t.o.t.o.t.o.n.", "o.to.to.o.to.to.o.to.to.o.to.tn.",
                     "O.t.o.t.O.t.o.t.O.t.o.t.O.t.o.n.", "o.t.t.o.t.t.o.t.o.t.t.o.t.t.o.n."},
    [PF_CUMBIA] = {"..t...t...t...t...t...t...t...t.", "..t...t...t..tt...t...t...t..tn.",
                   "..T...t...T...t...T...t...T...t.", "..t.t.t...t.t.t...t.t.t...t.t.n."},
    [PF_SAMBA] = {"..t.t..t.t..t.t...t.t..t.t..t.n.", "t..t..t...t..t..t..t..t...t..n..",
                  "..t.t..t..t.t.t...t.t..t..t.t.n.", "t.t..t.t..t..t.tt.t..t.t..t..n.."},
    [PF_GUAJEO] = {"o.o.o.o.o.o.o.n.o.o.o.o.o.o.o.n.", "o..oo..oo..oo.n.o..oo..oo..oo.n.",
                   "o.o.o...o.o.o.n.o.o.o...o.o.o.n.", "O.o.o.o.O.o.o.n.O.o.o.o.O.o.o.n."},
    [PF_BOLERO] = {"b.a.a.a.b.a.a.a.b.a.a.a.b.a.a.a.", "b...t...a.a.t...b...t...a.a.t...",
                   "b.a.t...b.a.t...b.a.t...b.a.n...", "B...a.a.a.a.a.a.B...a.a.a.a.a.a."},
    [PF_BOSSA] = {"t..t..t...t..t..t..t..t...t..n..", "t...t..t..t...t.t...t..t..t...n.",
                  "..t..t..t...t...t..t..t...t..n..", "t..t...t..t.t...t..t...t..t.n..."},
    [PF_TANGO] = {"B...c...B...c...B...c...B.c.c...", "B..c..B.c...B...B..c..B.c...B...",
                  "B.c.c.c.B.c.c.c.B.c.c.c.B.c.c.c.", "B...c...c...c...B...c...c...c.c."},
};

/* the broken montuno of the groove's sections: 'o' the chord tone in octaves on the strong places, 'r' a chord tone
 * at a time, up and back, on the syncopated sixteenths between; 'n' the next chord anticipated */
static const char *const ARP_PAT8[2] = {   /* (the fast styles': on the eighths) */
    "o.r.r.r.o.r.r.n.o.r.r.r.o.r.r.n.", "o.r.o.r.r.r.o.n.o.r.o.r.r.r.o.n.",
};
static const char *const ARP_PAT[4] = {
    "o.r.rr.ro.r.rr.no.r.rr.ro.rr.r.n", "o..rr.r.o.rr.r.no..rr.r.o.r.rr.n",
    "orr.r.rrorr.r.rnorr.r.rrorr.r.rn", "o.rro.rro.rro.rno.rro.rro.r.o.rn",
};

/* the guitar, when the PIANO part plays the GUITAR: a style's two comping patterns over two bars (the PIANO card on
 * GROOVE picks one, 1 and 3 the first). The guitar is Felucca's PHYS string: three voices, so a strum is three
 * strings. 'D' / 'd' a down-strum, low to high (accented / soft); 'U' / 'u' an up-strum, high to low, softer; 'm' the
 * muted chuck, damped at once; 'b' the thumb on the root, 'f' on the fifth; 'c' the fingers, the top two at once (the
 * thumb's note rings on under them); 'a' the arpeggio, a string at a time, up and back */
static const char *const GUITAR_PAT[MA_STYLES][2] = {
    [MA_ST_SALSA] = {"D.u...d.m.u.d.u.D.u.d...m.u.d.u.", "b.u.m.u.b.u.m.u.b.u.m.u.f.u.m.u."},
    [MA_ST_MAMBO] = {"D.u...d.m.u.d.u.D.u.d...m.u.d.u.", "b.u.m.u.b.u.m.u.b.u.m.u.f.u.m.u."},
    [MA_ST_MERENGUE] = {"D.u.m.u.D.u.m.u.D.u.m.u.D.u.m.u.", "a.a.a.a.a.a.a.a.a.a.a.a.a.a.a.a."},
    [MA_ST_CUMBIA] = {"b.u.f.u.b.u.f.u.b.u.f.u.b.u.f.u.", "b...u.u.f...u.u.b...u.u.f...u.u."},
    [MA_ST_SAMBA] = {"b.cc.c.cf.cc.c.cb.cc.c.cf.c..c.c", "b..c.c.cf..c.c.cb..c.c.cf.c.c.c."},
    [MA_ST_SON] = {"b.u.m.u.b.u.m.u.b.u.m.u.f.u.m.u.", "D..u..u.m.u.D.u.D..u.d.um.u.d.u."},
    [MA_ST_CHACHA] = {"D...m...D...d.d.D...m...D...d.d.", "b.u.m.u.f.u.d.d.b.u.m.u.f.u.d.d."},
    [MA_ST_BOLERO] = {"b.a.a.a.f.a.a.a.b.a.a.a.f.a.a.a.", "b...a.a.f...a.a.b...a.a.f.a.a.a."},
    [MA_ST_BOSSA] = {"b..c..c.f.c..c..b..c..c.f..c..c.", "b.c..c.cf.c..c..b.c..c.cf..c.c.."},
    [MA_ST_TANGO] = {"D...D...D...D...D...D...D...D...", "D..D..D.D...D...D..D..D.D...D..."},
    [MA_ST_GUARACHA] = {"D.u...d.m.u.d.u.D.u.d...m.u.d.u.", "b.u.m.u.b.u.m.u.b.u.m.u.f.u.m.u."},
    [MA_ST_DANZON] = {"b.a.a.a.f.a.a.a.b.a.a.a.f.a.a.a.", "D...m...D...d.d.D...m...D...d.d."},
};
/* the bass: '.' rest, 'r' root, 'f' fifth, 'o' octave, 't' third, 'n' the next chord's root (the tumbao's
 * anticipation), 'p' a semitone below the next root (the approach); '-' holds the note before. Four of each family
 * (GROOVE's TUMBAO) */
static const char *const BASS_PAT[BF_COUNT][4] = {
    [BF_TUMBAO] = {"......f-----n-------f-----n-----", "......f-----n-----......f---n---",
                   "r-....f-----n-------f-----n-----", "......f-----n---....f-f---n-----"},
    [BF_CHACHA] = {"r-------f-----r-r-------f---r-r-", "r-------f---f-r-r-------f---r-p-",
                   "r-----r-f-----r-r-----r-f-----n-", "r-------f-------r-------f---r-p-"},
    [BF_MERENGUE] = {"r-..f-..r-..f-p-r-..f-..r-..f-p-", "r-.rf-..r-.rf-..r-.rf-..r-.rf-p-",
                     "r-..f-..o-..f-..r-..f-..o-..f-p-", "r-..r-..f-..f-..r-..r-..f-..f-p-"},
    [BF_CUMBIA] = {"r---f-..r---f-p-r---f-..r---f-p-", "r-..f-..r-..f-..r-..f-..r-..f-p-",
                   "r---o---f---o---r---o---f---n---", "r-.rf---r-.rf-p-r-.rf---r-.rf-p-"},
    [BF_SAMBA] = {"r-.r--f-r-.r--f-r-.r--f-r-.r--p-", "r--rf---r--rf---r--rf---r--rf-p-",
                  "r-..f---r-..f---r-..f---r-..f-n-", "r-.rf-.fr-.rf-.fr-.rf-.fr-.rf-p-"},
    [BF_BOLERO] = {"r-------f-------r-------f-----p-", "r-----r-f-------r-----r-f-----p-",
                   "r-------t-------f-------o-------", "r-------f---r---r-------f---p---"},
    [BF_BOSSA] = {"r-----f-f-----n-r-----f-f-----n-", "r-------f-------r-------f-----p-",
                  "r-----f-f-------r-----f-f-----p-", "r-..r-f-f-----n-r-..r-f-f-----p-"},
    [BF_TANGO] = {"r-..f-..r-..f-..r-..f-..r-p-n-..", "r-....r-..r-....r-....r-..p-....",
                  "r-..t-..f-..o-..r-..f-..t-..p-..", "r-....f-..r-....r-....f-..p-...."},
};

/* the percussion: per style, lanes of two bars each, a drum, its PERC page group and its pattern ('.' rest, 'o' soft,
 * 'x' a hit, 'X' accented). The montuno's and the mambo's (and the moña's) bongo bell and timbal bell are the
 * switch of the AUTO lanes there (perc_step) */
enum { G_CONGA, G_BONGO, G_TIMBAL, G_EXTRA, G_CLAVE, G_KICK, G_OTHER, G_COUNT };
typedef struct { uint8_t drum, group; const char *p; } lane_t;
#define LANES_MAX 9
typedef struct { uint8_t n; lane_t l[LANES_MAX]; } perc_set_t;
/* the generic lanes the PERC cards pick (TUMBAO MARCHA BOLERO, MARTILLO BELL, CASCARA BELL, GUIRO SHAKER CABASA) */
#define L_TUMBAO_0 {MA_DR_CONGA_MUTE, G_CONGA, "o.o.X.o.o.o...o.o.o.X.o.o.o...o."}
#define L_TUMBAO_1 {MA_DR_CONGA, G_CONGA, "............x.x.............x.x."}
#define L_TUMBAO_2 {MA_DR_TUMBA, G_CONGA, "..............x...............x."}
static const lane_t L_TUMBAO[3] = {L_TUMBAO_0, L_TUMBAO_1, L_TUMBAO_2};
#define L_MARCHA_0 {MA_DR_CONGA_MUTE, G_CONGA, "o...X...o...o...o...X...o...o..."}
#define L_MARCHA_1 {MA_DR_CONGA, G_CONGA, "..o.......o.x.x...o.......o.x.x."}
#define L_MARCHA_2 {MA_DR_QUINTO, G_CONGA, "......x...............x........."}
static const lane_t L_MARCHA[3] = {L_MARCHA_0, L_MARCHA_1, L_MARCHA_2};
#define L_CBOLERO_0 {MA_DR_CONGA_MUTE, G_CONGA, "o.......X.......o.......X......."}
#define L_CBOLERO_1 {MA_DR_TUMBA, G_CONGA, "............x.x.............x.x."}
static const lane_t L_CBOLERO[2] = {L_CBOLERO_0, L_CBOLERO_1};
#define L_MARTILLO_0 {MA_DR_BONGO_HI, G_BONGO, "X...x...X...x...X...x...X...x..."}
#define L_MARTILLO_1 {MA_DR_BONGO_MUTE, G_BONGO, "..o...o...o.......o...o...o....."}
#define L_MARTILLO_2 {MA_DR_BONGO_LO, G_BONGO, "..............x...............x."}
static const lane_t L_MARTILLO[3] = {L_MARTILLO_0, L_MARTILLO_1, L_MARTILLO_2};
#define L_BBELL_0 {MA_DR_BELL_BONGO, G_BONGO, "X...x...X...x...X...x...X...x.x."}
static const lane_t L_BBELL[1] = {L_BBELL_0};
#define L_CASCARA_0 {MA_DR_PAILA, G_TIMBAL, "x...x...x.x...x.x...x.x...x...x."}
static const lane_t L_CASCARA[1] = {L_CASCARA_0};
#define L_TBELL_0 {MA_DR_BELL_MAMBO, G_TIMBAL, "X...x...X...x...X...x.x.X...x..."}
#define L_TBELL_1 {MA_DR_BELL_MUTE, G_TIMBAL, "..o...o...o...o...o.......o...o."}
static const lane_t L_TBELL[2] = {L_TBELL_0, L_TBELL_1};
#define L_GUIRO_0 {MA_DR_GUIRO_LONG, G_EXTRA, "x.......x.......x.......x......."}
#define L_GUIRO_1 {MA_DR_GUIRO_SHORT, G_EXTRA, "....o.o.....o.o.....o.o.....o.o."}
static const lane_t L_GUIRO[2] = {L_GUIRO_0, L_GUIRO_1};
#define L_SHAKER_0 {MA_DR_SHAKER_FWD, G_EXTRA, "x.o.x.o.x.o.x.o.x.o.x.o.x.o.x.o."}
#define L_SHAKER_1 {MA_DR_SHAKER_BACK, G_EXTRA, ".o.o.o.o.o.o.o.o.o.o.o.o.o.o.o.o"}
static const lane_t L_SHAKER[2] = {L_SHAKER_0, L_SHAKER_1};
#define L_CABASA_0 {MA_DR_CABASA, G_EXTRA, "x.o.x.o.x.o.x.o.x.o.x.o.x.o.x.o."}
static const lane_t L_CABASA[1] = {L_CABASA_0};
/* the washboard: a colour, not a beat: one scrape a bar; now and then, hot, a tap of the thimbles on the half bar
 * ('h': only with HEAT high, one bar in four) */
#define L_WASH_0 {MA_DR_WASH, G_EXTRA, "X...............X..............."}
#define L_WASH_1 {MA_DR_WASH_TAP, G_EXTRA, "........................h......."}
static const lane_t L_WASH[2] = {L_WASH_0, L_WASH_1};

/* the texture of the groove's sections (the intro, the verso, the piano's): the maracas' sixteenths and a woodblock's
 * syncopations around the clave, as a Havana band's percussionists add them (the dance styles; HEAT from 35) */
#define L_TX_0 {MA_DR_SHAKER_FWD, G_EXTRA, "x.oxx.oxx.oxx.oxx.oxx.oxx.oxx.ox"}
#define L_TX_1 {MA_DR_WOOD, G_OTHER, "..x..x....x..x....x..x.x...x..x."}
static const lane_t L_TX[2] = {L_TX_0, L_TX_1};
static const lane_t L_TX8[2] = {{MA_DR_SHAKER_FWD, G_EXTRA, "x.o.x.o.x.o.x.o.x.o.x.o.x.o.x.o."},
                                {MA_DR_WOOD, G_OTHER, "..x...x...x.x.......x...x.x...x."}};   /* (fast: on the eighths) */

static const perc_set_t PERC[MA_STYLES] = {
    [MA_ST_SALSA] = {7, {L_TUMBAO_0, L_TUMBAO_1, L_TUMBAO_2, L_MARTILLO_0, L_MARTILLO_1, L_MARTILLO_2,
                         L_CASCARA_0}},
    [MA_ST_MAMBO] = {8, {L_TUMBAO_0, L_TUMBAO_1, L_TUMBAO_2, L_MARTILLO_0, L_MARTILLO_1, L_CASCARA_0,
                         {MA_DR_GUIRO_SHORT, G_EXTRA, "......o...............o........."},   /* (a colour) */
                         {MA_DR_TIMBAL_LO, G_OTHER, "..............................x."}}},
    [MA_ST_MERENGUE] = {6, {{MA_DR_TUMBA, G_CONGA, "X.......x.......X.......x......."},
                            {MA_DR_CONGA_MUTE, G_CONGA, "..o...o.o.o...o...o...o.o.o...o."},
                            {MA_DR_QUINTO, G_CONGA, "....x.......x.x.....x.......x.x."},
                            {MA_DR_GUIRO_LONG, G_EXTRA, "x..............................."},   /* the guira's scrape on the
                                                                                     * guiro: a colour, once in two bars */
                            {MA_DR_KICK, G_KICK, "x...x...x...x...x...x...x...x..."},
                            {MA_DR_BONGO_MUTE, G_BONGO, "......o.......o.......o.......o."}}},
    [MA_ST_CUMBIA] = {6, {{MA_DR_CONGA, G_CONGA, "....x.......x.......x.......x..."},
                          {MA_DR_CONGA_MUTE, G_CONGA, "..o.....o.o...o...o.....o.o...o."},
                          {MA_DR_QUINTO, G_CONGA, "...........x...x...........x.x.."},
                          /* the guacharaca's scrape: a colour, once in two bars, not the beat (the shaker keeps it) */
                          {MA_DR_GUIRO_LONG, G_EXTRA, "................o..............."},
                          {MA_DR_GUIRO_SHORT, G_EXTRA, "..............................o."},
                          {MA_DR_SHAKER_FWD, G_EXTRA, "x.o.x.o.x.o.x.o.x.o.x.o.x.o.x.o."}}},
    [MA_ST_SAMBA] = {7, {{MA_DR_KICK, G_KICK, "o.......o.......o.......o......."},
                         {MA_DR_TOM_LO, G_KICK, "....X.......X.......X.......X..."},
                         {MA_DR_BONGO_MUTE, G_BONGO, "x.x..x.x.x..x.x.x.x..x.x.x..x.x."},
                         {MA_DR_AGOGO_LO, G_TIMBAL, "x.....x...x.....x.....x...x....."},
                         {MA_DR_AGOGO_HI, G_TIMBAL, "..x.x...x...x.x...x.x...x...x.x."},
                         {MA_DR_SHAKER_FWD, G_EXTRA, "o.oxo.oxo.oxo.oxo.oxo.oxo.oxo.ox"},
                         {MA_DR_CONGA_MUTE, G_CONGA, "......o.......o.......o.......o."}}},
    [MA_ST_SON] = {6, {L_MARTILLO_0, L_MARTILLO_1, L_MARTILLO_2, {MA_DR_TUMBA, G_CONGA, "..............x...............x."},
                       {MA_DR_SHAKER_FWD, G_EXTRA, "x...x...x...x...x...x...x...x..."},
                       {MA_DR_SHAKER_BACK, G_EXTRA, "..o...o...o...o...o...o...o...o."}}},
    [MA_ST_CHACHA] = {7, {L_GUIRO_0, {MA_DR_GUIRO_SHORT, G_EXTRA, "............o...............o..."},
                          {MA_DR_BELL_MAMBO, G_TIMBAL, "X...o...X...o...X...o...X...o..."},
                          {MA_DR_BELL_MUTE, G_TIMBAL, "....x.......x.......x.......x..."},
                          {MA_DR_CONGA_MUTE, G_CONGA, "o...X...o...o...o...X...o...o..."},
                          {MA_DR_CONGA, G_CONGA, "............x.x.............x.x."},
                          {MA_DR_TIMBAL_LO, G_OTHER, "............................x.x."}}},
    [MA_ST_BOLERO] = {5, {{MA_DR_BONGO_HI, G_BONGO, "x.......o.o.o...x.......o.o.o..."},
                          {MA_DR_BONGO_LO, G_BONGO, "......o.......x.......o.......x."},
                          L_CBOLERO_0, L_CBOLERO_1,
                          {MA_DR_SHAKER_FWD, G_EXTRA, "o...o...o...o...o...o...o...o..."}}},
    /* the bossa: the cross-stick on its clave, the bass drum as the surdo (one and three, the pickup before), the
     * shaker's eighths leaning on the off-beats */
    [MA_ST_BOSSA] = {4, {{MA_DR_TIMBAL_RIM, G_TIMBAL, "X..x..x...x..x..X..x..x...x..x.."},
                         {MA_DR_KICK, G_KICK, "x.....x.x.....x.x.....x.x.....x."},
                         {MA_DR_SHAKER_FWD, G_EXTRA, "o.x.o.x.o.x.o.x.o.x.o.x.o.x.o.x."},
                         {MA_DR_TRIANGLE_MUTE, G_OTHER, "..o...o...o...o...o...o...o...o."}}},
    /* the tango's orquesta has no drums: its band is its rhythm. Manta gives it what its players do with their
     * instruments: the knock on the bandoneon's box (the woodblock) on the marcato's four, the strong ones accented,
     * the chicharra (the violin's scrape, the guiro) before the bar's last beat, the low tom as the double bass's
     * slap on the one */
    [MA_ST_TANGO] = {3, {{MA_DR_WOOD, G_OTHER, "X...x...x...x...X...x...x.x.x..."},
                         {MA_DR_GUIRO_SHORT, G_EXTRA, "..........x.............x...x..."},
                         {MA_DR_TOM_LO, G_KICK, "x...............x.........x....."}}},
    /* the guaracha: a conjunto's, the bongo's martillo, the maracas on the eighths (the claves on their own) */
    [MA_ST_GUARACHA] = {6, {L_MARTILLO_0, L_MARTILLO_1, L_MARTILLO_2,
                            {MA_DR_SHAKER_FWD, G_EXTRA, "x...x...x...x...x...x...x...x..."},
                            {MA_DR_SHAKER_BACK, G_EXTRA, "..o...o...o...o...o...o...o...o."},
                            {MA_DR_CONGA_MUTE, G_CONGA, "......o.......o.......o.......o."}}},
    /* the danzon: the pailas (the timbal's shell) on the cinquillo and its answer, the low paila's boom at the end of
     * the phrase, the guiro; no congas, no clave */
    [MA_ST_DANZON] = {4, {{MA_DR_PAILA, G_TIMBAL, "X.xx.xx.x...x.x.X.xx.xx.x...x.x."},
                          {MA_DR_TIMBAL_LO, G_TIMBAL, "..............................X."},
                          {MA_DR_GUIRO_LONG, G_EXTRA, "x...............x..............."},   /* (a colour: once a bar) */
                          {MA_DR_GUIRO_SHORT, G_EXTRA, "............o...............o..."}}},
};

/* the lanes for the PERC panel: CONGA BONGO TIMBAL EXTRA CLAVE and the rest */
/* what each PERC row played, a bit a sixteenth: this bar's so far, the bar before's after the step playing (the
 * screen: everything that sounded, the cards', the bells', the fills', not only the style's lanes) */
static int drum_row(int d)
{
    if (d <= MA_DR_TUMBA) return G_CONGA;
    if (d <= MA_DR_BONGO_MUTE || d == MA_DR_BELL_BONGO) return G_BONGO;
    if (d <= MA_DR_BELL_MUTE) return G_TIMBAL;
    if (d == MA_DR_CLAVE) return G_CLAVE;
    if ((d >= MA_DR_GUIRO_SHORT && d <= MA_DR_CABASA) || d == MA_DR_WASH || d == MA_DR_WASH_TAP) return G_EXTRA;
    return G_OTHER;
}
uint32_t ma_row_played(const ma_groove *g, int row)
{
    int now = g->playing && g->step >= 0 ? g->step : -1;
    uint32_t bits = 0;
    for (int d = 0; d < MA_DR_COUNT; ++d) {
        if (drum_row(d) != row)
            continue;
        uint32_t cur = g->dr_hits[d], last = g->dr_last[d];
        uint32_t mask = now >= 0 ? (2u << now) - 1u : 0u;
        bits |= (cur & mask) | (last & ~mask & 0xFFFFu);
    }
    return bits;
}

uint32_t ma_lane_bits(const ma_groove *g, int lane, int bar)
{
    const perc_set_t *ps = &PERC[clampi(g->s.snd[3], 0, MA_STYLES - 1)];
    uint32_t bits = 0;
    int cb = cbar(g, bar);
    if (lane == G_CLAVE) {
        for (int i = 0; i < 16; ++i)
            if (ma_clave_hit(g, bar, i))
                bits |= 1u << i;
        return bits;
    }
    for (int k = 0; k < ps->n; ++k) {
        const lane_t *l = &ps->l[k];
        if (l->group != lane && !(lane == G_OTHER && l->group == G_KICK))
            continue;
        for (int i = 0; i < 16; ++i) {
            char c = l->p[cb * 16 + i];
            if (c == 'x' || c == 'X' || c == 'o')
                bits |= 1u << i;
        }
    }
    return bits;
}

/* ---- the engine ------------------------------------------------------------------------------------------- */
void ma_init(ma_groove *g, float sample_rate)
{
    memset(g, 0, sizeof *g);
    g->sr = sample_rate;
    ma_defaults(&g->s);
    g->step = -1;
    g->next_style = g->next_song = -1;
    g->hit_bar = -1;
    g->desc_end = -1;
    g->solo_note = -1;
    for (int k = 0; k < 27; ++k)
        g->key_note[k] = -1;
    g->edit = -1;
    g->elen = 2;
    g->egrp = -1;
    for (int k = 0; k < 27; ++k)
        g->ekey_note[k] = -1;
    pats_clear(&pats_now);
    pats_clear(&pats_edit);
    for (int i = 0; i < MA_SETS; ++i)
        pats_clear(&pats_sets[i]);
    memset(cap_n, 0, sizeof cap_n);
    memset(cap_len, 0, sizeof cap_len);
    memset(cap_dr, 0, sizeof cap_dr);
    for (int p = 0; p < MA_PARTS; ++p)
        g->tone[p] = 127;
    g->pan_horns = 64;
    ma_rebuild(g);
}

void ma_set_sound(ma_groove *g, void (*fn)(void *ctx, const ma_event *e), void *ctx)
{
    g->sound = fn;
    g->sound_ctx = ctx;
}

static void emit(ma_groove *g, int on, int part, int note, int vel)
{
    if (!g->sound)
        return;
    ma_event e = {(uint8_t)on, (uint8_t)part, (uint8_t)clampi(note, 0, 127), (uint8_t)clampi(vel, 1, 127)};
    g->sound(g->sound_ctx, &e);
}

static void feed(ma_groove *g, int part, int note, int vel)
{
    uint8_t w = (uint8_t)((g->feed_w + 1u) % MA_FEED);
    if (w == g->feed_r)
        return;
    ma_seen *n = &g->feed[g->feed_w];
    n->part = (uint8_t)part;
    n->note = (uint8_t)note;
    n->vel = (uint8_t)vel;
    g->feed_w = w;
}

bool ma_feed_pop(ma_groove *g, ma_seen *n)
{
    if (g->feed_r == g->feed_w)
        return false;
    *n = g->feed[g->feed_r];
    g->feed_r = (uint8_t)((g->feed_r + 1u) % MA_FEED);
    return true;
}

static void msg(ma_groove *g, const char *m)
{
    cpy(g->msg, m, sizeof g->msg);
    g->msg_new = 1;
}

static void note_off_now(ma_groove *g, int i)
{
    emit(g, 0, g->held[i].part, g->held[i].note, 0);
    g->held[i] = g->held[--g->nheld];
}

/* how long each one-shot rings (s): then its voice is free again (tools/ma_perc.py's lengths) */
static const float RING[MA_DR_COUNT] = {0.40f, 0.45f, 0.22f, 0.60f, 0.28f, 0.32f, 0.16f, 0.45f, 0.50f, 0.30f, 0.12f,
                                        0.45f, 0.35f, 0.14f, 0.45f, 0.45f, 0.22f, 0.14f, 0.22f, 0.55f, 0.18f, 0.18f,
                                        0.15f, 0.80f, 0.18f, 0.55f, 0.65f, 0.45f, 0.30f, 0.10f};

/* a note now, its note-off after len frames; a note of the part still sounding at that pitch is cut first */
static void note(ma_groove *g, int part, int n, int vel, float len_frames)
{
    if (part == MA_PERC)
        len_frames = RING[clampi(n, 0, MA_DR_COUNT - 1)] * g->sr;
    for (int i = 0; i < g->nheld; ++i)
        if (g->held[i].part == part && g->held[i].note == n) {
            note_off_now(g, i);
            break;
        }
    if (g->nheld >= MA_HELD) {                     /* full: the oldest note goes */
        int o = 0;
        for (int i = 1; i < g->nheld; ++i)
            if ((int32_t)(g->held[i].off - g->held[o].off) < 0)
                o = i;
        note_off_now(g, o);
    }
    emit(g, 1, part, n, vel);
    feed(g, part, n, vel);
    if (g->step >= 0) {
        if (part == MA_PERC)                       /* (each drum's, the PERC page; and the part's, PLAY's meter) */
            g->dr_hits[clampi(n, 0, MA_DR_COUNT - 1)] |= (uint16_t)(1u << g->step);
        g->hits[part] |= (uint16_t)(1u << g->step);
    }
    if (part == MA_HORNS && g->soloing) {
        g->solo_on_clock = g->clock;
        g->solo_note = n;
    }
    g->held[g->nheld].note = (uint8_t)n;
    g->held[g->nheld].part = (uint8_t)part;
    g->held[g->nheld].off = g->clock + (uint32_t)(len_frames > 1.0f ? len_frames : 1.0f);
    g->nheld++;
}

void ma_all_off(ma_groove *g)
{
    while (g->nheld > 0)
        note_off_now(g, g->nheld - 1);
}

static void part_off(ma_groove *g, int part)
{
    for (int i = g->nheld - 1; i >= 0; --i)
        if (g->held[i].part == part)
            note_off_now(g, i);
}

/* the pending notes: a swung or late sixteenth's notes, played when the clock reaches them */
#define MA_PEND 48
static struct { uint32_t at; uint8_t part, note, vel, pad; float len; } pend[MA_PEND];
static int npend;
static float step_frames(const ma_groove *g);
static void schedule(ma_groove *g, uint32_t delay, int part, int n, int vel, float len)
{
    if (part == MA_PIANO || part == MA_KEYS)       /* the piano kept out of its thin top: down an octave above A5 */
        while (n > 81)
            n -= 12;
    if (g->step >= 0 && g->bar >= 0) {             /* what it played (the instrument pages, EDIT's start) */
        int pos = cbar(g, g->bar) * 16 + g->step;
        if (part == MA_PERC) {
            cap_dr[pos] |= 1u << clampi(n, 0, 31);
        } else if (part < MA_PERC) {
            for (int k = 0; k < MA_PAT_NOTES; ++k)
                if (cap_n[part][pos][k] == 0 || cap_n[part][pos][k] == n) {
                    cap_n[part][pos][k] = (uint8_t)clampi(n, 1, 127);
                    break;
                }
            float sf = step_frames(g);
            int ls = clampi((int)(len / sf + 0.5f), 1, 16);
            if (ls > cap_len[part][pos])
                cap_len[part][pos] = (uint8_t)ls;
        }
    }
    if (delay == 0 || npend >= MA_PEND) {
        note(g, part, n, vel, len);
        return;
    }
    pend[npend].at = g->clock + delay;
    pend[npend].part = (uint8_t)part;
    pend[npend].note = (uint8_t)n;
    pend[npend].vel = (uint8_t)vel;
    pend[npend].len = len;
    npend++;
}

static float step_frames(const ma_groove *g) { return g->sr * 60.0f / (float)clampi(g->s.bpm, 60, 220) / 4.0f; }

/* ---- the form ---------------------------------------------------------------------------------------------- */
int ma_sec_len(const ma_groove *g, int sec)
{
    switch (sec) {
    case MA_SEC_IN: return g->s.bpm >= 150 ? 8 : 4;   /* (a few bars: more of them when fast) */
    case MA_SEC_VRS: return g->s.verse >= 16 ? 16 : 8;
    case MA_SEC_CORTE: return 1;
    case MA_SEC_FRASE: return 4;
    case MA_SEC_MON: return g->s.mbars >= 16 ? 16 : 8;
    case MA_SEC_FIN: return 4;
    default: return 8;                             /* the tema, the piano, the mambo, the moña */
    }
}
static int sec_next(const ma_groove *g, int sec)
{
    if (sec == MA_SEC_FIN)                         /* round again from the tema, a new melody */
        return MA_SEC_TEMA;
    if (sec == MA_SEC_MON && !g->s.mambo)          /* no mambo: the montuno goes to the cierre */
        return MA_SEC_FIN;
    return sec + 1;
}
/* a fast style: written two bars to the clave (salsa, mambo, merengue, the guaracha), its pulse the eighth, so its
 * piano, guitar and solo keep to the eighths (a sixteenth there would be a thirty-second) */
static bool fast(const ma_groove *g)
{
    int st = g->s.style;
    return st == MA_ST_SALSA || st == MA_ST_MAMBO || st == MA_ST_MERENGUE || st == MA_ST_GUARACHA;
}
static int sec_bar(const ma_groove *g) { return g->s.arc ? g->sec_bar : (g->bar < 0 ? 0 : g->bar); }
static int sec_now(const ma_groove *g) { return g->s.arc ? g->sec : MA_SEC_MON; }
/* the horns' section plays its riff now (the mambo and the moña; ARC off: SEQ's) */
static bool riff_now(const ma_groove *g)
{
    int sc = sec_now(g);
    if (!g->s.arc)
        return g->mambo_live;
    if (sc == MA_SEC_MBO)
        return true;
    if (sc == MA_SEC_MONA)                         /* the moña: the riff, the solo over it every other two bars */
        return (g->sec_bar / 2) % 2 == 0;
    return false;
}

/* this round's VERSO: the piano's or the horns' (by the song and the round: sometimes one, sometimes the other) */
static bool vrs_piano_of(const ma_groove *g)
{
    uint32_t r = (uint32_t)g->s.song * 2654435761u + (uint32_t)g->round * 40503u + (uint32_t)g->s.style * 97u + 11u;
    return (xs(&r) >> 7) & 1u;
}

/* a new bar: what waited for the bar line comes in, the form moves on */
static void bar_line(ma_groove *g)
{
    for (int p = 0; p < MA_PARTS; ++p) {
        g->hits_last[p] = g->hits[p];
        g->hits[p] = 0;
    }
    g->arp_k = 0;                                  /* (the broken montuno's figure the same each bar: an ostinato) */
    for (int d = 0; d < MA_DR_COUNT; ++d) {
        g->dr_last[d] = g->dr_hits[d];
        g->dr_hits[d] = 0;
    }
    if (g->next_style >= 0) {
        int keep_song = g->next_song;
        ma_settings old = g->s;
        ma_style_apply(&g->s, g->next_style);
        for (int i = 0; i < 4; ++i)                /* MIX, PAN 3D, ARC and MENU stay the player's */
            g->s.lvl[i] = old.lvl[i];
        g->s.width = old.width;
        g->s.p3mode = old.p3mode;
        g->s.p3angle = old.p3angle;
        g->s.p3height = old.p3height;
        g->s.p3dist = old.p3dist;
        g->s.arc = old.arc;
        g->s.verse = old.verse;
        g->s.mbars = old.mbars;
        g->s.mambo = old.mambo;
        g->s.palette = old.palette;
        g->s.look = old.look;
        g->s.keysnap = old.keysnap;
        if (keep_song >= 0) {
            g->s.song = (uint16_t)keep_song;
            ma_song_sounds(&g->s);
        }
        g->next_style = -1;
        g->next_song = -1;
        g->sound_sig++;
        ma_all_off(g);
        ma_rebuild(g);
    } else if (g->next_song >= 0) {
        g->s.song = (uint16_t)g->next_song;
        uint32_t r = (uint32_t)g->s.song * 2246822519u + (uint32_t)g->s.style;
        g->s.chords = MA_STYLE[g->s.style].loops[rnd_n(&r, 4)];
        ma_song_sounds(&g->s);
        g->sound_sig++;
        g->next_song = -1;
        ma_rebuild(g);
    }
    if (g->hit_wait) {                             /* BAND HIT tapped: its cierre this bar */
        g->hit_wait = false;
        g->hit_bar = g->bar;
    }
    if (g->desc && !g->desc_held && g->desc_end >= 0 && g->bar >= g->desc_end) {
        g->desc = false;                           /* the descarga over: the band back on the bar line */
        g->desc_end = -1;
    }
    if (g->mambo_next) {                           /* SEQ: the mambo now */
        g->mambo_next = false;
        if (g->s.arc) {
            g->sec = MA_SEC_MBO;
            g->sec_bar = -1;                       /* (bar 0 below) */
            g->riff_var++;
            ma_rebuild(g);
        } else {
            g->mambo_live = !g->mambo_live;
        }
    }
    if (g->s.arc) {
        if (++g->sec_bar >= ma_sec_len(g, g->sec)) {
            int nx = sec_next(g, g->sec);
            if (g->sec == MA_SEC_FIN) {            /* round again: a new melody, a new riff */
                g->round++;
                g->phr_var++;
                g->riff_var++;
                ma_rebuild(g);
            }
            g->sec = nx;
            g->sec_bar = 0;
            g->vib = false;                        /* (the tema's held note sung out) */
        }
    }
    g->vrs_piano = vrs_piano_of(g);
}

/* is part p out (the form, the descarga, its level 0) */
static bool part_out(const ma_groove *g, int p)
{
    if (p == MA_KEYS && !g->guitar && !g->tres)   /* the upright beside the guitar or the tres only */
        return true;
    if (g->s.lvl[p == MA_KEYS ? MA_PIANO : p] == 0)
        return true;
    if (g->desc)                                   /* the descarga: the percussion alone */
        return p != MA_PERC;
    if (!g->s.arc)
        return false;
    int pp = g->guitar || g->tres ? MA_KEYS : MA_PIANO;   /* (the piano: the upright beside the guitar, or the PIANO part) */
    switch (g->sec) {
    case MA_SEC_IN:                                /* the groove: the piano, the guitar, the percussion, the bass a bar on */
        return p == MA_HORNS || (p == MA_BASS && g->sec_bar < 1);
    case MA_SEC_VRS:                               /* the piano's verso: the horns rest */
        return g->vrs_piano && p == MA_HORNS;
    case MA_SEC_CORTE:                             /* the soloist alone */
        return g->vrs_piano ? p != pp : p != MA_HORNS;
    case MA_SEC_PNO:                               /* the piano's solo: no horns, the guitar rests */
        return p == MA_HORNS || ((g->guitar || g->tres) && p == MA_PIANO);
    default: break;
    }
    return false;
}

/* the timbal roll: the fill over the last beat, rising (or the whole bar before the band comes back) */
static void timbal_roll(ma_groove *g, int i, uint32_t delay, bool whole)
{
    if (!whole && i < 12)
        return;
    int v = 60 + (i - (whole ? 0 : 12)) * (whole ? 4 : 14);
    int d = (i % 4 < 2) ? MA_DR_TIMBAL_HI : MA_DR_TIMBAL_LO;
    if (whole && i < 8 && (i % 2))
        return;                                    /* (eighths first, then sixteenths) */
    schedule(g, delay, MA_PERC, d, clampi(v, 40, 125), 0);
    if (i == 15)
        schedule(g, delay + (uint32_t)(step_frames(g) * 0.5f), MA_PERC, MA_DR_TIMBAL_RIM, 110, 0);
}

/* a lane's choice by its PERC card: -1 the style's own, else which generic lanes */
static bool lane_group_on(const ma_settings *s, int group)
{
    switch (group) {
    case G_CONGA: return s->conga != MA_CONGA_OFF;
    case G_BONGO: return s->bongo != MA_BONGO_OFF;
    case G_TIMBAL: return s->timbal != MA_TIMBAL_OFF;
    case G_EXTRA: return s->extra != MA_EXTRA_OFF;
    default: return true;
    }
}

static void play_lanes(ma_groove *g, const lane_t *l, int n, int i, uint32_t delay, float hum, float heat)
{
    int cb = cbar(g, g->bar);
    for (int k = 0; k < n; ++k) {
        char c = l[k].p[cb * 16 + i];
        if (c == 'h' && (heat < 0.7f || g->bar % 4 != 3))   /* 'h': hot, one bar in four */
            continue;
        if (c != 'x' && c != 'X' && c != 'o' && c != 'h')
            continue;
        if (c == 'o' && heat < 0.25f && (i % 2))   /* cool: the soft sixteenths rest */
            continue;
        int v = c == 'X' ? 118 : c == 'x' ? 100 : c == 'h' ? 84 : 72;
        /* the quiet styles' percussion brought forward, so it is heard under the band (their soft strokes most) */
        static const int8_t LIFT[MA_STYLES] = {[MA_ST_SON] = 18, [MA_ST_BOLERO] = 18, [MA_ST_DANZON] = 18,
                                               [MA_ST_TANGO] = 22, [MA_ST_CUMBIA] = 8};
        int lift = LIFT[clampi(g->s.snd[3], 0, MA_STYLES - 1)];
        v += c == 'o' ? lift + lift / 2 : lift;
        v = (int)((float)v * (0.82f + 0.18f * heat)) + (int)(hum * 10.0f);
        schedule(g, delay, MA_PERC, l[k].drum, clampi(v, 1, 127), 0);
    }
}

/* the percussion of step i: the style's lanes (or the PERC cards'), the clave, the bells in the montuno and the
 * mambo, the fills */
static void play_perc(ma_groove *g, int i, uint32_t delay, float hum)
{
    const ma_settings *s = &g->s;
    const perc_set_t *ps = &PERC[clampi(s->snd[3], 0, MA_STYLES - 1)];
    float heat = (float)s->heat / 100.0f;
    int sc = sec_now(g);
    bool bells = sc == MA_SEC_MON || sc == MA_SEC_MBO || sc == MA_SEC_MONA || sc == MA_SEC_PNO;
    bool salsa_like = s->snd[3] == MA_ST_SALSA || s->snd[3] == MA_ST_MAMBO || s->snd[3] == MA_ST_SON ||
                      s->snd[3] == MA_ST_GUARACHA;
    bool intro_soft = s->arc && g->sec == MA_SEC_IN && g->sec_bar == 0;
    /* the style's lanes, but for the groups a card has set to its own pattern (or OFF) */
    for (int k = 0; k < ps->n; ++k) {
        const lane_t *l = &ps->l[k];
        if (!lane_group_on(s, l->group))
            continue;
        if (l->group == G_CONGA && s->conga > MA_CONGA_AUTO)
            continue;
        if (l->group == G_BONGO && (s->bongo > MA_BONGO_AUTO || (salsa_like && bells && s->bongo == MA_BONGO_AUTO)))
            continue;                              /* (AUTO in the montuno: the bongo bell, below) */
        if (l->group == G_TIMBAL && (s->timbal > MA_TIMBAL_AUTO || (salsa_like && bells && s->timbal == MA_TIMBAL_AUTO)))
            continue;
        if (l->group == G_EXTRA && s->extra > MA_EXTRA_AUTO)
            continue;
        if (l->group == G_KICK && heat < 0.3f && s->snd[3] != MA_ST_SAMBA && s->snd[3] != MA_ST_BOSSA)
            continue;
        if (s->snd[3] == MA_ST_TANGO && heat < 0.3f)   /* (the tango cool: no knocks at all) */
            continue;
        if (intro_soft && l->group != G_CONGA && l->group != G_EXTRA)
            continue;                              /* (the first bar: the congas and the shakers alone) */
        play_lanes(g, l, 1, i, delay, hum, heat);
    }
    /* the groove's texture: the maracas and the woodblock in the intro, the verso and the piano's section */
    bool tx_sec = !s->arc || sc == MA_SEC_IN || sc == MA_SEC_VRS || sc == MA_SEC_PNO || sc == MA_SEC_TEMA;
    int st3 = s->snd[3];
    bool tx_style = st3 != MA_ST_TANGO && st3 != MA_ST_BOSSA && st3 != MA_ST_BOLERO && st3 != MA_ST_DANZON &&
                    st3 != MA_ST_SAMBA && st3 != MA_ST_SON;   /* (the son: its own maracas and claves, no more) */
    bool own_shaker = st3 == MA_ST_GUARACHA || st3 == MA_ST_SON || st3 == MA_ST_MERENGUE;
    if (tx_sec && tx_style && heat >= 0.35f && s->extra == MA_EXTRA_AUTO && !intro_soft)
        play_lanes(g, (own_shaker ? 1 : 0) + (fast(g) ? L_TX8 : L_TX), own_shaker ? 1 : 2, i, delay, hum,
                   heat * 0.85f);   /* (the son's maracas, the merengue's guira: the woodblock only) */
    /* the cards' own patterns, and the montuno's bells */
    if (s->conga == MA_CONGA_TUMBAO) play_lanes(g, L_TUMBAO, 3, i, delay, hum, heat);
    if (s->conga == MA_CONGA_MARCHA) play_lanes(g, L_MARCHA, 3, i, delay, hum, heat);
    if (s->conga == MA_CONGA_BOLERO) play_lanes(g, L_CBOLERO, 2, i, delay, hum, heat);
    if (s->bongo == MA_BONGO_MARTILLO) play_lanes(g, L_MARTILLO, 3, i, delay, hum, heat);
    if (s->bongo == MA_BONGO_BELL || (s->bongo == MA_BONGO_AUTO && salsa_like && bells))
        play_lanes(g, L_BBELL, 1, i, delay, hum, heat);
    if (s->timbal == MA_TIMBAL_CASCARA) play_lanes(g, L_CASCARA, 1, i, delay, hum, heat);
    if (s->timbal == MA_TIMBAL_BELL || (s->timbal == MA_TIMBAL_AUTO && salsa_like && bells && s->snd[3] != MA_ST_SON))
        play_lanes(g, L_TBELL, 2, i, delay, hum, heat);
    if (s->extra == MA_EXTRA_GUIRO) play_lanes(g, L_GUIRO, 2, i, delay, hum, heat);
    if (s->extra == MA_EXTRA_SHAKER) play_lanes(g, L_SHAKER, 2, i, delay, hum, heat);
    if (s->extra == MA_EXTRA_CABASA) play_lanes(g, L_CABASA, 1, i, delay, hum, heat);
    if (s->extra == MA_EXTRA_WASH) play_lanes(g, L_WASH, 2, i, delay, hum, heat);
    /* the clave: the sticks (the styles that have it on) */
    if (MA_STYLE[s->snd[3]].clave_on && ma_clave_hit(g, g->bar, i))
        schedule(g, delay, MA_PERC, MA_DR_CLAVE, clampi(96 + (int)(hum * 6.0f), 1, 127), 0);
    /* the fills: a timbal roll over the last beat every FILLS bars, and before each new section */
    static const uint8_t EVERY[MA_FILLS_COUNT] = {0, 8, 4, 2};
    int ev = EVERY[s->fills], b = sec_bar(g);
    bool sec_end = s->arc && g->sec_bar == ma_sec_len(g, g->sec) - 1 && g->sec != MA_SEC_FIN;
    if ((ev && (b % ev) == ev - 1) || sec_end)
        if (s->snd[3] != MA_ST_TANGO && s->snd[3] != MA_ST_BOSSA)
            timbal_roll(g, i, delay, false);
    /* heat high: the quinto speaks now and then over the last beat */
    if (heat > 0.7f && i >= 10 && (rnd_n(&g->rnd, 100) < (int)((heat - 0.7f) * 60.0f)))
        schedule(g, delay, MA_PERC, MA_DR_QUINTO, clampi(70 + rnd_n(&g->rnd, 40), 1, 127), 0);
}

/* the chord sounding at bar b, the next one */
static const ma_chord *chord_at(const ma_groove *g, int b)
{
    int bpc = ma_bars_per_chord(&g->s);
    return &g->loop[((b < 0 ? 0 : b) / bpc) % (g->nloop > 0 ? g->nloop : 1)];
}
static const ma_chord *chord_after(const ma_groove *g, int b)
{
    int bpc = ma_bars_per_chord(&g->s);
    return &g->loop[(((b < 0 ? 0 : b) / bpc) + 1) % (g->nloop > 0 ? g->nloop : 1)];
}
int ma_chord_now(const ma_groove *g)
{
    int bpc = ma_bars_per_chord(&g->s);
    return g->nloop > 0 ? ((g->bar < 0 ? 0 : g->bar) / bpc) % g->nloop : 0;
}
/* the chord a pattern's step plays: the next one where the bar is the chord's last and the step anticipates */
static const ma_chord *chord_for(const ma_groove *g, bool anticipate)
{
    int bpc = ma_bars_per_chord(&g->s);
    bool last_bar = ((g->bar < 0 ? 0 : g->bar) % bpc) == bpc - 1;
    return anticipate && last_bar ? chord_after(g, g->bar) : chord_at(g, g->bar);
}

/* the chord's triad near lo: three close notes, the lowest the first of them at or above lo */
static void triad_at(const ma_chord *c, int lo, int out[3])
{
    int best = 0;
    out[0] = 999;
    for (int k = 0; k < 3; ++k) {
        int n = lo;
        while ((n % 12) != c->tone[k])
            ++n;
        if (n < out[0]) {
            best = k;
            out[0] = n;
        }
    }
    for (int j = 1; j < 3; ++j) {
        int n = out[j - 1] + 1;
        while ((n % 12) != c->tone[(best + j) % 3])
            ++n;
        out[j] = n;
    }
}
static int tone_at(const ma_chord *c, int k, int lo)
{
    int n = lo;
    while ((n % 12) != c->tone[k % 4])
        ++n;
    return n;
}

/* the piano of step i (two bars of its family's pattern, the clave's) */
static bool solo_note(ma_groove *g, int b, int i, bool improv, int *out, int *len);
static void play_run(ma_groove *g, int part, int i, uint32_t delay, float sf);

/* the piano as the VERSO's soloist: the melody in octaves in the right hand, the left hand's root on the one and a
 * chord on the and of two (the anticipation of the montuno) */
static void piano_melody(ma_groove *g, int pp, int i, uint32_t delay, float sf, float hum)
{
    int n, len, vel = 98 + (i % 4 == 0 ? 8 : 0) + (int)(hum * 6.0f);
    if (solo_note(g, sec_bar(g), i, false, &n, &len)) {
        schedule(g, delay, pp, n, clampi(vel + 6, 1, 127), sf * (float)len * 0.9f);
        schedule(g, delay, pp, n - 12, clampi(vel - 6, 1, 127), sf * (float)len * 0.9f);
    }
    const ma_chord *c = chord_for(g, i >= 14);
    if (i == 0)
        schedule(g, delay, pp, tone_at(c, 0, 40), clampi(vel - 8, 1, 127), sf * 4.0f);
    if (i == 6 || i == 14) {
        int t[3];
        triad_at(c, 53, t);
        for (int k = 0; k < 3; ++k)
            schedule(g, delay, pp, t[k], clampi(vel - 16, 1, 127), sf * 1.5f);
    }
}

/* the PIANO section: the montuno solo, the right hand running the scale in broken thirds on syncopated sixteenths
 * (a cell a beat: which steps sound), turning at the ends of its range, its accents on the off-beats; the left hand
 * the root on the one and the next chord's root anticipated on the last sixteenth */
static void piano_solo(ma_groove *g, int pp, int i, uint32_t delay, float sf, float hum)
{
    static const char *const CELL[8] = {"x.xx", "xx.x", ".x.x", "x.x.", ".xxx", "x..x", "xx.x", ".xx."};
    const ma_settings *s = &g->s;
    uint32_t r = (uint32_t)(g->bar * 2654435761u) ^ (uint32_t)((i / 4) * 40503 + g->round * 977 + s->song * 131);
    const char *cell = CELL[rnd_n(&r, 8)];
    const ma_chord *c = chord_for(g, i >= 14);
    float heat = (float)s->heat / 100.0f;
    if (i == 0)
        schedule(g, delay, pp, tone_at(c, 0, 40), clampi(100 + (int)(hum * 6.0f), 1, 127), sf * 3.0f);
    if (i == 15) {
        schedule(g, delay, pp, tone_at(chord_for(g, true), 0, 40), clampi(96, 1, 127), sf * 2.0f);
        return;
    }
    if (fast(g) ? ((i & 1) || cell[(i / 2) % 4] != 'x') : cell[i % 4] != 'x')
        return;                                    /* (fast: the cells on the eighths) */
    int step = rnd_n(&r, 4) == 0 ? 1 : 2;          /* broken thirds, now and then a step */
    g->pno_deg = (int8_t)(g->pno_deg + g->pno_dir * step);
    if (g->pno_deg > 11) g->pno_dir = -1;
    if (g->pno_deg < 0) g->pno_dir = 1;
    g->pno_deg = (int8_t)clampi(g->pno_deg, -1, 12);
    int n = step_note(s->scale, 60 + s->key + (s->key < 5 ? 12 : 0), g->pno_deg);
    if (i % 4 == 0) {                              /* the beat onto the chord */
        int best = n, bd = 99;
        for (int t = 0; t < 3; ++t) {
            int cand = n - ((n % 12) - c->tone[t]);
            int d = cand > n ? cand - n : n - cand;
            if (d < bd) { bd = d; best = cand; }
        }
        n = best;
    }
    int vel = 88 + (i % 2 ? 14 : 0) + (int)(heat * 10.0f) + (int)(hum * 8.0f);   /* (the off-beats accented) */
    schedule(g, delay, pp, n, clampi(vel, 1, 127), sf * 1.3f);
    if (i % 4 == 1 || i % 4 == 3)                  /* the syncopated ones doubled an octave down */
        schedule(g, delay, pp, n - 12, clampi(vel - 18, 1, 127), sf * 1.3f);
}

/* the guitar's strings for a chord: the thumb's root low (E2 .. D#3) and a triad above it (three strings) */
static void guitar_strings(const ma_chord *c, int out[4])
{
    out[0] = 40 + ((c->root - 40) % 12 + 12) % 12;
    triad_at(c, out[0] + 7, out + 1);
}

static void play_guitar(ma_groove *g, int i, uint32_t delay, float sf, float hum)
{
    const char *pat = GUITAR_PAT[clampi(g->s.style, 0, MA_STYLES - 1)][g->s.montuno & 1];
    char ch = pat[cbar(g, g->bar) * 16 + i];
    if (ch == '.')
        return;
    bool acc = ch >= 'A' && ch <= 'Z';
    char c = acc ? (char)(ch - 'A' + 'a') : ch;
    float heat = (float)g->s.heat / 100.0f;
    int vel = (acc ? 108 : c == 'u' ? 78 : 92) + (i % 4 == 0 ? 4 : 0) + (int)(hum * 8.0f);
    vel = (int)((float)vel * (0.85f + 0.15f * heat));
    const ma_chord *ch2 = chord_for(g, i >= 14);
    int str[4];
    guitar_strings(ch2, str);
    uint32_t gap = (uint32_t)(g->sr * (c == 'u' ? 0.007f : 0.010f));   /* the strum's spread, string to string */
    float ring = g->s.style == MA_ST_BOLERO || g->s.style == MA_ST_BOSSA ? sf * 6.0f : sf * 3.0f;
    switch (c) {
    case 'd':
        for (int k = 1; k < 4; ++k)
            schedule(g, delay + gap * (uint32_t)(k - 1), MA_PIANO, str[k], clampi(vel - (k == 1 ? 4 : 0), 1, 127), ring);
        break;
    case 'u':
        for (int k = 0; k < 3; ++k)
            schedule(g, delay + gap * (uint32_t)k, MA_PIANO, str[3 - k], clampi(vel - 4 * k, 1, 127), sf * 2.0f);
        break;
    case 'm':                                      /* the chuck: the strings damped by the palm as they are struck */
        for (int k = 1; k < 4; ++k)
            schedule(g, delay + gap / 2u * (uint32_t)(k - 1), MA_PIANO, str[k], clampi(vel - 30, 1, 127), sf * 0.25f);
        break;
    case 'b':
    case 'f': {
        int n = str[0];
        if (c == 'f') {
            n = str[0] + 7;
            if (n > 52) n -= 12;
        }
        schedule(g, delay, MA_PIANO, n, clampi(vel, 1, 127), ring);
        break;
    }
    case 'c':
        for (int k = 2; k < 4; ++k)
            schedule(g, delay, MA_PIANO, str[k], clampi(vel - 10, 1, 127), sf * 2.5f);
        break;
    case 'a': {                                    /* up through the top strings, and back */
        static const uint8_t ARP[8] = {1, 2, 3, 2, 1, 2, 3, 2};
        schedule(g, delay, MA_PIANO, str[ARP[(i / 2) % 8]], clampi(vel - 8, 1, 127), ring);
        break;
    }
    default: break;
    }
}

/* the piano's patterns, on part pp: the PIANO part's own, or the upright beside the guitar (MA_KEYS) */
static void play_piano_on(ma_groove *g, int pp, int i, uint32_t delay, float sf, float hum)
{
    if (g->s.arc) {                                /* the piano's turns as the soloist */
        int sc = g->sec;
        if (sc == MA_SEC_VRS && g->vrs_piano) {
            piano_melody(g, pp, i, delay, sf, hum);
            return;
        }
        if (sc == MA_SEC_CORTE) {
            if (g->vrs_piano)
                play_run(g, pp, i, delay, sf);
            return;
        }
        if (sc == MA_SEC_PNO) {
            piano_solo(g, pp, i, delay, sf, hum);
            return;
        }
    }
    const ma_style *st = &MA_STYLE[g->s.style];
    int fam = st->piano;
    if (g->tres)                                   /* the tres its guajeo, the upright beside it its montuno */
        fam = pp == MA_KEYS ? PF_MONTUNO : PF_GUAJEO;
    const char *pat = PIANO_PAT[fam][g->s.montuno & 3];
    /* the groove's sections (the intro, the tema, the horns' verso, the frase): the montuno broken up, arpeggiated on
     * syncopated sixteenths (the bolero, the bossa and the tango keep their own) */
    int sc = sec_now(g);
    bool arp_sec = g->s.arc && (sc == MA_SEC_IN || sc == MA_SEC_TEMA || sc == MA_SEC_VRS || sc == MA_SEC_FRASE);
    if (arp_sec && fam != PF_BOLERO && fam != PF_BOSSA && fam != PF_TANGO && !(g->tres && pp == MA_PIANO))
        pat = fast(g) ? ARP_PAT8[g->s.montuno & 1] : ARP_PAT[(g->s.montuno + (fam == PF_GUAJEO)) & 3];
    char ch = pat[cbar(g, g->bar) * 16 + i];
    if (ch == '.' || ch == ' ')
        return;
    if (fast(g) && (i & 1))                       /* (fast: the eighths are the pulse)  */
        return;
    bool acc = ch >= 'A' && ch <= 'Z';
    char c = acc ? (char)(ch - 'A' + 'a') : ch;
    float heat = (float)g->s.heat / 100.0f;
    int vel = (acc ? 112 : 96) + (i % 4 == 0 ? 4 : 0) + (int)(hum * 8.0f);
    vel = (int)((float)vel * (0.85f + 0.15f * heat));
    float gate = fam == PF_BOLERO ? 1.6f : fam == PF_TANGO ? 0.45f : 0.7f;
    float len = sf * 2.0f * gate;
    switch (c) {
    case 'o': {                                    /* a chord tone in octaves: the montuno's melody, walking the chord */
        const ma_chord *ch2 = chord_for(g, i >= 14);
        static const uint8_t WALK[8] = {0, 1, 2, 1, 0, 2, 1, 0};
        int k = WALK[((g->bar & 1) * 8 + i / 2) % 8];
        int n = tone_at(ch2, k, 64);
        schedule(g, delay, pp, n, clampi(vel, 1, 127), len);
        if (fam != PF_GUAJEO || heat > 0.5f) /* (the tres's line single; hotter, doubled) */
            schedule(g, delay, pp, n + 12, clampi(vel - 8, 1, 127), len);
        {   /* now and then (four bars in a song's eight, by the bar) the montuno in double octaves, both hands */
            uint32_t r = (uint32_t)((g->bar < 0 ? 0 : g->bar) / 4) * 2654435761u + (uint32_t)g->s.song * 97u + 1u;
            if ((xs(&r) >> 9) & 1u)
                schedule(g, delay, pp, n - 12, clampi(vel - 12, 1, 127), len);
        }
        break;
    }
    case 't':
    case 'n': {
        const ma_chord *ch2 = chord_for(g, c == 'n');
        int t[3];
        triad_at(ch2, fam == PF_BOSSA ? 58 : 62, t);
        for (int k = 0; k < 3; ++k)
            schedule(g, delay, pp, t[k], clampi(vel - (k == 2 ? 0 : 6), 1, 127), c == 'n' ? sf * 3.0f : len);
        break;
    }
    case 'r': {                                    /* the broken montuno: the chord's tones up and back, a note a time */
        const ma_chord *ch2 = chord_for(g, i >= 14);
        static const uint8_t UPDN[8] = {0, 1, 2, 3, 2, 1, 2, 3};
        int k = UPDN[(g->arp_k++) & 7];
        int n = tone_at(ch2, k == 3 ? 0 : k, 62) + (k == 3 ? 12 : 0);
        schedule(g, delay, pp, n, clampi(vel - 4 + (i % 2 ? 6 : 0), 1, 127), sf * 1.6f);
        break;
    }
    case 'a': {                                    /* the arpeggio: up through the chord, the bolero's */
        const ma_chord *ch2 = chord_at(g, g->bar);
        int k = (i / 2) % 4;
        int n = tone_at(ch2, k == 3 ? 1 : k, 60) + (k == 3 ? 12 : 0);
        schedule(g, delay, pp, n, clampi(vel - 10, 1, 127), sf * 4.0f);
        break;
    }
    case 'b':                                      /* the left hand's root */
        schedule(g, delay, pp, tone_at(chord_at(g, g->bar), 0, 43), clampi(vel, 1, 127), fam == PF_TANGO ? sf : sf * 6.0f);
        break;
    case 'c': {                                    /* the left hand's chord, low (the tango's marcato) */
        int t[3];
        triad_at(chord_at(g, g->bar), 52, t);
        for (int k = 0; k < 3; ++k)
            schedule(g, delay, pp, t[k], clampi(vel - 6, 1, 127), sf * 0.9f);
        break;
    }
    default: break;
    }
}

/* the PIANO part: the guitar's strums when it is the GUITAR, the piano's patterns otherwise */
static void play_piano(ma_groove *g, int i, uint32_t delay, float sf, float hum)
{
    if (g->guitar)
        play_guitar(g, i, delay, sf, hum);
    else
        play_piano_on(g, MA_PIANO, i, delay, sf, hum);
}

static int bass_root(const ma_chord *c)
{
    int root = 36 + ((c->root - 36) % 12 + 12) % 12;   /* C2 .. B2, then G#1 .. G2: the bass sits low */
    return root > 43 ? root - 12 : root;
}

/* the bass never quite the same: each bar, by the song, the round and the bar, it plays its pattern as written
 * (half the time), or another of its family's rhythms, or hesitates (its first note left out), or, on a chord's last
 * bar, walks up the scale to the next root (its last two beats); and now and then a fifth becomes the octave or the
 * third. Always in the key, always going on */
enum { BV_ASIS, BV_OTHER, BV_HESITATE, BV_WALK };
static int bass_var(const ma_groove *g)
{
    uint32_t r = (uint32_t)(g->bar < 0 ? 0 : g->bar) * 2654435761u ^ ((uint32_t)g->s.song * 40503u + (uint32_t)g->round * 977u + 5u);
    int k = rnd_n(&r, 20);
    return k < 10 ? BV_ASIS : k < 15 ? BV_OTHER : k < 17 ? BV_HESITATE : BV_WALK;
}

static void play_bass(ma_groove *g, int i, uint32_t delay, float sf, float hum)
{
    const ma_style *st = &MA_STYLE[g->s.style];
    int var = bass_var(g);
    int bpc = ma_bars_per_chord(&g->s);
    bool last_bar = ((g->bar < 0 ? 0 : g->bar) % bpc) == bpc - 1;
    if (var == BV_WALK && !last_bar)
        var = BV_ASIS;
    int pv = g->s.tumbao & 3;
    if (var == BV_OTHER)
        pv = (pv + 1 + (g->bar & 1)) & 3;
    const char *pat = BASS_PAT[st->bass][pv];
    int pos = cbar(g, g->bar) * 16 + i;
    if (var == BV_WALK && i >= 8) {                /* the walk: two scale steps below the next root, then the root */
        if (i != 8 && i != 12 && i != 14)
            return;
        int target = bass_root(chord_after(g, g->bar));
        int n = i == 14 ? target : ma_snap(&g->s, target - (i == 8 ? 4 : 2));
        if (n < 30) n += 12;
        int vel = (i == 14 ? 110 : 98) + (int)(hum * 8.0f);
        schedule(g, delay, MA_BASS, n, clampi(vel, 1, 127), sf * (i == 14 ? 3.6f : 1.8f));
        return;
    }
    char c = pat[pos];
    if (c == '.' || c == '-')
        return;
    if (var == BV_HESITATE && c != 'n') {          /* the bar's first note held back */
        bool first = true;
        for (int k = cbar(g, g->bar) * 16; k < pos; ++k)
            if (pat[k] != '.' && pat[k] != '-')
                first = false;
        if (first)
            return;
    }
    if (c == 'f') {                                /* now and then the fifth's place another chord tone */
        uint32_t r = (uint32_t)(g->bar * 131 + i * 7 + g->s.song * 17 + 3);
        int k = rnd_n(&r, 8);
        if (k == 0) c = 'o';
        else if (k == 1) c = 't';
    }
    int len = 1;
    while (pos + len < 32 && pat[pos + len] == '-')
        ++len;
    if (pos + len == 32 && pat[0] == '-')          /* (a note held over into the next clave) */
        ++len;
    const ma_chord *cur = chord_at(g, g->bar);
    const ma_chord *nx = last_bar ? chord_after(g, g->bar) : cur;
    int root = bass_root(cur), n = root;
    switch (c) {
    case 'f': n = root + ((cur->tone[2] - cur->root + 12) % 12); if (n > 50) n -= 12; break;
    case 'o': n = root + 12; break;
    case 't': n = root + ((cur->tone[1] - cur->root + 12) % 12); break;
    case 'n': n = bass_root(nx); break;            /* the tumbao's anticipation: the next chord's root, early */
    case 'p': n = nx == cur ? root + 12 : bass_root(nx) - 1; break;
    default: break;
    }
    int vel = (i % 4 == 0 ? 108 : 100) + (c == 'n' ? 8 : 0) + (int)(hum * 8.0f);
    schedule(g, delay, MA_BASS, n, clampi(vel, 1, 127), sf * (float)len * 0.92f);
}

/* the solo's note at step i of bar b of the phrase (the 4-bar phrase over and over): false on a rest. Its long and
 * strong notes onto the chord; improvising, moved about now and then (the montuno's, the frase's) */
static bool solo_note(ma_groove *g, int b, int i, bool improv, int *out, int *len)
{
    const ma_settings *s = &g->s;
    int hp = (b % 4) * 16 + i;
    if (g->phr[hp] == -128)
        return false;
    *len = g->phr_len[hp];
    int n = step_note(s->scale, 60 + s->key + (s->key < 5 ? 12 : 0), g->phr[hp]);
    const ma_chord *c = chord_at(g, g->bar);
    if (*len >= 2 || i % 4 == 0) {
        int best = n, bd = 99;
        for (int t = 0; t < 3; ++t)
            for (int o = -1; o <= 1; ++o) {
                int cand = n - ((n % 12) - c->tone[t]) + o * 12;
                int d = cand - n < 0 ? n - cand : cand - n;
                if (d < bd) {
                    bd = d;
                    best = cand;
                }
            }
        n = best;
    }
    if (improv) {
        uint32_t r = (uint32_t)(g->bar * 131 + i * 7 + g->round * 977);
        if (rnd_n(&r, 3) == 0)
            n += rnd_n(&r, 2) ? 2 : -2;
        n = ma_snap(s, n);
    }
    *out = n;
    return true;
}

/* the phrase's last note in its fourth bar starts at step i */
static bool last_phrase_note(const ma_groove *g, int i)
{
    for (int k = 15; k >= 0; --k)
        if (g->phr[48 + k] != -128)
            return k == i;
    return false;
}

/* the key's tonic, high (the trumpet's ring: D5 .. C#6) */
static int high_tonic(const ma_settings *s)
{
    int n = 74;
    while ((n % 12) != s->key % 12)
        ++n;
    return n;
}

/* the CORTE: a bar of the soloist alone, a run up the scale into the band's return: three syncopated notes, then
 * sixteenths up to the tonic an octave higher (the piano in octaves) */
static void play_run(ma_groove *g, int part, int i, uint32_t delay, float sf)
{
    static const char RUN[17] = "x..x..x.xxxxxxxx";
    static const int8_t DEG[16] = {0, 0, 0, 2, 0, 0, 4, 0, 2, 3, 4, 5, 4, 5, 6, 7};
    if (RUN[i] != 'x')
        return;
    const ma_settings *s = &g->s;
    int base = 60 + s->key + (s->key < 5 ? 12 : 0);
    int n = step_note(s->scale, base, i == 15 ? 7 : DEG[i]);
    if (i == 15)
        n = base + 12;                             /* (it lands home, an octave up) */
    int vel = (i < 8 ? 108 : 92 + i) + (i % 4 == 0 ? 6 : 0);
    float len = i < 8 ? sf * 2.5f : sf * 0.9f;
    schedule(g, delay, part, n, clampi(vel, 1, 127), i == 15 ? sf * 3.0f : len);
    if (part != MA_HORNS)
        schedule(g, delay, part, n - 12, clampi(vel - 10, 1, 127), i == 15 ? sf * 3.0f : len);
}

/* the horns: the section's riff (two horns a chord tone apart), the solo's phrase, or nothing (the keys have it) */
/* the violins: no note cut short, each into the next (at least an eighth, longer than written), the accents eased;
 * the second desk's bow a little after the first's */
static void bow(ma_groove *g, float *len, int *vel, float sf)
{
    (void)g;
    *len = *len * 1.6f < sf * 2.2f ? sf * 2.2f : *len * 1.6f;
    *vel = 90 + (*vel - 90) / 2;
}
static uint32_t bow_late(ma_groove *g)
{
    return g->bowed ? (uint32_t)((float)(4 + rnd_n(&g->rnd, 14)) * g->sr * 0.001f) : 0u;
}

static void play_horns(ma_groove *g, int i, uint32_t delay, float sf, float hum)
{
    const ma_settings *s = &g->s;
    int sc = sec_now(g);
    if (riff_now(g)) {
        g->soloing = false;
        int pos = cbar(g, g->bar) * 16 + i;
        if (g->riff[pos] == -128)
            return;
        const ma_chord *c = chord_for(g, i >= 14);
        int k = g->riff[pos];
        int top = tone_at(c, k % 3, 70) + (k >= 3 ? 0 : 0);
        while (top > 82) top -= 12;
        while (top < 70) top += 12;
        int low = top - 1;                         /* the second horn: the chord tone below the first */
        while (low % 12 != c->tone[0] && low % 12 != c->tone[1] && low % 12 != c->tone[2])
            --low;
        if (g->trombone)                           /* the sampled brass: the trombone, an octave under the trumpet's */
            low -= 12;
        float heat = (float)s->heat / 100.0f;
        int vel = 104 + (g->riff_len[pos] == 1 ? 10 : 0) + (int)(hum * 6.0f) + (int)(heat * 10.0f);
        float len = sf * (float)g->riff_len[pos] * (g->riff_len[pos] == 1 ? 0.6f : 0.9f);
        if (g->bowed)
            bow(g, &len, &vel, sf);
        schedule(g, delay, MA_HORNS, top, clampi(vel, 1, 127), len);
        schedule(g, delay + bow_late(g), MA_HORNS, low, clampi(vel - 10, 1, 127), len);
        return;
    }
    /* the coro, played: where a son's singers would answer the soloist (the montuno's other four bars) the section
     * plays the coro's refrain instead, the phrase's first two bars over and over, the same each time, in two voices
     * (Manta leaves the singing to whoever sings along, and fills the room with the horns) */
    bool mon = s->arc ? sc == MA_SEC_MON : true;
    if (mon && (sec_bar(g) / 4) % 2 == 0) {
        g->soloing = false;
        if (g->keys)                               /* (a key held: the player's, not the coro's) */
            return;
        int cp = (sec_bar(g) % 2) * 16 + i;
        if (g->phr[cp] == -128)
            return;
        const ma_chord *c = chord_at(g, g->bar);
        int n = step_note(s->scale, 60 + s->key + (s->key < 5 ? 12 : 0), g->phr[cp]);
        int top = n, bd = 99;                      /* every note of the refrain onto the chord: sung, it is a chord */
        for (int t = 0; t < 3; ++t)
            for (int o = -1; o <= 1; ++o) {
                int cand = n - ((n % 12) - c->tone[t]) + o * 12;
                int d = cand > n ? cand - n : n - cand;
                if (d < bd) {
                    bd = d;
                    top = cand;
                }
            }
        while (top > 82) top -= 12;
        while (top < 67) top += 12;
        int low = top - 1;                         /* the second voice: the chord tone below */
        while (low % 12 != c->tone[0] && low % 12 != c->tone[1] && low % 12 != c->tone[2])
            --low;
        if (g->trombone)
            low -= 12;
        float len = sf * (float)g->phr_len[cp] * 0.85f;
        int vel = 94 + (i % 4 == 0 ? 6 : 0) + (int)(hum * 6.0f) + (int)((float)s->heat / 100.0f * 8.0f);
        if (g->bowed)
            bow(g, &len, &vel, sf);
        schedule(g, delay, MA_HORNS, top, clampi(vel, 1, 127), len);
        schedule(g, delay + bow_late(g), MA_HORNS, low, clampi(vel - 10, 1, 127), len);
        return;
    }
    /* the solo: the TEMA (the theme, twice, its last note the tonic high, held, with vibrato), the VERSO's melody when
     * the horns have it, the CORTE's bar alone, the FRASE (an improvised phrase), every other four bars of the
     * montuno an improvised answer, in the moña over the riff's rests; the keys take it over */
    g->soloing = true;
    if (g->keys || s->solo != MA_SOLO_PHRASE)
        return;
    bool plays = false, improv = false;
    if (!s->arc) {
        plays = (sec_bar(g) / 4) % 2 == 1;
        improv = true;
    } else {
        switch (sc) {
        case MA_SEC_TEMA: plays = true; break;
        case MA_SEC_VRS: plays = !g->vrs_piano; break;
        case MA_SEC_CORTE:
            if (!g->vrs_piano)
                play_run(g, MA_HORNS, i, delay, sf);
            return;
        case MA_SEC_FRASE:
        case MA_SEC_MONA: plays = improv = true; break;
        case MA_SEC_MON: plays = improv = (sec_bar(g) / 4) % 2 == 1; break;
        default: break;
        }
    }
    if (!plays)
        return;
    int n, len;
    if (!solo_note(g, sec_bar(g), i, improv, &n, &len))
        return;
    /* only a really long note sings (0.7 s and more, whatever the tempo), its vibrato eased in late and light */
    g->vib = sf * (float)len >= 0.7f * g->sr;
    if (s->arc && sc == MA_SEC_TEMA && g->sec_bar == ma_sec_len(g, sc) - 1 && last_phrase_note(g, i)) {
        n = high_tonic(s);                         /* the theme's end: the tonic, high, held, singing */
        len = 16 - i;
        g->vib = true;
    }
    /* a player, not a sequencer: each note a little late or not (up to 20 ms), louder or softer, held its own length */
    int vel = 100 + (i % 4 == 0 ? 8 : 0) + (i % 2 ? 4 : 0) + (int)(hum * 6.0f);   /* (the off-beats leaned on) */
    vel += rnd_n(&g->rnd, 21) - 12;
    uint32_t late = (uint32_t)((float)rnd_n(&g->rnd, 21) * g->sr * 0.001f);
    float hold = len >= 4 ? 0.9f + 0.02f * (float)rnd_n(&g->rnd, 6) : 0.8f + 0.03f * (float)rnd_n(&g->rnd, 6);   /* (legato) */
    if (g->bowed) {                                /* the bow: one note into the next, the dynamics gentle */
        hold = 1.15f;
        vel = 92 + (vel - 92) / 2;
    }
    schedule(g, delay + late, MA_HORNS, n, clampi(vel, 1, 127), sf * (float)len * hold);
}

/* the cierre: the whole band hits the clave's figure of the bar together, nothing between (a band hit, the FIN) */
static void play_cierre(ma_groove *g, int i, uint32_t delay, float sf)
{
    if (!ma_clave_hit(g, g->bar, i))
        return;
    bool last = cbar(g, g->bar) == 0 ? i == 12 : i == 8;
    const ma_chord *c = chord_at(g, g->bar);
    int t[3];
    triad_at(c, 62, t);
    float len = last ? sf * 4.0f : sf * 1.2f;
    for (int k = 0; k < 3; ++k)
        schedule(g, delay, MA_PIANO, t[k], 118, len);
    schedule(g, delay, MA_BASS, bass_root(c), 118, len);
    int top = tone_at(c, 2, 72);
    schedule(g, delay, MA_HORNS, top, 120, len);
    schedule(g, delay, MA_HORNS, tone_at(c, 0, top - 9), 112, len);
    schedule(g, delay, MA_PERC, MA_DR_TIMBAL_HI, 120, 0);
    schedule(g, delay, MA_PERC, MA_DR_BELL_MAMBO, 112, 0);
    schedule(g, delay, MA_PERC, last ? MA_DR_TUMBA : MA_DR_QUINTO, 115, 0);
}

/* this bar a cierre: BAND HIT, HITS's every 4, 8 or 16 bars (the last bar of each), the FIN's last bar */
static bool cierre_bar(const ma_groove *g)
{
    if (g->bar == g->hit_bar)
        return true;
    if (g->s.arc && g->sec == MA_SEC_FIN && g->sec_bar == ma_sec_len(g, MA_SEC_FIN) - 1)
        return true;
    static const uint8_t EVERY[MA_HITS_COUNT] = {0, 4, 8, 16};
    int ev = EVERY[g->s.hits], sc = sec_now(g);
    if (ev && sc == MA_SEC_MON && (sec_bar(g) % ev) == ev - 1)
        return true;
    return false;
}

/* one sixteenth: the parts, swung and humanized */
/* ---- EDIT: the parts' own patterns ---------------------------------------------------------------------------- */
/* MINE in the form: the bass's and the percussion's always; the piano's but for its solos (the verso's melody, the
 * corte's run, its PIANO section); the horns' for the section (the riff, the coro), the solo its own */
static bool mine_plays(const ma_groove *g, int p)
{
    int sc = g->s.arc ? g->sec : MA_SEC_MON;
    if (p == MA_PIANO && g->s.arc)
        return !((sc == MA_SEC_VRS && g->vrs_piano) || (sc == MA_SEC_CORTE && g->vrs_piano) || sc == MA_SEC_PNO);
    if (p == MA_HORNS) {
        if (riff_now(g))
            return true;
        bool mon = g->s.arc ? sc == MA_SEC_MON : true;
        int b = g->s.arc ? g->sec_bar : (g->bar < 0 ? 0 : g->bar);
        return mon && (b / 4) % 2 == 0 && !g->keys;
    }
    return true;
}
/* a pattern's note over chord c: kept as semitones over the root's C3; its third, fifth and seventh the chord's own */
static int pat_note(const ma_chord *c, int off)
{
    int root = c->root % 12, iv = ((off % 12) + 12) % 12, oct = off - iv;
    int third = (c->tone[1] - c->root + 24) % 12, fifth = (c->tone[2] - c->root + 24) % 12,
        sev = (c->tone[3] - c->root + 24) % 12;
    if (iv == 3 || iv == 4) iv = third;
    else if (iv == 6 || iv == 7 || iv == 8) iv = fifth;
    else if ((iv == 10 || iv == 11) && (sev == 10 || sev == 11)) iv = sev;
    return clampi(48 + root + oct + iv, 0, 127);
}
static int pat_off(const ma_chord *c, int note) { return clampi(note - 48 - c->root % 12, -127, 127); }

/* the step the keys write now: playing, the step playing (or the next, past its middle); stopped, the cursor */
int ma_pos(const ma_groove *g)
{
    if (!g->playing || g->step < 0)
        return -1;
    return cbar(g, g->bar) * 16 + g->step;
}
int ma_edit_pos(const ma_groove *g)
{
    int p = ma_pos(g);
    if (p < 0)
        return g->ecur % MA_PAT_STEPS;
    if (g->wait < step_frames(g) * 0.5f)
        p = (p + 1) % MA_PAT_STEPS;
    return p;
}
/* the chord over step pos of the two bars: playing, this bar's or the next's; stopped, the loop's first two */
static const ma_chord *pos_chord(const ma_groove *g, int pos)
{
    if (!g->playing || g->bar < 0)
        return chord_at(g, pos / 16);
    int now = cbar(g, g->bar), b = g->bar + ((pos / 16) != now ? 1 : 0);
    return chord_at(g, b);
}

int ma_pat_notes(const ma_groove *g, const ma_pats *p, int part, int k, int out[MA_PAT_NOTES], int *len)
{
    int n = 0;
    if (part < 0 || part > 2)
        return 0;
    const ma_chord *c = pos_chord(g, k);
    for (int j = 0; j < MA_PAT_NOTES; ++j)
        if (p->n[part][k][j] != MA_PAT_REST)
            out[n++] = pat_note(c, p->n[part][k][j]);
    *len = p->len[part][k] ? p->len[part][k] : 1;
    return n;
}
int ma_played(const ma_groove *g, int part, int k, int out[MA_PAT_NOTES], int *len)
{
    (void)g;
    int n = 0;
    if (part < 0 || part > 2 || k < 0 || k >= MA_PAT_STEPS)
        return 0;
    for (int j = 0; j < MA_PAT_NOTES; ++j)
        if (cap_n[part][k][j])
            out[n++] = cap_n[part][k][j];
    *len = cap_len[part][k] ? cap_len[part][k] : 1;
    return n;
}
uint32_t ma_played_dr(int k) { return cap_dr[clampi(k, 0, MA_PAT_STEPS - 1)]; }

/* the pattern the part plays now, if its own: the edit's copy while it is edited, MINE's when saved so */
static const ma_pats *mine_for(const ma_groove *g, int part)
{
    if (g->edit == part)
        return &pats_edit;
    if (part <= MA_PERC && ((g->s.mine >> part) & 1u) && ((pats_now.has >> part) & 1u))
        return &pats_now;
    return 0;
}
static void play_mine(ma_groove *g, const ma_pats *p, int part, int i, uint32_t delay, float sf, float hum)
{
    int pos = cbar(g, g->bar) * 16 + i;
    float heat = (float)g->s.heat / 100.0f;
    int vel = (part == MA_PERC ? 100 : part == MA_BASS ? 104 : 100) + (i % 4 == 0 ? 6 : 0) + (int)(hum * 8.0f);
    vel = (int)((float)vel * (0.85f + 0.15f * heat));
    if (part == MA_PERC) {
        for (int d = 0; d < MA_DR_COUNT; ++d)
            if ((p->dr[pos] >> d) & 1u)
                schedule(g, delay, MA_PERC, d, clampi(vel, 1, 127), 0);
        return;
    }
    const ma_chord *c = chord_at(g, g->bar);
    int len = p->len[part][pos] ? p->len[part][pos] : 1;
    for (int j = 0; j < MA_PAT_NOTES; ++j)
        if (p->n[part][pos][j] != MA_PAT_REST)
            schedule(g, delay, part, pat_note(c, p->n[part][pos][j]), clampi(vel - j * 3, 1, 127), sf * (float)len * 0.92f);
}

void ma_edit_begin(ma_groove *g, int part)
{
    if (part < 0 || part > MA_PERC)
        return;
    pats_edit = pats_now;                          /* (the other parts as they are: only this one is written) */
    if (!((pats_now.has >> part) & 1u)) {          /* none of its own yet: what it played, to write over */
        if (part == MA_PERC) {
            memcpy(pats_edit.dr, cap_dr, sizeof cap_dr);
        } else {
            for (int k = 0; k < MA_PAT_STEPS; ++k) {
                const ma_chord *c = pos_chord(g, k);
                for (int j = 0; j < MA_PAT_NOTES; ++j)
                    pats_edit.n[part][k][j] = cap_n[part][k][j] ? (int8_t)pat_off(c, cap_n[part][k][j]) : MA_PAT_REST;
                pats_edit.len[part][k] = cap_len[part][k];
            }
        }
    }
    part_off(g, part);
    g->edit = (int8_t)part;
    g->euse = true;
    g->egrp = -1;
    g->ekeys = 0;
    g->ecur = 0;
}

void ma_edit_end(ma_groove *g, bool save)
{
    int part = g->edit;
    if (part < 0)
        return;
    for (int k = 0; k < 27; ++k)
        if (g->ekey_note[k] >= 0) {
            emit(g, 0, part, g->ekey_note[k], 0);
            g->ekey_note[k] = -1;
        }
    if (save) {
        if (part == MA_PERC) {
            memcpy(pats_now.dr, pats_edit.dr, sizeof pats_now.dr);
        } else {
            memcpy(pats_now.n[part], pats_edit.n[part], sizeof pats_now.n[part]);
            memcpy(pats_now.len[part], pats_edit.len[part], sizeof pats_now.len[part]);
        }
        pats_now.has |= (uint8_t)(1u << part);
        if (g->euse)
            g->s.mine |= (uint8_t)(1u << part);
        else
            g->s.mine &= (uint8_t)~(1u << part);
    }
    part_off(g, part);
    g->edit = -1;
    g->ekeys = 0;
    g->egrp = -1;
}

void ma_edit_key(ma_groove *g, int k, int note, bool down)
{
    int part = g->edit;
    if (part < 0 || k < 0 || k >= 27)
        return;
    int pos = ma_edit_pos(g);
    if (part == MA_PERC) {                         /* a drum: heard; written at its own step */
        int d = ma_key_drum(k);
        if (!down)
            return;
        emit(g, 1, MA_PERC, d, 104);
        feed(g, MA_PERC, d, 104);
        if (g->playing) {
            pats_edit.dr[pos] |= 1u << d;
        } else {
            pats_edit.dr[pos] ^= 1u << d;          /* (stopped: the cursor's step, on or off) */
            if (g->ekeys == 0)
                g->ecur = (uint8_t)((pos + g->elen) % MA_PAT_STEPS);
        }
        return;
    }
    if (down) {
        if (g->ekey_note[k] >= 0)
            emit(g, 0, part, g->ekey_note[k], 0);
        g->ekey_note[k] = (int8_t)clampi(note, 0, 127);
        emit(g, 1, part, note, 104);
        feed(g, part, note, 104);
        if (g->ekeys == 0 || g->egrp < 0) {        /* the first key: its step written over */
            g->egrp = (int8_t)pos;
            g->egrp_clock = g->clock;
            for (int j = 0; j < MA_PAT_NOTES; ++j)
                pats_edit.n[part][pos][j] = MA_PAT_REST;
            pats_edit.len[part][pos] = (uint8_t)(g->playing ? 1 : g->elen);
        }
        int8_t *n = pats_edit.n[part][g->egrp];
        const ma_chord *c = pos_chord(g, g->egrp);
        for (int j = 0; j < MA_PAT_NOTES; ++j)     /* the others held with it: one chord */
            if (n[j] == MA_PAT_REST) {
                n[j] = (int8_t)pat_off(c, note);
                break;
            }
        g->ekeys++;
        return;
    }
    if (g->ekey_note[k] >= 0)
        emit(g, 0, part, g->ekey_note[k], 0);
    g->ekey_note[k] = -1;
    if (g->ekeys > 0)
        g->ekeys--;
    if (g->ekeys == 0 && g->egrp >= 0) {           /* all let go: the length held (playing), on to the next (stopped) */
        if (g->playing) {
            int ls = (int)((float)(g->clock - g->egrp_clock) / step_frames(g) + 0.5f);
            pats_edit.len[part][g->egrp] = (uint8_t)clampi(ls, 1, 16);
        } else {
            g->ecur = (uint8_t)((g->egrp + g->elen) % MA_PAT_STEPS);
        }
        g->egrp = -1;
    }
}

void ma_edit_erase(ma_groove *g, int dir)
{
    int part = g->edit;
    if (part < 0)
        return;
    if (!g->playing && dir < 0)                    /* back a step, then cleared (a backspace) */
        g->ecur = (uint8_t)((g->ecur + MA_PAT_STEPS - 1) % MA_PAT_STEPS);
    int pos = g->playing ? (ma_pos(g) < 0 ? 0 : ma_pos(g)) : g->ecur;
    if (part == MA_PERC) {
        pats_edit.dr[pos] = 0;
    } else {
        for (int j = 0; j < MA_PAT_NOTES; ++j)
            pats_edit.n[part][pos][j] = MA_PAT_REST;
        pats_edit.len[part][pos] = 0;
    }
    if (!g->playing && dir > 0)
        g->ecur = (uint8_t)((g->ecur + 1) % MA_PAT_STEPS);
}

static void do_step(ma_groove *g)
{
    if (++g->step >= 16) {
        g->step = 0;
        g->bar++;
        bar_line(g);
    }
    int i = g->step;
    if (i == 0)
        g->rnd = (uint32_t)g->bar * 2654435761u + (uint32_t)g->s.song * 97u + 1u;
    {   /* this step's place in what was played: written afresh */
        int pos = cbar(g, g->bar) * 16 + i;
        for (int p = 0; p < 3; ++p) {
            for (int j = 0; j < MA_PAT_NOTES; ++j)
                cap_n[p][pos][j] = 0;
            cap_len[p][pos] = 0;
        }
        cap_dr[pos] = 0;
    }
    float sf = step_frames(g);
    uint32_t delay = 0;
    if (i % 2 && g->s.swing)                        /* swing: the off sixteenths late */
        delay = (uint32_t)(sf * (float)g->s.swing / 100.0f * 0.66f);
    for (int p = 0; p < MA_PARTS; ++p) {
        bool out = part_out(g, p);
        if (out != g->muted[p] && out)
            part_off(g, p);
        g->muted[p] = out;
    }
    if (cierre_bar(g) && !g->desc) {               /* the cierre: the band on the clave, together */
        if (i == 0)
            for (int p = 0; p < MA_PARTS; ++p)
                part_off(g, p);
        g->soloing = false;
        play_cierre(g, i, 0, sf);
        if (MA_STYLE[g->s.snd[3]].clave_on && ma_clave_hit(g, g->bar, i))
            schedule(g, 0, MA_PERC, MA_DR_CLAVE, 100, 0);
        return;
    }
    if (g->desc && g->desc_end >= 0 && g->bar == g->desc_end - 1) {
        timbal_roll(g, i, 0, true);                /* the descarga's last bar: the roll, the band in on the bar line */
        return;
    }
    for (int p = 0; p < MA_PARTS; ++p) {
        if (g->muted[p])
            continue;
        float hum = (rnd_f(&g->rnd) - 0.5f) * 2.0f * (float)g->s.human / 100.0f;
        uint32_t d = delay + (uint32_t)((hum > 0 ? hum : 0.0f) * g->sr * 0.008f);   /* late only, up to 8 ms */
        /* the old Havana's ease: the piano, the strings and the trumpet sit behind the beat, the bass a little (ms) */
        static const uint8_t LAY[MA_STYLES] = {[MA_ST_SON] = 22, [MA_ST_GUARACHA] = 10, [MA_ST_BOLERO] = 28,
                                               [MA_ST_DANZON] = 14, [MA_ST_CHACHA] = 10};
        int lay = LAY[clampi(g->s.style, 0, MA_STYLES - 1)];
        if (p == MA_PIANO || p == MA_KEYS || p == MA_HORNS)
            d += (uint32_t)((float)lay * g->sr * 0.001f);
        else if (p == MA_BASS)
            d += (uint32_t)((float)lay * g->sr * 0.0003f);
        {   /* a part's own pattern (EDIT): the piano's and the horns' solos still theirs, but while edited */
            const ma_pats *mp = mine_for(g, p == MA_KEYS ? MA_PIANO : p);
            if (mp && p == MA_KEYS && (g->edit == MA_PIANO || mine_plays(g, MA_PIANO)))
                continue;                          /* (the upright beside the guitar makes way for the piano's own) */
            if (p == MA_KEYS)
                mp = 0;
            if (mp && (g->edit == p || mine_plays(g, p))) {
                if (p == MA_HORNS)
                    g->soloing = false;
                play_mine(g, mp, p, i, d, sf, hum);
                continue;
            }
        }
        switch (p) {
        case MA_PIANO: play_piano(g, i, d, sf, hum); break;
        case MA_KEYS: play_piano_on(g, MA_KEYS, i, d, sf, hum); break;
        case MA_BASS: play_bass(g, i, d, sf, hum); break;
        case MA_HORNS: play_horns(g, i, d, sf, hum); break;
        case MA_PERC: play_perc(g, i, d, hum); break;
        default: break;
        }
    }
}

void ma_play(ma_groove *g, bool on)
{
    if (on == g->playing)
        return;
    ma_all_off(g);
    npend = 0;
    g->playing = on;
    g->clock = 0;
    g->wait = 0;
    g->step = -1;
    g->bar = 0;
    g->hit_bar = -1;
    g->hit_wait = false;
    g->desc = g->desc_held = false;
    g->desc_end = -1;
    g->mambo_next = false;
    g->mambo_live = false;
    g->sec = g->s.arc ? MA_SEC_IN : MA_SEC_MON;
    g->sec_bar = 0;
    g->round = 0;
    g->vrs_piano = vrs_piano_of(g);
    g->vib = false;
    g->pno_deg = 4;
    g->pno_dir = 1;
    g->phr_var = 0;
    g->riff_var = 0;
    g->soloing = false;
    g->solo_note = -1;
    if (on) {
        if (g->next_style >= 0 || g->next_song >= 0) {   /* (queued while stopped: at once) */
            g->bar = -1;
            g->step = 15;
            bar_line(g);
            g->bar = 0;
            g->step = -1;
            g->sec = g->s.arc ? MA_SEC_IN : MA_SEC_MON;
            g->sec_bar = 0;
        }
    }
    ma_rebuild(g);
}

static void now_bar_line(ma_groove *g)
{
    int b = g->bar, sb = g->sec_bar, sc = g->sec;
    g->bar = -1;
    bar_line(g);
    g->bar = b;
    g->sec = sc;
    g->sec_bar = sb;
}

void ma_queue_style(ma_groove *g, int style)
{
    style = ((style % MA_STYLES) + MA_STYLES) % MA_STYLES;
    g->next_style = style;
    if (!g->playing) {
        now_bar_line(g);
    } else {
        char m[24] = "NEXT ";
        cpy(m + 5, MA_STYLE[style].name, (int)sizeof m - 5);
        msg(g, m);
    }
}

void ma_queue_song(ma_groove *g, int song)
{
    g->next_song = ((song % 1000) + 1000) % 1000;
    if (!g->playing)
        now_bar_line(g);
}

void ma_band_hit(ma_groove *g, bool down)
{
    if (!g->playing)
        return;
    if (down) {
        g->hit_wait = true;                        /* a tap: the cierre on the next bar line */
        g->desc_held = true;
        msg(g, "BAND HIT");
    } else {
        g->desc_held = false;
        if (g->desc) {                             /* held into a descarga: the roll next bar, the band after */
            g->desc_end = g->bar + 2;
            msg(g, "BAND IN");
        }
    }
}

void ma_mambo(ma_groove *g)
{
    if (!g->playing)
        return;
    g->mambo_next = true;
    msg(g, g->s.arc ? "MAMBO NEXT" : g->mambo_live ? "MAMBO OUT" : "MAMBO NEXT");
}

void ma_key(ma_groove *g, int k, int note, bool down)
{
    if (k < 0 || k >= 27)
        return;
    if (down) {
        int n = g->s.keysnap ? ma_snap(&g->s, note) : note;   /* (SNAP on: in the key; off: as pressed) */
        if (g->key_note[k] >= 0)
            emit(g, 0, MA_HORNS, g->key_note[k], 0);
        if (!g->keys)                              /* the solo makes way */
            part_off(g, MA_HORNS);
        g->keys |= 1u << k;
        g->key_note[k] = n;
        g->soloing = true;
        emit(g, 1, MA_HORNS, n, 106);
        feed(g, MA_HORNS, n, 106);
        g->solo_on_clock = g->clock;
        g->solo_note = n;
    } else {
        g->keys &= ~(1u << k);
        if (g->key_note[k] >= 0)
            emit(g, 0, MA_HORNS, g->key_note[k], 0);
        g->key_note[k] = -1;
    }
}

/* the solo's angle now (degrees): FIXED the card's, ORBIT once round every eight bars, STEP a new place each phrase */
static float p3_angle_now(const ma_groove *g)
{
    float a = (float)g->s.p3angle * 10.0f;
    if (!g->playing)
        return a;
    float pos = ((float)(g->bar < 0 ? 0 : g->bar) + (float)(g->step < 0 ? 0 : g->step) / 16.0f);
    if (g->s.p3mode == MA_P3_ORBIT)
        return a + pos / 8.0f * 360.0f;
    if (g->s.p3mode == MA_P3_STEP) {
        uint32_t r = (uint32_t)((g->bar < 0 ? 0 : g->bar) / 4) * 2654435761u + 99u;
        return a + (float)rnd_n(&r, 12) * 30.0f;
    }
    return a;
}

/* the block: notes in time, then the horns' tone, gain and place for the glue */
void ma_process(ma_groove *g, int frames)
{
    if (g->playing) {
        float sf = step_frames(g);
        int left = frames;
        while (left > 0) {
            if (g->wait <= 0.0f) {
                do_step(g);
                g->wait += sf;
            }
            int chunk = (int)g->wait + 1;
            if (chunk > left)
                chunk = left;
            if (chunk < 1)
                chunk = 1;
            g->clock += (uint32_t)chunk;
            g->wait -= (float)chunk;
            left -= chunk;
            for (int k = 0; k < npend;) {
                if ((int32_t)(g->clock - pend[k].at) >= 0) {
                    if (!g->muted[pend[k].part])
                        note(g, pend[k].part, pend[k].note, pend[k].vel, pend[k].len);
                    pend[k] = pend[--npend];
                } else {
                    ++k;
                }
            }
            for (int k = 0; k < g->nheld;) {
                if ((int32_t)(g->clock - g->held[k].off) >= 0)
                    note_off_now(g, k);
                else
                    ++k;
            }
        }
    }
    for (int p = 0; p < MA_PARTS; ++p) {
        g->tone[p] = 127;
        g->gain[p] = 0;
    }
    g->pan_horns = 64;
    /* the horns' MUTE, on the section and the solo alike (one track, one mute: a bolero's horns all muted); a mute
     * is a trumpet's: on the other HORNS sounds (a sax, the violins, a guitar) it does nothing */
    /* AUTO: the section's chords open; each solo turn takes the next mute, round the four */
    bool solo_now = g->soloing || g->keys;
    if (solo_now && !g->solo_was)
        g->solo_mute = (uint8_t)(g->solo_mute % 4u + 1u);
    g->solo_was = solo_now;
    int mute = g->s.mute % MA_MUTE_COUNT;
    if (mute == MA_MUTE_AUTO)
        mute = solo_now ? (g->solo_mute ? g->solo_mute : MA_MUTE_STRAIGHT) : MA_MUTE_OPEN;
    if (!g->brass)
        mute = MA_MUTE_OPEN;
    g->mute_live = (uint8_t)mute;
    /* (the sampled brass: the engine's mute filter is the mute; the TONE only takes a little off the cup's top, and
     * the wah's sweep is the filter's) */
    static const float MUTE_TONE_BRASS[MA_MUTE_COUNT] = {1.0f, 1.0f, 0.85f, 1.0f, 0.6f, 1.0f};
    float t = MUTE_TONE_BRASS[mute];
    if (mute == MA_MUTE_WAH) {                /* the wah: the plunger opening and closing on the eighths */
        float beat = g->playing ? ((float)(g->step < 0 ? 0 : g->step) + 1.0f - g->wait / step_frames(g)) / 2.0f : 0.0f;
        t = 0.3f + 0.65f * (0.5f + 0.5f * sin_turns(beat));
    }
    /* the styles whose horns sit back (the bolero's: a muted trumpet behind the voice that is not there) */
    static const uint8_t HORNS_BACK[MA_STYLES] = {[MA_ST_BOLERO] = 85};
    float gn = g->brass ? (float)HORNS_BACK[clampi(g->s.style, 0, MA_STYLES - 1)] / 127.0f : 0.0f;
    if (!g->soloing && !g->keys) {                 /* the section: its mute, in the middle */
        g->tone[MA_HORNS] = (uint8_t)clampi((int)(clampf(t, 0.0f, 1.0f) * 127.0f + 0.5f), 0, 127);
        g->gain[MA_HORNS] = (uint8_t)clampi((int)(gn * 127.0f + 0.5f), 0, 127);
        return;
    }
    /* the solo: the mute, the BELL, PAN 3D (behind: duller, as the bell turns away; far: softer, darker) */
    t *= 0.6f + 0.4f * (float)g->s.bell / 100.0f;
    float ang = p3_angle_now(g);
    g->p3_angle = ang;
    if (g->s.p3mode != MA_P3_OFF) {
        float turns = ang / 360.0f, front = cos_turns(turns);   /* 1 in front, -1 behind */
        t *= 0.62f + 0.38f * (0.5f + 0.5f * front);
        t *= 0.88f + 0.24f * (float)g->s.p3height / 100.0f;
        float dist = (float)g->s.p3dist / 100.0f;
        t *= 1.0f - 0.35f * dist;
        gn += 0.6f * dist;
        g->pan_horns = (uint8_t)clampi(64 + (int)(63.0f * sin_turns(turns) * (0.4f + 0.6f * (float)g->s.width / 100.0f)), 0, 127);
    }
    g->tone[MA_HORNS] = (uint8_t)clampi((int)(clampf(t, 0.0f, 1.0f) * 127.0f + 0.5f), 0, 127);
    g->gain[MA_HORNS] = (uint8_t)clampi((int)(gn * 127.0f + 0.5f), 0, 127);
}
