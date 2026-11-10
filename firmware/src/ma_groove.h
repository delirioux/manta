/* SPDX-License-Identifier: GPL-3.0-only */
/* MANTA groove engine: ten Latin styles played by a band on four parts (PIANO, BASS, HORNS, PERC), locked to the
 * clave. It runs in time, sixteenth by sixteenth, and hands each note-on and note-off to the sound (Felucca's tracks on
 * the FM-1, a renderer on the host). Besides the notes it computes, every block, what the glue turns into sound:
 *
 *   the TONE   how bright each part sounds (0..127): the horns' MUTE, WAH and BELL, the solo behind you (PAN 3D)
 *   the GAIN   how far each part is turned down (0..127): the solo far away (PAN 3D DIST)
 *   the PAN    where the horns sit (0..127, 64 the middle): PAN 3D's angle, ORBIT going round
 *
 * A song is a style plus a number (the SONG, turned by ALGORITHM): the number picks the chord loop, the patterns, the
 * horns' riffs and the solo's phrase, so the same style and number always play the same song. A new style or song
 * comes in on the next bar line; the knobs act at once.
 *
 * The clave is two bars: 3-2 (the three side first) or 2-3. The piano's montuno, the bass's tumbao, the horns' riffs
 * and the percussion all phrase with it.
 *
 * The song's form (ARC on): IN (the percussion, then the piano, then the bass), VRS (the verso: the solo trumpet sings
 * the melody over the band), MON (the montuno: the piano's figure, the solo improvising, the bongos on the bell),
 * MBO (the mambo: the horn section's riff, the timbales on the big bell), MON again, MOÑ (the moña: the horns' riff
 * with the solo over it), FIN (the cierre: the band's hits on the clave, then round again from the verso with a new
 * melody). The MAMBO card leaves the mambo out. ARC off: the montuno on and on.
 *
 * Performance: BAND HIT (REC tapped: on the next downbeat the whole band hits the clave's figure together, a cierre,
 * and is back the bar after; held: a descarga, the percussion alone until let go, a timbal roll, the band back on the
 * bar line), MAMBO (SEQ: the horn section into its riff from the next bar), the keys (the solo, live).
 *
 * No libm, no doubles: floats on the FPU (single precision), as Sill, Daurade, Flyfish and Tuna. */
#ifndef MA_GROOVE_H
#define MA_GROOVE_H

#include <stdbool.h>
#include <stdint.h>

enum { MA_PIANO, MA_BASS, MA_HORNS, MA_PERC, MA_PARTS };
enum { MA_SIDE_CALIENTE, MA_SIDE_NOCHE };
#define MA_STYLES 10
enum { MA_ST_SALSA, MA_ST_MAMBO, MA_ST_MERENGUE, MA_ST_CUMBIA, MA_ST_SAMBA,
       MA_ST_SON, MA_ST_CHACHA, MA_ST_BOLERO, MA_ST_BOSSA, MA_ST_TANGO };
enum { MA_SC_MINOR, MA_SC_MAJOR, MA_SC_DORIAN, MA_SC_HARMONIC, MA_SCALES };
enum { MA_CLAVE_32, MA_CLAVE_23, MA_CLAVE_COUNT };
enum { MA_SOLO_OFF, MA_SOLO_PHRASE, MA_SOLO_KEYS, MA_SOLO_COUNT };
enum { MA_HITS_OFF, MA_HITS_4, MA_HITS_8, MA_HITS_16, MA_HITS_COUNT };
enum { MA_MUTE_OPEN, MA_MUTE_STRAIGHT, MA_MUTE_CUP, MA_MUTE_HARMON, MA_MUTE_WAH, MA_MUTE_COUNT };
enum { MA_PHR_GEN, MA_PHR_TRAD, MA_PHR_MIX, MA_PHR_COUNT };
enum { MA_P3_OFF, MA_P3_FIXED, MA_P3_ORBIT, MA_P3_STEP, MA_P3_COUNT };   /* PAN 3D's MODE */
enum { MA_FILLS_OFF, MA_FILLS_8, MA_FILLS_4, MA_FILLS_2, MA_FILLS_COUNT };
/* the PERC page's lanes: each OFF, the style's own (AUTO), or a pattern */
enum { MA_CONGA_OFF, MA_CONGA_AUTO, MA_CONGA_TUMBAO, MA_CONGA_MARCHA, MA_CONGA_BOLERO, MA_CONGA_COUNT };
enum { MA_BONGO_OFF, MA_BONGO_AUTO, MA_BONGO_MARTILLO, MA_BONGO_BELL, MA_BONGO_COUNT };
enum { MA_TIMBAL_OFF, MA_TIMBAL_AUTO, MA_TIMBAL_CASCARA, MA_TIMBAL_BELL, MA_TIMBAL_COUNT };
enum { MA_EXTRA_OFF, MA_EXTRA_AUTO, MA_EXTRA_GUIRO, MA_EXTRA_SHAKER, MA_EXTRA_CABASA, MA_EXTRA_WASH, MA_EXTRA_COUNT };
/* the percussion as the groove names it: the kit's one-shots in order (tools/ma_perc.py, ma_sounds.h) */
enum { MA_DR_QUINTO, MA_DR_CONGA, MA_DR_CONGA_MUTE, MA_DR_TUMBA, MA_DR_BONGO_HI, MA_DR_BONGO_LO, MA_DR_BONGO_MUTE,
       MA_DR_TIMBAL_HI, MA_DR_TIMBAL_LO, MA_DR_TIMBAL_RIM, MA_DR_PAILA, MA_DR_BELL_MAMBO, MA_DR_BELL_BONGO,
       MA_DR_BELL_MUTE, MA_DR_AGOGO_HI, MA_DR_AGOGO_LO, MA_DR_CLAVE, MA_DR_WOOD, MA_DR_GUIRO_SHORT, MA_DR_GUIRO_LONG,
       MA_DR_SHAKER_FWD, MA_DR_SHAKER_BACK, MA_DR_CABASA, MA_DR_TRIANGLE, MA_DR_TRIANGLE_MUTE, MA_DR_TOM_HI,
       MA_DR_TOM_LO, MA_DR_KICK, MA_DR_WASH, MA_DR_WASH_TAP, MA_DR_COUNT };
/* the form's sections, in the order they play (ARC on); after FIN, round again from VRS */
enum { MA_SEC_IN, MA_SEC_VRS, MA_SEC_MON, MA_SEC_MBO, MA_SEC_MON2, MA_SEC_MONA, MA_SEC_FIN, MA_SECS };
#define MA_SETS 8
#define MA_SOUNDS 8                                /* each part's SOUND list (ma_sounds.h) */
#define MA_PIANO_UPRIGHT 0                         /* PIANO sound 0: the sampled upright */
#define MA_LOOPS 12                                /* the chord loops (ma_groove.c MA_LOOP) */

/* what the player set: the cards. Saved as it is (ma_fw.c), so append only */
typedef struct {
    uint8_t style;
    uint8_t heat, clave, solo, hits;               /* PLAY: HEAT 0..100, CLAVE, SOLO, HITS */
    uint8_t snd[4];                                /* SOUND: PIANO BASS HORNS (each part's list), PERC (a style's set) */
    uint8_t mute, phrase, bell;                    /* SOLO: MUTE, PHRASE, BELL 0..100 (INSTR is SOUND's HORNS) */
    uint8_t p3mode, p3angle, p3height, p3dist;     /* PAN 3D: MODE, ANGLE 0..35 (tens of degrees, 0 in front),
                                                    * HEIGHT 0..100 (50 level), DIST 0..100 */
    uint8_t key, scale, chords, bars;              /* KEY: 0..11 (C..B), MA_SC_*, the loop (0..MA_LOOPS-1), 1 2 4 */
    uint8_t swing, montuno, tumbao, fills;         /* GROOVE: SWING 0..60 %, MONTUNO 0..3, TUMBAO 0..3, FILLS */
    uint8_t conga, bongo, timbal, extra;           /* PERC */
    uint8_t lvl[4];                                /* MIX 0..100 */
    uint8_t room, plate, warmth, width;            /* MIX again: the reverb, the horns' and piano's plate, the tape */
    uint8_t arc, verse, mbars, mambo;              /* ARC: on, verse bars 8 16, montuno bars 8 16, the mambo on */
    uint8_t bpm;                                   /* SELECT: 60..220 */
    uint8_t palette, look, keysnap;                /* MENU: COLOR, LOOK (FLAT, LINE), SNAP (the keys onto the scale) */
    uint8_t human;                                 /* (no card) */
    uint16_t song;                                 /* ALGORITHM: the song number in the style, 0..999 */
} ma_settings;

typedef struct {
    uint8_t on, part, note, vel;                   /* PERC: note is MA_DR_*; vel 1..127 (on) */
} ma_event;

/* what the screen hears: each note-on */
typedef struct { uint8_t part, note, vel, pad; } ma_seen;

/* a chord as played: its root (pitch class), the notes (MIDI) it voices, its name */
typedef struct {
    uint8_t root, n;
    uint8_t note[5];                               /* the piano's voicing, low to high */
    uint8_t tone[4];                               /* chord tones as pitch classes: root, third, fifth, seventh */
    char name[8];
} ma_chord;

#define MA_HELD 24
#define MA_FEED 32
#define MA_LOOP_MAX 8
#define MA_PHR_LEN 64                              /* the solo's phrase: four bars of sixteenths */
#define MA_RIFF_LEN 32                             /* the horns' riff: two bars (a clave) */

typedef struct {
    ma_settings s;
    float sr;
    bool playing;
    uint32_t clock;                                /* frames since PLAY */
    float wait;                                    /* frames until the next sixteenth */
    int step;                                      /* the sixteenth sounding in its bar (0..15; -1 before the first) */
    int bar;                                       /* bars since PLAY (0: the first) */
    /* the song: built from the style, the song number, the key and the loop (ma_rebuild) */
    ma_chord loop[MA_LOOP_MAX];
    int nloop;
    uint8_t pat;                                   /* the song's pattern variation (0..3) */
    uint8_t phr_var, riff_var;
    int8_t phr[MA_PHR_LEN];                        /* scale steps from the key's tonic (-128: rest) */
    uint8_t phr_len[MA_PHR_LEN];
    int8_t riff[MA_RIFF_LEN];                      /* the horns' riff: chord-tone index (0..5 up the chord), -128 rest */
    uint8_t riff_len[MA_RIFF_LEN];
    uint32_t rnd;                                  /* the per-note randomness: seeded per bar */
    /* what waits for the next bar line */
    int next_style;                                /* -1 none */
    int next_song;                                 /* -1 none */
    bool mambo_next;                               /* SEQ: the mambo from the next bar */
    bool mambo_live;                               /* ARC off: the horns' riff on (SEQ toggles it) */
    /* performance */
    int hit_bar;                                   /* BAND HIT: the bar its cierre plays (-1 none) */
    bool hit_wait;                                 /* tapped: on the next bar line */
    bool desc;                                     /* the descarga: the percussion alone */
    bool desc_held;
    int desc_end;                                  /* the bar the band comes back on (-1: held) */
    uint32_t keys;                                 /* the FM-1's keys held (bit note - 53) */
    int key_note[27];                              /* the note each key plays, -1 none */
    /* the form */
    int sec, sec_bar;
    int round;
    /* the block's controls for the glue */
    uint8_t tone[MA_PARTS];                        /* 0..127: how bright each part is (127: as its sound is) */
    uint8_t gain[MA_PARTS];                        /* 0..127: how far each part is turned down (0: not at all) */
    uint8_t pan_horns;                             /* 0..127: the horns' place (64: the middle) */
    float p3_angle;                                /* the solo's angle now (degrees, 0 in front, 90 right) */
    bool muted[MA_PARTS];                          /* out (the form, a break, a level of 0) */
    bool soloing;                                  /* the horns' track is the solo now (the form, the keys) */
    uint8_t mute_live;                             /* the solo's MUTE now (MA_MUTE_OPEN while the section plays) */
    /* in, from the glue: the HORNS sound is the sampled trumpet (its mutes are its own filters, the TONE only the bell
     * and PAN 3D), and has the trombone too (the section's second horn an octave down, on it) */
    bool brass, trombone;
    /* what each part played, a bit per sixteenth: the bar playing and the bar before (the screen) */
    uint16_t hits[MA_PARTS], hits_last[MA_PARTS];
    uint16_t dr_hits[MA_DR_COUNT], dr_last[MA_DR_COUNT];
    uint32_t solo_on_clock;                        /* the solo's latest note-on (the SOLO page) */
    int solo_note;                                 /* its pitch (MIDI), -1 none */
    /* the notes sounding: their note-off at clock off */
    struct { uint8_t note, part; uint32_t off; } held[MA_HELD];
    int nheld;
    ma_seen feed[MA_FEED];
    uint8_t feed_w, feed_r;
    char msg[24];                                  /* a word for the screen ("NEXT MAMBO", "BAND HIT"); the UI clears it */
    uint8_t msg_new;
    uint8_t sound_sig;                             /* bumped when every part's sound must load again (a style, a set) */
    void (*sound)(void *ctx, const ma_event *e);
    void *sound_ctx;
} ma_groove;

typedef struct {
    const char *name;
    uint8_t side, bpm, key, scale;
    uint8_t heat, clave, hits;
    uint8_t snd[3];                                /* PIANO BASS HORNS sound */
    uint8_t loops[4];                              /* the chord loops a song picks from (MA_LOOP indices) */
    uint8_t bars;                                  /* bars a chord */
    uint8_t swing;
    uint8_t piano, bass, horns, perc;              /* the families (ma_groove.c) */
    uint8_t clave_on;                              /* the claves (the sticks) play the clave */
    uint8_t room, plate, warmth;
    uint8_t mute;                                  /* the solo's mute (BOLERO: HARMON) */
    uint8_t alt[3][3];                             /* PIANO BASS HORNS: three more sounds each a song can pick */
} ma_style;

extern const ma_style MA_STYLE[MA_STYLES];

void ma_defaults(ma_settings *s);                  /* SALSA, as it starts */
void ma_style_apply(ma_settings *s, int style);    /* a style's own settings into s (PLAY SOUND SOLO KEY GROOVE PERC and
                                                    * the tempo); MIX, PAN 3D, ARC and MENU stay */
void ma_settings_fix(ma_settings *s);              /* every field into its range (a loaded record) */
void ma_song_sounds(ma_settings *s);               /* the song's sounds: song 1 (0) the style's own, the others one of four */
void ma_init(ma_groove *g, float sample_rate);
void ma_set_sound(ma_groove *g, void (*fn)(void *ctx, const ma_event *e), void *ctx);

void ma_play(ma_groove *g, bool on);
void ma_rebuild(ma_groove *g);                     /* the song again from the settings (KEY, the loop, riff, phrase) */
void ma_queue_style(ma_groove *g, int style);      /* playing: on the next bar line; stopped: at once */
void ma_queue_song(ma_groove *g, int song);
void ma_band_hit(ma_groove *g, bool down);         /* REC: a tap a cierre, held the descarga */
void ma_mambo(ma_groove *g);                       /* SEQ: the mambo from the next bar */
void ma_key(ma_groove *g, int key, int note, bool down);   /* the FM-1's key (0..26) playing note: the solo */
void ma_process(ma_groove *g, int frames);         /* the audio: notes in time, the tone, gain and pan */
void ma_all_off(ma_groove *g);

bool ma_feed_pop(ma_groove *g, ma_seen *n);
int ma_chord_now(const ma_groove *g);              /* the loop's chord sounding (0..nloop-1) */
int ma_bars_per_chord(const ma_settings *s);
int ma_sec_len(const ma_groove *g, int sec);       /* a section's bars */
const char *ma_style_name(int i);
const char *ma_scale_name(int i);
const char *ma_note_name(int pc);
const char *ma_sec_name(int sec);                  /* "INTRO" .. */
const char *ma_sec_short(int sec);                 /* "IN" "VRS" .. */
const char *ma_drum_name(int d);                   /* "CONGA" .. (the PERC page's lanes) */
void ma_loop_name(const ma_settings *s, int loop, char *out, int max);
int ma_snap(const ma_settings *s, int note);       /* a note onto the scale (the nearest step, ties down) */
bool ma_clave_hit(const ma_groove *g, int bar, int step);   /* the clave strikes there (the screen, the hits) */
int ma_clave_side(const ma_groove *g, int bar);    /* 3 or 2: the side of the clave bar b plays */
uint32_t ma_lane_bits(const ma_groove *g, int lane, int bar);   /* the PERC panel: lane 0..5's steps in bar b */

#endif
