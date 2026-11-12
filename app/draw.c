/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* GLOAM's screen: the header (the section playing, SCREW, the transport), the page, the knobs' band, and the lights.
 * Everything that moves the sound is drawn where it acts: a hit off the grid sits off its cell, a section's macros are
 * ramps across its bar on the song page, the lanterns pulse with the beat. */

static uint16_t dim(uint16_t c) { return (uint16_t)((c >> 1) & 0x7BEFu); }

static void draw_head(void)
{
    char t[40];
    const section_t *s = &P.sec[G.sec];
    s_cpy(t, U.lantern ? "LANTERN" : U.page == PG_LANE ? LANE_NM[U.lane] : PG_NM[U.page]);
    if (T.playing) {
        s_cat(t, "  ");
        s_cat(t, s->name[0] ? s->name : "-");
        if (G.cue != 0xFFu) {
            s_cat(t, " > ");
            s_cat(t, P.sec[G.cue].name[0] ? P.sec[G.cue].name : "?");
        }
    }
    kit_header("GLOAM", t, ACCENT, T.playing, U.rec, T.bpm10);
}

/* a ramp's picture: from its FROM to its TO across w */
static void ramp_draw(int32_t x, int32_t y, int32_t w, int32_t h, const section_t *s, uint32_t m, uint16_t c)
{
    int32_t i;
    if (s->from[m] == MC_HOLD)
        return;
    for (i = 0; i < w; i += 2) {
        float a = (float)s->from[m] / 127.0f, b = s->to[m] == MC_HOLD ? a : (float)s->to[m] / 127.0f;
        float v = a + (b - a) * curve(s->curve, (float)i / (float)w);
        cv_rect(x + i, y + h - (int32_t)(v * (float)h), 1, 1, c);
    }
}

static void draw_song(void)
{
    uint32_t i, first = U.sec_cur > 9u ? U.sec_cur - 9u : 0u;
    char t[40];
    cv_text(4, 0, &FONT_T, "SECTION        BARS  THEN", K_DIM);
    for (i = first; i < P.nsec && i < first + 11u; i++) {
        const section_t *s = &P.sec[i];
        int32_t y = 14 + (int32_t)(i - first) * 15;
        int playing = T.playing && G.sec == i, cued = G.cue == i;
        uint32_t m;
        if (i == U.sec_cur)
            cv_rect(0, y - 1, 240, 15, K_PANEL);
        if (playing) {                                  /* how far through it is */
            int32_t w = (int32_t)(G.sec_tick * 236u / (s->bars * BAR ? s->bars * BAR : 1u));
            cv_rect(2, y + 12, w, 2, ACCENT);
        }
        t[0] = 0;
        if (i < 9u) s_cat(t, " ");
        i_cat(t, (int32_t)i + 1);
        cv_text(4, y - 1, &FONT_XS, t, playing ? K_FG : K_DIM);
        cv_text(24, y - 1, &FONT_XS, s->name[0] ? s->name : "-", cued ? K_REC : playing ? ACCENT : K_FG);
        t[0] = 0;
        i_cat(t, s->bars);
        if (s->repeats > 1u) { s_cat(t, "x"); i_cat(t, s->repeats); }
        cv_text(86, y - 1, &FONT_XS, t, K_DIM);
        s_cpy(t, FO_NM[s->follow % NFOLLOW]);
        if (s->follow == FO_JUMP || s->follow == FO_CHANCE) { s_cat(t, " "); i_cat(t, s->target + 1); }
        if (s->follow == FO_RANDOM) { s_cat(t, " "); i_cat(t, s->target + 1); s_cat(t, "-"); i_cat(t, s->target2 + 1); }
        cv_text(120, y, &FONT_T, t, K_DIM);
        for (m = 0; m < NLANE_SEQ; m++)                 /* the lanes heard: four little marks */
            cv_rect(186 + (int32_t)m * 6, y + 3, 4, 7, s->pat[m] == 0xFFu ? K_LINE : LANE_C[m]);
        if (s->trans)
            cv_rect(212, y + 3, 7, 7, K_REC);
        for (m = 0; m < NMACRO; m++)                    /* its ramps, faint */
            ramp_draw(222, y + 1, 16, 11, s, m, dim(ACCENT));
    }
    {
        char f[56];
        s_cpy(f, G.hold ? "HOLD  " : "");
        s_cat(f, G.cue_now ? "BEAT  " : "");
        s_cat(f, G.rec_arr ? "REC ARR  " : "");
        s_cat(f, G.song ? "" : "JAM  ");
        s_cat(f, "FORM ");
        s_cat(f, FORM_NM[U.form % NFORM]);
        cv_rect(0, 182, 240, 1, K_LINE);
        cv_text(4, 185, &FONT_T, f, G.rec_arr ? K_REC : K_DIM);
    }
}

static void draw_drums(uint32_t p)
{
    uint32_t r, s;
    int32_t cw = 14;
    for (r = 0; r < NDRUM_ROW; r++) {
        int32_t y = 4 + (int32_t)r * 19;
        int sel = r == U.row;
        if (sel)
            cv_rect(0, y - 2, 240, 19, K_PANEL);
        cv_text(2, y - 1, &FONT_T, DROW_NM[r], sel ? LANE_C[LN_DRUMS] : K_DIM);
        for (s = 0; s < 16u; s++) {
            uint32_t st = U.spage * 16u + s;
            const dhit_t *h = &P.drum[p][r][st];
            int32_t x = 6 + 30 + (int32_t)s * 12 + (s / 4u) * 2, off;
            int now = T.playing && G.step[LN_DRUMS] == (int16_t)st;
            cv_rect(x, y + 13, 10, 1, now ? K_FG : K_LINE);
            if (!h->vel)
                continue;
            off = h->nudge * 5 / 12;                     /* the hit drawn as far off the grid as it plays */
            cv_rect(x + off, y + 13 - (int32_t)h->vel / 12, 10, (int32_t)h->vel / 12, now ? K_FG : LANE_C[LN_DRUMS]);
            if ((h->roll & 7u) > 1u)
                cv_rect(x + off + 3, y, 4, 2, K_FG);
            (void)cw;
        }
    }
}

static void draw_steps(uint32_t p)                      /* bass, chords, choir: 16 cells with their values */
{
    uint32_t s, len = P.info[U.lane][p].len ? P.info[U.lane][p].len : 16u;
    char t[16];
    for (s = 0; s < 16u; s++) {
        uint32_t st = U.spage * 16u + s;
        int32_t x = 4 + (int32_t)(s % 8u) * 29, y = 6 + (int32_t)(s / 8u) * 74;
        int now = T.playing && G.step[U.lane] == (int16_t)st, held = U.held == (int)s, on = step_on(U.lane, p, st);
        uint16_t c = LANE_C[U.lane];
        cv_rect(x, y, 27, 68, st >= len ? K_BG : held ? K_PANEL : K_BG);
        box(x, y, 27, 68, now ? K_FG : held ? c : st >= len ? K_BG : K_LINE);
        if (!on || st >= len)
            continue;
        t[0] = 0;
        if (U.lane == LN_BASS) {
            const bstep_t *b = &P.bass[p][st];
            int32_t hgt = 8 + (b->deg + 4) * 5;
            cv_rect(x + 3, y + 64 - hgt, 21, 4, c);
            {
                static const char *const TONE[4] = {"ROOT", "3RD", "5TH", "7TH"};
                if (b->flags & BF_ABS) { s_cpy(t, "KEY "); i_cat(t, b->deg + 1); }
                else if (b->deg >= 0 && b->deg < 4) s_cpy(t, TONE[b->deg]);
                else { t[0] = 0; sgn_cat(t, b->deg); }
            }
            cv_text(x + 3, y + 2, &FONT_T, t, K_FG);
            t[0] = 0;
            i_cat(t, b->len);
            cv_text(x + 3, y + 14, &FONT_T, t, K_DIM);
            if (b->flags & BF_GLIDE) cv_text(x + 15, y + 14, &FONT_T, "/", c);
            if (b->wob) cv_text(x + 3, y + 50, &FONT_T, WOB_NM[(b->wob - 1u) % NWOB], K_DIM);
        } else if (U.lane == LN_CHORDS) {
            static const char *const RN[14] = {"i", "ii", "iii", "iv", "v", "vi", "vii", "I'", "II'", "III'", "IV'", "V'", "VI'", "VII'"};
            const cstep_t *ch = &P.chord[p][st];
            uint32_t i;
            (void)RN;
            t[0] = (char)('1' + ch->deg % 7u);
            t[1] = ch->deg >= 7u ? '\'' : 0;
            t[2] = 0;
            cv_text(x + 4, y + 2, &FONT_B, t, c);
            for (i = 0; i < sizeof COLOURS && COLOURS[i] != ch->mods; i++)
                ;
            if (ch->mods)
                cv_text(x + 3, y + 22, &FONT_T, i < sizeof COLOURS ? COLOUR_NM[i] : "+", K_DIM);
            t[0] = 0;
            i_cat(t, ch->len);
            cv_text(x + 3, y + 50, &FONT_T, ch->len ? t : "STAB", K_DIM);
        } else {
            const vstep_t *v = &P.choir[p][st];
            const slice_t *sl = &BANK.sl[v->slice % NSLICE];
            cv_text(x + 3, y + 2, &FONT_T, sl->name[0] ? sl->name : "?", c);
            t[0] = 0;
            sgn_cat(t, v->pitch);
            cv_text(x + 3, y + 16, &FONT_T, t, K_FG);
            t[0] = 'F';
            t[1] = 0;
            sgn_cat(t, v->formant);
            cv_text(x + 3, y + 30, &FONT_T, t, K_DIM);
            if (v->flags & VF_REV) cv_text(x + 3, y + 44, &FONT_T, "REV", K_REC);
        }
    }
}

static void draw_air(void)
{
    static const char *const NM[5] = {"RAIN", "CRACKLE", "HISS", "WIND", "HUM"};
    const uint8_t v[5] = {P.snd.rain, P.snd.crackle, P.snd.hiss, P.snd.wind, P.snd.hum};
    uint32_t i;
    for (i = 0; i < 5u; i++) {
        int32_t y = 10 + (int32_t)i * 30;
        cv_text(8, y, &FONT_XS, NM[i], K_FG);
        bar(80, y + 5, 150, 6, v[i], 127, LANE_C[LN_AIR]);
    }
    cv_text(8, 146, &FONT_T, "PRESETS TURNS THE HUM", K_DIM);
}

static void draw_lane(void)
{
    uint32_t p = lane_pat(), l, pg;
    char t[40];
    if (U.lane == LN_AIR) {
        draw_air();
    } else if (U.lane == LN_DRUMS) {
        draw_drums(p);
    } else {
        draw_steps(p);
    }
    /* the foot: pattern, page, the lanes' meters, SCREW and UNSNAP */
    cv_rect(0, 160 + 0, 240, 1, K_LINE);
    if (U.lane < NLANE_SEQ) {
        uint32_t sp = P.sec[G.sec].pat[U.lane];
        s_cpy(t, "PAT ");
        if (sp == 0xFFu) s_cat(t, "SILENT");
        else i_cat(t, (int32_t)p + 1);
        s_cat(t, "  ");
        s_cat(t, RATE_NM[P.info[U.lane][p].rate & 3u]);
        s_cat(t, "  ");
        i_cat(t, P.info[U.lane][p].len);
        s_cat(t, " ST");
        cv_text(4, 163, &FONT_T, t, K_DIM);
        for (pg = 0; pg < 4u; pg++)                     /* the four pages of steps */
            cv_rect(150 + (int32_t)pg * 9, 167, 7, 4, pg == U.spage ? K_FG : pg * 16u < P.info[U.lane][p].len ? K_DIM : K_LINE);
    }
    for (l = 0; l < NLANE; l++) {
        int32_t lv = (int32_t)(MX.meter[l] * 40.0f);
        cv_rect(196 + (int32_t)l * 8, 162, 6, 14, K_LINE);
        if (lv > 14) lv = 14;
        if (lv > 0)
            cv_rect(196 + (int32_t)l * 8, 176 - lv, 6, lv, (U.mute >> l & 1u) ? K_FAINT : LANE_C[l]);
    }
    s_cpy(t, "SCREW ");
    i_cat(t, P.screw * 100 / 127);
    s_cat(t, "%   UNSNAP ");
    i_cat(t, P.unsnap * 100 / 127);
    s_cat(t, "%");
    if (G.freeze) s_cat(t, "   FREEZE");
    cv_text(4, 182, &FONT_T, t, P.screw ? ACCENT : K_DIM);
}

static void draw_lantern(void)
{
    static const char *const WL[16] = {"I", "II", "III", "IV", "V", "VI", "VII", "V1", "V2", "V3", "V4", "V5", "V6", "V7", "V8", "BOOM"};
    static const char *const BL[11] = {"K", "SN", "CL", "HH", "OH", "RM", "SH", "PC", "RS", "DO", "TS"};
    uint32_t i, beat = (G.abs / 96u) % 4u;
    float pulse = T.playing ? 1.0f - (float)(G.abs % 96u) / 96.0f : 0.4f;
    for (i = 0; i < 16u; i++) {                         /* the white keys, as lanterns */
        int32_t x = 2 + (int32_t)i * 15, y = 96;
        int held = (int)(K.keys >> WKEY[i] & 1u);
        uint16_t c = i < 7u ? LANE_C[LN_CHORDS] : i < 15u ? LANE_C[LN_CHOIR] : K_REC;
        int32_t h = 40 + (int32_t)(pulse * 20.0f);
        cv_rect(x, y + 60 - h, 13, h, held ? K_FG : dim(c));
        cv_text(x + 1, y + 64, &FONT_T, WL[i], K_DIM);
    }
    for (i = 0; i < 11u; i++) {
        static const uint8_t AT[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};
        int32_t x = 2 + (int32_t)AT[i] * 15 + 8, y = 20;
        int held = (int)(K.keys >> BKEY[i] & 1u);
        uint16_t c = i < 8u ? LANE_C[LN_DRUMS] : ACCENT;
        cv_rect(x, y, 13, 50, held ? K_FG : dim(c));
        cv_text(x, y + 52, &FONT_T, BL[i], K_DIM);
    }
    {
        char t[32] = "BEAT ";
        i_cat(t, (int32_t)beat + 1);
        s_cat(t, U.rec ? "   REC: HITS LAND AS PLAYED" : "   GLO: KEYS BACK TO STEPS");
        cv_text(4, 184, &FONT_T, t, U.rec ? K_REC : K_DIM);
    }
}

static void draw_list_sec(void)
{
    uint32_t vis = 11, first = U.secrow >= vis ? U.secrow - vis + 1u : 0u, r;
    char l[32], v[32];
    s_cpy(l, "SECTION ");
    i_cat(l, U.sec_cur + 1);
    s_cat(l, "  ");
    s_cat(l, cursec()->name);
    cv_text(6, 0, &FONT_T, l, ACCENT);
    for (r = first; r < NSR && r < first + vis; r++) {
        int32_t y = 14 + (int32_t)(r - first) * 16;
        if (r == U.secrow)
            cv_rect(2, y - 1, 236, 16, K_PANEL);
        sec_label(r, l);
        sec_value(r, v);
        cv_text(10, y, &FONT_XS, l, r == U.secrow ? K_FG : K_DIM);
        text_r(230, y, &FONT_XS, v, r == U.secrow ? ACCENT : K_FG);
    }
}

static void draw_list_snd(void)
{
    uint32_t n = snd_count(), vis = 11, first = U.sndrow >= vis ? U.sndrow - vis + 1u : 0u, r;
    char v[24];
    cv_text(6, 0, &FONT_T, LANE_NM[U.lane], LANE_C[U.lane]);
    for (r = first; r < n && r < first + vis; r++) {
        const srow_t *s = snd_row(r);
        int32_t y = 14 + (int32_t)(r - first) * 16;
        if (r == U.sndrow)
            cv_rect(2, y - 1, 236, 16, K_PANEL);
        cv_text(10, y, &FONT_XS, s->label, r == U.sndrow ? K_FG : K_DIM);
        snd_value(s, v);
        text_r(230, y, &FONT_XS, v, r == U.sndrow ? ACCENT : K_FG);
    }
}

static void draw_list_setup(void)
{
    uint32_t vis = 11, first = U.setrow >= vis ? U.setrow - vis + 1u : 0u, r;
    char v[32];
    cv_text(6, 0, &FONT_T, "SETUP", ACCENT);
    for (r = first; r < NGR_ROWS && r < first + vis; r++) {
        int32_t y = 14 + (int32_t)(r - first) * 16;
        if (r == U.setrow)
            cv_rect(2, y - 1, 236, 16, K_PANEL);
        cv_text(10, y, &FONT_XS, GR_NM[r], r == U.setrow ? K_FG : K_DIM);
        setup_value(r, v);
        text_r(230, y, &FONT_XS, v, r == U.setrow ? ACCENT : K_FG);
    }
}

static void draw_knobs(void)
{
    char a[24], b[24], c[24], d[24];
    if (U.page == PG_SONG) {
        const section_t *s = cursec();
        s_cpy(a, "BARS "); i_cat(a, s->bars);
        s_cpy(b, "x"); i_cat(b, s->repeats);
        s_cpy(c, FO_NM[s->follow % NFOLLOW]);
        s_cpy(d, "TO "); i_cat(d, s->target + 1);
        kit_knobs(a, b, c, d, ACCENT);
    } else if (U.page != PG_LANE) {
        kit_knobs("VALUE", "", "", "", ACCENT);
    } else if (U.held >= 0 && U.lane < NLANE_SEQ) {
        static const char *const H[4][4] = {{"VELOCITY", "NUDGE", "ROLL", "DRIFT"}, {"DEGREE", "LENGTH", "GLIDE/OCT", "WOBBLE"},
                                            {"DEGREE", "COLOUR", "LENGTH", "VELOCITY"}, {"SLICE", "PITCH", "FORMANT", "LENGTH"}};
        kit_knobs(H[U.lane][0], H[U.lane][1], H[U.lane][2], H[U.lane][3], K_FG);
    } else {
        static const char *const L[5][4] = {{"TONE", "DECAY", "UNSNAP", "SHUFFLE"}, {"MODE", "CUTOFF", "WOBBLE", "RATE"},
                                            {"WAVE", "CUTOFF", "SPREAD", "RELEASE"}, {"LEVEL", "GRAIN", "REVERB", "PUMP"},
                                            {"RAIN", "CRACKLE", "WIND", "HISS"}};
        kit_knobs(L[U.lane][0], L[U.lane][1], L[U.lane][2], L[U.lane][3], LANE_C[U.lane]);
    }
}

static void draw(void)
{
    draw_head();
    cv_begin(240, 200, K_BG);
    if (K.card < NB && CARD_TEXT[K.card])
        kit_card(K.card, CARD_TEXT[K.card], 200, ACCENT);
    else if (U.lantern && U.page == PG_LANE)
        draw_lantern();
    else if (U.page == PG_SONG)
        draw_song();
    else if (U.page == PG_SECTION)
        draw_list_sec();
    else if (U.page == PG_SOUND)
        draw_list_snd();
    else if (U.page == PG_SETUP)
        draw_list_setup();
    else
        draw_lane();
    cv_blit(0, 22);
    draw_knobs();
    if (U.held >= 0 && U.page == PG_LANE && U.lane != LN_DRUMS && !kit_hint(HINTS, ACCENT)) {
        cv_begin(240, 18, LANE_C[U.lane]);
        cv_text(4, 2, &FONT_T, U.lane == LN_BASS ? "BLACK: CHORD TONE 1-7" : U.lane == LN_CHORDS ? "BLACK: DEGREE I-VII" :
                               "BLACK 1-8: SLICE  9: REVERSE  10: FOLLOW CHORD", K_BG);
        cv_blit(0, 222);
    } else {
        kit_hint(HINTS, ACCENT);
    }
}

static void lights(void)
{
    uint32_t btn = 0, keys = K.keys, dkeys = 0, i;
    static const uint8_t PB[NPG] = {B_SEQ, B_HOME, B_EDIT, B_ENV, B_LFO};
    btn |= 1u << PB[U.page];
    if (T.playing)
        btn |= 1u << B_PLAY | LED_PLAY_GREEN;
    if (U.rec)
        btn |= 1u << B_REC;
    if (U.dirty)
        btn |= 1u << B_SAVE;
    if (U.lantern)
        btn |= 1u << B_GLO;
    if (G.freeze)
        btn |= 1u << B_FX;
    if (U.lantern) {                                    /* the lanterns: lit on the beat, dim between */
        if (!T.playing || G.abs % 96u < 24u)
            keys |= 0x7FFFFFFu;
        dkeys = 0x7FFFFFFu;
    } else if (U.page == PG_SONG) {
        for (i = 0; i < 16u; i++) {
            uint32_t s = U.spage * 16u + i;
            if (s >= P.nsec)
                continue;
            if ((T.playing && G.sec == s) || (G.cue == s && (plat_ms() / 150u) & 1u))
                keys |= 1u << WKEY[i];
            else
                dkeys |= 1u << WKEY[i];
        }
        if (G.hold) keys |= 1u << BKEY[0];
        if (G.cue_now) keys |= 1u << BKEY[1];
        if (G.rec_arr) keys |= 1u << BKEY[2];
        if (G.song) dkeys |= 1u << BKEY[8];
    } else if (U.page == PG_LANE && U.lane < NLANE_SEQ) {
        uint32_t p = lane_pat();
        for (i = 0; i < 16u; i++) {
            uint32_t s = U.spage * 16u + i;
            if (T.playing && G.step[U.lane] == (int16_t)s)
                keys |= 1u << WKEY[i];
            else if (step_on(U.lane, p, s))
                dkeys |= 1u << WKEY[i];
        }
        if (U.lane == LN_DRUMS)
            keys |= 1u << BKEY[U.row];
        else
            for (i = 0; i < (U.lane == LN_CHOIR ? 8u : 7u); i++)
                dkeys |= 1u << BKEY[i];
    }
    plat_lights(btn, keys, 0, dkeys, 1);
}
