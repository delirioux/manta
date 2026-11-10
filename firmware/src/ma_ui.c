/* SPDX-License-Identifier: GPL-3.0-only */
/* MANTA screen and controls (ma_ui.h): Manta's pages in Sill's frame (Tuna's, Daurade's layout), drawn with Felucca's gfx.c.
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
enum { SC_NUM, SC_LIST, SC_SND, SC_KEY, SC_SIGNED, SC_LOOP, SC_VALS, SC_PAL, SC_ANGLE, SC_SETSEL, SC_LOAD, SC_STORE,
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
static const char *const SN_CLAVE[] = {"3-2", "2-3"};
static const char *const SN_SOLO[] = {"OFF", "PHRASE", "KEYS"};
static const char *const SN_HITS[] = {"OFF", "4 BARS", "8 BARS", "16 BARS"};
static const char *const SN_MUTE[] = {"OPEN", "CONE", "CUP", "HARMON", "WAH", "AUTO"};   /* (CONE: the straight mute) */
static const char *const SN_PHRASE[] = {"GEN", "TRAD", "MIX"};
static const char *const SN_P3MODE[] = {"OFF", "FIXED", "ORBIT", "STEP"};
static const char *const SN_FILLS[] = {"OFF", "8 BARS", "4 BARS", "2 BARS"};
static const char *const SN_PATTERN[] = {"1", "2", "3", "4"};
static const char *const SN_CONGA[] = {"OFF", "AUTO", "TUMBAO", "MARCHA", "BOLERO"};
static const char *const SN_BONGO[] = {"OFF", "AUTO", "MARTIL", "BELL"};   /* (the martillo) */
static const char *const SN_TIMBAL[] = {"OFF", "AUTO", "SHELL", "BELL"};    /* (SHELL: the cascara) */
static const char *const SN_EXTRA[] = {"OFF", "AUTO", "GUIRO", "SHAKER", "CABASA", "WASH"};   /* (the washboard) */
static const char *const SN_SCALE[] = {"MIN", "MAJ", "DOR", "HARM"};
static const char *const SN_SCALE_LONG[] = {"MINOR", "MAJOR", "DORIAN", "HARMONIC MINOR"};
static const uint8_t SV_BARS[] = {1, 2, 4}, SV_SECTION[] = {8, 16};
#define C_NUM(l, f) {l, SS_(f), SC_NUM, 0, 100, 5, 0, 0}
#define C_LIST(l, f, n) {l, SS_(f), SC_LIST, 0, (int16_t)(sizeof n / sizeof n[0]) - 1, 1, n, 0}
#define C_VALS(l, f, v) {l, SS_(f), SC_VALS, 0, (int16_t)(sizeof v / sizeof v[0]) - 1, 1, 0, v}
#define C_SND(l, p) {l, (uint8_t)(SS_(snd) + (p)), SC_SND, 0, (p) == 3 ? MA_STYLES - 1 : MA_SOUNDS - 1, 1, 0, 0}
#define C_OFF {"", 0, SC_OFF, 0, 0, 0, 0, 0}
static const mu_cdef MU_CARDS[MU_PAGES][4] = {
    [MU_PG_PLAY] = {C_NUM("HEAT", heat), C_LIST("CLAVE", clave, SN_CLAVE), C_LIST("SOLO", solo, SN_SOLO),
                    C_LIST("HITS", hits, SN_HITS)},
    [MU_PG_SOUND] = {C_SND("PIANO", 0), C_SND("BASS", 1), C_SND("HORNS", 2), C_SND("PERC", 3)},
    [MU_PG_SOLO] = {C_SND("INSTR", 2), C_LIST("MUTE", mute, SN_MUTE), C_LIST("PHRASE", phrase, SN_PHRASE),
                    C_NUM("BELL", bell)},
    [MU_PG_PAN3D] = {C_LIST("MODE", p3mode, SN_P3MODE), {"ANGLE", SS_(p3angle), SC_ANGLE, 0, 35, 1, 0, 0},
                     C_NUM("HEIGHT", p3height), C_NUM("DIST", p3dist)},
    [MU_PG_PERC] = {C_LIST("CONGAS", conga, SN_CONGA), C_LIST("BONGO", bongo, SN_BONGO),
                    C_LIST("TIMBAL", timbal, SN_TIMBAL), C_LIST("EXTRA", extra, SN_EXTRA)},
    [MU_PG_KEY] = {{"KEY", SS_(key), SC_KEY, 0, 11, 1, 0, 0}, C_LIST("SCALE", scale, SN_SCALE),
                   {"CHORDS", SS_(chords), SC_LOOP, 0, MA_LOOPS - 1, 1, 0, 0}, C_VALS("BARS", bars, SV_BARS)},
    [MU_PG_GROOVE] = {{"SWING", SS_(swing), SC_NUM, 0, 60, 2, 0, 0}, C_LIST("PIANO", montuno, SN_PATTERN),
                      C_LIST("BASS", tumbao, SN_PATTERN), C_LIST("FILLS", fills, SN_FILLS)},
    [MU_PG_MIX] = {C_NUM("PIANO", lvl[0]), C_NUM("BASS", lvl[1]), C_NUM("HORNS", lvl[2]), C_NUM("PERC", lvl[3])},
    [MU_PG_SPACE] = {C_NUM("ROOM", room), C_NUM("PLATE", plate), C_NUM("WARMTH", warmth), C_NUM("WIDTH", width)},
    [MU_PG_ARC] = {C_LIST("ARC", arc, SN_ONOFF), C_VALS("VERSO", verse, SV_SECTION), C_VALS("MON", mbars, SV_SECTION),
                   C_LIST("MAMBO", mambo, SN_ONOFF)},
    [MU_PG_SETS] = {{"SET", 0, SC_SETSEL, 0, MA_SETS - 1, 1, 0, 0}, {"LOAD", 0, SC_LOAD, 0, 0, 1, 0, 0},
                    {"STORE", 0, SC_STORE, 0, 0, 1, 0, 0}, {"CLEAR", 0, SC_CLEAR, 0, 0, 1, 0, 0}},
    [MU_PG_MENU] = {{"COLOR", SS_(palette), SC_PAL, 0, 10, 1, 0, 0}, C_LIST("LOOK", look, SN_LOOK),
                    C_LIST("SNAP", keysnap, SN_ONOFF), C_OFF},
};
static const char *const MU_PAGE_NAMES[MU_PAGES] = {"PLAY", "SOUND", "SOLO", "PAN 3D", "PERC", "KEY", "GROOVE", "MIX",
                                                    "SPACE", "ARC", "SETS", "MENU"};
const char *mu_page_name(int page) { return page >= 0 && page < MU_PAGES ? MU_PAGE_NAMES[page] : ""; }

/* PAN 3D's angle as words: FRONT, R10 .. R170 (to the right), BACK, L170 .. L10 */
static void mu_angle_name(int v, char *t, unsigned n)
{
    char a[8];
    v = mu_wrap(v, 36);
    if (v == 0) { mu_cpy(t, "FRONT", n); return; }
    if (v == 18) { mu_cpy(t, "BACK", n); return; }
    mu_cpy(t, v < 18 ? "R" : "L", n);
    mu_fmt_int(a, (v < 18 ? v : 36 - v) * 10);
    mu_cat(t, a, n);
}

static int mu_get(const ma_ui *u, const mu_cdef *c)
{
    const uint8_t *b = (const uint8_t *)&u->g->s + c->off;
    if (c->kind == SC_SIGNED)
        return (int)*(const int8_t *)b;
    if (c->kind == SC_VALS) {
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
    case SC_ANGLE: mu_angle_name(v, t, n); break;
    case SC_VALS: {
        int x = c->vals[mu_clampi(v, c->lo, c->hi)];
        mu_fmt_int(t, x);
        if (c->off == SS_(verse) || c->off == SS_(mbars)) mu_cat(t, " BARS", n);
        break;
    }
    case SC_PAL: mu_cpy(t, UI_PALETTES[mu_wrap(v, (int)NPALETTES)].name, n); break;
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

/* a card greyed: PAN 3D while the solo is off (the horns' section has no place to move) */
static bool mu_card_grey(const ma_ui *u, const mu_cdef *c)
{
    const ma_settings *s = &u->g->s;
    if (u->page == MU_PG_PAN3D && c->off != SS_(p3mode))
        return s->p3mode == MA_P3_OFF;
    return false;
}

static void mu_draw_card(ma_ui *u, int i, const mu_cdef *c)
{
    bool hot = u->hot == i && u->hot_ms > 0, grey = mu_card_grey(u, c);
    uint16_t lc = hot ? T_ACCENT : grey ? T_DIM : T_MID, vc = hot ? T_ACCENT : grey ? T_DIM : T_THEME;
    char val[24];
    mu_card_value(u, c, val, sizeof val);
    int ratio = -1;
    if ((c->kind == SC_NUM || c->kind == SC_SIGNED || c->kind == SC_VALS || c->kind == SC_SETSEL || c->kind == SC_ANGLE ||
         c->kind == SC_LOOP || c->kind == SC_KEY || c->kind == SC_SND) && c->hi > c->lo)
        ratio = (mu_get(u, c) - c->lo) * 1000 / (c->hi - c->lo);
    uint32_t sig = mu_hash(2166136261u + (uint32_t)i * 7u + ux.gen * 131u + (uint32_t)hot * 3u + (uint32_t)u->page * 977u +
                           (uint32_t)grey * 5u, c->label);
    sig = mu_hash(sig, val) + (uint32_t)(ratio + 1) * 31u;
    if (!u->force && sig == u->sig_card[i])
        return;
    u->sig_card[i] = sig;
    cv_begin(MU_CARD_W, MU_CARD_H, T_BG);
    cv_rrect(0, 0, MU_CARD_W, MU_CARD_H, 4, T_SURF, T_BG);
    if (c->kind == SC_OFF) {
        cv_rect(MU_CARD_W / 2 - 6, MU_CARD_H / 2, 12, 1, T_DIM);
    } else {
        cv_text_fit(5, 3, &AF_S, c->label, lc, T_SURF, MU_CARD_W - 6);
        const aafont_t *vf = text_w(&AF_M, val) <= MU_CARD_W - 6 ? &AF_M : &AF_S;
        cv_text_fit(5, vf == &AF_M ? 17 : 19, vf, val, vc, T_SURF, MU_CARD_W - 6);
        if (ratio >= 0) {
            int32_t w = MU_CARD_W - 10, x0 = 5, y = 37, r = mu_clampi(ratio, 0, 1000) * w / 1000;
            cv_rect(x0, y, w, 3, T_LINE);
            cv_rect(x0, y, r > 0 ? r : 1, 3, vc);
        }
    }
    cv_blit((uint32_t)MU_CARD_X(i), MU_CARD_Y);
}

/* ---- the header: playing, the tempo, a word (the section, NEXT, BAND HIT), a badge (the clave's side, HELD) ----- */
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
    } else if (g->playing && g->desc) {
        mu_cpy(t, "DESCARGA", n);
    } else if (g->playing) {
        mu_cpy(t, g->s.arc ? ma_sec_name(g->sec) : g->mambo_live ? "MAMBO" : "MONTUNO", n);
    }
}
static void mu_head_badge(const ma_ui *u, char *t, unsigned n)
{
    const ma_groove *g = u->g;
    t[0] = 0;
    if (!g->playing)
        return;
    if (g->desc && g->desc_held)
        mu_cpy(t, "HELD", n);
    else
        mu_cpy(t, ma_clave_side(g, g->bar) == 3 ? "3" : "2", n);   /* the clave's side this bar */
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
        if (g->s.arc && !g->desc) {
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


/* sin of x turns without libm (a parabola refined, as ma_groove.c's) */
static float mu_sin(float x)
{
    x -= (float)(int)x;
    if (x < 0.0f) x += 1.0f;
    float y = x < 0.5f ? x * 2.0f : (x - 0.5f) * 2.0f, q = 4.0f * y * (1.0f - y);
    q = q * (0.775f + 0.225f * q);
    return x < 0.5f ? q : -q;
}
static void mu_circle(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t px = cx, py = cy - r;
    for (int k = 1; k <= 32; ++k) {
        float t = (float)k / 32.0f;
        int32_t x = cx + (int32_t)(mu_sin(t) * (float)r), y = cy - (int32_t)(mu_sin(t + 0.25f) * (float)r);
        cv_line_t(px, py, x, y, c, 1);
        px = x;
        py = y;
    }
}

/* the loop's chords, the one sounding filled */
static void mu_chord_strip(const ma_groove *g, int32_t y, int now)
{
    int n = g->nloop < 4 ? g->nloop : 4;
    int32_t gap = 5, w = (224 - gap * (n - 1)) / (n > 0 ? n : 1);
    for (int i = 0; i < n; ++i) {
        int32_t x = 10 + i * (w + gap);
        if (i == now) {
            cv_rrect(x, y, w, 18, 3, T_THEME, T_SURF);
            cv_text_in(x, y + CAP_IN(S, 18), w, &AF_S, g->loop[i].name, T_INK, T_THEME);
        } else {
            cv_rrect(x, y, w, 18, 3, mu_well(), T_SURF);
            cv_rrect(x + 1, y + 1, w - 2, 16, 3, T_SURF, mu_well());
            cv_text_in(x, y + CAP_IN(S, 18), w, &AF_S, g->loop[i].name, T_TEXT, T_SURF);
        }
    }
}

/* the form as a strip: its sections, the one playing in the accent, how far into it under it */
static void mu_form_strip(const ma_groove *g, int32_t y)
{
    for (int k = 0; k < MA_SECS; ++k) {
        int32_t x = 10 + k * 22, w = 19;
        bool skip = g->s.arc && !g->s.mambo && (k == MA_SEC_MBO || k == MA_SEC_MONA);
        bool now = g->playing && g->s.arc && !g->desc && g->sec == k;
        bool past = g->playing && g->s.arc && !g->desc && k < g->sec;
        if (!g->s.arc)
            now = k == (g->mambo_live ? MA_SEC_MBO : MA_SEC_MON);
        uint16_t c = now ? T_ACCENT : past ? ux_mix(T_SURF, T_THEME, 55) : skip ? T_LINE : mu_well();
        cv_rect(x, y, w, 3, c);
        if (now && g->playing && g->s.arc) {
            int len = ma_sec_len(g, k);
            int32_t p = (g->sec_bar * 16 + (g->step < 0 ? 0 : g->step)) * w / (len * 16 > 0 ? len * 16 : 1);
            cv_rect(x, y + 4, p > 0 ? p : 1, 1, T_ACCENT);
        }
        cv_text_on(x, y + 8, &AF_S, ma_sec_short(k), now ? T_ACCENT : skip ? T_DIM : T_MID, T_SURF);
    }
}

/* the bar as it plays: a column a sixteenth, as tall as the parts that sounded on it, the clave's strokes in the
 * palm-green second colour (the theme) and a dot under them */
static void mu_step_meter(const ma_groove *g, int32_t y, int32_t h)
{
    int now = g->playing && g->step >= 0 ? g->step : -1;
    int bar = g->bar < 0 ? 0 : g->bar;
    for (int k = 0; k < 16; ++k) {
        int n = 0;
        for (int p = 0; p < MA_PARTS; ++p) {
            uint16_t bits = k <= now ? g->hits[p] : g->hits_last[p];
            n += (bits >> k) & 1u;
        }
        if (n > 4)
            n = 4;
        bool cl = ma_clave_hit(g, bar, k);
        int32_t x = 10 + k * 14, bh = n ? 6 + n * (h - 6) / 4 : 2;
        uint16_t c = k == now ? T_ACCENT : cl ? T_THEME : ux_mix(T_SURF, T_THEME, 60);
        cv_rect(x, y + h - bh, 10, bh, n ? c : T_LINE);
        cv_rrect(x + 3, y + h + 4, 4, 4, 2, cl ? T_THEME : T_LINE, T_SURF);
    }
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
    if (g->desc) {                                 /* the descarga: the percussion alone, the band back in */
        static const char *const parts[4] = {"PIANO", "BASS", "HORNS", "PERC"};
        cv_text_on(10, 6, &AF_L, "DESCARGA", T_ACCENT, T_SURF);
        bool roll = g->desc_end >= 0 && g->bar == g->desc_end - 1;
        cv_text_r(230, mu_yb_s(10), &AF_S, g->desc_held ? "HELD" : roll ? "ROLL" : "BACK IN", T_TEXT, T_SURF);
        for (int i = 0; i < 4; ++i) {
            int32_t y = 40 + i * 15;
            bool on = i == MA_PERC;
            mu_label(10, y, parts[i], on ? T_TEXT : T_DIM);
            if (on)
                cv_rect(66, y + 4, 162, 3, T_THEME);
            else
                cv_rect(66, y + 5, 162, 1, T_LINE);
        }
        if (!g->desc_held)
            mu_label(10, 102, "TIMBAL ROLL > THE BAND", T_ACCENT);
        return;
    }
    mu_chord_strip(g, 5, ma_chord_now(g));
    mu_label(10, 28, ma_clave_side(g, g->bar) == 3 ? "CLAVE 3 SIDE" : "CLAVE 2 SIDE", T_DIM);
    mu_step_meter(g, 40, 40);
    mu_form_strip(g, 96);
}

/* PRESETS turned: the ten styles, the queued one marked */
static void mu_panel_styles(ma_ui *u)
{
    const ma_groove *g = u->g;
    int sel = g->next_style >= 0 ? g->next_style : g->s.style;
    mu_label(10, 5, "CALIENTE", T_MID);
    mu_label(124, 5, "NOCHE", T_MID);
    for (int i = 0; i < MA_STYLES; ++i) {
        int col = MA_STYLE[i].side, row = 0;
        for (int k = 0; k < i; ++k) row += MA_STYLE[k].side == col;
        int32_t x = col ? 120 : 6, y = 21 + row * 16;
        bool now = i == g->s.style, q = i == sel && g->next_style >= 0;
        if (q || (i == sel && !g->playing)) {
            cv_rrect(x, y - 2, 110, 15, 3, T_ACCENT, T_SURF);
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
    static const char *const nm[4] = {"PIANO", "BASS", "HORNS", "PERC"};
    static const char *const role[4] = {"MONTUNO", "TUMBAO", "SECTION AND SOLO", "THE STYLE'S SET"};
    for (int i = 0; i < 4; ++i) {
        int32_t y = 6 + i * 28;
        bool hot = u->hot == i && u->hot_ms > 0;
        cv_text_on(10, y, &AF_S, nm[i], hot ? T_ACCENT : T_MID, T_SURF);
        cv_text_fit(64, y, &AF_S, mas_name(i, g->s.snd[i]), T_TEXT, T_SURF, 164);
        uint16_t bits = g->playing ? g->hits_last[i] : 0;
        if (g->playing) {
            for (int k = 0; k < 16; ++k) {
                bool on = (bits >> k) & 1;
                cv_rect(64 + k * 10, y + 16, 7, 2, on ? (hot ? T_ACCENT : T_THEME) : T_LINE);
            }
        } else {
            cv_text_on(64, y + 13, &AF_S, role[i], T_DIM, T_SURF);
        }
    }
}

/* SOLO: the phrase playing as a roll (four bars), what plays it, the solo's note lit */
static void mu_panel_solo(ma_ui *u)
{
    const ma_groove *g = u->g;
    mu_label(10, 4, g->keys ? "YOU HAVE THE SOLO" : g->s.solo == MA_SOLO_PHRASE ? "THE PHRASE, 4 BARS" :
                    g->s.solo == MA_SOLO_KEYS ? "THE SOLO IS ON THE KEYS" : "SOLO OFF", T_MID);
    if (g->s.phrase != MA_PHR_GEN)
        cv_text_r(230, 4, &AF_S, "TRAD: SOON, GEN NOW", T_DIM, T_SURF);
    int32_t x0 = 10, w = 220, y0 = 24, h = 72, n = 64;
    int lo = -2, hi = 9;
    for (int b = 0; b <= 4; ++b)
        cv_rect(x0 + b * w / 4 - (b == 4), y0, 1, h, T_LINE);
    int pos = -1;
    if (g->playing && g->step >= 0)
        pos = ((g->s.arc ? g->sec_bar : g->bar) % 4) * 16 + g->step;
    bool lit = g->playing && (float)(g->clock - g->solo_on_clock) < g->sr * 0.25f;
    for (int k = 0; k < n; ++k) {
        if (g->phr[k] == -128)
            continue;
        int len = g->phr_len[k];
        int32_t x = x0 + k * w / n, ww = len * w / n - 2;
        int32_t y = y0 + h - 7 - (mu_clampi(g->phr[k], lo, hi) - lo) * (h - 10) / (hi - lo);
        bool on = pos >= k && pos < k + len && lit;
        cv_rrect(x + 1, y, ww > 2 ? ww : 2, 5, 2, on ? T_ACCENT : T_THEME, T_SURF);
    }
    if (pos >= 0)
        cv_rect(x0 + pos * w / n, y0 + h + 3, w / n > 2 ? w / n : 2, 2, T_ACCENT);
    char t[32];
    mu_cpy(t, mas_name(MA_HORNS, g->s.snd[MA_HORNS]), sizeof t);
    mu_cat(t, ", ", sizeof t);
    mu_cat(t, SN_MUTE[g->s.mute % MA_MUTE_COUNT], sizeof t);
    mu_label(10, 104, t, T_DIM);
}

/* PAN 3D: the top view, you in the middle, the solo where it is; the side view, its height */
static void mu_panel_pan3d(ma_ui *u)
{
    const ma_groove *g = u->g;
    int32_t cx = 62, cy = 62, r = 46;
    mu_circle(cx, cy, r, T_LINE);
    cv_text_in(cx - 10, cy - r - 2 - 11, 20, &AF_S, "F", T_DIM, T_SURF);
    cv_rrect(cx - 6, cy - 6, 13, 13, 6, T_MID, T_SURF);
    if (g->s.p3mode == MA_P3_OFF) {
        mu_label(130, 30, "OFF:", T_ACCENT);
        mu_label(130, 46, "THE SOLO IN", T_DIM);
        mu_label(130, 62, "THE HORNS' PLACE", T_DIM);
        mu_label(130, 86, "MODE TURNS IT ON", T_DIM);
        return;
    }
    float a = g->p3_angle / 360.0f;
    float s = mu_sin(a), c = mu_sin(a + 0.25f);
    float dist = 0.45f + 0.55f * (float)g->s.p3dist / 100.0f;
    int32_t px = cx + (int32_t)(s * (float)r * dist), py = cy - (int32_t)(c * (float)r * dist);
    bool lit = g->playing && (float)(g->clock - g->solo_on_clock) < g->sr * 0.25f;
    cv_rrect(px - 5, py - 5, 11, 11, 5, lit ? T_ACCENT : T_THEME, T_SURF);
    /* the side view: the height */
    int32_t sx = 186, sy = 62;
    cv_rect(sx - 30, sy, 60, 1, T_LINE);
    cv_rrect(sx - 5, sy - 5, 11, 11, 5, T_MID, T_SURF);
    int32_t hy = sy - (int32_t)(((float)g->s.p3height - 50.0f) / 50.0f * 30.0f);
    cv_rrect(sx + 16, hy - 4, 9, 9, 4, T_THEME, T_SURF);
    mu_label(150, 100, "HEADPHONES", T_DIM);
}

/* PERC: every lane over the bar as the style plays it, the step lit */
static void mu_panel_perc(ma_ui *u)
{
    const ma_groove *g = u->g;
    static const char *const nm[6] = {"CONGA", "BONGO", "TIMBAL", "EXTRA", "CLAVE", "OTHER"};
    int bar = g->bar < 0 ? 0 : g->bar;
    for (int r = 0; r < 6; ++r) {
        int32_t y = 5 + r * 19;
        uint32_t bits = ma_lane_bits(g, r, bar);
        mu_label(10, y + 2, nm[r], bits ? T_MID : T_DIM);
        for (int k = 0; k < 16; ++k) {
            int32_t x = 62 + k * 10;
            bool on = (bits >> k) & 1u, now = g->playing && g->step == k;
            cv_rrect(x, y, 8, 13, 2, on ? (now ? T_ACCENT : r == 4 ? T_THEME : ux_mix(T_SURF, T_ACCENT, 70)) : mu_well(), T_SURF);
        }
    }
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

/* GROOVE: the piano's, the bass's and the clave's last bar */
static void mu_panel_groove(ma_ui *u)
{
    const ma_groove *g = u->g;
    static const char *const rows[4] = {"PIANO", "BASS", "HORNS", "CLAVE"};
    uint16_t m[4];
    m[0] = g->hits_last[MA_PIANO];
    m[1] = g->hits_last[MA_BASS];
    m[2] = g->hits_last[MA_HORNS];
    m[3] = (uint16_t)ma_lane_bits(g, 4, g->bar < 1 ? 0 : g->bar - 1);
    for (int r = 0; r < 4; ++r) {
        int32_t y = 10 + r * 26;
        mu_label(10, y + 2, rows[r], T_MID);
        for (int k = 0; k < 16; ++k) {
            bool on = (g->playing || r == 3) && ((m[r] >> k) & 1);
            int32_t x = 58 + k * 11;
            cv_rrect(x, y, 9, 16, 2, on ? (r == 3 ? T_THEME : r == 1 ? T_ACCENT : ux_mix(T_SURF, T_ACCENT, 70)) : mu_well(), T_SURF);
        }
    }
    if (g->playing && g->step >= 0)
        cv_rect(58 + g->step * 11, 112, 9, 2, T_ACCENT);
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

/* SPACE: the room's size, the plate on the piano and the horns, the warmth, the width as bars */
static void mu_panel_space(ma_ui *u)
{
    const ma_groove *g = u->g;
    static const char *const nm[4] = {"ROOM", "PLATE", "WARMTH", "WIDTH"};
    const uint8_t v[4] = {g->s.room, g->s.plate, g->s.warmth, g->s.width};
    for (int i = 0; i < 4; ++i) {
        int32_t y = 10 + i * 26, w = v[i] * 150 / 100;
        mu_label(10, y + 2, nm[i], T_MID);
        cv_rrect(70, y, 150, 14, 3, mu_well(), T_SURF);
        if (w > 4)
            cv_rrect(70, y, w, 14, 3, i == 1 ? T_ACCENT : T_THEME, mu_well());
    }
}

/* ARC: a bar a section, as tall as it is full, the one playing in the accent */
static void mu_panel_arc(ma_ui *u)
{
    const ma_groove *g = u->g;
    if (!g->s.arc) {
        mu_label(10, 30, "ARC OFF", T_THEME);
        mu_label(10, 50, "THE MONTUNO ON AND ON:", T_MID);
        mu_label(10, 66, "SEQ BRINGS THE MAMBO IN AND OUT", T_MID);
        mu_label(10, 82, "REC HITS THE CLAVE", T_MID);
        return;
    }
    static const int LVL[MA_SECS] = {40, 60, 55, 15, 65, 70, 80, 100, 95, 60};
    for (int k = 0; k < MA_SECS; ++k) {
        int32_t x = 10 + k * 22, w = 19, h = LVL[k] * 74 / 100, top = 92 - h;
        bool skip = !g->s.mambo && (k == MA_SEC_MBO || k == MA_SEC_MONA);
        bool now = g->playing && !g->desc && g->sec == k, past = g->playing && !g->desc && k < g->sec;
        uint16_t c = skip ? T_LINE : now ? T_ACCENT : past || !g->playing ? T_THEME : ux_mix(T_SURF, T_THEME, 45);
        cv_rect(x, top, w, h, c);
        if (now) {
            int len = ma_sec_len(g, k);
            int32_t p = (g->sec_bar * 16 + (g->step < 0 ? 0 : g->step)) * h / (len * 16 > 0 ? len * 16 : 1);
            cv_rect(x, 92 - p, w, 2, T_TEXT);
        }
        cv_text_on(x, 98, &AF_S, ma_sec_short(k), now ? T_ACCENT : skip ? T_DIM : T_MID, T_SURF);
    }
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

/* MENU: what Manta is, and the CPU (the audio's load) as Tuna's MENU > SYSTEM > INFO shows it */
#define MU_CPU_MS 500
static void mu_panel_menu(ma_ui *u)
{
    char cpu[8];
    cv_text_on(30, 14, &AF_M, "MANTA " MA_VERSION, T_TEXT, T_SURF);
    cv_text_on(30, 38, &AF_S, "LATIN GROOVES FOR THE FM-1", T_MID, T_SURF);
    cv_text_on(30, 54, &AF_S, "ON FELUCCA, AFTER SILL", T_MID, T_SURF);
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
        case MU_PG_SOLO: mu_panel_solo(u); break;
        case MU_PG_PAN3D: mu_panel_pan3d(u); break;
        case MU_PG_PERC: mu_panel_perc(u); break;
        case MU_PG_KEY: mu_panel_key(u); break;
        case MU_PG_GROOVE: mu_panel_groove(u); break;
        case MU_PG_MIX: mu_panel_mix(u); break;
        case MU_PG_SPACE: mu_panel_space(u); break;
        case MU_PG_ARC: mu_panel_arc(u); break;
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
    if (ux.style == ST_LINE) {
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
    ma_rebuild(g);
    g->sound_sig++;
}

void mu_knob(ma_ui *u, int k, int d)
{
    ma_groove *g = u->g;
    char m[24], a[8];
    switch (k) {
    case MU_K_SELECT:
        g->s.bpm = (uint8_t)mu_clampi(g->s.bpm + d, 60, 220);
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
        if (mu_card_grey(u, c)) {                  /* (greyed: says why) */
            mu_msg(u, "MODE FIRST");
            return;
        }
        if (c->kind == SC_LOAD) {
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
        if (c->kind == SC_STORE) {
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
        if (c->kind == SC_CLEAR) {
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
        if (c->kind == SC_PAL || c->kind == SC_ANGLE)
            v = c->kind == SC_PAL ? mu_wrap(v, (int)NPALETTES) : mu_wrap(v, 36);
        else if (c->kind == SC_SND || c->kind == SC_KEY || c->kind == SC_LOOP)
            v = c->lo + mu_wrap(v - c->lo, c->hi - c->lo + 1);
        v = mu_clampi(v, c->lo, c->hi);
        mu_set(u, c, v);
        if (c->off == SS_(key) || c->off == SS_(scale) || c->off == SS_(chords) || c->off == SS_(bars) ||
            c->off == SS_(snd) + 2 || c->off == SS_(snd) + 3)
            ma_rebuild(g);
        if (c->off == SS_(scale))
            mu_msg(u, SN_SCALE_LONG[g->s.scale % MA_SCALES]);
        if (c->off == SS_(palette) || c->off == SS_(look))
            mu_apply_style(u);
        if (c->off == SS_(arc) && g->playing) {    /* ARC turned on while playing: from a montuno */
            g->sec = MA_SEC_MON;
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
    if (b == MU_B_REC) {                           /* BAND HIT: down and up both count */
        if (!g->playing) {
            if (down) mu_msg(u, "PLAY FIRST");
            return;
        }
        ma_band_hit(g, down);
        return;
    }
    if (!down)
        return;
    switch (b) {
    case MU_B_PLAY: ma_play(g, !g->playing); break;
    case MU_B_SEQ:
        if (g->playing) ma_mambo(g);
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
    case MU_B_FX: mu_goto_page(u, MU_PG_SOUND); break;
    case MU_B_LFO: mu_goto_page(u, u->page == MU_PG_SOLO ? MU_PG_PAN3D : MU_PG_SOLO); break;
    case MU_B_SCL: mu_goto_page(u, MU_PG_KEY); break;
    case MU_B_ARP: mu_goto_page(u, MU_PG_GROOVE); break;
    case MU_B_EDIT: mu_goto_page(u, MU_PG_PERC); break;
    case MU_B_GLO: mu_goto_page(u, u->page == MU_PG_MIX ? MU_PG_SPACE : MU_PG_MIX); break;
    case MU_B_ENV: mu_goto_page(u, MU_PG_ARC); break;
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
        u->key_sent[k] = note + 12 * u->oct + 12;  /* (a trumpet's register: an octave over the keys as printed) */
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
    if ((u->cpu_ms -= ms) <= 0) {
        u->cpu_ms = MU_CPU_MS;
        u->cpu_shown = u->cpu;
    }
    ma_seen n;
    while (ma_feed_pop(g, &n))
        ;
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
