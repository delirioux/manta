/* SPDX-License-Identifier: GPL-3.0-only */
/* MANTA screen and controls (ma_ui.h): Manta's pages in Tuna's frame (Daurade's layout), drawn with Felucca's gfx.c.
 * Included after gfx.c (the firmware's felucca.c unit through ma_fw.c, or tests/ma_ui_test.c). Needs ma_sounds.h
 * (the sounds' names) before it. */
#include "ma_ui.h"
#include "ma_version.h"
#include "ui_icons.h"

#define MU_HEAD 24
#define MU_CARD_Y 28
#define MU_CARD_W 57
#define MU_CARD_H 44
#define MU_CARD_X(c) (3 + 59 * (int32_t)(c))
#define MU_PANEL_Y 76
#define MU_PANEL_H 122
#define MU_FOOT_Y 202
#define MU_FOOT_H 38
#define MU_MENU_HOLD_MS 600

static int mu_clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int mu_wrap(int v, int n) { return n > 0 ? ((v % n) + n) % n : 0; }
static void mu_cpy(char *d, const char *s, unsigned n)
{
    unsigned i = 0;
    for (; s[i] && i + 1 < n; ++i) d[i] = s[i];
    d[i] = 0;
}
static void mu_cat(char *d, const char *s, unsigned n)
{
    unsigned k = 0;
    while (d[k]) ++k;
    mu_cpy(d + k, s, n > k ? n - k : 0);
}
static void mu_fmt_int(char *d, int v)
{
    char b[12];
    int k = 0, neg = v < 0;
    unsigned u = neg ? (unsigned)-v : (unsigned)v;
    do { b[k++] = (char)('0' + u % 10); u /= 10; } while (u && k < 11);
    int j = 0;
    if (neg) d[j++] = '-';
    while (k) d[j++] = b[--k];
    d[j] = 0;
}
static uint32_t mu_hash(uint32_t h, const char *s)
{
    while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}
static int32_t mu_icon_cell(uint32_t *size, uint32_t id)
{
    const uint8_t *idx = *size == 24u ? AI24_IDX : *size == 16u ? AI16_IDX : AI12_IDX;
    if (id >= ICON_COUNT)
        return -1;
    if (idx[id] == 0xFFu) {
        if (*size == 12u || AI12_IDX[id] == 0xFFu)
            return -1;
        *size = 12u;
        idx = AI12_IDX;
    }
    return idx[id];
}
/* the icon's ink centred up and down on the line cy */
static int32_t mu_icon_mid(int32_t x, int32_t cy, uint32_t size, uint32_t id, uint16_t fg, uint16_t bg)
{
    uint32_t sz = size;
    int32_t k = mu_icon_cell(&sz, id), y0, y1;
    if (k < 0)
        return 0;
    if (sz == 24u) {
        uint32_t v = AI24_INK[k];
        y0 = (int32_t)(v >> 8 & 255u);
        y1 = (int32_t)(v >> 24 & 255u);
    } else {
        uint32_t v = sz == 16u ? AI16_INK[k] : AI12_INK[k];
        y0 = (int32_t)(v >> 4 & 15u);
        y1 = (int32_t)(v >> 12 & 15u) + 1;
    }
    int32_t y = cy - HALF_UP(y1 - y0) - y0;
    cv_alpha_hc(x, y, sz, sz, sz == 24u ? AI24_DATA + AI24_OFF[k] : sz == 16u ? AI16_DATA + AI16_OFF[k] :
                AI12_DATA + AI12_OFF[k], AI_HC, ramp(fg, bg));
    GFX_HOOK_TEXT(x, y + cv_oy, x + (int32_t)sz, y + cv_oy + (int32_t)sz, "icon", 4u);
    return (int32_t)sz;
}

/* ---- the cards ------------------------------------------------------------------------------------------- */
enum { SC_NUM, SC_LIST, SC_SND, SC_KEY, SC_SIGNED, SC_LOOP, SC_VALS, SC_PAL, SC_CRUSH, SC_SETSEL, SC_LOAD, SC_STORE,
       SC_CLEAR, SC_OFF };
typedef struct {
    const char *label;
    uint8_t off;                                   /* offsetof(ma_settings, the field) */
    uint8_t kind;
    int16_t lo, hi;
    uint8_t step;
    const char *const *names;
    const uint8_t *vals;                           /* SC_VALS: the values it steps through (hi + 1 of them) */
} mu_cdef;
#define SS_(f) ((uint8_t)__builtin_offsetof(ma_settings, f))
static const char *const SN_ONOFF[] = {"OFF", "ON"};
static const char *const SN_LOOK[] = {"FLAT", "LINE"};
static const char *const SN_HATS[] = {"8THS", "16THS", "OFFBEAT", "OPEN"};
static const char *const SN_FILLS[] = {"OFF", "8 BARS", "4 BARS", "2 BARS"};
static const char *const SN_BASSLINE[] = {"OFFBEAT", "OCTAVE", "ROLLING", "REGGAE", "LONG", "BOUNCE"};
static const char *const SN_KICK[] = {"PUNCH", "ROUND"};
static const char *const SN_CLAP[] = {"CLAP", "SNARE", "BOTH"};
static const char *const SN_CRASH[] = {"OFF", "SECTION", "8 BARS"};
static const char *const SN_VOX[] = {"OFF", "CHORUS", "ALL"};
static const char *const SN_KEYUP[] = {"OFF", "+1", "+2"};
static const char *const SN_SPEED[] = {"SLOW", "MID", "FAST", "PAD"};
static const char *const SN_SCALE[] = {"MIN", "MAJ", "DOR", "HARM"};
static const char *const SN_SCALE_LONG[] = {"MINOR", "MAJOR", "DORIAN", "HARMONIC MINOR"};
static const uint8_t SV_BARS[] = {1, 2, 4}, SV_SECTION[] = {8, 16}, SV_REPEAT[] = {1, 2, 4}, SV_RANGE[] = {1, 2};
#define C_NUM(l, f) {l, SS_(f), SC_NUM, 0, 100, 5, 0, 0}
#define C_LIST(l, f, n) {l, SS_(f), SC_LIST, 0, (int16_t)(sizeof n / sizeof n[0]) - 1, 1, n, 0}
#define C_VALS(l, f, v) {l, SS_(f), SC_VALS, 0, (int16_t)(sizeof v / sizeof v[0]) - 1, 1, 0, v}
#define C_SND(l, p) {l, (uint8_t)(SS_(snd) + (p)), SC_SND, 0, (p) == 3 ? MA_KIT_COUNT - 1 : MA_SOUNDS - 1, 1, 0, 0}
#define C_OFF {"", 0, SC_OFF, 0, 0, 0, 0, 0}
static const mu_cdef MU_CARDS[MU_PAGES][4] = {
    [MU_PG_PLAY] = {C_NUM("LIFT", lift), C_NUM("PUMP", pump), C_LIST("VOX", vox, SN_VOX), C_NUM("ENERGY", energy)},
    [MU_PG_SOUND] = {C_SND("STABS", 0), C_SND("BASS", 1), C_SND("HOOK", 2), C_SND("DRUMS", 3)},
    [MU_PG_TONE] = {C_NUM("RESO", reso), C_NUM("DRIVE", drive), C_NUM("DETUNE", ensemble),
                    {"CRUSH", SS_(crush), SC_CRUSH, 0, 100, 5, 0, 0}},
    [MU_PG_VOX] = {{"VOWELS", SS_(vowels), SC_LIST, 0, MA_VOWELS_COUNT - 1, 1, MAS_VOWEL_NAMES, 0},
                   {"SHIFT", SS_(shift), SC_SIGNED, -12, 12, 1, 0, 0},
                   {"CHOIR", SS_(choir), SC_LIST, 0, MA_CHOIR_COUNT - 1, 1, MAS_CHOIR_NAMES, 0}, C_NUM("GLIDE", glide)},
    [MU_PG_KEY] = {{"KEY", SS_(key), SC_KEY, 0, 11, 1, 0, 0}, C_LIST("SCALE", scale, SN_SCALE),
                   {"CHORDS", SS_(chords), SC_LOOP, 0, MA_LOOPS - 1, 1, 0, 0}, C_VALS("BARS", bars, SV_BARS)},
    [MU_PG_GROOVE] = {{"SWING", SS_(swing), SC_NUM, 0, 60, 2, 0, 0}, C_LIST("HATS", hats, SN_HATS),
                      C_LIST("BASS", bassline, SN_BASSLINE), C_LIST("FILLS", fills, SN_FILLS)},
    [MU_PG_DRUMS] = {C_SND("KIT", 3), C_LIST("KICK", kick, SN_KICK), C_LIST("CLAP", clap, SN_CLAP),
                     C_LIST("CRASH", crash, SN_CRASH)},
    [MU_PG_MIX] = {C_NUM("STABS", lvl[0]), C_NUM("BASS", lvl[1]), C_NUM("HOOK", lvl[2]), C_NUM("DRUMS", lvl[3])},
    [MU_PG_SPACE] = {C_NUM("REVERB", reverb), C_NUM("DELAY", delay),
                     {"TIME", SS_(dtime), SC_LIST, 0, MA_DTIME_COUNT - 1, 1, MAS_DTIME_NAMES, 0}, C_NUM("WIDTH", width)},
    [MU_PG_ARC] = {C_LIST("ARC", arc, SN_ONOFF), C_VALS("VERSE", verse, SV_SECTION), C_VALS("CHORUS", chorus, SV_SECTION),
                   C_LIST("KEY UP", keyup, SN_KEYUP)},
    [MU_PG_HOOK] = {C_LIST("SPEED", speed, SN_SPEED), C_VALS("RANGE", range, SV_RANGE),
                    {"GATE", SS_(gate), SC_NUM, 10, 100, 5, 0, 0}, C_VALS("REPEAT", repeat, SV_REPEAT)},
    [MU_PG_SETS] = {{"SET", 0, SC_SETSEL, 0, MA_SETS - 1, 1, 0, 0}, {"LOAD", 0, SC_LOAD, 0, 0, 1, 0, 0},
                    {"STORE", 0, SC_STORE, 0, 0, 1, 0, 0}, {"CLEAR", 0, SC_CLEAR, 0, 0, 1, 0, 0}},
    [MU_PG_MENU] = {{"COLOR", SS_(palette), SC_PAL, 0, 10, 1, 0, 0}, C_LIST("LOOK", look, SN_LOOK), C_LIST("SNAP", keysnap, SN_ONOFF), C_OFF},
};
static const char *const MU_PAGE_NAMES[MU_PAGES] = {"PLAY", "SOUND", "TONE", "VOX", "KEY", "GROOVE", "DRUMS", "MIX",
                                                    "SPACE", "ARC", "HOOK", "SETS", "MENU"};
const char *mu_page_name(int page) { return page >= 0 && page < MU_PAGES ? MU_PAGE_NAMES[page] : ""; }

static int mu_get(const ma_ui *u, const mu_cdef *c)
{
    const uint8_t *b = (const uint8_t *)&u->g->s + c->off;
    if (c->kind == SC_SIGNED)
        return (int)*(const int8_t *)b;
    if (c->kind == SC_VALS) {                      /* the index of the value it holds */
        for (int i = 0; i <= c->hi; ++i)
            if (c->vals[i] == *b) return i;
        return 0;
    }
    if (c->kind == SC_SETSEL)
        return u->set_sel;
    return (int)*b;
}
static void mu_set(ma_ui *u, const mu_cdef *c, int v)
{
    uint8_t *b = (uint8_t *)&u->g->s + c->off;
    if (c->kind == SC_SIGNED) *(int8_t *)b = (int8_t)v;
    else if (c->kind == SC_VALS) *b = c->vals[mu_clampi(v, c->lo, c->hi)];
    else if (c->kind == SC_SETSEL) u->set_sel = v;
    else *b = (uint8_t)v;
}
static void mu_card_value(const ma_ui *u, const mu_cdef *c, char *t, unsigned n)
{
    int v = mu_get(u, c);
    t[0] = 0;
    switch (c->kind) {
    case SC_LIST: mu_cpy(t, c->names[mu_clampi(v, c->lo, c->hi)], n); break;
    case SC_SND: mu_cpy(t, mas_name(c->off - SS_(snd), v), n); break;
    case SC_KEY: mu_cpy(t, ma_note_name(v), n); break;
    case SC_SIGNED: if (v > 0) { mu_cpy(t, "+", n); } mu_fmt_int(t + (v > 0), v); break;
    case SC_LOOP: ma_loop_name(&u->g->s, v, t, (int)n); break;
    case SC_VALS: {
        int x = c->vals[mu_clampi(v, c->lo, c->hi)];
        mu_fmt_int(t, x);
        if (c->off == SS_(verse) || c->off == SS_(chorus)) mu_cat(t, " BARS", n);
        if (c->off == SS_(repeat)) mu_cat(t, x == 1 ? " BAR" : " BARS", n);
        if (c->off == SS_(range)) mu_cat(t, " OCT", n);
        break;
    }
    case SC_PAL: mu_cpy(t, UI_PALETTES[mu_wrap(v, (int)NPALETTES)].name, n); break;
    case SC_CRUSH: if (v) mu_fmt_int(t, v); else mu_cpy(t, "OFF", n); break;
    case SC_SETSEL: mu_fmt_int(t, v + 1); break;
    case SC_LOAD: mu_cpy(t, (u->set_used >> u->set_sel) & 1u ? ">" : "EMPTY", n); break;
    case SC_STORE: mu_cpy(t, ">", n); break;
    case SC_CLEAR:
        if (u->clear_turns == 1) mu_cpy(t, ">>", n);
        else if (u->clear_turns == 2) mu_cpy(t, ">", n);
        else mu_cpy(t, ">>>", n);
        break;
    case SC_OFF: break;
    default: mu_fmt_int(t, v); break;
    }
}

static void mu_draw_card(ma_ui *u, int i, const mu_cdef *c)
{
    bool hot = u->hot == i && u->hot_ms > 0;
    uint16_t lc = hot ? T_ACCENT : T_MID, vc = hot ? T_ACCENT : T_THEME;
    char val[24];
    mu_card_value(u, c, val, sizeof val);
    int ratio = -1;
    if ((c->kind == SC_NUM || c->kind == SC_CRUSH || c->kind == SC_SIGNED || c->kind == SC_VALS || c->kind == SC_SETSEL ||
         c->kind == SC_LOOP || c->kind == SC_KEY || c->kind == SC_SND) && c->hi > c->lo)
        ratio = (mu_get(u, c) - c->lo) * 1000 / (c->hi - c->lo);
    uint32_t sig = mu_hash(2166136261u + (uint32_t)i * 7u + ux.gen * 131u + (uint32_t)hot * 3u + (uint32_t)u->page * 977u,
                           c->label);
    sig = mu_hash(sig, val) + (uint32_t)(ratio + 1) * 31u;
    if (!u->force && sig == u->sig_card[i])
        return;
    u->sig_card[i] = sig;
    cv_begin(MU_CARD_W, MU_CARD_H, T_BG);
    cv_rrect(0, 0, MU_CARD_W, MU_CARD_H, 4, T_SURF, T_BG);
    if (c->kind == SC_OFF) {
        cv_rect(MU_CARD_W / 2 - 6, MU_CARD_H / 2, 12, 1, T_DIM);
    } else {
        cv_text_fit(5, 3, &AF_S, c->label, lc, T_SURF, MU_CARD_W - 8);
        const aafont_t *vf = text_w(&AF_M, val) <= MU_CARD_W - 8 ? &AF_M : &AF_S;
        cv_text_fit(5, vf == &AF_M ? 17 : 19, vf, val, vc, T_SURF, MU_CARD_W - 8);
        if (ratio >= 0) {
            int32_t w = MU_CARD_W - 10, x0 = 5, y = 37, r = mu_clampi(ratio, 0, 1000) * w / 1000;
            cv_rect(x0, y, w, 3, T_LINE);
            if (c->kind == SC_SIGNED) {
                int32_t mid = x0 + w / 2;
                if (r >= w / 2) cv_rect(mid, y, r - w / 2 + 1, 3, vc);
                else cv_rect(x0 + r, y, mid - x0 - r + 1, 3, vc);
                cv_rect(mid, y - 2, 1, 7, hot ? T_ACCENT : T_MID);
            } else {
                cv_rect(x0, y, r > 0 ? r : 1, 3, vc);
            }
        }
    }
    cv_blit((uint32_t)MU_CARD_X(i), MU_CARD_Y);
}

/* ---- the header: playing, the tempo, a word (the section, NEXT, BREAK), a badge (the key up, HELD), the battery -- */
static void mu_head_msg(const ma_ui *u, char *t, unsigned n)
{
    const ma_groove *g = u->g;
    t[0] = 0;
    if (u->msg_ms > 0) {
        mu_cpy(t, u->msg, n);
    } else if (g->next_style >= 0) {
        mu_cpy(t, "NEXT ", n);
        mu_cat(t, ma_style_name(g->next_style), n);
    } else if (g->next_song >= 0) {
        char a[8];
        mu_cpy(t, "NEXT SONG ", n);
        mu_fmt_int(a, g->next_song + 1);
        mu_cat(t, a, n);
    } else if (g->playing && g->brk) {
        mu_cpy(t, "BREAK", n);
    } else if (g->playing) {
        mu_cpy(t, g->s.arc ? ma_sec_name(g->sec) : "CHORUS", n);
    }
}
static void mu_head_badge(const ma_ui *u, char *t, unsigned n)
{
    const ma_groove *g = u->g;
    t[0] = 0;
    if (!g->playing)
        return;
    if (g->brk && g->brk_held) {
        mu_cpy(t, "HELD", n);
    } else if (g->tr > 0) {
        char a[4];
        mu_cpy(t, "+", n);
        mu_fmt_int(a, g->tr);
        mu_cat(t, a, n);
    }
}

static void mu_draw_head(ma_ui *u)
{
    const ma_groove *g = u->g;
    char b[16], m[40], bd[8];
    bool hot = u->hot == 9 && u->hot_ms > 0;
    mu_head_msg(u, m, sizeof m);
    mu_head_badge(u, bd, sizeof bd);
    uint32_t sig = mu_hash(mu_hash((uint32_t)g->playing * 3u + (uint32_t)g->s.bpm * 101u + ux.gen * 13u +
                                   (uint32_t)u->batt * 7777u + (uint32_t)hot * 23u, m), bd);
    if (!u->force && sig == u->sig_head)
        return;
    u->sig_head = sig;
    cv_begin(240, MU_HEAD, T_BG);
    mu_icon_mid(8, MU_HEAD / 2, 16, g->playing ? ICON_X_PLAY : ICON_X_STOP, g->playing ? T_THEME : T_MID, T_BG);
    mu_icon_mid(54, MU_HEAD / 2, 16, ICON_TEMPO, T_MID, T_BG);
    mu_fmt_int(b, g->s.bpm);
    cv_text_on(72, CAP_IN(M, MU_HEAD), &AF_M, b, hot ? T_ACCENT : T_THEME, T_BG);
    int32_t right = 208;
    if (bd[0]) {                                   /* the badge: the key up or HELD, in the accent */
        int32_t w = text_w(&AF_S, bd) + 10;
        cv_rrect(right - w, 4, w, MU_HEAD - 8, 3, T_ACCENT, T_BG);
        cv_text_in(right - w, 4 + CAP_IN(S, MU_HEAD - 8), w, &AF_S, bd, T_INK, T_ACCENT);
        right -= w + 6;
    }
    if (m[0]) {
        bool sec = g->playing && u->msg_ms <= 0 && g->next_style < 0 && g->next_song < 0;
        if (m[0] == 'N' && m[1] == 'E' && m[2] == 'X' && m[3] == 'T' && m[4] == ' ') {   /* NEXT in the accent */
            int32_t x = cv_text_on(106, CAP_IN(S, MU_HEAD), &AF_S, "NEXT", T_ACCENT, T_BG) + 5;
            cv_free_text(x, CAP_IN(S, MU_HEAD), &AF_S, m + 5, T_TEXT, T_BG, right - x);
        } else {
            cv_free_text(106, CAP_IN(S, MU_HEAD), &AF_S, m, sec ? T_ACCENT : T_TEXT, T_BG, right - 106);
        }
    }
    {
        static const uint8_t BAT[5] = {ICON_X_BAT0, ICON_X_BAT1, ICON_X_BAT3, ICON_X_BAT4, ICON_X_BAT_CHG};
        int lv = mu_clampi(u->batt, 0, 4);
        mu_icon_mid(214, MU_HEAD / 2, 24, BAT[lv], lv == 1 ? T_ACCENT : lv == 0 ? T_MID : T_THEME, T_BG);
    }
    cv_blit(0, 0);
}

/* ---- the footer: the section's bars as 16 dashes, the style and its song number, the page ---------------------- */
static void mu_draw_foot(ma_ui *u)
{
    const ma_groove *g = u->g;
    int filled = 0;
    if (g->playing) {
        if (g->s.arc && !g->brk) {
            int len = ma_sec_len(g, g->sec);
            filled = g->sec_bar * 16 / (len > 0 ? len : 1);
        } else {
            filled = g->bar % 16;
        }
    }
    const char *style = ma_style_name(g->s.style);
    char song[8];                                  /* the song playing (ALGORITHM), on every page */
    mu_fmt_int(song, g->s.song + 1);
    const char *right = mu_page_name(u->page);
    uint32_t sig = mu_hash(mu_hash(mu_hash((uint32_t)filled * 7u + (uint32_t)g->playing * 3u + ux.gen * 11u +
                                           (uint32_t)u->page * 13u, style), right), song);
    if (!u->force && sig == u->sig_foot)
        return;
    u->sig_foot = sig;
    cv_begin(240, MU_FOOT_H, T_BG);
    for (int i = 0; i < 16; ++i)
        cv_rect(10 + i * 14, 6, 10, 2, i < filled ? T_THEME : i == filled && g->playing ? T_ACCENT : T_LINE);
    cv_free_text(10, 17, &AF_M, style, T_THEME, T_BG, 130);
    int32_t nx = 10 + text_w(&AF_M, style);
    if (nx > 140) nx = 140;
    int32_t yb = 17 + AF_M_CAP_Y + AF_M_CAP_H - AF_S_CAP_Y - AF_S_CAP_H;
    int32_t px = 230 - text_w(&AF_S, right) - 8;
    if (px - nx - 6 > 24)
        cv_free_text(nx + 6, yb, &AF_S, song, T_TEXT, T_BG, px - nx - 6);
    cv_text_r(230, yb, &AF_S, right, T_MID, T_BG);
    cv_blit(0, MU_FOOT_Y);
}

/* ---- the pictures -------------------------------------------------------------------------------------------- */
static void mu_panel_begin(void)
{
    cv_begin(240, MU_PANEL_H, T_BG);
    cv_rrect(3, 0, 234, MU_PANEL_H, 4, T_SURF, T_BG);
}
static int32_t mu_yb_s(int32_t y_m) { return y_m + AF_M_CAP_Y + AF_M_CAP_H - AF_S_CAP_Y - AF_S_CAP_H; }
static void mu_label(int32_t x, int32_t y, const char *s, uint16_t c) { cv_text_on(x, y, &AF_S, s, c, T_SURF); }
static uint16_t mu_well(void) { return ux_mix(T_SURF, T_LINE, 100); }

/* the loop's chords, the one sounding filled */
static void mu_chord_strip(const ma_groove *g, int32_t y, int now)
{
    int n = g->nloop < 4 ? g->nloop : 4;
    int32_t gap = 5, w = (224 - gap * (n - 1)) / (n > 0 ? n : 1);
    for (int i = 0; i < n; ++i) {
        int32_t x = 10 + i * (w + gap);
        if (i == now) {
            cv_rrect(x, y, w, 20, 3, T_THEME, T_SURF);
            cv_text_in(x, y + CAP_IN(S, 20), w, &AF_S, g->loop[i].name, T_INK, T_THEME);
        } else {
            cv_rrect(x, y, w, 20, 3, mu_well(), T_SURF);
            cv_rrect(x + 1, y + 1, w - 2, 18, 3, T_SURF, mu_well());
            cv_text_in(x, y + CAP_IN(S, 20), w, &AF_S, g->loop[i].name, T_TEXT, T_SURF);
        }
    }
}

/* the form as a strip: its six sections, the one playing in the accent, the bars into it under it; the key up's
 * +1 or +2 over the last chorus */
static void mu_form_strip(const ma_groove *g, int32_t y)
{
    for (int k = 0; k < MA_SECS; ++k) {
        int32_t x = 10 + k * 37, w = 33;
        bool now = g->playing && g->s.arc && !g->brk && g->sec == k;
        bool past = g->playing && g->s.arc && !g->brk && k < g->sec;
        if (!g->s.arc)
            now = k == MA_SEC_CHORUS;
        uint16_t c = now ? T_ACCENT : past ? ux_mix(T_SURF, T_THEME, 55) : mu_well();
        cv_rect(x, y, w, 4, c);
        if (now && g->playing && g->s.arc) {       /* how far into it */
            int len = ma_sec_len(g, k);
            int32_t p = (g->sec_bar * 16 + (g->step < 0 ? 0 : g->step)) * w / (len * 16 > 0 ? len * 16 : 1);
            cv_rect(x, y + 5, p > 0 ? p : 1, 1, T_ACCENT);
        }
        cv_text_on(x, y + 9, &AF_S, ma_sec_short(k), now ? T_ACCENT : T_MID, T_SURF);
        if (k == MA_SEC_CHORUS2 && g->s.arc && g->s.keyup) {   /* the key up: an arrow (one, or two for +2) */
            for (int a = 0; a < g->s.keyup; ++a) {
                int32_t ax = x + w - 3, ay = y + 10 + a * 5;
                cv_line_t(ax - 3, ay + 3, ax, ay, T_THEME, 2);
                cv_line_t(ax, ay, ax + 3, ay + 3, T_THEME, 2);
            }
        }
    }
}

/* the bar as it plays: a column a sixteenth, as tall as the parts that sounded on it; the one playing lit */
static void mu_step_meter(const ma_groove *g, int32_t y, int32_t h)
{
    int now = g->playing && g->step >= 0 ? g->step : -1;
    for (int k = 0; k < 16; ++k) {
        int n = 0;
        for (int p = 0; p < MA_PARTS; ++p) {
            uint16_t bits = k <= now ? g->hits[p] : g->hits_last[p];
            n += (bits >> k) & 1u;
        }
        int32_t x = 10 + k * 14, bh = n ? 6 + n * (h - 6) / 4 : 2;
        uint16_t c = k == now ? T_ACCENT : k % 4 == 0 ? T_THEME : ux_mix(T_SURF, T_THEME, 75);
        cv_rect(x, y + h - bh, 10, bh, n ? c : T_LINE);
    }
}

/* the low-pass's curve: flat, the resonant peak at the cutoff, then the fall; cutoff and reso 0..1000 */
static void mu_filter_curve(int32_t x0, int32_t y0, int32_t w, int32_t h, int cutoff, int reso, uint16_t c, bool fill)
{
    int32_t cx = x0 + 8 + cutoff * (w - 16) / 1000, base = y0 + h * 45 / 100, py = -1;
    int32_t pk = reso * (h * 40 / 100) / 1000;
    for (int32_t x = x0; x < x0 + w; ++x) {
        int32_t d = x - cx, y;
        if (d < -24) y = base;
        else if (d <= 0) y = base - pk * (24 + d) * (24 + d) / (24 * 24);
        else y = base - pk + d * d / 6;
        if (d > 0 && y < base - pk + d) y = base - pk + d;
        if (y > y0 + h) y = y0 + h;
        if (y < y0) y = y0;
        if (fill)
            cv_rect(x, y + 1, 1, y0 + h - y, ux_mix(T_SURF, c, 18));
        if (py >= 0)
            cv_line_t(x - 1, py, x, y, c, 2);
        py = y;
    }
    cv_rect(cx, y0 + h + 3, 1, 4, T_ACCENT);
}

static void mu_panel_play(ma_ui *u)
{
    const ma_groove *g = u->g;
    if (!g->playing) {
        char line[40], a[8];
        int32_t w = kc_w(KC_PLAY) + 5 + text_w(&AF_S, "TO START");
        int32_t x = 3 + HALF_UP(234 - w), y = MU_PANEL_H / 2 - KC_H / 2 - 8;
        cv_keycap(x, y, KC_PLAY, T_KEY, T_INK, T_SURF);
        cv_text_on(x + kc_w(KC_PLAY) + 5, y + CAP_IN(S, KC_H), &AF_S, "TO START", T_MID, T_SURF);
        mu_cpy(line, "SONG ", sizeof line);
        mu_fmt_int(a, g->s.song + 1);
        mu_cat(line, a, sizeof line);
        mu_cat(line, "   ", sizeof line);
        mu_cat(line, ma_note_name(g->s.key), sizeof line);
        mu_cat(line, " ", sizeof line);
        mu_cat(line, SN_SCALE_LONG[g->s.scale % MA_SCALES], sizeof line);
        int32_t lw = text_w(&AF_S, line);
        cv_text_on(3 + HALF_UP(234 - lw), MU_PANEL_H / 2 + 14, &AF_S, line, T_DIM, T_SURF);
        return;
    }
    if (g->brk) {                                  /* BREAK: the stabs alone, the roll back in */
        static const char *const parts[4] = {"STABS", "BASS", "HOOK", "DRUMS"};
        char t[16], a[8];
        cv_text_on(10, 6, &AF_L, "BREAK", T_ACCENT, T_SURF);
        int back = g->brk_held ? -1 : g->brk_end - g->bar;
        if (back > 0) {
            mu_cpy(t, "BACK IN ", sizeof t);
            mu_fmt_int(a, back);
            mu_cat(t, a, sizeof t);
        } else {
            mu_cpy(t, "HELD", sizeof t);
        }
        cv_text_r(230, mu_yb_s(10), &AF_S, t, T_TEXT, T_SURF);
        bool roll = !g->brk_held && g->bar == g->brk_end - 1;
        for (int i = 0; i < 4; ++i) {
            int32_t y = 38 + i * 15;
            bool on = i == MA_STABS || (i == MA_DRUMS && roll);
            mu_label(10, y, parts[i], on ? T_TEXT : T_DIM);
            if (on)
                cv_rect(66, y + 4, 162, 3, T_THEME);
            else
                cv_rect(66, y + 5, 162, 1, T_LINE);
        }
        if (roll)
            mu_label(10, 102, "SNARE ROLL > CHORUS", T_ACCENT);
        return;
    }
    mu_chord_strip(g, 6, ma_chord_now(g));
    mu_step_meter(g, 32, 50);
    mu_form_strip(g, 90);
}

/* PRESETS turned: the ten styles, the queued one marked */
static void mu_panel_styles(ma_ui *u)
{
    const ma_groove *g = u->g;
    int sel = g->next_style >= 0 ? g->next_style : g->s.style;
    mu_label(10, 5, "CLUB", T_MID);
    mu_label(124, 5, "RADIO", T_MID);
    for (int i = 0; i < 10; ++i) {
        int col = MA_STYLE[i].side, row = 0;
        for (int k = 0; k < i; ++k) row += MA_STYLE[k].side == col;
        int32_t x = col ? 120 : 6, y = 22 + row * 19;
        bool now = i == g->s.style, q = i == sel && g->next_style >= 0;
        if (q || (i == sel && !g->playing)) {
            cv_rrect(x, y - 3, 110, 17, 3, T_ACCENT, T_SURF);
            cv_text_fit(x + 6, y, &AF_S, MA_STYLE[i].name, T_INK, T_ACCENT, 100);
        } else {
            cv_text_fit(x + 6, y, &AF_S, MA_STYLE[i].name, now ? T_THEME : T_TEXT, T_SURF, 100);
        }
        if (now)
            cv_rect(x - 1, y + 2, 2, 8, T_THEME);
    }
}

static void mu_panel_sound(ma_ui *u)
{
    const ma_groove *g = u->g;
    static const char *const nm[4] = {"STABS", "BASS", "HOOK", "DRUMS"};
    for (int i = 0; i < 4; ++i) {
        int32_t y = 8 + i * 28;
        bool hot = u->hot == i && u->hot_ms > 0;
        cv_text_on(10, y, &AF_S, nm[i], hot ? T_ACCENT : T_MID, T_SURF);
        cv_text_fit(64, y, &AF_S, mas_name(i, g->s.snd[i]), T_TEXT, T_SURF, 164);
        uint16_t bits = g->playing ? g->hits_last[i] : 0;
        for (int k = 0; k < 16; ++k) {
            bool on = (bits >> k) & 1;
            cv_rect(64 + k * 10, y + 18, 7, 2, on ? (hot ? T_ACCENT : T_THEME) : T_LINE);
        }
    }
}

static void mu_panel_tone(ma_ui *u)
{
    const ma_groove *g = u->g;
    mu_label(10, 6, "THE FILTER THE LIFT MOVES", T_MID);
    mu_filter_curve(10, 30, 220, 56, (int)(g->lift_now * 1000.0f), 150 + g->s.reso * 8, T_THEME, true);
    char t[24], a[8];
    mu_cpy(t, "RESO ", sizeof t);
    mu_fmt_int(a, g->s.reso);
    mu_cat(t, a, sizeof t);
    cv_text_r(230, 100, &AF_S, t, T_THEME, T_SURF);
    if (g->s.crush) {
        mu_cpy(t, "CRUSH", sizeof t);
    } else {
        mu_cpy(t, "DETUNE ", sizeof t);
        mu_fmt_int(a, g->s.ensemble);
        mu_cat(t, a, sizeof t);
    }
    mu_label(10, 100, t, T_DIM);
}

/* the choir's chords over two bars of the chorus as CHOIR sings them (ma_groove.c play_choir), the one singing lit */
static int mu_choir_at(int choir, int bar, int i)
{
    if (i != 0)
        return -1;
    return choir == MA_CHOIR_MIX ? MA_DR_OH + bar : choir == MA_CHOIR_AH ? MA_DR_AH : MA_DR_OH;
}

/* the OOH's vowels: the formant gliding from the first to the second after each hook note; the choir under it */
static void mu_panel_vox(ma_ui *u)
{
    const ma_groove *g = u->g;
    static const char *const VN[5] = {"AH", "EH", "EE", "OH", "OO"};
    int a = mu_clampi((MAS_VOWELS[g->s.vowels % MA_VOWELS_COUNT][0] + 16) / 32, 0, 4);
    int b = mu_clampi((MAS_VOWELS[g->s.vowels % MA_VOWELS_COUNT][1] + 16) / 32, 0, 4);
    char t[24];
    mu_label(10, 4, "VOWELS", T_MID);
    mu_cpy(t, VN[a], sizeof t);
    mu_cat(t, " > ", sizeof t);
    mu_cat(t, VN[b], sizeof t);
    cv_text_r(230, 4, &AF_S, t, T_THEME, T_SURF);
    /* the glide: a wave from one vowel's height to the other's, the dot where the voice is now */
    float talk_ms = 1.0f;
    {
        int16_t v = mas_talk(&g->s);
        for (int k = 0; k < v; ++k) talk_ms *= 1.0752f;     /* 10000^(1/127) */
    }
    float since = g->playing ? (float)(g->clock - g->hook_on_clock) / g->sr * 1000.0f : talk_ms;
    float f = since / talk_ms;
    if (f > 1.0f) f = 1.0f;
    if (f < 0.0f) f = 0.0f;
    int32_t ya = 30 + a * 8, yb = 30 + b * 8, py = -1, dotx = 10 + (int32_t)(f * 220.0f);
    for (int32_t x = 10; x < 230; ++x) {
        int32_t s = (x - 10) * 1000 / 220;          /* smoothstep from a to b */
        int32_t sm = s * s / 1000 * (3000 - 2 * s) / 1000;
        int32_t y = ya + (yb - ya) * sm / 1000;
        if (py >= 0)
            cv_line_t(x - 1, py, x, y, T_THEME, 2);
        py = y;
        if (x == dotx)
            cv_rrect(x - 4, y - 4, 9, 9, 3, T_ACCENT, T_SURF);
    }
    const mas_sound *hk = mas_of(g, MA_HOOK);
    if (hk->engine != MAS_VOICE)
        mu_label(10, 66, "HOOK: OOH SINGS THESE", T_DIM);
    /* the choir */
    mu_label(10, 80, "CHOIR", T_MID);
    if (g->s.snd[3] != MA_KIT_EURO) {
        mu_label(64, 80, "KIT EURO BRINGS THE CHOIR", T_DIM);
        return;
    }
    bool lit = g->playing && (float)(g->clock - g->choir_clock) < g->sr * 0.6f;
    static const char *const CN[2] = {"OH", "AH"};
    for (int bar = 0; bar < 2; ++bar)
        for (int i = 0; i < 16; i += 2) {
            int32_t x = 10 + bar * 112 + i * 7;
            int d = g->s.vox == MA_VOX_OFF ? -1 : mu_choir_at(g->s.choir, bar, i);
            if (d < 0) {
                cv_rect(x + 4, 104, 3, 3, T_LINE);
                continue;
            }
            const char *nm = CN[d - MA_DR_OH];
            int32_t w = text_w(&AF_S, nm) + 8;
            bool on = lit && g->choir_last == d;
            cv_rrect(x, 97, w, 17, 3, on ? T_ACCENT : T_THEME, T_SURF);
            cv_text_in(x, 97 + CAP_IN(S, 17), w, &AF_S, nm, T_INK, on ? T_ACCENT : T_THEME);
            i += 2 * ((w + 13) / 14);              /* (room for the word) */
            i -= 2;
        }
    if (g->s.vox == MA_VOX_OFF)
        cv_text_r(230, 80, &AF_S, "VOX OFF", T_DIM, T_SURF);
}

static void mu_panel_key(ma_ui *u)
{
    const ma_groove *g = u->g;
    char t[32], a[8];
    int bpc = ma_bars_per_chord(&g->s), now = g->playing ? ma_chord_now(g) : -1;
    mu_cpy(t, "THE LOOP, ", sizeof t);
    mu_fmt_int(a, bpc);
    mu_cat(t, a, sizeof t);
    mu_cat(t, bpc == 1 ? " BAR A CHORD" : " BARS A CHORD", sizeof t);
    mu_label(10, 6, t, T_MID);
    int n = g->nloop < 4 ? g->nloop : 4;
    int32_t gap = 4, w = (224 - gap * (n - 1)) / (n > 0 ? n : 1);
    for (int i = 0; i < n; ++i) {
        int32_t x = 10 + i * (w + gap);
        uint16_t f = i == now ? T_THEME : mu_well();
        cv_rrect(x, 30, w, 52, 4, f, T_SURF);
        const aafont_t *ft = text_w(&AF_M, g->loop[i].name) <= w - 4 ? &AF_M : &AF_S;
        cv_text_in(x, 30 + (ft == &AF_M ? CAP_IN(M, 52) : CAP_IN(S, 52)), w, ft, g->loop[i].name, i == now ? T_INK : T_TEXT, f);
        int b0 = i * bpc + 1, b1 = b0 + bpc - 1;
        mu_fmt_int(t, b0);
        if (b1 > b0) { mu_cat(t, "-", sizeof t); mu_fmt_int(a, b1); mu_cat(t, a, sizeof t); }
        cv_text_in(x, 92, w, &AF_S, t, T_MID, T_SURF);
    }
}

static void mu_panel_groove(ma_ui *u)
{
    const ma_groove *g = u->g;
    static const char *const rows[4] = {"KICK", "CLAP", "HAT", "BASS"};
    uint16_t m[4];
    m[0] = g->drum_last[MA_DR_KICK];
    m[1] = (uint16_t)(g->drum_last[MA_DR_SNARE] | g->drum_last[MA_DR_CLAP]);
    m[2] = (uint16_t)(g->drum_last[MA_DR_HAT] | g->drum_last[MA_DR_OPEN]);
    m[3] = g->hits_last[MA_BASS];
    for (int r = 0; r < 4; ++r) {
        int32_t y = 10 + r * 26;
        mu_label(10, y + 2, rows[r], T_MID);
        for (int k = 0; k < 16; ++k) {
            bool on = g->playing && ((m[r] >> k) & 1);
            int32_t x = 54 + k * 11;
            cv_rrect(x, y, 9, 16, 2, on ? (r == 3 ? T_ACCENT : T_THEME) : mu_well(), T_SURF);
        }
    }
    if (g->playing && g->step >= 0)
        cv_rect(54 + g->step * 11, 112, 9, 2, T_ACCENT);
}

static void mu_panel_drums(ma_ui *u)
{
    const ma_groove *g = u->g;
    mu_label(10, 6, g->s.snd[3] == MA_KIT_EURO ? "ONE-SHOTS AND THE CROWD" : "FELUCCA'S DRUM ENGINE", T_MID);
    static const char *const nm[5] = {"KICK", "CLAP", "HAT", "OPEN", "CRASH"};
    static const int LEN[5] = {70, 46, 12, 60, 100};
    static const uint8_t DR[5] = {MA_DR_KICK, MA_DR_CLAP, MA_DR_HAT, MA_DR_OPEN, MA_DR_CRASH};
    for (int i = 0; i < 5; ++i) {
        int32_t x = 10 + i * 45, y = 30, h = 56;
        int len = LEN[i] * (60 + g->s.snap * 4 / 10) / 100;
        bool hit = g->playing && ((g->drum_hits[DR[i]] | (i == 1 ? g->drum_hits[MA_DR_SNARE] : 0)) >> (g->step < 0 ? 0 : g->step)) & 1u;
        for (int k = 0; k < 40; ++k) {             /* the hit: a hard edge, then its decay */
            int32_t a = k < 2 ? h : h * (len - k * len / 40) / (len > 0 ? len : 1);
            a = a * mu_clampi(len * 2 - k * 3, 0, 100) / 100;
            if (a > 0) cv_rect(x + k, y + h - a, 1, a, k < 2 ? T_ACCENT : ux_mix(T_SURF, T_THEME, hit ? 100 : 70));
        }
        cv_text_on(x, 94, &AF_S, nm[i], T_TEXT, T_SURF);
    }
}

static void mu_panel_mix(ma_ui *u)
{
    const ma_groove *g = u->g;
    for (int i = 0; i < 4; ++i) {
        int32_t x = 32 + i * 50, top = 12, h = 90, v = g->s.lvl[i] * h / 100;
        cv_rrect(x, top, 18, h, 4, mu_well(), T_SURF);
        if (v > 0)
            cv_rrect(x, top + h - v, 18, v, 4, g->muted[i] && g->playing ? T_DIM : i == 3 ? T_TEXT : T_THEME, mu_well());
    }
}

static void mu_panel_space(ma_ui *u)
{
    const ma_groove *g = u->g;
    mu_label(10, 6, "ECHOES ON THE BEAT", T_MID);
    static const int GAP[MA_DTIME_COUNT] = {12, 24, 36, 48};   /* px a sixteenth .. a quarter, a beat 48 px */
    int gap = GAP[g->s.dtime % MA_DTIME_COUNT];
    int n = 1 + g->s.delay / 20;
    for (int k = 0; k <= n && 14 + k * gap < 226; ++k) {
        int32_t x = 14 + k * gap, h = 60 * (n + 1 - k) / (n + 1);
        cv_rrect(x, 90 - h, 8, h, 2, k == 0 ? T_THEME : ux_mix(T_SURF, T_THEME, 80 - k * 10), T_SURF);
    }
    for (int b = 0; b < 5; ++b)
        cv_rect(14 + b * 48, 96, 1, 6, T_MID);
}

/* the form: a bar a section, as tall as it is loud, the one playing in the accent, the key up over the last */
static void mu_panel_arc(ma_ui *u)
{
    const ma_groove *g = u->g;
    if (!g->s.arc) {
        mu_label(10, 30, "ARC OFF", T_THEME);
        mu_label(10, 50, "THE CHORUS ON AND ON:", T_MID);
        mu_label(10, 66, "REC BREAKS, SEQ KEYS UP", T_MID);
        return;
    }
    static const int LVL[MA_SECS] = {30, 52, 68, 88, 38, 100};
    for (int k = 0; k < MA_SECS; ++k) {
        int32_t x = 10 + k * 37, w = 33, h = LVL[k] * 74 / 100, top = 92 - h;
        bool now = g->playing && !g->brk && g->sec == k, past = g->playing && !g->brk && k < g->sec;
        uint16_t c = now ? T_ACCENT : past || !g->playing ? T_THEME : ux_mix(T_SURF, T_THEME, 45);
        cv_rect(x, top, w, h, c);
        if (now) {                                 /* how far into it */
            int len = ma_sec_len(g, k);
            int32_t p = (g->sec_bar * 16 + (g->step < 0 ? 0 : g->step)) * h / (len * 16 > 0 ? len * 16 : 1);
            cv_rect(x, 92 - p, w, 2, T_TEXT);
        }
        cv_text_on(x, 98, &AF_S, ma_sec_short(k), now ? T_ACCENT : T_MID, T_SURF);
        if (k == MA_SEC_CHORUS2 && g->s.keyup) {
            char t[4] = "+1";
            t[1] = (char)('0' + g->s.keyup);
            cv_text_in(x, top - 15, w, &AF_S, t, T_THEME, T_SURF);
        }
    }
}

/* the hook as a roll: REPEAT bars of its notes, low to high, the step playing marked; ITALO's arpeggio as a zigzag */
static void mu_panel_hook(ma_ui *u)
{
    const ma_groove *g = u->g;
    char t[32], a[8];
    int rep = ma_repeat_bars(&g->s);
    bool arp = MA_STYLE[g->s.style].hook == 1;     /* (HOOK_ARP) */
    if (g->s.speed == MA_SPEED_PAD) {              /* a pad: the chords held, a block each, the one sounding lit */
        mu_label(10, 4, "A PAD ON THE CHORDS", T_MID);
        int bpc = ma_bars_per_chord(&g->s), n = g->nloop < 4 ? g->nloop : 4, now = g->playing ? ma_chord_now(g) : -1;
        int32_t gap = 4, w = (224 - gap * (n - 1)) / (n > 0 ? n : 1);
        for (int i = 0; i < n; ++i) {
            int32_t x = 10 + i * (w + gap);
            cv_rrect(x, 30, w, 60, 4, i == now ? T_THEME : mu_well(), T_SURF);
            cv_text_in(x, 30 + CAP_IN(S, 60), w, &AF_S, g->loop[i].name, i == now ? T_INK : T_TEXT,
                       i == now ? T_THEME : mu_well());
        }
        char b[24], a2[8];
        mu_fmt_int(a2, bpc);
        mu_cpy(b, a2, sizeof b);
        mu_cat(b, bpc == 1 ? " BAR A CHORD" : " BARS A CHORD", sizeof b);
        cv_text_in(10, 100, 224, &AF_S, b, T_DIM, T_SURF);
        return;
    }
    if (arp) {
        mu_cpy(t, "AN ARPEGGIO THROUGH THE CHORD", sizeof t);
    } else {
        mu_cpy(t, "THE HOOK, ", sizeof t);
        mu_fmt_int(a, rep);
        mu_cat(t, a, sizeof t);
        mu_cat(t, rep == 1 ? " BAR" : " BARS", sizeof t);
    }
    mu_label(10, 4, t, T_MID);
    int32_t x0 = 10, w = 220, y0 = 24, h = 76, n = rep * 16;
    int lo = g->s.range >= 2 ? -3 : 0, hi = g->s.range >= 2 ? 10 : 7;
    for (int b = 0; b <= rep; ++b)
        cv_rect(x0 + b * w / rep - (b == rep), y0, 1, h, T_LINE);
    int pos = -1;
    if (g->playing && g->step >= 0) {
        int sb = g->s.arc ? g->sec_bar : g->bar;
        pos = (sb % rep) * 16 + g->step;
    }
    if (arp) {
        static const uint8_t ORD[8] = {0, 1, 2, 3, 4, 3, 2, 1};
        for (int k = 0; k < n; ++k) {
            int32_t x = x0 + k * w / n, y = y0 + h - 8 - ORD[k % 8] * (h - 12) / 4;
            cv_rrect(x + 1, y, w / n - 2 > 2 ? w / n - 2 : 2, 5, 2, k == pos ? T_ACCENT : T_THEME, T_SURF);
        }
    } else {
        for (int k = 0; k < n; ++k) {
            if (g->hook[k] == -128)
                continue;
            int len = g->hook_len[k];
            if (g->hook_tup[k]) {                  /* a tuplet: its notes evenly over its span, a run */
                int tn = g->hook_tup[k], dir = g->hook[k] >= 5 ? -1 : 1;
                bool on = pos >= k && pos < k + len;
                for (int j = 0; j < tn; ++j) {
                    int st = mu_clampi(g->hook[k] + dir * j, lo, hi);
                    int32_t x = x0 + k * w / n + j * (len * w / n) / tn, ww = (len * w / n) / tn - 1;
                    int32_t y = y0 + h - 7 - (st - lo) * (h - 10) / (hi - lo > 0 ? hi - lo : 1);
                    cv_rrect(x + 1, y, ww > 2 ? ww : 2, 5, 2, on ? T_ACCENT : T_TEXT, T_SURF);
                }
                continue;
            }
            int32_t x = x0 + k * w / n, ww = len * w / n - 2;
            int32_t y = y0 + h - 7 - (mu_clampi(g->hook[k], lo, hi) - lo) * (h - 10) / (hi - lo > 0 ? hi - lo : 1);
            bool on = pos >= k && pos < k + len;
            cv_rrect(x + 1, y, ww > 2 ? ww : 2, 5, 2, on ? T_ACCENT : T_THEME, T_SURF);
        }
    }
    if (pos >= 0)
        cv_rect(x0 + pos * w / n, y0 + h + 3, w / n > 2 ? w / n : 2, 2, T_ACCENT);
    if (!ma_in_chorus(g) && g->playing && !(g->s.arc && g->sec == MA_SEC_BREAK))
        cv_text_r(230, 4, &AF_S, "RESTS TILL THE CHORUS", T_DIM, T_SURF);
}

static void mu_panel_sets(ma_ui *u)
{
    for (int i = 0; i < MA_SETS; ++i) {
        int32_t col = i / 4, row = i % 4, x = 10 + col * 112, y = 8 + row * 27;
        bool sel = i == u->set_sel, used = (u->set_used >> i) & 1u;
        if (sel) cv_rrect(x - 4, y - 4, 110, 21, 3, T_THEME, T_SURF);
        char n[4];
        mu_fmt_int(n, i + 1);
        uint16_t bg = sel ? T_THEME : T_SURF;
        cv_text_on(x, y, &AF_S, n, sel ? T_INK : T_MID, bg);
        cv_text_fit(x + 14, y, &AF_S, used ? ma_style_name(u->sets[i].style) : "EMPTY", sel ? T_INK : used ? T_TEXT : T_DIM, bg, 88);
    }
}

/* MENU: what Manta is, and the CPU (the audio's load) as Tuna's MENU > SYSTEM > INFO shows it: a row, its value in
 * the theme and the unit small beside it, taken every MU_CPU_MS */
#define MU_CPU_MS 500
static void mu_panel_menu(ma_ui *u)
{
    char cpu[8];
    cv_text_on(30, 14, &AF_M, "MANTA " MA_VERSION, T_TEXT, T_SURF);
    cv_text_on(30, 38, &AF_S, "90S EURODANCE FOR THE FM-1", T_MID, T_SURF);
    cv_text_on(30, 54, &AF_S, "ON FELUCCA, AFTER TUNA", T_MID, T_SURF);
    cv_rect(30, 72, 180, 1, T_LINE);
    int32_t vy = 80;
    cv_text_on(30, vy, &AF_M, "CPU", T_TEXT, T_SURF);
    mu_fmt_int(cpu, mu_clampi(u->cpu_shown, 0, 100));
    cv_text_r(cv_text_r(210, vy + AF_M.asc - AF_S.asc, &AF_S, "%", T_MID, T_SURF) - 3, vy, &AF_M, cpu, T_THEME, T_SURF);
    cv_text_on(30, 102, &AF_S, "GPL-3.0, HOME CLOSES", T_DIM, T_SURF);
}

static void mu_draw_panel(ma_ui *u)
{
    mu_panel_begin();
    if (u->browse_ms > 0) {
        mu_panel_styles(u);
    } else {
        switch (u->page) {
        case MU_PG_PLAY: mu_panel_play(u); break;
        case MU_PG_SOUND: mu_panel_sound(u); break;
        case MU_PG_TONE: mu_panel_tone(u); break;
        case MU_PG_VOX: mu_panel_vox(u); break;
        case MU_PG_KEY: mu_panel_key(u); break;
        case MU_PG_GROOVE: mu_panel_groove(u); break;
        case MU_PG_DRUMS: mu_panel_drums(u); break;
        case MU_PG_MIX: mu_panel_mix(u); break;
        case MU_PG_SPACE: mu_panel_space(u); break;
        case MU_PG_ARC: mu_panel_arc(u); break;
        case MU_PG_HOOK: mu_panel_hook(u); break;
        case MU_PG_SETS: mu_panel_sets(u); break;
        case MU_PG_MENU: mu_panel_menu(u); break;
        }
    }
    cv_blit(0, MU_PANEL_Y);
}

static void mu_background(void)
{
    cv_begin(240, 40, T_BG);
    for (int y = 0; y < 240; y += 40)
        cv_blit(0, (uint32_t)y);
    if (ux.style == ST_LINE) {                    /* LINE: no cards, 1 px rules between the areas */
        cv_begin(240, 1, T_RULE);
        cv_blit(0, MU_HEAD + 1);
        cv_blit(0, MU_CARD_Y + MU_CARD_H + 1);
        cv_blit(0, MU_FOOT_Y - 3);
        cv_begin(1, MU_CARD_H, T_RULE);
        for (int c = 1; c < 4; ++c)
            cv_blit((uint32_t)(MU_CARD_X(c) - 2), MU_CARD_Y);
    }
}

void mu_draw(ma_ui *u)
{
    if (u->force)
        mu_background();
    mu_draw_head(u);
    for (int i = 0; i < 4; ++i)
        mu_draw_card(u, i, &MU_CARDS[u->page][i]);
    mu_draw_panel(u);
    mu_draw_foot(u);
    u->force = false;
    u->frame++;
}

void mu_draw_all(ma_ui *u)
{
    u->force = true;
    mu_draw(u);
}

/* ---- input --------------------------------------------------------------------------------------------------- */
static void mu_msg(ma_ui *u, const char *m)
{
    mu_cpy(u->msg, m, sizeof u->msg);
    u->msg_ms = 1500;
}

static void mu_goto_page(ma_ui *u, int pg)
{
    if (pg != u->page) {
        u->page = pg;
        u->force = true;
    }
    u->browse_ms = 0;
}

static void mu_apply_style(ma_ui *u)
{
    ux.style = (uint8_t)(u->g->s.look ? ST_LINE : ST_FLAT);
    palette_set((uint32_t)u->g->s.palette % NPALETTES);
    u->force = true;
}

void mu_init(ma_ui *u, ma_groove *g)
{
    memset(u, 0, sizeof *u);
    u->g = g;
    u->page = MU_PG_PLAY;
    u->hot = -1;
    u->batt = 3;
    for (int k = 0; k < 27; ++k)
        u->key_sent[k] = -1;
    mu_apply_style(u);
}

/* a set loaded: its settings, the menu's colour and look stay; at once (the sound follows in the glue) */
static void mu_load_set(ma_ui *u, int i)
{
    ma_groove *g = u->g;
    ma_settings s = u->sets[i];
    s.palette = g->s.palette;
    s.look = g->s.look;
    s.keysnap = g->s.keysnap;
    ma_settings_fix(&s);
    ma_all_off(g);
    g->s = s;
    g->next_style = g->next_song = -1;
    g->perf_up = 0;
    g->perf_next = -1;
    g->tr = 0;
    ma_rebuild(g);
    g->sound_sig++;
}

void mu_knob(ma_ui *u, int k, int d)
{
    ma_groove *g = u->g;
    char m[24], a[8];
    switch (k) {
    case MU_K_SELECT:
        g->s.bpm = (uint8_t)mu_clampi(g->s.bpm + d, 60, 200);
        u->hot = 9;
        u->hot_ms = 700;
        u->changed = true;
        return;
    case MU_K_PRESETS: {
        int cur = g->next_style >= 0 ? g->next_style : g->s.style;
        ma_queue_style(g, mu_wrap(cur + d, MA_STYLES));
        u->browse_ms = 1800;
        u->changed = true;
        return;
    }
    case MU_K_ALGO: {
        int cur = g->next_song >= 0 ? g->next_song : g->s.song;
        ma_queue_song(g, mu_wrap(cur + d, 1000));
        mu_cpy(m, g->playing ? "NEXT SONG " : "SONG ", sizeof m);
        mu_fmt_int(a, mu_wrap(cur + d, 1000) + 1);
        mu_cat(m, a, sizeof m);
        mu_msg(u, m);
        u->changed = true;
        return;
    }
    case MU_K1: case MU_K2: case MU_K3: case MU_K4: {
        const mu_cdef *c = &MU_CARDS[u->page][k - MU_K1];
        u->hot = k - MU_K1;
        u->hot_ms = 700;
        if (c->kind == SC_OFF)
            return;
        if (c->kind == SC_LOAD) {                  /* turned right: the set, at once */
            if (d > 0 && ((u->set_used >> u->set_sel) & 1u)) {
                mu_load_set(u, u->set_sel);
                mu_cpy(m, "SET ", sizeof m);
                mu_fmt_int(a, u->set_sel + 1);
                mu_cat(m, a, sizeof m);
                mu_cat(m, " LOADED", sizeof m);
                mu_msg(u, m);
                mu_apply_style(u);
                u->changed = true;
            } else if (d > 0) {
                mu_msg(u, "THAT SET IS EMPTY");
            }
            return;
        }
        if (c->kind == SC_STORE) {                 /* turned right: what plays now into the set */
            if (d > 0) {
                u->sets[u->set_sel] = g->s;
                u->set_used |= (uint8_t)(1u << u->set_sel);
                u->sets_changed = true;
                mu_cpy(m, "STORED IN SET ", sizeof m);
                mu_fmt_int(a, u->set_sel + 1);
                mu_cat(m, a, sizeof m);
                mu_msg(u, m);
            }
            return;
        }
        if (c->kind == SC_CLEAR) {                 /* three turns right within a second */
            if (d > 0) {
                u->clear_turns++;
                u->clear_ms = 1000;
                if (u->clear_turns >= 3) {
                    u->set_used &= (uint8_t)~(1u << u->set_sel);
                    u->sets_changed = true;
                    u->clear_turns = 0;
                    mu_msg(u, "SET CLEARED");
                } else {
                    mu_msg(u, "TURN ON TO CLEAR");
                }
            }
            return;
        }
        int v = mu_get(u, c) + d * c->step;
        if (c->kind == SC_PAL)
            v = mu_wrap(v, (int)NPALETTES);
        else if (c->kind == SC_SND || c->kind == SC_KEY || c->kind == SC_LOOP)
            v = c->lo + mu_wrap(v - c->lo, c->hi - c->lo + 1);
        v = mu_clampi(v, c->lo, c->hi);
        mu_set(u, c, v);
        if (c->off == SS_(key) || c->off == SS_(scale) || c->off == SS_(chords) || c->off == SS_(bars) ||
            c->off == SS_(speed) || c->off == SS_(range) || c->off == SS_(repeat))
            ma_rebuild(g);
        if (c->off == SS_(scale))
            mu_msg(u, SN_SCALE_LONG[g->s.scale % MA_SCALES]);
        if (c->off == SS_(palette) || c->off == SS_(look))
            mu_apply_style(u);
        if (c->off == SS_(arc) && g->playing) {    /* ARC turned on while playing: from a chorus */
            g->sec = MA_SEC_CHORUS;
            g->sec_bar = 0;
        }
        u->changed = true;
        return;
    }
    default:
        return;
    }
}

void mu_button(ma_ui *u, int b, bool down)
{
    ma_groove *g = u->g;
    if (b == MU_B_HOME) {
        if (down) {
            u->home_down = true;
            u->home_ms = 0;
        } else if (u->home_down) {
            u->home_down = false;
            if (u->home_ms < MU_MENU_HOLD_MS) mu_goto_page(u, MU_PG_PLAY);
        }
        return;
    }
    if (b == MU_B_REC) {                           /* BREAK: down and up both count */
        if (!g->playing) {
            if (down) mu_msg(u, "PLAY FIRST");
            return;
        }
        ma_break(g, down);
        return;
    }
    if (!down)
        return;
    switch (b) {
    case MU_B_PLAY: ma_play(g, !g->playing); break;
    case MU_B_SEQ:
        if (g->playing) ma_keyup(g);
        else mu_msg(u, "PLAY FIRST");
        break;
    case MU_B_OCTDN: case MU_B_OCTUP: {
        char m[16], a[8];
        u->oct = mu_clampi(u->oct + (b == MU_B_OCTUP ? 1 : -1), -2, 2);
        mu_cpy(m, "KEYS OCT ", sizeof m);
        if (u->oct > 0) mu_cat(m, "+", sizeof m);
        mu_fmt_int(a, u->oct);
        mu_cat(m, a, sizeof m);
        mu_msg(u, m);
        break;
    }
    case MU_B_FX: mu_goto_page(u, u->page == MU_PG_SOUND ? MU_PG_TONE : MU_PG_SOUND); break;
    case MU_B_LFO: mu_goto_page(u, MU_PG_VOX); break;
    case MU_B_SCL: mu_goto_page(u, MU_PG_KEY); break;
    case MU_B_ARP: mu_goto_page(u, MU_PG_GROOVE); break;
    case MU_B_EDIT: mu_goto_page(u, MU_PG_DRUMS); break;
    case MU_B_GLO: mu_goto_page(u, u->page == MU_PG_MIX ? MU_PG_SPACE : MU_PG_MIX); break;
    case MU_B_ENV: mu_goto_page(u, u->page == MU_PG_ARC ? MU_PG_HOOK : MU_PG_ARC); break;
    case MU_B_SAVE: mu_goto_page(u, MU_PG_SETS); break;
    default: break;
    }
}

void mu_key(ma_ui *u, int note, bool down)
{
    if (note < MU_KEY_LO || note > MU_KEY_HI)
        return;
    int k = note - MU_KEY_LO;
    uint32_t bit = 1u << k;
    if (down) {
        u->keys_down |= bit;
        u->key_sent[k] = note + 12 * u->oct;      /* (the octave it was pressed in: its release lets go of the same) */
        ma_key(u->g, k, u->key_sent[k], true);
    } else {
        u->keys_down &= ~bit;
        ma_key(u->g, k, u->key_sent[k], false);
        u->key_sent[k] = -1;
    }
}

void mu_tick(ma_ui *u, int ms)
{
    ma_groove *g = u->g;
    if ((u->cpu_ms -= ms) <= 0) {                  /* MENU's CPU: a reading every MU_CPU_MS, not every frame */
        u->cpu_ms = MU_CPU_MS;
        u->cpu_shown = u->cpu;
    }
    ma_seen n;
    while (ma_feed_pop(g, &n))
        ;                                          /* (the panels read the groove's own state) */
    if (g->msg_new) {
        g->msg_new = 0;
        mu_msg(u, g->msg);
    }
    if (u->hot_ms > 0) u->hot_ms -= ms;
    if (u->clear_ms > 0 && (u->clear_ms -= ms) <= 0) u->clear_turns = 0;
    if (u->browse_ms > 0 && (u->browse_ms -= ms) <= 0) {
        u->browse_ms = 0;
        u->force = true;
    }
    if (u->msg_ms > 0) u->msg_ms -= ms;
    if (u->home_down) {
        u->home_ms += ms;
        if (u->home_ms >= MU_MENU_HOLD_MS && u->page != MU_PG_MENU) {
            mu_goto_page(u, MU_PG_MENU);
            u->home_down = false;
        }
    }
}
