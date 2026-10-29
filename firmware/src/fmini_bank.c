/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Guilherme Ruppert (FMini-1 fork) */
/* FMini-1 preset bank (1.14.0): 256 slots. Slots 0..99 start as the factory patches, 100..255 as INIT (one sawtooth
 * on oscillator 1, no filter envelope, no effects). A slot that was saved holds its own record (fmini_rec.c); a slot
 * never saved reads its factory record. The records live in 16 storage objects (OBJ_PRESET0..15), 16 slots each; each
 * object starts with a stamp, and a new firmware with another factory bank clears them at boot (install = factory).
 * The name and number of every slot are the only copy in RAM (fb_name); the record is read when a slot is chosen. */

#define FB_NSLOT 256u
#define FB_PER_OBJ 16u
#define FB_SLOT 128u                          /* one record: name 14 + patch 34 + extra 30 + preset 20 bytes, room to grow */
#define FB_STAMP 0x31564246u                  /* "FBV1": the factory bank this build writes */
#define FB_PAYLOAD (4u + FB_PER_OBJ * FB_SLOT)
#define FB_INIT_SLOT FMINI_NFACTORY           /* the first INIT slot */

static char fb_name[FB_NSLOT][13];            /* RAM: the list's names (upper case, 0-terminated) */
static uint8_t fb_obj_buf[FB_PAYLOAD] __attribute__((aligned(4), section(".pool")));   /* (main loop only) */

/* the INIT patch: a sawtooth on oscillator 1 alone (FG_ bytes, eng_fmini.c) */
static void fb_init_patch(uint8_t *p)
{
    uint32_t i;
    for (i = 0; i < FG_SIZE; i++)
        p[i] = 0;
    p[FG_R1] = 3; p[FG_R2] = 3; p[FG_R3] = 3;   /* 8' */
    p[FG_W1] = 2; p[FG_W2] = 2; p[FG_W3] = 2;   /* SAW */
    p[FG_F2] = 64; p[FG_F3] = 64;               /* 0 detune */
    p[FG_L1] = 127;                             /* oscillator 1 at full, 2 and 3 and the noise off */
    p[FG_SW1] = FS_O1;
    p[FG_TUNE] = 64;
}

/* the record of slot k as the factory has it (k < 100: the bank; else INIT), into rec (FB_SLOT bytes); its length */
static uint32_t fb_factory_rec(uint32_t k, uint8_t *rec)
{
    uint8_t patch[FG_SIZE], par[18];
    fmini_x_t x;
    const char *name = k < FMINI_NFACTORY ? FMINI_PRESETS[k].name : "INIT";
    uint32_t n = 0, i, nl = 0;
    while (nl < 12u && name[nl])
        nl++;
    if (k < FMINI_NFACTORY) {
        memcpy(patch, FMINI_FACTORY[k], FG_SIZE);
        x = FMINI_X[k];
    } else {                                    /* INIT: no arpeggiator, LFO, effects, modulation; the bank's level and octave 0 */
        fb_init_patch(patch);
        memset(&x, 0, sizeof x);
        x.level = FMINI_X[0].level;
    }
    /* PARAM: e[8] (signed), env[4] (ATK DEC SUS REL), fenv, mono, fx[4] (stored +1: 1 = no send) */
    for (i = 0; i < 8; i++)
        par[i] = (uint8_t)(k < FMINI_NFACTORY ? FMINI_PRESETS[k].e[i] : (i == 0 ? 127 : 0));
    for (i = 0; i < 4; i++)
        par[8 + i] = (uint8_t)(k < FMINI_NFACTORY ? FMINI_PRESETS[k].env[i] : (i == 2 ? 127 : 0));
    par[12] = (uint8_t)(k < FMINI_NFACTORY ? FMINI_PRESETS[k].fenv : 0);
    par[13] = (uint8_t)(k < FMINI_NFACTORY ? FMINI_PRESETS[k].mono : 0);
    for (i = 0; i < 4; i++)
        par[14 + i] = (uint8_t)(k < FMINI_NFACTORY ? FMINI_PRESETS[k].fx[i] : 1);
    n = fr_put(rec, FB_SLOT, n, FR_NAME, (const uint8_t *)name, nl);
    n = fr_put(rec, FB_SLOT, n, FR_PATCH, patch, FG_SIZE);
    n = fr_put(rec, FB_SLOT, n, FR_EXTRA, (const uint8_t *)&x, sizeof x);
    n = fr_put(rec, FB_SLOT, n, FR_PARAM, par, sizeof par);
    return n;
}

/* the name of slot k into fb_name (the list's copy) */
static void fb_set_name(uint32_t k, const uint8_t *name, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < 12u && i < len; i++)
        fb_name[k][i] = (char)name[i];
    fb_name[k][i < 12u ? i : 12u] = 0;
}

/* the RAM list at boot: every slot's name, from its record (saved, or the factory's) */
static void fb_names_init(void)
{
    uint32_t k;
    uint8_t rec[FB_SLOT];
    for (k = 0; k < FB_NSLOT; k++) {
        uint32_t n = fb_factory_rec(k, rec);
        const uint8_t *d = 0;
        uint32_t l = 0;
        if (fr_find(rec, n, FR_NAME, &d, &l))
            fb_set_name(k, d, l);
    }
}
