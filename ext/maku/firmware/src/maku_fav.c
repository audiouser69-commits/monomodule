/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MAKU's likes (docs/SPEC.md, docs/decisions.md 2026-10-11): REC tapped keeps the world as it is now; HOME held then draws
 * the next world from them as often as there are likes, a little changed, so the machine grows towards what its player
 * liked. REC held ~2 s forgets them all. Included by ui.c after maku_world; the main loop only (never the audio ISR).
 *
 *   the ring   MAKU_FAV_MAX worlds, oldest first; a like more pushes the oldest out.
 *   a like     what maku_world decides, no more: the seed, the floor / haze / voice (by the catalog's stable ids,
 *              maku_voices.c), ROOT, SCALE, BPM, the 12 macros, the two INSERTs (type, RATE, DEPTH, FDBK) and the
 *              floor's and the haze's pitch levels (PM_*; a like kept before them has 0 there: WARP, it sounds as it did).
 *   a world    with N likes, one of them in MAKU_FAV_STEP * N % of the worlds (at most MAKU_FAV_CAP %), the newer the
 *              likelier (weights 1 .. N, oldest to newest); each of its parts (the three voices, ROOT, SCALE, every
 *              macro, the pitch levels) is drawn anew with MAKU_MUT_P % (from the catalog's weights and the macros' own ranges, as a new
 *              world would). No likes: maku_world, exactly as before.
 *   the flash  only when REC is pressed (a like or the clearing): project.c maku_fav_store, its own storage object.
 *              The blob has its own magic, version and checksum; a broken or foreign one starts the ring empty; a like
 *              naming a voice this catalog does not have (or a value out of range) is left out, the rest stay.
 *
 * The constants are to be set by ear. */
#define MAKU_FAV_MAX 16u             /* likes kept */
#define MAKU_FAV_STEP 15u            /* % of the worlds from the likes, per like */
#define MAKU_FAV_CAP 90u             /* .. at most (the rest are new) */
#define MAKU_MUT_P 8u                /* % per part of a like's world drawn anew */
#define MAKU_FAV_CLEAR_MS 2000u      /* REC held this long: the likes are forgotten */

#define MAKU_FAV_MAGIC 0x56464B4Du   /* "MKFV" */
#define MAKU_FAV_VER 1u
#define MAKU_FAV_ESZ 32u             /* bytes a like in the blob */
#define MAKU_FAV_HEAD 8u             /* magic, version, count, entry size */
#define MAKU_FAV_BYTES (MAKU_FAV_HEAD + MAKU_FAV_MAX * MAKU_FAV_ESZ + 4u)   /* .. and the checksum */
#define MAKU_FAV_NEL 18u             /* the parts a like's world can change: 3 voices, ROOT, SCALE, 12 macros, the pitch levels */

typedef struct {
    uint32_t seed;                   /* the world's seed (what it was drawn from; a like's own world keeps the seed it was made with) */
    uint8_t v[3];                    /* the floor, the haze, the voice: catalog ids (maku_voices.c), not rows */
    uint8_t root, scale, bpm;
    uint8_t m[12];                   /* the macros, track by track (SELECT PRESETS ALGORITHM) */
    uint8_t it[2], ia[2], ib[2], ic[2];   /* the INSERT of the floor and the haze: type, RATE, DEPTH, FDBK */
    uint8_t pm;                      /* the pitch levels: the floor's | the haze's << 2 (in a byte the blob had spare: 0 before them) */
} maku_fav_t;

static struct {
    maku_fav_t e[MAKU_FAV_MAX];      /* oldest first */
    uint8_t n;
    uint8_t liked;                   /* the world now has been liked */
} maku_fav __attribute__((section(".pool")));

/* REC's LED in MAKU: lit only while "LIKED n/16" (or "LIKED, NOT SAVED") is up */
static int maku_fav_lit(void) { return maku_fav.liked && ui.msg_t && !memcmp(ui.msg, "LIKED", 5); }

static int maku_fav_store(void);     /* project.c: the ring to flash (0 = written) */
static void maku_fav_tidy(void);     /* project.c: the other copy erased ahead of the next REC */

/* the share of the worlds (percent) drawn from n likes */
static uint32_t maku_fav_rate(uint32_t n)
{
    return n * MAKU_FAV_STEP > MAKU_FAV_CAP ? MAKU_FAV_CAP : n * MAKU_FAV_STEP;
}

/* a like this firmware can play: every voice known, every value in range */
static int maku_fav_valid(const maku_fav_t *e)
{
    uint32_t k;
    for (k = 0; k < 3u; k++)
        if (maku_voice_row(1u + k, e->v[k]) == 0xFFu)
            return 0;
    if (e->root > 11u || e->scale > (uint32_t)TP[P_SCALE].max || e->bpm < (uint32_t)GP[G_BPM].min ||
        e->bpm > (uint32_t)GP[G_BPM].max)
        return 0;
    for (k = 0; k < 12u; k++)
        if (e->m[k] > 127u)
            return 0;
    for (k = 0; k < 2u; k++)
        if (e->it[k] > (uint32_t)TP[P_ITYPE].max || e->ia[k] > 127u || e->ib[k] > 127u || e->ic[k] > 127u)
            return 0;
    return e->pm < 16u && (e->pm & 3u) < PM_N && (e->pm >> 2) < PM_N;
}

/* the world now as a like; 0 if there is none to keep (no voices yet) */
static int maku_fav_capture(maku_fav_t *e)
{
    uint32_t k;
    memset(e, 0, sizeof *e);
    if (!maku.on)
        return 0;
    e->seed = maku.seed;
    for (k = 0; k < 3u; k++) {
        uint32_t r = maku.voice[1u + k];
        if (r >= MAKU_V[1u + k].n)
            return 0;
        e->v[k] = MAKU_V[1u + k].v[r].id;
    }
    e->root = (uint8_t)(trk[MAKU_DRA].p[P_ROOT] % 12);
    e->scale = (uint8_t)trk[MAKU_DRA].p[P_SCALE];
    e->bpm = (uint8_t)clamp(song.g[G_BPM], GP[G_BPM].min, GP[G_BPM].max);
    for (k = 0; k < 12u; k++)
        e->m[k] = maku.m[k / 3u][k % 3u];
    e->m[0] = maku.dens;                               /* (the kick's DENSITY: CC20 sets it without the macro) */
    for (k = 0; k < 2u; k++) {
        const track_t *t = &trk[MAKU_DRA + k];
        e->it[k] = (uint8_t)t->p[P_ITYPE];
        e->ia[k] = (uint8_t)t->p[P_IA];
        e->ib[k] = (uint8_t)t->p[P_IB];
        e->ic[k] = (uint8_t)t->p[P_IC];
    }
    e->pm = (uint8_t)(maku.pm[MAKU_DRA] | maku.pm[MAKU_DRB] << 2);
    return maku_fav_valid(e);
}

/* one like more, at the newest end; a full ring lets its oldest go */
static void maku_fav_push(const maku_fav_t *e)
{
    uint32_t i;
    if (maku_fav.n >= MAKU_FAV_MAX) {
        for (i = 1; i < MAKU_FAV_MAX; i++)
            maku_fav.e[i - 1u] = maku_fav.e[i];
        maku_fav.n = MAKU_FAV_MAX - 1u;
    }
    maku_fav.e[maku_fav.n++] = *e;
}

/* the world of like e (after maku_setup, as maku_world): its voices, INSERTs, key, tempo and macros; `seed` names it */
static void maku_fav_apply(const maku_fav_t *e, uint32_t seed)
{
    uint32_t i, j;
    maku.seed = seed;
    maku.world_n++;
    for (i = 0; i < 3u; i++)
        maku_voice_apply(1u + i, maku_voice_row(1u + i, e->v[i]));
    trk[MAKU_DRA].p[P_LRATE] = 14;                     /* ~0.1 Hz (maku_world) */
    maku.pm[MAKU_DRA] = (uint8_t)(e->pm & 3u);           /* (before the macros: they are bounded by them) */
    maku.pm[MAKU_DRB] = (uint8_t)(e->pm >> 2);
    for (i = 0; i < 2u; i++) {
        track_t *t = &trk[MAKU_DRA + i];
        t->p[P_ITYPE] = e->it[i];
        t->p[P_IA] = e->ia[i];
        t->p[P_IB] = e->ib[i];
        t->p[P_IC] = e->ic[i];
    }
    trk[MAKU_DRA].p[P_ROOT] = e->root;
    trk[MAKU_DRA].p[P_SCALE] = e->scale;
    maku_follow();
    maku.root = (uint8_t)trk[MAKU_DRA].p[P_ROOT];
    maku.scale = (uint8_t)trk[MAKU_DRA].p[P_SCALE];
    song.g[G_BPM] = e->bpm;
    for (i = 0; i < 4u; i++)
        for (j = 0; j < 3u; j++)
            maku_macro_set(i, j, e->m[3u * i + j]);
}

/* part k (0..2: the floor, the haze, the voice) of like e drawn anew by the catalog's weights, under maku_world's rules
 * (the floor or the haze held; no second PHYS bowed sound), another row than it had if there is one */
static void maku_fav_revoice(maku_fav_t *e, uint32_t k)
{
    uint32_t t, r = 0, row[3], arts = ARTS_ALL, pb = 0;
    for (t = 0; t < 3u; t++)
        row[t] = maku_voice_row(1u + t, e->v[t]);
    for (t = 0; t < 3u; t++)
        if (t != k) {
            const maku_voice_t *v = &MAKU_V[1u + t].v[row[t]];
            pb |= v->art == ART_BOW && v->eng == ENGI_PHYS;
        }
    if (k < 2u && MAKU_V[2u - k].v[row[1u - k]].art < ART_SWEL)   /* (the other of the floor and the haze is struck) */
        arts = ARTS_HELD;
    for (t = 0; t < 8u && (r = maku_voice_pick(1u + k, arts, pb)) == row[k]; t++)
        ;
    e->v[k] = MAKU_V[1u + k].v[r].id;
}

/* each part of like e drawn anew with MAKU_MUT_P %; bit i of the result: part i changed (0..2 the voices, 3 ROOT,
 * 4 SCALE, 5..16 the macros, 17 the pitch levels: both drawn as a new world's) */
static uint32_t maku_fav_mutate(maku_fav_t *e)
{
    uint32_t k, t, v, did = 0;
    for (k = 0; k < MAKU_FAV_NEL; k++) {
        uint32_t old;
        if (rng() % 100u >= MAKU_MUT_P)
            continue;
        if (k < 3u) {
            old = e->v[k];
            maku_fav_revoice(e, k);
            did |= (uint32_t)(e->v[k] != old) << k;
        } else if (k == 3u) {
            e->root = (uint8_t)((e->root + 1u + rng() % 11u) % 12u);
            did |= 1u << k;
        } else if (k == 4u) {
            for (t = 0, v = e->scale; t < 8u && v == e->scale; t++)
                v = MAKU_CALM[rng() % sizeof MAKU_CALM];
            did |= (uint32_t)(v != e->scale) << k;
            e->scale = (uint8_t)v;
        } else if (k == 17u) {
            uint8_t a, b;
            for (t = 0, v = e->pm; t < 8u && v == e->pm; t++) {
                maku_pm_draw(&a, &b);
                v = a | (uint32_t)b << 2;
            }
            did |= (uint32_t)(v != e->pm) << k;
            e->pm = (uint8_t)v;
        } else {
            uint32_t m = k - 5u;
            for (t = 0, v = e->m[m]; t < 8u && v == e->m[m]; t++)
                v = maku_macro_draw(m / 3u, m % 3u);
            did |= (uint32_t)(v != e->m[m]) << k;
            e->m[m] = (uint8_t)v;
        }
    }
    return did;
}

/* a like by weight: the newest n, the oldest 1 */
static uint32_t maku_fav_pick(void)
{
    uint32_t n = maku_fav.n, x = rng() % (n * (n + 1u) / 2u), i;
    for (i = 0; i < n; i++) {
        if (x < i + 1u)
            return i;
        x -= i + 1u;
    }
    return n - 1u;
}

/* a new world from `seed` (after maku_setup): with likes kept, one of them (a little changed) as often as maku_fav_rate
 * says, else maku_world. The choice and the changes draw from a stream of their own, so the new world of a seed is the
 * same one with likes or without, and with none nothing else happens. 1: the world came from a like */
static uint32_t maku_world_pref(uint32_t seed)
{
    if (maku_fav.n) {
        uint32_t k;
        rng_state = maku_hash(seed ^ 0x454B494Cu) | 1u;   /* ("LIKE") */
        for (k = 0; k < 4u; k++)
            (void)rng();
        if (rng() % 100u < maku_fav_rate(maku_fav.n)) {
            maku_fav_t e = maku_fav.e[maku_fav_pick()];
            maku_fav_mutate(&e);
            maku_fav_apply(&e, seed);
            maku_fav.liked = 0;
            return 1;
        }
    }
    maku_world(seed);
    maku_fav.liked = 0;
    return 0;
}

/* the ring as stored: magic, version, count, entry size, MAKU_FAV_MAX entries (little-endian), FNV-1a of all before it */
static uint32_t maku_fav_sum(const uint8_t *b, uint32_t n)
{
    uint32_t h = 2166136261u;
    while (n--) {
        h ^= *b++;
        h *= 16777619u;
    }
    return h;
}
static void maku_put32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}
static uint32_t maku_get32(const uint8_t *b) { return b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static void maku_fav_pack(uint8_t *b)
{
    uint32_t i, k;
    memset(b, 0, MAKU_FAV_BYTES);
    maku_put32(b, MAKU_FAV_MAGIC);
    b[4] = MAKU_FAV_VER;
    b[5] = maku_fav.n;
    b[6] = MAKU_FAV_ESZ;
    for (i = 0; i < maku_fav.n; i++) {
        const maku_fav_t *e = &maku_fav.e[i];
        uint8_t *o = b + MAKU_FAV_HEAD + i * MAKU_FAV_ESZ;
        maku_put32(o, e->seed);
        for (k = 0; k < 3u; k++)
            o[4 + k] = e->v[k];
        o[7] = e->root;
        o[8] = e->scale;
        o[9] = e->bpm;
        memcpy(o + 10, e->m, 12);
        for (k = 0; k < 2u; k++) {
            o[22 + k] = e->it[k];
            o[24 + k] = e->ia[k];
            o[26 + k] = e->ib[k];
            o[28 + k] = e->ic[k];
        }
        o[30] = e->pm;
    }
    maku_put32(b + MAKU_FAV_BYTES - 4u, maku_fav_sum(b, MAKU_FAV_BYTES - 4u));
}
/* the ring from a stored blob of n bytes (n < 0: none). Anything that is not this format (length, magic, version,
 * checksum) starts it empty; a like this firmware cannot play is left out, the others kept in order. Returns the likes */
static uint32_t maku_fav_unpack(const uint8_t *b, int n)
{
    uint32_t i, k, cnt;
    maku_fav.n = 0;
    maku_fav.liked = 0;
    if (n != (int)MAKU_FAV_BYTES || maku_get32(b) != MAKU_FAV_MAGIC || b[4] != MAKU_FAV_VER || b[6] != MAKU_FAV_ESZ ||
        b[5] > MAKU_FAV_MAX || maku_get32(b + MAKU_FAV_BYTES - 4u) != maku_fav_sum(b, MAKU_FAV_BYTES - 4u))
        return 0;
    cnt = b[5];
    for (i = 0; i < cnt; i++) {
        const uint8_t *o = b + MAKU_FAV_HEAD + i * MAKU_FAV_ESZ;
        maku_fav_t *e = &maku_fav.e[maku_fav.n];
        e->seed = maku_get32(o);
        for (k = 0; k < 3u; k++)
            e->v[k] = o[4 + k];
        e->root = o[7];
        e->scale = o[8];
        e->bpm = o[9];
        memcpy(e->m, o + 10, 12);
        for (k = 0; k < 2u; k++) {
            e->it[k] = o[22 + k];
            e->ia[k] = o[24 + k];
            e->ib[k] = o[26 + k];
            e->ic[k] = o[28 + k];
        }
        e->pm = o[30];
        if (maku_fav_valid(e))
            maku_fav.n++;
    }
    return maku_fav.n;
}

/* REC tapped: the world now is a like, and the ring goes to flash */
static void maku_fav_like(void)
{
    maku_fav_t e;
    char b[8], *p = b;
    if (!maku_fav_capture(&e)) {
        ui_message("NOTHING TO LIKE");
        return;
    }
    maku_fav_push(&e);
    maku_fav.liked = 1;
    if (maku_fav.n >= 10u)                             /* "LIKED 3/16" */
        *p++ = (char)('0' + maku_fav.n / 10u);
    *p++ = (char)('0' + maku_fav.n % 10u);
    *p++ = '/';
    *p++ = (char)('0' + MAKU_FAV_MAX / 10u % 10u);
    *p++ = (char)('0' + MAKU_FAV_MAX % 10u);
    *p = 0;
    if (maku_fav_store())
        ui_message("LIKED, NOT SAVED");
    else
        ui_say("LIKED ", b);
}
/* REC held: every like forgotten (in flash too) */
static void maku_fav_clear(void)
{
    maku_fav.n = 0;
    maku_fav.liked = 0;
    ui_message(maku_fav_store() ? "CLEARED, NOT SAVED" : "LIKES CLEARED");
}
