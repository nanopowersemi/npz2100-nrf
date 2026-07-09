/**
 * @file npz2100.c
 * @brief nPZ2100 low-level I²C driver implementation.
 *
 * All public functions are declared in npz2100_hal.h.
 * No dynamic memory allocation is used anywhere in this file.
 * The maximum stack depth per call is one small local buffer (≤ 257 bytes).
 *
 * I²C efficiency strategy
 * -----------------------
 * Every write — whether single-register or burst — issues exactly one I²C
 * START/STOP pair by prepending the register address to the data in a local
 * stack buffer before handing off to the HAL write callback.
 *
 * This matters on the nPZ2100's 100 kHz bus: eliminating extra START
 * conditions roughly halves bus-active time for multi-byte writes.
 *
 * Watchdog note
 * -------------
 * Per datasheet §2.2.16, reading STA1 (0x02) or STA2 (0x03) resets the
 * watchdog timer.  Callers performing periodic status reads should exploit
 * this — there is no separate "kick watchdog" command.
 *
 * @version 0.7
 * @date    2026-05-06
 * @author  Nanopower Semiconductor AS
 */

#include "npz2100_hal.h"
#include <stddef.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/**
 * @brief Validate that a HAL descriptor is usable before attempting I²C.
 * Returns NPZ2100_ERR_ARG immediately if @p hal or its callbacks are NULL.
 */
static npz2100_err_t hal_check(const npz2100_hal_t *hal)
{
    if (hal == NULL || hal->write == NULL || hal->read == NULL) {
        return NPZ2100_ERR_ARG;
    }
    return NPZ2100_OK;
}

/* -------------------------------------------------------------------------
 * npz2100_reg_write
 * ---------------------------------------------------------------------- */

/**
 * @brief Write a single register byte via one I²C transaction.
 *
 * Packs [reg, value] into a 2-byte stack buffer so the HAL callback can
 * issue a single START + STOP.  No heap allocation.
 */
npz2100_err_t npz2100_reg_write(const npz2100_hal_t *hal,
                                 uint8_t              reg,
                                 uint8_t              value)
{
    npz2100_err_t err;
    /* Stack buffer: [register address, data byte] — one I²C transfer. */
    uint8_t buf[2];

    err = hal_check(hal);
    if (err != NPZ2100_OK) {
        return err;
    }

    buf[0] = reg;
    buf[1] = value;

    return hal->write(hal->i2c_addr, buf, sizeof(buf), hal->ctx);
}

/* -------------------------------------------------------------------------
 * npz2100_reg_read
 * ---------------------------------------------------------------------- */

/**
 * @brief Read a single register byte.
 *
 * The HAL read callback is responsible for the write-then-read sequence
 * (register pointer write + read restart).
 */
npz2100_err_t npz2100_reg_read(const npz2100_hal_t *hal,
                                uint8_t              reg,
                                uint8_t             *value)
{
    npz2100_err_t err;

    err = hal_check(hal);
    if (err != NPZ2100_OK) {
        return err;
    }
    if (value == NULL) {
        return NPZ2100_ERR_ARG;
    }

    return hal->read(hal->i2c_addr, reg, value, 1u, hal->ctx);
}

/* -------------------------------------------------------------------------
 * npz2100_reg_burst_write
 * ---------------------------------------------------------------------- */

/**
 * @brief Burst-write up to 256 data bytes to consecutive registers.
 *
 * Assembles [start_reg, data[0], data[1], ..., data[len-1]] on the stack
 * and issues a single I²C transaction.  Maximum burst size is 256 bytes
 * (limited by the nPZ2100's SRAM window size and I²C address space).
 *
 * Stack usage: len + 1 bytes (max 257 bytes).  Callers on extremely
 * constrained stacks should chunk large SRAM writes into ≤ 128-byte bursts.
 */
npz2100_err_t npz2100_reg_burst_write(const npz2100_hal_t *hal,
                                       uint8_t              start_reg,
                                       const uint8_t       *data,
                                       size_t               len)
{
    npz2100_err_t err;
    /*
     * Maximum single burst: 256 data bytes + 1 register address byte = 257.
     * This covers the full SRAM window in one transaction.
     */
    uint8_t buf[257];

    err = hal_check(hal);
    if (err != NPZ2100_OK) {
        return err;
    }
    if (data == NULL || len == 0u || len > 256u) {
        return NPZ2100_ERR_ARG;
    }

    /* Prepend register address — single I²C START/STOP for the whole burst. */
    buf[0] = start_reg;
    for (size_t i = 0u; i < len; i++) {
        buf[i + 1u] = data[i];
    }

    return hal->write(hal->i2c_addr, buf, len + 1u, hal->ctx);
}

/* -------------------------------------------------------------------------
 * npz2100_reg_burst_read
 * ---------------------------------------------------------------------- */

/**
 * @brief Burst-read consecutive registers.
 *
 * Issues one register-pointer write followed by one multi-byte read.
 * The HAL callback handles the repeated START between them.
 */
npz2100_err_t npz2100_reg_burst_read(const npz2100_hal_t *hal,
                                      uint8_t              start_reg,
                                      uint8_t             *data,
                                      size_t               len)
{
    npz2100_err_t err;

    err = hal_check(hal);
    if (err != NPZ2100_OK) {
        return err;
    }
    if (data == NULL || len == 0u) {
        return NPZ2100_ERR_ARG;
    }

    return hal->read(hal->i2c_addr, start_reg, data, len, hal->ctx);
}

/* -------------------------------------------------------------------------
 * npz2100_reg_rmw
 * ---------------------------------------------------------------------- */

/**
 * @brief Read-modify-write a register field.
 *
 * Procedure:
 *   1. Read current register value  (1 I²C read transaction).
 *   2. Clear the masked bits.
 *   3. OR in the new value aligned to the mask.
 *   4. Write the result back        (1 I²C write transaction).
 *
 * Total bus cost: 2 transactions.  Use burst writes when multiple fields in
 * the same register need updating — it is cheaper to compute the full byte
 * and call npz2100_reg_write() once.
 */
npz2100_err_t npz2100_reg_rmw(const npz2100_hal_t *hal,
                                uint8_t              reg,
                                uint8_t              mask,
                                uint8_t              value)
{
    npz2100_err_t err;
    uint8_t       current;

    err = hal_check(hal);
    if (err != NPZ2100_OK) {
        return err;
    }

    /* Step 1: read current register value. */
    err = hal->read(hal->i2c_addr, reg, &current, 1u, hal->ctx);
    if (err != NPZ2100_OK) {
        return err;
    }

    /* Step 2–3: clear masked bits, insert new value. */
    current = (uint8_t)((current & ~mask) | (value & mask));

    /* Step 4: write back — single I²C transaction via write primitive. */
    {
        uint8_t buf[2];
        buf[0] = reg;
        buf[1] = current;
        return hal->write(hal->i2c_addr, buf, 2u, hal->ctx);
    }
}
