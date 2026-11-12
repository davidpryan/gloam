/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* GLOAM's sound, beyond the shared synth (core/synth.c, which plays the drums and the chords):
 *   the CHOIR    two voices that sing slices of a vocal bank, pitch-synchronously (PSOLA): grains two periods long
 *                are laid at the new pitch's period, each read at the formant's rate, so the note and the size of
 *                the voice move apart. SCREW slows and lowers all three together.
 *   the BASS     one voice: an 808 (a sine that falls into its note, driven), a REESE (two detuned saws and a sub)
 *                or a WOBBLE (saw and square through a resonant low-pass an LFO moves, its rate locked to the beat)
 *   the AIR      rain, vinyl crackle, hiss, wind and mains hum, all made from noise and sines (the FM-1 has no mic)
 *   the REVERB   four delay lines in a feedback matrix, damped; FREEZE holds the tail and lets nothing in
 *   the MIX      each lane its bus; the kick ducks the others by each lane's PUMP; reverb sends; a master low-pass
 *                (the FILTER macro); a soft clip. */

#define NCV 2u                                          /* choir voices */
#define NGR 4u                                          /* grains a voice */

/* ------------------------------------------------------------------ the vocal bank --- */
/* The resource region (fm1.toml resources): "GLB1", the slice count, the rate, then 16 slices {offset, length,
 * f0 x 10, name[8]} and the samples as 8-bit mu-law. Without it (before the installer has written it) a small bank
 * is sung here at boot: four vowels. */
#define NSLICE 16u
typedef struct {
    uint32_t off, len;                                  /* bytes from the data's start */
    uint16_t f0x10;                                     /* its pitch, Hz x 10 */
    uint8_t note, rsv;                                  /* the MIDI note nearest that pitch */
    char name[8];
} slice_t;
static struct {
    const uint8_t *data;                                /* mu-law samples */
    uint32_t n, rate;
    slice_t sl[NSLICE];
    uint8_t factory;                                    /* 1: the built-in vowels */
} BANK;
static int16_t ULAW[256];
static float HANN[257];                                 /* a grain's window */
#define RV_TOTAL (1931u + 2459u + 3049u + 3727u)
static float rv_buf[RV_TOTAL] PLAT_POOL;
#define FB_LEN 7000u                                    /* the built-in vowels: 4 x 7,000 samples at 16 kHz */
static uint8_t fallback[4u * FB_LEN] PLAT_POOL;

static uint8_t ulaw_enc(int32_t x)
{
    int32_t sign = x < 0 ? 0x80 : 0, exp, mant;
    if (x < 0)
        x = -x;
    if (x > 32635)
        x = 32635;
    x += 0x84;
    for (exp = 7; exp > 0 && !(x & (0x4000 >> (7 - exp))); exp--)
        ;
    mant = (x >> (exp + 3)) & 0x0F;
    return (uint8_t)~(sign | exp << 4 | mant);
}
static void ulaw_table(void)
{
    uint32_t i;
    for (i = 0; i < 256u; i++) {
        uint32_t u = ~i & 0xFFu;
        int32_t t = (int32_t)(((u & 0x0Fu) << 3) + 0x84) << ((u & 0x70u) >> 4);
        ULAW[i] = (int16_t)((u & 0x80u) ? 0x84 - t : t - 0x84);
    }
}

/* the built-in vowels: a glottal pulse through three formants, with vibrato and breath (a crude singer, but it is a
 * voice the grains can carry; a real bank replaces it) */
static void fallback_sing(void)
{
    static const float F[4][3] = {{730, 1090, 2440}, {570, 840, 2410}, {300, 870, 2240}, {270, 2290, 3010}};   /* ah oh oo ee */
    static const float F0[4] = {220.0f, 196.0f, 246.9f, 261.6f};
    static const char *const NM[4] = {"AAH", "OH", "OOH", "EE"};
    uint32_t v, i, k;
    uint32_t seed = 0x51CEu;
    for (v = 0; v < 4u; v++) {
        float ph = 0.0f, y1[3] = {0, 0, 0}, y2[3] = {0, 0, 0}, peak = 0.0f;
        float *tmp = rv_buf;                            /* (the reverb's lines, not yet in use, as scratch) */
        for (i = 0; i < FB_LEN; i++) {
            float t = (float)i / 16000.0f, f0 = F0[v] * (1.0f + 0.012f * fm_sinf(6.2831853f * 5.2f * t));
            float src, out = 0.0f, env;
            ph += f0 / 16000.0f;
            if (ph >= 1.0f)
                ph -= 1.0f;
            src = ph < 0.6f ? fm_sinf(3.14159265f * ph / 0.6f) : 0.0f;   /* an open-close pulse */
            src = src * src - 0.3f;
            seed = seed * 1664525u + 1013904223u;
            src += 0.05f * (float)(int32_t)seed * (1.0f / 2147483648.0f);
            for (k = 0; k < 3u; k++) {                  /* a resonator per formant */
                float r = fm_expf(-3.14159265f * 90.0f * (1.0f + (float)k) / 16000.0f);
                float c = 2.0f * r * fm_cosf(6.2831853f * F[v][k] / 16000.0f);
                float y = (1.0f - r) * src + c * y1[k] - r * r * y2[k];
                y2[k] = y1[k];
                y1[k] = y;
                out += y / (1.0f + (float)k);
            }
            env = t < 0.04f ? t / 0.04f : (t > 0.38f ? (0.4375f - t) / 0.0575f : 1.0f);
            tmp[i] = out * (env < 0.0f ? 0.0f : env);
            if (fm_fabsf(tmp[i]) > peak)
                peak = fm_fabsf(tmp[i]);
        }
        for (i = 0; i < FB_LEN; i++)
            fallback[v * FB_LEN + i] = ulaw_enc((int32_t)(tmp[i] / (peak + 1e-6f) * 26000.0f));
        BANK.sl[v].off = v * FB_LEN;
        BANK.sl[v].len = FB_LEN;
        BANK.sl[v].f0x10 = (uint16_t)(F0[v] * 10.0f);
        BANK.sl[v].note = (uint8_t)(69.0f + 12.0f * fm_log2f(F0[v] / 440.0f) + 0.5f);
        for (k = 0; k < 7u && NM[v][k]; k++)
            BANK.sl[v].name[k] = NM[v][k];
        BANK.sl[v].name[k] = 0;
    }
    for (v = 4; v < NSLICE; v++)                        /* the rest: the same four, an octave down and up */
        BANK.sl[v] = BANK.sl[v % 4u];
    BANK.data = fallback;
    BANK.n = 4u;
    BANK.rate = 16000;
    BANK.factory = 1;
}

static void bank_init(void)
{
    uint32_t len = 0, i, n;
    const uint8_t *r = plat_res(&len);
    ulaw_table();
    {
        uint32_t i0;
        for (i0 = 0; i0 <= 256u; i0++)
            HANN[i0] = 0.5f - 0.5f * fm_cosf(6.2831853f * (float)i0 / 256.0f);
    }
    if (r && len > 8u + NSLICE * sizeof(slice_t) && r[0] == 'G' && r[1] == 'L' && r[2] == 'B' && r[3] == '1') {
        n = (uint32_t)(r[4] | r[5] << 8);
        BANK.rate = (uint32_t)(r[6] | r[7] << 8);
        for (i = 0; i < NSLICE; i++) {
            const uint8_t *s = r + 8u + i * sizeof(slice_t);
            uint32_t k;
            for (k = 0; k < sizeof(slice_t); k++)
                ((uint8_t *)&BANK.sl[i])[k] = s[k];
        }
        BANK.data = r + 8u + NSLICE * sizeof(slice_t);
        BANK.n = n > NSLICE ? NSLICE : n;
        BANK.factory = 0;
        if (BANK.n && BANK.rate >= 8000u)
            return;
    }
    fallback_sing();
    for (i = 0; i < RV_TOTAL; i++)                      /* the reverb's lines, clean again after the scratch */
        rv_buf[i] = 0.0f;
}

/* ------------------------------------------------------------------ the choir --- */
typedef struct {
    uint8_t on, rev, gate;
    float pos, end, start;                              /* the slice in source samples: where the time line is, its ends */
    float tstep;                                        /* source samples per output sample on the time line */
    float frate;                                        /* a grain's read speed (source samples per output sample) */
    float period;                                       /* output samples between grains */
    float wait;
    float glen;                                         /* a grain's length in source samples */
    struct {
        uint8_t on;
        float at, k;                                    /* where it reads from, how far it has read */
    } g[NGR];
    float amp, env, rel;
    uint32_t left;                                      /* output samples until the gate closes */
} cvoice_t;
static cvoice_t CV[NCV];
static uint8_t cv_next;

static float src_at(float x)                            /* the bank at a fractional sample, interpolated */
{
    int32_t i = (int32_t)x;
    float f = x - (float)i, a, b;
    if (i < 0)
        return 0.0f;
    a = (float)ULAW[BANK.data[i]];
    b = (float)ULAW[BANK.data[i + 1]];
    return (a + (b - a) * f) * (1.0f / 32768.0f);
}

/* a slice sung: pitch and formant in semitones, rev, gate in output samples, vel 0..127 */
static void choir_on(uint32_t slice, float pitch, float formant, int rev, uint32_t gate, uint32_t vel, float grain)
{
    cvoice_t *v = &CV[cv_next++ % NCV];
    const slice_t *s = &BANK.sl[slice % NSLICE];
    float f0 = (float)s->f0x10 * 0.1f, rate = (float)BANK.rate, p, fr;
    uint32_t i;
    if (!BANK.data || !s->len)
        return;
    if (f0 < 60.0f)
        f0 = 60.0f;
    p = fm_exp2f(pitch / 12.0f) * screw_mul;
    fr = fm_exp2f(formant / 12.0f) * screw_mul;
    v->start = (float)s->off;
    v->end = (float)(s->off + s->len - 2u);
    v->rev = (uint8_t)rev;
    v->pos = rev ? v->end - 1.0f : v->start;
    v->tstep = rate / SR * screw_mul;
    v->frate = rate / SR * fr;
    v->glen = 2.0f * rate / f0 * (0.6f + grain * 1.4f);
    v->period = SR / (f0 * p);
    v->wait = 0.0f;
    for (i = 0; i < NGR; i++)
        v->g[i].on = 0;
    v->amp = (0.2f + 0.8f * (float)vel / 127.0f);
    v->env = 0.0f;
    v->rel = 0.0f;
    v->left = gate;
    v->gate = 1;
    v->on = 1;
}

static float choir_sample(cvoice_t *v)
{
    float out = 0.0f;
    uint32_t i;
    if (!v->on)
        return 0.0f;
    if (v->wait <= 0.0f) {                              /* a new grain, centred on the time line */
        for (i = 0; i < NGR && v->g[i].on; i++)
            ;
        if (i < NGR) {
            v->g[i].on = 1;
            v->g[i].at = v->pos - v->glen * 0.5f;
            v->g[i].k = 0.0f;
        }
        v->wait += v->period;
    }
    v->wait -= 1.0f;
    for (i = 0; i < NGR; i++) {
        float x, w;
        if (!v->g[i].on)
            continue;
        x = v->g[i].at + (v->rev ? v->glen - v->g[i].k : v->g[i].k);
        w = HANN[(uint32_t)(v->g[i].k / v->glen * 256.0f) & 255u];
        if (x > v->start && x < v->end)
            out += src_at(x) * w;
        v->g[i].k += v->frate;
        if (v->g[i].k >= v->glen)
            v->g[i].on = 0;
    }
    v->pos += v->rev ? -v->tstep : v->tstep;
    if (v->pos >= v->end || v->pos <= v->start)
        v->gate = 0;
    if (v->left && !--v->left)
        v->gate = 0;
    v->env += ((v->gate ? 1.0f : 0.0f) - v->env) * (v->gate ? 0.004f : 0.0012f);
    if (!v->gate && v->env < 0.001f)
        v->on = 0;
    return out * v->env * v->amp * 0.7f;
}

/* ------------------------------------------------------------------ the bass --- */
static struct {
    uint8_t on, gate, mode;
    float ph1, ph2, ph3, inc, target, glide_k;
    float env, drop;                                    /* the 808's fall into its note */
    float lp, bp, lfo, lfo_inc;
    uint32_t left;
    float amp;
} BS;
static const uint8_t WOB_T[] = {192, 96, 64, 48, 32, 24, 16, 12};   /* the wobble's period in ticks */
static const char *const WOB_NM[] = {"1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
#define NWOB 8u

static void bass_on(int32_t note, uint32_t vel, uint32_t gate, int glide, uint32_t wob_ticks, const sound_t *sn)
{
    float inc = 440.0f * fm_exp2f(((float)note - 69.0f) / 12.0f) / SR;
    BS.mode = sn->bmode;
    BS.target = inc;
    if (!glide || !BS.on || !BS.gate) {
        BS.inc = inc;
        BS.drop = BS.mode == BM_808 ? 1.0f : 0.0f;
        BS.env = BS.on ? BS.env : 0.0f;
    }
    BS.glide_k = glide ? 1.0f - fm_expf(-1.0f / (SR * (0.01f + (float)sn->bglide * 0.002f))) : 1.0f;
    BS.lfo_inc = 1.0f / ((float)wob_ticks * samples_per_tick());
    if (!glide)
        BS.lfo = 0.0f;
    BS.left = gate;
    BS.gate = 1;
    BS.on = 1;
    BS.amp = 0.35f + 0.65f * (float)vel / 127.0f;
}

static float bass_sample(const sound_t *sn, float wobble_macro)
{
    float s, f, hp, inc, cut;
    if (!BS.on)
        return 0.0f;
    BS.inc += (BS.target - BS.inc) * BS.glide_k;
    inc = BS.inc * screw_mul;
    if (BS.left && !--BS.left)
        BS.gate = 0;
    BS.env += ((BS.gate ? 1.0f : 0.0f) - BS.env) * (BS.gate ? 0.01f : (BS.mode == BM_808 ? 0.00008f : 0.0006f));
    if (!BS.gate && BS.env < 0.0005f) {
        BS.on = 0;
        return 0.0f;
    }
    switch (BS.mode) {
    case BM_808:
        BS.drop *= 0.9993f;
        BS.ph1 += inc * (1.0f + BS.drop * 1.5f);
        if (BS.ph1 >= 1.0f)
            BS.ph1 -= 1.0f;
        s = fm_sin_pi(BS.ph1 * 2.0f - 1.0f);
        s = fm_tanhf(s * (1.0f + (float)sn->bdrive * (6.0f / 127.0f)));
        return s * BS.env * BS.amp;
    case BM_REESE:
        BS.ph1 += inc * 1.007f;
        BS.ph2 += inc * 0.993f;
        BS.ph3 += inc * 0.5f;
        if (BS.ph1 >= 1.0f) BS.ph1 -= 1.0f;
        if (BS.ph2 >= 1.0f) BS.ph2 -= 1.0f;
        if (BS.ph3 >= 1.0f) BS.ph3 -= 1.0f;
        s = (BS.ph1 - 0.5f) + (BS.ph2 - 0.5f) + 0.8f * fm_sin_pi(BS.ph3 * 2.0f - 1.0f);
        cut = (float)sn->bcut * (1.0f / 127.0f);
        break;
    default:                                            /* the wobble */
        BS.ph1 += inc;
        BS.ph2 += inc * 1.003f;
        if (BS.ph1 >= 1.0f) BS.ph1 -= 1.0f;
        if (BS.ph2 >= 1.0f) BS.ph2 -= 1.0f;
        s = 1.2f * (BS.ph1 - 0.5f) + 0.6f * (BS.ph2 < 0.5f ? 0.5f : -0.5f);
        BS.lfo += BS.lfo_inc;
        if (BS.lfo >= 1.0f)
            BS.lfo -= 1.0f;
        {
            float l = 0.5f - 0.5f * fm_cosf(6.2831853f * BS.lfo);   /* 0..1, opening on the beat */
            float depth = (float)sn->bwob * (1.0f / 127.0f) * (0.4f + wobble_macro * 0.6f);
            cut = (float)sn->bcut * (1.0f / 127.0f) * (1.0f - depth) + depth * l;
        }
        break;
    }
    f = 40.0f * fm_exp2f(cut * 8.0f);
    f = 2.0f * fm_sin_pi(fm_minf(f / SR, 0.16f));
    hp = s - BS.lp - (1.0f - (float)sn->breso * (0.95f / 127.0f)) * 2.0f * BS.bp;
    BS.bp = fm_flush(BS.bp + f * hp);
    BS.lp = fm_flush(BS.lp + f * BS.bp);
    return fm_tanhf(BS.lp * (1.0f + (float)sn->bdrive * (3.0f / 127.0f))) * BS.env * BS.amp;
}

/* ------------------------------------------------------------------ the air --- */
static struct {
    uint32_t seed;
    float rain_lp, rain_hp, drop_env, drop_lp, crack_lp, wind_lp, wind_bp, wind_lfo, hum_ph, hiss_lp, flutter;
} AIR;
static float air_sample(const sound_t *sn, float level)
{
    float n, out = 0.0f;
    if (level <= 0.0f)
        return 0.0f;
    AIR.seed = AIR.seed * 1664525u + 1013904223u;
    n = (float)(int32_t)AIR.seed * (1.0f / 2147483648.0f);
    if (sn->rain) {                                     /* a hiss of drops, and some that land close */
        AIR.rain_lp += 0.6f * (n - AIR.rain_lp);
        AIR.rain_hp += 0.02f * (AIR.rain_lp - AIR.rain_hp);
        AIR.flutter += 0.0005f * ((n > 0.0f ? 1.0f : 0.4f) - AIR.flutter);
        out += (AIR.rain_lp - AIR.rain_hp) * 0.35f * AIR.flutter * (float)sn->rain * (1.0f / 127.0f);
        if ((AIR.seed >> 8) % 6000u < (uint32_t)sn->rain / 8u)
            AIR.drop_env = 0.6f + 0.4f * fm_fabsf(n);
        AIR.drop_env *= 0.996f;
        AIR.drop_lp += 0.3f * (n * AIR.drop_env - AIR.drop_lp);
        out += AIR.drop_lp * 0.5f * (float)sn->rain * (1.0f / 127.0f);
    }
    if (sn->crackle && (AIR.seed >> 4) % 40000u < (uint32_t)sn->crackle) {   /* a click of the record */
        float c = (n > 0.0f ? 1.0f : -1.0f) * (0.3f + 0.7f * fm_fabsf(n));
        AIR.crack_lp = c;
    }
    AIR.crack_lp *= 0.6f;
    out += AIR.crack_lp * 0.6f;
    if (sn->hiss) {
        AIR.hiss_lp += 0.3f * (n - AIR.hiss_lp);
        out += (n - AIR.hiss_lp) * 0.05f * (float)sn->hiss * (1.0f / 127.0f);
    }
    if (sn->wind) {                                     /* a slow band of noise that comes and goes */
        float f;
        AIR.wind_lfo += 0.07f / SR;
        if (AIR.wind_lfo >= 1.0f)
            AIR.wind_lfo -= 1.0f;
        f = 0.004f + 0.01f * (0.5f + 0.5f * fm_sin_pi(AIR.wind_lfo * 2.0f - 1.0f));
        AIR.wind_bp += f * (n - AIR.wind_lp - 0.2f * AIR.wind_bp);
        AIR.wind_lp += f * AIR.wind_bp;
        out += AIR.wind_bp * 1.5f * (float)sn->wind * (1.0f / 127.0f);
    }
    if (sn->hum) {
        AIR.hum_ph += 50.0f / SR;
        if (AIR.hum_ph >= 1.0f)
            AIR.hum_ph -= 1.0f;
        out += (fm_sin_pi(AIR.hum_ph * 2.0f - 1.0f) + 0.4f * fm_sin_pi(AIR.hum_ph * 6.0f - 1.0f)) * 0.04f * (float)sn->hum * (1.0f / 127.0f);
    }
    return out * level;
}

/* ------------------------------------------------------------------ the reverb --- */
#define RV_N 4u
static const uint16_t RV_LEN[RV_N] = {1931, 2459, 3049, 3727};
static float ap_buf[225u + 556u] PLAT_POOL;
static struct {
    uint32_t w[RV_N], apw[2];
    float lp[RV_N];
    float fb, damp;
    uint8_t freeze;
} RV;
static void reverb_set(uint32_t size, uint32_t tone, int freeze)
{
    RV.fb = freeze ? 0.9995f : 0.55f + (float)size * (0.42f / 127.0f);
    RV.damp = freeze ? 0.98f : 0.25f + (float)tone * (0.7f / 127.0f);
    RV.freeze = (uint8_t)freeze;
}
static void reverb_sample(float in, float *l, float *r)
{
    static const uint16_t APL[2] = {225, 556};
    float x[RV_N], o[RV_N], s;
    uint32_t i, base = 0;
    if (RV.freeze)
        in = 0.0f;
    for (i = 0; i < 2u; i++) {                          /* two allpasses blur the input */
        float *b = ap_buf + (i ? 225u : 0u);
        float d = b[RV.apw[i]], y = -0.6f * in + d;
        b[RV.apw[i]] = in + 0.6f * y;
        RV.apw[i] = (RV.apw[i] + 1u) % APL[i];
        in = y;
    }
    for (i = 0; i < RV_N; i++) {
        o[i] = rv_buf[base + RV.w[i]];
        base += RV_LEN[i];
    }
    s = 0.5f * (o[0] + o[1] + o[2] + o[3]);             /* a Householder mix */
    for (i = 0; i < RV_N; i++)
        x[i] = o[i] - s;
    base = 0;
    for (i = 0; i < RV_N; i++) {
        RV.lp[i] += RV.damp * (x[i] - RV.lp[i]);
        rv_buf[base + RV.w[i]] = fm_flush(in + RV.lp[i] * RV.fb);
        RV.w[i] = (RV.w[i] + 1u) % RV_LEN[i];
        base += RV_LEN[i];
    }
    *l = o[0] + o[2] * 0.6f;
    *r = o[1] + o[3] * 0.6f;
}

/* ------------------------------------------------------------------ the mix --- */
static struct {
    float duck;                                         /* the kick's envelope (1 at a hit) */
    float master_lp, master_bp, master_lp2;
    float lane_lvl[NLANE];                              /* the section's / the hand's levels */
    float air, verb, filter, choir, wobble, pump, energy;
    float riser, riser_ph;                              /* a riser sounding (its level), noise sweep phase */
    float swell;                                        /* a reverse swell building */
    uint8_t dropout;                                    /* silent but the choir and air */
    float meter[NLANE];
} MX;

static void duck_hit(void) { MX.duck = 1.0f; }

static void app_mix(int32_t *lr, uint32_t off, uint32_t n, float gain)
{
    const sound_t *sn = (const sound_t *)&P.snd;
    uint32_t i, k;
    float send_l, send_r;
    float ch_send = (float)sn->send[LN_CHORDS] / 127.0f, dr_send = (float)sn->send[LN_DRUMS] / 127.0f;
    float bs_send = (float)sn->send[LN_BASS] / 127.0f, cv_send = (float)sn->send[LN_CHOIR] / 127.0f;
    float ai_send = (float)sn->send[LN_AIR] / 127.0f, verb = 0.3f + MX.verb * 1.4f;
    float pump[NLANE], fc = 60.0f * fm_exp2f(MX.filter * 8.4f), fcoef = 2.0f * fm_sin_pi(fm_minf(fc / SR, 0.16f));
    for (k = 0; k < NLANE; k++)
        pump[k] = (float)sn->pump[k] / 127.0f * (0.3f + MX.pump * 0.7f);
    for (i = off; i < off + n; i++) {
        float dr = 0.0f, ch = 0.0f, bs, cv = 0.0f, ai, dry, rin, rl, rr, d;
        for (k = 0; k < NVOICE; k++) {                  /* the shared synth's voices: drums and chords (and lantern) */
            voice_t *v = &V[k];
            float s;
            if (!v->on)
                continue;
            s = voice_sample(v) * v->vel * v->lvl;
            if (v->owner == LN_DRUMS)
                dr += s;
            else
                ch += s;
        }
        bs = bass_sample(sn, MX.wobble);
        for (k = 0; k < NCV; k++)
            cv += choir_sample(&CV[k]);
        cv *= (float)sn->vlevel / 127.0f * (0.2f + MX.choir * 0.8f);
        ai = air_sample(sn, MX.air);
        if (MX.riser > 0.0f) {                          /* a riser: noise through a rising band */
            float f;
            AIR.seed = AIR.seed * 1664525u + 1013904223u;
            MX.riser_ph += 0.002f * (MX.riser - MX.riser_ph);
            f = 0.01f + 0.14f * MX.riser_ph;
            MX.master_lp2 += f * ((float)(int32_t)AIR.seed * (1.0f / 2147483648.0f) - MX.master_lp2);
            ai += MX.master_lp2 * 0.3f * MX.riser;
        }
        d = MX.duck;
        MX.duck *= 0.99985f;
        ch *= 1.0f - pump[LN_CHORDS] * d;
        bs *= (1.0f - pump[LN_BASS] * d) * (float)sn->blevel / 127.0f;
        cv *= 1.0f - pump[LN_CHOIR] * d;
        ai *= 1.0f - pump[LN_AIR] * d;
        if (MX.dropout) {
            dr = 0.0f;
            ch = 0.0f;
            bs = 0.0f;
        }
        dr *= MX.lane_lvl[LN_DRUMS];
        bs *= MX.lane_lvl[LN_BASS];
        ch *= MX.lane_lvl[LN_CHORDS];
        cv *= MX.lane_lvl[LN_CHOIR];
        MX.meter[LN_DRUMS] = fm_maxf(MX.meter[LN_DRUMS] * 0.9995f, fm_fabsf(dr));
        MX.meter[LN_BASS] = fm_maxf(MX.meter[LN_BASS] * 0.9995f, fm_fabsf(bs));
        MX.meter[LN_CHORDS] = fm_maxf(MX.meter[LN_CHORDS] * 0.9995f, fm_fabsf(ch));
        MX.meter[LN_CHOIR] = fm_maxf(MX.meter[LN_CHOIR] * 0.9995f, fm_fabsf(cv));
        MX.meter[LN_AIR] = fm_maxf(MX.meter[LN_AIR] * 0.9995f, fm_fabsf(ai));
        rin = (dr * dr_send + ch * ch_send + bs * bs_send + cv * cv_send + ai * ai_send) * 0.5f + MX.swell * ch;
        reverb_sample(rin, &rl, &rr);
        dry = dr + ch * 0.8f + bs + cv + ai;
        send_l = dry + rl * verb;
        send_r = dry + rr * verb;
        /* the master low-pass (FILTER), the same on both sides */
        {
            float mid = 0.5f * (send_l + send_r), side = 0.5f * (send_l - send_r), hp;
            hp = mid - MX.master_lp - 1.2f * MX.master_bp;
            MX.master_bp = fm_flush(MX.master_bp + fcoef * hp);
            MX.master_lp = fm_flush(MX.master_lp + fcoef * MX.master_bp);
            mid = MX.filter > 0.99f ? mid : MX.master_lp;
            side *= MX.filter > 0.99f ? 1.0f : MX.filter;
            send_l = mid + side;
            send_r = mid - side;
        }
        lr[2 * i] += (int32_t)(fm_tanhf(send_l * 0.75f) * gain);
        lr[2 * i + 1] += (int32_t)(fm_tanhf(send_r * 0.75f) * gain);
    }
}
