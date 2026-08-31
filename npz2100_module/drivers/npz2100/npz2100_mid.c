/**
 * @file npz2100_mid.c
 * @brief nPZ2100 mid-level API implementation.
 *
 * See npz2100_mid.h for design rationale and layering diagram.
 *
 * Implementation notes
 * --------------------
 *
 * Shadow ↔ register address mapping
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Two static helper tables translate between a raw register address and the
 * corresponding uint8_t field inside npz2100_config_t.  This avoids a
 * large switch statement and keeps the walk O(n) where n is the number
 * of data bytes in the regmap stream, not the number of registers in the
 * device.
 *
 * Byte-stream parsing
 * ~~~~~~~~~~~~~~~~~~~~
 * The regmap is a flat `const uint8_t[]` — a concatenation of segments
 * shaped `[length][start_addr][data...]`. The static map_walk() function is
 * the single parsing primitive: it validates segment framing and invokes a
 * visitor callback once per (addr, value) pair in stream order. Apply, diff,
 * and validate are each just a different visitor over the same walk.
 *
 * Banked peripheral registers (0x20–0x2D)
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * The nPZ2100 exposes one window of peripheral registers at fixed addresses
 * 0x20–0x2D, selected by writing P_BANK (0x1F). P_BANK is just another
 * register address in the stream — writing it updates cfg->p_bank like any
 * other shadowed register, and subsequent banked addresses resolve against
 * whatever bank was last selected, exactly as on real hardware. No special
 * casing or segment lookahead is required.
 *
 * No dynamic memory, no floating point, no stdlib beyond string.h memset.
 *
 * @version 0.7
 * @date    2026-05-06
 * @author  Nanopower Semiconductor AS
 */

#include "npz2100_mid.h"
#include <string.h>   /* memset */

/* =========================================================================
 * Internal: shadow field lookup
 *
 * Maps a non-banked register address → pointer to the matching uint8_t field
 * in an npz2100_config_t.  Returns NULL for read-only or unrecognised addrs.
 * ======================================================================= */

#if NPZ2100_SHADOW_ENABLE
/* shadow_field, shadow_periph_field, and is_periph_addr are only
 * needed when shadow tracking is active. */

/**
 * @brief Look up the shadow byte for a non-banked, non-peripheral register.
 *
 * @param cfg   Config shadow.
 * @param addr  Register address.
 * @return Pointer to the shadow field, or NULL if not found / read-only.
 */
static uint8_t *shadow_field(npz2100_config_t *cfg, uint8_t addr)
{
    switch (addr) {
        /* IDLE_RST always reads 0x00; we track last intent for diffing */
        case NPZ2100_REG_IDLE_RST:   return &cfg->idle_rst;
        case NPZ2100_REG_P_BANK:     return &cfg->p_bank;
        case NPZ2100_REG_IOCFG1:     return &cfg->iocfg1;
        case NPZ2100_REG_IOCFG2:     return &cfg->iocfg2;
        case NPZ2100_REG_IOCFG3:     return &cfg->iocfg3;
        case NPZ2100_REG_IOCFG4:     return &cfg->iocfg4;
        case NPZ2100_REG_IOCFG5:     return &cfg->iocfg5;
        case NPZ2100_REG_SYSCFG1:    return &cfg->syscfg1;
        case NPZ2100_REG_SYSCFG2:    return &cfg->syscfg2;
        case NPZ2100_REG_TOUT_L:     return &cfg->tout_l;
        case NPZ2100_REG_TOUT_H:     return &cfg->tout_h;
        case NPZ2100_REG_GCT_MS:     return &cfg->gct_ms;
        case NPZ2100_REG_GCT_0:      return &cfg->gct_0;
        case NPZ2100_REG_GCT_1:      return &cfg->gct_1;
        case NPZ2100_REG_GCT_2:      return &cfg->gct_2;
        case NPZ2100_REG_GCT_3:      return &cfg->gct_3;
        case NPZ2100_REG_GCT_ALM_0:  return &cfg->gct_alm_0;
        case NPZ2100_REG_GCT_ALM_1:  return &cfg->gct_alm_1;
        case NPZ2100_REG_GCT_ALM_2:  return &cfg->gct_alm_2;
        case NPZ2100_REG_GCT_ALM_3:  return &cfg->gct_alm_3;
        case NPZ2100_REG_WDOG_L:     return &cfg->wdog_l;
        case NPZ2100_REG_WDOG_H:     return &cfg->wdog_h;
        case NPZ2100_REG_GTC_CFG:    return &cfg->gtc_cfg;
        case NPZ2100_REG_PA_CFG:     return &cfg->pa_cfg;
        case NPZ2100_REG_ADCCFG:     return &cfg->adccfg;
        case NPZ2100_REG_THROVA1:    return &cfg->throva1;
        case NPZ2100_REG_THRUNA1:    return &cfg->thruna1;
        case NPZ2100_REG_THROVA2:    return &cfg->throva2;
        case NPZ2100_REG_THRUNA2:    return &cfg->thruna2;
        case NPZ2100_REG_THROVA3:    return &cfg->throva3;
        case NPZ2100_REG_THRUNA3:    return &cfg->thruna3;
        case NPZ2100_REG_LOGCFG:     return &cfg->logcfg;
        case NPZ2100_REG_LOGSADDR:   return &cfg->logsaddr;
        case NPZ2100_REG_CNT_VAL_0:  return &cfg->cnt_val_0;
        case NPZ2100_REG_CNT_VAL_1:  return &cfg->cnt_val_1;
        case NPZ2100_REG_CNT_VAL_2:  return &cfg->cnt_val_2;
        case NPZ2100_REG_CNT_VAL_3:  return &cfg->cnt_val_3;
        case NPZ2100_REG_CNTCFG:     return &cfg->cntcfg;
        case NPZ2100_REG_CNT_TRIG_0: return &cfg->cnt_trig_0;
        case NPZ2100_REG_CNT_TRIG_1: return &cfg->cnt_trig_1;
        case NPZ2100_REG_CNT_TRIG_2: return &cfg->cnt_trig_2;
        case NPZ2100_REG_CNT_TRIG_3: return &cfg->cnt_trig_3;
        case NPZ2100_REG_SRAM_BANK:  return &cfg->sram_bank;
        default:                      return NULL;
    }
}

/**
 * @brief Look up the shadow byte for a banked peripheral register (0x20–0x2D).
 *
 * @param cfg   Config shadow.
 * @param slot  Peripheral slot 0–5.
 * @param addr  Banked register address (0x20–0x2D).
 * @return Pointer to shadow field, or NULL if addr out of banked range.
 */
static uint8_t *shadow_periph_field(npz2100_config_t *cfg,
                                    uint8_t           slot,
                                    uint8_t           addr)
{
    npz2100_periph_shadow_t *p = &cfg->periph[slot];
    switch (addr) {
        case NPZ2100_REG_CFGP:      return &p->cfgp;
        case NPZ2100_REG_IOP:       return &p->iop;
        case NPZ2100_REG_MODP:      return &p->modp;
        case NPZ2100_REG_PERP_L:    return &p->perp_l;
        case NPZ2100_REG_PERP_H:    return &p->perp_h;
        case NPZ2100_REG_NCMDP:     return &p->ncmdp;
        case NPZ2100_REG_ADDRP:     return &p->addrp;
        case NPZ2100_REG_RREGP:     return &p->rregp;
        case NPZ2100_REG_THROVP_L:  return &p->throvp_l;
        case NPZ2100_REG_THROVP_H:  return &p->throvp_h;
        case NPZ2100_REG_THRUNP_L:  return &p->thrunp_l;
        case NPZ2100_REG_THRUNP_H:  return &p->thrunp_h;
        case NPZ2100_REG_TWTP:      return &p->twtp;
        case NPZ2100_REG_TCFGP:     return &p->tcfgp;
        default:                     return NULL;
    }
}

/** True if addr falls in the banked peripheral window. */
static bool is_periph_addr(uint8_t addr)
{
    return (addr >= NPZ2100_REG_CFGP && addr <= NPZ2100_REG_TCFGP);
}

#endif /* NPZ2100_SHADOW_ENABLE */

/* =========================================================================
 * Internal: bank select helper
 * ======================================================================= */

/**
 * @brief Write P_BANK only if the active bank needs to change, and update
 *        the shadow's p_bank field to match.
 *
 * @param hal          HAL descriptor.
 * @param cfg          Shadow — p_bank field is updated on success.
 * @param target_slot  Desired slot (0–5).
 */
static npz2100_err_t ensure_bank(const npz2100_hal_t *hal,
                                  npz2100_config_t    *cfg,
                                  uint8_t              target_slot)
{
    if (cfg->p_bank == target_slot) {
        return NPZ2100_OK;  /* Already selected — no I²C transaction needed. */
    }
    npz2100_err_t err = npz2100_reg_write(hal, NPZ2100_REG_P_BANK,
                                           NPZ2100_P_BANK(target_slot));
    if (err == NPZ2100_OK) {
        cfg->p_bank = target_slot;
    }
    return err;
}

/* =========================================================================
 * Internal: regmap byte-stream walker
 *
 * Format: concatenation of segments shaped
 *   [length][start_addr][data_0]...[data_(length-2)]
 * where `length` includes start_addr, so data payload is (length - 1) bytes.
 *
 * The walker calls `visit(addr, value, user)` for every individual register
 * byte produced by every segment, in stream order. This single iteration
 * primitive backs npz2100_map_apply(), npz2100_map_diff_count(), and
 * npz2100_map_validate() — each just supplies a different visitor.
 * ======================================================================= */

/** Visitor callback signature for npz2100_map_walk(). */
typedef npz2100_err_t (*regmap_visit_fn)(uint8_t addr, uint8_t value, void *user);

/**
 * @brief Walk a regmap byte stream, calling @p visit for every register byte.
 *
 * Validates segment framing as it goes: if a segment's declared length would
 * read past the end of the buffer, parsing stops immediately with
 * NPZ2100_ERR_ARG (no partial segment is ever visited).
 *
 * A segment with length == 0 is invalid (there must be at least a start_addr
 * byte) and also yields NPZ2100_ERR_ARG.
 *
 * @param map      Pointer to the byte stream.
 * @param map_len  Total length of the stream.
 * @param visit    Callback invoked once per (addr, value) pair.
 * @param user     Opaque pointer forwarded to every visit call.
 * @return NPZ2100_OK if the entire stream was consumed without error, or
 *         whatever non-OK code the visitor returns (propagated immediately),
 *         or NPZ2100_ERR_ARG on malformed framing.
 */
static npz2100_err_t map_walk(const uint8_t  *map,
                               size_t          map_len,
                               regmap_visit_fn visit,
                               void           *user)
{
    size_t pos = 0u;

    while (pos < map_len) {
        /* Need at least 1 byte for `length`. */
        if (pos + 1u > map_len) {
            return NPZ2100_ERR_ARG;
        }
        uint8_t length = map[pos];

        /* A segment must contain at least start_addr (length >= 1). */
        if (length == 0u) {
            return NPZ2100_ERR_ARG;
        }

        /* Total bytes consumed by this segment: 1 (length field) + length. */
        if (pos + 1u + (size_t)length > map_len) {
            return NPZ2100_ERR_ARG;  /* Segment runs past end of buffer. */
        }

        uint8_t start_addr = map[pos + 1u];
        uint8_t data_len    = (uint8_t)(length - 1u);  /* bytes after start_addr */
        const uint8_t *data = &map[pos + 2u];

        for (uint8_t i = 0u; i < data_len; i++) {
            uint8_t addr = (uint8_t)(start_addr + i);
            npz2100_err_t err = visit(addr, data[i], user);
            if (err != NPZ2100_OK) {
                return err;
            }
        }

        pos += 1u + (size_t)length;
    }

    return NPZ2100_OK;
}

/* =========================================================================
 * Shadow initialisation
 * ======================================================================= */

npz2100_err_t npz2100_config_init_defaults(npz2100_config_t *cfg)
{
    if (cfg == NULL) {
        return NPZ2100_ERR_ARG;
    }

    /* Start from a clean slate. */
    memset(cfg, 0x00u, sizeof(*cfg));

    /*
     * Apply reset defaults from datasheet Table 3 and register reset fields.
     * Only non-zero reset values are set explicitly below; everything else
     * is already 0 from memset.
     */

    /* P_BANK: device powers up with bank 0 selected. */
    cfg->p_bank = 0x00u;

    /* IOCFG3: all INT pull-ups ON at ≈100 kΩ (reset = 0xFF). */
    cfg->iocfg3 = 0xFFu;

    /*
     * IOCFG5 reset: SPI_AUTO=1, I2C_PUP_AUTO=1, I2C_PUP_EN=1, PSW_SR=1.
     * Bits [3:0] = 0b1111 → 0x0F; bit[4]=1 (SPI_AUTO) → 0x1F.
     */
    cfg->iocfg5 = NPZ2100_IOCFG5_PSW_SR(1)
                | NPZ2100_IOCFG5_I2C_PUP_EN(1)
                | NPZ2100_IOCFG5_I2C_PUP_AUTO(1)
                | NPZ2100_IOCFG5_SPI_AUTO(1);

    /* TOUT: default 0xFFFF (max time-out). */
    cfg->tout_l = 0xFFu;
    cfg->tout_h = 0xFFu;

    /* PERP: reset default per-peripheral is 0x0002 (datasheet §2.2.23). */
    for (uint8_t i = 0u; i < 6u; i++) {
        cfg->periph[i].perp_l = 0x02u;
        cfg->periph[i].perp_h = 0x00u;
    }

    return NPZ2100_OK;
}

/* =========================================================================
 * Register-map operations
 * ======================================================================= */

/** Context passed to the apply visitor. */
typedef struct {
    const npz2100_hal_t *hal;
    npz2100_config_t    *cfg;
} apply_ctx_t;

#if NPZ2100_SHADOW_ENABLE

/**
 * @brief Visitor for npz2100_map_apply() — shadow mode.
 *
 * Diffs each register against the shadow and writes only when the value
 * has changed.  Skips unchanged registers entirely (zero I2C transactions).
 * P_BANK (0x1F) is handled like any other register: writing it updates
 * cfg->p_bank so subsequent banked addresses resolve to the correct slot.
 */
static npz2100_err_t apply_visitor(uint8_t addr, uint8_t value, void *user)
{
    apply_ctx_t *ctx = (apply_ctx_t *)user;
    npz2100_config_t *cfg = ctx->cfg;

    uint8_t *field = is_periph_addr(addr)
                    ? shadow_periph_field(cfg, cfg->p_bank, addr)
                    : shadow_field(cfg, addr);

    if (field == NULL) {
        return NPZ2100_OK;  /* Read-only / reserved / SRAM — skip silently. */
    }
    if (*field == value) {
        return NPZ2100_OK;  /* No change — zero I2C transactions. */
    }

    npz2100_err_t err = npz2100_reg_write(ctx->hal, addr, value);
    if (err != NPZ2100_OK) {
        return err;
    }
    *field = value;
    return NPZ2100_OK;
}

#else /* NPZ2100_SHADOW_ENABLE == 0 */

/**
 * @brief Visitor for npz2100_map_apply() — no-shadow mode.
 *
 * Writes every register unconditionally without any diff or shadow update.
 * P_BANK (0x1F) is still written — it must be sent to the device to
 * select the correct peripheral bank before banked register writes.
 */
static npz2100_err_t apply_visitor(uint8_t addr, uint8_t value, void *user)
{
    apply_ctx_t *ctx = (apply_ctx_t *)user;

    /* Skip read-only, reserved, and SRAM addresses. */
    if (addr == NPZ2100_REG_ID    ||
        addr == NPZ2100_REG_STA1  ||
        addr == NPZ2100_REG_STA2  ||
        addr == NPZ2100_REG_STA3  ||
        addr == NPZ2100_REG_VALP_L ||
        addr == NPZ2100_REG_VALP_H ||
        (addr >= NPZ2100_SRAM_WINDOW_BASE)) {
        return NPZ2100_OK;
    }

    return npz2100_reg_write(ctx->hal, addr, value);
}

#endif /* NPZ2100_SHADOW_ENABLE */

npz2100_err_t npz2100_map_apply(const npz2100_hal_t *hal,
                                 npz2100_config_t    *cfg,
                                 const uint8_t        *map,
                                 size_t                map_len)
{
    if (hal == NULL || map == NULL || map_len == 0u) {
        return NPZ2100_ERR_ARG;
    }
#if !NPZ2100_SHADOW_ENABLE
    (void)cfg;  /* cfg unused when shadow is disabled */
#else
    if (cfg == NULL) {
        return NPZ2100_ERR_ARG;
    }
#endif

    apply_ctx_t ctx = { .hal = hal, .cfg = cfg };
    return map_walk(map, map_len, apply_visitor, &ctx);
}

/* -------------------------------------------------------------------------*/

#if NPZ2100_SHADOW_ENABLE

npz2100_err_t npz2100_map_readback(const npz2100_hal_t *hal,
                                    npz2100_config_t    *cfg)
{
    if (hal == NULL || cfg == NULL) {
        return NPZ2100_ERR_ARG;
    }

    npz2100_err_t err;
    uint8_t       v;

    /* --- Non-banked registers ------------------------------------------- */
    /* Read in address order; burst where consecutive. */

#define RB(addr, field) \
    do { \
        err = npz2100_reg_read(hal, (addr), &v); \
        if (err != NPZ2100_OK) { return err; } \
        cfg->field = v; \
    } while (0)

    RB(NPZ2100_REG_IOCFG1,     iocfg1);
    RB(NPZ2100_REG_IOCFG2,     iocfg2);
    RB(NPZ2100_REG_IOCFG3,     iocfg3);
    RB(NPZ2100_REG_IOCFG4,     iocfg4);
    RB(NPZ2100_REG_IOCFG5,     iocfg5);
    RB(NPZ2100_REG_SYSCFG1,    syscfg1);
    RB(NPZ2100_REG_SYSCFG2,    syscfg2);

    /* TOUT: 2 consecutive bytes — use burst for efficiency. */
    {
        uint8_t buf[2];
        err = npz2100_reg_burst_read(hal, NPZ2100_REG_TOUT_L, buf, 2u);
        if (err != NPZ2100_OK) { return err; }
        cfg->tout_l = buf[0];
        cfg->tout_h = buf[1];
    }

    /* GCT: 5 consecutive bytes. */
    {
        uint8_t buf[5];
        err = npz2100_reg_burst_read(hal, NPZ2100_REG_GCT_MS, buf, 5u);
        if (err != NPZ2100_OK) { return err; }
        cfg->gct_ms = buf[0];
        cfg->gct_0  = buf[1];
        cfg->gct_1  = buf[2];
        cfg->gct_2  = buf[3];
        cfg->gct_3  = buf[4];
    }

    /* GCTALM: 4 consecutive bytes. */
    {
        uint8_t buf[4];
        err = npz2100_reg_burst_read(hal, NPZ2100_REG_GCT_ALM_0, buf, 4u);
        if (err != NPZ2100_OK) { return err; }
        cfg->gct_alm_0 = buf[0];
        cfg->gct_alm_1 = buf[1];
        cfg->gct_alm_2 = buf[2];
        cfg->gct_alm_3 = buf[3];
    }

    /* WDOG: 2 consecutive bytes. */
    {
        uint8_t buf[2];
        err = npz2100_reg_burst_read(hal, NPZ2100_REG_WDOG_L, buf, 2u);
        if (err != NPZ2100_OK) { return err; }
        cfg->wdog_l = buf[0];
        cfg->wdog_h = buf[1];
    }

    RB(NPZ2100_REG_GTC_CFG, gtc_cfg);
    RB(NPZ2100_REG_PA_CFG,  pa_cfg);

    /* --- Per-peripheral banks ------------------------------------------- */
    for (uint8_t slot = 0u; slot <= NPZ2100_P_BANK_MAX; slot++) {
        err = npz2100_reg_write(hal, NPZ2100_REG_P_BANK, NPZ2100_P_BANK(slot));
        if (err != NPZ2100_OK) { return err; }

        /* Banked regs 0x20–0x2D are 14 consecutive bytes. */
        uint8_t buf[14];
        err = npz2100_reg_burst_read(hal, NPZ2100_REG_CFGP, buf, 14u);
        if (err != NPZ2100_OK) { return err; }

        npz2100_periph_shadow_t *p = &cfg->periph[slot];
        p->cfgp     = buf[0];
        p->iop      = buf[1];
        p->modp     = buf[2];
        p->perp_l   = buf[3];
        p->perp_h   = buf[4];
        p->ncmdp    = buf[5];
        p->addrp    = buf[6];
        p->rregp    = buf[7];
        p->throvp_l = buf[8];
        p->throvp_h = buf[9];
        p->thrunp_l = buf[10];
        p->thrunp_h = buf[11];
        p->twtp     = buf[12];
        p->tcfgp    = buf[13];
    }

    /* Restore P_BANK to 0 after readback. */
    err = npz2100_reg_write(hal, NPZ2100_REG_P_BANK, 0x00u);
    if (err != NPZ2100_OK) { return err; }
    cfg->p_bank = 0x00u;

    /* --- ADC ------------------------------------------------------------ */
    RB(NPZ2100_REG_ADCCFG,  adccfg);
    RB(NPZ2100_REG_THROVA1, throva1);
    RB(NPZ2100_REG_THRUNA1, thruna1);
    RB(NPZ2100_REG_THROVA2, throva2);
    RB(NPZ2100_REG_THRUNA2, thruna2);
    RB(NPZ2100_REG_THROVA3, throva3);
    RB(NPZ2100_REG_THRUNA3, thruna3);

    /* --- Logging -------------------------------------------------------- */
    RB(NPZ2100_REG_LOGCFG,   logcfg);
    RB(NPZ2100_REG_LOGSADDR, logsaddr);

    /* --- Event counter -------------------------------------------------- */
    {
        uint8_t buf[4];
        err = npz2100_reg_burst_read(hal, NPZ2100_REG_CNT_VAL_0, buf, 4u);
        if (err != NPZ2100_OK) { return err; }
        cfg->cnt_val_0 = buf[0];
        cfg->cnt_val_1 = buf[1];
        cfg->cnt_val_2 = buf[2];
        cfg->cnt_val_3 = buf[3];
    }
    RB(NPZ2100_REG_CNTCFG, cntcfg);
    {
        uint8_t buf[4];
        err = npz2100_reg_burst_read(hal, NPZ2100_REG_CNT_TRIG_0, buf, 4u);
        if (err != NPZ2100_OK) { return err; }
        cfg->cnt_trig_0 = buf[0];
        cfg->cnt_trig_1 = buf[1];
        cfg->cnt_trig_2 = buf[2];
        cfg->cnt_trig_3 = buf[3];
    }

    RB(NPZ2100_REG_SRAM_BANK, sram_bank);

#undef RB

    return NPZ2100_OK;
}

#endif /* NPZ2100_SHADOW_ENABLE */

/* -------------------------------------------------------------------------*/

#if NPZ2100_SHADOW_ENABLE

/** Context passed to the diff-count visitor. */
typedef struct {
    npz2100_config_t *cfg;      /* Non-const working copy for field lookup. */
    uint8_t            p_bank;   /* Local bank tracker — does not touch cfg. */
    uint8_t            count;
} diff_ctx_t;

/**
 * @brief Visitor for npz2100_map_diff_count(): counts differing registers
 *        without writing to the device or modifying the caller's shadow.
 *
 * Tracks P_BANK locally (not via cfg->p_bank) so the function remains
 * non-destructive with respect to the caller-supplied shadow.
 */
static npz2100_err_t diff_visitor(uint8_t addr, uint8_t value, void *user)
{
    diff_ctx_t *ctx = (diff_ctx_t *)user;

    if (addr == NPZ2100_REG_P_BANK) {
        ctx->p_bank = value & NPZ2100_P_BANK_MSK;
        /* P_BANK itself still counts as a register that may differ. */
    }

    const uint8_t *field = is_periph_addr(addr)
                          ? shadow_periph_field(ctx->cfg, ctx->p_bank, addr)
                          : shadow_field(ctx->cfg, addr);

    if (field != NULL && *field != value) {
        ctx->count++;
    }
    return NPZ2100_OK;
}

uint8_t npz2100_map_diff_count(const npz2100_config_t *cfg,
                                const uint8_t          *map,
                                size_t                  map_len)
{
    if (cfg == NULL || map == NULL || map_len == 0u) {
        return 0u;
    }

    /*
     * We need a non-const copy of cfg to use shadow_field() — cast is safe
     * because the diff visitor never writes through the pointer.
     */
    npz2100_config_t *cfg_nc = (npz2100_config_t *)(uintptr_t)cfg;

    diff_ctx_t ctx = { .cfg = cfg_nc, .p_bank = cfg->p_bank, .count = 0u };

    /* Malformed streams simply stop early; partial count is still useful. */
    (void)map_walk(map, map_len, diff_visitor, &ctx);

    return ctx.count;
}

#endif /* NPZ2100_SHADOW_ENABLE */

/* -------------------------------------------------------------------------*/

/**
 * @brief Visitor for npz2100_map_validate(): no-op, just lets map_walk()
 *        do the framing checks.
 */
static npz2100_err_t validate_visitor(uint8_t addr, uint8_t value, void *user)
{
    (void)addr;
    (void)value;
    (void)user;
    return NPZ2100_OK;
}

npz2100_err_t npz2100_map_validate(const uint8_t *map, size_t map_len)
{
    if (map == NULL || map_len == 0u) {
        return NPZ2100_ERR_ARG;
    }
    return map_walk(map, map_len, validate_visitor, NULL);
}

/* -------------------------------------------------------------------------*/

npz2100_err_t npz2100_shadow_write_reg(const npz2100_hal_t *hal,
                                        npz2100_config_t    *cfg,
                                        uint8_t              addr,
                                        uint8_t              value)
{
    if (hal == NULL) {
        return NPZ2100_ERR_ARG;
    }

    npz2100_err_t err = npz2100_reg_write(hal, addr, value);
    if (err != NPZ2100_OK) {
        return err;
    }

#if NPZ2100_SHADOW_ENABLE
    /* Update shadow — best-effort; ignore unknown addresses. */
    if (cfg != NULL) {
        uint8_t *field = shadow_field(cfg, addr);
        if (field != NULL) {
            *field = value;
        }
    }
#else
    (void)cfg;
#endif /* NPZ2100_SHADOW_ENABLE */

    return NPZ2100_OK;
}

/* =========================================================================
 * Typed configuration helpers — shadow writes only
 * ======================================================================= */

npz2100_err_t npz2100_sys_set(npz2100_config_t       *cfg,
                               const npz2100_sys_cfg_t *sys)
{
    if (cfg == NULL || sys == NULL) {
        return NPZ2100_ERR_ARG;
    }
    if (sys->tout_value < NPZ2100_TOUT_MIN_SAFE) {
        return NPZ2100_ERR_ARG;
    }

    /* SYSCFG1: peripheral wake-up enables + mode. */
    cfg->syscfg1 = (uint8_t)(sys->wup_periph_mask & 0x3Fu)
                 | NPZ2100_SYSCFG1_WUPMOD(sys->wup_any ? 0u : 1u);

    /* SYSCFG2: ADC wake-ups + clock + TOUT extension. */
    cfg->syscfg2 = (uint8_t)(sys->wup_adc1 ? NPZ2100_SYSCFG2_WUP_ADC1_MSK : 0u)
                 | (uint8_t)(sys->wup_adc2 ? NPZ2100_SYSCFG2_WUP_ADC2_MSK : 0u)
                 | (uint8_t)(sys->wup_adc3 ? NPZ2100_SYSCFG2_WUP_ADC3_MSK : 0u)
                 | NPZ2100_SYSCFG2_TOUT_EXT(sys->tout_ext ? 1u : 0u)
                 | NPZ2100_SYSCFG2_SCLK_SEL(sys->clk_xo  ? 1u : 0u);

    /* TOUT: 16-bit little-endian. */
    cfg->tout_l = (uint8_t)(sys->tout_value & 0xFFu);
    cfg->tout_h = (uint8_t)((sys->tout_value >> 8u) & 0xFFu);

    return NPZ2100_OK;
}

/* -------------------------------------------------------------------------*/

npz2100_err_t npz2100_timer_set(npz2100_config_t         *cfg,
                                 const npz2100_timer_cfg_t *timer)
{
    if (cfg == NULL || timer == NULL) {
        return NPZ2100_ERR_ARG;
    }

    /* GCT: fractional + 32-bit seconds. */
    cfg->gct_ms = timer->gct_ms & NPZ2100_GCT_MS_MSK;
    cfg->gct_0  = (uint8_t)( timer->gct_seconds        & 0xFFu);
    cfg->gct_1  = (uint8_t)((timer->gct_seconds >>  8u) & 0xFFu);
    cfg->gct_2  = (uint8_t)((timer->gct_seconds >> 16u) & 0xFFu);
    cfg->gct_3  = (uint8_t)((timer->gct_seconds >> 24u) & 0xFFu);

    /* GCTALM: 32-bit alarm in seconds. */
    cfg->gct_alm_0 = (uint8_t)( timer->alarm_seconds        & 0xFFu);
    cfg->gct_alm_1 = (uint8_t)((timer->alarm_seconds >>  8u) & 0xFFu);
    cfg->gct_alm_2 = (uint8_t)((timer->alarm_seconds >> 16u) & 0xFFu);
    cfg->gct_alm_3 = (uint8_t)((timer->alarm_seconds >> 24u) & 0xFFu);

    /* WDOG: 16-bit value in 2-second units. */
    cfg->wdog_l = (uint8_t)( timer->wdog_value       & 0xFFu);
    cfg->wdog_h = (uint8_t)((timer->wdog_value >> 8u) & 0xFFu);

    /* GTC_CFG: alarm enable + watchdog enable. */
    cfg->gtc_cfg = NPZ2100_GTC_CFG_GTC_AEN(timer->alarm_enable  ? 1u : 0u)
                 | NPZ2100_GTC_CFG_WDOGEN(timer->wdog_enable    ? 1u : 0u);

    return NPZ2100_OK;
}

/* -------------------------------------------------------------------------*/

npz2100_err_t npz2100_pa_set(npz2100_config_t      *cfg,
                               const npz2100_pa_cfg_t *pa)
{
    if (cfg == NULL || pa == NULL) {
        return NPZ2100_ERR_ARG;
    }
    if (pa->src > NPZ2100_PA_SRC_ADC3) {
        return NPZ2100_ERR_ARG;
    }

    cfg->pa_cfg = NPZ2100_PA_CFG_PA_EN(pa->enable   ? 1u : 0u)
                | NPZ2100_PA_CFG_PA_NOWUP(pa->no_wup ? 1u : 0u)
                | NPZ2100_PA_CFG_PA_SRC(pa->src);

    return NPZ2100_OK;
}

/* -------------------------------------------------------------------------*/

npz2100_err_t npz2100_io_set(npz2100_config_t *cfg,
                               uint8_t           iocfg1,
                               uint8_t           iocfg2,
                               uint8_t           iocfg3,
                               uint8_t           iocfg4,
                               uint8_t           iocfg5)
{
    if (cfg == NULL) {
        return NPZ2100_ERR_ARG;
    }

    cfg->iocfg1 = iocfg1;
    cfg->iocfg2 = iocfg2;
    cfg->iocfg3 = iocfg3;
    cfg->iocfg4 = iocfg4;
    cfg->iocfg5 = iocfg5;

    return NPZ2100_OK;
}

/* -------------------------------------------------------------------------*/

npz2100_err_t npz2100_periph_set(npz2100_config_t           *cfg,
                                   uint8_t                     slot,
                                   const npz2100_periph_cfg_t *pcfg)
{
    if (cfg == NULL || pcfg == NULL || slot > NPZ2100_P_BANK_MAX) {
        return NPZ2100_ERR_ARG;
    }
    if (pcfg->period == 0u) {
        /* Datasheet §2.2.23: period = 0 causes undefined behaviour. */
        return NPZ2100_ERR_ARG;
    }

    npz2100_periph_shadow_t *p = &cfg->periph[slot];

    /* CFGP */
    p->cfgp = NPZ2100_CFGP_PWMOD(pcfg->pwmod)
            | NPZ2100_CFGP_TMOD(pcfg->tmod)
            | NPZ2100_CFGP_PLOG(pcfg->log_en  ? 1u : 0u)
            | NPZ2100_CFGP_PLOGTS(pcfg->log_ts ? 1u : 0u)
            | NPZ2100_CFGP_PLOGF(pcfg->log_freq);

    /* IOP */
    p->iop = NPZ2100_IOP_SELPSW(pcfg->psw_pin)
           | NPZ2100_IOP_SELINT(pcfg->int_pin)
           | NPZ2100_IOP_SELCSN(pcfg->csn_pin)
           | NPZ2100_IOP_PAMOD(pcfg->pamod);

    /* MODP */
    p->modp = NPZ2100_MODP_CMOD(pcfg->inv_cmp   ? 1u : 0u)
            | NPZ2100_MODP_DTYPE(pcfg->dtype)
            | NPZ2100_MODP_SEQRW(pcfg->seq_rw    ? 1u : 0u)
            | NPZ2100_MODP_WUNAK(pcfg->wunak      ? 1u : 0u)
            | NPZ2100_MODP_SWPRREG(pcfg->swap_bytes ? 1u : 0u)
            | NPZ2100_MODP_SPIMOD(pcfg->spi_mode);

    /* PERP: 16-bit polling period. */
    p->perp_l = (uint8_t)( pcfg->period       & 0xFFu);
    p->perp_h = (uint8_t)((pcfg->period >> 8u) & 0xFFu);

    /* NCMDP */
    p->ncmdp = pcfg->ncmd;

    /* ADDRP: I²C address (7-bit) or SPI read byte count. */
    p->addrp = pcfg->i2c_addr;   /* Tool sets this to byte count if use_spi. */

    /* RREGP */
    p->rregp = pcfg->read_reg;

    /* THROVP: 16-bit over-threshold. */
    p->throvp_l = (uint8_t)( pcfg->threshold_ov       & 0xFFu);
    p->throvp_h = (uint8_t)((pcfg->threshold_ov >> 8u) & 0xFFu);

    /* THRUNP: 16-bit under-threshold. */
    p->thrunp_l = (uint8_t)( pcfg->threshold_un       & 0xFFu);
    p->thrunp_h = (uint8_t)((pcfg->threshold_un >> 8u) & 0xFFu);

    /* TWTP */
    p->twtp = pcfg->twt;

    /* TCFGP */
    p->tcfgp = NPZ2100_TCFGP_TWT_EN(pcfg->twt_en     ? 1u : 0u)
             | NPZ2100_TCFGP_TWT_EXT(pcfg->twt_ext    ? 1u : 0u)
             | NPZ2100_TCFGP_TINIT_EN(pcfg->tinit_en  ? 1u : 0u)
             | NPZ2100_TCFGP_TINIT_EXT(pcfg->tinit_ext ? 1u : 0u)
             | NPZ2100_TCFGP_I2CRET(pcfg->i2c_retries)
             | NPZ2100_TCFGP_I2CRO(pcfg->i2c_read_only ? 1u : 0u)
             | NPZ2100_TCFGP_SPIEN(pcfg->use_spi       ? 1u : 0u);

    return NPZ2100_OK;
}

/* -------------------------------------------------------------------------*/

npz2100_err_t npz2100_periph_apply(const npz2100_hal_t *hal,
                                    npz2100_config_t    *cfg,
                                    uint8_t              slot)
{
    if (hal == NULL || cfg == NULL || slot > NPZ2100_P_BANK_MAX) {
        return NPZ2100_ERR_ARG;
    }

    npz2100_err_t err;

    /*
     * The banked registers 0x20–0x2D are 14 consecutive addresses.
     * Build a small lookup table of (addr, shadow-field-offset) pairs and
     * write each one unconditionally — npz2100_periph_set() has already
     * placed the *desired* values into the shadow, so this call's purpose
     * is to push that intent to the device. Use npz2100_map_apply() (with
     * a tool-generated stream) or npz2100_map_readback() beforehand if you
     * need true diff-against-device-state semantics for this peripheral.
     */
    const struct { uint8_t addr; uint8_t offset; } fields[] = {
        { NPZ2100_REG_CFGP,      offsetof(npz2100_periph_shadow_t, cfgp)     },
        { NPZ2100_REG_IOP,       offsetof(npz2100_periph_shadow_t, iop)      },
        { NPZ2100_REG_MODP,      offsetof(npz2100_periph_shadow_t, modp)     },
        { NPZ2100_REG_PERP_L,    offsetof(npz2100_periph_shadow_t, perp_l)   },
        { NPZ2100_REG_PERP_H,    offsetof(npz2100_periph_shadow_t, perp_h)   },
        { NPZ2100_REG_NCMDP,     offsetof(npz2100_periph_shadow_t, ncmdp)    },
        { NPZ2100_REG_ADDRP,     offsetof(npz2100_periph_shadow_t, addrp)    },
        { NPZ2100_REG_RREGP,     offsetof(npz2100_periph_shadow_t, rregp)    },
        { NPZ2100_REG_THROVP_L,  offsetof(npz2100_periph_shadow_t, throvp_l) },
        { NPZ2100_REG_THROVP_H,  offsetof(npz2100_periph_shadow_t, throvp_h) },
        { NPZ2100_REG_THRUNP_L,  offsetof(npz2100_periph_shadow_t, thrunp_l) },
        { NPZ2100_REG_THRUNP_H,  offsetof(npz2100_periph_shadow_t, thrunp_h) },
        { NPZ2100_REG_TWTP,      offsetof(npz2100_periph_shadow_t, twtp)     },
        { NPZ2100_REG_TCFGP,     offsetof(npz2100_periph_shadow_t, tcfgp)    },
    };

    const uint8_t n = (uint8_t)(sizeof(fields) / sizeof(fields[0]));
    const uint8_t *base = (const uint8_t *)&cfg->periph[slot];

    /* Select the bank once, up front. */
    err = ensure_bank(hal, cfg, slot);
    if (err != NPZ2100_OK) { return err; }

    for (uint8_t i = 0u; i < n; i++) {
        uint8_t desired = base[fields[i].offset];
        err = npz2100_reg_write(hal, fields[i].addr, desired);
        if (err != NPZ2100_OK) { return err; }
    }

    /* Restore P_BANK to slot 0 after apply. */
    err = ensure_bank(hal, cfg, 0x00u);
    if (err != NPZ2100_OK) { return err; }

    return NPZ2100_OK;
}

/* -------------------------------------------------------------------------*/

npz2100_err_t npz2100_adc_set(npz2100_config_t       *cfg,
                                const npz2100_adc_cfg_t *adc)
{
    if (cfg == NULL || adc == NULL) {
        return NPZ2100_ERR_ARG;
    }

    cfg->adccfg = NPZ2100_ADCCFG_ADC1_EN(adc->en_ch1    ? 1u : 0u)
                | NPZ2100_ADCCFG_ADC2_EN(adc->en_ch2    ? 1u : 0u)
                | NPZ2100_ADCCFG_ADC3_EN(adc->en_ch3    ? 1u : 0u)
                | NPZ2100_ADCCFG_ADC_CLK_SEL(adc->clk_sel)
                | NPZ2100_ADCCFG_ADC1_PSYNC(adc->psync_ch1 ? 1u : 0u)
                | NPZ2100_ADCCFG_ADC2_PSYNC(adc->psync_ch2 ? 1u : 0u);

    cfg->throva1 = adc->throva[0];
    cfg->thruna1 = adc->thruna[0];
    cfg->throva2 = adc->throva[1];
    cfg->thruna2 = adc->thruna[1];
    cfg->throva3 = adc->throva[2];
    cfg->thruna3 = adc->thruna[2];

    return NPZ2100_OK;
}

/* -------------------------------------------------------------------------*/

npz2100_err_t npz2100_log_set(npz2100_config_t       *cfg,
                                const npz2100_log_cfg_t *log)
{
    if (cfg == NULL || log == NULL) {
        return NPZ2100_ERR_ARG;
    }

    cfg->logcfg  = NPZ2100_LOGCFG_LOG_EN(log->enable  ? 1u : 0u)
                 | NPZ2100_LOGCFG_LOG_ROT(log->rotate  ? 1u : 0u);
    cfg->logsaddr = log->start_addr;

    return NPZ2100_OK;
}

/* -------------------------------------------------------------------------*/

npz2100_err_t npz2100_counter_set(npz2100_config_t           *cfg,
                                   const npz2100_counter_cfg_t *cnt)
{
    if (cfg == NULL || cnt == NULL) {
        return NPZ2100_ERR_ARG;
    }
    if (cnt->src > NPZ2100_CNT_SRC_SW_LP4) {
        return NPZ2100_ERR_ARG;
    }

    cfg->cntcfg    = NPZ2100_CNTCFG_CNT_EN(cnt->enable ? 1u : 0u)
                   | NPZ2100_CNTCFG_CNT_SRC(cnt->src);
    cfg->cnt_trig_0 = (uint8_t)( cnt->trigger        & 0xFFu);
    cfg->cnt_trig_1 = (uint8_t)((cnt->trigger >>  8u) & 0xFFu);
    cfg->cnt_trig_2 = (uint8_t)((cnt->trigger >> 16u) & 0xFFu);
    cfg->cnt_trig_3 = (uint8_t)((cnt->trigger >> 24u) & 0xFFu);

    return NPZ2100_OK;
}

/* =========================================================================
 * Device control helpers
 * ======================================================================= */

npz2100_err_t npz2100_probe_ll(const npz2100_hal_t *hal)
{
    if (hal == NULL) {
        return NPZ2100_ERR_ARG;
    }

    uint8_t id;
    npz2100_err_t err = npz2100_reg_read(hal, NPZ2100_REG_ID, &id);
    if (err != NPZ2100_OK) {
        return err;
    }
    return (id == NPZ2100_ID_EXPECTED) ? NPZ2100_OK : NPZ2100_ERR_DEV;
}

/* -------------------------------------------------------------------------*/

npz2100_err_t npz2100_status_read(const npz2100_hal_t *hal,
                                   uint8_t             *sta1,
                                   uint8_t             *sta2,
                                   uint8_t             *sta3)
{
    if (hal == NULL) {
        return NPZ2100_ERR_ARG;
    }

    /*
     * STA1–STA3 are at 0x02, 0x03, 0x04 — three consecutive bytes.
     * Read in a single burst to minimise bus time and ensure the watchdog
     * kick (triggered by reading STA1/STA2) covers both registers.
     */
    uint8_t buf[3];
    npz2100_err_t err = npz2100_reg_burst_read(hal, NPZ2100_REG_STA1, buf, 3u);
    if (err != NPZ2100_OK) {
        return err;
    }

    if (sta1 != NULL) { *sta1 = buf[0]; }
    if (sta2 != NULL) { *sta2 = buf[1]; }
    if (sta3 != NULL) { *sta3 = buf[2]; }

    return NPZ2100_OK;
}

/* -------------------------------------------------------------------------*/

npz2100_err_t npz2100_periph_read_value_ll(const npz2100_hal_t    *hal,
                                         const npz2100_config_t *cfg,
                                         uint8_t                 slot,
                                         uint16_t               *value)
{
    if (hal == NULL || cfg == NULL || value == NULL || slot > NPZ2100_P_BANK_MAX) {
        return NPZ2100_ERR_ARG;
    }

    /* Select the peripheral bank. */
    npz2100_err_t err = npz2100_reg_write(hal, NPZ2100_REG_P_BANK,
                                           NPZ2100_P_BANK(slot));
    if (err != NPZ2100_OK) { return err; }

    /* Read VALP_L and VALP_H in one burst. */
    uint8_t buf[2];
    err = npz2100_reg_burst_read(hal, NPZ2100_REG_VALP_L, buf, 2u);
    if (err != NPZ2100_OK) { return err; }

    /* Reconstruct value per dtype. */
    uint8_t dtype = NPZ2100_MODP_DTYPE_GET(cfg->periph[slot].modp);
    if (dtype == NPZ2100_DTYPE_UINT8) {
        *value = (uint16_t)buf[0];
    } else {
        *value = (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8u));
    }

    /* Restore P_BANK to 0. */
    return npz2100_reg_write(hal, NPZ2100_REG_P_BANK, 0x00u);
}

/* -------------------------------------------------------------------------*/

npz2100_err_t npz2100_adc_read(const npz2100_hal_t *hal,
                                uint8_t             *ch1,
                                uint8_t             *ch2,
                                uint8_t             *ch3)
{
    if (hal == NULL) {
        return NPZ2100_ERR_ARG;
    }

    /* VAL_ADC1–3 at 0x47, 0x48, 0x49 — three consecutive bytes. */
    uint8_t buf[3];
    npz2100_err_t err = npz2100_reg_burst_read(hal, NPZ2100_REG_VAL_ADC1,
                                                buf, 3u);
    if (err != NPZ2100_OK) { return err; }

    if (ch1 != NULL) { *ch1 = buf[0]; }
    if (ch2 != NULL) { *ch2 = buf[1]; }
    if (ch3 != NULL) { *ch3 = buf[2]; }

    return NPZ2100_OK;
}

/* -------------------------------------------------------------------------*/

npz2100_err_t npz2100_sram_write_ll(const npz2100_hal_t *hal,
                                  npz2100_config_t    *cfg,
                                  uint8_t              sram_addr,
                                  const uint8_t       *data,
                                  size_t               len)
{
    if (hal == NULL || cfg == NULL || data == NULL || len == 0u) {
        return NPZ2100_ERR_ARG;
    }
    /* Bounds check: sram_addr + len must not exceed SRAM size. */
    if ((size_t)sram_addr + len > (size_t)NPZ2100_SRAM_TOTAL_SIZE) {
        return NPZ2100_ERR_ARG;
    }

    npz2100_err_t err;
    size_t        written   = 0u;
    uint8_t       cur_addr  = sram_addr;

    while (written < len) {
        /* Determine which bank this byte lives in. */
        uint8_t bank        = (cur_addr >= NPZ2100_SRAM_WINDOW_SIZE) ? 1u : 0u;
        uint8_t bank_offset = (bank == 0u)
                            ? cur_addr
                            : (uint8_t)(cur_addr - NPZ2100_SRAM_WINDOW_SIZE);
        uint8_t i2c_addr_w  = (uint8_t)(NPZ2100_SRAM_WINDOW_BASE + bank_offset);

        /* How many bytes remain in this bank? */
        size_t bank_remaining = (bank == 0u)
                              ? (size_t)(NPZ2100_SRAM_WINDOW_SIZE - bank_offset)
                              : (size_t)(NPZ2100_SRAM_TOTAL_SIZE  - cur_addr);
        size_t chunk = (len - written) < bank_remaining
                     ? (len - written)
                     : bank_remaining;

        /* Switch bank only if needed. */
        if (cfg->sram_bank != bank) {
            err = npz2100_reg_write(hal, NPZ2100_REG_SRAM_BANK,
                                    NPZ2100_SRAM_BANK(bank));
            if (err != NPZ2100_OK) { return err; }
            cfg->sram_bank = bank;
        }

        /* Burst-write the chunk. */
        err = npz2100_reg_burst_write(hal, i2c_addr_w, data + written, chunk);
        if (err != NPZ2100_OK) { return err; }

        written  += chunk;
        cur_addr += (uint8_t)chunk;
    }

    return NPZ2100_OK;
}

/* -------------------------------------------------------------------------*/

npz2100_err_t npz2100_sram_read_ll(const npz2100_hal_t *hal,
                                 npz2100_config_t    *cfg,
                                 uint8_t              sram_addr,
                                 uint8_t             *data,
                                 size_t               len)
{
    if (hal == NULL || cfg == NULL || data == NULL || len == 0u) {
        return NPZ2100_ERR_ARG;
    }
    if ((size_t)sram_addr + len > (size_t)NPZ2100_SRAM_TOTAL_SIZE) {
        return NPZ2100_ERR_ARG;
    }

    npz2100_err_t err;
    size_t        read_n   = 0u;
    uint8_t       cur_addr = sram_addr;

    while (read_n < len) {
        uint8_t bank        = (cur_addr >= NPZ2100_SRAM_WINDOW_SIZE) ? 1u : 0u;
        uint8_t bank_offset = (bank == 0u)
                            ? cur_addr
                            : (uint8_t)(cur_addr - NPZ2100_SRAM_WINDOW_SIZE);
        uint8_t i2c_addr_r  = (uint8_t)(NPZ2100_SRAM_WINDOW_BASE + bank_offset);

        size_t bank_remaining = (bank == 0u)
                              ? (size_t)(NPZ2100_SRAM_WINDOW_SIZE - bank_offset)
                              : (size_t)(NPZ2100_SRAM_TOTAL_SIZE  - cur_addr);
        size_t chunk = (len - read_n) < bank_remaining
                     ? (len - read_n)
                     : bank_remaining;

        if (cfg->sram_bank != bank) {
            err = npz2100_reg_write(hal, NPZ2100_REG_SRAM_BANK,
                                    NPZ2100_SRAM_BANK(bank));
            if (err != NPZ2100_OK) { return err; }
            cfg->sram_bank = bank;
        }

        err = npz2100_reg_burst_read(hal, i2c_addr_r, data + read_n, chunk);
        if (err != NPZ2100_OK) { return err; }

        read_n   += chunk;
        cur_addr += (uint8_t)chunk;
    }

    return NPZ2100_OK;
}

/* -------------------------------------------------------------------------*/

npz2100_err_t npz2100_enter_idle_ll(const npz2100_hal_t *hal)
{
    if (hal == NULL) {
        return NPZ2100_ERR_ARG;
    }
    /*
     * IDLE_RST always reads 0x00 — no shadow update needed.
     * One I²C transaction hands control to the nPZ2100.
     */
    return npz2100_reg_write(hal, NPZ2100_REG_IDLE_RST,
                              NPZ2100_IDLE_RST_ENTER_IDLE);
}

/* -------------------------------------------------------------------------*/

npz2100_err_t npz2100_soft_reset_ll(const npz2100_hal_t *hal)
{
    if (hal == NULL) {
        return NPZ2100_ERR_ARG;
    }
    return npz2100_reg_write(hal, NPZ2100_REG_IDLE_RST,
                              NPZ2100_IDLE_RST_SOFT_RESET);
}
