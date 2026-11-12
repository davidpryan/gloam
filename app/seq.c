/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* GLOAM's engine: the lanes, the arrangement, the macros. It runs in the render (core/clock.c calls seq_tick on each
 * of 96 ticks a quarter, at its sample).
 * Each lane's next step is looked at half a step early, so a hit nudged before the grid, or drifting (its own drift
 * times UNSNAP), still lands on its tick; what is due waits in a small queue. The song is the list of sections: each
 * plays its patterns for its bars, its repeats times, then its follow action decides what is next, unless a cue (a
 * section launched by hand) or HOLD says otherwise. A section's macros move from their FROM to their TO along its
 * curve; its transition plays in its last bar (a fill, a riser, a swell, a dropout, a tape stop) and at the next
 * downbeat (an impact). */

#define BAR 384u
static const uint8_t RATE_T[4] = {24, 16, 12, 48};
static const char *const RATE_NM[4] = {"1/16", "1/16T", "1/32", "1/8"};

typedef struct {
    uint32_t due;                                       /* tick */
    uint8_t lane, row, pat, step;
    uint8_t vel, used, roll_i, rsv;
} ev_t;
#define NEVQ 64u
static ev_t EVQ[NEVQ];

static struct {
    uint8_t sec;                                        /* the section playing */
    uint8_t pass;                                       /* passes of it done */
    uint8_t cue, cue_now;                               /* a section waiting (0xFF none); at the beat, not the bar */
    uint8_t hold;                                       /* HOLD: it loops */
    uint8_t song;                                       /* 1: the follow actions run; 0: jam (it loops until cued) */
    uint8_t ended;
    uint32_t sec_tick;                                  /* ticks into this pass */
    uint32_t abs;                                       /* ticks since PLAY */
    uint8_t pat[NLANE_SEQ];                             /* what each lane plays (0xFF silent) */
    int8_t transpose;
    int16_t step[NLANE_SEQ];                            /* each lane's step now (the screen) */
    /* the chord */
    int8_t chord[6];
    uint8_t chord_n;
    int8_t pad[6];
    uint8_t pad_n;
    int8_t chord_root;
    /* the macros now, 0..1 */
    float mac[NMACRO];
    /* transitions */
    uint8_t trans_on;                                   /* the transition of this pass is under way */
    uint8_t tape;                                       /* tape stop running */
    float tape_mul;
    uint8_t impact_next;
    uint8_t freeze, dropout_hand, riser_hand, tape_hand;
    uint32_t riser_until, swell_until;
    /* arrangement recording */
    uint8_t rec_arr, rec_n;
    struct {
        uint8_t sec, bars;
    } rec_list[NSEC];
    uint32_t rec_start;
    uint32_t seed;
    uint8_t screw_last;
    uint16_t bpm_base;
} G;

static uint32_t step_ticks(uint32_t lane, uint32_t pat) { return RATE_T[P.info[lane][pat % NPAT].rate & 3u]; }

/* ------------------------------------------------------------------ the chord --- */
static int32_t key_deg(int32_t d, int32_t base) { return scale_deg(d, P.root, P.scale, base) + G.transpose; }

static void chord_from(const cstep_t *c)
{
    int32_t d = c->deg % 7u, up = c->deg >= 7u ? 12 : 0, r, t3, t5;
    uint32_t n = 0, i, k;
    int32_t centre = 60, sum = 0;
    r = key_deg(d, 48) + up;
    t3 = key_deg(d + 2, 48) + up;
    t5 = key_deg(d + 4, 48) + up;
    if (c->mods & CM_FLIP)
        t3 = (t3 - r) % 12 == 4 ? t3 - 1 : t3 + 1;
    if (c->mods & CM_SUS2)
        t3 = key_deg(d + 1, 48) + up;
    if (c->mods & CM_SUS4)
        t3 = key_deg(d + 3, 48) + up;
    G.chord[n++] = (int8_t)r;
    G.chord[n++] = (int8_t)t3;
    G.chord[n++] = (int8_t)t5;
    if (c->mods & CM_7)
        G.chord[n++] = (int8_t)(key_deg(d + 6, 48) + up);
    if (c->mods & CM_ADD9)
        G.chord[n++] = (int8_t)(key_deg(d + 8, 48) + up);
    G.chord_n = (uint8_t)n;
    G.chord_root = (int8_t)r;
    /* voice-led around the last voicing's middle */
    for (i = 0; i < G.pad_n; i++)
        sum += G.pad[i];
    if (G.pad_n)
        centre = sum / (int32_t)G.pad_n;
    if (centre < 54 || centre > 70)
        centre = 62;
    for (i = 0; i < n; i++) {
        int32_t x = G.chord[i];
        while (x < centre - 6)
            x += 12;
        while (x > centre + 6)
            x -= 12;
        if (P.snd.cspread > 64u && (i & 1u))
            x += 12;
        G.pad[i] = (int8_t)x;
    }
    G.pad_n = (uint8_t)n;
    for (i = 1; i < n; i++)
        for (k = i; k > 0u && G.pad[k] < G.pad[k - 1u]; k--) {
            int8_t t = G.pad[k];
            G.pad[k] = G.pad[k - 1u];
            G.pad[k - 1u] = t;
        }
}

/* ------------------------------------------------------------------ the sounds of a step --- */
static patch_t drum_patch, chord_patch;
static void patches_update(void)
{
    const sound_t *s = &P.snd;
    patch_default(&drum_patch, PT_DRUM, s->chan[LN_DRUMS]);
    drum_patch.cutoff = s->tone;
    drum_patch.decay = s->decay;
    drum_patch.level = s->level;
    patch_default(&chord_patch, PT_SYNTH, s->chan[LN_CHORDS]);
    chord_patch.wave = s->cwave;
    chord_patch.cutoff = s->ccut;
    chord_patch.reso = 30;
    chord_patch.envmod = 40;
    chord_patch.attack = s->catt;
    chord_patch.decay = 70;
    chord_patch.sustain = 90;
    chord_patch.release = s->crel;
    chord_patch.level = s->clevel;
    chord_patch.detune = 30;
}

static void drum_hit(uint32_t row, uint32_t vel, float pitch)
{
    if (row == DRW_KICK)
        duck_hit();
    hit_pitch = pitch;
    play_note(LN_DRUMS, &drum_patch, DROW_NOTE[row], vel, ticks_to_samples(12), 0);
}

static void chord_play(uint32_t vel, uint32_t len_ticks)
{
    uint32_t i;
    offs_owner(LN_CHORDS);
    for (i = 0; i < G.pad_n; i++)
        play_note(LN_CHORDS, &chord_patch, G.pad[i], vel, ticks_to_samples(len_ticks), 0);
}

static int32_t bass_note(const bstep_t *b)
{
    int32_t n;
    if (b->flags & BF_ABS)
        n = key_deg(b->deg, 36);
    else {                                              /* a degree of the chord: 0 its root, 1 its third ... */
        int32_t i = b->deg, oct = 0;
        if (!G.chord_n)
            n = key_deg(0, 36);
        else {
            while (i < 0) { i += (int32_t)G.chord_n; oct--; }
            n = G.chord[(uint32_t)i % G.chord_n] + 12 * (oct + i / (int32_t)G.chord_n);
            while (n > 52) n -= 12;
            while (n < 28) n += 12;
        }
    }
    if (b->flags & BF_OCT_UP) n += 12;
    if (b->flags & BF_OCT_DN) n -= 12;
    return n;
}

static void play_bass(const bstep_t *b, uint32_t st)
{
    int32_t n = bass_note(b);
    uint32_t wob = b->wob ? WOB_T[(b->wob - 1u) % NWOB] : WOB_T[P.snd.bwob_rate % NWOB];
    bass_on(n, b->vel, ticks_to_samples(st * (b->len ? b->len : 1u)) * 15u / 16u, b->flags & BF_GLIDE, wob, &P.snd);
    if (P.snd.chan[LN_BASS])
        midi_out(0x90u | (P.snd.chan[LN_BASS] - 1u), (uint32_t)n, b->vel), midi_out(0x80u | (P.snd.chan[LN_BASS] - 1u), (uint32_t)n, 0);
    heard[LN_BASS] = 8;
}

static void play_choir(const vstep_t *v, uint32_t st)
{
    float pitch = (float)v->pitch + (float)G.transpose;
    const slice_t *s = &BANK.sl[v->slice % NSLICE];
    if (v->flags & VF_FOLLOW && G.chord_n) {            /* to the chord tone nearest */
        int32_t want = (int32_t)s->note + v->pitch, best = want, bd = 99, i, o;
        for (i = 0; i < (int32_t)G.chord_n; i++)
            for (o = -2; o <= 2; o++) {
                int32_t x = G.chord[i] + 12 * o, d = x - want < 0 ? want - x : x - want;
                if (d < bd) { bd = d; best = x; }
            }
        pitch = (float)(best - (int32_t)s->note);
    }
    choir_on(v->slice, pitch, (float)v->formant, v->flags & VF_REV, ticks_to_samples(st * (v->len ? v->len : 1u)),
             v->vel, (float)P.snd.vgrain / 127.0f);
    if (P.snd.chan[LN_CHOIR])
        midi_out(0x90u | (P.snd.chan[LN_CHOIR] - 1u), (uint32_t)clamp((int32_t)s->note + (int32_t)pitch, 0, 127), v->vel),
            midi_out(0x80u | (P.snd.chan[LN_CHOIR] - 1u), (uint32_t)clamp((int32_t)s->note + (int32_t)pitch, 0, 127), 0);
    heard[LN_CHOIR] = 8;
}

/* ------------------------------------------------------------------ the queue --- */
static void evq_put(uint32_t due, uint32_t lane, uint32_t row, uint32_t pat, uint32_t step, uint32_t vel, uint32_t ri)
{
    uint32_t i;
    for (i = 0; i < NEVQ; i++)
        if (!EVQ[i].used) {
            EVQ[i].due = due;
            EVQ[i].lane = (uint8_t)lane;
            EVQ[i].row = (uint8_t)row;
            EVQ[i].pat = (uint8_t)pat;
            EVQ[i].step = (uint8_t)step;
            EVQ[i].vel = (uint8_t)vel;
            EVQ[i].roll_i = (uint8_t)ri;
            EVQ[i].used = 1;
            return;
        }
}

static int32_t offset_of(int8_t nudge, int8_t drift, uint32_t step, uint32_t st)
{
    float u = (float)P.snd.unsnap * (1.0f / 127.0f) * (0.3f + (float)P.unsnap * (0.7f / 127.0f));
    int32_t o = nudge + (int32_t)((float)drift * u);
    if (st == 24u && (step & 1u))                       /* the shuffle: odd sixteenths late */
        o += (int32_t)P.snd.swing * 12 / 127;
    return clamp(o, -12, 23);
}

static int in_fill(void);
/* lane l's step s (pattern p) is coming at tick at: what it plays goes in the queue */
static void lane_look(uint32_t l, uint32_t p, uint32_t s, uint32_t at, uint32_t st)
{
    uint32_t r;
    if (l == LN_DRUMS) {
        uint8_t rows = P.sec[G.sec].rows;
        uint32_t thr = (uint32_t)((1.0f - G.mac[MC_ENERGY]) * 100.0f);   /* low energy: the quiet hits go first */
        for (r = 0; r < NDRUM_ROW; r++) {
            const dhit_t *h = &P.drum[p][r][s];
            uint32_t n, k;
            if (!h->vel || !(rows >> r & 1u) || (h->vel < thr && r != DRW_KICK))
                continue;
            if (in_fill() && r != DRW_KICK)
                continue;
            n = (h->roll & 7u) > 1u ? h->roll & 7u : 1u;
            for (k = 0; k < n; k++)
                evq_put((uint32_t)((int32_t)at + offset_of(h->nudge, h->drift, s, st)) + k * st / n, l, r, p, s, h->vel, k);
        }
        return;
    }
    if (l == LN_BASS && P.bass[p][s].deg != 0x7F)
        evq_put((uint32_t)((int32_t)at + offset_of(P.bass[p][s].nudge, P.bass[p][s].drift, s, st)), l, 0, p, s, 0, 0);
    else if (l == LN_CHORDS && P.chord[p][s].deg != 0xFFu)
        evq_put(at, l, 0, p, s, 0, 0);
    else if (l == LN_CHOIR && P.choir[p][s].slice != 0xFFu)
        evq_put((uint32_t)((int32_t)at + offset_of(P.choir[p][s].nudge, P.choir[p][s].drift, s, st)), l, 0, p, s, 0, 0);
}

static void evq_run(void)
{
    uint32_t i;
    for (i = 0; i < NEVQ; i++) {
        ev_t *e = &EVQ[i];
        uint32_t st;
        if (!e->used || (int32_t)(G.abs - e->due) < 0)
            continue;
        e->used = 0;
        st = step_ticks(e->lane, e->pat);
        switch (e->lane) {
        case LN_DRUMS: {
            const dhit_t *h = &P.drum[e->pat][e->row][e->step];
            float pitch = (h->roll & 8u) ? fm_exp2f(-(float)e->roll_i * 2.0f / 12.0f) : 1.0f;
            uint32_t vel = e->roll_i ? e->vel * 3u / 4u : e->vel;
            drum_hit(e->row, vel, pitch);
            break;
        }
        case LN_BASS: play_bass(&P.bass[e->pat][e->step], st); break;
        case LN_CHORDS: {
            const cstep_t *c = &P.chord[e->pat][e->step];
            chord_from(c);
            chord_play(c->vel, c->len ? st * c->len : 10u);
            break;
        }
        default: play_choir(&P.choir[e->pat][e->step], st); break;
        }
    }
}

/* ------------------------------------------------------------------ sections --- */
static float curve(uint32_t c, float x)
{
    switch (c) {
    case CV_EASE_IN: return x * x;
    case CV_EASE_OUT: return 1.0f - (1.0f - x) * (1.0f - x);
    case CV_AT_END: return x > 0.97f ? 1.0f : 0.0f;
    case CV_S: return x * x * (3.0f - 2.0f * x);
    default: return x;
    }
}

static uint32_t sec_len(void) { return (uint32_t)(P.sec[G.sec].bars ? P.sec[G.sec].bars : 1u) * BAR; }

static void sec_enter(uint32_t s)
{
    const section_t *sc;
    uint32_t l;
    if (G.rec_arr && G.rec_n < NSEC) {                  /* the arrangement recorder: the section just left, its bars */
        uint32_t bars = (G.abs - G.rec_start + BAR / 2u) / BAR;
        if (G.rec_n || bars) {
            if (G.rec_n)
                G.rec_list[G.rec_n - 1u].bars = (uint8_t)clamp((int32_t)bars, 1, 64);
        }
        G.rec_list[G.rec_n].sec = (uint8_t)s;
        G.rec_list[G.rec_n].bars = P.sec[s].bars;
        G.rec_n++;
        G.rec_start = G.abs;
    }
    G.sec = (uint8_t)(s % NSEC);
    sc = &P.sec[G.sec];
    G.sec_tick = 0;
    G.pass = 0;
    G.trans_on = 0;
    for (l = 0; l < NLANE_SEQ; l++) {
        G.pat[l] = sc->pat[l];
        G.step[l] = -1;
    }
    G.transpose = sc->transpose;
}

static void next_pass(void)
{
    const section_t *sc = &P.sec[G.sec];
    uint32_t to = G.sec;
    if (G.cue != 0xFFu) {
        to = G.cue;
        G.cue = 0xFFu;
        sec_enter(to);
        return;
    }
    if (G.hold || !G.song) {
        G.sec_tick = 0;
        G.trans_on = 0;
        return;
    }
    if (++G.pass < (sc->repeats ? sc->repeats : 1u)) {
        G.sec_tick = 0;
        G.trans_on = 0;
        return;
    }
    switch (sc->follow) {
    case FO_LOOP: G.sec_tick = 0; G.pass = 0; return;
    case FO_JUMP: to = sc->target; break;
    case FO_RANDOM: {
        uint32_t a = sc->target, b = sc->target2 < a ? a : sc->target2;
        to = a + xs32(&G.seed) % (b - a + 1u);
        break;
    }
    case FO_CHANCE: to = xs32(&G.seed) % 100u < sc->chance ? sc->target : G.sec + 1u; break;
    case FO_FIRST: to = 0; break;
    case FO_BACK: to = G.sec >= (sc->target ? sc->target : 1u) ? G.sec - (sc->target ? sc->target : 1u) : 0u; break;
    case FO_STOP: G.ended = 1; return;
    default: to = G.sec + 1u; break;
    }
    if (to >= P.nsec) {                                 /* past the end: the song is over */
        G.ended = 1;
        return;
    }
    sec_enter(to);
}

/* the transition plays in the last bar of a pass that leaves this section */
static int leaving(void)
{
    const section_t *sc = &P.sec[G.sec];
    if (G.cue != 0xFFu)
        return 1;
    if (G.hold || !G.song)
        return 0;
    return G.pass + 1u >= (sc->repeats ? sc->repeats : 1u) && sc->follow != FO_LOOP;
}
static int in_last(uint32_t ticks) { return G.sec_tick + ticks >= sec_len() && leaving(); }
static int in_fill(void) { return (P.sec[G.sec].trans & TR_FILL) && in_last(BAR / 2u); }

static void macros_update(void)
{
    const section_t *sc = &P.sec[G.sec];
    float x = (float)G.sec_tick / (float)sec_len();
    uint32_t m;
    for (m = 0; m < NMACRO; m++) {
        float hand = (float)P.mac[m] / 127.0f, a, b;
        if (sc->from[m] == MC_HOLD) {
            G.mac[m] = hand;
            continue;
        }
        a = (float)sc->from[m] / 127.0f;
        b = sc->to[m] == MC_HOLD ? a : (float)sc->to[m] / 127.0f;
        G.mac[m] = a + (b - a) * curve(sc->curve, x);
    }
    /* the transition's own moves */
    if ((sc->trans & TR_FILTERUP) && in_last(BAR)) {
        float y = 1.0f - (float)(sec_len() - G.sec_tick) / (float)BAR;
        G.mac[MC_FILTER] = 0.2f + 0.8f * y;
    }
    MX.riser = ((sc->trans & TR_RISER) && in_last(BAR * 2u)) ? 1.0f - (float)(sec_len() - G.sec_tick) / (float)(BAR * 2u) :
               (G.riser_until > G.abs ? 1.0f - (float)(G.riser_until - G.abs) / (float)BAR : 0.0f);
    if (MX.riser <= 0.0f)
        MX.riser_ph = 0.0f;
    MX.swell = ((sc->trans & TR_SWELL) && in_last(96u)) ? 1.5f * (1.0f - (float)(sec_len() - G.sec_tick) / 96.0f) :
               G.swell_until > G.abs ? 1.2f * (1.0f - (float)(G.swell_until - G.abs) / 96.0f) : 0.0f;
    MX.dropout = (uint8_t)(((sc->trans & TR_DROPOUT) && in_last(96u)) || G.dropout_hand);
    G.tape = (uint8_t)(((sc->trans & TR_TAPESTOP) && in_last(96u)) || G.tape_hand);
    MX.air = G.mac[MC_AIR];
    MX.verb = G.mac[MC_VERB];
    MX.filter = G.mac[MC_FILTER];
    MX.choir = G.mac[MC_CHOIR];
    MX.wobble = G.mac[MC_WOBBLE];
    MX.pump = G.mac[MC_PUMP];
}

/* SCREW (and a tape stop) slow the tempo and lower every pitch together */
static void screw_update(void)
{
    float s = G.mac[MC_SCREW] + (float)P.screw / 127.0f, mul;
    if (s > 1.0f)
        s = 1.0f;
    mul = fm_exp2f(-s);                                 /* none .. an octave down */
    if (G.tape) {
        G.tape_mul *= 0.9975f;
        if (G.tape_mul < 0.25f)
            G.tape_mul = 0.25f;
    } else {
        G.tape_mul = 1.0f;
    }
    screw_mul = mul * G.tape_mul;
    T.bpm10 = (uint16_t)clamp((int32_t)((float)G.bpm_base * screw_mul), 150, 3000);
}

/* ------------------------------------------------------------------ the clock's calls --- */
static void seq_started(void)
{
    uint32_t i;
    for (i = 0; i < NEVQ; i++)
        EVQ[i].used = 0;
    G.abs = 0;
    G.ended = 0;
    G.rec_n = 0;
    G.rec_start = 0;
    G.tape_mul = 1.0f;
    if (G.cue != 0xFFu) {
        uint32_t c = G.cue;
        G.cue = 0xFFu;
        sec_enter(c);
    } else {
        sec_enter(G.sec);
    }
    patches_update();
}
static void seq_stopped(void)
{
    uint32_t i;
    for (i = 0; i < NEVQ; i++)
        EVQ[i].used = 0;
    if (G.rec_arr && G.rec_n)                           /* the last section's bars */
        G.rec_list[G.rec_n - 1u].bars = (uint8_t)clamp((int32_t)((G.abs - G.rec_start + BAR / 2u) / BAR), 1, 64);
    screw_mul = 1.0f;
    T.bpm10 = G.bpm_base;
    MX.riser = MX.swell = 0.0f;
    MX.dropout = 0;
}

static void seq_tick(void)
{
    uint32_t l;
    const section_t *sc;
    if (G.ended) {
        T.want = 2;
        return;
    }
    /* a cue at the beat (at once) or the bar */
    if (G.cue != 0xFFu && ((G.cue_now && G.sec_tick % 96u == 0u) || G.sec_tick % BAR == 0u) && G.sec_tick) {
        uint32_t c = G.cue;
        G.cue = 0xFFu;
        if (P.sec[G.sec].trans & TR_IMPACT)
            G.impact_next = 1;
        sec_enter(c);
    }
    if (G.sec_tick >= sec_len()) {
        if (P.sec[G.sec].trans & TR_IMPACT && leaving())
            G.impact_next = 1;
        next_pass();
        if (G.ended)
            return;
    }
    sc = &P.sec[G.sec];
    if (G.impact_next && G.sec_tick == 0u) {            /* the downbeat after an IMPACT */
        G.impact_next = 0;
        drum_hit(DRW_KICK, 127, 0.75f);
        hit_pitch = 1.0f;
        play_note(LN_DRUMS, &drum_patch, 49, 120, ticks_to_samples(96), 0);
    }
    macros_update();
    screw_update();
    /* the fill: the last half bar, snares getting closer and louder */
    if (in_fill()) {
        uint32_t left = sec_len() - G.sec_tick, gap = left > 96u ? 24u : 12u;
        if (G.sec_tick % gap == 0u)
            drum_hit(DRW_SNARE, 60u + (uint32_t)(60u * (BAR / 2u - left) / (BAR / 2u)), 1.0f);
    }
    /* each lane: the step half a step ahead */
    for (l = 0; l < NLANE_SEQ; l++) {
        uint32_t p = G.pat[l], st, ahead, s, len;
        if (p == 0xFFu)
            continue;
        st = step_ticks(l, p);
        len = P.info[l][p].len ? P.info[l][p].len : 16u;
        ahead = G.sec_tick + st / 2u;
        if (ahead % st == 0u && ahead < sec_len()) {
            s = (ahead / st) % len;
            lane_look(l, p, s, G.abs + st / 2u, st);
        }
        if (G.sec_tick % st == 0u)
            G.step[l] = (int16_t)((G.sec_tick / st) % len);
    }
    (void)sc;
    evq_run();
    G.sec_tick++;
    G.abs++;
}

/* MIDI in plays the lane shown (the UI sets it) */
static volatile uint8_t midi_lane;
static void seq_midi_note(int32_t note, uint32_t vel, uint32_t chan)
{
    (void)chan;
    if (!vel)
        return;
    if (midi_lane == LN_DRUMS)
        drum_hit((uint32_t)note % NDRUM_ROW, vel, 1.0f);
    else if (midi_lane == LN_BASS) {
        bstep_t b = {0, 4, 0, BF_ABS, 0, 0, 0, 0};
        b.vel = (uint8_t)vel;
        bass_on(note, vel, ticks_to_samples(48), 0, WOB_T[P.snd.bwob_rate % NWOB], &P.snd);
        (void)b;
    } else if (midi_lane == LN_CHOIR) {
        choir_on((uint32_t)note % NSLICE, 0.0f, 0.0f, 0, ticks_to_samples(96), vel, (float)P.snd.vgrain / 127.0f);
    } else {
        cstep_t c = {0, 0, 4, 100};
        c.deg = (uint8_t)((uint32_t)note % 7u);
        chord_from(&c);
        chord_play(vel, 96);
    }
}
