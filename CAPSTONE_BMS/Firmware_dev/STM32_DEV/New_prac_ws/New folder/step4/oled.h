/*
 * oled.h — SH1106 / SSD1306 128x64 I2C OLED driver, page-sliced
 *
 *  Originally: harsh, Aug 23 2026. Reworked for M1 Step 4.
 *
 *  WHAT CHANGED AND WHY
 *  1. Burst page writes. The old driver sent every display byte as its own
 *     I2C transaction (addr + control + 1 data byte). A full frame was
 *     1048 transactions x 27 clocks = 28,296 clocks = ~283 ms at 100 kHz
 *     [CALCULATED]. One page is now one transaction:
 *     (1 addr + 1 control + 128 data) x 9 clocks = 1170 clocks
 *     = 11.7 ms at 100 kHz [CALCULATED].
 *
 *  2. Page slicing. OLED_Service() pushes AT MOST ONE changed page per
 *     call. That puts a hard bound (~12 ms + small command overhead) on
 *     how long the display can occupy I2C1, which String A's INA260 also
 *     uses.
 *
 *  3. Failure isolation. The old driver called OLED_I2C_Recover() on
 *     EVERY failed byte (>= 18 ms each). With the OLED unplugged that was
 *     1048 x 18 ms = ~19 s per frame with the safety loop frozen
 *     [CALCULATED]. Now: first failure aborts, counts, and backs off for
 *     2 s. The OLED driver NEVER performs bus recovery — the INA260 read
 *     path on I2C1 owns that. One owner, no fighting.
 *
 *  4. Text rows are page-aligned (row r = pixel rows 8r..8r+7), so one
 *     text line = one page = one I2C write when only that line changes.
 *
 *  THREADING: call only from the main loop. Not ISR-safe, not re-entrant.
 *  The I2C1 handle is shared with the INA260 driver; both are used only
 *  from the main loop, so they cannot interleave.
 */

#ifndef OLED_H
#define OLED_H

#include "main.h"
#include <stdint.h>

/* ---- Controller selection --------------------------------------------
 * 1.3" modules are usually SH1106: 132-column RAM, visible window starts
 * at column 2. 0.96" modules are usually SSD1306: 128 columns, offset 0.
 * Size is only a hint. CONFIRM with the boot-screen border:
 *   - all four edges visible and unbroken          -> setting is correct
 *   - left edge missing / 2 px gap / wrap on right -> wrong offset
 *   - garbage or blank                             -> try the other chip
 */
#define OLED_CTRL_SSD1306   0
#define OLED_CTRL_SH1106    1

#ifndef OLED_CONTROLLER
#define OLED_CONTROLLER     OLED_CTRL_SH1106
#endif

#define OLED_ADDR7          0x3C     /* 7-bit; matches the bench-proven value */
#define OLED_WIDTH          128u
#define OLED_HEIGHT         64u
#define OLED_PAGES          8u
#define OLED_TEXT_COLS      21u      /* 6 px/char, x = 1..126 (cols 0/127 kept
                                        clear for the boot border)          */

/**
 * @brief  Boot-time init (blocking, includes a 100 ms power-settle delay).
 *         Call before the control loop starts; switches must already be
 *         off. Returns 1 if the display ACKed the whole init sequence.
 *         On failure the driver enters back-off and retries init later on
 *         its own from OLED_Service() — no caller action needed.
 */
uint8_t  OLED_Init(I2C_HandleTypeDef *hi2c);

/* ---- Framebuffer drawing: RAM only, no I2C, fast ---------------------- */
void     OLED_Clear(void);
/** Print one 21-char text row (row 0..7). Longer text is truncated.
 *  invert = 1 draws light-on-dark background (used for fault lines). */
void     OLED_PrintRow(uint8_t row, const char *text, uint8_t invert);
/** 1-pixel frame around the full 128x64 area — column-offset check. */
void     OLED_DrawBorder(void);

/* ---- Transfer --------------------------------------------------------- */
/** Compare framebuffer with what the panel holds; mark changed pages. */
void     OLED_Commit(void);
/** Send at most ONE dirty page (or one re-init attempt). Main loop only. */
void     OLED_Service(void);
/** Boot use only: service until clean or timeout. Returns 1 if clean. */
uint8_t  OLED_FlushBlocking(uint32_t timeout_ms);

/* ---- Diagnostics ------------------------------------------------------ */
uint32_t OLED_ErrorCount(void);
uint8_t  OLED_IsOnline(void);

#endif /* OLED_H */
