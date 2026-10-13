/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* I2S output (ALNK0 -> external codec) and the
 * audio ISR: per half buffer, blocks of CTL samples: mix_block (fx.c: events ->
 * each synth part -> dist -> level / pan -> sends -> buses -> master) -> USB audio (uac_tap) -> the metronome's
 * click (click.c, the DAC only) -> 24-bit stereo. */
/* registers: hal/fm1_audio.h */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define DAC_TICKS 544u            /* TIMER4 ticks per I2S frame (24 MHz / 44,117.6 Hz) */
#define CPU_AVG (4096u / HALF_FRAMES)   /* halves the load meter averages: ~93 ms whatever the half */
#define OUT_SHIFT 7               /* Q15 -> 24-bit, -6 dBFS ceiling (M0f ran clean at -18 dBFS) */

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));

/* diagnostics, kept across resets and UBOOT entry: read with `fm1t memr` */
#define DBG_MAGIC 0x44424731u                       /* "DBG1" */
struct felucca_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots;
    uint32_t stage, page, home;           /* where the main loop is (breadcrumbs) */
    uint32_t prev_stage, prev_page, prev_home, prev_rst, prev_frames;   /* as found at boot */
} felucca_dbg __attribute__((section(".noinit")));
static volatile uint32_t audio_halves, audio_max_us;
static volatile uint32_t t5_nested_ticks;              /* TIMER4 ticks TIMER5 spent nested in this ISR (main.c) */
static uint32_t audio_cpu_rem;                         /* keep the fractional IIR step: no low-load bias */
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;

/* MANNER:fade-in MANNER:erase-dip (docs/UI_RULES.md 10)
 * SOFT: one gain on what the DAC gets (not USB audio: uac_tap has taken its copy), moved a little each block, on a
 * squared curve (a fade-in starts gently). Unity costs one compare per block. Three users:
 *  - power-on (main.c): soft_fade_in, the first second swells from silence, so a MASTER knob left up does not
 *    shout out of the small speaker the moment the plug goes in (the world is already playing under it);
 *  - a flash erase (storage_hw.c st_erase): soft_down_wait fades out and waits until the DAC has played the silence,
 *    the erase then stops the audio for its ~40 ms with nothing cut, and soft_up brings it back: a short dip
 *    instead of a click at both edges. (The erase ahead of time, storage.c st_tidy, makes most saves need none.)
 * Q24: SOFT_ONE is unity. soft_g is the audio ISR's, soft_tgt / soft_step the main loop's */
#define SOFT_ONE (1u << 24)
#define SOFT_IN_MS 800u           /* power-on swell */
#define SOFT_OUT_MS 10u           /* before an erase: out ... */
#define SOFT_UP_MS 25u            /* ... and back in */
static volatile uint32_t soft_g = SOFT_ONE, soft_tgt = SOFT_ONE, soft_step = SOFT_ONE;
static volatile uint8_t audio_live;                    /* audio_init ran: the ISR renders (a wait on it would end) */
static void soft_set(uint32_t tgt, uint32_t ms)        /* ms: the whole 0..unity sweep */
{
    uint32_t n = (uint32_t)FS * ms / 1000u;
    soft_step = n ? SOFT_ONE / n + 1u : SOFT_ONE;
    soft_tgt = tgt;
}
static void soft_fade_in(void)                         /* before audio_init */
{
    soft_g = 0;
    soft_set(SOFT_ONE, SOFT_IN_MS);
}
static void soft_down_wait(void)                       /* returns when the DAC plays silence (or after 60 ms) */
{
    uint32_t t = fm1_ms, h;
    soft_set(0, SOFT_OUT_MS);
    while (soft_g != 0 && fm1_ms - t < 40u)
        ;
    h = audio_halves;                                  /* (the halves already rendered still play: 2 more) */
    while (audio_halves - h < 3u && fm1_ms - t < 60u)
        ;
}
static void soft_up(void) { soft_set(SOFT_ONE, SOFT_UP_MS); }
static inline int32_t soft_q(uint32_t g) { uint32_t u = g >> 12; return (int32_t)((u * u) >> 12); }   /* 0..4096 */
static __attribute__((noinline)) void soft_apply(int32_t *out, uint32_t n)   /* audio ISR, per block */
{
    uint32_t g0 = soft_g, tgt = soft_tgt, g1, st = soft_step * n, i;
    int32_t q0, dq;
    if (g0 == tgt && g0 == SOFT_ONE)
        return;
    g1 = g0 < tgt ? (tgt - g0 < st ? tgt : g0 + st) : (g0 - tgt < st ? tgt : g0 - st);
    q0 = soft_q(g0);
    dq = ((soft_q(g1) - q0) * 256) / (int32_t)n;      /* Q8 per sample */
    for (i = 0; i < n; i++) {
        int32_t m = q0 + ((dq * (int32_t)i) >> 8);
        out[2u * i] = (out[2u * i] * m) >> 12;
        out[2u * i + 1u] = (out[2u * i + 1u] * m) >> 12;
    }
    soft_g = g1;
}

static void audio_block(int32_t *out, uint32_t n)       /* mix (fx.c), then Q15 -> 24 bit */
{
    uint32_t i;
    mix_block(out, n);
#if FELUCCA_UAC
    uac_tap(out, n);                                    /* the USB audio input: the same master output */
#endif
    if (fx_usb_fixed)                                   /* USB LEVEL FIXED: USB took the full level, the DAC */
        usb_fixed_dac(out, n);                          /* (speaker, headphones) gets MASTER's (fx.c) */
    soft_apply(out, n);                                 /* the power-on swell, the dip around a flash erase */
    for (i = 1; i < n; i += 2u)                         /* the HOME scope: the music, before the click */
        scope_buf[scope_w++ & (SCOPE_N - 1u)] = (int16_t)out[2u * i];
    click_render(out, n);                               /* the metronome (click.c): the DAC only, not USB */
    for (i = 0; i < n; i++) {
        out[2u * i] *= 1 << OUT_SHIFT;
        out[2u * i + 1u] *= 1 << OUT_SHIFT;
    }
}

/* Overload: two halves in a row above 85 % (the render plus TIMER5 nested in it), fade one voice over the
 * next block, and one more each half while it lasts. Keep each part's
 * lowest POLY note and MONO / LEGATO / UNISON lead, as the shared budget does. */
static volatile uint8_t shed_req;
static uint8_t shed_over;                              /* bit k: the half k halves ago was over 85 % */
#define SHED_TICKS ((HALF_FRAMES * 1000000u / FS) * 85u / 100u * FM1_TICKS_PER_US)   /* 85 % of a half */
static uint32_t shed_count;

/* end of a half: all = its ticks, TIMER5 nested in it included -> the render alone in us. The deadline sees
 * both; shed after 2 overloaded halves in a row (out of line: keeps the ISR's render loops as they were) */
static __attribute__((noinline)) uint32_t shed_check(uint32_t all)
{
    shed_over = (uint8_t)(shed_over << 1 | (all > SHED_TICKS));
    if ((shed_over & 3u) == 3u)
        shed_req = 1;
    return (all - t5_nested_ticks) / FM1_TICKS_PER_US;
}

static void shed_voice(void)
{
    uint32_t p, i;
    voice_t *best = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &trk[p].v[i];
            if (v->active && !v->gate && v->stage != 4u && (!best || v->env < best->env))
                best = v;
        }
    if (best) {
        voice_kill(best);
        shed_count++;
        return;
    }
    for (p = 0; p < NPART; p++) {
        const track_t *t = &trk[p];
        uint32_t mode = trk_vmode(t), low = mode == V_POLY ? lowest_held(t) : 0u;
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &trk[p].v[i];
            if (v->active && v->gate && v->stage != 4u &&
                ((mode == V_POLY && i != low) || (mode != V_POLY && i > 0u))) {
                if (!best || v->age < best->age)
                    best = v;
            }
        }
    }
    if (best) {
        voice_kill(best);
        shed_count++;
    }
}

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    uint32_t t0;
    felucca_dbg.in_audio = 1;                   /* first: TIMER5 nests from here on (main.c) */
    p = fm1_audio_pending();
    t0 = fm1_ticks();
    t5_nested_ticks = 0;
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, us;
        int32_t *o = &abuf[half * HALF_WORDS];
        if (shed_req) {
            shed_req = 0;
            shed_voice();
        }
#if FELUCCA_UAC
        uac_render_start();
#endif
        for (b = 0; b < HALF_FRAMES; b += CTL) {
#ifdef FM1_INPUT_LAT
            kb_out_tick = t0 + (HALF_FRAMES + b) * DAC_TICKS;   /* when this block plays (seq.c kb_lat) */
#endif
            audio_block(o + 2u * b, CTL);
        }
        fm1_audio_ack_half();
        audio_halves++;
        us = shed_check(fm1_ticks() - t0);
        if (us > audio_max_us)
            audio_max_us = us;
        {
            uint32_t load = song.cpu_q8 * (CPU_AVG - 1u) + (us * 256u) / (HALF_FRAMES * 1000000u / FS) + audio_cpu_rem;
            song.cpu_q8 = load / CPU_AVG;
            audio_cpu_rem = load % CPU_AVG;
        }
        if (fm1_audio_free_half() != half)
            felucca_dbg.late++;                         /* the DMA moved on while we rendered */
        felucca_dbg.halves++;
        felucca_dbg.last_us = us;
        if (us > felucca_dbg.max_us)
            felucca_dbg.max_us = us;
        felucca_dbg.cpu_q8 = song.cpu_q8;
    }
    felucca_dbg.in_audio = 0;
}
extern void isr_alnk0(void);

static void audio_init(void)                   /* codec and ALNK0 bring-up (fm1_audio.h) */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
    audio_live = 1;
}
