/* SPDX-License-Identifier: GPL-3.0-only */
/* MANTA's screen and controls on the host: firmware/src/ma_ui.c drawn by Felucca's gfx.c into a 240 x 240 frame,
 * the groove running (its notes counted, no sound), the controls driven as a player would, every page in every
 * palette written out, and the layout lint (from Tuna's and Flyfish's UI tests): every text inside its canvas, not
 * cut, not ellipsised unless it is a name, no two texts of one canvas overlapping.
 *   tests/ma_ui_test.sh [OUTDIR]     (exit 0: all passed; PNGs and a GIF of PLAY under OUTDIR) */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ---- the layout lint: the ink boxes of one canvas, checked when it is blitted -------------------------- */
typedef struct { int x0, y0, x1, y1, flags; char s[24]; } box;
static box boxes[256];
static int nbox;
static const char *lint_where = "";
static int lint_findings;
static void hook_text(int x0, int y0, int x1, int y1, const char *s, int flags)
{
    if (nbox < 256) {
        box *b = &boxes[nbox++];
        b->x0 = x0; b->y0 = y0; b->x1 = x1; b->y1 = y1; b->flags = flags;
        strncpy(b->s, s, sizeof b->s - 1);
        b->s[sizeof b->s - 1] = 0;
    }
}
#define GFX_HOOK_TEXT(x0, y0, x1, y1, s, flags) hook_text((x0), (y0), (x1), (y1), (s), (flags))
#define GFX_HOOK_BLIT(x, y, r0) ((void)0)
#define GFX_HOOK_BEGIN() (nbox = 0)
#define GFX_HOOK_PIXELS(n) ((void)0)

static uint16_t fb[240 * 240];
static void lint_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    for (int i = 0; i < nbox; ++i) {
        box *b = &boxes[i];
        if (b->x1 <= b->x0)
            continue;
        bool name = (b->flags & 8) != 0;            /* free text (a name, a message) may be ellipsised */
        if (b->x0 < 0 || b->y0 < 0 || b->x1 > (int)w || b->y1 > (int)h) {
            printf("LINT %s: \"%s\" leaves its canvas (%d,%d..%d,%d of %ux%u at %u,%u)\n", lint_where, b->s, b->x0,
                   b->y0, b->x1, b->y1, w, h, x, y);
            lint_findings++;
        }
        if ((int)x + b->x1 > 240 || (int)y + b->y1 > 240) {
            printf("LINT %s: \"%s\" off the screen\n", lint_where, b->s);
            lint_findings++;
        }
        if ((b->flags & 1) && !name) {
            printf("LINT %s: \"%s\" ellipsised\n", lint_where, b->s);
            lint_findings++;
        }
        for (int j = i + 1; j < nbox; ++j) {
            box *c = &boxes[j];
            if (c->x1 <= c->x0)
                continue;
            if (b->x0 < c->x1 && c->x0 < b->x1 && b->y0 < c->y1 && c->y0 < b->y1) {
                printf("LINT %s: \"%s\" overlaps \"%s\" (%d,%d..%d,%d / %d,%d..%d,%d)\n", lint_where, b->s, c->s, b->x0,
                       b->y0, b->x1, b->y1, c->x0, c->y0, c->x1, c->y1);
                lint_findings++;
            }
        }
    }
    nbox = 0;
}
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    lint_blit(x, y, w, h);
    for (uint32_t j = 0; j < h && y + j < 240; j++)
        for (uint32_t i = 0; i < w && x + i < 240; i++)
            fb[(y + j) * 240 + x + i] = p[j * w + i];
}
#define main hostsim_main
#include "hostsim.c"                               /* (core.h, the matrix: ma_sounds.h's track settings) */
#undef main
#include "gfx.c"
#include "ma_groove.h"
#include "ma_sounds.h"
#include "ma_ui.c"

static const char *outdir;
static int frames_written;
static void write_ppm(const char *name)
{
    if (!outdir) return;
    char path[512];
    snprintf(path, sizeof path, "%s/ppm/%s.ppm", outdir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6 240 240 255\n");
    for (int i = 0; i < 240 * 240; i++) {
        uint16_t c = (uint16_t)((fb[i] >> 8) | (fb[i] << 8));
        uint8_t px[3] = {(uint8_t)((c >> 11) << 3 | (c >> 13)), (uint8_t)(((c >> 5) & 63) << 2 | ((c >> 9) & 3)),
                         (uint8_t)((c & 31) << 3 | ((c >> 2) & 7))};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    frames_written++;
}

static int notes_on, notes_off;
static int part_on[MA_PARTS];
static void sound(void *ctx, const ma_event *e)
{
    (void)ctx;
    if (e->on) { notes_on++; part_on[e->part % MA_PARTS]++; }
    else notes_off++;
}

static ma_groove g;
static ma_ui ui;
static void run_ms(int ms)
{
    for (int t = 0; t < ms; t += 16) {
        ma_process(&g, 44100 * 16 / 1000);
        mu_tick(&ui, 16);
        mu_draw(&ui);
    }
}
static void press(int b) { mu_button(&ui, b, true); mu_button(&ui, b, false); }
static void shot(const char *name)
{
    lint_where = name;
    mu_draw_all(&ui);
    write_ppm(name);
}
static void all_palettes(const char *page)
{
    int keep = g.s.palette, keep_st = g.s.look;
    for (int p = 0; p < (int)NPALETTES; ++p)
        for (int st = 0; st < 2; ++st) {
            char name[64];
            g.s.palette = (uint8_t)p;
            g.s.look = (uint8_t)st;
            mu_apply_style(&ui);
            snprintf(name, sizeof name, "%s%s_%s", st ? "LINE_" : "", UI_PALETTES[p].name, page);
            shot(name);
        }
    g.s.palette = (uint8_t)keep;
    g.s.look = (uint8_t)keep_st;
    mu_apply_style(&ui);
}
static void bars(int n)                            /* play until n bars on */
{
    int b = g.bar + n;
    for (int k = 0; k < 4000 && g.bar < b; ++k)
        run_ms(16);
}

int main(int argc, char **argv)
{
    outdir = argc > 1 ? argv[1] : 0;
    ma_init(&g, 44100.0f);
    ma_set_sound(&g, sound, 0);
    mu_init(&ui, &g);
    ui.cpu = 47;                                   /* (the screenshots: a load on MENU) */
    shot("01_stopped");
    CHECK(g.s.style == MA_ST_SALSA, "SALSA first");
    CHECK(g.s.palette == UI_HAVANA_INDEX && !strcmp(UI_PALETTES[UI_HAVANA_INDEX].name, "HAVANA"), "HAVANA by default");
    all_palettes("play_stopped");
    /* PLAY: the intro, the parts in one by one */
    press(MU_B_PLAY);
    CHECK(g.playing, "PLAY plays");
    for (int k = 0; k < 40; ++k) { run_ms(16); if (k % 2 == 0) { char n[32]; snprintf(n, sizeof n, "anim_%03d", k / 2); lint_where = n; write_ppm(n); } }
    bars(8);
    CHECK(g.sec == MA_SEC_TEMA, "the intro over at bar 8 (%s at %d)", ma_sec_name(g.sec), g.bar);
    CHECK(part_on[MA_PIANO] && part_on[MA_BASS] && part_on[MA_PERC], "the parts play (%d %d %d %d)", part_on[0],
          part_on[1], part_on[2], part_on[3]);
    bars(1);
    shot("02_play");
    all_palettes("play");
    /* the cards: HEAT, CLAVE, SOLO, HITS */
    mu_knob(&ui, MU_K2, 1);
    CHECK(g.s.clave == MA_CLAVE_23, "K2: CLAVE 2-3");
    mu_knob(&ui, MU_K2, -1);
    mu_knob(&ui, MU_K1, -2);
    CHECK(g.s.heat == 60, "K1: HEAT down (%d)", g.s.heat);
    /* REC: BAND HIT */
    press(MU_B_REC);
    CHECK(g.hit_wait, "REC: a band hit on the next bar");
    bars(1);
    run_ms(200);
    shot("03_bandhit");
    /* REC held: the descarga */
    mu_button(&ui, MU_B_REC, true);
    bars(1);
    g.desc = true;                                 /* (held past the cierre) */
    bars(1);
    shot("04_descarga");
    mu_button(&ui, MU_B_REC, false);
    bars(3);
    CHECK(!g.desc, "let go: the band back");
    /* SEQ: MAMBO */
    press(MU_B_SEQ);
    bars(1);
    run_ms(100);
    CHECK(g.sec == MA_SEC_MBO, "SEQ: the mambo (%s)", ma_sec_name(g.sec));
    shot("05_mambo");
    /* PRESETS: the styles, on the bar line */
    mu_knob(&ui, MU_K_PRESETS, 7);
    CHECK(g.next_style == MA_ST_BOLERO, "PRESETS +7: BOLERO queued (%d)", g.next_style);
    shot("06_styles");
    all_palettes("styles");
    bars(1);
    CHECK(g.s.style == MA_ST_BOLERO && g.s.bpm == MA_STYLE[MA_ST_BOLERO].bpm, "BOLERO on the bar line (%d, %d BPM)",
          g.s.style, g.s.bpm);
    run_ms(2000);
    /* the pages */
    press(MU_B_FX);
    CHECK(ui.page == MU_PG_SOUND, "FX: SOUND");
    shot("07_sound");
    all_palettes("sound");
    int snd0 = g.s.snd[MA_PIANO];
    mu_knob(&ui, MU_K1, 1);
    CHECK(g.s.snd[MA_PIANO] == (snd0 + 1) % MA_SOUNDS, "K1: the next PIANO sound (%d)", g.s.snd[MA_PIANO]);
    for (int p = 0; p < 4; ++p) {                 /* every sound's name on its card */
        int keep = g.s.snd[p], n = p == 3 ? MA_STYLES : MA_SOUNDS;
        for (int i = 0; i < n; ++i) {
            char nm[24];
            g.s.snd[p] = (uint8_t)i;
            snprintf(nm, sizeof nm, "sound_%d_%d", p, i);
            lint_where = nm;
            mu_draw_all(&ui);
        }
        g.s.snd[p] = (uint8_t)keep;
    }
    press(MU_B_LFO);
    CHECK(ui.page == MU_PG_SOLO, "LFO: SOLO");
    for (int m = 0; m < MA_MUTE_COUNT; ++m) {
        mu_knob(&ui, MU_K2, m ? 1 : -9);
        char nm[24];
        snprintf(nm, sizeof nm, "solo_mute_%d", m);
        lint_where = nm;
        mu_draw_all(&ui);
    }
    CHECK(g.s.mute == MA_MUTE_AUTO, "MUTE up to AUTO (%d)", g.s.mute);
    mu_knob(&ui, MU_K2, -2);
    shot("08_solo");
    all_palettes("solo");
    press(MU_B_LFO);
    CHECK(ui.page == MU_PG_PAN3D, "LFO again: PAN 3D");
    mu_knob(&ui, MU_K2, 1);
    CHECK(g.s.p3angle == 3, "PAN 3D OFF: ANGLE greyed, untouched (%d)", g.s.p3angle);
    mu_knob(&ui, MU_K1, 2);
    CHECK(g.s.p3mode == MA_P3_ORBIT, "MODE: ORBIT (%d)", g.s.p3mode);
    for (int a = 0; a < 36; ++a) {
        mu_knob(&ui, MU_K2, 1);
        char nm[24];
        snprintf(nm, sizeof nm, "pan3d_%d", a);
        lint_where = nm;
        mu_draw_all(&ui);
    }
    run_ms(500);
    shot("09_pan3d");
    all_palettes("pan3d");
    press(MU_B_EDIT);
    CHECK(ui.page == MU_PG_PERC, "EDIT: PERC");
    mu_knob(&ui, MU_K4, 4);
    CHECK(g.s.extra == MA_EXTRA_WASH, "EXTRA: WASHBOARD (%d)", g.s.extra);
    shot("10_perc");
    all_palettes("perc");
    press(MU_B_SCL);
    CHECK(ui.page == MU_PG_KEY, "SCL: KEY");
    shot("11_key");
    press(MU_B_ARP);
    CHECK(ui.page == MU_PG_GROOVE, "ARP: GROOVE");
    mu_knob(&ui, MU_K2, 1);
    CHECK(g.s.montuno == 1, "MONTUNO 2 (%d)", g.s.montuno);
    shot("12_groove");
    press(MU_B_GLO);
    CHECK(ui.page == MU_PG_MIX, "GLO: MIX");
    shot("13_mix");
    press(MU_B_GLO);
    CHECK(ui.page == MU_PG_SPACE, "GLO again: SPACE");
    shot("14_space");
    press(MU_B_ENV);
    CHECK(ui.page == MU_PG_ARC, "ENV: ARC");
    shot("15_arc");
    all_palettes("arc");
    mu_knob(&ui, MU_K4, -1);
    CHECK(!g.s.mambo, "MAMBO off");
    shot("16_arc_nomambo");
    mu_knob(&ui, MU_K4, 1);
    /* SETS */
    press(MU_B_SAVE);
    CHECK(ui.page == MU_PG_SETS, "SAVE: SETS");
    mu_knob(&ui, MU_K1, 2);
    mu_knob(&ui, MU_K3, 1);
    CHECK((ui.set_used & 4u) && ui.sets[2].style == MA_ST_BOLERO, "STORE into set 3");
    ui.sets[0] = g.s; ui.sets[0].style = MA_ST_MAMBO; ui.set_used |= 1u;
    ui.sets[4] = g.s; ui.sets[4].style = MA_ST_TANGO; ui.set_used |= 16u;
    shot("17_sets");
    mu_knob(&ui, MU_K1, -2);
    mu_knob(&ui, MU_K2, 1);
    CHECK(g.s.style == MA_ST_MAMBO, "LOAD set 1: MAMBO at once");
    /* the keys: the solo */
    int before = part_on[MA_HORNS];
    mu_key(&ui, 60, true);
    CHECK(part_on[MA_HORNS] == before + 1, "a key plays the solo");
    mu_key(&ui, 60, false);
    /* MENU */
    mu_button(&ui, MU_B_HOME, true);
    run_ms(700);
    mu_button(&ui, MU_B_HOME, false);
    CHECK(ui.page == MU_PG_MENU, "HOME held: MENU");
    shot("18_menu");
    all_palettes("menu");
    press(MU_B_HOME);
    CHECK(ui.page == MU_PG_PLAY, "HOME: PLAY");
    /* every style's PLAY page */
    for (int st = 0; st < MA_STYLES; ++st) {
        ma_queue_style(&g, st);
        bars(2);
        char nm[32];
        snprintf(nm, sizeof nm, "style_%02d_%s", st, ma_style_name(st));
        shot(nm);
    }
    /* ALGORITHM: a new song on the next bar */
    int song = g.s.song;
    mu_knob(&ui, MU_K_ALGO, 1);
    bars(1);
    CHECK(g.s.song == song + 1, "ALGORITHM: the next song (%d)", g.s.song);
    press(MU_B_PLAY);
    CHECK(!g.playing, "PLAY stops");
    CHECK(g.nheld == 0, "nothing left sounding");
    CHECK(lint_findings == 0, "the layout lint: %d findings", lint_findings);
    printf("ma_ui: %d checks, %d failed, %d frames, %d notes on (piano %d, bass %d, horns %d, perc %d)\n", checks, fails,
           frames_written, notes_on, part_on[0], part_on[1], part_on[2], part_on[3]);
    return fails ? 1 : 0;
}
