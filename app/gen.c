/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* GLOAM's generators (ARP writes the lane's pattern; SEL + ARP takes it back), the arranger's forms, and the project a
 * new FM-1 starts with. Each generator knows the genre:
 *   DRUMS   2-STEP (garage: the kick skips, snares on 2 and 4, shuffled hats, every hit its own drift),
 *           HALF-TIME (dubstep at 140: kick on 1, snare on 3, sparse), WITCH (slow, trap triplet rolls falling in pitch)
 *   BASS    the chord's root, held or pulsed, an octave jump, a glide, wobble rates that change at the bar
 *   CHORDS  four bars from minor progressions, held, colours by chance
 *   CHOIR   slices on the off-beats, some reversed, following the chord, the voice made smaller or larger */

enum { DS_2STEP, DS_HALF, DS_WITCH, NDSTYLE };
static const char *const DSTYLE_NM[NDSTYLE] = {"2-STEP", "HALF-TIME", "WITCH"};

static void hit(uint32_t p, uint32_t r, uint32_t s, uint32_t vel, uint32_t *seed)
{
    dhit_t *h = &P.drum[p][r][s % NSTEP];
    h->vel = (uint8_t)vel;
    h->nudge = 0;
    h->drift = (int8_t)((int32_t)(xs32(seed) % 25u) - 12);
    h->roll = 0;
}

static void gen_drums(uint32_t p, uint32_t style, uint32_t seed)
{
    uint32_t r, s, n = 32;
    for (r = 0; r < NDRUM_ROW; r++)
        for (s = 0; s < NSTEP; s++)
            P.drum[p][r][s].vel = 0;
    P.info[LN_DRUMS][p].len = (uint8_t)n;
    P.info[LN_DRUMS][p].rate = 0;
    switch (style) {
    case DS_2STEP:                                      /* garage: a skipping kick, snares on 2 and 4 */
        for (s = 0; s < n; s += 16) {
            hit(p, DRW_KICK, s, 120, &seed);
            if (xs32(&seed) & 1u) hit(p, DRW_KICK, s + 10, 96, &seed);
            else hit(p, DRW_KICK, s + 11, 90, &seed);
            hit(p, DRW_SNARE, s + 4, 112, &seed);
            hit(p, DRW_SNARE, s + 12, 112, &seed);
            hit(p, DRW_CLAP, s + 12, 70, &seed);
        }
        for (s = 0; s < n; s += 2)
            hit(p, DRW_HAT, s + 1u, 60u + xs32(&seed) % 40u, &seed);   /* the off-beat hats */
        for (s = 0; s < 3u; s++)
            hit(p, DRW_RIM, xs32(&seed) % n, 50 + xs32(&seed) % 30u, &seed);
        hit(p, DRW_SHAKE, 7, 40, &seed);
        hit(p, DRW_SHAKE, 23, 40, &seed);
        P.snd.swing = 70;
        break;
    case DS_HALF:                                       /* dubstep: the kick on 1, the snare on 3 */
        for (s = 0; s < n; s += 16) {
            hit(p, DRW_KICK, s, 127, &seed);
            hit(p, DRW_SNARE, s + 8, 120, &seed);
            hit(p, DRW_CLAP, s + 8, 90, &seed);
        }
        if (xs32(&seed) & 1u) hit(p, DRW_KICK, 27, 100, &seed);
        else hit(p, DRW_KICK, 14, 90, &seed);
        for (s = 0; s < n; s += 4)
            hit(p, DRW_HAT, s + 2u, 70u + xs32(&seed) % 30u, &seed);
        hit(p, DRW_OHAT, 30, 80, &seed);
        hit(p, DRW_PERC, 21, 60, &seed);
        P.snd.swing = 20;
        break;
    default:                                            /* witch house: slow, rolls that fall */
        for (s = 0; s < n; s += 16) {
            hit(p, DRW_KICK, s, 127, &seed);
            hit(p, DRW_SNARE, s + 8, 116, &seed);
        }
        hit(p, DRW_KICK, 22, 100, &seed);
        for (s = 0; s < n; s += 4) {
            hit(p, DRW_HAT, s, 72, &seed);
            if (xs32(&seed) % 3u == 0u) {
                P.drum[p][DRW_HAT][s].roll = (uint8_t)(3u | 8u);   /* a triplet that falls */
                if (xs32(&seed) & 1u) P.drum[p][DRW_HAT][s].roll = (uint8_t)(6u | 8u);
            }
        }
        hit(p, DRW_OHAT, 14, 70, &seed);
        hit(p, DRW_CLAP, 24, 80, &seed);
        P.snd.swing = 0;
        break;
    }
}

static void gen_bass(uint32_t p, uint32_t seed)
{
    uint32_t s, n = 32;
    for (s = 0; s < NSTEP; s++)
        P.bass[p][s].deg = 0x7F;
    P.info[LN_BASS][p].len = (uint8_t)n;
    P.info[LN_BASS][p].rate = 0;
    for (s = 0; s < n; s += 8) {
        bstep_t *b = &P.bass[p][s];
        uint32_t r = xs32(&seed) % 6u;
        b->deg = 0;
        b->len = (uint8_t)(r < 3u ? 8u : 3u);
        b->vel = (uint8_t)(s % 16u ? 100u : 120u);
        b->flags = 0;
        b->wob = (uint8_t)(r == 2u ? 4u : r == 4u ? 6u : 0u);
        b->nudge = 0;
        b->drift = (int8_t)((int32_t)(xs32(&seed) % 9u) - 4);
        if (r == 3u) {                                  /* an octave answer, glided */
            bstep_t *c = &P.bass[p][s + 5u];
            *c = *b;
            c->len = 2;
            c->flags = BF_GLIDE | BF_OCT_UP;
        }
    }
}

static const uint8_t PROGS[][4] = {{0, 5, 2, 6}, {0, 3, 5, 4}, {0, 6, 5, 6}, {0, 5, 3, 4}, {5, 3, 0, 6}, {0, 2, 3, 5}};
#define NPROG (sizeof PROGS / sizeof PROGS[0])
static void gen_chords(uint32_t p, uint32_t seed)
{
    uint32_t s, k, pr = xs32(&seed) % NPROG;
    for (s = 0; s < NSTEP; s++)
        P.chord[p][s].deg = 0xFFu;
    P.info[LN_CHORDS][p].len = 64;
    P.info[LN_CHORDS][p].rate = 0;
    for (k = 0; k < 4u; k++) {
        cstep_t *c = &P.chord[p][k * 16u];
        c->deg = PROGS[pr][k];
        c->mods = (uint8_t)(xs32(&seed) % 3u == 0u ? CM_ADD9 : xs32(&seed) % 4u == 0u ? CM_7 : 0u);
        c->len = 16;
        c->vel = 90;
    }
}

static void gen_choir(uint32_t p, uint32_t seed)
{
    uint32_t s, n = 32, k;
    for (s = 0; s < NSTEP; s++)
        P.choir[p][s].slice = 0xFFu;
    P.info[LN_CHOIR][p].len = (uint8_t)n;
    P.info[LN_CHOIR][p].rate = 0;
    for (k = 0; k < 5u; k++) {
        static const uint8_t AT[8] = {2, 6, 11, 14, 18, 22, 27, 30};
        vstep_t *v = &P.choir[p][AT[xs32(&seed) % 8u]];
        v->slice = (uint8_t)(xs32(&seed) % (BANK.n ? BANK.n : 4u));
        v->pitch = (int8_t)(xs32(&seed) % 3u == 0u ? 12 : 0);
        v->formant = (int8_t)((int32_t)(xs32(&seed) % 9u) - 4);
        v->flags = (uint8_t)(VF_FOLLOW | (xs32(&seed) % 4u == 0u ? VF_REV : 0u));
        v->len = (uint8_t)(2u + xs32(&seed) % 6u);
        v->vel = (uint8_t)(70u + xs32(&seed) % 50u);
        v->nudge = 0;
        v->drift = (int8_t)((int32_t)(xs32(&seed) % 13u) - 6);
    }
}

/* ---- the arranger's forms: the song rewritten as sections that use the patterns there are ---- */
enum { FORM_DROP, FORM_LOOP, FORM_WITCH, FORM_GHOST, NFORM };
static const char *const FORM_NM[NFORM] = {"DROP", "LOOP", "WITCH", "GHOST"};

static void sec_clear(section_t *s)
{
    uint32_t m, i;
    for (i = 0; i < sizeof(section_t); i++)
        ((uint8_t *)s)[i] = 0;
    s->bars = 4;
    s->repeats = 1;
    s->follow = FO_NEXT;
    s->chance = 50;
    s->rows = 0xFF;
    for (i = 0; i < NLANE_SEQ; i++)
        s->pat[i] = 0;
    for (m = 0; m < NMACRO; m++)
        s->from[m] = s->to[m] = MC_HOLD;
    s->used = 1;
}
static void sec_name(section_t *s, const char *n)
{
    uint32_t i;
    for (i = 0; i < 7u && n[i]; i++)
        s->name[i] = n[i];
    s->name[i] = 0;
}
static section_t *sec_add(const char *name, uint32_t bars, uint32_t dr, uint32_t bs, uint32_t ch, uint32_t cv)
{
    section_t *s;
    if (P.nsec >= NSEC)
        return &P.sec[NSEC - 1u];
    s = &P.sec[P.nsec++];
    sec_clear(s);
    sec_name(s, name);
    s->bars = (uint8_t)bars;
    s->pat[LN_DRUMS] = (uint8_t)dr;
    s->pat[LN_BASS] = (uint8_t)bs;
    s->pat[LN_CHORDS] = (uint8_t)ch;
    s->pat[LN_CHOIR] = (uint8_t)cv;
    return s;
}
static void ramp(section_t *s, uint32_t m, uint32_t a, uint32_t b)
{
    s->from[m] = (uint8_t)a;
    s->to[m] = (uint8_t)b;
}
#define OFF 0xFFu

static void form_apply(uint32_t f)
{
    section_t *s;
    P.nsec = 0;
    switch (f) {
    case FORM_DROP:                                     /* melodic dubstep: build, drop, break, drop */
        s = sec_add("INTRO", 8, OFF, OFF, 0, 0); ramp(s, MC_FILTER, 30, 90); ramp(s, MC_AIR, 110, 70); s->curve = CV_EASE_IN;
        s = sec_add("BUILD", 8, 0, OFF, 0, 0); ramp(s, MC_ENERGY, 40, 127); ramp(s, MC_FILTER, 70, 127);
        s->trans = TR_RISER | TR_FILL | TR_DROPOUT | TR_IMPACT;
        s = sec_add("DROP", 16, 1, 0, 0, 1); ramp(s, MC_PUMP, 110, 110); ramp(s, MC_WOBBLE, 60, 127);
        s->trans = TR_SWELL;
        s = sec_add("BREAK", 8, OFF, OFF, 0, 0); ramp(s, MC_SCREW, 0, 40); ramp(s, MC_VERB, 80, 120); s->curve = CV_S;
        s->trans = TR_RISER | TR_TAPESTOP | TR_IMPACT;
        s = sec_add("DROP 2", 16, 1, 1, 0, 1); ramp(s, MC_PUMP, 110, 110);
        s = sec_add("OUTRO", 8, OFF, OFF, 0, 0); ramp(s, MC_FILTER, 127, 20); ramp(s, MC_AIR, 60, 127);
        s->follow = FO_FIRST;
        break;
    case FORM_LOOP:                                     /* one groove that varies itself */
        s = sec_add("GROOVE", 4, 0, 0, 0, OFF); s->repeats = 2; s->follow = FO_RANDOM; s->target = 1; s->target2 = 3;
        s = sec_add("VOICES", 4, 0, 0, 0, 0); s->follow = FO_JUMP; s->target = 0;
        s = sec_add("BARE", 4, 0, OFF, 0, 0); s->rows = 0x0F; s->follow = FO_JUMP; s->target = 0;
        s = sec_add("LIFT", 4, 0, 0, 0, 0); ramp(s, MC_FILTER, 80, 127); s->trans = TR_FILL; s->follow = FO_JUMP; s->target = 0;
        break;
    case FORM_WITCH:                                    /* slow, screwed, drowned */
        s = sec_add("SUMMON", 8, OFF, OFF, 0, 0); ramp(s, MC_SCREW, 60, 60); ramp(s, MC_VERB, 120, 120); ramp(s, MC_AIR, 90, 90);
        s = sec_add("RITE", 16, 2, 0, 0, 0); ramp(s, MC_SCREW, 50, 50); ramp(s, MC_VERB, 100, 100); s->trans = TR_TAPESTOP;
        s = sec_add("VOID", 4, OFF, OFF, OFF, 0); ramp(s, MC_SCREW, 50, 90); ramp(s, MC_VERB, 127, 127); s->curve = CV_EASE_IN;
        s->trans = TR_IMPACT;
        s = sec_add("RITE 2", 16, 2, 1, 0, 1); ramp(s, MC_SCREW, 50, 50); s->follow = FO_CHANCE; s->target = 2; s->chance = 50;
        s = sec_add("FADE", 8, OFF, OFF, 0, 0); ramp(s, MC_FILTER, 100, 10); ramp(s, MC_SCREW, 50, 80); s->follow = FO_STOP;
        break;
    default:                                            /* GHOST: Burial's way, sections that drift in and out */
        s = sec_add("RAIN", 4, OFF, OFF, 0, 0); ramp(s, MC_AIR, 127, 127); ramp(s, MC_FILTER, 50, 70);
        s = sec_add("STEPS", 8, 0, 0, 0, 0); ramp(s, MC_AIR, 80, 80); s->repeats = 2;
        s->follow = FO_CHANCE; s->target = 3; s->chance = 40;
        s = sec_add("STREET", 8, 0, OFF, 0, 1); s->rows = 0xFE; s->follow = FO_JUMP; s->target = 1;
        s = sec_add("MEMORY", 4, OFF, OFF, 0, 0); ramp(s, MC_SCREW, 0, 30); ramp(s, MC_VERB, 90, 127); ramp(s, MC_AIR, 127, 127);
        s->trans = TR_SWELL; s->follow = FO_RANDOM; s->target = 1; s->target2 = 2;
        break;
    }
}

/* ---- a new project: a song that plays at once ---- */
static void sound_defaults(sound_t *s)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)s;
    for (i = 0; i < sizeof *s; i++)
        b[i] = 0;
    s->tone = 60; s->decay = 70; s->level = 110; s->unsnap = 90; s->swing = 60;
    s->bmode = BM_WOBBLE; s->bcut = 40; s->breso = 70; s->bdrive = 40; s->bwob = 90; s->bwob_rate = 3; s->bglide = 40; s->blevel = 110;
    s->cwave = WV_SUPER; s->ccut = 60; s->cspread = 80; s->catt = 40; s->crel = 80; s->clevel = 70;
    s->vlevel = 100; s->vgrain = 50; s->vtexture = 30;
    s->rain = 60; s->crackle = 30; s->hiss = 20; s->wind = 30; s->hum = 10;
    s->send[LN_DRUMS] = 25; s->send[LN_BASS] = 5; s->send[LN_CHORDS] = 70; s->send[LN_CHOIR] = 100; s->send[LN_AIR] = 50;
    s->pump[LN_CHORDS] = 80; s->pump[LN_BASS] = 60; s->pump[LN_CHOIR] = 40; s->pump[LN_AIR] = 50;
    s->verb_size = 95; s->verb_tone = 60;
    s->chan[LN_DRUMS] = 10; s->chan[LN_BASS] = 2; s->chan[LN_CHORDS] = 3; s->chan[LN_CHOIR] = 4;
}

static void project_new(void)
{
    uint32_t i, l, p, s;
    uint8_t *b = (uint8_t *)&P;
    for (i = 0; i < sizeof P; i++)
        b[i] = 0;
    P.bpm10 = 1400;
    P.root = 1;                                         /* C# minor */
    P.scale = SC_MINOR;
    P.clk_mode = CK_INTERNAL;
    P.clk_send = 1;
    for (i = 0; i < NMACRO; i++)
        P.mac[i] = 100;
    P.mac[MC_SCREW] = 0;
    P.mac[MC_ENERGY] = 127;
    P.mac[MC_FILTER] = 127;
    P.mac[MC_AIR] = 70;
    P.mac[MC_VERB] = 70;
    P.mac[MC_PUMP] = 90;
    P.mac[MC_WOBBLE] = 80;
    sound_defaults(&P.snd);
    for (l = 0; l < NLANE_SEQ; l++)
        for (p = 0; p < NPAT; p++) {
            P.info[l][p].len = l == LN_CHORDS ? 64 : 32;
            P.info[l][p].rate = 0;
        }
    for (p = 0; p < NPAT; p++)
        for (s = 0; s < NSTEP; s++) {
            P.bass[p][s].deg = 0x7F;
            P.chord[p][s].deg = 0xFFu;
            P.choir[p][s].slice = 0xFFu;
        }
    gen_drums(0, DS_2STEP, 11);
    gen_drums(1, DS_HALF, 23);
    gen_drums(2, DS_WITCH, 5);
    gen_bass(0, 7);
    gen_bass(1, 19);
    P.info[LN_BASS][1].len = 32;
    gen_chords(0, 3);
    gen_choir(0, 9);
    gen_choir(1, 31);
    P.snd.swing = 60;
    form_apply(FORM_DROP);
}
