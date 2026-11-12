/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* GLOAM's project: five lanes of patterns, the sounds, and the arrangement.
 *   DRUMS   8 sounds x 64 steps: velocity, nudge, roll (the drift each hit keeps is its own byte)
 *   BASS    64 steps: a degree of the chord (or of the key), length, glide, wobble rate
 *   CHORDS  64 steps: a degree, colours, length, stab or hold
 *   CHOIR   64 steps: a slice of the vocal bank, pitch, formant, reverse, length
 *   AIR     no steps: rain, crackle, hiss, wind and hum, set by the knobs and the sections
 * Each lane has 8 patterns; a section of the song picks one per lane. Timing is 96 ticks a quarter; a step is a
 * sixteenth (24 ticks), a triplet sixteenth or a thirty-second by the pattern's rate. */
#pragma once

#define NLANE_SEQ 4u                                    /* lanes with steps: DRUMS BASS CHORDS CHOIR */
enum { LN_DRUMS, LN_BASS, LN_CHORDS, LN_CHOIR, LN_AIR, NLANE };
static const char *const LANE_NM[NLANE] = {"DRUMS", "BASS", "CHORDS", "CHOIR", "AIR"};
#define NPAT 8u
#define NSTEP 64u
#define NDRUM_ROW 8u
enum { DRW_KICK, DRW_SNARE, DRW_CLAP, DRW_HAT, DRW_OHAT, DRW_RIM, DRW_SHAKE, DRW_PERC };
static const char *const DROW_NM[NDRUM_ROW] = {"KICK", "SNARE", "CLAP", "HAT", "OPEN", "RIM", "SHAKER", "PERC"};
static const uint8_t DROW_NOTE[NDRUM_ROW] = {36, 38, 39, 42, 46, 37, 70, 45};   /* General MIDI */

typedef struct {                                        /* a drum hit */
    uint8_t vel;                                        /* 0 none */
    int8_t nudge;                                       /* ticks, -12..+12 (half a step either way) */
    int8_t drift;                                       /* its own looseness, -12..+12, scaled by UNSNAP */
    uint8_t roll;                                       /* low 3 bits: hits 1..6 (0/1 none); bit 3: they fall in pitch */
} dhit_t;

typedef struct {
    int8_t deg;                                         /* degree; 0x7F none */
    uint8_t len, vel;                                   /* len in steps (1..16) */
    uint8_t flags;                                      /* BF_GLIDE, BF_ABS (the key, not the chord), BF_OCT_UP/DN */
    uint8_t wob;                                        /* wobble rate: 0 the lane's, 1.. WOB_T index + 1 */
    int8_t nudge, drift, rsv;
} bstep_t;
enum { BF_GLIDE = 1, BF_ABS = 2, BF_OCT_UP = 4, BF_OCT_DN = 8 };

typedef struct {
    uint8_t deg;                                        /* 0..13 (7.. an octave up), 0xFF none */
    uint8_t mods;                                       /* CM_* */
    uint8_t len, vel;                                   /* len in steps; 0 stab */
} cstep_t;
enum { CM_7 = 1, CM_SUS2 = 2, CM_SUS4 = 4, CM_ADD9 = 8, CM_FLIP = 16 };

typedef struct {
    uint8_t slice;                                      /* 0..15, 0xFF none */
    int8_t pitch;                                       /* semitones -24..+24 */
    int8_t formant;                                     /* semitones -12..+12 (the voice's size, not its note) */
    uint8_t flags;                                      /* VF_REV, VF_HOLD (to the next), VF_FOLLOW (snap to the chord) */
    uint8_t len, vel;
    int8_t nudge, drift;
} vstep_t;
enum { VF_REV = 1, VF_FOLLOW = 2 };

typedef struct {
    uint8_t len;                                        /* steps 1..64 */
    uint8_t rate;                                       /* 0 sixteenths, 1 triplet sixteenths, 2 thirty-seconds, 3 eighths */
    uint8_t rsv[2];
} patinfo_t;

/* the sounds */
enum { BM_808, BM_REESE, BM_WOBBLE, NBM };
static const char *const BM_NM[NBM] = {"808", "REESE", "WOBBLE"};
typedef struct {
    uint8_t tone, decay, level, unsnap, swing;          /* drums: tone, decay, level; the lane's looseness and shuffle */
    uint8_t bmode, bcut, breso, bdrive, bwob, bwob_rate, bglide, blevel;
    uint8_t cwave, ccut, cspread, catt, crel, clevel;
    uint8_t vlevel, vgrain, vtexture, vbank;            /* choir: level, grain size, breath/blur, bank offset */
    uint8_t rain, crackle, hiss, wind, hum;             /* air */
    uint8_t send[NLANE];                                /* reverb sends */
    uint8_t pump[NLANE];                                /* sidechain depth from the kick */
    uint8_t verb_size, verb_tone;
    uint8_t chan[NLANE];                                /* MIDI channels (0 off) */
    uint8_t rsv[8];
} sound_t;

/* the arrangement */
#define NSEC 32u
enum { MC_SCREW, MC_FILTER, MC_AIR, MC_VERB, MC_ENERGY, MC_PUMP, MC_CHOIR, MC_WOBBLE, NMACRO };
static const char *const MC_NM[NMACRO] = {"SCREW", "FILTER", "AIR", "REVERB", "ENERGY", "PUMP", "CHOIR", "WOBBLE"};
enum { FO_NEXT, FO_LOOP, FO_JUMP, FO_RANDOM, FO_CHANCE, FO_FIRST, FO_BACK, FO_STOP, NFOLLOW };
static const char *const FO_NM[NFOLLOW] = {"NEXT", "LOOP", "JUMP TO", "RANDOM IN", "CHANCE TO", "FIRST", "BACK", "STOP"};
enum { TR_FILL = 1, TR_RISER = 2, TR_SWELL = 4, TR_DROPOUT = 8, TR_TAPESTOP = 16, TR_IMPACT = 32, TR_FILTERUP = 64 };
#define NTRANS 7u
static const char *const TR_NM[NTRANS] = {"DRUM FILL", "RISER", "REVERSE SWELL", "DROPOUT", "TAPE STOP", "IMPACT", "FILTER SWEEP"};
enum { CV_LINEAR, CV_EASE_IN, CV_EASE_OUT, CV_AT_END, CV_S, NCURVE };
static const char *const CV_NM[NCURVE] = {"LINEAR", "EASE IN", "EASE OUT", "AT THE END", "S-CURVE"};
#define MC_HOLD 0xFFu                                   /* a macro the section leaves as it is */

typedef struct {
    char name[8];
    uint8_t pat[NLANE_SEQ];                             /* pattern per lane, 0xFF the lane is silent */
    uint8_t bars;                                       /* 1..64 */
    uint8_t repeats;                                    /* 1..16 */
    uint8_t follow, target, target2, chance;            /* FO_*; JUMP/CHANCE target; RANDOM range target..target2 */
    uint8_t trans;                                      /* TR_* into whatever follows */
    uint8_t rows;                                       /* drum rows heard (bit per row), 0xFF all */
    int8_t transpose;                                   /* semitones for the melodic lanes */
    uint8_t curve;
    uint8_t from[NMACRO], to[NMACRO];                   /* each macro across the section; MC_HOLD = untouched */
    uint8_t used;
    uint8_t rsv[3];
} section_t;

#define PROJ_MAGIC 0x314D4C47u                          /* "GLM1" */
#define PROJ_VER 1u
typedef struct {
    uint16_t bpm10;
    uint8_t root, scale, clk_mode, clk_send;
    uint8_t screw, unsnap;                              /* the live macros (sections may move them) */
    uint8_t mac[NMACRO];                                /* the macros as set by hand (0..127; screw 0 none .. 127 an octave) */
    uint8_t nsec;                                       /* sections in the song */
    uint8_t rsv0[3];
    patinfo_t info[NLANE_SEQ][NPAT];
    dhit_t drum[NPAT][NDRUM_ROW][NSTEP];
    bstep_t bass[NPAT][NSTEP];
    cstep_t chord[NPAT][NSTEP];
    vstep_t choir[NPAT][NSTEP];
    sound_t snd;
    section_t sec[NSEC];
    uint8_t rsv[32];
} proj_t;
