/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Editor protocol: the FMINI patch (FMini-1; EDITOR_PROTOCOL.md "FMINI patches", cmds 82, 83). The patch is the 32
 * bytes of eng_fmini.c (FG_*: ranges, waves, tuning, mixer, switches, TUNE, MOD MIX, KBD, GLIDE), every byte 0..127.
 * Targets: 0 a track's own patch (index 0..3), 2 a factory patch (0..FMINI_NFACTORY-1, read only), 3 a user preset's
 * patch (0..UP_SLOTS-1, read only; the preset must be an FMINI one: rc 2 otherwise; a preset stored without its own patch
 * gives the factory patch its SLOT names, as loading it does).
 * GET: target, index -> target, index, rc, [FG_SIZE bytes]. For a track the reply ends with its SLOT (P_E7, 0..127:
 * 0..99 = F1..F100, 100 = OWN) after the patch, which is what the panel shows as the patch number.
 * PUT: target 0, index, FG_SIZE bytes -> target, index, rc. The patch becomes the track's own (SLOT OWN, or the
 * factory patch it equals), as an edit on the panel does. rc: 0 ok, 1 arguments, 2 not an FMINI track.
 * Extras (INFO 48 01 01): the reply of GET target 0 ends with the keys' octave + 4 (1..7) after the SLOT, and PUT takes that
 * byte after the 32 (optional; 1..7, else ignored). 84 FMINI_PRESET: slot -> slot, rc, then 14 x v14: what a user preset
 * kept besides the track's values (FMini-1: the tempo, the octave, the FX pop-ups' globals), as stored: BPM (40..240),
 * OCT (octave + 4: 1..7), the FX page's G_RSIZE G_DRREV G_CRATE G_DRLVL + 1 (1..128), then G_RTYPE G_RDAMP G_DTIME G_DFDBK
 * G_ENGGO G_CDEPTH G_INFO G_MIDI + 1; 0 = the preset has none (older). rc 2: not an FMINI preset. */
enum { ED_FMINI_GET = 82, ED_FMINI_PUT, ED_FMINI_PRESET, ED_FMINI_REC, ED_FMINI_RPUT, ED_FMINI_RESTORE };   /* 85..87: the backup (protocol 4) */
enum { ED_FMINI_TRACK = 0, ED_FMINI_FACTORY = 2, ED_FMINI_USER = 3 };


/* 85 FMINI_REC: slot (two 7-bit bytes) -> slot, rc, length (two 7-bit bytes), then the record: each byte as two nibbles,
 * low first. The saved record, or the factory's: the whole preset */
static void ed_fmini_rec_get(const uint8_t *a, uint32_t n)
{
    uint8_t rec[FB_SLOT];
    uint32_t k = n == 2u ? (uint32_t)a[0] | (uint32_t)a[1] << 7 : FB_NSLOT, len = 0, i;
    if (k < FB_NSLOT)
        len = fb_read(k, rec);
    ed_b(n ? a[0] : 127u); ed_b(n > 1u ? a[1] : 127u);
    ed_b(len ? 0u : 1u);
    ed_b(len & 127u); ed_b((len >> 7) & 127u);
    for (i = 0; i < len; i++) {
        ed_b(rec[i] & 15u);
        ed_b(rec[i] >> 4);
    }
}

/* 86 FMINI_RPUT: slot, length (as above), the record's nibbles -> slot, rc. Only while the device has said YES (state 2).
 * rc: 0 written, 1 arguments, 2 not accepted on the device, 3 not a preset record, 4 the flash refused */
static void ed_fmini_rec_put(const uint8_t *a, uint32_t n)
{
    uint8_t rec[FB_SLOT];
    uint32_t k = 0, len = 0, i, rc = 1;
    if (n >= 4u) {
        k = (uint32_t)a[0] | (uint32_t)a[1] << 7;
        len = (uint32_t)a[2] | (uint32_t)a[3] << 7;
        if (k < FB_NSLOT && len > 0u && len <= FB_SLOT && n == 4u + 2u * len)
            rc = 0;
    }
    if (!rc && fb_rs_state() != FB_RS_OK)
        rc = 2;
    if (!rc)
        for (i = 0; i < len && !rc; i++) {
            if (a[4u + 2u * i] > 15u || a[5u + 2u * i] > 15u)
                rc = 1;
            else
                rec[i] = (uint8_t)(a[4u + 2u * i] | a[5u + 2u * i] << 4);
        }
    if (!rc && !fr_valid(rec, len))
        rc = 3;
    if (!rc && fb_write(k, rec, len) != 0)
        rc = 4;
    if (!rc)
        fb_rs_touch();
    ed_b(n >= 2u ? a[0] : 127u); ed_b(n >= 2u ? a[1] : 127u); ed_b(rc);
}

/* 87 FMINI_RESTORE: op -> op, rc, state. 0 ASK (the device shows its question; nothing if one is up or accepted), 1 STATUS,
 * 2 END (the restore is over: IDLE). State: 0 idle, 1 the question is up, 2 accepted (write now), 3 refused or timed out */
static void ed_fmini_restore(const uint8_t *a, uint32_t n)
{
    uint32_t rc = 0;
    if (n != 1u)
        rc = 1;
    else if (a[0] == 0u) {
        uint8_t st = fb_rs_state();
        if (st != FB_RS_ASK && st != FB_RS_OK) {
            fb_rs = FB_RS_ASK;
            fb_rs_yes = 0;
            fb_rs_touch();
            ui.force = 1;
        }
    } else if (a[0] == 2u) {
        fb_rs = FB_RS_IDLE;
        ui.force = 1;
    } else if (a[0] != 1u)
        rc = 1;
    ed_b(n ? a[0] : 127u); ed_b(rc); ed_b(fb_rs_state());
}

static int ed_fmini_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    if (cmd == ED_FMINI_REC) { ed_fmini_rec_get(a, n); return 1; }
    if (cmd == ED_FMINI_RPUT) { ed_fmini_rec_put(a, n); return 1; }
    if (cmd == ED_FMINI_RESTORE) { ed_fmini_restore(a, n); return 1; }
    uint32_t i, rc = 0;
    uint8_t pk[FG_SIZE];
    if (cmd == ED_FMINI_PRESET) {                           /* slot -> slot, rc, 14 x v14 */
        const up_rec_t *r = n == 1u && a[0] < UP_SLOTS ? up_rec(a[0]) : 0;
        rc = !r ? 1u : !up_used(a[0]) || r->engine != ENGI_FMINI ? 2u : 0u;
        ed_b(n ? a[0] : 127u); ed_b(rc);
        if (!rc) {
            uint32_t ok = r->ver >= 4u;
            ed_v(ok && r->np <= UP_BPM_AT ? r->packed[UP_BPM_AT] : 0);
            ed_v(ok && r->np <= UP_OCT_AT ? r->packed[UP_OCT_AT] : 0);
            for (i = 0; i < 4u; i++)
                ed_v(ok && r->np <= UP_FX_AT ? r->packed[UP_FX_AT + i] : 0);
            for (i = 0; i < 8u; i++)
                ed_v(ok && r->np <= UP_FX2_AT ? r->packed[UP_FX2_AT + i] : 0);
        }
        return 1;
    }
    if (cmd != ED_FMINI_GET && cmd != ED_FMINI_PUT)
        return 0;
    if (cmd == ED_FMINI_GET)
        rc = n != 2u ? 1u : 0u;
    else
        rc = (n != 2u + FG_SIZE && n != 3u + FG_SIZE) || a[0] != ED_FMINI_TRACK ? 1u : 0u;
    if (!rc && a[0] != ED_FMINI_TRACK && a[0] != ED_FMINI_FACTORY && a[0] != ED_FMINI_USER)
        rc = 1;
    if (!rc && a[1] >= (a[0] == ED_FMINI_FACTORY ? FMINI_NFACTORY : a[0] == ED_FMINI_USER ? (uint32_t)UP_SLOTS : (uint32_t)NTRK))
        rc = 1;
    if (!rc && a[0] == ED_FMINI_USER && (!up_used(a[1]) || up_rec(a[1])->engine != ENGI_FMINI))
        rc = 2;
    if (!rc && a[0] == ED_FMINI_TRACK && trk[a[1]].eng_req != ENGI_FMINI)
        rc = 2;
    if (!rc && cmd == ED_FMINI_PUT) {
        fmini_set_patch(a[1], a + 2);                     /* (sanitized into its ranges) */
        fmini_adopt(a[1]);
        if (n == 3u + FG_SIZE && a[2 + FG_SIZE] >= 1u && a[2 + FG_SIZE] <= 7u)
            song.octave = (int8_t)((int)a[2 + FG_SIZE] - 4);
        ui.force = 1;
    }
    if (!rc && cmd == ED_FMINI_GET) {
        if (a[0] == ED_FMINI_USER) {
            uint8_t w[FM6_PACKED];
            if (!upf_get(a[1], w)) {
                memcpy(pk, w, FG_SIZE);
            } else {
                int16_t v[P_COUNT];
                up_values(up_rec(a[1]), v);
                memcpy(pk, FMINI_FACTORY[v[P_E7] >= 0 && v[P_E7] < (int32_t)FMINI_NFACTORY ? v[P_E7] : 0], FG_SIZE);
            }
        } else {
            memcpy(pk, a[0] == ED_FMINI_FACTORY ? FMINI_FACTORY[a[1]] : fmini_patch[a[1]], FG_SIZE);
        }
    }
    ed_b(n ? a[0] : 127u); ed_b(n > 1u ? a[1] : 127u); ed_b(rc);
    if (!rc && cmd == ED_FMINI_GET) {
        for (i = 0; i < FG_SIZE; i++)
            ed_b(pk[i]);
        if (a[0] == ED_FMINI_TRACK) {
            {   /* the SLOT (0..199 factory-or-INIT, 200 = OWN): two 7-bit bytes, low first (protocol 3) */
                uint32_t sl = (uint32_t)clamp(trk[a[1]].p[P_E7], 0, 16383);
                ed_b(sl & 0x7Fu);
                ed_b((sl >> 7) & 0x7Fu);
            }
            ed_b((uint32_t)(clamp(song.octave, -3, 3) + 4));
        }
    }
    return 1;
}
