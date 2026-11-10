/* SPDX-License-Identifier: GPL-3.0-only */
/* MANTA groove engine: ten styles of generative 90s eurodance on four parts (STABS, BASS, HOOK, DRUMS), with a choir
 * that sings along (OH, AH: the drum kit's sung chords). It runs in time, sixteenth by sixteenth, and hands each note-on
 * and note-off to the sound (Felucca's tracks on the FM-1, a renderer on the host). Besides the notes it computes,
 * every block, what the glue turns into sound:
 *
 *   the LIFT    how far the filter of the lifted parts is open (0..127 per part): the LIFT knob is its ceiling; the
 *               intro and the pre-chorus open it up to there, the break holds it lower
 *   the PUMP    how far each part ducks under the kick (0..127): a fast duck on every beat, a smooth return over
 *               most of the beat; the drums themselves never duck
 *
 * A song is a style plus a number (the SONG, turned by ALGORITHM): the number picks the chord loop, the parts'
 * patterns and the hook, so the same style and number always play the same song. A new style or song comes in on
 * the next bar line; the knobs act at once.
 *
 * The song's form (ARC on): INTRO (the parts in one by one), VERSE (no hook: the rap's room), PRE (held chords, a
 * snare roll, the filter lifting), CHORUS (everything, the hook, the choir), BREAK (the stabs and the hook alone, a
 * roll back in), CHORUS again (KEY UP: a semitone or two higher), then round again from the verse with a new hook.
 * ARC off: the chorus on and on.
 *
 * Performance: BREAK (REC: everything but the stabs out for three bars, a snare roll in the fourth, the chorus on
 * the bar line; held: until let go, then the roll), KEY UP (SEQ: the song a semitone up on the next bar, up to +3,
 * then home), the keys (the hook live, snapped to the scale).
 *
 * No libm, no doubles: floats on the FPU (single precision), as Daurade, Flyfish and Tuna. */
#ifndef MA_GROOVE_H
#define MA_GROOVE_H

#include <stdbool.h>
#include <stdint.h>

enum { MA_STABS, MA_BASS, MA_HOOK, MA_DRUMS, MA_PARTS };
enum { MA_SIDE_CLUB, MA_SIDE_RADIO };
#define MA_STYLES 10
enum { MA_ST_EUROSTOMP, MA_ST_VIKING, MA_ST_BUBBLEGUM, MA_ST_HEARTACHE, MA_ST_RAVE,
       MA_ST_SKANK, MA_ST_PIANOHOUSE, MA_ST_PARTY, MA_ST_BALLAD, MA_ST_ITALO };
enum { MA_SC_MINOR, MA_SC_MAJOR, MA_SC_DORIAN, MA_SC_HARMONIC, MA_SCALES };
enum { MA_HATS_8THS, MA_HATS_16THS, MA_HATS_OFFBEAT, MA_HATS_OPEN, MA_HATS_COUNT };
enum { MA_FILLS_OFF, MA_FILLS_8, MA_FILLS_4, MA_FILLS_2, MA_FILLS_COUNT };
enum { MA_BL_OFFBEAT, MA_BL_OCTAVE, MA_BL_ROLLING, MA_BL_REGGAE, MA_BL_LONG, MA_BL_BOUNCE, MA_BL_COUNT };
enum { MA_KIT_EURO, MA_KIT_SESSION, MA_KIT_MACHINE, MA_KIT_COUNT };
enum { MA_KICK_PUNCH, MA_KICK_ROUND, MA_KICK_COUNT };
enum { MA_CLAP_CLAP, MA_CLAP_SNARE, MA_CLAP_BOTH, MA_CLAP_COUNT };
enum { MA_CRASH_OFF, MA_CRASH_SECTION, MA_CRASH_8, MA_CRASH_COUNT };
enum { MA_VOX_OFF, MA_VOX_CHORUS, MA_VOX_ALL, MA_VOX_COUNT };
enum { MA_CHOIR_OH, MA_CHOIR_AH, MA_CHOIR_MIX, MA_CHOIR_COUNT };
enum { MA_VOWELS_OO_AH, MA_VOWELS_AH_EE, MA_VOWELS_OH_AH, MA_VOWELS_OO_EE, MA_VOWELS_AH_OH, MA_VOWELS_COUNT };
enum { MA_DTIME_16, MA_DTIME_8, MA_DTIME_3_16, MA_DTIME_4, MA_DTIME_COUNT };
enum { MA_SPEED_SLOW, MA_SPEED_MID, MA_SPEED_FAST, MA_SPEED_PAD, MA_SPEED_COUNT };   /* PAD: no tune, the chords held */
/* the drums as the groove names them, the choir's two vowels among them; the glue maps them to the kit (ma_sounds.h) */
enum { MA_DR_KICK, MA_DR_SNARE, MA_DR_CLAP, MA_DR_HAT, MA_DR_OPEN, MA_DR_CRASH, MA_DR_OH, MA_DR_AH, MA_DR_COUNT };
/* a choir note on the DRUMS part: its note is MA_CHOIR_NOTE(vowel, pitch), the pitch MA_CHOIR_LO .. MA_CHOIR_LO + 23
 * (the key up in it); a drum's note is its MA_DR_* (below 32; bit 7: the open hat's choke) */
#define MA_CHOIR_LO 48
#define MA_CHOIR_NOTE(v, p) (32 + 32 * (v) + (p) - MA_CHOIR_LO)
static inline bool ma_is_choir(int n) { return n >= 32 && n < 96; }
static inline int ma_choir_vowel(int n) { return (n - 32) / 32; }           /* 0 OH, 1 AH */
static inline int ma_choir_pitch(int n) { return MA_CHOIR_LO + (n - 32) % 32; }
/* a kick tuned to the chord's root: MA_KICK_NOTE(shift), shift -6..+5 semitones from the kick's own pitch (the
 * sample's: PUNCH about E1, ROUND about D1, MA_KICK_PC) */
#define MA_KICK_NOTE(sh) (96 + 6 + (sh))
static inline bool ma_is_kick_tuned(int n) { return n >= 96 && n < 108; }
static inline int ma_kick_shift(int n) { return n - 96 - 6; }
static const uint8_t MA_KICK_PC[2] = {4, 2};        /* PUNCH, ROUND: their pitch classes (tools/ma_drums.py: 41, 36 Hz) */
static inline int ma_drum_of(int n)
{
    return ma_is_choir(n) ? MA_DR_OH + ma_choir_vowel(n) : ma_is_kick_tuned(n) ? MA_DR_KICK : n % MA_DR_COUNT;
}
/* the tuplets the hook and the fills play, as (notes, in the space of sixteenths): 3 in 4 (a triplet: three in the
 * space of two eighths), 4 in 6 (a quadruplet: four in the space of three eighths), 5 in 4 (a quintuplet: five in
 * the space of four sixteenths), 6 in 4 (a sextuplet: six in the space of four sixteenths) */
static inline int ma_tuplet_span(int n) { return n == 4 ? 6 : 4; }
/* the form's sections, in the order they play (ARC on); after the second CHORUS, round again from VERSE */
enum { MA_SEC_INTRO, MA_SEC_VERSE, MA_SEC_PRE, MA_SEC_CHORUS, MA_SEC_BREAK, MA_SEC_CHORUS2, MA_SECS };
#define MA_SETS 8
#define MA_SOUNDS 16                               /* each part's SOUND list (ma_sounds.h) */
#define MA_STABS_PIANO 0                           /* STABS sound 0, PIANO: the offbeat piano chords, any style */
#define MA_HOOK_PAD 9                              /* HOOK sound 9, PAD: a warm pad (HOOK SPEED PAD plays it held) */
#define MA_LOOPS 12                                /* the chord loops (ma_groove.c MA_LOOP) */
#define MA_KEYUP_MAX 3                             /* SEQ: up to +3, then home */

/* what the player set: the cards (and the knobs above them). Saved as it is (ma_fw.c), so append only */
typedef struct {
    uint8_t style;
    uint8_t lift, pump, vox, energy;               /* PLAY: LIFT 0..100, PUMP 0..100, VOX (MA_VOX_*), ENERGY 0..100 */
    uint8_t snd[4];                                /* SOUND: STABS BASS HOOK (indices into each part's list), DRUMS = kit */
    uint8_t reso, drive, ensemble, crush;          /* TONE 0..100 (CRUSH 0: off) */
    uint8_t vowels, choir, glide;                  /* VOX: the vowels the OOH sings through, the choir's vowel, glide */
    int8_t shift;                                  /* VOX: formant shift -12..12 */
    uint8_t key, scale, chords, bars;              /* KEY: 0..11 (C..B), MA_SC_*, the loop (0..MA_LOOPS-1), 1 2 4 */
    uint8_t swing, human, hats, fills;             /* GROOVE: swing 0..60 %, human 0..100 (no card), MA_HATS_*, MA_FILLS_* */
    uint8_t bassline;                              /* GROOVE: MA_BL_* */
    uint8_t kick, clap, snap, crash;               /* DRUMS: MA_KICK_*, MA_CLAP_*, SNAP 0..100 (no card), MA_CRASH_* */
    uint8_t lvl[4];                                /* MIX 0..100 */
    uint8_t reverb, delay, dtime, width;           /* SPACE */
    uint8_t arc, verse, chorus, keyup;             /* ARC: on, verse bars 8 16, chorus bars 8 16, the last chorus up 0..2 */
    uint8_t speed, range, gate, repeat;            /* HOOK: MA_SPEED_* (PAD: a pad on the chords), octaves 1 2, gate
                                                    * 10..100 %, bars 1 2 4 */
    uint8_t bpm;                                   /* SELECT: 60..200 */
    uint8_t palette, look;                         /* MENU: COLOR, LOOK (FLAT, LINE) */
    uint8_t keysnap;                               /* MENU: SNAP, the keys onto the scale (1) or as pressed (0, the
                                                    * default); in the byte that was padding, so the size and the
                                                    * saved sets stay as they were */
    uint16_t song;                                 /* ALGORITHM: the song number in the style, 0..999 */
} ma_settings;

typedef struct {
    uint8_t on, part, note, vel;                   /* DRUMS: note is MA_DR_*; vel 1..127 (on) */
} ma_event;

/* what the screen hears: each note-on */
typedef struct { uint8_t part, note, vel, pad; } ma_seen;

/* a chord as played: its root (pitch class), the notes (MIDI) it voices, its name (with the key up in it) */
typedef struct {
    uint8_t root, n;
    uint8_t note[5];                               /* stab voicing, low to high */
    uint8_t tone[4];                               /* chord tones as pitch classes: root, third, fifth, seventh */
    char name[8];
} ma_chord;

#define MA_HELD 24
#define MA_FEED 32
#define MA_LOOP_MAX 8
#define MA_HOOK_LEN 64                             /* the hook: up to four bars of sixteenths (REPEAT) */

typedef struct {
    ma_settings s;
    float sr;
    bool playing;
    uint32_t clock;                                /* frames since PLAY */
    float wait;                                    /* frames until the next sixteenth */
    int step;                                      /* the sixteenth sounding in its bar (0..15; -1 before the first) */
    int bar;                                       /* bars since PLAY (0: the first) */
    float beat_ph;                                 /* 0..1 through the beat (the pump) */
    /* the song: built from the style, the song number, the key and the loop (ma_rebuild) */
    ma_chord loop[MA_LOOP_MAX];
    int nloop;
    uint8_t stab_pat, bass_pat, drum_var, hook_var;
    int8_t hook[MA_HOOK_LEN];                      /* scale steps from the key's tonic (-128: rest); length in hook_len */
    uint8_t hook_len[MA_HOOK_LEN];
    uint8_t hook_tup[MA_HOOK_LEN];                 /* a tuplet starting there: its notes (3 4 5 6; 0 none), over hook_len */
    uint32_t rnd;                                  /* the per-note randomness (human, ghosts): seeded per bar */
    /* what waits for the next bar line */
    int next_style;                                /* -1 none */
    int next_song;                                 /* -1 none */
    /* the key up: the song's notes this many semitones up (the arc's last chorus plus SEQ's) */
    int tr;                                        /* sounding now */
    int perf_up;                                   /* SEQ's share, 0..MA_KEYUP_MAX */
    int perf_next;                                 /* SEQ pressed: its share from the next bar (-1 none) */
    /* performance */
    bool brk;                                      /* BREAK: everything but the stabs (and the roll) out */
    int brk_end;                                   /* the bar it comes back on */
    bool brk_held;
    bool crash_next;                               /* a crash on the next downbeat */
    uint32_t keys;                                 /* the FM-1's keys held (bit note - 53) */
    int key_note[27];                              /* the note each key plays (snapped, the key up in it), -1 none */
    /* the form */
    int sec, sec_bar;                              /* the section (MA_SEC_*), bars into it */
    int round;
    /* the block's controls for the glue */
    uint8_t cut[MA_PARTS];                         /* 0..127: how far each part's filter is open */
    uint8_t duck[MA_PARTS];                        /* 0..127: how far each part is ducked now */
    float lift_now;                                /* 0..1: the filter for the screen */
    bool muted[MA_PARTS];                          /* out (BREAK, the form, a level of 0) */
    /* what each part played, a bit per sixteenth: the bar playing and the bar before (the screen) */
    uint16_t hits[MA_PARTS], hits_last[MA_PARTS];
    uint16_t drum_hits[MA_DR_COUNT], drum_last[MA_DR_COUNT];
    uint32_t hook_on_clock;                        /* the hook's latest note-on (the VOX page's vowel) */
    uint32_t choir_clock;                          /* the choir's latest chord (the VOX page) */
    uint8_t choir_last;                            /* which vowel (MA_DR_OH, MA_DR_AH) */
    /* the notes sounding: their note-off at clock off */
    struct { uint8_t note, part; uint32_t off; } held[MA_HELD];
    int nheld;
    ma_seen feed[MA_FEED];
    uint8_t feed_w, feed_r;
    char msg[24];                                  /* a word for the screen ("NEXT PARTY", "KEY UP +1"); the UI clears it */
    uint8_t msg_new;
    uint8_t sound_sig;                             /* bumped when every part's sound must load again (a style, a set) */
    void (*sound)(void *ctx, const ma_event *e);
    void *sound_ctx;
} ma_groove;

typedef struct {
    const char *name;
    uint8_t side, bpm, key, scale;
    uint8_t lift, pump, vox, energy;
    uint8_t snd[4];                                /* STABS BASS HOOK sound, the kit */
    uint8_t loops[4];                              /* the chord loops a song picks from (MA_LOOP indices) */
    uint8_t bars;                                  /* bars a chord */
    uint8_t swing, hats, fills, bassline;
    uint8_t kick, clap, crash, choir;
    uint8_t stab, hook, drums;                     /* the rhythm families (ma_groove.c) */
    uint8_t speed, range, gate, repeat;            /* the HOOK page */
    uint8_t lift_parts;                            /* bit per part: what the LIFT filters */
    uint8_t reverb, delay, dtime;
    uint8_t reso, drive, ensemble;
    uint8_t alt[3][3];                             /* STABS BASS HOOK: three more sounds each a song can pick (ma_song_sounds) */
} ma_style;

extern const ma_style MA_STYLE[MA_STYLES];

void ma_defaults(ma_settings *s);                  /* EUROSTOMP, as it starts */
void ma_style_apply(ma_settings *s, int style);    /* a style's own settings into s (PLAY SOUND TONE VOX KEY GROOVE DRUMS
                                                    * SPACE HOOK and the tempo); MIX ARC MENU stay */
void ma_settings_fix(ma_settings *s);              /* every field into its range (a loaded record) */
void ma_song_sounds(ma_settings *s);               /* the song's sounds: song 1 (0) the style's own, the others one of four
                                                    * per part, seeded by the song (the kit stays the style's) */
void ma_init(ma_groove *g, float sample_rate);
void ma_set_sound(ma_groove *g, void (*fn)(void *ctx, const ma_event *e), void *ctx);

void ma_play(ma_groove *g, bool on);
void ma_rebuild(ma_groove *g);                     /* the song again from the settings (KEY, the loop, the hook) */
void ma_queue_style(ma_groove *g, int style);      /* playing: on the next bar line; stopped: at once */
void ma_queue_song(ma_groove *g, int song);
void ma_break(ma_groove *g, bool down);            /* REC: down starts it, up lets it come back */
void ma_keyup(ma_groove *g);                       /* SEQ: a semitone up from the next bar (after +3: home) */
void ma_key(ma_groove *g, int key, int note, bool down);   /* the FM-1's key (0..26) playing note: the hook, snapped */
void ma_process(ma_groove *g, int frames);         /* the audio: notes in time, the lift and the pump */
void ma_all_off(ma_groove *g);

bool ma_feed_pop(ma_groove *g, ma_seen *n);
int ma_chord_now(const ma_groove *g);              /* the loop's chord sounding (0..nloop-1) */
int ma_bars_per_chord(const ma_settings *s);
int ma_sec_len(const ma_groove *g, int sec);       /* a section's bars */
int ma_repeat_bars(const ma_settings *s);          /* the hook's phrase: 1 2 4 bars */
bool ma_in_chorus(const ma_groove *g);             /* a chorus playing (the form's, or ARC off) */
const char *ma_style_name(int i);
const char *ma_scale_name(int i);
const char *ma_note_name(int pc);
const char *ma_sec_name(int sec);                  /* "INTRO" .. */
const char *ma_sec_short(int sec);                 /* "IN" "VRS" .. */
void ma_loop_name(const ma_settings *s, int loop, char *out, int max);   /* "LOOP 4" */
int ma_snap(const ma_settings *s, int note);       /* a note onto the scale (the nearest step, ties down) */

#endif
