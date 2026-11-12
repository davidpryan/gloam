/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* GLOAM's controls. One grammar everywhere: black keys choose WHAT (a drum, a degree, a slice, a section's action),
 * white keys are WHEN (steps, or sections on the song page); hold a step and the knobs are that step.
 *   SEQ   the LANE page (ALGO the lane: DRUMS BASS CHORDS CHOIR AIR; PRESETS its pattern in this section; OCT the
 *         step page)                           HOME  the SONG (the arranger)        EDIT  the SECTION in full
 *   ENV   the lane's SOUND                     LFO   SETUP                          GLO   LANTERN on/off (held + black
 *   1-5: mute lanes)                           FX held: freeze, swell, riser, tape stop, dropout, impact
 *   SELECT  SCREW (SEL held: UNSNAP)           ARP  generate (on SONG: the form)    SEL + ARP  take it back
 *   REC  record (lantern hits and MIDI land in the patterns, off the grid as played)        PLAY / SAVE */

#define ACCENT RGB(150, 120, 255)
enum { PG_LANE, PG_SONG, PG_SECTION, PG_SOUND, PG_SETUP, NPG };
static const char *const PG_NM[NPG] = {"LANE", "SONG", "SECTION", "SOUND", "SETUP"};
static const uint16_t LANE_C[NLANE] = {RGB(255, 120, 110), RGB(255, 180, 80), RGB(150, 120, 255), RGB(120, 210, 255),
                                       RGB(140, 200, 160)};
static const uint8_t ROLLS[] = {0, 2, 3, 4, 6, 2 | 8, 3 | 8, 4 | 8, 6 | 8};
static const char *const ROLL_NM[] = {"NONE", "2", "3", "4", "6", "2 FALL", "3 FALL", "4 FALL", "6 FALL"};
static const uint8_t COLOURS[] = {0, CM_7, CM_SUS2, CM_SUS4, CM_ADD9, CM_7 | CM_ADD9, CM_FLIP, CM_FLIP | CM_7};
static const char *const COLOUR_NM[] = {"TRIAD", "7", "SUS2", "SUS4", "ADD9", "7 ADD9", "FLIP", "FLIP 7"};
static const char *const SEC_NAMES[] = {"INTRO", "BUILD", "DROP", "BREAK", "VERSE", "HOOK", "OUTRO", "RAIN", "GHOST",
                                        "VOID", "RISE", "FALL", "RITE", "STEPS", "MEMORY", "SUMMON"};
#define NSECNAMES (sizeof SEC_NAMES / sizeof SEC_NAMES[0])

#define SET_MAGIC 0x31534C47u
typedef struct {
    uint32_t magic;
    uint8_t slot, ble_off, rsv[2];
} settings_t;
static settings_t set;

static struct {
    uint8_t page, lane, row, spage;
    int8_t held;                                        /* the white key of a step held, -1 none */
    uint8_t held_turned;
    uint8_t last_deg, last_chord, last_slice;
    uint8_t sec_cur, secrow, sndrow, setrow;
    uint8_t lantern, rec, form, dstyle;
    uint8_t mute;                                       /* lanes muted by hand */
    uint8_t dirty, save_wait;
    /* the undo of a generate: a lane's pattern as it was */
    uint8_t undo_lane, undo_pat, undo_ok, undo_secs;
    uint8_t undo_buf[sizeof(dhit_t) * NDRUM_ROW * NSTEP];
    section_t undo_sec[NSEC];
    uint8_t undo_nsec;
    int16_t lantern_note[NKEYS];
} U;

static const char *const CARD_TEXT[NB] = {
    "FX held: the effects on the white keys. 1 FREEZE (held), 2 SWELL, 3 RISER, 4 TAPE STOP (held), 5 DROPOUT (held), 6 IMPACT. K1-K4: reverb, filter, pump, air.",
    ("SEL: on the SONG page, opens the section. Held: SELECT turns UNSNAP, ARP undoes a generate, "
     "PRESETS changes the scale."),
    "ENV: the SOUND of the lane shown: tone, filters, envelopes, reverb send, how much the kick ducks it, MIDI channel.",
    "LFO: SETUP. Tempo, key, scale, reverb, clock, the four project slots, the vocal bank.",
    "EDIT: the SECTION in full: name, bars, repeats, what follows, the patterns, transitions, and the eight macros' ramps.",
    "GLO: LANTERN. Every key a lit pad: white 1-7 chords, 8-15 choir slices, 16 impact; black 1-8 drums, 9 riser, 10 dropout, 11 tape stop. Held + black 1-5: mute lanes.",
    ("HOME: the SONG. White keys launch sections at the bar; black keys: hold, at once, record the arrangement, insert, "
     "delete, move, form, follow on/off."),
    "SAVE writes the project to its slot. While playing it saves at the stop.",
    "ARP: GENERATE a pattern for the lane (drums: 2-step, half-time, witch in turn). On the SONG page: the form. SEL + ARP undoes.",
    "SEQ: the LANE page. ALGO picks the lane, PRESETS its pattern in this section, OCT the page of steps.",
    "PLAY starts the song from the section on the song page's cursor (or stops).",
    "REC: what you play (lantern pads, MIDI) is written into the section's patterns where you played it, off the grid as played.",
    "OCT-: the previous 16 steps (on the song page, sections 1-16).",
    "OCT+: the next 16 steps (on the song page, sections 17-32).",
#if PLAT_SMK37
    0, 0, 0, 0,
#endif
};
static const char *const HINTS[NB] = {
    [B_FX] = "1 FREEZE 2 SWELL 3 RISER 4 TAPESTOP 5 DROP 6 IMPACT",
    [B_SEL] = "SELECT: UNSNAP  ARP: UNDO  PRESETS: SCALE",
    [B_GLO] = "BLACK 1-5: MUTE LANES  TAP: LANTERN",
};

static void dirty(void) { U.dirty = 1; }
static void settings_save(void) { plat_store_save(OBJ_SETTINGS, &set, sizeof set); }
int gloam_ble_off(void) { return set.ble_off; }
static section_t *cursec(void) { return &P.sec[U.sec_cur % NSEC]; }
static uint32_t lane_pat(void)                          /* the pattern the lane shown plays in the section shown */
{
    uint32_t p = T.playing ? G.pat[U.lane % NLANE_SEQ] : P.sec[G.sec].pat[U.lane % NLANE_SEQ];
    return p == 0xFFu ? 0u : p;
}

static void project_save_now(void)
{
    int r;
    P.bpm10 = G.bpm_base;
    r = slot_save(set.slot, PROJ_MAGIC, PROJ_VER, &P, sizeof P);
    if (r == 0)
        U.dirty = 0;
    app_say(r == 0 ? "SAVED" : "SAVE", r == 0 ? "" : r == -1 ? "TOO BIG" : "FAILED");
}
static void transport_wait_stop(void)
{
    uint32_t t0 = plat_ms();
    T.want = 2;
    while (T.playing && plat_ms() - t0 < 200u)
        plat_yield();
}
static void project_apply(void)
{
    G.bpm_base = P.bpm10;
    T.bpm10 = P.bpm10;
    T.clk_mode = P.clk_mode;
    T.clk_send = P.clk_send;
    reverb_set(P.snd.verb_size, P.snd.verb_tone, 0);
    patches_update();
}

/* ------------------------------------------------------------------ list pages --- */
static void v_u8(char *o, uint32_t v) { i_text((int32_t)v, o); }
static void ic(uint8_t *f, int32_t d, int32_t lo, int32_t hi) { *f = (uint8_t)clamp((int32_t)*f + d, lo, hi); dirty(); patches_update(); }

/* the SECTION page: each row a field of the section under the song cursor */
enum { SR_NAME, SR_BARS, SR_REP, SR_FOLLOW, SR_TARGET, SR_TARGET2, SR_CHANCE, SR_PAT0, SR_PAT1, SR_PAT2, SR_PAT3,
       SR_ROWS, SR_TRANSPOSE, SR_CURVE, SR_TRANS0, SR_MAC0 = SR_TRANS0 + NTRANS, NSR = SR_MAC0 + 2 * NMACRO };
static const uint8_t ROWMASKS[] = {0xFF, 0x0F, 0xFE, 0x01, 0x03, 0xF8, 0x1C};
static const char *const ROWMASK_NM[] = {"ALL", "KICK SN CLAP HAT", "NO KICK", "KICK ONLY", "KICK SNARE", "HATS AND PERC", "CLAP HAT OPEN"};
#define NROWMASK (sizeof ROWMASKS / sizeof ROWMASKS[0])
static void sec_label(uint32_t r, char *o)
{
    if (r >= SR_MAC0) {
        s_cpy(o, MC_NM[(r - SR_MAC0) / 2u]);
        s_cat(o, (r - SR_MAC0) & 1u ? " TO" : " FROM");
    } else if (r >= SR_TRANS0) {
        s_cpy(o, TR_NM[r - SR_TRANS0]);
    } else {
        static const char *const L[] = {"NAME", "BARS", "REPEATS", "THEN", "TARGET", "RANGE TO", "CHANCE",
                                        "DRUMS PATTERN", "BASS PATTERN", "CHORDS PATTERN", "CHOIR PATTERN", "DRUM ROWS",
                                        "TRANSPOSE", "RAMP CURVE"};
        s_cpy(o, L[r]);
    }
}
static void sec_value(uint32_t r, char *o)
{
    const section_t *s = cursec();
    uint32_t i;
    o[0] = 0;
    if (r >= SR_MAC0) {
        uint8_t v = (r - SR_MAC0) & 1u ? s->to[(r - SR_MAC0) / 2u] : s->from[(r - SR_MAC0) / 2u];
        if (v == MC_HOLD) s_cpy(o, "AS SET");
        else i_cat(o, v);
        return;
    }
    if (r >= SR_TRANS0) {
        s_cpy(o, s->trans >> (r - SR_TRANS0) & 1u ? "ON" : "-");
        return;
    }
    switch (r) {
    case SR_NAME: s_cpy(o, s->name[0] ? s->name : "-"); break;
    case SR_BARS: i_cat(o, s->bars); break;
    case SR_REP: s_cat(o, "x"); i_cat(o, s->repeats); break;
    case SR_FOLLOW: s_cpy(o, FO_NM[s->follow % NFOLLOW]); break;
    case SR_TARGET: i_cat(o, s->target + 1); break;
    case SR_TARGET2: i_cat(o, s->target2 + 1); break;
    case SR_CHANCE: i_cat(o, s->chance); s_cat(o, "%"); break;
    case SR_ROWS:
        for (i = 0; i < NROWMASK && ROWMASKS[i] != s->rows; i++)
            ;
        s_cpy(o, i < NROWMASK ? ROWMASK_NM[i] : "CUSTOM");
        break;
    case SR_TRANSPOSE: sgn_cat(o, s->transpose); break;
    case SR_CURVE: s_cpy(o, CV_NM[s->curve % NCURVE]); break;
    default: {
        uint8_t p = s->pat[r - SR_PAT0];
        if (p == 0xFFu) s_cpy(o, "SILENT");
        else i_cat(o, p + 1);
        break;
    }
    }
}
static void sec_change(uint32_t r, int32_t d)
{
    section_t *s = cursec();
    if (r >= SR_MAC0) {
        uint8_t *v = (r - SR_MAC0) & 1u ? &s->to[(r - SR_MAC0) / 2u] : &s->from[(r - SR_MAC0) / 2u];
        int32_t x = *v == MC_HOLD ? -1 : *v;
        x = clamp(x + d * 4, -1, 127);
        *v = x < 0 ? MC_HOLD : (uint8_t)x;
    } else if (r >= SR_TRANS0) {
        s->trans ^= (uint8_t)(1u << (r - SR_TRANS0));
    } else {
        switch (r) {
        case SR_NAME: {
            uint32_t i = 0, k;
            for (i = 0; i < NSECNAMES; i++) {
                for (k = 0; SEC_NAMES[i][k] && SEC_NAMES[i][k] == s->name[k]; k++)
                    ;
                if (!SEC_NAMES[i][k] && !s->name[k])
                    break;
            }
            i = (uint32_t)(((int32_t)(i < NSECNAMES ? i : 0u) + d + (int32_t)NSECNAMES * 4) % (int32_t)NSECNAMES);
            sec_name(s, SEC_NAMES[i]);
            break;
        }
        case SR_BARS: s->bars = (uint8_t)clamp(s->bars + d, 1, 64); break;
        case SR_REP: s->repeats = (uint8_t)clamp(s->repeats + d, 1, 16); break;
        case SR_FOLLOW: s->follow = (uint8_t)clamp(s->follow + d, 0, NFOLLOW - 1); break;
        case SR_TARGET: s->target = (uint8_t)clamp(s->target + d, 0, NSEC - 1); break;
        case SR_TARGET2: s->target2 = (uint8_t)clamp(s->target2 + d, 0, NSEC - 1); break;
        case SR_CHANCE: s->chance = (uint8_t)clamp(s->chance + d * 5, 0, 100); break;
        case SR_ROWS: {
            uint32_t i;
            for (i = 0; i < NROWMASK && ROWMASKS[i] != s->rows; i++)
                ;
            s->rows = ROWMASKS[(uint32_t)clamp((int32_t)(i < NROWMASK ? i : 0u) + d, 0, NROWMASK - 1)];
            break;
        }
        case SR_TRANSPOSE: s->transpose = (int8_t)clamp(s->transpose + d, -12, 12); break;
        case SR_CURVE: s->curve = (uint8_t)clamp(s->curve + d, 0, NCURVE - 1); break;
        default: {
            uint8_t *p = &s->pat[r - SR_PAT0];
            int32_t x = *p == 0xFFu ? -1 : *p;
            x = clamp(x + d, -1, NPAT - 1);
            *p = x < 0 ? 0xFFu : (uint8_t)x;
            if (T.playing && U.sec_cur == G.sec)
                G.pat[r - SR_PAT0] = *p;
            break;
        }
        }
    }
    dirty();
}

/* the SOUND page: rows of the lane shown, then the reverb */
typedef struct {
    const char *label;
    uint8_t *(*field)(void);
    uint8_t max;
} srow_t;
#define F(name, expr) static uint8_t *name(void) { return &(expr); }
F(f_tone, P.snd.tone) F(f_decay, P.snd.decay) F(f_level, P.snd.level) F(f_unsnap, P.snd.unsnap) F(f_swing, P.snd.swing)
F(f_bmode, P.snd.bmode) F(f_bcut, P.snd.bcut) F(f_breso, P.snd.breso) F(f_bdrive, P.snd.bdrive) F(f_bwob, P.snd.bwob)
F(f_bwobr, P.snd.bwob_rate) F(f_bglide, P.snd.bglide) F(f_blevel, P.snd.blevel)
F(f_cwave, P.snd.cwave) F(f_ccut, P.snd.ccut) F(f_cspread, P.snd.cspread) F(f_catt, P.snd.catt) F(f_crel, P.snd.crel)
F(f_clevel, P.snd.clevel) F(f_vlevel, P.snd.vlevel) F(f_vgrain, P.snd.vgrain)
F(f_rain, P.snd.rain) F(f_crackle, P.snd.crackle) F(f_hiss, P.snd.hiss) F(f_wind, P.snd.wind) F(f_hum, P.snd.hum)
F(f_send, P.snd.send[U.lane % NLANE]) F(f_pump, P.snd.pump[U.lane % NLANE]) F(f_chan, P.snd.chan[U.lane % NLANE])
static const srow_t SND_DR[] = {{"TONE", f_tone, 127}, {"DECAY", f_decay, 127}, {"LEVEL", f_level, 127},
                                {"UNSNAP (LOOSENESS)", f_unsnap, 127}, {"SHUFFLE", f_swing, 127}};
static const srow_t SND_BS[] = {{"MODE", f_bmode, NBM - 1}, {"CUTOFF", f_bcut, 127}, {"RESONANCE", f_breso, 127},
                                {"DRIVE", f_bdrive, 127}, {"WOBBLE DEPTH", f_bwob, 127}, {"WOBBLE RATE", f_bwobr, NWOB - 1},
                                {"GLIDE", f_bglide, 127}, {"LEVEL", f_blevel, 127}};
static const srow_t SND_CH[] = {{"WAVE", f_cwave, NWAVE - 1}, {"CUTOFF", f_ccut, 127}, {"SPREAD", f_cspread, 127},
                                {"ATTACK", f_catt, 127}, {"RELEASE", f_crel, 127}, {"LEVEL", f_clevel, 127}};
static const srow_t SND_CV[] = {{"LEVEL", f_vlevel, 127}, {"GRAIN SIZE", f_vgrain, 127}};
static const srow_t SND_AI[] = {{"RAIN", f_rain, 127}, {"VINYL CRACKLE", f_crackle, 127}, {"HISS", f_hiss, 127},
                                {"WIND", f_wind, 127}, {"MAINS HUM", f_hum, 127}};
static const srow_t SND_ALL[] = {{"REVERB SEND", f_send, 127}, {"DUCKED BY KICK", f_pump, 127}, {"MIDI CHANNEL", f_chan, 16}};
static const srow_t *snd_rows(uint32_t *n)
{
    switch (U.lane) {
    case LN_DRUMS: *n = 5; return SND_DR;
    case LN_BASS: *n = 8; return SND_BS;
    case LN_CHORDS: *n = 6; return SND_CH;
    case LN_CHOIR: *n = 2; return SND_CV;
    default: *n = 5; return SND_AI;
    }
}
static uint32_t snd_count(void) { uint32_t n; snd_rows(&n); return n + 3u; }
static const srow_t *snd_row(uint32_t r)
{
    uint32_t n;
    const srow_t *t = snd_rows(&n);
    return r < n ? &t[r] : &SND_ALL[(r - n) % 3u];
}
static void snd_value(const srow_t *s, char *o)
{
    uint8_t v = *s->field();
    if (s->field == f_bmode) s_cpy(o, BM_NM[v % NBM]);
    else if (s->field == f_bwobr) s_cpy(o, WOB_NM[v % NWOB]);
    else if (s->field == f_cwave) s_cpy(o, WAVE_NM[v % NWAVE]);
    else if (s->field == f_chan && !v) s_cpy(o, "OFF");
    else i_text(v, o);
}

/* SETUP */
enum { GR_TEMPO, GR_ROOT, GR_SCALE, GR_VSIZE, GR_VTONE, GR_CLOCK, GR_CLKOUT, GR_SLOT, GR_SAVE, GR_LOAD, GR_NEW, GR_BLE, GR_BANK, NGR_ROWS };
static const char *const GR_NM[NGR_ROWS] = {"TEMPO", "KEY", "SCALE", "REVERB SIZE", "REVERB TONE", "CLOCK", "CLOCK OUT",
                                            "SLOT", "SAVE TO SLOT", "LOAD SLOT", "NEW PROJECT", "BLUETOOTH", "VOCAL BANK"};
static void setup_value(uint32_t r, char *o)
{
    o[0] = 0;
    switch (r) {
    case GR_TEMPO: i_cat(o, G.bpm_base / 10); s_cat(o, "."); i_cat(o, G.bpm_base % 10); break;
    case GR_ROOT: s_cpy(o, ROOT_NM[P.root % 12u]); break;
    case GR_SCALE: s_cpy(o, SCALE_NM[P.scale % NSCALE]); break;
    case GR_VSIZE: i_cat(o, P.snd.verb_size); break;
    case GR_VTONE: i_cat(o, P.snd.verb_tone); break;
    case GR_CLOCK: s_cpy(o, CK_NM[T.clk_mode % NCK]); break;
    case GR_CLKOUT: s_cpy(o, T.clk_send ? "SEND" : "OFF"); break;
    case GR_SLOT: i_cat(o, set.slot + 1); s_cat(o, slot_used(set.slot, PROJ_MAGIC) ? "  (SAVED)" : "  (EMPTY)"); break;
    case GR_BLE: s_cpy(o, set.ble_off ? "OFF" : "ON"); break;
    case GR_BANK: if (BANK.factory) s_cpy(o, "BUILT-IN VOWELS"); else { i_cat(o, (int32_t)BANK.n); s_cat(o, " SLICES"); } break;
    default: s_cpy(o, "SEL >"); break;
    }
}
static void setup_change(uint32_t r, int32_t d)
{
    switch (r) {
    case GR_TEMPO: G.bpm_base = (uint16_t)clamp((int32_t)G.bpm_base + d * 10, 300, 3000); P.bpm10 = G.bpm_base; if (!T.playing) T.bpm10 = G.bpm_base; break;
    case GR_ROOT: P.root = (uint8_t)((P.root + 12u + (uint32_t)(d % 12)) % 12u); break;
    case GR_SCALE: P.scale = (uint8_t)clamp(P.scale + d, 1, NSCALE - 1); break;
    case GR_VSIZE: P.snd.verb_size = (uint8_t)clamp(P.snd.verb_size + d * 3, 0, 127); reverb_set(P.snd.verb_size, P.snd.verb_tone, G.freeze); break;
    case GR_VTONE: P.snd.verb_tone = (uint8_t)clamp(P.snd.verb_tone + d * 3, 0, 127); reverb_set(P.snd.verb_size, P.snd.verb_tone, G.freeze); break;
    case GR_CLOCK: T.clk_mode = (uint8_t)clamp(T.clk_mode + d, 0, NCK - 1); P.clk_mode = T.clk_mode; break;
    case GR_CLKOUT: T.clk_send = d > 0; P.clk_send = T.clk_send; break;
    case GR_SLOT: set.slot = (uint8_t)clamp(set.slot + d, 0, NSLOTS - 1); settings_save(); return;
    case GR_BLE: set.ble_off = d < 0; plat_ble_enable(!set.ble_off); settings_save(); return;
    default: return;
    }
    dirty();
}
static void setup_act(uint32_t r)
{
    if (r == GR_SAVE) {
        if (T.playing) {
            U.save_wait = 1;
            app_say("SAVES", "AT THE STOP");
        } else {
            project_save_now();
        }
    } else if (r == GR_LOAD) {
        if (T.playing)
            transport_wait_stop();
        if (slot_load(set.slot, PROJ_MAGIC, &P, sizeof P)) {
            project_apply();
            U.dirty = 0;
            app_say("LOADED", "");
        } else {
            app_say("SLOT", "EMPTY");
        }
    } else if (r == GR_NEW) {
        if (T.playing)
            transport_wait_stop();
        project_new();
        project_apply();
        G.sec = 0;
        dirty();
        app_say("NEW", "PROJECT");
    }
}

/* a list's cursor and value (the three list pages share this) */
static void list_input(uint8_t *cur, uint32_t n, void (*change)(uint32_t, int32_t), void (*act)(uint32_t))
{
    int32_t d = K.enc[EN_PRESET] + K.enc[EN_K1];
    *cur = (uint8_t)clamp((int32_t)*cur + K.enc[EN_SELECT], 0, (int32_t)n - 1);
    if (d)
        change(*cur, d);
    if (TAPPED(B_SEL) && act)
        act(*cur);
}
static void snd_change(uint32_t r, int32_t d)
{
    const srow_t *s = snd_row(r);
    ic(s->field(), d * (s->max > 20u ? 2 : 1), 0, s->max);
}

/* ------------------------------------------------------------------ the lane page's steps --- */
static uint32_t step_of(int w) { return U.spage * 16u + (uint32_t)w; }

static int step_on(uint32_t lane, uint32_t p, uint32_t s)
{
    switch (lane) {
    case LN_DRUMS: return P.drum[p][U.row][s].vel != 0u;
    case LN_BASS: return P.bass[p][s].deg != 0x7F;
    case LN_CHORDS: return P.chord[p][s].deg != 0xFFu;
    case LN_CHOIR: return P.choir[p][s].slice != 0xFFu;
    default: return 0;
    }
}

static void step_toggle(uint32_t s)
{
    uint32_t p = lane_pat();
    switch (U.lane) {
    case LN_DRUMS: {
        dhit_t *h = &P.drum[p][U.row][s];
        if (h->vel) h->vel = 0;
        else {
            h->vel = 100;
            h->nudge = 0;
            h->drift = (int8_t)((int32_t)(xs32(&G.seed) % 25u) - 12);
            h->roll = 0;
        }
        break;
    }
    case LN_BASS: {
        bstep_t *b = &P.bass[p][s];
        if (b->deg != 0x7F) b->deg = 0x7F;
        else {
            b->deg = (int8_t)U.last_deg;
            b->len = 2;
            b->vel = 110;
            b->flags = 0;
            b->wob = 0;
            b->nudge = 0;
            b->drift = (int8_t)((int32_t)(xs32(&G.seed) % 9u) - 4);
        }
        break;
    }
    case LN_CHORDS: {
        cstep_t *c = &P.chord[p][s];
        if (c->deg != 0xFFu) c->deg = 0xFFu;
        else {
            c->deg = U.last_chord;
            c->mods = 0;
            c->len = 16;
            c->vel = 90;
        }
        break;
    }
    case LN_CHOIR: {
        vstep_t *v = &P.choir[p][s];
        if (v->slice != 0xFFu) v->slice = 0xFFu;
        else {
            v->slice = U.last_slice;
            v->pitch = 0;
            v->formant = 0;
            v->flags = VF_FOLLOW;
            v->len = 4;
            v->vel = 100;
            v->nudge = 0;
            v->drift = (int8_t)((int32_t)(xs32(&G.seed) % 13u) - 6);
        }
        break;
    }
    default: break;
    }
    dirty();
}

/* the held step's four knobs */
static void held_knob(uint32_t k, int32_t d)
{
    uint32_t p = lane_pat(), s = step_of(U.held);
    if (!step_on(U.lane, p, s))
        step_toggle(s);
    switch (U.lane) {
    case LN_DRUMS: {
        dhit_t *h = &P.drum[p][U.row][s];
        if (k == 0) h->vel = (uint8_t)clamp(h->vel + d * 4, 1, 127);
        else if (k == 1) h->nudge = (int8_t)clamp(h->nudge + d, -12, 12);
        else if (k == 2) {
            uint32_t i;
            for (i = 0; i < sizeof ROLLS && ROLLS[i] != h->roll; i++)
                ;
            h->roll = ROLLS[clamp((int32_t)(i < sizeof ROLLS ? i : 0u) + d, 0, (int32_t)sizeof ROLLS - 1)];
        } else h->drift = (int8_t)clamp(h->drift + d, -12, 12);
        break;
    }
    case LN_BASS: {
        bstep_t *b = &P.bass[p][s];
        if (k == 0) b->deg = (int8_t)clamp(b->deg + d, -4, 8);
        else if (k == 1) b->len = (uint8_t)clamp(b->len + d, 1, 16);
        else if (k == 2) {
            static const uint8_t FL[] = {0, BF_GLIDE, BF_OCT_UP, BF_OCT_UP | BF_GLIDE, BF_OCT_DN, BF_ABS};
            uint32_t i;
            for (i = 0; i < sizeof FL && FL[i] != b->flags; i++)
                ;
            b->flags = FL[clamp((int32_t)(i < sizeof FL ? i : 0u) + d, 0, (int32_t)sizeof FL - 1)];
        } else b->wob = (uint8_t)clamp(b->wob + d, 0, NWOB);
        break;
    }
    case LN_CHORDS: {
        cstep_t *c = &P.chord[p][s];
        if (k == 0) c->deg = (uint8_t)clamp(c->deg + d, 0, 13);
        else if (k == 1) {
            uint32_t i;
            for (i = 0; i < sizeof COLOURS && COLOURS[i] != c->mods; i++)
                ;
            c->mods = COLOURS[clamp((int32_t)(i < sizeof COLOURS ? i : 0u) + d, 0, (int32_t)sizeof COLOURS - 1)];
        } else if (k == 2) c->len = (uint8_t)clamp(c->len + d, 0, 64);
        else c->vel = (uint8_t)clamp(c->vel + d * 4, 10, 127);
        break;
    }
    case LN_CHOIR: {
        vstep_t *v = &P.choir[p][s];
        if (k == 0) v->slice = (uint8_t)clamp(v->slice + d, 0, NSLICE - 1);
        else if (k == 1) v->pitch = (int8_t)clamp(v->pitch + d, -24, 24);
        else if (k == 2) v->formant = (int8_t)clamp(v->formant + d, -12, 12);
        else v->len = (uint8_t)clamp(v->len + d, 1, 32);
        break;
    }
    default: break;
    }
    dirty();
}

/* the lane's four knobs with nothing held */
static void lane_knob(uint32_t k, int32_t d)
{
    static uint8_t *const DR[4] = {&P.snd.tone, &P.snd.decay, &P.snd.unsnap, &P.snd.swing};
    static uint8_t *const CH[4] = {&P.snd.cwave, &P.snd.ccut, &P.snd.cspread, &P.snd.crel};
    static uint8_t *const CV[4] = {&P.snd.vlevel, &P.snd.vgrain, &P.snd.send[LN_CHOIR], &P.snd.pump[LN_CHOIR]};
    static uint8_t *const AI[4] = {&P.snd.rain, &P.snd.crackle, &P.snd.wind, &P.snd.hiss};
    switch (U.lane) {
    case LN_DRUMS: ic(DR[k], d * 3, 0, 127); break;
    case LN_BASS:
        if (k == 0) ic(&P.snd.bmode, d, 0, NBM - 1);
        else if (k == 1) ic(&P.snd.bcut, d * 3, 0, 127);
        else if (k == 2) ic(&P.snd.bwob, d * 3, 0, 127);
        else ic(&P.snd.bwob_rate, d, 0, NWOB - 1);
        break;
    case LN_CHORDS: ic(CH[k], k ? d * 3 : d, 0, k ? 127 : NWAVE - 1); break;
    case LN_CHOIR: ic(CV[k], d * 3, 0, 127); break;
    default: ic(AI[k], d * 3, 0, 127); break;
    }
}

static void audition(uint32_t b)                        /* a black key on the lane page */
{
    switch (U.lane) {
    case LN_DRUMS:
        U.row = (uint8_t)(b % NDRUM_ROW);
        live_note(LN_DRUMS, &drum_patch, DROW_NOTE[U.row], 100, 1);
        live_note(LN_DRUMS, &drum_patch, DROW_NOTE[U.row], 0, 0);
        break;
    case LN_BASS: {
        bstep_t bs = {0, 4, 110, 0, 0, 0, 0, 0};
        bs.deg = (int8_t)b;
        U.last_deg = (uint8_t)b;
        if (!T.playing)
            bass_on(bass_note(&bs), 110, ticks_to_samples(48), 0, WOB_T[P.snd.bwob_rate % NWOB], &P.snd);
        break;
    }
    case LN_CHORDS: {
        cstep_t c = {0, 0, 16, 90};
        c.deg = (uint8_t)b;
        U.last_chord = (uint8_t)b;
        if (!T.playing) {
            uint32_t i;
            chord_from(&c);
            for (i = 0; i < G.pad_n; i++) {
                live_note(LN_CHORDS, &chord_patch, G.pad[i], 90, 1);
                live_note(LN_CHORDS, &chord_patch, G.pad[i], 0, 0);
            }
        }
        break;
    }
    case LN_CHOIR:
        U.last_slice = (uint8_t)b;
        if (!T.playing)
            choir_on(b, 0.0f, 0.0f, 0, ticks_to_samples(96), 100, (float)P.snd.vgrain / 127.0f);
        break;
    default: break;
    }
}

/* the held step with a black key: its main value (the drum: the row) */
static void held_black(uint32_t b)
{
    uint32_t p = lane_pat(), s = step_of(U.held);
    U.held_turned = 1;
    if (!step_on(U.lane, p, s))
        step_toggle(s);
    switch (U.lane) {
    case LN_BASS: P.bass[p][s].deg = (int8_t)b; U.last_deg = (uint8_t)b; break;
    case LN_CHORDS: if (b < 7u) { P.chord[p][s].deg = (uint8_t)b; U.last_chord = (uint8_t)b; } break;
    case LN_CHOIR:
        if (b < 8u) { P.choir[p][s].slice = (uint8_t)b; U.last_slice = (uint8_t)b; }
        else if (b == 8u) P.choir[p][s].flags ^= VF_REV;
        else if (b == 9u) P.choir[p][s].flags ^= VF_FOLLOW;
        break;
    default: break;
    }
    dirty();
}

/* ------------------------------------------------------------------ generate and undo --- */
static void undo_keep(uint32_t lane, uint32_t p)
{
    const uint8_t *src;
    uint32_t n, i;
    switch (lane) {
    case LN_DRUMS: src = (const uint8_t *)P.drum[p]; n = sizeof P.drum[p]; break;
    case LN_BASS: src = (const uint8_t *)P.bass[p]; n = sizeof P.bass[p]; break;
    case LN_CHORDS: src = (const uint8_t *)P.chord[p]; n = sizeof P.chord[p]; break;
    default: src = (const uint8_t *)P.choir[p]; n = sizeof P.choir[p]; break;
    }
    for (i = 0; i < n; i++)
        U.undo_buf[i] = src[i];
    U.undo_lane = (uint8_t)lane;
    U.undo_pat = (uint8_t)p;
    U.undo_ok = 1;
    U.undo_secs = 0;
}
static void undo_take(void)
{
    uint8_t *dst;
    uint32_t n, i;
    if (U.undo_secs) {
        for (i = 0; i < NSEC; i++)
            P.sec[i] = U.undo_sec[i];
        P.nsec = U.undo_nsec;
        U.undo_secs = 0;
        app_say("UNDO", "THE FORM");
        dirty();
        return;
    }
    if (!U.undo_ok) {
        app_say("UNDO", "NOTHING");
        return;
    }
    switch (U.undo_lane) {
    case LN_DRUMS: dst = (uint8_t *)P.drum[U.undo_pat]; n = sizeof P.drum[0]; break;
    case LN_BASS: dst = (uint8_t *)P.bass[U.undo_pat]; n = sizeof P.bass[0]; break;
    case LN_CHORDS: dst = (uint8_t *)P.chord[U.undo_pat]; n = sizeof P.chord[0]; break;
    default: dst = (uint8_t *)P.choir[U.undo_pat]; n = sizeof P.choir[0]; break;
    }
    for (i = 0; i < n; i++) {                           /* swapped, so a second undo redoes */
        uint8_t t = dst[i];
        dst[i] = U.undo_buf[i];
        U.undo_buf[i] = t;
    }
    app_say("UNDO", LANE_NM[U.undo_lane]);
    dirty();
}
static void generate(void)
{
    uint32_t p = lane_pat(), seed = plat_ms() * 2654435761u;
    if (U.page == PG_SONG) {
        uint32_t i;
        for (i = 0; i < NSEC; i++)
            U.undo_sec[i] = P.sec[i];
        U.undo_nsec = P.nsec;
        U.undo_secs = 1;
        if (T.playing)
            transport_wait_stop();
        form_apply(U.form);
        U.sec_cur = 0;
        G.sec = 0;
        app_say("FORM", FORM_NM[U.form % NFORM]);
        dirty();
        return;
    }
    if (U.lane >= LN_AIR)
        return;
    undo_keep(U.lane, p);
    switch (U.lane) {
    case LN_DRUMS:
        gen_drums(p, U.dstyle, seed);
        app_say("DRUMS", DSTYLE_NM[U.dstyle]);
        U.dstyle = (uint8_t)((U.dstyle + 1u) % NDSTYLE);
        break;
    case LN_BASS: gen_bass(p, seed); app_say("GENERATED", "BASS"); break;
    case LN_CHORDS: gen_chords(p, seed); app_say("GENERATED", "CHORDS"); break;
    default: gen_choir(p, seed); app_say("GENERATED", "CHOIR"); break;
    }
    dirty();
}

/* ------------------------------------------------------------------ the song page's actions --- */
static void song_insert(void)
{
    uint32_t i;
    if (P.nsec >= NSEC)
        return;
    for (i = P.nsec; i > U.sec_cur + 1u; i--)
        P.sec[i] = P.sec[i - 1u];
    P.sec[U.sec_cur + 1u] = P.sec[U.sec_cur];
    P.nsec++;
    U.sec_cur++;
    dirty();
}
static void song_delete(void)
{
    uint32_t i;
    if (P.nsec <= 1u)
        return;
    if (T.playing)
        transport_wait_stop();
    for (i = U.sec_cur; i + 1u < P.nsec; i++)
        P.sec[i] = P.sec[i + 1u];
    P.nsec--;
    if (U.sec_cur >= P.nsec)
        U.sec_cur = (uint8_t)(P.nsec - 1u);
    if (G.sec >= P.nsec)
        G.sec = 0;
    dirty();
}
static void song_move(int d)
{
    int32_t j = (int32_t)U.sec_cur + d;
    section_t t;
    if (j < 0 || j >= (int32_t)P.nsec)
        return;
    t = P.sec[j];
    P.sec[j] = P.sec[U.sec_cur];
    P.sec[U.sec_cur] = t;
    U.sec_cur = (uint8_t)j;
    dirty();
}
static void launch(uint32_t s)                          /* a white key on the song page */
{
    if (s >= P.nsec)
        return;
    U.sec_cur = (uint8_t)s;
    if (!T.playing) {
        G.sec = (uint8_t)s;
        G.cue = 0xFFu;
        T.want = 1;
        return;
    }
    G.cue = (uint8_t)s;
}
/* the arrangement recorder stopped: the song becomes what was played (each section copied with the bars it ran) */
static void rec_arr_commit(void)
{
    section_t tmp[NSEC];
    uint32_t i;
    if (!G.rec_n)
        return;
    for (i = 0; i < NSEC; i++)
        U.undo_sec[i] = P.sec[i];
    U.undo_nsec = P.nsec;
    U.undo_secs = 1;
    for (i = 0; i < G.rec_n; i++) {
        tmp[i] = P.sec[G.rec_list[i].sec];
        tmp[i].bars = G.rec_list[i].bars;
        tmp[i].repeats = 1;
        tmp[i].follow = i + 1u < G.rec_n ? FO_NEXT : FO_STOP;
    }
    for (i = 0; i < G.rec_n; i++)
        P.sec[i] = tmp[i];
    P.nsec = G.rec_n;
    app_say("ARRANGEMENT", "RECORDED");
    dirty();
}

/* ------------------------------------------------------------------ lantern --- */
/* the lantern's hit written into the section's pattern where it was played: the nearest step, and how far off it */
static void lantern_write(uint32_t lane, uint32_t row, uint32_t val)
{
    uint32_t p = G.pat[lane], st, len, pos, s;
    int32_t off;
    if (!U.rec || !T.playing || p == 0xFFu)
        return;
    st = step_ticks(lane, p);
    len = P.info[lane][p].len ? P.info[lane][p].len : 16u;
    pos = G.sec_tick % (len * st);
    s = ((pos + st / 2u) / st) % len;
    off = (int32_t)pos - (int32_t)(s * st);
    if (off > (int32_t)(len * st) / 2)
        off -= (int32_t)(len * st);
    off = clamp(off, -12, 12);
    if (lane == LN_DRUMS) {
        dhit_t *h = &P.drum[p][row][s];
        h->vel = (uint8_t)val;
        h->nudge = (int8_t)off;
        h->drift = 0;
        h->roll = 0;
    } else if (lane == LN_CHORDS) {
        cstep_t *c = &P.chord[p][s];
        c->deg = (uint8_t)row;
        c->mods = 0;
        c->len = 8;
        c->vel = (uint8_t)val;
    } else if (lane == LN_CHOIR) {
        vstep_t *v = &P.choir[p][s];
        v->slice = (uint8_t)row;
        v->pitch = 0;
        v->formant = 0;
        v->flags = VF_FOLLOW;
        v->len = 4;
        v->vel = (uint8_t)val;
        v->nudge = (int8_t)off;
        v->drift = 0;
    }
    dirty();
}

static void lantern_key(uint32_t k, uint32_t vel)
{
    int w = white_of(k), b = black_of(k);
    if (w >= 0 && w < 7) {                              /* a chord on degree w */
        cstep_t c = {0, 0, 8, 0};
        uint32_t i;
        c.deg = (uint8_t)w;
        c.vel = (uint8_t)vel;
        chord_from(&c);
        for (i = 0; i < G.pad_n; i++)
            live_note(LN_CHORDS, &chord_patch, G.pad[i], vel, 1);
        U.lantern_note[k] = 1;
        lantern_write(LN_CHORDS, (uint32_t)w, vel);
    } else if (w >= 7 && w < 15) {
        choir_on((uint32_t)(w - 7), 0.0f, 0.0f, 0, ticks_to_samples(192), vel, (float)P.snd.vgrain / 127.0f);
        lantern_write(LN_CHOIR, (uint32_t)(w - 7), vel);
    } else if (w == 15) {
        G.impact_next = 1;
        if (!T.playing) {
            live_note(LN_DRUMS, &drum_patch, 36, 127, 1);
            live_note(LN_DRUMS, &drum_patch, 49, 120, 1);
        }
    } else if (b >= 0 && b < (int)NDRUM_ROW) {
        live_note(LN_DRUMS, &drum_patch, DROW_NOTE[b], vel, 1);
        live_note(LN_DRUMS, &drum_patch, DROW_NOTE[b], 0, 0);
        if (b == DRW_KICK)
            duck_hit();
        lantern_write(LN_DRUMS, (uint32_t)b, vel);
    } else if (b == 8) {
        G.riser_until = G.abs + BAR;
    } else if (b == 9) {
        G.dropout_hand = 1;
    } else if (b == 10) {
        G.tape_hand = 1;
    }
}
static void lantern_up(uint32_t k)
{
    int b = black_of(k);
    if (U.lantern_note[k]) {                            /* the chord let go */
        uint32_t i;
        for (i = 0; i < G.pad_n; i++)
            live_note(LN_CHORDS, &chord_patch, G.pad[i], 0, 0);
        U.lantern_note[k] = 0;
    }
    if (b == 9)
        G.dropout_hand = 0;
    if (b == 10)
        G.tape_hand = 0;
}

/* ------------------------------------------------------------------ the FX layer --- */
static void fx_key(int w, int down)
{
    if (w == 0) {
        G.freeze = (uint8_t)down;
        reverb_set(P.snd.verb_size, P.snd.verb_tone, down);
    } else if (w == 1 && down) {
        G.swell_until = G.abs + 96u;
    } else if (w == 2 && down) {
        G.riser_until = G.abs + BAR;
    } else if (w == 3) {
        G.tape_hand = (uint8_t)down;
    } else if (w == 4) {
        G.dropout_hand = (uint8_t)down;
    } else if (w == 5 && down) {
        G.impact_next = 1;
    }
}

/* ------------------------------------------------------------------ input --- */
static void input(void)
{
    uint32_t k;
    kit_input();
    if (DOWN(B_PLAY)) {
        if (T.playing) {
            T.want = 2;
        } else {
            if (U.page == PG_SONG || U.page == PG_SECTION)
                G.sec = U.sec_cur;
            G.cue = 0xFFu;
            T.want = 1;
        }
    }
    if (DOWN(B_REC)) {
        U.rec = !U.rec;
        app_say("REC", U.rec ? "WHAT YOU PLAY IS KEPT" : "OFF");
    }
    if (DOWN(B_SEQ)) U.page = PG_LANE;
    if (DOWN(B_HOME)) U.page = PG_SONG;
    if (DOWN(B_EDIT)) U.page = PG_SECTION;
    if (DOWN(B_ENV)) { U.page = PG_SOUND; U.sndrow = 0; }
    if (DOWN(B_LFO)) U.page = PG_SETUP;
    if (TAPPED(B_GLO)) {
        U.lantern = !U.lantern;
        app_say("LANTERN", U.lantern ? "KEYS ARE PADS" : "OFF");
    }
    if (TAPPED(B_SAVE))
        setup_act(GR_SAVE);
    if (DOWN(B_ARP)) {
        if (HELD(B_SEL)) {
            kit_use(B_SEL);
            undo_take();
        } else {
            generate();
        }
    }
    if (DOWN(B_OCTDN) || DOWN(B_OCTUP)) {
        int d = DOWN(B_OCTUP) ? 1 : -1;
        if (U.page == PG_SONG)
            U.spage = (uint8_t)clamp(U.spage + d, 0, 1);
        else
            U.spage = (uint8_t)clamp(U.spage + d, 0, 3);
    }
    /* the encoders */
    if (U.page == PG_SECTION) {
        if (K.enc[EN_ALGO])
            U.sec_cur = (uint8_t)clamp(U.sec_cur + K.enc[EN_ALGO], 0, P.nsec ? P.nsec - 1 : 0);
        list_input(&U.secrow, NSR, sec_change, 0);
    } else if (U.page == PG_SOUND) {
        if (K.enc[EN_ALGO]) { U.lane = (uint8_t)clamp(U.lane + K.enc[EN_ALGO], 0, NLANE - 1); U.sndrow = 0; }
        list_input(&U.sndrow, snd_count(), snd_change, 0);
    } else if (U.page == PG_SETUP) {
        list_input(&U.setrow, NGR_ROWS, setup_change, setup_act);
    } else {
        if (K.enc[EN_SELECT]) {                         /* SCREW, or with SEL held, UNSNAP */
            if (HELD(B_SEL)) {
                kit_use(B_SEL);
                P.unsnap = (uint8_t)clamp(P.unsnap + K.enc[EN_SELECT] * 4, 0, 127);
            } else {
                P.screw = (uint8_t)clamp(P.screw + K.enc[EN_SELECT] * 4, 0, 127);
            }
            dirty();
        }
        if (HELD(B_FX)) {                               /* the FX layer's knobs */
            kit_use(B_FX);
            if (K.enc[EN_K1]) { P.snd.verb_size = (uint8_t)clamp(P.snd.verb_size + K.enc[EN_K1] * 3, 0, 127); reverb_set(P.snd.verb_size, P.snd.verb_tone, G.freeze); }
            if (K.enc[EN_K2]) P.mac[MC_FILTER] = (uint8_t)clamp(P.mac[MC_FILTER] + K.enc[EN_K2] * 4, 0, 127);
            if (K.enc[EN_K3]) P.mac[MC_PUMP] = (uint8_t)clamp(P.mac[MC_PUMP] + K.enc[EN_K3] * 4, 0, 127);
            if (K.enc[EN_K4]) P.mac[MC_AIR] = (uint8_t)clamp(P.mac[MC_AIR] + K.enc[EN_K4] * 4, 0, 127);
        } else if (U.page == PG_SONG) {
            section_t *s = cursec();
            if (K.enc[EN_ALGO]) U.sec_cur = (uint8_t)clamp(U.sec_cur + K.enc[EN_ALGO], 0, P.nsec ? P.nsec - 1 : 0);
            if (K.enc[EN_K1]) { s->bars = (uint8_t)clamp(s->bars + K.enc[EN_K1], 1, 64); dirty(); }
            if (K.enc[EN_K2]) { s->repeats = (uint8_t)clamp(s->repeats + K.enc[EN_K2], 1, 16); dirty(); }
            if (K.enc[EN_K3]) { s->follow = (uint8_t)clamp(s->follow + K.enc[EN_K3], 0, NFOLLOW - 1); dirty(); }
            if (K.enc[EN_K4]) { s->target = (uint8_t)clamp(s->target + K.enc[EN_K4], 0, NSEC - 1); dirty(); }
            if (K.enc[EN_PRESET]) { U.form = (uint8_t)clamp(U.form + K.enc[EN_PRESET], 0, NFORM - 1); app_say("FORM (ARP)", FORM_NM[U.form]); }
            if (TAPPED(B_SEL)) U.page = PG_SECTION;
        } else {                                        /* the lane page */
            if (K.enc[EN_ALGO]) {
                U.lane = (uint8_t)clamp(U.lane + K.enc[EN_ALGO], 0, NLANE - 1);
                midi_lane = U.lane;
            }
            if (K.enc[EN_PRESET] && U.lane < NLANE_SEQ) {
                if (HELD(B_SEL)) {
                    kit_use(B_SEL);
                    P.scale = (uint8_t)clamp(P.scale + K.enc[EN_PRESET], 1, NSCALE - 1);
                    app_say("SCALE", SCALE_NM[P.scale]);
                } else {                                /* the lane's pattern in the section playing */
                    section_t *s = &P.sec[G.sec];
                    int32_t x = s->pat[U.lane] == 0xFFu ? -1 : s->pat[U.lane];
                    x = clamp(x + K.enc[EN_PRESET], -1, NPAT - 1);
                    s->pat[U.lane] = x < 0 ? 0xFFu : (uint8_t)x;
                    if (T.playing)
                        G.pat[U.lane] = s->pat[U.lane];
                }
                dirty();
            } else if (K.enc[EN_PRESET] && U.lane == LN_AIR) {
                ic(&P.snd.hum, K.enc[EN_PRESET] * 3, 0, 127);
            }
            for (k = 0; k < 4u; k++)
                if (K.enc[EN_K1 + k]) {
                    if (U.held >= 0) {
                        U.held_turned = 1;
                        held_knob(k, K.enc[EN_K1 + k]);
                    } else {
                        lane_knob(k, K.enc[EN_K1 + k]);
                    }
                }
        }
    }
    /* the keys */
    for (k = 0; k < NKEYS; k++) {
        int w = white_of(k), b = black_of(k);
        if (K.kdown >> k & 1u) {
            if (HELD(B_GLO) && b >= 0 && b < (int)NLANE) {
                kit_use(B_GLO);
                U.mute ^= (uint8_t)(1u << b);
                MX.lane_lvl[b] = (U.mute >> b & 1u) ? 0.0f : 1.0f;
                app_say(U.mute >> b & 1u ? "MUTE" : "UNMUTE", LANE_NM[b]);
            } else if (HELD(B_FX) && w >= 0) {
                kit_use(B_FX);
                fx_key(w, 1);
            } else if (U.lantern) {
                lantern_key(k, K.vel[k]);
            } else if (U.page == PG_SONG) {
                if (w >= 0)
                    launch(U.spage * 16u + (uint32_t)w);
                else if (b == 0) { G.hold = !G.hold; app_say("HOLD", G.hold ? "ON" : "OFF"); }
                else if (b == 1) { G.cue_now = !G.cue_now; app_say("LAUNCH", G.cue_now ? "ON THE BEAT" : "AT THE BAR"); }
                else if (b == 2) {
                    G.rec_arr = !G.rec_arr;
                    if (G.rec_arr) { G.rec_n = 0; G.rec_start = G.abs; if (T.playing) { G.rec_list[0].sec = G.sec; G.rec_list[0].bars = 1; G.rec_n = 1; } }
                    else rec_arr_commit();
                    app_say("ARRANGEMENT", G.rec_arr ? "RECORDING" : "STOPPED");
                } else if (b == 3) song_insert();
                else if (b == 4) song_delete();
                else if (b == 5) song_move(-1);
                else if (b == 6) song_move(1);
                else if (b == 7) { U.form = (uint8_t)((U.form + 1u) % NFORM); app_say("FORM (ARP)", FORM_NM[U.form]); }
                else if (b == 8) { G.song = !G.song; app_say("SONG", G.song ? "FOLLOWS ON" : "JAM: SECTIONS LOOP"); }
            } else if (U.page == PG_LANE) {
                if (w >= 0) {
                    U.held = (int8_t)w;
                    U.held_turned = 0;
                } else if (b >= 0) {
                    if (U.held >= 0)
                        held_black((uint32_t)b);
                    else
                        audition((uint32_t)b);
                }
            }
        }
        if (K.kup >> k & 1u) {
            if (U.lantern)
                lantern_up(k);
            if (w >= 0 && (HELD(B_FX) || UP(B_FX)))
                fx_key(w, 0);
            if (U.page == PG_LANE && w >= 0 && U.held == w) {
                if (!U.held_turned && !U.lantern)
                    step_toggle(step_of(w));
                U.held = -1;
            }
        }
    }
    if (UP(B_FX)) {                                     /* the FX layer let go: what was held lets go too */
        if (G.freeze) fx_key(0, 0);
        G.tape_hand = 0;
        G.dropout_hand = 0;
    }
    kit_cards(CARD_TEXT);
}
