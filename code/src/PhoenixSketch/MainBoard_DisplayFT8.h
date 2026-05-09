#ifndef MAINBOARD_DISPLAYFT8_H
#define MAINBOARD_DISPLAYFT8_H
#include "SDT.h"

/*
 * FT8 message pane renderer.
 *
 * Phoenix has no dedicated FT8 screen. When ED.modulation == FT8_INTERNAL,
 * MainBoard_DisplayHome.cpp::DrawSpectrumPane dispatches to the function
 * below to render decoded messages over the spectrum pane area instead of
 * the spectrum/waterfall.
 *
 * Layout: three columns (All / CQ / RX), T41-style. Each column shows the
 * most recent N decoded messages with a 2-line-per-message format:
 *   Row 1: MM:SS  freq  SNR
 *   Row 2: message text (truncated to column width)
 *
 * The pane is invalidated by DSP_FT8.cpp's DisplayAllMessages() (which
 * runs once per FT8 decode cycle in RunFT8DecoderLoop) by setting
 * PaneSpectrum.stale = true.
 */

/**
 * @brief Render the FT8 three-column message list inside the given rect.
 * @param x0     Top-left X of the render area (typically PaneSpectrum.x0).
 * @param y0     Top-left Y (typically PaneSpectrum.y0).
 * @param width  Pixels wide (typically PaneSpectrum.width = 520).
 * @param height Pixels tall (typically PaneSpectrum.height = 345).
 * @note Reads message contents from rxBuf[] / allList / cqList / rxList in
 *       DSP_FT8.cpp. Safe to call when no FT8 messages have been decoded yet
 *       (renders empty columns with headers).
 */
void DrawFT8MessageList(uint16_t x0, uint16_t y0, uint16_t width, uint16_t height);

#endif /* MAINBOARD_DISPLAYFT8_H */
