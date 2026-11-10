/* SPDX-License-Identifier: GPL-3.0-only */
/* MANTA on the FM-1: the glue between Manta (the groove engine and its screen) and Felucca's firmware (the
 * engines, voices and effects, the panel, the screen driver, USB MIDI, the flash). Included by felucca.c after
 * Felucca's own sources when FELUCCA_MANTA is 1; the groove engine (ma_groove.c) and the maths (tuna_libm.c, from
 * Tuna) are their own translation units, linked beside felucca.o (tools/build.py). Shaped as Flyfish's ff_fw.c.
 *
 * Who runs what:
 *   the audio ISR (audio.c audio_block -> ma_audio_pre): the groove in time, its notes straight into Felucca's
 *     tracks (trk_note_on / off) and out as USB MIDI; the lift and the pump into the tracks (mas_block);
 *   the main loop (main.c -> ma_frame, ma_input): the panel into the controls (the audio ISR held off while one
 *     acts), a part's sound loaded when it changed (a preset load is the main loop's), the cards into the tracks,
 *     the LEDs, the settings and sets saved while stopped, the screen.
 * Tracks: 1 STABS, 2 BASS, 3 HOOK, 4 DRUMS (ma_sounds.h). */
#include "ma_groove.h"
#include "ma_sounds.h"
#include "ma_ui.h"
#include "ma_ui.c"
#include "ma_version.h"

static ma_groove ma_g;
static ma_ui ma_u;

/* what the tracks were last set to */
static ma_settings ma_applied;
static bool ma_applied_ok;
static uint8_t ma_loaded_sig = 0xFF;
static int8_t ma_loaded_snd[MA_PARTS] = {-1, -1, -1, -1};

/* ---- the audio ISR --------------------------------------------------------------------------------------------- */
static void ma_sound(void *ctx, const ma_event *e)
{
    (void)ctx;
    uint32_t p = e->part % MA_PARTS;
    int n = p == MA_DRUMS ? mas_drum_note(&ma_g.s, e->note) : e->note;
    if (n < 0)                                                /* (the choir on a kit without it) */
        return;
    if (e->on)
        trk_note_on(&trk[p], (uint32_t)n, e->vel);
    else
        trk_note_off(&trk[p], (uint32_t)n);
    if (p == MA_DRUMS && ma_is_choir(e->note))                /* (the choir has no General MIDI drum: not sent) */
        return;
    /* MIDI out: STABS on channel 1, BASS 2, HOOK 3, the drums 10 (General MIDI's) */
    uint32_t ch = p == MA_DRUMS ? 9u : p, st = (e->on ? 0x90u : 0x80u) | ch, cin = st >> 4;
    int dr = ma_drum_of(e->note);
    int mn = p == MA_DRUMS ? (int)(dr == MA_DR_KICK ? 36 : dr == MA_DR_SNARE ? 38 : dr == MA_DR_CLAP ? 39
                                   : dr == MA_DR_HAT ? 42 : dr == MA_DR_OPEN ? 46 : 49) : n;
    midi_out_event(cin | st << 8 | (uint32_t)mn << 16 | (uint32_t)(e->on ? e->vel : 0u) << 24);
}

static void ma_audio_pre(uint32_t n)
{
    ma_process(&ma_g, (int)n);
    mas_block(&ma_g);
}

static void ma_audio_post(int32_t *out, uint32_t n)
{
    (void)out;
    (void)n;
}

/* ---- the settings: one record in Felucca's first project object, the sets in the second; saved only while stopped
 * (the flash is the code's: XIP, writing it while playing would stall the audio) ---- */
#define MA_REC_MAGIC 0x31534953u                             /* "SIS1" */
#define MA_SETS_MAGIC 0x31544544u                            /* "DET1" */
typedef struct { uint32_t magic; uint16_t size, ver; ma_settings s; } ma_record;
typedef struct { uint32_t magic; uint16_t size; uint8_t used, pad; ma_settings s[MA_SETS]; } ma_sets_rec;
static uint32_t ma_save_ms;
static bool ma_dirty;
static void ma_settings_load(void)
{
#if FELUCCA_FLASH
    ma_record r;
    memset(&r, 0, sizeof r);
    int n = st_load(OBJ_PROJECT0, &r, sizeof r);
    if (n >= (int)(sizeof r - sizeof r.s) && r.magic == MA_REC_MAGIC) {
        ma_settings s;
        ma_defaults(&s);                                     /* (a shorter, older record: the new fields their defaults) */
        uint32_t k = r.size < sizeof s ? r.size : sizeof s;
        memcpy(&s, &r.s, k);
        ma_settings_fix(&s);
        ma_g.s = s;
    }
#endif
}
static void ma_settings_save(void)
{
#if FELUCCA_FLASH
    ma_record r;
    memset(&r, 0, sizeof r);
    r.magic = MA_REC_MAGIC;
    r.size = (uint16_t)sizeof r.s;
    r.ver = 1;
    r.s = ma_g.s;
    st_save(OBJ_PROJECT0, &r, sizeof r);
#endif
}
static void ma_sets_load(void)
{
#if FELUCCA_FLASH
    static ma_sets_rec r;                                    /* (static: 8 records, off the stack) */
    memset(&r, 0, sizeof r);
    if (st_load(OBJ_PROJECT0 + 1u, &r, sizeof r) == (int)sizeof r && r.magic == MA_SETS_MAGIC && r.size == sizeof r.s[0]) {
        for (int i = 0; i < MA_SETS; ++i) {
            ma_u.sets[i] = r.s[i];
            ma_settings_fix(&ma_u.sets[i]);
        }
        ma_u.set_used = r.used;
    }
#endif
}
static void ma_sets_save(void)
{
#if FELUCCA_FLASH
    static ma_sets_rec r;
    memset(&r, 0, sizeof r);
    r.magic = MA_SETS_MAGIC;
    r.size = (uint16_t)sizeof r.s[0];
    r.used = ma_u.set_used;
    memcpy(r.s, ma_u.sets, sizeof r.s);
    st_save(OBJ_PROJECT0 + 1u, &r, sizeof r);
#endif
}

/* ---- the sounds into the tracks (main loop) ---------------------------------------------------------------------- */
/* a sound of Manta's own over the factory preset just loaded (as ui.c apply_preset_to puts a preset in) */
static void ma_apply_own(int16_t *p, const preset_t *pr)
{
    static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
    for (int i = 0; i < 8; i++)
        p[P_E0 + i] = pr->e[i];
    p[P_ATK] = pr->env[0];
    p[P_DEC] = pr->env[1];
    p[P_SUS] = pr->env[2];
    p[P_REL] = pr->env[3];
    p[P_ED_FLT] = pr->fenv;
    p[P_VOICE] = pr->mono ? V_LEGATO : V_POLY;
    for (int i = 0; i < 4; i++)
        p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
}
/* one of Tuna's FM6 patches as the track's own (SLOT OWN), so nothing reloads a factory one over it (eng_fm6.c) */
static void ma_fm6_load(track_t *t, const uint8_t *packed)
{
    uint32_t tr = (uint32_t)(t - trk) % NTRK;
    uint8_t v[FP_SIZE + 1u];
    fm6_unpack(packed, v);
    fm6_set_patch(tr, v);
    t->p[P_E7] = FM6_OWN;
    fm6_slot[tr] = FM6_OWN;
    fm6_own_ok &= (uint8_t)~(1u << tr);
}
static void ma_load_part(uint32_t k)
{
    undo_depth++;                                            /* (no undo copy of Manta's loads) */
    if (k == MA_DRUMS) {
        set_engine_of(&trk[k], mas_kit_engine(ma_g.s.snd[3]));
        apply_preset_to(&trk[k], 0u);
        undo_depth--;
        return;
    }
    const mas_sound *snd = mas_of(&ma_g, (int)k);
    set_engine_of(&trk[k], snd->engine);
    apply_preset_to(&trk[k], snd->preset);
    undo_depth--;
    fm1_irq_off();
    if (snd->own)
        ma_apply_own(trk[k].p, snd->own);
    if (snd->fm6)
        ma_fm6_load(&trk[k], snd->fm6);
    fm1_irq_on();
}

static void ma_apply(void)
{
    const ma_settings *s = &ma_g.s;
    bool all = !ma_applied_ok, every = all || ma_loaded_sig != ma_g.sound_sig;   /* (a style or a set: every part) */
    for (uint32_t k = 0; k < MA_PARTS; ++k)                  /* a sound changed (a card, a style, a set): load it */
        if (every || ma_loaded_snd[k] != (int8_t)s->snd[k]) {
            ma_load_part(k);
            ma_loaded_snd[k] = (int8_t)s->snd[k];
            all = true;
        }
    ma_loaded_sig = ma_g.sound_sig;
    if (all || memcmp(s, &ma_applied, sizeof *s)) {
        fm1_irq_off();
        for (uint32_t k = 0; k < MA_PARTS; ++k)
            mas_track(&trk[k], &ma_g, (int)k);
        mas_song(s);
        fm1_irq_on();
    }
    if (s->palette != settings.palette % NPALETTES) {        /* (the menu's colour, kept as Felucca keeps its own) */
        settings.palette = s->palette;
        settings_save();
    }
    ma_applied = *s;
    ma_applied_ok = true;
}

/* ---- the power-on screen: Manta's logo (tools/ma_logo.py: 176 px, 8 greys, rows run-length coded) and the
 * Felucca it runs on; main.c holds it 0.75 s ---- */
#include "ma_logo.inc"
#define MA_SPLASH_MS 750u
static void ma_splash(void)
{
    uint16_t row[2][MA_LOGO_SIZE];                           /* (two: one fills while the other goes out by DMA) */
    uint32_t y, x0 = (240u - MA_LOGO_SIZE) / 2u, y0 = 16u;
    char line[40] = "Manta " MA_VERSION " on Felucca ";
    uint32_t k = sizeof "Manta " MA_VERSION " on Felucca " - 1u;
    lcd_fill(0, 0, 240, 240, MA_LOGO_BG);
    for (y = 0; y < MA_LOGO_SIZE; y++) {
        const uint8_t *p = MA_LOGO_RLE + MA_LOGO_ROW[y];
        uint16_t *r = row[y & 1u];
        uint32_t x = 0;
        while (x < MA_LOGO_SIZE) {
            uint32_t n = (*p >> 3) + 1u, c = MA_LOGO_PAL[*p & 7u];
            p++;
            while (n-- && x < MA_LOGO_SIZE)
                r[x++] = (uint16_t)c;
        }
        lcd_blit(x0, y0 + y, MA_LOGO_SIZE, 1, r);
    }
    for (const char *v = FELUCCA_VERSION; *v && k < sizeof line - 1u; v++)
        line[k++] = *v;
    line[k] = 0;
    draw_text_line(0, y0 + MA_LOGO_SIZE + 18u, 240, &AF_S, line, 0xA535u, MA_LOGO_BG, 1);   /* (grey, 160 165 170) */
    lcd_sync();
}

/* ---- start ------------------------------------------------------------------------------------------------------ */
static void ma_fw_init(void)
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    undo_depth++;
    fm6_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        set_engine_of(t, i == 3u ? mas_kit_engine(MA_KIT_EURO) : MAS_ANALOG);
        apply_preset_to(t, 0u);
        t->engine = t->eng_req;
        track_defaults_steps(t);                             /* (Felucca's sequencer stays empty) */
    }
    undo_depth--;
    song.sel = 0;
    song.master_q12 = 2048;
    ma_init(&ma_g, (float)FS);
    ma_settings_load();
    ma_g.s.palette = (uint8_t)(settings.palette % NPALETTES);
    ma_rebuild(&ma_g);
    ma_set_sound(&ma_g, ma_sound, 0);
    mu_init(&ma_u, &ma_g);
    ma_sets_load();
    ma_apply();
}

/* ---- the main loop ----------------------------------------------------------------------------------------------- */
static uint32_t ma_notes_prev;
static void ma_input(void)
{
    uint32_t released = 0, pressed = fm1_input_edges(&released), b, k;
    static const uint8_t KNOB[NE] = {MU_K_SELECT, MU_K_ALGO, MU_K_PRESETS, MU_K1, MU_K2, MU_K3, MU_K4};
    fm1_input_note_edges();                                  /* (the keys: from their state below) */
    for (b = 0; b < NB; b++) {                               /* (Felucca's B_* are Manta's MU_B_*, in the same order) */
        uint32_t bit = 1u << panel.btn[b];
        if (pressed & bit) {
            fm1_irq_off();
            mu_button(&ma_u, (int)b, true);
            fm1_irq_on();
        }
        if (released & bit) {
            fm1_irq_off();
            mu_button(&ma_u, (int)b, false);
            fm1_irq_on();
        }
    }
    for (k = 0; k < NE; k++) {
        int32_t s = panel_enc(k);
        if (s) {
            fm1_irq_off();
            mu_knob(&ma_u, KNOB[k], s);
            fm1_irq_on();
        }
    }
    uint32_t notes = fm1_in.notes, ch = notes ^ ma_notes_prev;
    for (k = 0; ch && k < 27u; k++)
        if ((ch >> k) & 1u) {
            fm1_irq_off();
            mu_key(&ma_u, MU_KEY_LO + (int)k, (notes >> k) & 1u);
            fm1_irq_on();
        }
    ma_notes_prev = notes;
}

/* the LEDs: the page's button, REC while a BREAK holds, SEQ while the key is up, PLAY green while playing; the keys: the
 * ones held, else the chord's tones (the notes that fit right now) */
static void ma_leds(void)
{
    static const uint8_t PAGE_BTN[MU_PAGES] = {
        [MU_PG_PLAY] = B_HOME, [MU_PG_SOUND] = B_FX, [MU_PG_TONE] = B_FX, [MU_PG_VOX] = B_LFO, [MU_PG_KEY] = B_SCL,
        [MU_PG_GROOVE] = B_ARP, [MU_PG_DRUMS] = B_EDIT, [MU_PG_MIX] = B_GLO, [MU_PG_SPACE] = B_GLO, [MU_PG_ARC] = B_ENV,
        [MU_PG_HOOK] = B_ENV, [MU_PG_SETS] = B_SAVE, [MU_PG_MENU] = B_HOME};
    static uint8_t ready;
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0};
    uint32_t k, c;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    led_put(nl, panel.btn[PAGE_BTN[ma_u.page % MU_PAGES]], 1);
    led_put(nl, panel.btn[B_REC], ma_g.brk);
    led_put(nl, panel.btn[B_SEQ], ma_g.tr > 0);
    for (k = 0; k < NB; k++)
        led_put(nd, panel.btn[k], 1);
    uint32_t lit = 0;
    if (ma_g.playing && ma_g.nloop > 0) {
        const ma_chord *chd = &ma_g.loop[ma_chord_now(&ma_g)];
        for (k = 0; k < 27u; k++)
            for (int t = 0; t < 4; ++t)
                if ((MU_KEY_LO + k) % 12u == (uint32_t)(chd->tone[t] + ma_g.tr) % 12u) lit |= 1u << k;
    }
    for (k = 0; k < 27u; k++) {
        led_put(nl, 14u + k, (int)((fm1_in.notes >> k) & 1u));
        led_put(nd, 14u + k, (int)((lit >> k) & 1u) || !((fm1_in.notes >> k) & 1u));
    }
    if (ma_g.playing)
        led_clear(nd, panel.btn[B_PLAY]);
    for (c = 0; c < FM1_NCOL; c++)
        nd[c] = settings_leds == LEDS_DIM || settings_leds == LEDS_DIM_LO ? (uint8_t)(nd[c] & ~nl[c]) : 0u;
    /* the chord's keys glow dim even with the dim LEDs off: they are music, not decoration */
    for (k = 0; k < 27u; k++)
        if ((lit >> k) & 1u && !((fm1_in.notes >> k) & 1u))
            led_put(nd, 14u + k, 1);
    if (ma_g.playing)
        nl[LED_PLAY_GREEN >> 3] |= (uint8_t)(1u << (LED_PLAY_GREEN & 7u));
    fm1_led_dim_level(settings_leds == LEDS_DIM_LO);
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led_dim[c] = nd[c];
        fm1_led_breath[c] = 0;
        fm1_led[c] = nl[c];
    }
}

/* a frame: the settings into the tracks, the LEDs, the screen; the settings and sets saved a while after the last
 * change, stopped */
static uint32_t ma_last_ms;
static void ma_frame(void)
{
    uint32_t now = fm1_ms, dt = now - ma_last_ms;
    static uint8_t first = 1;
    ma_last_ms = now;
    if (ma_u.changed) {
        ma_u.changed = false;
        ma_dirty = true;
        ma_save_ms = now;
    }
    ma_apply();
    if (ma_dirty && !ma_g.playing && now - ma_save_ms > 2000u) {
        ma_settings_save();
        ma_dirty = false;
    }
    if (ma_u.sets_changed && !ma_g.playing) {                /* (stopped: the flash is the code's while playing) */
        ma_u.sets_changed = false;
        ma_sets_save();
    }
    song.playing = ma_g.playing;                             /* (PLAY's green LED, Felucca's meters) */
    ma_leds();
    ma_u.batt = (int)batt_shown();
    ma_u.cpu = (int)(song.cpu_q8 * 100u / 256u);
    mu_tick(&ma_u, dt > 100u ? 100 : (int)dt);
    if (first) {
        mu_draw_all(&ma_u);
        first = 0;
    } else {
        mu_draw(&ma_u);
    }
}
