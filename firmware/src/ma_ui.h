/* SPDX-License-Identifier: GPL-3.0-only */
/* MANTA screen and controls: Manta's pages on Felucca's drawing (gfx.c: canvas, fonts, palettes, icons), in Sill's
 * frame (Tuna's header, four knob cards, the panel, the footer). Compiled after gfx.c in one unit, as Felucca's ui*.c
 * are (the firmware's unity build, or a host test).
 *
 * The controls
 *   SELECT     tempo                                  MASTER     volume (Felucca's)
 *   PRESETS    the style (the list shows while turning; playing, it comes in on the next bar)
 *   ALGORITHM  a new song in the same style (on the next bar)
 *   KNOB 1..4  the four cards of the page (PLAY: HEAT CLAVE SOLO HITS)
 *   PLAY       start (with the intro, ARC on) / stop
 *   REC        BAND HIT: tapped, the band hits the clave together on the next bar (a cierre); held, the descarga
 *              (the percussion alone) until let go, a timbal roll, the band back on the bar line
 *   SEQ        MAMBO: the horn section into its mambo from the next bar (ARC off: the riff in and out)
 *   keys       the solo trumpet, live, as pressed (MENU's SNAP: onto the scale). Music only, never settings
 *   OCT- OCT+  the keys' octave
 *   HOME       PLAY; held: MENU (colour, look, snap)
 *   FX         SOUND                        LFO   SOLO, again: PAN 3D   SCL   KEY
 *   ARP        GROOVE                       EDIT  PERC                  GLO   MIX, again: SPACE
 *   ENV        ARC                          SAVE  SETS (eight sets on the knobs) */
#ifndef MA_UI_H
#define MA_UI_H

#include <stdbool.h>
#include <stdint.h>
#include "ma_groove.h"

enum { MU_K_MASTER, MU_K_SELECT, MU_K_PRESETS, MU_K_ALGO, MU_K1, MU_K2, MU_K3, MU_K4, MU_KNOBS };
enum {
    MU_B_FX, MU_B_SCL, MU_B_ENV, MU_B_LFO, MU_B_EDIT, MU_B_GLO,
    MU_B_HOME, MU_B_SAVE, MU_B_ARP, MU_B_SEQ, MU_B_PLAY, MU_B_REC,
    MU_B_OCTDN, MU_B_OCTUP, MU_BUTTONS
};
enum { MU_PG_PLAY, MU_PG_SOUND, MU_PG_SOLO, MU_PG_PAN3D, MU_PG_PERC, MU_PG_KEY, MU_PG_GROOVE, MU_PG_MIX,
       MU_PG_SPACE, MU_PG_ARC, MU_PG_SETS, MU_PG_MENU, MU_PAGES };
#define MU_KEY_LO 53                      /* F3: the FM-1's lowest key */
#define MU_KEY_HI 79                      /* G5 */

typedef struct {
    ma_groove *g;
    int page;
    int hot, hot_ms;                      /* the knob just turned (its card in the accent); 9: SELECT */
    int browse_ms;                        /* the style list over the panel (PRESETS) */
    char msg[32];
    int msg_ms;
    int home_ms;
    bool home_down;
    uint32_t keys_down;                   /* bit (note - MU_KEY_LO) */
    int oct;                              /* OCT- OCT+: the keys' octave, -2 .. 2 */
    int key_sent[27];                     /* the note each key sent (with the octave it was pressed in) */
    /* SETS */
    ma_settings sets[MA_SETS];
    uint8_t set_used;                     /* bit per set */
    int set_sel;
    int clear_turns, clear_ms;            /* CLEAR: three turns right within a second */
    bool sets_changed;                    /* the glue saves the sets while stopped */
    uint32_t frame;
    int batt;                             /* 0..3 bars, 4 = USB power */
    int cpu;                              /* the audio's load, 0..100 % (the glue: Felucca's song.cpu_q8) */
    int cpu_shown, cpu_ms;                /* MENU's CPU: the load as shown, taken every MU_CPU_MS (Tuna's INFO_CPU_MS) */
    bool changed;                         /* a setting changed: the glue applies it (sound, mix) and saves it later */
    uint32_t sig_head, sig_card[4], sig_foot;
    bool force;
} ma_ui;

void mu_init(ma_ui *u, ma_groove *g);
void mu_knob(ma_ui *u, int knob, int delta);
void mu_button(ma_ui *u, int button, bool down);
void mu_key(ma_ui *u, int note, bool down);
void mu_tick(ma_ui *u, int ms);           /* timers; every frame */
void mu_draw(ma_ui *u);                   /* what changed; the panel every frame */
void mu_draw_all(ma_ui *u);
const char *mu_page_name(int page);

#endif
