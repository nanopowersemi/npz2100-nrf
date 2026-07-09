/**
 * @file npz2100_hal.h
 * @brief Platform-agnostic HAL interface for the nPZ2100 power-saving IC.
 *
 * The user must implement two callbacks and populate an npz2100_hal_t struct
 * before calling any API function.  Every I²C transaction is routed through
 * these callbacks, so porting to a new MCU requires only filling in those two
 * functions — nothing else in the driver changes.
 *
 * I²C efficiency note
 * -------------------
 * Write transactions pass a single contiguous buffer where buf[0] is the
 * register address and buf[1..n] is the data.  This allows the HAL to issue
 * one START + one STOP per write, which is critical on a 100 kHz bus shared
 * with low-power sensors.
 *
 * @version 0.7
 * @date    2026-05-06
 * @author  Nanopower Semiconductor AS
 */

#ifndef NPZ2100_HAL_H
#define NPZ2100_HAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Error type
 * ---------------------------------------------------------------------- */

/**
 * @defgroup npz2100_errors Error codes
 * @{
 */
typedef enum {
    NPZ2100_OK            =  0,  /**< Operation completed successfully.          */
    NPZ2100_ERR_IO        = -1,  /**< I²C bus error (NAK, timeout, bus fault).   */
    NPZ2100_ERR_ARG       = -2,  /**< Invalid argument supplied by caller.       */
    NPZ2100_ERR_DEV       = -3,  /**< Device not responding / ID mismatch.       */
    NPZ2100_ERR_TIMEOUT   = -4,  /**< Operation timed out.                       */
    NPZ2100_ERR_STATE     = -5,  /**< Device in wrong state for this operation.  */
} npz2100_err_t;
/** @} */

/* -------------------------------------------------------------------------
 * HAL callbacks
 * ---------------------------------------------------------------------- */

/**
 * @brief I²C write callback.
 *
 * The driver calls this to write @p len bytes from @p buf to the device.
 * buf[0] is always the target register address; buf[1..len-1] is the data.
 * The implementation must issue a single I²C transfer (START, addr+W, buf[0..len-1], STOP).
 *
 * @param[in] i2c_addr  7-bit I²C address of the nPZ2100 (typically 0x6F).
 * @param[in] buf       Pointer to buffer: [reg_addr, data0, data1, ...].
 * @param[in] len       Total number of bytes in buf (register byte included).
 * @param[in] ctx       User context pointer passed through from npz2100_hal_t.
 * @return NPZ2100_OK on success, NPZ2100_ERR_IO on bus error.
 */
typedef npz2100_err_t (*npz2100_i2c_write_fn)(uint8_t        i2c_addr,
                                               const uint8_t *buf,
                                               size_t         len,
                                               void          *ctx);

/**
 * @brief I²C read callback.
 *
 * The driver calls this to read @p len bytes into @p buf from register @p reg.
 * The implementation must:
 *   1. Issue a write of @p reg (START, addr+W, reg, STOP or repeated START).
 *   2. Issue a read of @p len bytes (START, addr+R, buf[0..len-1], STOP).
 *
 * @param[in]  i2c_addr  7-bit I²C address of the nPZ2100.
 * @param[in]  reg       Register address to read from.
 * @param[out] buf       Destination buffer for the received bytes.
 * @param[in]  len       Number of bytes to read.
 * @param[in]  ctx       User context pointer passed through from npz2100_hal_t.
 * @return NPZ2100_OK on success, NPZ2100_ERR_IO on bus error.
 */
typedef npz2100_err_t (*npz2100_i2c_read_fn)(uint8_t  i2c_addr,
                                              uint8_t  reg,
                                              uint8_t *buf,
                                              size_t   len,
                                              void    *ctx);

/* -------------------------------------------------------------------------
 * HAL context structure
 * ---------------------------------------------------------------------- */

/**
 * @brief HAL descriptor — populate once and pass to every API call.
 *
 * Example (bare-metal pseudocode):
 * @code
 *   static npz2100_hal_t hal = {
 *       .write    = my_i2c_write,
 *       .read     = my_i2c_read,
 *       .i2c_addr = NPZ2100_I2C_ADDR_DEFAULT,
 *       .ctx      = NULL,
 *   };
 * @endcode
 */
typedef struct {
    npz2100_i2c_write_fn write;      /**< Platform I²C write implementation. */
    npz2100_i2c_read_fn  read;       /**< Platform I²C read implementation.  */
    uint8_t              i2c_addr;   /**< 7-bit device I²C address.          */
    void                *ctx;        /**< Optional user context (e.g. bus handle). */
} npz2100_hal_t;

/** Default I²C address (verify with hardware strapping). */
#define NPZ2100_I2C_ADDR_DEFAULT  (0x6Fu)

/* -------------------------------------------------------------------------
 * Low-level primitives (used internally; exposed for advanced users)
 * ---------------------------------------------------------------------- */

/**
 * @brief Write a single register byte.
 *
 * Issues one I²C transaction: [reg, value].
 *
 * @param[in] hal   Pointer to initialised HAL descriptor.
 * @param[in] reg   Register address (see npz2100_regs_*.h).
 * @param[in] value Byte value to write.
 * @return NPZ2100_OK or an error code.
 */
npz2100_err_t npz2100_reg_write(const npz2100_hal_t *hal,
                                 uint8_t              reg,
                                 uint8_t              value);

/**
 * @brief Read a single register byte.
 *
 * @param[in]  hal   Pointer to initialised HAL descriptor.
 * @param[in]  reg   Register address.
 * @param[out] value Pointer to destination byte.
 * @return NPZ2100_OK or an error code.
 */
npz2100_err_t npz2100_reg_read(const npz2100_hal_t *hal,
                                uint8_t              reg,
                                uint8_t             *value);

/**
 * @brief Burst-write consecutive registers in a single I²C transaction.
 *
 * Writes @p len bytes from @p data starting at @p start_reg.
 * Uses one START/STOP pair regardless of length — maximally bus-efficient.
 *
 * @param[in] hal       Pointer to initialised HAL descriptor.
 * @param[in] start_reg First register address.
 * @param[in] data      Pointer to data bytes (len bytes).
 * @param[in] len       Number of bytes to write.
 * @return NPZ2100_OK or an error code.
 */
npz2100_err_t npz2100_reg_burst_write(const npz2100_hal_t *hal,
                                       uint8_t              start_reg,
                                       const uint8_t       *data,
                                       size_t               len);

/**
 * @brief Burst-read consecutive registers in a single I²C transaction.
 *
 * @param[in]  hal       Pointer to initialised HAL descriptor.
 * @param[in]  start_reg First register address.
 * @param[out] data      Destination buffer (len bytes).
 * @param[in]  len       Number of bytes to read.
 * @return NPZ2100_OK or an error code.
 */
npz2100_err_t npz2100_reg_burst_read(const npz2100_hal_t *hal,
                                      uint8_t              start_reg,
                                      uint8_t             *data,
                                      size_t               len);

/**
 * @brief Read-modify-write a single register using a mask.
 *
 * Reads the current value, clears the bits in @p mask, ORs in
 * (@p value & @p mask), then writes the result back.  This is the
 * correct pattern for bitfield-level updates without disturbing
 * neighbouring fields.
 *
 * @param[in] hal   Pointer to initialised HAL descriptor.
 * @param[in] reg   Register address.
 * @param[in] mask  Bitmask of the field(s) to modify.
 * @param[in] value New value for the masked bits (unmasked bits ignored).
 * @return NPZ2100_OK or an error code.
 */
npz2100_err_t npz2100_reg_rmw(const npz2100_hal_t *hal,
                                uint8_t              reg,
                                uint8_t              mask,
                                uint8_t              value);

#ifdef __cplusplus
}
#endif

#endif /* NPZ2100_HAL_H */
