/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Guilherme Ruppert (FMini-1 fork) */
#ifndef FMINI_BANK_C
#define FMINI_BANK_C
/* FMini-1 preset bank (1.14.0): 200 slots. Slots 0..99 start as the factory patches, 100..199 as INIT (one sawtooth
 * on oscillator 1, no filter envelope, no effects). A slot that was saved holds its own record (fmini_rec.c); a slot
 * never saved reads its factory record. The records live in 13 storage objects (OBJ_PRESET0..12), 16 slots each; each
 * object starts with a stamp, and a new firmware with another factory bank clears them at boot (install = factory).
 * The name and number of every slot are the only copy in RAM (fb_name); the record is read when a slot is chosen. */

#define FB_NSLOT 200u
#define FB_NOBJ ((FB_NSLOT + FB_PER_OBJ - 1u) / FB_PER_OBJ)   /* 13 objects: the last one holds 8 slots */
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

/* the record's length: its blocks up to the first empty tag (the slot is zero beyond it) */
static uint32_t fb_len(const uint8_t *rec)
{
    uint32_t i = 0;
    while (i + 2u <= FB_SLOT && rec[i] != 0 && i + 2u + rec[i + 1u] <= FB_SLOT)
        i += 2u + rec[i + 1u];
    return i;
}

/* slot j's record from fb_obj_buf (an object read): its length, 0 when the slot was never saved or is damaged */
static uint32_t fb_slot_from_buf(uint32_t j, uint8_t *rec)
{
    const uint8_t *src = fb_obj_buf + 4u + j * FB_SLOT;
    uint32_t n;
    if (src[0] == 0)
        return 0;
    n = fb_len(src);
    if (n == 0 || !fr_valid(src, n))
        return 0;
    memcpy(rec, src, n);
    return n;
}

#if FELUCCA_FLASH
static int fb_obj_read(uint32_t o);
static int fb_obj_clear(uint32_t o);
#endif
/* slot k's record into rec: the saved one, else its factory record; its length */
static uint32_t fb_read(uint32_t k, uint8_t *rec)
{
#if FELUCCA_FLASH
    if (k < FB_NSLOT && fb_obj_read(k / FB_PER_OBJ)) {
        uint32_t n = fb_slot_from_buf(k % FB_PER_OBJ, rec);
        if (n)
            return n;
    }
#endif
    return fb_factory_rec(k, rec);
}

#if FELUCCA_FLASH
/* save slot k (its record rec, n bytes) into its object: the other slots of the object are kept. 0 = written */
static int fb_write(uint32_t k, const uint8_t *rec, uint32_t n)
{
    uint32_t o = k / FB_PER_OBJ, j = k % FB_PER_OBJ, i;
    uint8_t *dst;
    if (k >= FB_NSLOT || n == 0u || n > FB_SLOT || !fr_valid(rec, n))
        return -1;
    if (!fb_obj_read(o))
        fb_obj_clear(o);                        /* no bank here yet: a bank of stamp and empty slots */
    dst = fb_obj_buf + 4u + j * FB_SLOT;
    for (i = 0; i < FB_SLOT; i++)
        dst[i] = i < n ? rec[i] : 0;
    if (st_save(OBJ_PRESET0 + o, fb_obj_buf, FB_PAYLOAD) != 0)
        return -1;
    {
        const uint8_t *d = 0;
        uint32_t l = 0;
        if (fr_find(rec, n, FR_NAME, &d, &l))
            fb_set_name(k, d, l);
    }
    return 0;
}
#endif

#if FELUCCA_FLASH
/* object o into fb_obj_buf; 1 when it was read and its stamp is this bank's (an empty bank is a stamp only) */
static int fb_obj_read(uint32_t o)
{
    uint32_t stamp;
    if (!flash_ok || st_load(OBJ_PRESET0 + o, fb_obj_buf, FB_PAYLOAD) != (int)FB_PAYLOAD)
        return 0;
    stamp = (uint32_t)fb_obj_buf[0] | (uint32_t)fb_obj_buf[1] << 8 | (uint32_t)fb_obj_buf[2] << 16 | (uint32_t)fb_obj_buf[3] << 24;
    return stamp == FB_STAMP;
}

/* a new object: the stamp and no saved slots (every slot is its factory record); its save result: 0 = written */
static int fb_obj_clear(uint32_t o)
{
    uint32_t i;
    for (i = 0; i < FB_PAYLOAD; i++)
        fb_obj_buf[i] = 0;
    fb_obj_buf[0] = (uint8_t)FB_STAMP;
    fb_obj_buf[1] = (uint8_t)(FB_STAMP >> 8);
    fb_obj_buf[2] = (uint8_t)(FB_STAMP >> 16);
    fb_obj_buf[3] = (uint8_t)(FB_STAMP >> 24);
    return flash_ok ? st_save(OBJ_PRESET0 + o, fb_obj_buf, FB_PAYLOAD) : -1;
}

/* the bank at boot (after persist_boot: flash_ok is known): an object without this bank's stamp (another firmware's
 * bank, or none) is cleared, so every slot reads its factory record. Once per boot: 16 object reads, and the names. */
static void fb_boot(void)
{
    uint32_t o, j, k;
    uint8_t rec[FB_SLOT];
    for (o = 0; o < FB_NOBJ; o++) {
        if (!fb_obj_read(o))
            fb_obj_clear(o);                    /* (a failed write leaves the factory reads: nothing saved) */
        for (j = 0; j < FB_PER_OBJ; j++) {
            k = o * FB_PER_OBJ + j;
            if (fb_slot_from_buf(j, rec)) {
                const uint8_t *d = 0;
                uint32_t l = 0;
                if (fr_find(rec, fb_len(rec), FR_NAME, &d, &l))
                    fb_set_name(k, d, l);
            } else {
                uint32_t n = fb_factory_rec(k, rec);
                const uint8_t *d = 0;
                uint32_t l = 0;
                if (fr_find(rec, n, FR_NAME, &d, &l))
                    fb_set_name(k, d, l);
            }
        }
    }
}
#endif

/* the patch of slot s (any of the 256) into dst: its record's, the factory's if the record is damaged */
static void fmini_slot_patch(uint32_t s, uint8_t *dst)
{
    uint8_t rec[FB_SLOT];
    uint32_t n = fb_read(s, rec), l = 0;
    const uint8_t *d = 0;
    if (fr_find(rec, n, FR_PATCH, &d, &l) && l == FG_SIZE)
        memcpy(dst, d, FG_SIZE);
    else
        memcpy(dst, FMINI_FACTORY[s < FMINI_NFACTORY ? s : 0], FG_SIZE);
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

#endif /* FMINI_BANK_C */
