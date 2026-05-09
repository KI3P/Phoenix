#ifndef MAINBOARD_DISPLAYPSK31_H
#define MAINBOARD_DISPLAYPSK31_H
#include "SDT.h"

/*
 * PSK31 decoded-text pane renderer.
 *
 * Phoenix has no dedicated PSK31 screen. When ED.modulation == PSK31,
 * MainBoard_DisplayHome.cpp::DrawSpectrumPane dispatches to the function
 * below to render the PSK31 decoded-text view over the spectrum pane area
 * instead of the spectrum/waterfall.
 *
 * Layout: a status line at the top showing RX freq and pipeline state,
 * then a wrapping text panel with the most-recent decoded characters from
 * the PSK31 ring buffer (DSP_PSK31.cpp::PSK31GetText).
 */

/**
 * @brief Render the PSK31 decoded-text pane inside the given rect.
 * @param x0     Top-left X (typically PaneSpectrum.x0).
 * @param y0     Top-left Y.
 * @param width  Pixels wide (typically 520).
 * @param height Pixels tall (typically 345).
 * @note Reads decoded text via PSK31GetText() in DSP_PSK31.cpp.
 */
void DrawPSK31Pane(uint16_t x0, uint16_t y0, uint16_t width, uint16_t height);

#endif /* MAINBOARD_DISPLAYPSK31_H */
