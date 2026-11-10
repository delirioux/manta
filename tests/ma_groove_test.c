/* SPDX-License-Identifier: GPL-3.0-only */
/* MANTA's groove engine on its own (firmware/src/ma_groove.c): every style plays, in time, in key, every note
 * ends; the form goes intro, verse, pre, chorus, break, chorus (the key up) and round again; a song replays exactly;
 * a style and a song come in on the bar line; BREAK, KEY UP and the keys do what the manual says; the choir sings
 * where VOX says, in key and in its range; the settings survive a bad record.
 *   cc -std=c11 -O2 -Ifirmware/src -o build/host/ma_groove_test tests/ma_groove_test.c firmware/src/ma_groove.c -lm */
#include <stdio.h>
#include <string.h>
#include "ma_groove.h"

static int fails, checks;
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            fails++;                                                       \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
        }                                                                  \
    } while (0)

#define SR 44100.0f
static ma_groove g;
static int on[MA_PARTS], off[MA_PARTS], sounding[MA_PARTS][128], bad_pc, hook_in_verse, chants, chant_kind[MA_DR_COUNT], bad_choir;
static int drum_on[MA_DR_COUNT];
static int tup_watch, tup_notes, tup_off, kicks, bad_kick;   /* the tuplets' notes: how many, how many off the chord */
static uint32_t hash;
static int stab_tri, stab_not_tri;
static void sound(void *ctx, const ma_event *e)
{
    (void)ctx;
    if (e->on && e->part == MA_STABS && g.nloop) {   /* (the piano test: each stab note a tone of the triad) */
        const ma_chord *c = &g.loop[ma_chord_now(&g)];
        int pc = ((e->note - g.tr) % 12 + 12) % 12;
        if (pc == c->tone[0] || pc == c->tone[1] || pc == c->tone[2]) stab_tri++; else stab_not_tri++;
    }
    hash = (hash ^ (uint32_t)(e->on << 24 | e->part << 16 | e->note << 8 | e->vel)) * 16777619u + g.clock;
    if (e->on) {
        on[e->part]++;
        sounding[e->part][e->note]++;
        if (e->part != MA_DRUMS && e->part != MA_HOOK && ma_snap(&g.s, e->note - g.tr) != e->note - g.tr)
            bad_pc++;                              /* (in the scale but for the dominant's third and the approach) */
        if (e->part == MA_HOOK && g.s.arc && g.sec == MA_SEC_VERSE && !g.keys)
            hook_in_verse++;
        if (e->part == MA_HOOK && tup_watch && g.step >= 0)   /* (ARC off, REPEAT 1: the hook's step is the bar's) */
            for (int k = 0; k <= g.step; ++k)
                if (g.hook_tup[k] && g.step < k + g.hook_len[k]) {
                    const ma_chord *c = &g.loop[ma_chord_now(&g)];
                    int pc = (e->note - g.tr) % 12;
                    tup_notes++;
                    tup_off += pc != c->tone[0] && pc != c->tone[1] && pc != c->tone[2];
                    break;
                }
        if (e->part == MA_DRUMS) {
            drum_on[ma_drum_of(e->note)]++;
            if (ma_drum_of(e->note) == MA_DR_KICK) {
                int pc = MA_KICK_PC[g.s.kick == MA_KICK_ROUND] + ma_kick_shift(e->note);
                kicks++;
                bad_kick += !ma_is_kick_tuned(e->note) ||
                            ((pc % 12 + 12) % 12) != (g.loop[ma_chord_now(&g)].root + g.tr) % 12;
            }
            if (ma_is_choir(e->note)) {
                int p = ma_choir_pitch(e->note);
                chants++;
                chant_kind[ma_drum_of(e->note)]++;
                const ma_chord *c = &g.loop[ma_chord_now(&g)];
                int pc = ((p - g.tr) % 12 + 12) % 12;
                if (p < MA_CHOIR_LO || p > MA_CHOIR_LO + 23 || ma_choir_vowel(e->note) > 1 ||
                    (pc != c->tone[0] && pc != c->tone[1] && pc != c->tone[2]))
                    bad_choir++;              /* (the chord's triad, the key up in it) */
            }
        }
    } else {
        off[e->part]++;
        if (sounding[e->part][e->note] > 0) sounding[e->part][e->note]--;
    }
}
static void reset_counts(void)
{
    memset(on, 0, sizeof on);
    memset(off, 0, sizeof off);
    memset(sounding, 0, sizeof sounding);
    memset(drum_on, 0, sizeof drum_on);
    memset(chant_kind, 0, sizeof chant_kind);
    chants = hook_in_verse = bad_pc = bad_choir = kicks = bad_kick = 0;
}
static int still_sounding(int p) { int n = 0; for (int k = 0; k < 128; ++k) n += sounding[p][k]; return n; }
static void run_bars(int n)
{
    int b = g.bar + n;
    while (g.bar < b) ma_process(&g, 32);
}
static void run_to_sec(int sec)
{
    for (int k = 0; k < 200 && g.sec != sec; ++k) run_bars(1);
}

int main(void)
{
    ma_init(&g, SR);
    ma_set_sound(&g, sound, 0);
    /* every style: a whole round of the form and more, its parts play, nothing hangs, in key */
    for (int st = 0; st < MA_STYLES; ++st) {
        ma_style_apply(&g.s, st);
        ma_rebuild(&g);
        reset_counts();
        ma_play(&g, true);
        run_bars(64);
        ma_play(&g, false);
        for (int p = 0; p < MA_PARTS; ++p) {
            CHECK(on[p] > 0, "%s: part %d plays (%d)", ma_style_name(st), p, on[p]);
            if (p != MA_DRUMS)
                CHECK(!still_sounding(p), "%s: part %d left %d notes sounding", ma_style_name(st), p, still_sounding(p));
        }
        CHECK(bad_pc < on[MA_STABS] / 8 + on[MA_BASS] / 8 + 8, "%s: %d stab and bass notes outside the scale",
              ma_style_name(st), bad_pc);
        if (g.s.speed == MA_SPEED_PAD)
            CHECK(hook_in_verse > 0, "%s: the pad fills the verse (%d notes)", ma_style_name(st), hook_in_verse);
        else
            CHECK(hook_in_verse == 0, "%s: the hook rests in the verse (%d notes)", ma_style_name(st), hook_in_verse);
        CHECK(g.loop[0].name[0], "%s: its first chord has a name (%s)", ma_style_name(st), g.loop[0].name);
        if (g.s.vox != MA_VOX_OFF)
            CHECK(chants > 0, "%s: the choir sings (VOX on)", ma_style_name(st));
        else
            CHECK(chants == 0, "%s: no choir with VOX OFF (%d)", ma_style_name(st), chants);
        CHECK(kicks > 0 && bad_kick == 0, "%s: the kick on the chord's root (%d of %d off)", ma_style_name(st), bad_kick,
              kicks);
        CHECK(bad_choir == 0, "%s: the choir sings the chord, in its range (%d off)", ma_style_name(st), bad_choir);
    }
    /* the form: INTRO 8, VERSE, PRE 4, CHORUS, BREAK 8, CHORUS a key up, round again from the verse */
    ma_style_apply(&g.s, MA_ST_EUROSTOMP);
    g.s.arc = 1;
    g.s.verse = 8;
    g.s.chorus = 8;
    g.s.keyup = 1;
    ma_rebuild(&g);
    ma_play(&g, true);
    CHECK(g.sec == MA_SEC_INTRO, "PLAY starts with the intro");
    static const int ORDER[] = {MA_SEC_VERSE, MA_SEC_PRE, MA_SEC_CHORUS, MA_SEC_BREAK, MA_SEC_CHORUS2, MA_SEC_VERSE};
    static const int AT[] = {8, 16, 20, 28, 36, 44};
    for (int k = 0; k < 6; ++k) {
        while (g.bar < AT[k]) ma_process(&g, 32);
        CHECK(g.sec == ORDER[k], "bar %d: %s (got %s)", AT[k], ma_sec_name(ORDER[k]), ma_sec_name(g.sec));
        if (ORDER[k] == MA_SEC_CHORUS2)
            CHECK(g.tr == 1, "the last chorus a semitone up (%d)", g.tr);
        if (k == 5)
            CHECK(g.tr == 0 && g.round == 1, "round again: home key, round 1 (%d %d)", g.tr, g.round);
    }
    /* in the break the bass rests; its last two bars a roll */
    run_to_sec(MA_SEC_BREAK);
    reset_counts();
    run_bars(5);
    CHECK(on[MA_BASS] == 0 && on[MA_DRUMS] == 0 && on[MA_STABS] > 0, "the break: stabs, no bass, no drums (%d %d %d)",
          on[MA_STABS], on[MA_BASS], on[MA_DRUMS]);
    reset_counts();
    run_bars(3);
    CHECK(drum_on[MA_DR_CLAP] + drum_on[MA_DR_SNARE] > 8 && drum_on[MA_DR_KICK] <= 1,
          "the break's roll: snare or clap, no kick but the chorus's first (%d %d)", drum_on[MA_DR_CLAP] + drum_on[MA_DR_SNARE],
          drum_on[MA_DR_KICK]);
    ma_play(&g, false);
    /* the same song twice: the same notes at the same times */
    uint32_t h1, h2;
    ma_style_apply(&g.s, MA_ST_PARTY);
    g.s.song = 17;
    ma_rebuild(&g);
    hash = 2166136261u; ma_play(&g, true); run_bars(24); ma_play(&g, false); h1 = hash;
    hash = 2166136261u; ma_play(&g, true); run_bars(24); ma_play(&g, false); h2 = hash;
    CHECK(h1 == h2, "a song replays exactly");
    g.s.song = 18;
    ma_rebuild(&g);
    hash = 2166136261u; ma_play(&g, true); run_bars(24); ma_play(&g, false);
    CHECK(hash != h1, "another song differs");
    /* the tempo: 16 bars at 120 BPM are 32 s */
    ma_style_apply(&g.s, MA_ST_EUROSTOMP);
    g.s.bpm = 120;
    g.s.arc = 0;
    ma_rebuild(&g);
    ma_play(&g, true);
    run_bars(16);
    float secs = (float)g.clock / SR;
    CHECK(secs > 31.9f && secs < 32.1f, "16 bars at 120 BPM: %.3f s", (double)secs);
    CHECK(ma_in_chorus(&g), "ARC off: the chorus on and on");
    /* a style queued: on the next bar line, not before */
    int bar = g.bar;
    while (g.step != 5) ma_process(&g, 32);
    ma_queue_style(&g, MA_ST_RAVE);
    CHECK(g.s.style == MA_ST_EUROSTOMP, "queued: still EUROSTOMP mid-bar");
    while (g.bar == bar) ma_process(&g, 32);
    CHECK(g.s.style == MA_ST_RAVE && g.step == 0, "RAVE from the bar line (step %d)", g.step);
    /* a song queued: the same */
    int song = g.s.song;
    ma_queue_song(&g, song + 3);
    CHECK(g.s.song == song, "the song waits for the bar");
    run_bars(1);
    CHECK(g.s.song == song + 3, "the next song from the bar line");
    /* KEY UP: from the next bar, +1 +2 +3, then home */
    while (g.step != 3) ma_process(&g, 32);
    ma_keyup(&g);
    CHECK(g.tr == 0, "KEY UP waits for the bar");
    run_bars(1);
    CHECK(g.tr == 1, "KEY UP: +1 from the bar (%d)", g.tr);
    reset_counts();
    run_bars(2);
    CHECK(bad_pc < on[MA_STABS] / 8 + on[MA_BASS] / 8 + 4, "a key up: the stabs and bass in the new key (%d out)", bad_pc);
    ma_keyup(&g); run_bars(1);
    ma_keyup(&g); run_bars(1);
    CHECK(g.tr == 3, "+3 (%d)", g.tr);
    ma_keyup(&g); run_bars(1);
    CHECK(g.tr == 0, "after +3: home (%d)", g.tr);
    /* BREAK: three bars of stabs, the roll in the fourth, back on the bar */
    while (g.step != 0) ma_process(&g, 32);
    reset_counts();
    ma_break(&g, true);
    ma_break(&g, false);
    int brk_bar = g.bar;
    run_bars(1);
    CHECK(g.brk && on[MA_BASS] == 0 && on[MA_HOOK] == 0 && on[MA_DRUMS] == 0, "BREAK: no bass, hook, drums (%d %d %d)",
          on[MA_BASS], on[MA_HOOK], on[MA_DRUMS]);
    run_bars(2);
    CHECK(on[MA_STABS] > 0, "the stabs play on (%d)", on[MA_STABS]);
    reset_counts();
    while (g.bar < brk_bar + 3) ma_process(&g, 32);
    run_bars(1);
    CHECK(drum_on[MA_DR_CLAP] + drum_on[MA_DR_SNARE] > 6, "the roll in its last bar (%d)",
          drum_on[MA_DR_CLAP] + drum_on[MA_DR_SNARE]);
    CHECK(!g.brk && g.bar == brk_bar + 4, "back on the bar line four bars on (%d)", g.bar - brk_bar);
    /* held: stays until let go, then the roll, then back */
    ma_break(&g, true);
    run_bars(5);
    CHECK(g.brk, "held: still in the break after five bars");
    ma_break(&g, false);
    run_bars(3);
    CHECK(!g.brk, "let go: the roll, then back");
    /* the keys: snapped to the scale (the key up in it), the hook makes way */
    reset_counts();
    ma_key(&g, 1, 54, true);
    CHECK(on[MA_HOOK] == 1, "a key plays the hook");
    ma_key(&g, 1, 54, false);
    {   /* SNAP: OFF (the default) the key as pressed, ON onto the scale */
        int off_out = 0, on_out = 0;
        for (int n = 53; n <= 79; ++n) {
            ma_key(&g, n - 53, n, true);
            off_out += g.key_note[n - 53] != n;
            ma_key(&g, n - 53, n, false);
        }
        g.s.keysnap = 1;
        for (int n = 53; n <= 79; ++n) {
            ma_key(&g, n - 53, n, true);
            on_out += ma_snap(&g.s, g.key_note[n - 53] - g.tr) != g.key_note[n - 53] - g.tr;
            ma_key(&g, n - 53, n, false);
        }
        g.s.keysnap = 0;
        CHECK(off_out == 0 && on_out == 0, "SNAP: OFF as pressed (%d off), ON in the scale (%d out)", off_out, on_out);
    }
    CHECK(off[MA_HOOK] >= 1, "its release lets go");
    ma_play(&g, false);
    /* the choir: none with VOX OFF; the vowel chosen with VOX on, MIX both */
    ma_style_apply(&g.s, MA_ST_EUROSTOMP);
    g.s.arc = 0;
    g.s.vox = MA_VOX_OFF;
    ma_rebuild(&g);
    reset_counts();
    ma_play(&g, true); run_bars(8); ma_play(&g, false);
    CHECK(chants == 0, "VOX OFF: silence from the choir (%d)", chants);
    g.s.vox = MA_VOX_CHORUS;
    g.s.choir = MA_CHOIR_AH;
    reset_counts();
    ma_play(&g, true); run_bars(8); ma_play(&g, false);
    CHECK(chant_kind[MA_DR_AH] >= 8 * 2 && chant_kind[MA_DR_OH] == 0, "CHOIR AH: a chord of AH a bar (%d), no OH (%d)",
          chant_kind[MA_DR_AH], chant_kind[MA_DR_OH]);
    g.s.choir = MA_CHOIR_MIX;
    reset_counts();
    ma_play(&g, true); run_bars(8); ma_play(&g, false);
    CHECK(chant_kind[MA_DR_AH] > 0 && chant_kind[MA_DR_OH] > 0, "CHOIR MIX: OH (%d) and AH (%d)", chant_kind[MA_DR_OH],
          chant_kind[MA_DR_AH]);
    /* PIANO plays triads, even over a loop of sevenths (PIANOHOUSE's) */
    ma_style_apply(&g.s, MA_ST_PIANOHOUSE);
    g.s.arc = 1;                                   /* (the whole form: the held chords of the pre and the break too) */
    ma_rebuild(&g);
    stab_tri = stab_not_tri = 0;
    ma_play(&g, true); run_bars(40); ma_play(&g, false);
    CHECK(stab_tri > 50 && stab_not_tri == 0, "PIANO: triads only (%d triad tones, %d others)", stab_tri, stab_not_tri);
    /* PIANO on the STABS: the offbeat chords, in any style (RAVE's own stabs are not offbeat) */
    ma_style_apply(&g.s, MA_ST_RAVE);
    g.s.arc = 0;
    g.s.snd[MA_STABS] = MA_STABS_PIANO;
    ma_rebuild(&g);
    ma_play(&g, true); run_bars(5);
    {
        int off = 0, on_beat = 0;
        for (int k = 0; k < 16; ++k)
            if ((g.hits_last[MA_STABS] >> k) & 1u) { if (k % 4 == 2) off++; else if (k % 4 == 0) on_beat++; }
        CHECK(off >= 2 && on_beat == 0, "PIANO: the chords on the offbeats (%d off, %d on the beat)", off, on_beat);
    }
    ma_play(&g, false);
    /* each song its own sounds: song 1 the style's, the others from its pool of four a part */
    for (int st = 0; st < MA_STYLES; ++st) {
        ma_settings t;
        ma_defaults(&t);
        ma_style_apply(&t, st);
        ma_song_sounds(&t);
        CHECK(t.snd[0] == MA_STYLE[st].snd[0] && t.snd[1] == MA_STYLE[st].snd[1] && t.snd[2] == MA_STYLE[st].snd[2],
              "%s: song 1 plays the style's own sounds", ma_style_name(st));
        int seen[3][MA_SOUNDS] = {{0}}, kinds = 0;
        for (int song = 0; song < 40; ++song) {
            t.song = (uint16_t)song;
            ma_song_sounds(&t);
            for (int p = 0; p < 3; ++p) {
                CHECK(t.snd[p] < MA_SOUNDS, "%s: a sound in the list", ma_style_name(st));
                if (!seen[p][t.snd[p]]++) kinds++;
            }
        }
        CHECK(kinds >= 6, "%s: the songs vary their sounds (%d different over 40 songs)", ma_style_name(st), kinds);
    }
    /* ALGORITHM: the next song brings its sounds on the bar line, and reloads them */
    ma_style_apply(&g.s, MA_ST_EUROSTOMP);
    g.s.arc = 0;
    ma_rebuild(&g);
    ma_play(&g, true); run_bars(1);
    {
        uint8_t sig = g.sound_sig;
        int target = -1;
        for (int song = 1; song < 50 && target < 0; ++song) {
            ma_settings t = g.s;
            t.song = (uint16_t)song;
            ma_song_sounds(&t);
            if (t.snd[0] != g.s.snd[0]) target = song;
        }
        ma_queue_song(&g, target);
        run_bars(1);
        CHECK(g.s.song == target && g.s.snd[0] != MA_STYLE[MA_ST_EUROSTOMP].snd[0] && g.sound_sig != sig,
              "song %d: other stabs (%d), reloaded", target + 1, g.s.snd[0]);
    }
    ma_play(&g, false);
    /* tuplets: the songs' hooks hold triplets, quadruplets, quintuplets and sextuplets, played evenly */
    {
        int kinds[7] = {0}, with = 0;
        ma_style_apply(&g.s, MA_ST_EUROSTOMP);
        for (int song = 0; song < 120; ++song) {
            g.s.song = (uint16_t)song;
            ma_rebuild(&g);
            int any = 0;
            for (int k = 0; k < MA_HOOK_LEN; ++k)
                if (g.hook_tup[k]) {
                    kinds[g.hook_tup[k]]++;
                    any = 1;
                    CHECK(g.hook_len[k] == ma_tuplet_span(g.hook_tup[k]), "a tuplet of %d over %d sixteenths (%d)",
                          g.hook_tup[k], ma_tuplet_span(g.hook_tup[k]), g.hook_len[k]);
                }
            with += any;
        }
        CHECK(kinds[3] && kinds[4] && kinds[5] && kinds[6], "all four tuplets turn up (3:%d 4:%d 5:%d 6:%d)", kinds[3],
              kinds[4], kinds[5], kinds[6]);
        CHECK(with > 20 && with < 100, "some songs have one, not every song (%d of 120)", with);
        /* played: every tuplet note is a tone of the chord playing (an arpeggio of it) */
        g.s.arc = 0;
        g.s.repeat = 1;
        tup_watch = 1;
        for (int song = 0; song < 40; ++song) {
            g.s.song = (uint16_t)song;
            ma_rebuild(&g);
            ma_play(&g, true); run_bars(4); ma_play(&g, false);
        }
        tup_watch = 0;
        CHECK(tup_notes > 20 && tup_off == 0, "the tuplets arpeggiate the chord (%d notes, %d off it)", tup_notes, tup_off);
    }
    /* the hook's phrase: REPEAT bars of it */
    g.s.repeat = 4;
    ma_rebuild(&g);
    int last = -1;
    for (int k = 0; k < MA_HOOK_LEN; ++k) if (g.hook[k] != -128) last = k;
    CHECK(last >= 48, "REPEAT 4: the hook runs four bars (its last note at %d)", last);
    g.s.repeat = 1;
    ma_rebuild(&g);
    last = -1;
    for (int k = 0; k < MA_HOOK_LEN; ++k) if (g.hook[k] != -128) last = k;
    CHECK(last < 16, "REPEAT 1: one bar (%d)", last);
    /* a bad record: every field back in range */
    ma_settings s;
    memset(&s, 0xEE, sizeof s);
    ma_settings_fix(&s);
    CHECK(s.style < MA_STYLES && s.lift <= 100 && s.bars == 4 && s.scale < MA_SCALES && s.bpm >= 60 && s.bpm <= 200 &&
          s.repeat == 4 && s.keyup <= 2 && s.vox < MA_VOX_COUNT && s.bassline < MA_BL_COUNT, "a bad record fixed");
    /* the scale snap: A MINOR has no G# */
    ma_style_apply(&s, MA_ST_EUROSTOMP);
    CHECK(ma_snap(&s, 68) == 67 || ma_snap(&s, 68) == 69, "G# onto A minor: %d", ma_snap(&s, 68));
    printf("ma_groove: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
