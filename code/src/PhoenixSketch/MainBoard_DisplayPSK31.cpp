#include "SDT.h"
#include "MainBoard_Display.h"
#include "MainBoard_DisplayPSK31.h"
#include "DSP_PSK31.h"
#include <RA8875.h>  // for RA8875 type and RA8875_* color constants

extern RA8875 tft;  /* defined in MainBoard_Display.cpp */

/*
 * PSK31 decoded-text pane.
 *
 * Top status row: RX:<freq> Hz   (and TXT:<count> as a rolling chars-decoded
 * indicator).
 *
 * Body: word-wrapped most-recent characters from PSK31GetText(). The buffer
 * size (256 chars) is more than enough to fill the pane; extra chars scroll
 * off the top.
 *
 * Render strategy:
 *   - Pull the entire ring as a flat string via PSK31GetText.
 *   - Compute how many chars fit per row at the default font.
 *   - Compute how many rows fit in (height - status row).
 *   - Show the trailing N rows worth of text (the most recent).
 *   - Newest character is at the right end of the bottom row.
 */

#define PSK31_PANE_FG_HEADER  RA8875_YELLOW
#define PSK31_PANE_FG_TEXT    RA8875_WHITE
#define PSK31_PANE_FG_SEP     DARKGREY
#define PSK31_PANE_FG_LABEL   RA8875_CYAN
#define PSK31_PANE_FG_VALUE   RA8875_WHITE
#define PSK31_PANE_FG_OK      RA8875_GREEN
#define PSK31_PANE_FG_WARN    RA8875_YELLOW
#define PSK31_PANE_FG_BAD     RA8875_RED

static void DrawPSK31StatusLine(int x, int y, int w, int h,
                                int charsCached, bool statusModeActive) {
    tft.fillRect(x, y, w, h, RA8875_BLACK);

    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);

    char line[80];
    if (statusModeActive) {
        snprintf(line, sizeof(line),
                 "PSK31  RX:%4d Hz  [Decoder Status]",
                 (int)psk31RxFreq);
    } else {
        snprintf(line, sizeof(line),
                 "PSK31  RX:%4d Hz  Decoded:%d chars",
                 (int)psk31RxFreq,
                 charsCached);
    }

    tft.setTextColor(PSK31_PANE_FG_HEADER);
    tft.setCursor(x + 2, y + 1);
    tft.print(line);

    tft.drawFastHLine(x, y + h - 1, w, PSK31_PANE_FG_SEP);
}

/*
 * Decoder-status sub-page renderer.
 *
 * Displays live Gardner clock-recovery telemetry. The user toggles into this
 * view by pressing the fine-tune encoder push switch while in PSK31 mode.
 *
 * Layout (under the status line):
 *   mu      :  X.XXX  symbols      [bar 0..T]
 *   dphase  : +X.XXX  rad          [bar -PI..+PI]
 *   |err|   :  X.XXX  smoothed     [bar 0..1]
 *   "Push fine-tune again to return to text"
 *
 * The bars give an at-a-glance sense of where each value sits within its
 * expected range. Healthy decode looks like: mu near 0, dphase concentrated
 * at 0 or +/-PI, |err| trending small.
 */
static void DrawHorizBar(int x, int y, int w, int h,
                         float v, float vmin, float vmax, uint16_t color) {
    /* Frame */
    tft.drawRect(x, y, w, h, PSK31_PANE_FG_SEP);
    /* Background */
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, RA8875_BLACK);

    /* Marker position. Clamp v into range. */
    if (v < vmin) v = vmin;
    if (v > vmax) v = vmax;
    float frac = (v - vmin) / (vmax - vmin);
    int marker = (int)(frac * (float)(w - 4));
    if (marker < 0) marker = 0;
    if (marker > w - 4) marker = w - 4;

    /* Center tick (the "0" reference for signed bars). */
    int center = (w - 2) / 2;
    tft.drawFastVLine(x + 1 + center, y + 1, h - 2, DARKGREY);

    /* Filled portion from center to marker for signed bars; from 0 for
     * unsigned. We assume signed if vmin < 0 < vmax. */
    if (vmin < 0.0f && vmax > 0.0f) {
        int from = center;
        int to = marker;
        if (to < from) { int t = from; from = to; to = t; }
        if (to > from) {
            tft.fillRect(x + 1 + from, y + 2,
                         (to - from) + 1, h - 4, color);
        }
    } else {
        if (marker > 0) {
            tft.fillRect(x + 2, y + 2, marker, h - 4, color);
        }
    }
}

static void DrawPSK31StatusSubpage(int x0, int y0, int width, int height,
                                   int charW, int charH) {
    /* Pull telemetry. */
    float mu = 0.0f, dphase = 0.0f, err = 0.0f;
    PSK31GetDecoderStatus(&mu, &dphase, &err);

    /* PSK31 baseband samples-per-symbol from DSP_PSK31.cpp.
     * Gardner mu range is [0, sps); for the bar we treat mu as offset from
     * mid-symbol so the display centers on the converged value. */
    const float sps = 8.0f;          /* PSK31_SAMPLES_PER_SYMBOL */
    float mu_centered = mu - (sps * 0.5f);
    /* err is already a smoothed magnitude; bound to [0, 1] for the bar. */

    int rowH = charH + 6;
    int row0 = y0 + 4;
    int labelW = 9 * charW;          /* 8 chars + space */
    int valueW = 14 * charW;
    int barX = x0 + 4 + labelW + valueW + 6;
    int barW = (x0 + width - 4) - barX;
    if (barW < 40) barW = 40;
    int barH = charH;

    char buf[40];

    /* --- Row 1: mu --- */
    tft.setTextColor(PSK31_PANE_FG_LABEL);
    tft.setCursor(x0 + 4, row0);
    tft.print("mu     :");
    snprintf(buf, sizeof(buf), " %+0.3f sym", (double)mu);
    tft.setTextColor(PSK31_PANE_FG_VALUE);
    tft.setCursor(x0 + 4 + labelW, row0);
    tft.print(buf);
    /* mu signed display: -sps/2 .. +sps/2 */
    DrawHorizBar(barX, row0, barW, barH,
                 mu_centered, -(sps * 0.5f), (sps * 0.5f),
                 PSK31_PANE_FG_OK);

    /* --- Row 2: dphase --- */
    int row1 = row0 + rowH;
    tft.setTextColor(PSK31_PANE_FG_LABEL);
    tft.setCursor(x0 + 4, row1);
    tft.print("dphase :");
    snprintf(buf, sizeof(buf), " %+0.3f rad", (double)dphase);
    tft.setTextColor(PSK31_PANE_FG_VALUE);
    tft.setCursor(x0 + 4 + labelW, row1);
    tft.print(buf);
    /* dphase spans -PI..+PI */
    DrawHorizBar(barX, row1, barW, barH,
                 dphase, -(float)PI, (float)PI,
                 PSK31_PANE_FG_WARN);

    /* --- Row 3: |err| smoothed --- */
    int row2 = row1 + rowH;
    tft.setTextColor(PSK31_PANE_FG_LABEL);
    tft.setCursor(x0 + 4, row2);
    tft.print("|err|  :");
    snprintf(buf, sizeof(buf), " %0.3f smth", (double)err);
    tft.setTextColor(PSK31_PANE_FG_VALUE);
    tft.setCursor(x0 + 4 + labelW, row2);
    tft.print(buf);
    /* err is unsigned in [0, 1] after the IIR smoothing of the saturated err. */
    uint16_t errColor = PSK31_PANE_FG_OK;
    if (err > 0.20f) errColor = PSK31_PANE_FG_WARN;
    if (err > 0.50f) errColor = PSK31_PANE_FG_BAD;
    DrawHorizBar(barX, row2, barW, barH,
                 err, 0.0f, 1.0f, errColor);

    /* --- Help line --- */
    int rowHelp = row2 + rowH + 8;
    tft.setTextColor(DARKGREY);
    tft.setCursor(x0 + 4, rowHelp);
    tft.print("Push fine-tune again to return to decoded text.");

    /* Note line about expected values. */
    int rowNote = rowHelp + rowH;
    tft.setTextColor(DARKGREY);
    tft.setCursor(x0 + 4, rowNote);
    tft.print("Healthy: mu~0, dphase concentrates at 0 or +/-PI, |err| small.");

    (void)height;  /* status sub-page intentionally doesn't fill the pane */
}

void DrawPSK31Pane(uint16_t x0, uint16_t y0, uint16_t width, uint16_t height) {
    /* Erase entire region first (covers the spectrum -> PSK31 transition). */
    tft.fillRect(x0, y0, width, height, RA8875_BLACK);

    /* Status line at the top */
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);
    int charW = tft.getFontWidth();
    int charH = tft.getFontHeight();
    if (charW <= 0) charW = 8;
    if (charH <= 0) charH = 16;

    int statusH = charH + 4;
    if (statusH > (int)height / 4) statusH = height / 4;

    /* Pull the decoded text */
    static char buf[PSK31_TEXT_BUFFER_SIZE + 1];
    int textLen = PSK31GetText(buf, sizeof(buf));

    bool statusMode = PSK31IsStatusPageActive();
    DrawPSK31StatusLine(x0, y0, width, statusH, textLen, statusMode);

    /* If the decoder-status sub-page is active, render that instead of
     * decoded text. Toggled via FINETUNE_BUTTON in PSK31 mode. */
    if (statusMode) {
        DrawPSK31StatusSubpage(x0, y0 + statusH + 4, width, height - statusH - 8,
                               charW, charH);
        return;
    }

    /* Body region for text */
    int bodyX = x0 + 4;
    int bodyY = y0 + statusH + 4;
    int bodyW = width - 8;
    int bodyH = height - statusH - 8;
    int rowsFit = bodyH / charH;
    int colsFit = bodyW / charW;
    if (rowsFit < 1) rowsFit = 1;
    if (colsFit < 1) colsFit = 1;

    if (textLen <= 0) {
        tft.setTextColor(DARKGREY);
        tft.setCursor(bodyX, bodyY);
        tft.print("(no decoded text yet)");
        return;
    }

    /* Word-wrap the text into lines of <= colsFit characters. We don't break
     * mid-word at the cost of slightly ragged right edges; hard-wrap at
     * column limit if a single token is wider than the line. CR/LF in the
     * text force a new line. */
    static char lines[40][96];  /* max 40 visible rows, 96 chars per line */
    int lineIdx = 0;
    int colIdx = 0;
    lines[0][0] = '\0';

    int maxRows  = rowsFit < 40 ? rowsFit : 40;
    int maxCols  = colsFit < 95 ? colsFit : 95;

    for (int i = 0; i < textLen; i++) {
        char c = buf[i];
        if (c == '\n' || c == '\r') {
            lines[lineIdx][colIdx] = '\0';
            lineIdx++;
            colIdx = 0;
            if (lineIdx >= 40) {
                /* shift up */
                for (int s = 1; s < 40; s++) {
                    memcpy(lines[s-1], lines[s], sizeof(lines[s]));
                }
                lineIdx = 39;
                lines[39][0] = '\0';
            } else {
                lines[lineIdx][0] = '\0';
            }
            continue;
        }
        if (colIdx >= maxCols) {
            lines[lineIdx][colIdx] = '\0';
            lineIdx++;
            colIdx = 0;
            if (lineIdx >= 40) {
                for (int s = 1; s < 40; s++) {
                    memcpy(lines[s-1], lines[s], sizeof(lines[s]));
                }
                lineIdx = 39;
                lines[39][0] = '\0';
            } else {
                lines[lineIdx][0] = '\0';
            }
        }
        lines[lineIdx][colIdx++] = c;
        lines[lineIdx][colIdx] = '\0';
    }

    /* Render the trailing maxRows lines */
    int firstLine = (lineIdx + 1 > maxRows) ? (lineIdx + 1 - maxRows) : 0;
    int rowsToShow = (lineIdx + 1) - firstLine;
    if (rowsToShow > maxRows) rowsToShow = maxRows;

    tft.setTextColor(PSK31_PANE_FG_TEXT);
    for (int r = 0; r < rowsToShow; r++) {
        tft.setCursor(bodyX, bodyY + r * charH);
        tft.print(lines[firstLine + r]);
    }
}
