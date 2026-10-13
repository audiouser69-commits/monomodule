/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MAKU's voice catalog (docs/VOICES_RATING.md): for each of the floor, the haze and the voice, a list of sounds, each an
 * engine and preset with an articulation (ART_*, maku.c) and the changes that make it that sound. A world draws one row per
 * part by weight (maku_world). Included by ui.c.
 *
 * The catalog is append-only: each row has a stable id (its part's own, from 1), which the likes (maku_fav.c) keep and the
 * flash holds. A row's id and meaning never change; a new sound, or new weights for one, is a new row with a new id (an
 * old row is retired by weight 0: it is never drawn again, but a like naming it still plays it).
 * Changing a row or a weight changes what a seed means: raise MAKU_CATALOG_VERSION with it, so the emulator's rating log
 * (web/emu/felucca_web.c) does not mix ratings of different catalogs. It is the rating log's key only: the likes do not
 * look at it. */
#define MAKU_CATALOG_VERSION 2u      /* 2: the pitch levels (maku.c PM_*) change what a seed draws */

typedef struct {
    const char *name;
    uint8_t eng, pre;            /* the engine, and its preset to start from */
    uint8_t art;                 /* ART_* */
    uint8_t w;                   /* the weight of the draw */
    uint8_t a, s, r;             /* ATK, SUS, REL over the articulation's own, +1 (0: keep) */
    uint8_t e[8];                /* P_E0..P_E7 over the preset's, +1 (0: keep) */
    mm_t t[3];                   /* the TONE macro of this sound: p 0 keeps the part's default entry, MM_NONE none */
    uint8_t l;                   /* the part's LEVEL over MAKU_R's, +1 (0: keep): evens out the loudness of the rows */
    uint8_t id;                  /* the stable id (1..; unique in its part, never reused): what a like keeps */
} maku_voice_t;
#define V1(v) ((uint8_t)((v) + 1))
#define LV_ANALOG 72               /* the ANALOG pads sound ~4 dB louder than the PHYS / GRAIN rows at MAKU_R's level */

/* the articulation's own ADSR (STRK keeps the preset's; the part's REL and the haze's ATK are set in maku_sound) */
static const struct { uint8_t a, d, s, r; } MAKU_ART[] = {
    {0, 0, 0, 0},                /* STRK: nothing changed */
    {50, 0, 0, 0},               /* SOFT: ATK 50 (about 0.15 s) */
    {105, 90, 120, 105},         /* SWEL: ATK ~2 s, REL ~2 s */
    {80, 90, 127, 100},          /* BOW */
    {100, 90, 120, 105},         /* HOLD */
};

/* the engines' parameter choices that recur below */
#define T_PHYS_BOW {{P_E2, 0, 127}, {P_E6, 20, 127}, {0, 0, 0}}           /* BRIT, BOW: a bowed sound opens and bows harder */
#define T_ANALOG {{P_E4, 10, 127}, {P_E5, 0, 70}, {0, 0, 0}}              /* CUT, RES: a pad opens, without screaming */
#define T_GRAIN {{P_E2, 30, 127}, {P_E7, 20, 127}, {0, 0, 0}}              /* SIZE, TONE */
#define T_SOFT {{P_E2, 0, 110}, {P_E3, 127, 30}, {P_E5, 10, 100}}           /* BRIT, DAMP, ACC: a soft strike stays soft */
#define T_ANALOG_LINE {{P_E4, 10, 127}, {P_E5, 0, 70}, {MM_NONE, 0, 0}}

static const maku_voice_t MAKU_V_DRA[] = {          /* the floor */
    {"DRONE STRING", ENGI_PHYS, 7, ART_STRK, 3, .id = 1},
    {"HARP", ENGI_PHYS, 8, ART_STRK, 2, .id = 2},
    {"BOWED METAL", ENGI_PHYS, 3, ART_BOW, 3, .t = {{P_E2, 0, 127}, {P_E6, 30, 127}, {0, 0, 0}}, .id = 3},
    {"BOWED STRING", ENGI_PHYS, 2, ART_BOW, 2, V1(85), 0, 0, {0, 0, V1(50), V1(110), 0, 0, V1(70), 0},   /* the string model (the preset's), bowed */
        T_PHYS_BOW, .id = 4},
    {"GLASS", ENGI_PHYS, 0, ART_BOW, 2, V1(95), 0, V1(105), {0, V1(34), V1(60), 0, 0, 0, V1(50), 0}, T_PHYS_BOW, .id = 5},
    {"SOFT PAD", 0, 1, ART_HOLD, 3, .t = T_ANALOG, .l = V1(LV_ANALOG), .id = 6},
    {"STRINGS", 0, 11, ART_HOLD, 2, .t = T_ANALOG, .l = V1(LV_ANALOG), .id = 7},
    {"PWM STR", 0, 3, ART_HOLD, 2, .t = T_ANALOG, .l = V1(LV_ANALOG), .id = 8},
    {"FROZEN", 8, 2, ART_HOLD, 2, V1(110), 0, 0, {0}, T_GRAIN, .id = 9},
};
static const maku_voice_t MAKU_V_DRB[] = {          /* the haze */
    {"CLOUD PAD", 8, 0, ART_SWEL, 3, V1(98), .id = 1},
    {"FROZEN", 8, 2, ART_SWEL, 2, V1(105), .id = 2},
    {"SHIMMER", 8, 3, ART_SWEL, 2, V1(92), .id = 3},
    {"GLASS HIGH", ENGI_PHYS, 0, ART_BOW, 2, V1(95), 0, V1(105), {0, V1(34), V1(60), 0, 0, 0, V1(50), 0}, T_PHYS_BOW, .id = 4},
    {"SPARKLE", 8, 3, ART_STRK, 2, V1(20), .id = 5},
};
static const maku_voice_t MAKU_V_ARP[] = {          /* the voice */
    {"KALIMBA", ENGI_PHYS, 4, ART_STRK, 2, .id = 1},
    {"MARIMBA", ENGI_PHYS, 1, ART_STRK, 1, .id = 2},
    {"PLUCK", ENGI_PHYS, 2, ART_STRK, 1, .id = 3},
    {"BELL TREE", ENGI_PHYS, 0, ART_STRK, 1, .id = 4},
    {"HARP", ENGI_PHYS, 8, ART_STRK, 1, .id = 5},
    {"FELT KALIMBA", ENGI_PHYS, 4, ART_SOFT, 2, 0, 0, V1(80), {0, 0, V1(8), 0, 0, V1(50), 0, V1(10)}, T_SOFT, .id = 6},
    {"FELT MARIMBA", ENGI_PHYS, 1, ART_SOFT, 2, V1(45), 0, V1(70), {0, 0, V1(25), 0, 0, V1(60), 0, V1(5)}, T_SOFT, .id = 7},
    {"SOFT HARP", ENGI_PHYS, 8, ART_SOFT, 1, V1(55), 0, V1(85), {0, 0, 0, 0, 0, V1(55), 0, 0}, T_SOFT, .id = 8},
    {"SINE LINE", 0, 5, ART_SWEL, 3, V1(90), V1(110), V1(95), {0}, T_ANALOG_LINE, .id = 9},
    {"BOWED LINE", ENGI_PHYS, 3, ART_BOW, 1, V1(75), 0, V1(95), {0}, T_PHYS_BOW, .id = 10},
};
/* (not const: the host test lays a longer, reordered catalog in to show that a like finds its rows by id) */
static struct { const maku_voice_t *v; uint8_t n; } MAKU_V[4] = {
    {0, 0},
    {MAKU_V_DRA, (uint8_t)NELEM(MAKU_V_DRA)},
    {MAKU_V_DRB, (uint8_t)NELEM(MAKU_V_DRB)},
    {MAKU_V_ARP, (uint8_t)NELEM(MAKU_V_ARP)},
};

/* track i plays catalog row r, its sound loaded: the articulation, the row's changes and its TONE macro */
static void maku_voice_set(uint32_t i, uint32_t r)
{
    const maku_voice_t *v = &MAKU_V[i].v[r];
    track_t *t = &trk[i];
    uint32_t k;
    if (v->l)
        t->p[P_LEVEL] = (int16_t)(v->l - 1);
    maku.art[i] = v->art;
    maku.voice[i] = (uint8_t)r;
    if (MAKU_ART[v->art].a)
        t->p[P_ATK] = MAKU_ART[v->art].a;
    if (MAKU_ART[v->art].d)
        t->p[P_DEC] = MAKU_ART[v->art].d;
    if (MAKU_ART[v->art].s)
        t->p[P_SUS] = MAKU_ART[v->art].s;
    if (MAKU_ART[v->art].r)
        t->p[P_REL] = MAKU_ART[v->art].r;
    if (v->a)
        t->p[P_ATK] = (int16_t)(v->a - 1);
    if (v->s)
        t->p[P_SUS] = (int16_t)(v->s - 1);
    if (v->r)
        t->p[P_REL] = (int16_t)(v->r - 1);
    for (k = 0; k < 8u; k++)
        if (v->e[k])
            t->p[P_E0 + k] = (int16_t)(v->e[k] - 1);
    if (maku_held(i) && i != MAKU_ARP)
        t->p[P_SDIV] = 1;                           /* 1/8: a swell, a chord of seven seconds, has the time to arrive */
    maku.atk0[i] = (uint8_t)t->p[P_ATK];
    maku.rel0[i] = (uint8_t)t->p[P_REL];
    for (k = 0; k < 3u; k++)
        if (v->t[k].p)
            maku_tone[i][k] = v->t[k];
}
/* track i plays catalog row r: the sound (maku_sound), then the rest */
static void maku_voice_apply(uint32_t i, uint32_t r)
{
    const maku_voice_t *v = &MAKU_V[i].v[r];
    maku_sound(i, v->eng, v->pre);
    maku_voice_set(i, r);
}

/* a row of part i by weight among the articulations in `arts` (bit ART_*); `not_phys_bow`: no PHYS BOW row (a second
 * one would cost the CPU of another bowed model and its voices). Always returns a row. */
static uint32_t maku_voice_pick(uint32_t i, uint32_t arts, uint32_t not_phys_bow)
{
    uint32_t n = MAKU_V[i].n, r, tot = 0, x;
    for (r = 0; r < n; r++) {
        const maku_voice_t *v = &MAKU_V[i].v[r];
        if (((arts >> v->art) & 1u) && !(not_phys_bow && v->art == ART_BOW && v->eng == ENGI_PHYS))
            tot += v->w;
    }
    if (!tot)
        return maku_voice_pick(i, 0x1Fu, 0);
    x = rng() % tot;
    for (r = 0; r < n; r++) {
        const maku_voice_t *v = &MAKU_V[i].v[r];
        if (((arts >> v->art) & 1u) && !(not_phys_bow && v->art == ART_BOW && v->eng == ENGI_PHYS)) {
            if (x < v->w)
                return r;
            x -= v->w;
        }
    }
    return 0;
}
#define ARTS_ALL 0x1Fu
#define ARTS_HELD ((1u << ART_SWEL) | (1u << ART_BOW) | (1u << ART_HOLD))
#define ARTS_STRUCK ((1u << ART_STRK) | (1u << ART_SOFT))

/* DRIFT's change of voice (docs/AMBIENT.md; the main loop, once a pass): a part that has come to its rest (maku_step sets sw_go) takes
 * another row of its catalog, of its own kind (held or struck) and under the world's rule of no second PHYS bowed sound. Only the
 * sound changes: what the world and the player set (level, sends, INSERT, key, pattern, the macros' places) stays, and the TONE and
 * ATK / REL macros are put on the new voice. The old voice's last notes are let go (the engine switch fades), the next chord (note)
 * is the new voice's. Quiet on purpose: no message */
static void maku_voice_swap(void)
{
    uint32_t i, k, r = 0, arts, pb = 0;
    int16_t keep[P_E0];
    int32_t lv;
    const maku_voice_t *v, *old;
    track_t *t;
    for (i = MAKU_DRA; i <= MAKU_ARP; i++)
        if ((maku.sw_go >> i) & 1u)
            break;
    if (i > MAKU_ARP)
        return;
    maku.sw_go = (uint8_t)(maku.sw_go & ~(1u << i));
    if (!maku.on || !maku.dr_tog || maku.voice[i] == 0xFFu)      /* (DRIFT was switched off meanwhile) */
        return;
    arts = maku_held(i) ? ARTS_HELD : ARTS_STRUCK;
    for (k = MAKU_DRA; k <= MAKU_ARP; k++)
        if (k != i && maku.voice[k] != 0xFFu) {
            v = &MAKU_V[k].v[maku.voice[k]];
            pb |= v->art == ART_BOW && v->eng == ENGI_PHYS;
        }
    for (k = 0; k < 8u && (r = maku_voice_pick(i, arts, pb)) == maku.voice[i]; k++)
        ;
    if (r == maku.voice[i])
        return;
    v = &MAKU_V[i].v[r];
    old = &MAKU_V[i].v[maku.voice[i]];
    t = &trk[i];
    memcpy(keep, t->p, sizeof keep);
    maku.art[i] = ART_STRK;                            /* (as maku_sound begins) */
    for (k = 0; k < 3u; k++)
        maku_tone[i][k] = MAKU_MAC[i][1].m[k];
    if (t->eng_req != v->eng)
        set_engine_of(t, v->eng);
    apply_preset_to(t, v->pre);
    for (k = 0; k < P_E0; k++)                         /* everything but the envelope and the filter envelope, back as it was */
        if (k != P_ATK && k != P_DEC && k != P_SUS && k != P_REL && k != P_ED_FLT)
            t->p[k] = keep[k];
    maku_voice_set(i, r);
    lv = keep[P_LEVEL] + (v->l ? v->l - 1 : MAKU_R[i].lvl) - (old->l ? old->l - 1 : MAKU_R[i].lvl);   /* the level the player set, by what the rows differ */
    t->p[P_LEVEL] = (int16_t)(lv < 0 ? 0 : lv > 127 ? 127 : lv);
    for (k = 1; k < 3u; k++)
        maku_macro_set(i, k, maku.m[i][k]);
}

/* the row of part i whose stable id is `id`; 0xFF: none (a like from a later catalog, or a broken one) */
static uint32_t maku_voice_row(uint32_t i, uint32_t id)
{
    uint32_t r;
    if (i < 1u || i > 3u || !id)
        return 0xFFu;
    for (r = 0; r < MAKU_V[i].n; r++)
        if (MAKU_V[i].v[r].id == id)
            return r;
    return 0xFFu;
}
