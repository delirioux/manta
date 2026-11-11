/* SPDX-License-Identifier: GPL-3.0-only */
/* MANTA's groove engine on its own (firmware/src/ma_groove.c): every style plays every part, in time, every note
 * ends; the piano and the bass in key; the clave on its two bars, 3-2 and 2-3; the form goes IN VRS MON MBO MON MOÑ FIN
 * and round again (and without the mambo); a song replays exactly; a style and a song come in on the bar line; BAND
 * HIT, the descarga, MAMBO and the keys do what the manual says; the solo's tone, gain and place; the settings survive
 * a bad record.
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
static int on[MA_PARTS], off[MA_PARTS], sounding[MA_PARTS][128], bad_pc, drum_on[MA_DR_COUNT];
static int clave_at[2][16];                       /* the claves' strokes by (bar of the clave, step) */
static int horns_low, horns_in_desc, others_in_cierre, horn_range_bad, watch_bar = -1, watch_piano;
static uint32_t hash;
static void sound(void *ctx, const ma_event *e)
{
    (void)ctx;
    hash = (hash ^ (uint32_t)(e->on << 24 | e->part << 16 | e->note << 8 | e->vel)) * 16777619u + g.clock;
    if (e->on) {
        on[e->part]++;
        if (e->part == MA_PIANO && g.bar == watch_bar) watch_piano++;
        sounding[e->part][e->note]++;
        if ((e->part == MA_PIANO || e->part == MA_BASS || e->part == MA_KEYS) && !g.keys) {
            /* in the scale, but for the dominant's third (a major one in minor) and the bass's approach */
            int n = e->note, ok = ma_snap(&g.s, n) == n;
            for (int k = 0; k < g.nloop && !ok; ++k)
                for (int t = 0; t < 4; ++t)
                    ok |= n % 12 == g.loop[k].tone[t] || (e->part == MA_BASS && (n + 1) % 12 == g.loop[k].root);
            bad_pc += !ok;
        }
        if (e->part == MA_PERC) {
            drum_on[e->note]++;
            if (e->note == MA_DR_CLAVE && g.step >= 0)
                clave_at[((g.bar + (g.s.clave == MA_CLAVE_23)) & 1)][g.step]++;
        }
        if (e->part == MA_HORNS && g.desc)
            horns_in_desc++;
        if (e->part == MA_HORNS && e->note < 64)
            horns_low++;
        if (e->part == MA_HORNS && !g.keys && (e->note < 52 || e->note > 96))
            horn_range_bad++;
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
    memset(clave_at, 0, sizeof clave_at);
    bad_pc = horns_low = horns_in_desc = others_in_cierre = horn_range_bad = 0;
}
static void run_bars(int bars)
{
    float bar_frames = SR * 60.0f / (float)g.s.bpm * 4.0f;
    int frames = (int)(bar_frames * (float)bars) + 64;
    while (frames > 0) {
        int n = frames > 32 ? 32 : frames;
        ma_process(&g, n);
        frames -= n;
    }
}
/* run until the bar count reaches b (just past its line) */
static void run_to_bar(int b)
{
    int guard = 0;
    while (g.bar < b && guard++ < 2000000)
        ma_process(&g, 32);
}
static int still_sounding(int part)
{
    int n = 0;
    for (int k = 0; k < 128; ++k)
        n += sounding[part][k];
    return n;
}

int main(void)
{
    ma_init(&g, SR);
    ma_set_sound(&g, sound, 0);

    /* every style: every part plays, notes end, in key */
    for (int st = 0; st < MA_STYLES; ++st) {
        ma_defaults(&g.s);
        ma_style_apply(&g.s, st);
        g.s.arc = 0;
        g.s.heat = 80;
        ma_rebuild(&g);
        reset_counts();
        ma_play(&g, true);
        run_bars(16);
        ma_play(&g, false);
        CHECK(on[MA_KEYS] == 0, "%s: no upright beside a guitar that is not there", ma_style_name(st));
        for (int p = 0; p < MA_TRACKS; ++p) {
            CHECK(on[p] > 0, "%s: part %d plays", ma_style_name(st), p);
            CHECK(still_sounding(p) == 0, "%s: part %d's notes end (%d left)", ma_style_name(st), p, still_sounding(p));
        }
        CHECK(bad_pc == 0, "%s: piano and bass in key (%d off)", ma_style_name(st), bad_pc);
        CHECK(horn_range_bad == 0, "%s: the horns in their range (%d out)", ma_style_name(st), horn_range_bad);
    }

    /* the clave: SALSA's sticks on 1, 2-and, 4 | 2, 3 of the 3-2 pattern; 2-3 starts on the two side */
    for (int cl = 0; cl < 2; ++cl) {
        ma_defaults(&g.s);
        g.s.arc = 0;
        g.s.clave = (uint8_t)cl;
        g.s.swing = 0;
        g.s.human = 0;
        ma_rebuild(&g);
        reset_counts();
        ma_play(&g, true);
        run_bars(8);
        ma_play(&g, false);
        static const int THREE[3] = {0, 6, 12}, TWO[2] = {4, 8};
        int ok = 1;
        for (int k = 0; k < 3; ++k) ok &= clave_at[0][THREE[k]] > 0;
        for (int k = 0; k < 2; ++k) ok &= clave_at[1][TWO[k]] > 0;
        int stray = 0;
        for (int b = 0; b < 2; ++b)
            for (int i = 0; i < 16; ++i)
                stray += clave_at[b][i] > 0 && !(b == 0 ? (i == 0 || i == 6 || i == 12) : (i == 4 || i == 8));
        CHECK(ok && !stray, "clave %s: the son clave, its sides in order", cl ? "2-3" : "3-2");
        CHECK(ma_clave_side(&g, 0) == (cl ? 2 : 3), "clave %s: bar 0 is the %s side", cl ? "2-3" : "3-2", cl ? "two" : "three");
    }

    /* the form: IN TEMA VERSO CORTE FRASE PIANO MON MBO MOÑA FIN and round again from the tema; without the mambo
     * ... MON FIN */
    {
        ma_defaults(&g.s);
        g.s.verse = 8;
        g.s.mbars = 8;
        ma_rebuild(&g);
        ma_play(&g, true);
        static const int ORDER[] = {MA_SEC_IN, MA_SEC_TEMA, MA_SEC_VRS, MA_SEC_CORTE, MA_SEC_FRASE, MA_SEC_PNO,
                                    MA_SEC_MON, MA_SEC_MBO, MA_SEC_MONA, MA_SEC_FIN, MA_SEC_TEMA};
        int bar = 0, ok = 1;
        for (unsigned k = 0; k < sizeof ORDER / sizeof ORDER[0]; ++k) {
            run_to_bar(bar);
            ma_process(&g, 64);
            ok &= g.sec == ORDER[k];
            if (g.sec != ORDER[k])
                printf("  bar %d: section %d, wanted %d\n", bar, g.sec, ORDER[k]);
            bar += ma_sec_len(&g, ORDER[k]);
        }
        CHECK(ok, "the form in order, round again");
        ma_play(&g, false);
        g.s.mambo = 0;
        ma_play(&g, true);
        int want[] = {MA_SEC_IN, MA_SEC_TEMA, MA_SEC_VRS, MA_SEC_CORTE, MA_SEC_FRASE, MA_SEC_PNO, MA_SEC_MON, MA_SEC_FIN,
                      MA_SEC_TEMA};
        bar = 0;
        ok = 1;
        for (unsigned k = 0; k < sizeof want / sizeof want[0]; ++k) {
            run_to_bar(bar);
            ma_process(&g, 64);
            ok &= g.sec == want[k];
            bar += ma_sec_len(&g, want[k]);
        }
        CHECK(ok, "MAMBO off: no mambo, no moña");
        ma_play(&g, false);
    }

    /* the new form's turns: the tema ends high with vibrato; the corte is the soloist alone; the piano section is the
     * piano's, no horns; over songs the verso goes now to the piano, now to the horns */
    {
        int piano_vrs = 0, horn_vrs = 0;
        for (int song = 0; song < 8; ++song) {
            ma_defaults(&g.s);
            g.s.verse = 8;
            g.s.song = (uint16_t)song;
            ma_rebuild(&g);
            ma_play(&g, true);
            int tema = ma_sec_len(&g, MA_SEC_IN), vrs = tema + 8, corte = vrs + 8, frase = corte + 1, pno = frase + 4;
            run_to_bar(tema + 7);
            ma_process(&g, (int)(g.sr * 60.0f / (float)g.s.bpm * 4.0f) - 100);
            bool vib = g.vib;
            run_to_bar(vrs);
            ma_process(&g, 64);
            bool vp = g.vrs_piano;
            piano_vrs += vp;
            horn_vrs += !vp;
            int bar_frames = (int)(g.sr * 60.0f / (float)g.s.bpm * 4.0f);
            run_to_bar(corte);
            reset_counts();
            ma_process(&g, bar_frames - 200);       /* (the corte's bar, not the next one's first step) */
            int others = on[MA_BASS] + on[MA_PERC] + (vp ? on[MA_HORNS] : on[MA_PIANO] + on[MA_KEYS]);
            int solo = vp ? on[MA_PIANO] + on[MA_KEYS] : on[MA_HORNS];
            run_to_bar(pno);
            reset_counts();
            for (int b = 0; b < 8; ++b)
                ma_process(&g, b < 7 ? bar_frames : bar_frames - 200);
            if (song == 0) {
                CHECK(vib, "the tema's last note: vibrato");
                CHECK(others == 0 && solo > 0, "the corte: the soloist alone (%d notes, %d others)", solo, others);
                CHECK(on[MA_HORNS] == 0 && on[MA_PIANO] > 30 && on[MA_PERC] > 0 && on[MA_BASS] > 0,
                      "the piano section: the piano's solo, no horns (%d piano)", on[MA_PIANO]);
            }
            ma_play(&g, false);
        }
        CHECK(piano_vrs > 0 && horn_vrs > 0, "the verso: the piano's in %d songs, the horns' in %d", piano_vrs, horn_vrs);
    }

    /* a song replays exactly */
    {
        uint32_t h[2];
        for (int k = 0; k < 2; ++k) {
            ma_defaults(&g.s);
            ma_style_apply(&g.s, MA_ST_MAMBO);
            g.s.song = 17;
            ma_rebuild(&g);
            hash = 2166136261u;
            ma_play(&g, true);
            run_bars(24);
            ma_play(&g, false);
            h[k] = hash;
        }
        CHECK(h[0] == h[1], "a song replays exactly");
    }

    /* a style on the bar line */
    {
        ma_defaults(&g.s);
        g.s.arc = 0;
        ma_rebuild(&g);
        ma_play(&g, true);
        run_bars(2);
        ma_process(&g, 800);
        int b = g.bar;
        ma_queue_style(&g, MA_ST_BOLERO);
        CHECK(g.s.style == MA_ST_SALSA && g.next_style == MA_ST_BOLERO, "a style waits for the bar line");
        run_to_bar(b + 1);
        ma_process(&g, 64);
        CHECK(g.s.style == MA_ST_BOLERO && g.s.bpm == MA_STYLE[MA_ST_BOLERO].bpm, "the style on the bar line, its tempo");
        ma_queue_song(&g, 5);
        run_to_bar(g.bar + 1);
        ma_process(&g, 64);
        CHECK(g.s.song == 5, "a song on the bar line");
        ma_play(&g, false);
    }

    /* BAND HIT tapped: the next bar is a cierre (the band on the clave's figure), the bar after the groove again */
    {
        ma_defaults(&g.s);
        g.s.arc = 0;
        g.s.hits = MA_HITS_OFF;
        g.s.swing = 0;
        g.s.human = 0;
        ma_rebuild(&g);
        ma_play(&g, true);
        run_bars(3);
        ma_process(&g, 300);
        int b = g.bar;
        ma_band_hit(&g, true);
        ma_band_hit(&g, false);
        watch_bar = b + 1;
        watch_piano = 0;
        run_to_bar(b + 2);
        watch_bar = -1;
        CHECK(g.hit_bar == b + 1, "BAND HIT: its cierre the next bar");
        /* the cierre bar: a figure of the band's hits, the piano's chords on them only */
        CHECK(watch_piano >= 3 * 2 && watch_piano <= 3 * 6, "the cierre: the piano on the hits only (%d notes)", watch_piano);
        {   /* tapped again: another figure each time, never the same twice running */
            int f0 = g.hit_fig, same = 0;
            for (int t = 0; t < 8; ++t) {
                ma_band_hit(&g, true);
                ma_band_hit(&g, false);
                run_to_bar(g.bar + 2);
                same += g.hit_fig == f0;
                f0 = g.hit_fig;
            }
            CHECK(!same && f0 != 0, "BAND HIT again: a new figure each tap");
        }
        reset_counts();
        run_to_bar(g.bar + 1);
        CHECK(on[MA_PIANO] > 3 * 3, "after the cierre the groove again (%d piano notes)", on[MA_PIANO]);
        ma_play(&g, false);
    }

    /* the descarga: REC held over a bar line, the percussion alone; let go, a roll, the band back */
    {
        ma_defaults(&g.s);
        g.s.arc = 0;
        g.s.hits = MA_HITS_OFF;
        ma_rebuild(&g);
        ma_play(&g, true);
        run_bars(2);
        ma_process(&g, 300);
        int b = g.bar;
        ma_band_hit(&g, true);
        run_to_bar(b + 1);                         /* the cierre bar */
        g.desc = true;                             /* (held past the cierre: the descarga) */
        reset_counts();
        run_to_bar(b + 3);
        CHECK(on[MA_PIANO] == 0 && on[MA_BASS] == 0 && horns_in_desc == 0 && on[MA_PERC] > 0,
              "the descarga: the percussion alone (piano %d bass %d horns %d perc %d)", on[MA_PIANO], on[MA_BASS],
              horns_in_desc, on[MA_PERC]);
        ma_band_hit(&g, false);
        int let = g.bar;
        run_to_bar(let + 2);
        reset_counts();
        run_to_bar(let + 3);
        CHECK(!g.desc && on[MA_PIANO] > 0 && on[MA_BASS] > 0, "let go: the band back after the roll");
        ma_play(&g, false);
    }

    /* MAMBO (SEQ): ARC on, the mambo from the next bar */
    {
        ma_defaults(&g.s);
        ma_rebuild(&g);
        ma_play(&g, true);
        run_to_bar(10);
        ma_process(&g, 300);
        CHECK(g.sec == MA_SEC_TEMA, "in the tema");
        ma_mambo(&g);
        run_to_bar(11);
        ma_process(&g, 64);
        CHECK(g.sec == MA_SEC_MBO && g.sec_bar == 0, "MAMBO: the mambo on the next bar line");
        ma_play(&g, false);
    }

    /* the keys: the solo, live; its notes end when let go; SNAP onto the scale */
    {
        ma_defaults(&g.s);
        g.s.arc = 0;
        ma_rebuild(&g);
        ma_play(&g, true);
        run_bars(1);
        reset_counts();
        ma_key(&g, 3, 56, true);
        CHECK(sounding[MA_HORNS][56] == 1 && g.soloing, "a key plays the solo as pressed");
        ma_key(&g, 3, 56, false);
        CHECK(sounding[MA_HORNS][56] == 0, "let go: its note ends");
        g.s.keysnap = 1;
        ma_key(&g, 4, 61, true);
        int n = g.key_note[4];
        CHECK(ma_snap(&g.s, n) == n, "SNAP: the key onto the scale (%d)", n);
        ma_key(&g, 4, 61, false);
        ma_play(&g, false);
    }

    /* no room left empty: in the montuno the solo's four bars and the coro's, played by the section, take turns */
    {
        ma_defaults(&g.s);
        g.s.arc = 0;
        ma_rebuild(&g);
        ma_play(&g, true);
        int empty = 0, blocks = 0;
        for (int b = 0; b < 4; ++b) {
            reset_counts();
            run_bars(4);
            blocks++;
            empty += on[MA_HORNS] == 0;
        }
        ma_play(&g, false);
        CHECK(empty == 0, "the montuno: the horns in every four bars (%d of %d empty)", empty, blocks);
    }

    /* the guitar: every style strums, its notes in the guitar's range, all ended */
    for (int st = 0; st < MA_STYLES; ++st) {
        ma_defaults(&g.s);
        ma_style_apply(&g.s, st);
        g.s.arc = 0;
        g.guitar = true;
        ma_rebuild(&g);
        reset_counts();
        ma_play(&g, true);
        run_bars(4);
        int out = 0;
        for (int n = 0; n < 128; ++n)
            if ((n < 40 || n > 76) && sounding[MA_PIANO][n]) out++;
        ma_play(&g, false);
        CHECK(on[MA_PIANO] >= 8 && out == 0 && still_sounding(MA_PIANO) == 0 && bad_pc == 0,
              "%s: the guitar strums (%d notes, %d out of range)", ma_style_name(st), on[MA_PIANO], out);
        CHECK(on[MA_KEYS] >= 8 && still_sounding(MA_KEYS) == 0, "%s: the upright beside it (%d notes)",
              ma_style_name(st), on[MA_KEYS]);
        g.guitar = false;
    }

    /* the tres: its guajeo on the PIANO track, the upright's montuno beside it on the PERC track */
    {
        ma_defaults(&g.s);
        ma_style_apply(&g.s, MA_ST_SON);
        g.s.arc = 0;
        g.tres = true;
        ma_rebuild(&g);
        reset_counts();
        ma_play(&g, true);
        run_bars(4);
        ma_play(&g, false);
        CHECK(on[MA_PIANO] >= 8 && on[MA_KEYS] >= 8 && bad_pc == 0 && still_sounding(MA_KEYS) == 0,
              "SON: the tres and the upright beside it (%d, %d)", on[MA_PIANO], on[MA_KEYS]);
        g.tres = false;
    }

    /* the bass never quite the same: over sixteen bars of the montuno several rhythms, all in key */
    {
        ma_defaults(&g.s);
        ma_style_apply(&g.s, MA_ST_SON);
        g.s.arc = 0;
        ma_rebuild(&g);
        reset_counts();
        ma_play(&g, true);
        uint16_t seen[16];
        int distinct = 0;
        for (int b = 0; b < 16; ++b) {
            run_bars(1);
            uint16_t h = g.hits_last[MA_BASS];
            bool dup = false;
            for (int k = 0; k < distinct; ++k)
                dup |= seen[k] == h;
            if (!dup && distinct < 16)
                seen[distinct++] = h;
        }
        ma_play(&g, false);
        CHECK(distinct >= 4 && bad_pc == 0, "the bass varies its rhythm (%d different bars in 16), in key", distinct);
    }

    /* the washboard: a scrape a bar, now and then (hot) a tap; the merengue's guira on the guiro, not on it */
    {
        ma_defaults(&g.s);
        g.s.arc = 0;
        g.s.heat = 100;
        g.s.extra = MA_EXTRA_WASH;
        ma_rebuild(&g);
        reset_counts();
        ma_play(&g, true);
        run_bars(16);
        ma_play(&g, false);
        int scrapes = drum_on[MA_DR_WASH], taps = drum_on[MA_DR_WASH_TAP];
        CHECK(scrapes >= 15 && scrapes <= 17 && taps <= 5, "WASH: a scrape a bar (%d in 16), a tap now and then (%d)",
              scrapes, taps);
        ma_defaults(&g.s);
        ma_style_apply(&g.s, MA_ST_MERENGUE);
        g.s.arc = 0;
        ma_rebuild(&g);
        reset_counts();
        ma_play(&g, true);
        run_bars(8);
        ma_play(&g, false);
        CHECK(drum_on[MA_DR_WASH] + drum_on[MA_DR_WASH_TAP] == 0 && drum_on[MA_DR_GUIRO_LONG] > 0 &&
              drum_on[MA_DR_GUIRO_LONG] + drum_on[MA_DR_GUIRO_SHORT] + drum_on[MA_DR_SHAKER_FWD] <= 2 * 8 + 1,
              "MERENGUE: the guira's long scrapes, two a bar (%d)", drum_on[MA_DR_GUIRO_LONG]);
    }

    /* the sampled brass: the section's second horn is the trombone, an octave under the trumpet */
    {
        ma_defaults(&g.s);
        g.s.arc = 0;
        ma_rebuild(&g);
        g.brass = g.trombone = true;
        reset_counts();
        ma_play(&g, true);
        ma_mambo(&g);                              /* (arc off: the section's riff from the next bar) */
        run_bars(8);
        ma_play(&g, false);
        int lows = horns_low;
        g.brass = g.trombone = false;
        reset_counts();
        ma_play(&g, true);
        ma_mambo(&g);
        run_bars(8);
        ma_play(&g, false);
        CHECK(lows > 0 && horns_low == 0 && horn_range_bad == 0,
              "brass: the trombone under the trumpets (%d low, %d without)", lows, horns_low);
        g.brass = false;
    }

    /* the solo's tone, gain and place: OPEN in front bright and centred; HARMON darker; PAN 3D behind duller, far
     * softer, to the right on the right */
    {
        ma_defaults(&g.s);
        g.soloing = true;
        g.s.mute = MA_MUTE_OPEN;
        g.s.bell = 100;
        g.s.p3mode = MA_P3_OFF;
        ma_process(&g, 32);
        g.soloing = true;
        int open = g.tone[MA_HORNS];
        CHECK(open == 127 && g.gain[MA_HORNS] == 0 && g.pan_horns == 64, "OPEN: bright, full, centred (%d)", open);
        g.s.mute = MA_MUTE_HARMON;
        ma_process(&g, 32);
        g.soloing = true;
        CHECK(g.tone[MA_HORNS] == open && g.mute_live == MA_MUTE_OPEN, "HARMON on a sax or a guitar: no mute (%d)",
              g.mute_live);
        g.brass = true;                            /* the sampled brass: the filter is the mute, not the TONE */
        ma_process(&g, 32);
        g.soloing = true;
        CHECK(g.tone[MA_HORNS] == open && g.mute_live == MA_MUTE_HARMON, "brass HARMON: the filter's, the tone open (%d)",
              g.tone[MA_HORNS]);
        g.soloing = false;
        g.keys = 0;
        ma_process(&g, 32);
        CHECK(g.mute_live == MA_MUTE_HARMON, "the section muted too (%d)", g.mute_live);
        g.s.mute = MA_MUTE_AUTO;                   /* AUTO: the section open, each solo turn the next mute */
        int seen = 0;
        bool open_section = true;
        for (int turn = 0; turn < 4; ++turn) {
            g.soloing = false;
            ma_process(&g, 32);
            g.soloing = false;
            open_section &= g.mute_live == MA_MUTE_OPEN;
            g.soloing = true;
            ma_process(&g, 32);
            g.soloing = true;
            ma_process(&g, 32);
            seen |= 1 << g.mute_live;
        }
        CHECK(open_section && seen == 0x1E, "AUTO: the section open, the solo turns through the four mutes (%x)", seen);
        g.brass = false;
        g.soloing = true;
        g.s.mute = MA_MUTE_OPEN;
        g.s.p3mode = MA_P3_FIXED;
        g.s.p3angle = 9;                           /* 90 degrees: right */
        g.s.p3dist = 0;
        ma_process(&g, 32);
        g.soloing = true;
        int right = g.tone[MA_HORNS];
        CHECK(g.pan_horns > 100, "PAN 3D at 90: to the right (%d)", g.pan_horns);
        g.s.p3angle = 18;                          /* behind */
        ma_process(&g, 32);
        g.soloing = true;
        CHECK(g.tone[MA_HORNS] < right, "behind: duller than at the side (%d < %d)", g.tone[MA_HORNS], right);
        g.s.p3dist = 100;
        ma_process(&g, 32);
        CHECK(g.gain[MA_HORNS] > 40, "far: softer (%d)", g.gain[MA_HORNS]);
        g.soloing = false;
    }

    /* HARMONY: the montuno on its vamp; ROUND: the next round a fourth up */
    {
        ma_init(&g, SR);
        ma_defaults(&g.s);
        g.s.arc = 1;
        g.s.vamp = 1;
        g.s.modu = MA_MOD_ROUND;
        g.s.mambo = 0;
        ma_rebuild(&g);
        ma_play(&g, true);
        int guard = 0;
        while (g.sec != MA_SEC_MON && guard++ < 400)
            run_to_bar(g.bar + 1);
        int n;
        const ma_chord *ch = ma_chords(&g, &n);
        CHECK(g.sec == MA_SEC_MON && ch == g.vamp && n == 2, "the montuno: on its vamp (i V7 in minor)");
        CHECK(ma_chord_cur(&g) == &g.vamp[0], "the vamp from the montuno's first bar");
        run_to_bar(g.bar + 1);
        CHECK(ma_chord_cur(&g) == &g.vamp[1] && (g.vamp[1].tone[1] - g.vamp[1].root + 12) % 12 == 4,
              "then the dominant, its major third");
        CHECK(g.mod == 0, "the first round in the song's key");
        guard = 0;
        while (g.round == 0 && guard++ < 400)
            run_to_bar(g.bar + 1);
        CHECK(g.round == 1 && g.mod == 5 && ma_key_now(&g) == (g.s.key + 5) % 12, "ROUND: the next round a fourth up (%d)", g.mod);
        g.s.modu = MA_MOD_OFF;
        while (g.round == 1 && guard++ < 800)
            run_to_bar(g.bar + 1);
        CHECK(g.mod == 0, "MOD OFF: the song's key again");
        ma_play(&g, false);
    }

    /* a bad record: everything into range */
    {
        ma_settings s;
        memset(&s, 0xEE, sizeof s);
        ma_settings_fix(&s);
        CHECK(s.style < MA_STYLES && s.clave < MA_CLAVE_COUNT && s.mute < MA_MUTE_COUNT && s.bpm >= 60 && s.bpm <= 220 &&
              s.snd[3] < MA_STYLES && s.extra < MA_EXTRA_COUNT && s.song <= 999, "a bad record fixed");
    }

    printf("ma_groove: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
