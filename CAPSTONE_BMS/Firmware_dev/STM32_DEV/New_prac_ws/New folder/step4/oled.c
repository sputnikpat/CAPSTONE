/*
 * oled.c — SH1106 / SSD1306 128x64 I2C OLED driver, page-sliced
 *
 *  Originally: harsh, Aug 23 2026. Reworked for M1 Step 4.
 *  See oled.h for the rationale behind every structural change.
 *
 *  >> VERIFY against the controller datasheet you actually have:
 *     - I2C control byte 0x00 = "command follows", 0x40 = "data stream
 *       follows" (SSD1306 datasheet §8.1.5; SH1106 datasheet, I2C
 *       interface section). Stated from memory, not from a file in this
 *       project.
 *     - 0xB0..0xB7 = set page address; 0x00..0x0F / 0x10..0x1F = column
 *       address low/high nibble. Valid in SSD1306 PAGE addressing mode and
 *       natively on SH1106.
 *     - The init sequence below is your bench-proven sequence, with one
 *       change: the old 0x20,0x00 ("horizontal addressing mode", an
 *       SSD1306-only command) is replaced by 0x20,0x02 (page mode) on
 *       SSD1306 and dropped on SH1106. The old driver mixed horizontal
 *       mode with page-mode commands; that only worked because a full
 *       frame was always written in order. Page slicing writes pages out
 *       of order, so the mode must really be page mode.
 */

#include "oled.h"
#include <string.h>

/* ---------------------------------------------------------------------- */
/* Configuration                                                           */
/* ---------------------------------------------------------------------- */

#if (OLED_CONTROLLER == OLED_CTRL_SH1106)
#define OLED_COL_OFFSET      2u   /* 132-col RAM, 128 visible, centred     */
#else
#define OLED_COL_OFFSET      0u
#endif

#define OLED_ADDR8           ((uint16_t)(OLED_ADDR7 << 1)) /* HAL wants 8-bit */

#define OLED_CTRL_CMD        0x00u  /* Co=0, D/C#=0 : command byte follows  */
#define OLED_CTRL_DATA       0x40u  /* Co=0, D/C#=1 : display data follows  */

/* Timeouts [CALCULATED]:
 *   one command = (addr + ctrl + cmd) 3 bytes x 9 clocks = 27 clocks
 *                 = 0.27 ms at 100 kHz           -> 5 ms is ample
 *   one page    = (addr + ctrl + 128) 130 x 9    = 1170 clocks
 *                 = 11.7 ms at 100 kHz           -> 25 ms gives ~2x margin
 * Never HAL_MAX_DELAY: a hung bus must not hang the control loop. */
#define OLED_CMD_TIMEOUT_MS   5u
#define OLED_PAGE_TIMEOUT_MS  25u

/* After any failure, leave the display alone for this long. Long enough
 * that a dead/unplugged OLED costs at most one ~25 ms timeout every 2 s. */
#define OLED_BACKOFF_MS       2000u

/* ---------------------------------------------------------------------- */
/* State                                                                   */
/* ---------------------------------------------------------------------- */

typedef enum {
    OLED_ST_UNINIT = 0,   /* OLED_Init() never called                     */
    OLED_ST_RUN,          /* panel believed configured, pushing pages      */
    OLED_ST_BACKOFF       /* last transfer failed; waiting, then re-init   */
} OledState;

static I2C_HandleTypeDef *oled_i2c   = NULL;
static OledState          oled_state = OLED_ST_UNINIT;
static uint32_t           backoff_t0 = 0;
static uint32_t           err_count  = 0;

/* fb     = what we WANT on screen (drawn by the application)
 * shadow = what we believe the panel HOLDS (updated only after a page
 *          write is ACKed)
 * shadow_valid bit p = 1 if shadow[p] is trustworthy. Cleared on any
 *          failure, because after a brown-out or re-plug the panel RAM is
 *          unknown and every page must be resent. */
static uint8_t  fb[OLED_PAGES][OLED_WIDTH];
static uint8_t  shadow[OLED_PAGES][OLED_WIDTH];
static uint8_t  shadow_valid = 0x00u;
static uint8_t  dirty_mask   = 0x00u;
static uint8_t  next_page    = 0u;    /* round-robin start, for fairness  */

/* Page TX buffer: control byte + one page. Static, not on the stack. */
static uint8_t  page_tx[1u + OLED_WIDTH];

/* ---------------------------------------------------------------------- */
/* Init sequence                                                            */
/* ---------------------------------------------------------------------- */

static const uint8_t oled_init_seq[] =
{
    0xAE,         /* display OFF while configuring                         */
    0xD5, 0x80,   /* display clock divide / oscillator                      */
    0xA8, 0x3F,   /* multiplex ratio = 64 rows                              */
    0xD3, 0x00,   /* display offset = 0                                     */
    0x40,         /* display start line = 0                                 */
    0x8D, 0x14,   /* SSD1306: enable charge pump. On SH1106 0x8D is not, to
                     my knowledge, a defined command — kept anyway because
                     this exact sequence is bench-proven on your module.    */
#if (OLED_CONTROLLER == OLED_CTRL_SSD1306)
    0x20, 0x02,   /* memory addressing mode = PAGE (required for slicing)   */
#endif
    0xA1,         /* segment remap (column 127 -> SEG0)                     */
    0xC8,         /* COM scan direction remapped                            */
    0xDA, 0x12,   /* COM pins hardware config                               */
    0x81, 0x7F,   /* contrast                                               */
    0xD9, 0xF1,   /* pre-charge period                                      */
    0xDB, 0x40,   /* VCOMH deselect level                                   */
    0xA4,         /* display follows RAM                                    */
    0xA6,         /* normal (not inverted)                                  */
    0xAF          /* display ON                                             */
};

/* ---------------------------------------------------------------------- */
/* 5x7 font. Column bytes, bit0 = top pixel row (same convention as an     */
/* SSD1306/SH1106 page byte, so glyphs copy straight into the buffer).     */
/* Glyph data A-Z, 0-9, - : . % carried over unchanged from the original.  */
/* ---------------------------------------------------------------------- */

static const uint8_t font_alpha[26][5] =
{
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, /* A B */
    {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C}, /* C D */
    {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01}, /* E F */
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F}, /* G H */
    {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01}, /* I J */
    {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40}, /* K L */
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, /* M N */
    {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06}, /* O P */
    {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46}, /* Q R */
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01}, /* S T */
    {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F}, /* U V */
    {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63}, /* W X */
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}  /* Y Z */
};

static const uint8_t font_digit[10][5] =
{
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, /* 0 1 */
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, /* 2 3 */
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39}, /* 4 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03}, /* 6 7 */
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}  /* 8 9 */
};

static const uint8_t glyph_minus[5]   = {0x08,0x08,0x08,0x08,0x08};
static const uint8_t glyph_colon[5]   = {0x00,0x36,0x36,0x00,0x00};
static const uint8_t glyph_dot[5]     = {0x00,0x60,0x60,0x00,0x00};
static const uint8_t glyph_percent[5] = {0x63,0x13,0x08,0x64,0x63};
/* New in Step 4 */
static const uint8_t glyph_plus[5]    = {0x08,0x08,0x3E,0x08,0x08};
static const uint8_t glyph_slash[5]   = {0x40,0x20,0x10,0x08,0x04};
static const uint8_t glyph_space[5]   = {0x00,0x00,0x00,0x00,0x00};
/* Unsupported character -> visible hollow box, never a silent blank.
   A silently dropped character could hide a '-' sign. */
static const uint8_t glyph_unknown[5] = {0x7F,0x41,0x41,0x41,0x7F};

static const uint8_t *oled_glyph(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');  /* no lowercase */

    if (c >= 'A' && c <= 'Z') return font_alpha[c - 'A'];
    if (c >= '0' && c <= '9') return font_digit[c - '0'];

    switch (c)
    {
        case ' ': return glyph_space;
        case '-': return glyph_minus;
        case ':': return glyph_colon;
        case '.': return glyph_dot;
        case '%': return glyph_percent;
        case '+': return glyph_plus;
        case '/': return glyph_slash;
        default:  return glyph_unknown;
    }
}

/* ---------------------------------------------------------------------- */
/* Low-level I2C — no retries, no recovery (by design, see oled.h)         */
/* ---------------------------------------------------------------------- */

static uint8_t oled_cmd(uint8_t c)
{
    uint8_t b[2] = { OLED_CTRL_CMD, c };
    return (HAL_I2C_Master_Transmit(oled_i2c, OLED_ADDR8, b, 2u,
                                    OLED_CMD_TIMEOUT_MS) == HAL_OK) ? 1u : 0u;
}

static uint8_t oled_send_init(void)
{
    for (uint32_t k = 0; k < sizeof(oled_init_seq); k++)
    {
        if (!oled_cmd(oled_init_seq[k])) return 0u;   /* abort on first error */
    }
    return 1u;
}

static uint8_t oled_write_page(uint8_t p)
{
    /* Point the controller at (page p, first visible column). */
    if (!oled_cmd((uint8_t)(0xB0u | p)))                      return 0u;
    if (!oled_cmd((uint8_t)(0x00u | (OLED_COL_OFFSET & 0x0Fu)))) return 0u;
    if (!oled_cmd((uint8_t)(0x10u | (OLED_COL_OFFSET >> 4))))    return 0u;

    /* One transaction: control byte 0x40, then 128 data bytes. */
    page_tx[0] = OLED_CTRL_DATA;
    memcpy(&page_tx[1], fb[p], OLED_WIDTH);

    return (HAL_I2C_Master_Transmit(oled_i2c, OLED_ADDR8, page_tx,
                                    (uint16_t)sizeof(page_tx),
                                    OLED_PAGE_TIMEOUT_MS) == HAL_OK) ? 1u : 0u;
}

static void oled_fail(void)
{
    err_count++;
    oled_state   = OLED_ST_BACKOFF;
    backoff_t0   = HAL_GetTick();
    shadow_valid = 0x00u;          /* panel contents now unknown           */
    dirty_mask   = 0xFFu;          /* resend everything once it is back    */
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */
/* ---------------------------------------------------------------------- */

uint8_t OLED_Init(I2C_HandleTypeDef *hi2c)
{
    oled_i2c     = hi2c;
    shadow_valid = 0x00u;
    dirty_mask   = 0xFFu;
    next_page    = 0u;
    OLED_Clear();

    if (oled_i2c == NULL) { oled_state = OLED_ST_UNINIT; return 0u; }

    HAL_Delay(100);                /* panel power-up settle (boot only)    */

    if (!oled_send_init()) { oled_fail(); return 0u; }

    oled_state = OLED_ST_RUN;
    return 1u;
}

void OLED_Clear(void)
{
    memset(fb, 0x00, sizeof(fb));
}

void OLED_PrintRow(uint8_t row, const char *text, uint8_t invert)
{
    if (row >= OLED_PAGES) return;

    uint8_t *line = fb[row];
    memset(line, 0x00, OLED_WIDTH);

    if (text != NULL)
    {
        /* Start at x = 1 so columns 0 and 127 stay free for the border.
           21 chars x 6 px = 126 px -> occupies x = 1..126. */
        uint32_t x = 1u;
        for (uint32_t n = 0; n < OLED_TEXT_COLS && text[n] != '\0'; n++)
        {
            const uint8_t *g = oled_glyph(text[n]);
            memcpy(&line[x], g, 5u);   /* column 6 of each cell stays blank */
            x += 6u;
        }
    }

    if (invert)
    {
        for (uint32_t x = 0; x < OLED_WIDTH; x++) line[x] ^= 0xFFu;
    }
}

void OLED_DrawBorder(void)
{
    for (uint32_t x = 0; x < OLED_WIDTH; x++)
    {
        fb[0][x]              |= 0x01u;   /* pixel row 0  (top)     */
        fb[OLED_PAGES - 1][x] |= 0x80u;   /* pixel row 63 (bottom)  */
    }
    for (uint32_t p = 0; p < OLED_PAGES; p++)
    {
        fb[p][0]              = 0xFFu;    /* column 0   (left)      */
        fb[p][OLED_WIDTH - 1] = 0xFFu;    /* column 127 (right)     */
    }
}

void OLED_Commit(void)
{
    for (uint8_t p = 0; p < OLED_PAGES; p++)
    {
        uint8_t bit = (uint8_t)(1u << p);
        if (!(shadow_valid & bit) || memcmp(fb[p], shadow[p], OLED_WIDTH) != 0)
            dirty_mask |= bit;
        else
            dirty_mask &= (uint8_t)~bit;   /* changed back -> nothing to send */
    }
}

void OLED_Service(void)
{
    if (oled_i2c == NULL || oled_state == OLED_ST_UNINIT) return;

    if (oled_state == OLED_ST_BACKOFF)
    {
        if ((HAL_GetTick() - backoff_t0) < OLED_BACKOFF_MS) return;

        /* The panel may have browned out or been re-plugged: its config
           registers are unknown, so re-run init before any page. Re-init
           is ~25 commands x 0.27 ms = ~7 ms [CALCULATED], less than one
           page write, so it fits in the same bus-time budget. */
        if (!oled_send_init()) { oled_fail(); return; }

        oled_state = OLED_ST_RUN;
        return;                    /* one bus job per call                 */
    }

    if (dirty_mask == 0u) return;

    /* Round-robin from next_page so one constantly-changing row cannot
       starve the others. */
    uint8_t p = next_page;
    for (uint8_t k = 0; k < OLED_PAGES; k++)
    {
        p = (uint8_t)((next_page + k) & (OLED_PAGES - 1u));
        if (dirty_mask & (1u << p)) break;
    }

    if (oled_write_page(p))
    {
        memcpy(shadow[p], &page_tx[1], OLED_WIDTH);  /* what was ACKed */
        shadow_valid |= (uint8_t)(1u << p);
        /* Only clear dirty if fb hasn't changed since we copied it. */
        if (memcmp(fb[p], shadow[p], OLED_WIDTH) == 0)
            dirty_mask &= (uint8_t)~(1u << p);
        next_page = (uint8_t)((p + 1u) & (OLED_PAGES - 1u));
    }
    else
    {
        oled_fail();
    }
}

uint8_t OLED_FlushBlocking(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    OLED_Commit();
    while (dirty_mask != 0u && oled_state == OLED_ST_RUN)
    {
        if ((HAL_GetTick() - t0) >= timeout_ms) break;
        OLED_Service();
    }
    return (dirty_mask == 0u) ? 1u : 0u;
}

uint32_t OLED_ErrorCount(void) { return err_count; }
uint8_t  OLED_IsOnline(void)   { return (oled_state == OLED_ST_RUN) ? 1u : 0u; }
