#include "SDT.h"
#include "MainBoard_Display.h"
#include "MainBoard_DisplayFT8.h"
#include "DSP_FT8.h"
#include <RA8875.h>  // for RA8875 type and RA8875_* color constants

extern RA8875 tft;  /* defined in MainBoard_Display.cpp */

/*
 * FT8 message pane renderer.
 *
 * Three columns side-by-side (All / CQ / RX), T41-style. Each column shows
 * the most recent N messages, newest at top, in a 2-line-per-message format.
 *
 * Layout math (assuming the standard PaneSpectrum size of 520x345):
 *   - 3 columns of width = (520 - 2 gutters * 4 px) / 3 = ~170 px each
 *   - Header row: ~17 px tall (default font)
 *   - Message rows: 2 * font_height each; ~10 messages per column visible
 *
 * Layout adapts to the rect passed in -- nothing is hardcoded to 520x345
 * so the pane can be re-located in a follow-up without touching this file.
 */

/* Header colors per column */
#define FT8_COL_HEADER_ALL  RA8875_WHITE
#define FT8_COL_HEADER_CQ   RA8875_GREEN
#define FT8_COL_HEADER_RX   RA8875_YELLOW

#define FT8_COL_TIME_COLOR  RA8875_CYAN
#define FT8_COL_FREQ_COLOR  RA8875_WHITE
#define FT8_COL_SNR_COLOR   DARKGREY
#define FT8_COL_MSG_COLOR   RA8875_WHITE

#define FT8_COLUMN_GUTTER_PX 4

/*
 * Render one column's worth of messages.
 * indices[head], indices[head-1], ... are the indices into rxBuf[] for
 * the messages to show, newest first.
 *
 * Each row is 2 lines:
 *   Row 1 (compact summary):  MM:SS  freq(Hz)  SNR(dB)
 *   Row 2 (message body):     truncated to columnWidth chars
 */
static void DrawFT8Column(int col_x, int col_y, int col_w, int col_h,
                          const char *headerText, uint16_t headerColor,
                          const int *indices, int count, int head) {
    /* Erase column background first */
    tft.fillRect(col_x, col_y, col_w, col_h, RA8875_BLACK);

    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);
    int charW = tft.getFontWidth();
    int charH = tft.getFontHeight();
    if (charW <= 0) charW = 8;
    if (charH <= 0) charH = 16;

    /* Header */
    tft.setTextColor(headerColor);
    tft.setCursor(col_x + 2, col_y + 1);
    tft.print(headerText);
    /* Rule under header */
    tft.drawFastHLine(col_x, col_y + charH + 1, col_w, headerColor);

    /* No messages? Show a placeholder. */
    if (count <= 0 || head < 0) {
        tft.setTextColor(DARKGREY);
        tft.setCursor(col_x + 2, col_y + charH + 6);
        tft.print("(no messages)");
        return;
    }

    /* Each rendered message takes 2 text lines + 2 px gap = (2*charH + 2) */
    int rowPx     = 2 * charH + 2;
    int firstY    = col_y + charH + 4;        /* below header rule */
    int availPx   = col_h - (charH + 4);      /* below header */
    int maxRows   = availPx / rowPx;
    if (maxRows < 1) maxRows = 1;

    /* Render newest first, walking backwards from `head` over the ring. */
    int rowsToShow = (count < maxRows) ? count : maxRows;
    for (int r = 0; r < rowsToShow; r++) {
        int slot = head - r;
        if (slot < 0) slot += FT8_MAX_DECODED_MESSAGES;
        int idx = indices[slot];
        if (idx < 0 || idx >= FT8_MAX_DECODED_MESSAGES) continue;

        const RxMsg *m = &rxBuf[idx];
        int rowY = firstY + r * rowPx;

        /* Row 1: MM:SS  freq  SNR */
        char line1[32];
        snprintf(line1, sizeof(line1), "%02u:%02u %4d %+3d",
                 (unsigned)m->slot_time.tm_min,
                 (unsigned)m->slot_time.tm_sec,
                 (int)(m->freq + 0.5f),
                 (int)(m->snr));
        tft.setTextColor(FT8_COL_TIME_COLOR);
        tft.setCursor(col_x + 2, rowY);
        tft.print(line1);

        /* Row 2: message body, truncated to column width */
        int msgChars = (col_w - 4) / charW;
        if (msgChars < 1) msgChars = 1;
        if (msgChars > 30) msgChars = 30;
        char line2[32];
        strncpy(line2, m->msg, msgChars);
        line2[(msgChars < (int)sizeof(line2)) ? msgChars : (int)sizeof(line2) - 1] = '\0';
        tft.setTextColor(FT8_COL_MSG_COLOR);
        tft.setCursor(col_x + 2, rowY + charH);
        tft.print(line2);
    }
}

/*
 * Render the status line at the top of the FT8 pane:
 *
 *   RX:1500  TX:1500  TXMODE:OFF  CQ:MAN  INT:EVEN
 *
 * Field widths chosen so the whole line fits in 520 px at the default font.
 * Repaints whenever the pane is invalidated -- which now happens on freq
 * change (DSP_FT8.cpp::ChangeFT8RxFreq/TxFreq) and on every decode cycle.
 */
static void DrawFT8StatusLine(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, RA8875_BLACK);

    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);
    int charW = tft.getFontWidth();
    if (charW <= 0) charW = 8;

    /* Build one packed line. Use chunks separated by two spaces. */
    char line[100];
    /* Show target callsign at the right end. Empty target -> "TGT:----". */
    const char *tgt = (ft8TargetCall[0] != '\0') ? ft8TargetCall : "----";
    snprintf(line, sizeof(line),
             "RX:%4d TX:%4d TXMODE:%s CQ:%s INT:%s TGT:%s",
             (int)ft8RxFreq,
             (int)ft8TxFreq,
             ft8TxState ? "ON " : "OFF",
             ft8CqState ? "AUTO" : "MAN ",
             ft8IntState ? "ODD " : "EVEN",
             tgt);

    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(x + 2, y + 1);
    tft.print(line);

    /* Subtle rule across the full width to separate from the columns below */
    tft.drawFastHLine(x, y + h - 1, w, DARKGREY);
}

void DrawFT8MessageList(uint16_t x0, uint16_t y0, uint16_t width, uint16_t height) {
    /* Erase entire region first (covers the case where we just transitioned
     * from spectrum to FT8 mode). */
    tft.fillRect(x0, y0, width, height, RA8875_BLACK);

    /* Status line carved off the top. Height = one font line + a 4 px gap. */
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);
    int charH = tft.getFontHeight();
    if (charH <= 0) charH = 16;
    int statusH = charH + 4;
    if (statusH > (int)height / 4) statusH = height / 4;  /* paranoid guard */

    DrawFT8StatusLine(x0, y0, width, statusH);

    /* Three equal-width columns with gutters, below the status line. */
    int gutter = FT8_COLUMN_GUTTER_PX;
    int col_w  = (width - 2 * gutter) / 3;
    int col_y  = y0 + statusH;
    int col_h  = height - statusH;

    int col1_x = x0;
    int col2_x = x0 + col_w + gutter;
    int col3_x = x0 + 2 * (col_w + gutter);

    DrawFT8Column(col1_x, col_y, col_w, col_h, "ALL", FT8_COL_HEADER_ALL,
                  allList, allMsgs, allHead);
    DrawFT8Column(col2_x, col_y, col_w, col_h, "CQ",  FT8_COL_HEADER_CQ,
                  cqList,  cqMsgs,  cqHead);
    DrawFT8Column(col3_x, col_y, col_w, col_h, "RX",  FT8_COL_HEADER_RX,
                  rxList,  rxMsgs,  rxHead);

    /* Vertical separators between columns */
    tft.drawFastVLine(col1_x + col_w + gutter / 2, col_y, col_h, DARKGREY);
    tft.drawFastVLine(col2_x + col_w + gutter / 2, col_y, col_h, DARKGREY);
}
