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
static const char *const SEC_NAMES[MA_SECS] = {"INTRO", "VERSO", "MONTUNO", "MAMBO", "MONTUNO", "MONA", "CIERRE"};
static const char *const SEC_SHORT[MA_SECS] = {"IN", "VRS", "MON", "MBO", "MON", "MNA", "FIN"};
const char *ma_sec_name(int s) { return SEC_NAMES[clampi(s, 0, MA_SECS - 1)]; }
const char *ma_sec_short(int s) { return SEC_SHORT[clampi(s, 0, MA_SECS - 1)]; }
static const char *const DR_NAMES[MA_DR_COUNT] = {
    "QUINTO", "CONGA", "CONGA M", "TUMBA", "BONGO H", "BONGO L", "BONGO M", "TIMBAL H", "TIMBAL L", "RIM", "PAILA",
    "BELL", "BONGO BELL", "BELL M", "AGOGO H", "AGOGO L", "CLAVE", "WOOD", "GUIRO", "GUIRO L", "SHAKER", "SHAKER B",
    "CABASA", "TRIANGLE", "TRI M", "TOM H", "TOM L", "KICK", "WASHBOARD", "WASH TAP"};
const char *ma_drum_name(int d) { return DR_NAMES[clampi(d, 0, MA_DR_COUNT - 1)]; }

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
 * Sounds (ma_sounds.h): PIANO 0 UPRIGHT 1 EPIANO 2 ORGAN 3 TRES 4 HARP 5 ACCORDION 6 BANDONEON 7 MARIMBA; BASS 0 BABY
 * 1 UPRIGHT 2 FM BASS 3 SUB 4 SLAP 5 FRETLESS 6 ROUND 7 TUBA; HORNS 0 TPT+TBN 1 TRUMPET 2 BRASS 3 SAX 4 FLUTE 5 VIOLINS
 * 6 ACCORDION 7 BANDONEON. The percussion's set is the style's own (snd[3] = the style) */
const ma_style MA_STYLE[MA_STYLES] = {
    {"SALSA", MA_SIDE_CALIENTE, 184, 9, MA_SC_MINOR, 70, MA_CLAVE_32, MA_HITS_8, {0, 0, 0}, {3, 1, 6, 0}, 1, 0,
     PF_MONTUNO, BF_TUMBAO, HF_SALSA, MA_ST_SALSA, 1, 30, 35, 20, MA_MUTE_OPEN, {{1, 3, 0}, {1, 2, 5}, {1, 2, 0}}},
    {"MAMBO", MA_SIDE_CALIENTE, 192, 7, MA_SC_MAJOR, 85, MA_CLAVE_23, MA_HITS_8, {0, 1, 0}, {1, 0, 11, 2}, 1, 0,
     PF_MONTUNO, BF_TUMBAO, HF_MAMBO, MA_ST_MAMBO, 1, 35, 40, 25, MA_MUTE_OPEN, {{1, 0, 7}, {0, 2, 6}, {2, 0, 3}}},
    {"MERENGUE", MA_SIDE_CALIENTE, 152, 2, MA_SC_MAJOR, 80, MA_CLAVE_32, MA_HITS_8, {0, 2, 3}, {2, 0, 9, 3}, 1, 0,
     PF_MERENGUE, BF_MERENGUE, HF_MERENGUE, MA_ST_MERENGUE, 0, 25, 25, 25, MA_MUTE_OPEN,
     {{5, 1, 0}, {0, 6, 4}, {6, 0, 2}}},
    {"CUMBIA", MA_SIDE_CALIENTE, 98, 7, MA_SC_MINOR, 60, MA_CLAVE_32, MA_HITS_16, {5, 6, 6}, {9, 2, 3, 0}, 1, 10,
     PF_CUMBIA, BF_CUMBIA, HF_CUMBIA, MA_ST_CUMBIA, 0, 30, 20, 30, MA_MUTE_OPEN, {{2, 0, 1}, {0, 2, 3}, {4, 3, 0}}},
    {"SAMBA", MA_SIDE_CALIENTE, 102, 0, MA_SC_MAJOR, 85, MA_CLAVE_32, MA_HITS_8, {3, 1, 2}, {7, 0, 8, 1}, 1, 0,
     PF_SAMBA, BF_SAMBA, HF_SAMBA, MA_ST_SAMBA, 0, 30, 30, 20, MA_MUTE_OPEN, {{4, 1, 7}, {0, 5, 6}, {0, 4, 3}}},
    {"SON", MA_SIDE_NOCHE, 112, 4, MA_SC_DORIAN, 55, MA_CLAVE_32, MA_HITS_16, {3, 1, 1}, {0, 6, 3, 1}, 1, 0,
     PF_GUAJEO, BF_TUMBAO, HF_SON, MA_ST_SON, 1, 35, 30, 30, MA_MUTE_OPEN, {{0, 4, 7}, {0, 6, 5}, {0, 4, 3}}},
    {"CHA-CHA", MA_SIDE_NOCHE, 120, 5, MA_SC_MAJOR, 55, MA_CLAVE_23, MA_HITS_16, {0, 0, 5}, {11, 0, 1, 2}, 1, 0,
     PF_CHACHA, BF_CHACHA, HF_CHACHA, MA_ST_CHACHA, 0, 35, 35, 20, MA_MUTE_STRAIGHT,
     {{1, 7, 2}, {1, 6, 2}, {4, 1, 3}}},
    {"BOLERO", MA_SIDE_NOCHE, 80, 2, MA_SC_HARMONIC, 35, MA_CLAVE_32, MA_HITS_OFF, {1, 6, 1}, {4, 5, 3, 1}, 1, 0,
     PF_BOLERO, BF_BOLERO, HF_BOLERO, MA_ST_BOLERO, 1, 50, 45, 25, MA_MUTE_HARMON, {{0, 4, 2}, {1, 5, 0}, {5, 3, 4}}},
    {"BOSSA", MA_SIDE_NOCHE, 136, 4, MA_SC_MAJOR, 40, MA_CLAVE_32, MA_HITS_OFF, {1, 5, 4}, {7, 8, 1, 6}, 1, 0,
     PF_BOSSA, BF_BOSSA, HF_BOSSA, MA_ST_BOSSA, 0, 40, 35, 30, MA_MUTE_CUP, {{0, 2, 4}, {1, 6, 0}, {3, 1, 5}}},
    {"TANGO", MA_SIDE_NOCHE, 118, 9, MA_SC_HARMONIC, 70, MA_CLAVE_32, MA_HITS_8, {0, 1, 7}, {10, 5, 3, 4}, 1, 0,
     PF_TANGO, BF_TANGO, HF_TANGO, MA_ST_TANGO, 0, 30, 30, 35, MA_MUTE_OPEN, {{6, 0, 1}, {0, 2, 6}, {0, 7, 5}}},
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
    [SR_FAST] = {{2, 0, 0, 2, 0, 0, 2, 0, 0, 0, 2, 0, 2, 0, 2, 0},
                 {0, 0, 2, 0, 2, 0, 0, 2, 0, 0, 2, 0, 4, 0, 0, 0},
                 {1, 0, 1, 0, 2, 0, 0, 2, 0, 0, 1, 1, 2, 0, 0, 0},
                 {3, 0, 0, 3, 0, 0, 2, 0, 2, 0, 2, 0, 4, 0, 0, 0}},
    [SR_MID] = {{4, 0, 0, 0, 2, 0, 2, 0, 3, 0, 0, 1, 4, 0, 0, 0},
                {2, 0, 2, 0, 4, 0, 0, 0, 2, 0, 2, 0, 4, 0, 0, 0},
                {0, 0, 2, 0, 2, 0, 4, 0, 0, 0, 2, 0, 4, 0, 0, 0},
                {3, 0, 0, 3, 0, 0, 2, 0, 6, 0, 0, 0, 0, 0, 2, 0}},
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
/* the washboard: a scrape on the beat, the thimbles tapping the sixteenths between (the guira's way) */
#define L_WASH_0 {MA_DR_WASH, G_EXTRA, "X...x...X...x...X...x...X...x..."}
#define L_WASH_1 {MA_DR_WASH_TAP, G_EXTRA, "..oo..oo..oo..oo..oo..oo..oo..oo"}
static const lane_t L_WASH[2] = {L_WASH_0, L_WASH_1};

static const perc_set_t PERC[MA_STYLES] = {
    [MA_ST_SALSA] = {7, {L_TUMBAO_0, L_TUMBAO_1, L_TUMBAO_2, L_MARTILLO_0, L_MARTILLO_1, L_MARTILLO_2,
                         L_CASCARA_0}},
    [MA_ST_MAMBO] = {8, {L_TUMBAO_0, L_TUMBAO_1, L_TUMBAO_2, L_MARTILLO_0, L_MARTILLO_1, L_CASCARA_0,
                         {MA_DR_GUIRO_SHORT, G_EXTRA, "x.o.x.o.x.o.x.o.x.o.x.o.x.o.x.o."},
                         {MA_DR_TIMBAL_LO, G_OTHER, "..............................x."}}},
    [MA_ST_MERENGUE] = {7, {{MA_DR_TUMBA, G_CONGA, "X.......x.......X.......x......."},
                            {MA_DR_CONGA_MUTE, G_CONGA, "..o...o.o.o...o...o...o.o.o...o."},
                            {MA_DR_QUINTO, G_CONGA, "....x.......x.x.....x.......x.x."},
                            {MA_DR_WASH, G_EXTRA, "X...x.x.X...x.x.X...x.x.X...x.x."},   /* the guira */
                            {MA_DR_WASH_TAP, G_EXTRA, ".oo.o..o.oo.o..o.oo.o..o.oo.o..o"},
                            {MA_DR_KICK, G_KICK, "x...x...x...x...x...x...x...x..."},
                            {MA_DR_BONGO_MUTE, G_BONGO, "......o.......o.......o.......o."}}},
    [MA_ST_CUMBIA] = {6, {{MA_DR_CONGA, G_CONGA, "....x.......x.......x.......x..."},
                          {MA_DR_CONGA_MUTE, G_CONGA, "..o.....o.o...o...o.....o.o...o."},
                          {MA_DR_QUINTO, G_CONGA, "...........x...x...........x.x.."},
                          {MA_DR_GUIRO_LONG, G_EXTRA, "x...x...x...x...x...x...x...x..."},
                          {MA_DR_GUIRO_SHORT, G_EXTRA, "..oo..oo..oo..oo..oo..oo..oo..oo"},
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
    [MA_ST_CHACHA] = {7, {L_GUIRO_0, {MA_DR_GUIRO_LONG, G_EXTRA, "....x.......x.......x.......x..."},
                          {MA_DR_BELL_MAMBO, G_TIMBAL, "X...o...X...o...X...o...X...o..."},
                          {MA_DR_BELL_MUTE, G_TIMBAL, "....x.......x.......x.......x..."},
                          {MA_DR_CONGA_MUTE, G_CONGA, "o...X...o...o...o...X...o...o..."},
                          {MA_DR_CONGA, G_CONGA, "............x.x.............x.x."},
                          {MA_DR_TIMBAL_LO, G_OTHER, "............................x.x."}}},
    [MA_ST_BOLERO] = {5, {{MA_DR_BONGO_HI, G_BONGO, "x.......o.o.o...x.......o.o.o..."},
                          {MA_DR_BONGO_LO, G_BONGO, "......o.......x.......o.......x."},
                          L_CBOLERO_0, L_CBOLERO_1,
                          {MA_DR_SHAKER_FWD, G_EXTRA, "o...o...o...o...o...o...o...o..."}}},
    [MA_ST_BOSSA] = {4, {{MA_DR_TIMBAL_RIM, G_TIMBAL, "x..x..x...x..x..x..x..x...x..x.."},
                         {MA_DR_KICK, G_KICK, "o.....o.o.....o.o.....o.o.....o."},
                         {MA_DR_SHAKER_FWD, G_EXTRA, "xoooxoooxoooxoooxoooxoooxoooxooo"},
                         {MA_DR_TRIANGLE_MUTE, G_OTHER, "..o...o...o...o...o...o...o...o."}}},
    /* the tango has no drums: the band is the rhythm. A wooden knock on the strong beats when HEAT is up */
    [MA_ST_TANGO] = {2, {{MA_DR_WOOD, G_OTHER, "x...............x..............."},
                         {MA_DR_TOM_LO, G_KICK, "o...............o..............."}}},
};

/* the lanes for the PERC panel: CONGA BONGO TIMBAL EXTRA CLAVE and the rest */
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
        if (part == MA_PERC)
            g->dr_hits[clampi(n, 0, MA_DR_COUNT - 1)] |= (uint16_t)(1u << g->step);
        else
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
static void schedule(ma_groove *g, uint32_t delay, int part, int n, int vel, float len)
{
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
    case MA_SEC_IN: return 4;
    case MA_SEC_VRS: return g->s.verse >= 16 ? 16 : 8;
    case MA_SEC_MON:
    case MA_SEC_MON2: return g->s.mbars >= 16 ? 16 : 8;
    case MA_SEC_FIN: return 4;
    default: return 8;                             /* the mambo, the moña */
    }
}
static int sec_next(const ma_groove *g, int sec)
{
    if (sec == MA_SEC_FIN)
        return MA_SEC_VRS;
    if (sec == MA_SEC_MON && !g->s.mambo)          /* no mambo: the montuno goes to the cierre */
        return MA_SEC_FIN;
    return sec + 1;
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

/* a new bar: what waited for the bar line comes in, the form moves on */
static void bar_line(ma_groove *g)
{
    for (int p = 0; p < MA_PARTS; ++p) {
        g->hits_last[p] = g->hits[p];
        g->hits[p] = 0;
    }
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
        }
    }
}

/* is part p out (the form, the descarga, its level 0) */
static bool part_out(const ma_groove *g, int p)
{
    if (g->s.lvl[p] == 0)
        return true;
    if (g->desc)                                   /* the descarga: the percussion alone */
        return p != MA_PERC;
    if (!g->s.arc)
        return false;
    if (g->sec == MA_SEC_IN) {                     /* the percussion, then the piano, then the bass, the horns last */
        static const uint8_t IN_AT[MA_PARTS] = {1, 2, 3, 0};
        return g->sec_bar < IN_AT[p];
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
        if (c != 'x' && c != 'X' && c != 'o')
            continue;
        if (c == 'o' && heat < 0.25f && (i % 2))   /* cool: the soft sixteenths rest */
            continue;
        int v = c == 'X' ? 118 : c == 'x' ? 100 : 72;
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
    bool bells = sc == MA_SEC_MON || sc == MA_SEC_MON2 || sc == MA_SEC_MBO || sc == MA_SEC_MONA;
    bool salsa_like = s->snd[3] == MA_ST_SALSA || s->snd[3] == MA_ST_MAMBO || s->snd[3] == MA_ST_SON;
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
        if (s->snd[3] == MA_ST_TANGO && heat < 0.6f)
            continue;
        if (intro_soft && l->group != G_CONGA && l->group != G_EXTRA)
            continue;                              /* (the first bar: the congas and the shakers alone) */
        play_lanes(g, l, 1, i, delay, hum, heat);
    }
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
static void play_piano(ma_groove *g, int i, uint32_t delay, float sf, float hum)
{
    const ma_style *st = &MA_STYLE[g->s.style];
    const char *pat = PIANO_PAT[st->piano][g->s.montuno & 3];
    char ch = pat[cbar(g, g->bar) * 16 + i];
    if (ch == '.' || ch == ' ')
        return;
    bool acc = ch >= 'A' && ch <= 'Z';
    char c = acc ? (char)(ch - 'A' + 'a') : ch;
    float heat = (float)g->s.heat / 100.0f;
    int vel = (acc ? 112 : 96) + (i % 4 == 0 ? 4 : 0) + (int)(hum * 8.0f);
    vel = (int)((float)vel * (0.85f + 0.15f * heat));
    float gate = st->piano == PF_BOLERO ? 1.6f : st->piano == PF_TANGO ? 0.45f : 0.7f;
    float len = sf * 2.0f * gate;
    switch (c) {
    case 'o': {                                    /* a chord tone in octaves: the montuno's melody, walking the chord */
        const ma_chord *ch2 = chord_for(g, i >= 14);
        static const uint8_t WALK[8] = {0, 1, 2, 1, 0, 2, 1, 0};
        int k = WALK[((g->bar & 1) * 8 + i / 2) % 8];
        int n = tone_at(ch2, k, 64);
        schedule(g, delay, MA_PIANO, n, clampi(vel, 1, 127), len);
        if (st->piano != PF_GUAJEO || heat > 0.5f) /* (the tres's line single; hotter, doubled) */
            schedule(g, delay, MA_PIANO, n + 12, clampi(vel - 8, 1, 127), len);
        break;
    }
    case 't':
    case 'n': {
        const ma_chord *ch2 = chord_for(g, c == 'n');
        int t[3];
        triad_at(ch2, st->piano == PF_BOSSA ? 58 : 62, t);
        for (int k = 0; k < 3; ++k)
            schedule(g, delay, MA_PIANO, t[k], clampi(vel - (k == 2 ? 0 : 6), 1, 127), c == 'n' ? sf * 3.0f : len);
        break;
    }
    case 'a': {                                    /* the arpeggio: up through the chord, the bolero's */
        const ma_chord *ch2 = chord_at(g, g->bar);
        int k = (i / 2) % 4;
        int n = tone_at(ch2, k == 3 ? 1 : k, 60) + (k == 3 ? 12 : 0);
        schedule(g, delay, MA_PIANO, n, clampi(vel - 10, 1, 127), sf * 4.0f);
        break;
    }
    case 'b':                                      /* the left hand's root */
        schedule(g, delay, MA_PIANO, tone_at(chord_at(g, g->bar), 0, 43), clampi(vel, 1, 127), st->piano == PF_TANGO ? sf : sf * 6.0f);
        break;
    case 'c': {                                    /* the left hand's chord, low (the tango's marcato) */
        int t[3];
        triad_at(chord_at(g, g->bar), 52, t);
        for (int k = 0; k < 3; ++k)
            schedule(g, delay, MA_PIANO, t[k], clampi(vel - 6, 1, 127), sf * 0.9f);
        break;
    }
    default: break;
    }
}

static int bass_root(const ma_chord *c)
{
    int root = 36 + ((c->root - 36) % 12 + 12) % 12;   /* C2 .. B2, then G#1 .. G2: the bass sits low */
    return root > 43 ? root - 12 : root;
}

static void play_bass(ma_groove *g, int i, uint32_t delay, float sf, float hum)
{
    const ma_style *st = &MA_STYLE[g->s.style];
    const char *pat = BASS_PAT[st->bass][g->s.tumbao & 3];
    int pos = cbar(g, g->bar) * 16 + i;
    char c = pat[pos];
    if (c == '.' || c == '-')
        return;
    int len = 1;
    while (pos + len < 32 && pat[pos + len] == '-')
        ++len;
    if (pos + len == 32 && pat[0] == '-')          /* (a note held over into the next clave) */
        ++len;
    const ma_chord *cur = chord_at(g, g->bar);
    int bpc = ma_bars_per_chord(&g->s);
    bool last_bar = ((g->bar < 0 ? 0 : g->bar) % bpc) == bpc - 1;
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

/* the horns: the section's riff (two horns a chord tone apart), the solo's phrase, or nothing (the keys have it) */
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
        if (g->brass)                              /* the sampled brass: the trombone, an octave under the trumpet's */
            low -= 12;
        float heat = (float)s->heat / 100.0f;
        int vel = 104 + (g->riff_len[pos] == 1 ? 10 : 0) + (int)(hum * 6.0f) + (int)(heat * 10.0f);
        float len = sf * (float)g->riff_len[pos] * (g->riff_len[pos] == 1 ? 0.6f : 0.9f);
        schedule(g, delay, MA_HORNS, top, clampi(vel, 1, 127), len);
        schedule(g, delay, MA_HORNS, low, clampi(vel - 10, 1, 127), len);
        return;
    }
    /* the solo: in the verso the melody, in the montuno every other four bars an improvised answer (a variation of
     * the phrase), in the moña over the riff's rests; the keys take it over */
    g->soloing = true;
    if (g->keys || s->solo != MA_SOLO_PHRASE)
        return;
    bool plays = sc == MA_SEC_VRS || sc == MA_SEC_MONA || ((sc == MA_SEC_MON || sc == MA_SEC_MON2) && (sec_bar(g) / 4) % 2 == 1);
    if (!s->arc)
        plays = (sec_bar(g) / 4) % 2 == 1;
    if (!plays)
        return;
    int hp = (sec_bar(g) % 4) * 16 + i;
    if (g->phr[hp] == -128)
        return;
    int len = g->phr_len[hp];
    int base = 60 + s->key + (s->key < 5 ? 12 : 0);
    int n = step_note(s->scale, base, g->phr[hp]);
    const ma_chord *c = chord_at(g, g->bar);
    if (len >= 2 || i % 4 == 0) {                  /* the long and strong notes onto the chord */
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
    if (sc != MA_SEC_VRS && s->arc) {              /* the montuno's improvisation: the phrase moved about */
        uint32_t r = (uint32_t)(g->bar * 131 + i * 7 + g->round * 977);
        if (rnd_n(&r, 3) == 0)
            n += rnd_n(&r, 2) ? 2 : -2;
        n = ma_snap(s, n);
    }
    int vel = 100 + (i % 4 == 0 ? 8 : 0) + (int)(hum * 6.0f);
    schedule(g, delay, MA_HORNS, n, clampi(vel, 1, 127), sf * (float)len * 0.88f);
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
    if (ev && (sc == MA_SEC_MON || sc == MA_SEC_MON2) && (sec_bar(g) % ev) == ev - 1)
        return true;
    return false;
}

/* one sixteenth: the parts, swung and humanized */
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
        switch (p) {
        case MA_PIANO: play_piano(g, i, d, sf, hum); break;
        case MA_BASS: play_bass(g, i, d, sf, hum); break;
        case MA_HORNS: play_horns(g, i, d, sf, hum); break;
        default: play_perc(g, i, d, hum); break;
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
    g->mute_live = MA_MUTE_OPEN;
    if (!g->soloing && !g->keys)                   /* the section: open, in the middle */
        return;
    g->mute_live = (uint8_t)(g->s.mute % MA_MUTE_COUNT);
    /* the solo: the MUTE's darkness, the BELL, PAN 3D (behind: duller, as the bell turns away; far: softer, darker) */
    static const float MUTE_TONE[MA_MUTE_COUNT] = {1.0f, 0.72f, 0.55f, 0.46f, 0.6f};
    /* (the sampled brass: the engine's mute filter is the mute; the TONE only takes a little off the cup's top, and
     * the wah's sweep is the filter's) */
    static const float MUTE_TONE_BRASS[MA_MUTE_COUNT] = {1.0f, 1.0f, 0.85f, 1.0f, 0.6f};
    float t = (g->brass ? MUTE_TONE_BRASS : MUTE_TONE)[g->s.mute % MA_MUTE_COUNT];
    if (g->s.mute == MA_MUTE_WAH) {                /* the wah: the plunger opening and closing on the eighths */
        float beat = g->playing ? ((float)(g->step < 0 ? 0 : g->step) + 1.0f - g->wait / step_frames(g)) / 2.0f : 0.0f;
        t = 0.3f + 0.65f * (0.5f + 0.5f * sin_turns(beat));
    }
    t *= 0.6f + 0.4f * (float)g->s.bell / 100.0f;
    float gn = 0.0f;
    float ang = p3_angle_now(g);
    g->p3_angle = ang;
    if (g->s.p3mode != MA_P3_OFF) {
        float turns = ang / 360.0f, front = cos_turns(turns);   /* 1 in front, -1 behind */
        t *= 0.62f + 0.38f * (0.5f + 0.5f * front);
        t *= 0.88f + 0.24f * (float)g->s.p3height / 100.0f;
        float dist = (float)g->s.p3dist / 100.0f;
        t *= 1.0f - 0.35f * dist;
        gn = 0.6f * dist;
        g->pan_horns = (uint8_t)clampi(64 + (int)(63.0f * sin_turns(turns) * (0.4f + 0.6f * (float)g->s.width / 100.0f)), 0, 127);
    }
    g->tone[MA_HORNS] = (uint8_t)clampi((int)(clampf(t, 0.0f, 1.0f) * 127.0f + 0.5f), 0, 127);
    g->gain[MA_HORNS] = (uint8_t)clampi((int)(gn * 127.0f + 0.5f), 0, 127);
}
