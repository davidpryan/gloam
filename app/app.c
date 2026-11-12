/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* GLOAM: the unity build, after fm1-base's platform.
 *   core/    shared with the six other firmwares (text, the synth and drums, the transport, slots, controls)
 *   model.h  the project       dsp.c  choir, bass, air, reverb, the mix       seq.c  lanes, sections, macros
 *   gen.c    generators, forms, the demo       ui.c  the controls       draw.c  the screen and lights */
#define APP_MIX                                 /* GLOAM mixes its own lanes (core/clock.c calls app_mix) */
#include "fastmath.h"
#include "model.h"
#include "core/util.c"
#include "core/synth.c"
#include "core/clock.c"
#include "core/slots.c"
#include "core/kit.c"
static proj_t P PLAT_POOL;
#include "dsp.c"
#include "seq.c"
#include "gen.c"
#include "ui.c"
#include "draw.c"

void app_init(void)
{
    uint32_t i;
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC || set.slot >= NSLOTS) {
        set.magic = SET_MAGIC;
        set.slot = 0;
        set.ble_off = 0;
    }
    bank_init();
    if (!slot_load(set.slot, PROJ_MAGIC, &P, sizeof P))
        project_new();
    project_apply();
    G.seed = 0x6A11u;
    G.cue = 0xFFu;
    G.song = 1;
    G.sec = 0;
    G.tape_mul = 1.0f;
    for (i = 0; i < NLANE; i++)
        MX.lane_lvl[i] = 1.0f;
    MX.filter = 1.0f;
    MX.air = 0.5f;
    for (i = 0; i < NKEYS; i++)
        U.lantern_note[i] = 0;
    U.held = -1;
    U.page = PG_LANE;
    U.lane = LN_DRUMS;
    K.card = NB;
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    if (!T.playing) {                           /* stopped: the macros as set by hand still shape what is heard */
        uint32_t m;
        for (m = 0; m < NMACRO; m++)
            G.mac[m] = (float)P.mac[m] / 127.0f;
        MX.air = G.mac[MC_AIR];
        MX.verb = G.mac[MC_VERB];
        MX.filter = G.mac[MC_FILTER];
        MX.choir = G.mac[MC_CHOIR];
        MX.wobble = G.mac[MC_WOBBLE];
        MX.pump = G.mac[MC_PUMP];
        MX.dropout = G.dropout_hand;
        screw_mul = fm_exp2f(-(float)P.screw / 127.0f);
    }
    clock_render(lr, n, (float)master_q12 * (1.0f / 4096.0f) * 7000000.0f);
}

void app_panic(void)
{
    T.playing = 0;
    voices_silence();
    BS.on = 0;
    CV[0].on = CV[1].on = 0;
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, K_BG);
    K.redraw = 1;
}

void app_input_only(void) { input(); }

void app_frame(void)
{
    static uint32_t last;
    uint32_t now = plat_ms(), i;
    input();
    if (ann_on() && ann_where()) {
        char t[48];
        s_cpy(t, APP_NAME ". ");
        s_cat(t, PG_NM[U.page]);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(now);
    if (U.save_wait && !T.playing) {
        U.save_wait = 0;
        project_save_now();
    }
    if (K.msg_until && now >= K.msg_until) {
        K.msg_until = 0;
        K.redraw = 1;
    }
    if ((T.playing || U.lantern) && now - last >= 40u)
        K.redraw = 1;
    for (i = 0; i < 16u; i++)
        if (heard[i])
            heard[i]--;
    if (kit_hint_due(HINTS))
        K.redraw = 1;
    lights();
    if (K.redraw) {
        K.redraw = 0;
        last = now;
        draw();
    }
}

/* backup's hooks: GLOAM keeps nothing unsaved in the store but its settings */
int app_backup_playing(void) { return T.playing; }
void app_backup_flush(void) { settings_save(); }
void app_backup_hold(int on) { (void)on; }
void app_backup_restored(void) { app_init(); }

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "playing")) *got = T.playing;
    else if (!strcmp(what, "page")) *got = U.page;
    else if (!strcmp(what, "lane")) *got = U.lane;
    else if (!strcmp(what, "row")) *got = U.row;
    else if (!strcmp(what, "sec")) *got = G.sec;
    else if (!strcmp(what, "cue")) *got = G.cue == 0xFFu ? -1 : G.cue;
    else if (!strcmp(what, "pass")) *got = G.pass;
    else if (!strcmp(what, "nsec")) *got = P.nsec;
    else if (!strcmp(what, "seccur")) *got = U.sec_cur;
    else if (!strcmp(what, "bars")) *got = cursec()->bars;
    else if (!strcmp(what, "follow")) *got = cursec()->follow;
    else if (!strcmp(what, "hold")) *got = G.hold;
    else if (!strcmp(what, "lantern")) *got = U.lantern;
    else if (!strcmp(what, "screw")) *got = P.screw;
    else if (!strcmp(what, "screwmul100")) *got = (int)(screw_mul * 100.0f + 0.5f);
    else if (!strcmp(what, "bpm10")) *got = T.bpm10;
    else if (!strcmp(what, "freeze")) *got = G.freeze;
    else if (!strcmp(what, "bank")) *got = BANK.factory ? 0 : (int)BANK.n;
    else if (!strcmp(what, "pat")) *got = P.sec[G.sec].pat[U.lane % NLANE_SEQ] == 0xFFu ? -1 : P.sec[G.sec].pat[U.lane % NLANE_SEQ];
    else if (!strncmp(what, "hit", 3)) { int s = atoi(what + 3); *got = P.drum[lane_pat()][U.row][s % NSTEP].vel; }
    else if (!strncmp(what, "nudge", 5)) { int s = atoi(what + 5); *got = P.drum[lane_pat()][U.row][s % NSTEP].nudge; }
    else if (!strncmp(what, "roll", 4)) { int s = atoi(what + 4); *got = P.drum[lane_pat()][U.row][s % NSTEP].roll; }
    else if (!strncmp(what, "bdeg", 4)) { int s = atoi(what + 4); *got = P.bass[lane_pat()][s % NSTEP].deg; }
    else if (!strncmp(what, "cdeg", 4)) { int s = atoi(what + 4); *got = P.chord[lane_pat()][s % NSTEP].deg == 0xFFu ? -1 : P.chord[lane_pat()][s % NSTEP].deg; }
    else if (!strncmp(what, "vslice", 6)) { int s = atoi(what + 6); *got = P.choir[lane_pat()][s % NSTEP].slice == 0xFFu ? -1 : P.choir[lane_pat()][s % NSTEP].slice; }
    else if (!strcmp(what, "choir_on")) *got = CV[0].on + CV[1].on;
    else if (!strcmp(what, "bass_on")) *got = BS.on;
    else if (!strcmp(what, "dirty")) *got = U.dirty;
    else if (!strcmp(what, "card")) *got = K.card == NB ? -1 : K.card;
    else if (!strcmp(what, "recn")) *got = G.rec_n;
    else return 0;
    return 1;
}
#endif
