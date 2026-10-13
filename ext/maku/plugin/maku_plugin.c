/* SPDX-License-Identifier: GPL-3.0-only
 * MAKU inside Monomodule Euclid: the browser build of MAKU (web/emu/felucca_web.c, unchanged) compiled as a native
 * shared library, plus the calls the plugin needs that the web page does not: the host's MIDI clock, the key the
 * sequencer and MAKU share, the four parts' levels and macros set directly, NEW WORLD and LIKE as one call each, and
 * the world / the likes as bytes for the plugin's state.
 *
 * One library file is loaded once per plugin instance (a private copy of the file, so every instance has its own
 * firmware state: the firmware is written for one device and keeps everything in file-scope variables). Every call
 * is made from the plugin's audio thread, between two renders, which is where the firmware's own main loop runs in
 * the web build: nothing here needs a lock.
 *
 * MAKU: Copyright (C) 2026 okumurari (@okumurari2). Felucca: Copyright (C) 2026 Leo Kuroshita (@kurogedelic),
 * Hügelton Instruments. This file: Copyright (C) 2026 the Monomodule Euclid contributors. */
#include "../web/emu/felucca_web.c"

#define MK EXPORT

MK uint32_t mk_api_version(void) { return 1; }

/* clock: 1 = follow MIDI clock (the host's transport, through mk_realtime), 0 = MAKU's own tempo, running */
MK void mk_clock_mode(uint32_t ext)
{
    song.g[G_CLOCK] = ext ? 1 : 0;
    if (!ext)
        transport_req = 1;                 /* (events_block: the mode change stops it, then this starts it) */
}
MK uint32_t mk_clock_get(void) { return (uint32_t)song.g[G_CLOCK]; }
/* the audio: n frames (<= 1024) into web_out_l / web_out_r, counted (the clock's time stamps below) */
static uint64_t mk_frames;
MK void mk_render(uint32_t n)
{
    web_render(n);
    mk_frames += n > 1024u ? 1024u : n;
}
/* a realtime byte, as a USB-MIDI packet with CIN 0xF (usb.c midi_enqueue): 0xF8 clock, 0xFA start, 0xFB continue,
 * 0xFC stop. Its time stamp is the millisecond of the last frame rendered, not the device's clock (which runs up to an
 * I2S half ahead of what has been handed out): the pulses keep the host's spacing, and the tempo MAKU measures from
 * them stays steady */
MK uint32_t mk_realtime(uint32_t status)
{
    uint32_t at = mi_w % MQ;
    if (status < 0xF8u || status > 0xFFu || midi_in_overflow)
        return 0;
    if (mi_w - mi_r >= MQ) {
        midi_in_overflow = 1;
        return 0;
    }
    midi_in_q[at] = 0xFu | status << 8;
    midi_in_ms[at] = (uint32_t)(mk_frames * 1000u / FS);
    midi_in_source[at] = 1u;
    RING_PUBLISH();
    mi_w++;
    return 1;
}

/* the key: root 0..11 | scale << 8 (params.c N_SCALE order) */
MK uint32_t mk_key_get(void) { return (uint32_t)(trk[MAKU_DRA].p[P_ROOT] % 12) | (uint32_t)trk[MAKU_DRA].p[P_SCALE] << 8; }
MK void mk_key_set(uint32_t root, uint32_t scale)
{
    track_t *a = &trk[MAKU_DRA];
    a->p[P_ROOT] = (int16_t)(root % 12u);
    a->p[P_SCALE] = (int16_t)(scale > (uint32_t)TP[P_SCALE].max ? 2u : scale);
    maku_follow();
    maku.root = (uint8_t)a->p[P_ROOT];
    maku.scale = (uint8_t)a->p[P_SCALE];
}
/* a fifth up (dir > 0) or down the circle: a change of key signature, what plays keeps its pitch (maku.c maku_fifth) */
MK void mk_fifth(int32_t dir)
{
    maku_fifth(dir > 0 ? 1 : -1);
    ui_say(dir < 0 ? "FLAT: " : "SHARP: ", N_NOTE[(uint32_t)trk[MAKU_DRA].p[P_ROOT] % 12u]);
}

/* the four parts: 0 PULSE (the kick), 1 FLOOR (the drone), 2 HAZE (the shimmer), 3 VOICE (the arp) */
MK const char *mk_part_name(uint32_t i) { return MAKU_TRK_NAME[i % 4u]; }
MK const char *mk_macro_name(uint32_t i, uint32_t j) { return MAKU_MAC[i % 4u][j % 3u].name; }
MK const char *mk_voice_name(uint32_t i) { return web_voice_name(i); }
MK uint32_t mk_focus_get(void) { return song.sel; }
MK void mk_focus(uint32_t i)
{
    if (i < 4u)
        track_select(i);
}
MK uint32_t mk_level_get(uint32_t i) { return (uint32_t)trk[i % 4u].p[P_LEVEL]; }
/* a part's volume (KNOB 1..4), focusing it as the knob does */
MK void mk_level_set(uint32_t i, uint32_t v)
{
    if (i >= 4u)
        return;
    {
        uint8_t force = ui.force;
        track_select(i);
        ui.force = force;
    }
    trk[i].p[P_LEVEL] = (int16_t)(v > 127u ? 127u : v);
    ui.hot_col = (uint8_t)i;
    ui.hot_t = 40;
}
/* macro j (0 SELECT, 1 PRESETS, 2 ALGORITHM) of part i */
MK uint32_t mk_macro_get(uint32_t i, uint32_t j) { return i < 4u && j < 3u ? (i == 0u && j == 0u ? maku.dens : maku.m[i][j]) : 0u; }
MK void mk_macro_set(uint32_t i, uint32_t j, uint32_t v)
{
    char b[8];
    if (i >= 4u || j >= 3u)
        return;
    track_select(i);
    maku_macro_set(i, j, v > 127u ? 127u : v);
    fmt_int(b, maku.m[i][j]);
    ui_say(MAKU_MAC[i][j].name, b);
}

MK uint32_t mk_bpm(void) { return (uint32_t)song.g[G_BPM]; }
MK uint32_t mk_drift_on(void) { return maku.dr_tog; }
MK uint32_t mk_verbs_held(void) { return maku.hv; }
MK uint32_t mk_world_number(void) { return maku.world_n; }
MK uint32_t mk_seed(void) { return maku.seed; }

/* HOME held: a new world (from the likes, often, once there are some) */
MK void mk_new_world(void) { maku_world_new(); }
/* REC tapped: the world now is a like */
MK void mk_like(void) { maku_fav_like(); }
MK uint32_t mk_likes(void) { return maku_fav.n; }
MK uint32_t mk_liked(void) { return maku_fav.liked; }
MK void mk_likes_clear(void) { maku_fav_clear(); }

/* the likes ring as maku_fav.c stores it (MAKU_FAV_BYTES): into b when n is enough, the size returned */
MK uint32_t mk_likes_size(void) { return MAKU_FAV_BYTES; }
MK uint32_t mk_likes_pack(uint8_t *b, uint32_t n)
{
    if (n >= MAKU_FAV_BYTES)
        maku_fav_pack(b);
    return MAKU_FAV_BYTES;
}
MK uint32_t mk_likes_unpack(const uint8_t *b, uint32_t n) { return maku_fav_unpack(b, (int)n); }

/* the world now in a like's 32 bytes (maku_fav.c's entry layout); 0 = no world to keep yet */
#define MK_WORLD_BYTES 32u
MK uint32_t mk_world_pack(uint8_t *o)
{
    maku_fav_t e;
    uint32_t k;
    if (!maku_fav_capture(&e))
        return 0;
    memset(o, 0, MK_WORLD_BYTES);
    maku_put32(o, e.seed);
    for (k = 0; k < 3u; k++)
        o[4 + k] = e.v[k];
    o[7] = e.root;
    o[8] = e.scale;
    o[9] = e.bpm;
    memcpy(o + 10, e.m, 12);
    for (k = 0; k < 2u; k++) {
        o[22 + k] = e.it[k];
        o[24 + k] = e.ia[k];
        o[26 + k] = e.ib[k];
        o[28 + k] = e.ic[k];
    }
    o[30] = e.pm;
    return MK_WORLD_BYTES;
}
/* back to a world packed by mk_world_pack (a session reopened): 0 if it is not one this build can play */
MK uint32_t mk_world_apply(const uint8_t *o, uint32_t n)
{
    maku_fav_t e;
    uint32_t k;
    if (n < MK_WORLD_BYTES)
        return 0;
    memset(&e, 0, sizeof e);
    e.seed = maku_get32(o);
    for (k = 0; k < 3u; k++)
        e.v[k] = o[4 + k];
    e.root = o[7];
    e.scale = o[8];
    e.bpm = o[9];
    memcpy(e.m, o + 10, 12);
    for (k = 0; k < 2u; k++) {
        e.it[k] = o[22 + k];
        e.ia[k] = o[24 + k];
        e.ib[k] = o[26 + k];
        e.ic[k] = o[28 + k];
    }
    e.pm = o[30];
    if (!maku_fav_valid(&e))
        return 0;
    maku_setup();
    maku_fav_apply(&e, e.seed);
    maku_fav.liked = 0;
    perf_kill = 1;
    perf_latched = 0;
    ui.force = 1;
    return 1;
}
