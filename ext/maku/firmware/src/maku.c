/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MAKU (幕間): the interlude mode. One DENSITY macro (0..127) joins a drone, a resonant kick and a random arpeggio.
 * Included by seq.c after the scale helpers; the ISR calls maku_step() instead of reading a track's stored step.
 *
 *   track 1  KICK   16 steps, the time axis. DENSITY thins it out: nothing -> once a bar (a hint) -> every 2 beats
 *                   (a pulse) -> every beat. The kick's pitch stays put (it does not follow the ROOT).
 *   track 2  DRONE  57 steps: a chord every 19 steps, open voicings (root + 5th + octave, sus2, sus4, a wide minor
 *                   10th), six of them in turn so the harmony drifts slowly instead of looping.
 *   track 3  SHIMMER 13 steps: a high note, then a dyad (a note and its 5th) 7 steps later, from a six-note cycle;
 *                   13 never lines up with 57.
 *   track 4  ARP    29 steps: a written 32-place phrase of wide, sparse pentatonic-like leaps with breaths (rests) in it,
 *                   played from a random place for N steps, then a jump. Its last 5 steps of 29 are silent: the phrase
 *                   breathes once every 29 steps, against the kick's 16.
 *                   Every note is a degree of the SCALE (maku_note), so nothing is ever out of key.
 *
 * "Tidy, loosen, tidy": the higher the DENSITY, the more the arp's table is re-rolled as it plays, the shorter its
 * runs, the more the swing and the velocity wander. Lowering it makes the same table come back.
 * Nothing here is stored in a project: MAKU starts off, maku_setup() (ui.c) sets the four tracks up. */
#define MAKU_KICK 0u
#define MAKU_DRA 1u
#define MAKU_DRB 2u
#define MAKU_ARP 3u
#define MAKU_DRA_LEN 57u
#define MAKU_DRB_LEN 13u
#define MAKU_ARP_LEN 29u
#define MAKU_ARP_GAP 24u         /* arp steps 24..28 are silent: the breath */
#define MAKU_REST 255u
#define MAKU_CUE_VEL 44u         /* the breath's cue (the kick, w > 1/2): soft */
#define MAKU_TABLE 32u
#define MAKU_PK 8u               /* the played notes the field remembers; a new one pushes out the weakest (the oldest) */

/* The verbs that hold three parameters of their own: while the button is held the screen shows them and SELECT / PRESETS /
 * ALGORITHM move them. HOME has none. The upper row (FX SCL ENV LFO EDIT GLO) holds one verb of change each, SINK CASCADE SWELL
 * WOBBLE LIFT SCRAMBLE (docs/VERBS_PLAN.md, docs/VERBS_TUNE_PLAN.md): a press sets it going, it stays at full while the button is
 * down and a while after, then fades. Its three parameters are the times of that: RISE, HOLD, FADE (maku.vt; they stay across
 * worlds); how far it goes is fixed (maku.vp, MAKU_PEAK). The lower row keeps what it does: ARP is the key (VB_ROOT, ROOT SCALE
 * REG), SEQ the tempo (VB_GLO), PLAY toggles DRIFT (RATE RANGE REACH). */
enum { VB_NONE, VB_SINK, VB_GLO, VB_ROOT, VB_SCRAM, VB_CASC, VB_DRIFT, VB_LIFT, VB_SWELL, VB_WOB, VB_N };
static uint32_t beat_samples(void);   /* (fx.c) */
static uint32_t div_samples(uint32_t div);   /* (fx.c) */
#define VBIT(v) (1u << (v))
#define MAKU_NSCALE 8u
static const uint8_t MAKU_SCALES[MAKU_NSCALE] = {1, 2, 9, 3, 4, 8, 5, 6};   /* MAJ MIN LYD DOR MIX PHRY PEN MPEN: major, minor, major, minor, ... */

static struct {
    uint8_t on;
    uint8_t dens;                /* 0..127 */
    uint8_t run_left;            /* arp steps still to play from the table before it jumps */
    uint8_t pos;                 /* the place in the table */
    uint8_t cyc_a, cyc_b;        /* drone cycles done: the chord note rotates with them */
    uint8_t bar_c;               /* the floor's chords struck at the head of a bar in the breath (three make a cycle) */
    uint16_t hv;                 /* the verbs held now, a bit each (VBIT) */
    uint8_t verb;                /* the one whose parameters the screen and the knobs have: the last pressed of those held (VB_NONE) */
    int16_t va[VB_N];            /* each verb's amount, Q12: RISE up while it is pressed, held for HOLD after it, FADE away (maku_env) */
    uint8_t vt[VB_N][3];         /* the upper verbs' times, 0..127: RISE HOLD FADE (maku_blocks); they stay when a world is made */
    uint8_t vt_ok;               /* .. they have their first values (maku_vt_defaults) */
    uint8_t vph[VB_N];           /* each verb's phase: 0 off, 1 up (rising, then held), 2 fading */
    uint16_t pp;                 /* the verbs held at the last block (an edge is a press) */
    uint8_t rst;                 /* SWELL was pressed: bit 0 the floor, bit 1 the haze strike their chord again at their next step */
    uint8_t dlast;               /* the haze step played last (1: dc[1], 2: dc[2]) */
    int32_t vq[VB_N];            /* .. the amount in Q24 */
    uint32_t vhl[VB_N];          /* .. the blocks of HOLD still to go once the amount is at full and the button up */
    uint32_t vft[VB_N];          /* .. FADE's position, Q24 (0: just begun) */
    int16_t tw_q;                /* LIFT: the amount the macros were last set for */
    step_t dc[3];                /* the drones' last steps (DRONE A, SHIMMER at 0 and at 7): a struck part plays them again while SWELL's STAY holds */
    uint8_t dvalid;              /* .. bit i: dc[i] holds one */
    struct { int16_t b, w; } ov[18];   /* SWELL, WOBBLE and LIFT write track parameters: the base value, and what was written last (maku_over) */
    uint8_t ovlive;              /* .. they are on, or have a value left to put back */
    uint8_t dr_tog;              /* DRIFT is on (PLAY's tap toggles it; PLAY's LED shows it) */
    uint8_t dr_moved;            /* PLAY was held and a knob turned: letting go does not toggle */
    uint8_t dr_on;               /* DRIFT is on and has taken its anchors */
    uint16_t dr_t;               /* DRIFT's clock: a step of the walk every 64 (small, frequent steps: the numbers glide) */
    uint8_t drs;                 /* the space DRIFT has added (0..127): the sends rise and the master's low-pass closes; it stays when switched off */
    int16_t dra[16];             /* DRIFT's anchors, taken at the switch-on: the 12 macros, BPM, TEMPO's SWING and DUCK, drs */
    int16_t drw[16];             /* .. and what it last left there: a value found changed since was moved by hand, and is the new anchor */
    uint32_t sw_t, sw_at;        /* DRIFT's change of voice: blocks counted, and the count at which the next part is asked to change (MAKU_SW_BLOCKS) */
    uint8_t sw_want, sw_go;      /* .. bit i: part i is asked to change at its next rest (maku_step); it has reached it, the main loop changes it (maku_voice_swap) */
    uint8_t sw_last;             /* .. the part changed last (never twice in a row) */
    uint8_t pk[MAKU_PK], pk_w[MAKU_PK], pk_av, pk_new;  /* the notes played, always melting into the arp phrase: pk_w the strength of each
                                  * (255 new, 0 gone; it fades), bit i of pk_av: pk[i] is outside the scale,
                                  * bit i of pk_new: not yet heard in the arp (the next arp step plays it for sure) */
    uint8_t stay_a, stay_b;      /* a held drone / haze: the chord (note) coming next is not struck, the last one rings through (OPEN / GRAIN) */
    uint8_t open;                /* DRONE: OPEN 0..127: how many voices of the chord and how wide */
    uint8_t loose;               /* ARP: LOOSE 0..127: how much the phrase unravels and climbs */
    uint8_t m[4][3];             /* the macro of each track's SELECT / PRESETS / ALGORITHM, 0..127 */
    uint8_t root, scale;         /* last ROOT / SCALE seen on any track (a change is copied to all four) */
    uint8_t frame_on, frame_root; /* a key change by fifths (maku_fifth) while an arp run plays: the run goes on in the old ROOT's
                                  * frame, with the new key signature, until it ends */
    uint8_t vp[VB_N][3];         /* the verbs' parameters, 0..127 (SINK: SINK WASH HUSH; SEQ (tempo): - SWING DUCK; ROOT: - - REG 0..2;
                                  * SCRAMBLE: ARP KICK SWING; CASCADE: DENSE SPAN HARM; DRIFT: RATE RANGE REACH;
                                  * LIFT: TONE EDGE THIN; SWELL: ATTACK RELEASE STAY; WOBBLE: RATE DEPTH PITCH).
                                  * Of the six upper verbs these are how far each goes (MAKU_PEAK): not on the screen */
    int32_t dive;                /* SINK's dive, Q12: the rest sinks into reverb and reverse delay; it comes up slowly */
    int32_t kfade;               /* the kick's level after a SINK, Q12: out fast, back in slowly */
    uint32_t seed;               /* the seed of this world (maku_world): the same seed and catalog give the same world */
    uint16_t world_n;            /* worlds made since power-on (the emulator's rating log names a rating by it) */
    uint8_t voice[4];            /* the catalog row each track plays (maku_voices.c); 0xFF: none */
    uint8_t art[4];              /* how each track sounds (ART_*): from the catalog row */
    uint8_t atk0[4], rel0[4];    /* the track's ATK / REL of its articulation: a sustained voice's macros move around them */
    int32_t bw;                  /* the breath slope's weight w, Q15 (0: tempo, 32768: breath); follows maku_breath_target slowly */
    int32_t bb;                  /* the breath envelope b(t), Q15 (0 empty .. 32768 full); maku_breath_env */
    int32_t bd;                  /* the drones' dip from it, Q12 (0: none): the DEPTH of the breath */
    int32_t duck;                /* the kick's dip of the other tracks' level, Q12 (4096: none); it recovers in maku_block */
    uint8_t pm[4];               /* how far each part's pitch may waver (PM_*; the floor and the haze, drawn per world) */
} maku;

/* How far a part's pitch may waver (docs/decisions.md 2026-10-11): a world draws one level for the floor and one for the
 * haze, mostly STILL. The level bounds what wavers the pitch: the haze GRAIN's RAND (each grain's random detune, +-12
 * semitones x (RAND / 127)^2, eng_grain.c) on its TONE macro, and the DEPTH of a swept INSERT (FLANG / CHOR) on SWAY / AIR,
 * so no macro, LIFT or DRIFT takes a STILL part past it. The INSERT drawn depends on it too (maku_world). WARP is 0: the
 * ranges as they were before the levels (maku_setup, and a like kept before them) */
enum { PM_WARP, PM_STILL, PM_SWAY, PM_N };
static const struct { uint8_t rlo, rhi, dlo, dhi; } MAKU_PM[PM_N] = {
    {0, 100, 30, 127},           /* WARP: RAND up to 100 (+-7 semitones), the whole sweep */
    {6, 18, 20, 60},             /* STILL: RAND up to 18 (+-24 cents, a chorus's blur), a shallow sweep */
    {10, 40, 30, 100},           /* SWAY: RAND up to 40 (+-1.2 semitones) */
};
#define MAKU_ENGI_GRAIN 8u
static step_t maku_cur[4];       /* the step each track played last (seq_ratchet repeats it) */

/* How far each upper verb goes at full (the three numbers of vp): the verbs have no knob for it, only for their times. By ear:
 * the sets are as strong as they can be without breaking the world (WOBBLE's PITCH and CASCADE's DENSE / HARM stop short) */
static const uint8_t MAKU_PEAK[VB_N][3] = {
    [VB_SINK] = {100, 127, 127},         /* SINK WASH HUSH */
    [VB_SCRAM] = {110, 127, 127},        /* ARP KICK SWING */
    [VB_CASC] = {120, 127, 110},         /* DENSE SPAN HARM */
    [VB_LIFT] = {127, 127, 127},         /* TONE EDGE THIN */
    [VB_SWELL] = {127, 127, 100},        /* ATTACK RELEASE STAY */
    [VB_WOB] = {127, 127, 100},          /* RATE DEPTH PITCH */
};
/* the amount of verb v (Q12) as a chance: true that often, this time */
static uint32_t maku_roll(uint32_t v)
{
    return (uint32_t)(rng() & 4095u) < (uint32_t)maku.va[v];
}
/* parameter k (0..127) of verb v at its amount: 0..k */
static uint32_t maku_vamt(uint32_t v, uint32_t k)
{
    uint32_t a = (uint32_t)maku.vp[v][k] * (uint32_t)maku.va[v] >> 12;
    return a * (uint32_t)(32768 - maku.bw / 2) >> 15;       /* the breath slope halves what a verb can do (BREATH_PLAN 1) */
}

/* How a part sounds (docs/VOICES_RATING.md). STRK: struck, over at once; SOFT: struck softly (slower attack); SWEL: swells
 * in and is held to the next note; BOW: an exciter that keeps sounding, held; HOLD: a drone held until the chord changes.
 * Ties (ST_TIE) hold a note from step to step; the track's DIV is slower for the drones, so a swell has time to arrive */
enum { ART_STRK, ART_SOFT, ART_SWEL, ART_BOW, ART_HOLD };
static uint32_t maku_held(uint32_t i) { return maku.art[i] >= ART_SWEL; }
#define MAKU_SLOW 4u             /* a slow melody (SWEL / BOW arp): one note per 4 steps, held through them */
#define MAKU_SHIM_HOLD_A 6u      /* the shimmer: note A (step 0) holds through step 5, the dyad (step 7) through step 11 */

/* is step idx of track i a TIE: it holds what plays. A pure function of (track, step): seq_step asks for the next one too */
static uint32_t maku_is_tie(uint32_t i, uint32_t idx)
{
    if (!maku.on || !maku_held(i))
        return 0;
    switch (i) {
    case MAKU_DRA:
        idx %= 19u;
        return (idx != 0 && idx != 18u) || maku.stay_a; /* the chord holds to step 17; step 18 lets go before the next one (unless it stays) */
    case MAKU_DRB:
        return (idx > 0 && idx < MAKU_SHIM_HOLD_A) || (idx > 7u && idx < 12u) || (maku.stay_b && (idx == 12u || idx == 0));
    case MAKU_ARP:
        return idx < MAKU_ARP_GAP && idx % MAKU_SLOW;
    }
    return 0;
}

/* DENSITY from which the four on the floor is certain (100 %); above it DENSITY adds ornaments instead */
#define MAKU_FOUR 96u

/* The 8 voices are shared: a held part sounds all the time (and its tails ring on), so it gets a share and keeps to it: the
 * floor 3 (a chord of three, or two and a tail), the haze 2, the arp 2, the kick 1 (and the struck floor / haze as before). 0: none */
static uint32_t maku_eff(void);
static uint32_t maku_voice_cap(const track_t *t)
{
    uint32_t i = trk_index(t);
    if (!maku.on || i >= 4u)
        return 0;
    if (i == MAKU_KICK)                                 /* (the kick alone, as before; with ornaments: it and two hats) */
        return maku_eff() > MAKU_FOUR ? 2u : 0u;
    if (i == MAKU_ARP)
        return maku_eff() > MAKU_FOUR ? 1u : 2u;                                      /* (a struck arp's tails too: it never takes the haze's or the floor's) */
    if (!maku_held(i))
        return 0;
    /* (a held PHYS model is exciting all the time, 12 modes a voice: the floor of 2 voices, not 3) */
    return i == MAKU_DRA && ENGINES[t->engine] != &ENG_PHYS && maku_eff() <= MAKU_FOUR ? 3u : 2u;   /* (the ornaments: the floor gives one up) */
}

static uint32_t maku_hash(uint32_t i)
{
    i = (i + 0x9E3779B9u) * 0x85EBCA6Bu;
    i ^= i >> 13;
    i *= 0xC2B2AE35u;
    return i ^ (i >> 16);
}

/* scale degree `deg` (0 = the root, may exceed the scale's size: next octave) above MIDI note base + ROOT, always in
 * the scale of track t */
static uint32_t maku_note_in(const track_t *t, uint32_t root, uint32_t deg, uint32_t base)
{
    uint32_t mask = scale_mask(t), count = (uint32_t)__builtin_popcount(mask), i, left;
    uint32_t n;
    if (!count)
        return base;
    left = deg % count;
    for (i = 0; i < 12u; i++)
        if ((mask >> i) & 1u) {
            if (!left)
                break;
            left--;
        }
    n = base + root + 12u * (deg / count) + i;
    return n > 127u ? 127u : n;
}
static uint32_t maku_note(const track_t *t, uint32_t deg, uint32_t base)
{
    return maku_note_in(t, (uint32_t)t->p[P_ROOT], deg, base);
}
/* the drones' register (ROOT held, REG): LOW / MID / HIGH, an octave apart (MID is as designed) */
static uint32_t maku_low(uint32_t base)
{
    return base + 12u * (uint32_t)maku.vp[VB_ROOT][2] - 12u;
}

/* the scale after the one in use: major and minor families take turns (MAKU_SCALES); a scale outside the list starts it over */
static uint32_t maku_scale_next(uint32_t cur)
{
    uint32_t i;
    for (i = 0; i < MAKU_NSCALE; i++)
        if (MAKU_SCALES[i] == cur)
            return MAKU_SCALES[(i + 1u) % MAKU_NSCALE];
    return MAKU_SCALES[0];
}

/* a key change by fifths is a change of the key signature: note n, a note of the scale at ROOT `from`, stays where it is
 * unless the new key (ROOT `to`, the same scale) lacks its pitch class, and then it moves the one semitone the
 * signature changes (the new sharp for a step up the circle, the new flat for a step down), else the nearest pitch
 * of the new key. A note outside the old scale (an avoid note played on the keys) stays as it was played */
static uint32_t maku_resig(uint32_t n, uint32_t mask, uint32_t from, uint32_t to)
{
    uint32_t d, up = (to + 12u - from) % 12u == 7u;
    if (!((mask >> ((n + 120u - from) % 12u)) & 1u) || ((mask >> ((n + 120u - to) % 12u)) & 1u))
        return n;
    for (d = 1; d < 6u; d++) {
        uint32_t a = up ? n + d : n - d, b = up ? n - d : n + d;
        if (((mask >> ((a + 120u - to) % 12u)) & 1u) && a < 128u)
            return a;
        if (((mask >> ((b + 120u - to) % 12u)) & 1u) && b < 128u)
            return b;
    }
    return n;
}

/* the written material, as scale degrees (0 = ROOT, 7 = the octave; maku_note keeps them in the scale) */
static const uint8_t MAKU_CHORD[6][3] = {      /* DRONE A, from C2: open and unresolved, no third in the first rows */
    {0, 4, 7},                                  /* root, 5th, octave */
    {0, 4, 8},                                  /* .. the 9th on top: sus2 */
    {0, 3, 7},                                  /* sus4 */
    {0, 4, 7},
    {0, 4, 9},                                  /* a wide minor 10th */
    {0, 1, 4},                                  /* root, 2nd, 5th: a cluster, the only close one */
};
static const uint8_t MAKU_SHIM_A[6] = {11, 9, 14, 11, 13, 9};   /* SHIMMER, step 0: a single high note */
static const uint8_t MAKU_SHIM_B[6] = {7, 9, 7, 8, 9, 11};      /* step 7: the lower note of a dyad (+4: its 5th) */
static const uint8_t MAKU_PHRASE[MAKU_TABLE] = {              /* ARP: two 16-place phrases, 255 a breath */
    11, 255, 14, 11, 9, 255, 7, 9, 11, 14, 16, 255, 14, 11, 9, 255,
    9, 11, 255, 7, 14, 255, 11, 9, 7, 255, 9, 11, 14, 255, 11, 255,
};

/* the kick's dip of the other tracks (Q12, below 4096), scaled by GLO's DUCK (64 = as designed, 0 = none, 127 = twice) */
static int32_t maku_dip(int32_t dip)
{
    int32_t d = 4096 - (4096 - dip) * (int32_t)maku.vp[VB_GLO][2] / 64;
    d = 4096 - (int32_t)((int64_t)(4096 - d) * (32768 - maku.bw) >> 15);   /* the breath slope takes the ducking away (w -> 1) */
    return d < 0 ? 0 : d;
}

/* the kick's reach into DENSITY: what sounded at 60 now sounds at 80 (0..80 runs 0..60, 80..127 runs 60..127) */
static uint32_t maku_kick_d(uint32_t d)
{
    return d <= 80u ? d * 60u / 80u : 60u + (d - 80u) * 67u / 47u;
}

/* the velocity of a black-key pad: quieter than a plain key, and above DENSITY 120 it grows again (to 100 at 127) */
static uint32_t maku_pad_vel(void)
{
    uint32_t d = maku.dens;
    return d > 120u ? 70u + (d - 120u) * 30u / 7u : 70u;
}

/* the kick's chance (percent) on step idx of 16 at density d: each rung adds steps, and fades them in so the
 * pulse grows rather than switches. d<4: silence; d >= MAKU_FOUR: steps 0 4 8 12, always */
static uint32_t maku_kick_chance(uint32_t idx, uint32_t d)
{
    if (idx == 0)
        return d < 4u ? 0u : d < 40u ? 25u + (d - 4u) * 75u / 36u : 100u;
    if (idx == 8u)
        return d < 40u ? 0u : d < 68u ? (d - 40u) * 100u / 28u : 100u;
    if (!(idx & 3u))                                /* 4, 12 */
        return d < 68u ? 0u : d < MAKU_FOUR ? (d - 68u) * 100u / 28u : 100u;
    return 0u;
}

/* the ornaments above the four on the floor: noise lanes of the kick's own DRUM track, on the steps the kick leaves
 * free. Each fades in over its own stretch of DENSITY above MAKU_FOUR; returns the lane bit (0 = none) and the chance */
static uint32_t maku_orn(uint32_t idx, uint32_t d, uint32_t *chance)
{
    uint32_t over = d > MAKU_FOUR ? d - MAKU_FOUR : 0u;
    *chance = 0;
    if (idx >= 16u || !(idx & 3u))
        return 0;
    if ((idx & 3u) == 2u) {                         /* the off-beat: the open hat, certain at 127 */
        *chance = over * 100u / (127u - MAKU_FOUR);
        return 1u << 4;
    }
    *chance = over > 8u ? (over - 8u) * 60u / 23u : 0u;   /* the other 16ths: the closed hat */
    return 1u << 3;
}

/* the kick's black keys: eight pads of hiss and grit (GM notes, dark to bright: ride 2's shimmer, long guiro's scrape,
 * pedal hat's tick, cabasa, tambourine, maracas, open hat, splash), every one noise (hats and cymbals, no clap or
 * snare body), played on the kick's own DRUM track. Short ones are the "chiri" grit, long ones the "saa" hiss */
static const uint8_t MAKU_PAD[8] = {59, 74, 44, 69, 54, 70, 46, 55};


/* the three parameters of each track (docs/AMBIENT.md): SELECT, PRESETS and ALGORITHM each own one macro, 0..127,
 * and a macro moves up to three real parameters at once, lo at 0 and hi at 127 (hi below lo: it runs down).
 * p 0xFF: nothing (the macro's effect is read from maku.* directly: DENSITY, OPEN, LOOSE) */
typedef struct { uint8_t p; int16_t lo, hi; } mm_t;
#define MM_NONE 0xFFu
#define MAKU_NM 5u                               /* ALGORITHM (j = 2) owns up to five parameters, the others three */
static const struct { const char *name; mm_t m[MAKU_NM]; } MAKU_MAC[4][3] = {
    {   /* 1 KICK */
        {"DENS", {{MM_NONE, 0, 0}, {MM_NONE, 0, 0}, {MM_NONE, 0, 0}}},
        {"TONE", {{P_E2, 0, 127}, {P_E4, 0, 127}, {P_E3, 127, 30}}},
        {"WASH", {{P_REV, 0, 127}, {P_DLY, 0, 100}, {P_E7, 0, 127}, {MM_NONE, 0, 0}, {MM_NONE, 0, 0}}},
    },
    {   /* 2 DRONE */
        {"OPEN", {{MM_NONE, 0, 0}, {MM_NONE, 0, 0}, {MM_NONE, 0, 0}}},
        {"TONE", {{P_E2, 0, 127}, {P_E6, 0, 127}, {P_IMIX, 0, 127}}},   /* (the INSERT's MIX; a world with none: inert) */
        {"SWAY", {{P_CHOR, 0, 127}, {P_LD_FLT, 0, 127}, {P_REL, 20, 127}, {P_IB, 30, 127}, {P_DETUNE, 0, 45}}},
    },
    {   /* 3 SHIMMER */
        {"GRAIN", {{P_E3, 0, 127}, {P_E2, 127, 20}, {P_IMIX, 0, 127}}},   /* (.. and here) */
        {"TONE", {{P_E7, 0, 127}, {P_E5, 0, 127}, {P_E6, 0, 100}}},
        {"AIR", {{P_REV, 0, 127}, {P_DLY, 0, 127}, {P_ATK, 20, 127}, {P_IB, 30, 127}, {P_DETUNE, 0, 45}}},
    },
    {   /* 4 ARP */
        {"LOOSE", {{MM_NONE, 0, 0}, {MM_NONE, 0, 0}, {MM_NONE, 0, 0}}},
        {"TONE", {{P_E2, 0, 127}, {P_E3, 127, 20}, {P_E5, 20, 127}}},
        {"TRAIL", {{P_DLY, 0, 127}, {P_REV, 0, 127}, {P_REL, 0, 127}, {P_CHOR, 0, 70}, {MM_NONE, 0, 0}}},
    },
};

static mm_t maku_tone[4][3];         /* each track's TONE macro (j = 1): MAKU_MAC's, or the voice's own (maku_voices.c) */
static void maku_set_density(uint32_t d);
/* macro j of track i to v (0..127): stored, and every parameter it owns moves */
static void maku_macro_set(uint32_t i, uint32_t j, uint32_t v)
{
    uint32_t n;
    if (i >= 4u || j >= 3u)
        return;
    v = v > 127u ? 127u : v;
    maku.m[i][j] = (uint8_t)v;
    if (j == 1u)                                     /* LIFT: every track's TONE, a way up to the top (TONE 127 and full: all the way) */
        v += ((127u - v) * maku_vamt(VB_LIFT, 0)) / 127u;
    for (n = 0; n < (j == 2u ? MAKU_NM : 3u); n++) {
        const mm_t *e = j == 1u ? &maku_tone[i][n] : &MAKU_MAC[i][j].m[n];
        int32_t lo = e->lo, hi = e->hi;
        if (e->p == MM_NONE)
            continue;
        if (maku_held(i) && (e->p == P_ATK || e->p == P_REL)) {   /* a held part: ATK / REL move around the voice's own */
            lo = (e->p == P_ATK ? maku.atk0[i] : maku.rel0[i]) - 12;
            hi = lo + 24;
            lo = lo < 0 ? 0 : lo;
            hi = hi > 127 ? 127 : hi;
        }
        if (maku.pm[i] != PM_WARP) {                                /* the part's pitch level bounds what wavers it */
            if (e->p == P_E6 && trk[i].eng_req == MAKU_ENGI_GRAIN) {
                lo = MAKU_PM[maku.pm[i]].rlo;
                hi = MAKU_PM[maku.pm[i]].rhi;
            } else if (e->p == P_IB && (trk[i].p[P_ITYPE] == IT_FLANGER || trk[i].p[P_ITYPE] == IT_CHORUS)) {
                lo = MAKU_PM[maku.pm[i]].dlo;
                hi = MAKU_PM[maku.pm[i]].dhi;
            }
        }
        trk[i].p[e->p] = (int16_t)(lo + ((hi - lo) * (int32_t)v + (hi >= lo ? 63 : -63)) / 127);
    }
    if (i == MAKU_DRB && j == 2)
        fx_rvs = (uint8_t)(v < 20u ? 0u : (v - 20u) * 90u / 107u);   /* AIR: the reverse swell comes in above ~20 */
    if (i == MAKU_KICK && j == 0)
        maku_set_density(v);
    else if (i == MAKU_DRA && j == 0)
        maku.open = (uint8_t)v;
    else if (i == MAKU_ARP && j == 0)
        maku.loose = (uint8_t)v;
}

/* the kick's pattern, set by hand: bit i = step i of 16 holds a kick (lane 0) in the track's stored steps */
static uint32_t maku_kick_mask(void)
{
    uint32_t i, m = 0;
    for (i = 0; i < 16u; i++)
        m |= (step_lanes(&trk[MAKU_KICK].step[i]) & 1u) << i;
    return m;
}
static uint32_t maku_kick_accents(void)
{
    uint32_t i, m = 0;
    for (i = 0; i < 16u; i++)
        m |= (step_accents(&trk[MAKU_KICK].step[i]) & 1u) << i;
    return m;
}
/* the steps DENSITY can add as a ghost now (not stored): bit i = chance above 0 */
static uint32_t maku_kick_ghosts(void)
{
    uint32_t i, m = 0;
    for (i = 0; i < 16u; i++)
        m |= (uint32_t)(maku_kick_chance(i, maku_kick_d(maku.dens)) > 0u) << i;
    return m & ~maku_kick_mask();
}

/* DENSITY as played */
static uint32_t maku_eff(void)
{
    return maku.dens;
}

/* a note played on the keys (the scale's mask of the arp's track says if it is an avoid note). It takes the place of the weakest
 * one remembered, which is the oldest: what was played first is pushed out first */
static void maku_pick(uint32_t note)
{
    uint32_t m = scale_mask(&trk[MAKU_ARP]), i, w = 0;
    for (i = 1; i < MAKU_PK; i++)
        if (maku.pk_w[i] < maku.pk_w[w])
            w = i;
    maku.pk[w] = (uint8_t)note;
    maku.pk_w[w] = 255u;
    maku.pk_new = (uint8_t)(maku.pk_new | (1u << w));
    maku.pk_av = (uint8_t)((maku.pk_av & ~(1u << w)) | ((((m >> ((note + 120u - (uint32_t)trk[MAKU_ARP].p[P_ROOT]) % 12u)) & 1u) ? 0u : 1u) << w));
}

/* the SELECT macro of the drones and the arp also stretches their time: below the middle the more of their steps are left
 * out (the kick's DENSITY the other way round), up to `max` percent. Rolled where the part would sound a new note */
static uint32_t maku_spare(uint32_t i, uint32_t max)
{
    uint32_t m = maku.m[i][0];
    return m < 64u && (uint32_t)(rng() % 100u) < (64u - m) * max / 64u;     /* (the middle and above: as before) */
}

/* ARP, one step of the phrase table as it runs. CASCADE (casc) plays every step and ignores the breath, SCRAMBLE (scr) jumps
 * about the table and re-rolls more of it */
static uint32_t maku_arp_step(const track_t *t, uint32_t idx, uint32_t d, uint32_t casc, step_t *out)
{
    uint32_t deg, scr = maku_vamt(VB_SCRAM, 0);
    if (idx >= MAKU_ARP_GAP && !casc) {             /* the breath: the phrase ends and the next one starts elsewhere */
        maku.run_left = 0;
        maku.frame_on = 0;
        return 0;
    }
    if (casc ? (uint32_t)(rng() % 127u) >= maku.vp[VB_CASC][0] : (d < 16u || (uint32_t)(rng() % 100u) >= 15u + (d - 16u) * 85u / 95u))
        return 0;
    if (!casc && maku_spare(MAKU_ARP, 65u))         /* LOOSE to the left: a longer, emptier phrase */
        return 0;
    if (scr && (uint32_t)(rng() % 127u) < scr)      /* SCRAMBLE: a jump now and then, to wherever */
        maku.run_left = 0;
    if (!maku.run_left) {                           /* jump: a random place, a run of 3..8 (shorter when dense) */
        maku.frame_on = 0;                          /* (a new run starts in the key as it is) */
        maku.pos = (uint8_t)(rng() % MAKU_TABLE);
        maku.run_left = (uint8_t)(3u + rng() % (6u - d / 32u));
    }
    if (maku.pk_new) {                              /* a key just played: the next arp step sounds it (any of them, if several), once */
        uint32_t j = 0, n = 0, k;
        for (k = 0; k < MAKU_PK; k++)
            if (((maku.pk_new >> k) & 1u) && maku.pk_w[k] && !(rng() % ++n))
                j = k;
        maku.pk_new = 0;
        if (n) {
            out->n = 1;
            out->note[0] = maku.pk[j];
            out->vel = (uint8_t)(52u + rng() % 20u);
            return 1;
        }
    }
    if ((uint32_t)(rng() % 100u) < 35u) {           /* what was played melts into the phrase: a note still remembered is taken as often */
        uint32_t j = 0, k, live = 0, pick, take;    /* as it is strong, and every try fades them all; an avoid note is taken less often */
        for (k = 0; k < MAKU_PK; k++)
            live += maku.pk_w[k] != 0;
        if (live) {
            pick = rng() % live;
            for (k = 0; k < MAKU_PK; k++)
                if (maku.pk_w[k] && !pick--) {
                    j = k;
                    break;
                }
            take = (uint32_t)(rng() % 255u) < maku.pk_w[j] && (!((maku.pk_av >> j) & 1u) || (uint32_t)(rng() % 100u) < 35u);
            for (k = 0; k < MAKU_PK; k++)
                if (maku.pk_w[k]) {                 /* (an avoid note fades twice as fast) */
                    uint32_t dec = (1u + maku.pk_w[k] / 24u) << ((maku.pk_av >> k) & 1u);
                    maku.pk_w[k] = (uint8_t)(dec < maku.pk_w[k] ? maku.pk_w[k] - dec : 0u);
                }
            if (take) {
                out->n = 1;
                out->note[0] = maku.pk[j];
                out->vel = (uint8_t)(52u + rng() % 20u);
                return 1;
            }
        }
    }
    maku.run_left--;
    deg = MAKU_PHRASE[maku.pos];
    maku.pos = (uint8_t)((maku.pos + 1u) % MAKU_TABLE);
    if (deg == MAKU_REST)
        return 0;
    if ((uint32_t)(rng() % 100u) < d / 3u + maku.loose / 3u + scr * 40u / 127u)   /* loosen: re-roll this note */
        deg = 7u + rng() % 10u;
    else if ((uint32_t)(rng() % 100u) < maku.loose / 4u)         /* and climb: an octave up */
        deg += 7u;
    out->n = 1;
    if (maku.frame_on)                              /* a key change mid-run: the run stays where it is, in the new signature */
        out->note[0] = (uint8_t)maku_resig(maku_note_in(t, maku.frame_root, deg, 48u), scale_mask(t),
                                            maku.frame_root, (uint32_t)t->p[P_ROOT]);
    else
        out->note[0] = (uint8_t)maku_note(t, deg, 48u);
    out->vel = (uint8_t)(46u + rng() % (14u + d / 4u));
    return 1;
}

/* a third above n: the second tone of the scale of track t above it (3 or 4 semitones, in the scale, HARM's); 0 if there is none */
static uint32_t maku_third(const track_t *t, uint32_t n)
{
    uint32_t mask = scale_mask(t), root = (uint32_t)t->p[P_ROOT] % 12u, m, left = 2u;
    for (m = n + 1u; m < 128u && m <= n + 7u; m++)
        if (((mask >> ((m + 120u - root) % 12u)) & 1u) && !--left)
            return m;
    return 0;
}

/* the arp's step: the table, then CASCADE's octaves (SPAN) and a third above (HARM) stacked on the note */
static uint32_t maku_arp_out(const track_t *t, uint32_t idx, uint32_t d, step_t *out)
{
    uint32_t r, casc;
    if (maku_held(MAKU_ARP) && idx % MAKU_SLOW)       /* a slow melody moves on every 4th step only */
        return 0;
    casc = maku_roll(VB_CASC);                        /* (CASCADE takes this step as often as it has risen) */
    r = maku_arp_step(t, idx, d, casc, out);
    if (r && casc) {
        uint32_t sp = maku.vp[VB_CASC][1], n0 = out->note[0], th;
        if (sp >= 32u && n0 + 12u < 128u)
            out->note[out->n++] = (uint8_t)(n0 + 12u);
        if (sp >= 80u && n0 + 24u < 128u)
            out->note[out->n++] = (uint8_t)(n0 + 24u);
        if (out->n < 4u && (uint32_t)(rng() % 127u) < maku.vp[VB_CASC][2] && (th = maku_third(t, n0)) != 0u)
            out->note[out->n++] = (uint8_t)th;
    }
    if (r && (uint32_t)(rng() % 254u) < maku_vamt(VB_SCRAM, 0))   /* SCRAMBLE: a note now and then stutters, 2 to 4 times */
        step_set_ratchet(out, 2u + rng() % 3u);
    return r;
}

/* SWELL held: this time the chord (note) stays, with the chance STAY says (a held part rings on, a struck one plays the last again) */
static uint32_t maku_stay(void)
{
    return maku_roll(VB_SWELL) && (uint32_t)(rng() % 127u) < maku.vp[VB_SWELL][2];
}

/* the step track i plays at idx, in place of its stored one. 0 = rest */
static __attribute__((noinline)) uint32_t maku_step(uint32_t i, uint32_t idx, step_t *out)
{
    const track_t *t = &trk[i];
    uint32_t d = maku_eff(), vel;
    uint32_t tie;
    memset(out, 0, sizeof *out);
    out->time = ST_NOTE;
    if (((maku.sw_want >> i) & 1u) && (i == MAKU_DRA ? idx % 19u == 18u : i == MAKU_DRB ? idx == 6u : idx == MAKU_ARP_GAP)) {   /* DRIFT: the rest */
        maku.sw_go = (uint8_t)(maku.sw_go | (1u << i));                                                                        /* before the next chord (note): change the voice now */
        maku.sw_want = (uint8_t)(maku.sw_want & ~(1u << i));
    }
    if ((i == MAKU_DRA || i == MAKU_DRB) && ((maku.rst >> (i - MAKU_DRA)) & 1u)) {   /* SWELL was pressed: the chord (the haze's last) again, now, */
        uint32_t c = i == MAKU_DRA ? 0u : maku.dlast;                              /* its attack long by then */
        maku.rst &= (uint8_t)~(1u << (i - MAKU_DRA));
        if ((i == MAKU_DRA || c) && c < 3u && ((maku.dvalid >> c) & 1u)) {
            *out = maku.dc[c];
            return 1;
        }
    }
    tie = maku_is_tie(i, idx);
    if (i == MAKU_DRA && idx % 19u == 0)                /* OPEN, GRAIN: the next chord / note is left out now and then (a held part rings on); SWELL's STAY too */
        maku.stay_a = (uint8_t)(maku_held(i) && (maku_spare(i, 70u) || maku_stay()));
    if (i == MAKU_DRB && idx == 7u)
        maku.stay_b = (uint8_t)(maku_held(i) && (maku_spare(i, 70u) || maku_stay()));
    if (tie) {                      /* a held part: this step only extends the note */
        out->time = ST_TIE;
        return 1;
    }
    switch (i) {
    case MAKU_KICK: {
        uint32_t hit = 0, oc, ol;
        if (idx >= 16u)
            return 0;
        if (maku.bw > 16384) {                      /* the breath: the kick is a soft cue at the head of the bar (the inhale), nothing else */
            if (idx)
                return 0;
            out->hit = 1;
            out->vel = MAKU_CUE_VEL;
            return 1;
        }
        d = maku_kick_d(d);                         /* (the kick's own reach into DENSITY) */
        vel = 0;
        if ((maku_kick_mask() >> idx) & 1u) {       /* a step set by hand (the grid) always plays; DENSITY only adds ghosts */
            vel = 96u + ((step_accents(&t->step[idx]) & 1u) ? 24u : 0u);
            hit = 1u;
        } else if ((uint32_t)(rng() % 100u) < maku_kick_chance(idx, d) + maku_vamt(VB_SCRAM, 1) * 60u / 127u) {   /* (SCRAMBLE: ghosts everywhere) */
            vel = 30u + d * 80u / 127u;
            if (idx == 0 && d >= 40u)
                vel += 10u;
            vel = (uint32_t)((int32_t)vel + (int32_t)(rng() % (1u + d / 8u)) - (int32_t)(d / 16u));   /* the wander grows with density */
            hit = 1u;                               /* lane 0: the kick */
        }
        if (hit && idx && (uint32_t)(rng() % 100u) < maku_vamt(VB_SCRAM, 1) * 30u / 127u)   /* SCRAMBLE: and a beat is left out now and then */
            hit = 0;
        if (hit && (uint32_t)(rng() % 100u) < maku_vamt(VB_SCRAM, 1) * 45u / 127u)       /* .. or struck again, 2 to 3 times (a roll) */
            step_set_ratchet(out, 2u + rng() % 2u);
        if (hit && d >= 40u) {                      /* ducking: the drones sink under it, deeper as it grows */
            int32_t dip = maku_dip(4096 - (int32_t)((d - 40u) * 1800u / 87u));   /* down to 0.56 at DENSITY 127 */
            if (dip < maku.duck)
                maku.duck = dip;
        }
        ol = maku_orn(idx, d, &oc);                 /* above the four on the floor: noise ornaments (hats) */
        if (ol && oc && (uint32_t)(rng() % 100u) < oc) {
            hit |= ol;
            if (!vel)
                vel = 26u + (d - MAKU_FOUR) * 40u / (127u - MAKU_FOUR) + (uint32_t)(rng() % 12u);
        }
        if (!hit)
            return 0;
        out->hit = (uint8_t)hit;
        out->vel = (uint8_t)(vel > 127u ? 127u : vel);
        return 1;
    }
    case MAKU_DRA:
        if (maku.bw > 16384) {                      /* the breath: the floor is struck once a bar, with the inhale (the kick track's step 0) */
            if (trk[MAKU_KICK].seq_idx)
                return 0;
            idx = (maku.bar_c++ % 3u) * 19u;        /* (the chords go on in their order, three to a cycle) */
        } else if (idx % 19u)
            return 0;
        if (!maku_held(i) && ((maku.dvalid >> 0) & 1u) && maku_stay()) {
            *out = maku.dc[0];                          /* SWELL: the struck chord stays (STAY: how surely) */
            return 1;
        }
        if (idx == 0)
            maku.cyc_a++;
        if (!maku_held(i) && maku.bw <= 16384 && maku_spare(i, 55u))   /* OPEN: a struck chord left out (a held one: stay_a) */
            return 0;
        {
            const uint8_t *c = MAKU_CHORD[(idx / 19u + 3u * maku.cyc_a) % 6u];   /* (3 a cycle: six chords in two cycles) */
            uint32_t k, nv = 2u + maku.open / 43u;       /* OPEN: 2 voices .. root, 5th, octave and the 9th above */
            if ((nv > 3u && d >= 90u) || maku_held(MAKU_DRA))
                nv = 3u;                                /* (dense and open: the voices run out; a held drone has its cap) */
            if (maku_held(MAKU_DRA) && nv > maku_voice_cap(t))
                nv = maku_voice_cap(t);
            for (k = 0; k < 3u && k < nv; k++)
                out->note[k] = (uint8_t)maku_note(t, c[k], maku_low(36u));
            if (nv > 3u)
                out->note[k++] = (uint8_t)maku_note(t, c[0] + 9u, maku_low(36u));
            out->n = (uint8_t)k;
        }
        out->vel = (uint8_t)(78u - d / 4u - (idx / 19u) * 6u);
        maku.dc[0] = *out;
        maku.dvalid |= 1u;
        return 1;
    case MAKU_DRB:
        if (idx != 0 && idx != 7u)
            return 0;
        if (!maku_held(i) && ((maku.dvalid >> (1u + (idx != 0u))) & 1u) && maku_stay()) {
            *out = maku.dc[1u + (idx != 0u)];
            maku.dlast = (uint8_t)(1u + (idx != 0u));
            return 1;
        }
        if (idx == 0)
            maku.cyc_b++;
        if (!maku_held(i) && maku_spare(i, 90u))        /* GRAIN: fewer notes of the haze (the smaller the grain, the sparser: up to 90 %) */
            return 0;
        if (idx == 0) {
            out->note[0] = (uint8_t)maku_note(t, MAKU_SHIM_A[maku.cyc_b % 6u], maku_low(36u));
            out->n = 1;
        } else {
            uint32_t lo = MAKU_SHIM_B[maku.cyc_b % 6u];
            out->note[0] = (uint8_t)maku_note(t, lo, maku_low(48u));
            out->note[1] = (uint8_t)maku_note(t, lo + 4u, maku_low(48u));
            out->n = 2;
        }
        out->vel = (uint8_t)(58u - d / 5u);
        maku.dc[1u + (idx != 0u)] = *out;
        maku.dvalid |= 1u << (1u + (idx != 0u));
        maku.dlast = (uint8_t)(1u + (idx != 0u));
        return 1;
    case MAKU_ARP:
        return maku_arp_out(t, idx, d, out);
    }
    return 0;
}

/* DENSITY changed (UI knob, MIDI CC): what follows from it besides the steps */
static void maku_apply(void)
{
    uint32_t d = maku.dens;
    uint32_t sw = (d * 24u / 127u + (uint32_t)maku.vp[VB_GLO][1] * 40u / 127u * (uint32_t)(32768 - maku.bw) / 32768u + maku_vamt(VB_SCRAM, 2) * 70u / 127u);   /* the arp drifts off the grid as it gets dense; GLO's SWING and SCRAMBLE's add to it */
    trk[MAKU_ARP].p[P_SSWING] = (int16_t)(sw > 100u ? 100u : sw);
}

static void maku_set_density(uint32_t d)
{
    maku.dens = (uint8_t)(d > 127u ? 127u : d);
    maku_apply();
}

/* the kick does not follow the root: its TUNE stays MAKU_KICK_LOW (2 semitones) under the design (E1 64): a lower, rounder kick */
#define MAKU_KICK_LOW 11
static void maku_follow(void)
{
    uint32_t i;
    trk[MAKU_KICK].p[P_E1] = 64 - MAKU_KICK_LOW;
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_ROOT] = trk[MAKU_DRA].p[P_ROOT];
        trk[i].p[P_SCALE] = trk[MAKU_DRA].p[P_SCALE];
    }
}

/* OCT+ / OCT- (dir > 0 / < 0): the key moves a fifth up / down the circle of fifths: a sharp more / a flat more. It is a change
 * of key signature, not a transposition: what already plays or was played keeps its pitch, and only the one note the
 * signature changes moves (F to F# from C major to G major). The notes played (melting into the phrase) are mapped that way at once and an
 * arp run in progress goes on in the old frame (maku_step); what starts after this is built on the new ROOT.
 * The chords already sounding ring on: they are not retuned, the next one is in the new key */
static void maku_fifth(int dir)
{
    track_t *a = &trk[MAKU_DRA];
    uint32_t from = (uint32_t)a->p[P_ROOT] % 12u, to = (from + (dir > 0 ? 7u : 5u)) % 12u, mask = scale_mask(a), i;
    if (!maku.on)
        return;
    for (i = 0; i < MAKU_PK; i++)
        maku.pk[i] = (uint8_t)maku_resig(maku.pk[i], mask, from, to);
    if (maku.run_left && !maku.frame_on) {
        maku.frame_root = (uint8_t)from;
        maku.frame_on = 1;
    }
    a->p[P_ROOT] = (int16_t)to;
    maku_follow();
    maku.root = (uint8_t)to;
    for (i = 0; i < MAKU_PK; i++)                                  /* (the avoid flags: against the new key) */
        maku.pk_av = (uint8_t)((maku.pk_av & ~(1u << i)) | ((((mask >> ((maku.pk[i] + 120u - to) % 12u)) & 1u) ? 0u : 1u) << i));
}

/* track t's parameter p moved for the verbs that hold it (slot: its own place in maku.ov): by off, or by f (Q12) of the way to its
 * top, whichever is more, so two verbs on one parameter do not add up. What a macro or a world writes in meanwhile is the new
 * base, so the value goes back to where they put it */
static void maku_over(uint32_t slot, uint32_t t, uint32_t p, int32_t off, int32_t f)
{
    int16_t *v = &trk[t].p[p];
    int32_t n, up;
    if (*v != maku.ov[slot].w)
        maku.ov[slot].b = *v;
    up = (TP[p].max - maku.ov[slot].b) * f >> 12;
    n = clamp(maku.ov[slot].b + (off > up ? off : up), TP[p].min, TP[p].max);
    *v = maku.ov[slot].w = (int16_t)n;
}

/* DRIFT, once per block: while it is on the values it reaches wander, a step of the walk now and then, never further from where
 * they were at the switch-on than RANGE says (a value moved by hand meanwhile is the new place to stay near), and stay where they
 * land. REACH: the 12 macros; from 43 the space; from 86 the tempo, swing and duck too (the tempo only on its own clock) */
#define MAKU_SW_BLOCKS 55000u   /* DRIFT changes the voice of one of the floor, the haze or the arp every 40 s (44.1 kHz, 32-sample blocks) and up to 20 s more */
static int32_t maku_walk(int32_t v, int32_t a, int32_t span, int32_t top)
{
    int32_t lo = a - span < 0 ? 0 : a - span, hi = a + span > top ? top : a + span;
    v += (int32_t)(rng() % 3u) - 1;
    return v < lo ? lo : v > hi ? hi : v;
}
static int32_t maku_dr_get(uint32_t k)
{
    return k < 12u ? maku.m[k / 3u][k % 3u] : k == 12u ? song.g[G_BPM] : k == 13u ? maku.vp[VB_GLO][1] : k == 14u ? maku.vp[VB_GLO][2] : maku.drs;
}
static void maku_dr_put(uint32_t k, int32_t v)
{
    if (k < 12u)
        maku_macro_set(k / 3u, k % 3u, (uint32_t)v);
    else if (k == 12u)
        song.g[G_BPM] = (int16_t)v;
    else if (k < 15u)
        maku.vp[VB_GLO][k - 12u] = (uint8_t)v;
    else
        maku.drs = (uint8_t)v;
}
static void maku_drift(void)
{
    uint32_t k;
    int32_t span, reach = maku.vp[VB_DRIFT][2], v, cur;
    if (!maku.dr_tog) {
        maku.dr_on = 0;
        return;
    }
    if (!maku.dr_on) {
        for (k = 0; k < 16u; k++)
            maku.dra[k] = maku.drw[k] = (int16_t)maku_dr_get(k);
        maku.dr_on = 1;
        maku.dr_t = 0;
        maku.sw_t = 0;
        maku.sw_at = MAKU_SW_BLOCKS + rng() % (MAKU_SW_BLOCKS / 2u);
    }
    if (!maku.sw_want && !maku.sw_go && ++maku.sw_t >= maku.sw_at) {   /* a voice is due: one of the three, not the one changed last; it waits for its rest */
        uint32_t p = 1u + rng() % 3u;
        if (p == maku.sw_last)
            p = 1u + p % 3u;
        maku.sw_want = (uint8_t)(1u << p);
        maku.sw_last = (uint8_t)p;
        maku.sw_t = 0;
        maku.sw_at = MAKU_SW_BLOCKS + rng() % (MAKU_SW_BLOCKS / 2u);
    }
    maku.dr_t = (uint16_t)(maku.dr_t + 1u + maku.vp[VB_DRIFT][0] / 8u);
    if (maku.dr_t < 64u)
        return;
    maku.dr_t = (uint16_t)(maku.dr_t - 64u);
    span = maku.vp[VB_DRIFT][1] / 2;
    for (k = 0; k < 16u; k++) {
        if (k == 0u)                                    /* the kick's DENSITY (macro 0 of track 0) is never drifted */
            continue;
        if (k >= 12u && k < 15u && reach < 86)
            continue;
        if (k == 15u && reach < 43)
            continue;
        cur = maku_dr_get(k);
        if (cur != maku.drw[k])                         /* turned by hand (or by the tempo layer) since: it is the new anchor */
            maku.dra[k] = (int16_t)cur;
        if (k == 12u && song.g[G_CLOCK]) {              /* (the tempo: only on its own clock) */
            maku.dra[k] = maku.drw[k] = (int16_t)cur;
            continue;
        }
        v = maku_walk(cur, maku.dra[k], k == 12u ? maku.vp[VB_DRIFT][1] * 16 / 127 : span, k == 12u ? 240 : 127);
        if (k == 12u && v < 70)                         /* the tempo never wanders down the breath slope (BREATH_PLAN 1) */
            v = cur;
        if (v != cur)
            maku_dr_put(k, v);
        maku.drw[k] = (int16_t)maku_dr_get(k);
    }
}

/* The upper verbs' times, from their knobs (0..127) and the tempo: RISE a quarter of a beat to 8 beats, HOLD none, then half a beat
 * to 32 beats, FADE 2 beats to 32 beats (doubling every 25.4, 21.2 and 31.8 steps). In 1/16 beats; maku_blocks makes them
 * control blocks (CTL = 32 samples) */
static const uint16_t MAKU_FR[16] = {4096, 4277, 4467, 4664, 4871, 5087, 5312, 5547, 5793, 6049, 6317, 6597, 6889, 7194, 7512, 7845};
static uint32_t maku_exp(uint32_t k, uint32_t lo, uint32_t oct)
{
    uint32_t x = k * oct * 16u / 127u;
    return ((lo * MAKU_FR[x & 15u]) >> 12) << (x >> 4);
}
static uint32_t maku_units(uint32_t v, uint32_t j)
{
    uint32_t k = maku.vt[v][j];
    return j == 0u ? maku_exp(k, 4u, 5u) : j == 1u ? (k ? maku_exp(k, 8u, 6u) : 0u) : maku_exp(k, 32u, 4u);
}
static uint32_t maku_blocks(uint32_t v, uint32_t j)
{
    uint32_t b = (maku_units(v, j) * (beat_samples() >> 5)) >> 4;
    return b < 32u ? 32u : b;
}
/* the defaults, once (they are the player's, not a world's): RISE HOLD FADE of SINK (the dive in ~0.5 s, back in ~3 s as it was),
 * SCRAMBLE (quick), CASCADE, LIFT (it climbs), SWELL (it swells, then lets go slowly), WOBBLE */
static void maku_vt_defaults(void)
{
    static const uint8_t D[VB_N][3] = {
        [VB_SINK] = {32, 64, 30}, [VB_SCRAM] = {8, 70, 50}, [VB_CASC] = {20, 84, 64},
        [VB_LIFT] = {90, 60, 64}, [VB_SWELL] = {50, 84, 80}, [VB_WOB] = {30, 84, 64},
    };
    if (maku.vt_ok)
        return;
    memcpy(maku.vt, D, sizeof D);
    maku.vt_ok = 1;
}
/* verb v's amount, once per block: it rises while it is pressed (RISE), stays full while it is held and HOLD after (counted from
 * the button up, once it is full), then fades (FADE) along an S, slow at both ends. Pressed again on the way: from where it is */
#define MAKU_ONE (1 << 24)
static void maku_env(uint32_t v)
{
    uint32_t pr = (maku.hv >> v) & 1u;
    int32_t q = maku.vq[v];
    if (!pr && !maku.vph[v])
        return;
    if (pr && maku.vph[v] != 1u)
        maku.vph[v] = 1u;
    if (maku.vph[v] == 1u) {
        if (q < MAKU_ONE) {
            q += (int32_t)(MAKU_ONE / maku_blocks(v, 0));
            q = q > MAKU_ONE ? MAKU_ONE : q;
            if (pr)
                maku.vhl[v] = maku_blocks(v, 1);
        } else if (pr) {
            maku.vhl[v] = maku_blocks(v, 1);
        } else if (maku.vhl[v]) {
            maku.vhl[v]--;
        } else {
            maku.vph[v] = 2u;
            maku.vft[v] = 0;
        }
    }
    if (maku.vph[v] == 2u) {
        maku.vft[v] += MAKU_ONE / maku_blocks(v, 2);
        if (maku.vft[v] >= (uint32_t)MAKU_ONE) {
            q = 0;
            maku.vph[v] = 0;
        } else {
            int32_t t = (int32_t)(maku.vft[v] >> 12), sm = ((t * t) >> 12) * (3 * 4096 - 2 * t) >> 12;
            q = (4096 - sm) << 12;
        }
    }
    maku.vq[v] = q;
    maku.va[v] = (int16_t)(q >> 12);
}

/* The breath slope's weight w (docs/BREATH_PLAN.md 1), Q15: the tempo value's smoothstep((70 - v) / 20), 0 on an external clock.
 * maku.bw walks to it (about a breath: 1/4096 of the way each block), so a quick turn of the knob, or a clock that stops, never jumps. */
static int32_t maku_breath_target(void)
{
    int32_t v = song.g[G_BPM], y;
    if (song.g[G_CLOCK] || v >= 70)
        return 0;
    if (v <= 50)
        return 32768;
    y = (70 - v) * 32768 / 20;
    return (int32_t)(((int64_t)y * y >> 15) * (98304 - 2 * y) >> 15);
}
static void maku_breath_step(void)
{
    int32_t t = maku_breath_target(), d = (t - maku.bw) >> 12;
    maku.bw += d ? d : t > maku.bw ? 1 : t < maku.bw ? -1 : 0;
}

/* The breath: b(t) over the bar (the kick track's 16 steps, its position in the step: the phase is where the sequencer is, so
 * entering the slope never restarts it). The inhale takes the share 1 - E of the bar, E = 0.5 .. 0.6 .. 0.7 for EXHALE (GLO's
 * SWING knob) 0 .. 64 .. 127, drawn toward 0.5 by 1 - w; each half is a smoothstep. The drones' level follows it by DEPTH (GLO's
 * DUCK knob: 0 none, 64 an anchor, 127 a clear guide) times w. Q15 */
static int32_t maku_smooth(int32_t x)                    /* smoothstep of x (Q15, 0..32768) */
{
    return (int32_t)(((int64_t)x * x >> 15) * (98304 - 2 * x) >> 15);
}
static void maku_breath_env(void)
{
    const track_t *k = &trk[MAKU_KICK];
    uint32_t len = k->p[P_SLEN] > 0 ? (uint32_t)k->p[P_SLEN] : 16u, per = div_samples((uint32_t)k->p[P_SDIV]);
    uint32_t pos = k->seq_pos < per ? k->seq_pos : per, n = (k->seq_idx % len) * per + pos, d = (uint32_t)maku.vp[VB_GLO][2];
    int32_t ex = maku.vp[VB_GLO][1], e, ph, amt;
    ex = 16384 + (ex <= 64 ? ex * 3277 / 64 : 3277 + (ex - 64) * 3276 / 63);    /* the exhale's share, Q15: .5 .6 .7 */
    e = 16384 + (int32_t)((int64_t)(ex - 16384) * maku.bw >> 15);
    ph = per ? (int32_t)((uint64_t)n * 32768u / ((uint64_t)len * per)) : 0;
    if (ph >= 32768 - e)                                 /* the exhale: down from full */
        maku.bb = 32768 - maku_smooth((ph - (32768 - e)) * 32768 / (e ? e : 1));
    else                                                 /* the inhale: up from empty */
        maku.bb = maku_smooth(ph * 32768 / (32768 - e));
    amt = d <= 64u ? (int32_t)d * 1024 / 64 : 1024 + ((int32_t)d - 64) * 1434 / 63;     /* of the drones' level, Q12: .25 .. .6 */
    maku.bd = (int32_t)((int64_t)amt * maku.bw >> 15) * (32768 - maku.bb) / 32768;
}

/* once per block, before the tracks tick: a ROOT / SCALE edit on any track becomes everyone's */
static __attribute__((noinline)) void maku_block(void)
{
    uint32_t i;
    if (!maku.on) {
        maku.bw = 0;
        maku.bd = 0;
        return;
    }
    maku_breath_step();
    maku_breath_env();
    maku_env(VB_SINK);
    for (i = VB_SCRAM; i < VB_N; i++)
        if (i != VB_DRIFT)
            maku_env(i);
    if (((maku.hv & ~maku.pp) >> VB_SWELL) & 1u)                    /* SWELL pressed: the floor and the haze strike their chord again */
        maku.rst = 3u;
    maku.pp = maku.hv;
    maku_drift();
    if (maku.va[VB_LIFT] || maku.tw_q) {                           /* LIFT: every track's TONE macro moves together */
        maku.tw_q = maku.va[VB_LIFT];
        for (i = 0; i < 4u; i++)
            maku_macro_set(i, 1, maku.m[i][1]);
    }
    if (maku.va[VB_SWELL] || maku.va[VB_WOB] || maku.va[VB_LIFT] || maku.dive || maku.ovlive) {   /* SWELL: longer attacks and releases; WOBBLE: the LFOs; LIFT: EDGE; SINK: softer attacks */
        int32_t sa = (int32_t)maku_vamt(VB_SWELL, 0) * 4096 / 127, sr = (int32_t)maku_vamt(VB_SWELL, 1) * 4096 / 127;   /* (SWELL: a way up to the top, Q12) */
        int32_t wr = (int32_t)maku_vamt(VB_WOB, 0) * 40 / 127, wd = (int32_t)maku_vamt(VB_WOB, 1) / 2;
        int32_t wp = (int32_t)maku_vamt(VB_WOB, 2) / 6, le = (int32_t)maku_vamt(VB_LIFT, 1) * 80 / 127;
        int32_t sk = maku_dive() * 80 >> 12;                       /* SINK: the notes lose their bite as it dives (up to +80 on ATK, WASH's share) */
        for (i = 1; i < 4u; i++) {
            maku_over((i - 1u) * 6u, i, P_ATK, sk, sa);            /* (SINK's and SWELL's: the one that moves it more) */
            maku_over((i - 1u) * 6u + 1u, i, P_REL, 0, sr);
            maku_over((i - 1u) * 6u + 2u, i, P_LRATE, wr, 0);
            maku_over((i - 1u) * 6u + 3u, i, P_LD_FLT, wd, 0);
            maku_over((i - 1u) * 6u + 4u, i, P_LD_PIT, wp, 0);
            maku_over((i - 1u) * 6u + 5u, i, P_DIST, le, 0);
        }
        maku.ovlive = (uint8_t)(maku.va[VB_SWELL] || maku.va[VB_WOB] || maku.va[VB_LIFT] || maku.dive);
    }
    maku_apply();                                                  /* (SWING: SCRAMBLE's) */
    {                                                              /* SINK: the dive is its amount; the kick is out by the time it is a sixth down, and back with the fade */
        int32_t k = maku.va[VB_SINK] * 6, tgt;
        k = k > 4096 ? 4096 : k;
        tgt = 4096 - ((int32_t)maku.vp[VB_SINK][2] * 4096 / 127) * k / 4096;      /* (HUSH: how far) */
        maku.dive = maku.va[VB_SINK];
        maku.kfade = tgt < maku.kfade - 64 ? maku.kfade - 64 : tgt;               /* (out in ~50 ms at the most, back as it rises) */
    }
    maku.duck += (4096 - maku.duck) / 160 + (maku.duck < 4096);   /* back up in ~120 ms (CTL blocks) */
    if (maku.duck > 4096)
        maku.duck = 4096;
    for (i = 0; i < NTRK; i++)
        if (trk[i].p[P_ROOT] != maku.root || trk[i].p[P_SCALE] != maku.scale) {
            trk[MAKU_DRA].p[P_ROOT] = trk[i].p[P_ROOT];
            trk[MAKU_DRA].p[P_SCALE] = trk[i].p[P_SCALE];
            break;
        }
    maku_follow();
    maku.root = (uint8_t)trk[MAKU_DRA].p[P_ROOT];
    maku.scale = (uint8_t)trk[MAKU_DRA].p[P_SCALE];
}

/* an upper verb: its three are RISE HOLD FADE (maku.vt) */
static uint32_t maku_timed(uint32_t v)
{
    return v == VB_SINK || v == VB_SCRAM || v == VB_CASC || v == VB_LIFT || v == VB_SWELL || v == VB_WOB;
}

/* the level factor of track i (Q12) for the mixer (fx.c mix_part): the drones take the whole dip, the arp half of it;
 * 4096 with MAKU off, so every other mix is bit for bit as before */
static int32_t maku_gain(uint32_t i)
{
    if (!maku.on)
        return 4096;
    if (i == MAKU_KICK)
        return (int32_t)((int64_t)maku.kfade * (32768 - maku.bw * 65 / 100) >> 15);   /* the kick softens into the breath's cue (a third of its level) */
    return (i == MAKU_ARP ? 4096 - (4096 - maku.duck) / 2 : maku.duck) * (4096 - maku.bd) >> 12;
}

/* SINK's depth for fx.c (Q12): the sends and the reverse swell sink as far as WASH says */
static int32_t maku_dive(void)
{
    return maku.on ? maku.dive * (int32_t)maku.vp[VB_SINK][1] / 127 : 0;
}

/* x towards top by the verb's amount f (Q12) of its parameter k (0..127) */
static int32_t maku_up(int32_t x, int32_t top, int32_t f, int32_t k)
{
    return x < top ? x + (((top - x) * f >> 12) * k) / 127 : x;
}
/* the sends of track i (fx.c mix_part, the scale of P_CHOR * 258 ..) with the held verbs on them: the space DRIFT has added
 * washes everything but the kick; WOBBLE's DEPTH adds chorus */
static void maku_boost(uint32_t i, int32_t *c, int32_t *d, int32_t *r)
{
    int32_t f;
    if (!maku.on)
        return;
    if (i != MAKU_KICK && maku.drs) {
        *r = maku_up(*r, 127 * 258 * 9 / 10, 4096, maku.drs);
        *d = maku_up(*d, 127 * 258 * 6 / 10, 4096, maku.drs);
        *c = maku_up(*c, 127 * 258 * 8 / 10, 4096, maku.drs / 2);
    }
    if (i != MAKU_KICK && (f = maku.va[VB_WOB]) != 0)               /* WOBBLE: the chorus rises with DEPTH */
        *c = maku_up(*c, 127 * 258 * 8 / 10, f, maku.vp[VB_WOB][1]);
}

/* SINK's pull on the reverb for fx.c (Q12): its tail feeds back harder the longer FX is held, as far as SINK says */
static int32_t maku_sink(void)
{
    return maku.on ? maku.dive * (int32_t)maku.vp[VB_SINK][0] / 127 : 0;
}
