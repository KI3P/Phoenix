/*
Copyright (C) 2026 T41 EP Software Contributors
See Contributors.txt for list of known authors.

This file is part of Phoenix.

Phoenix is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Phoenix is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Phoenix.
If not, see <https://www.gnu.org/licenses/>.
*/

/**
 * The screen of the automatic power and SWR calibration (the AUTO branch of
 * PowerCalSm). DrawCalibratePower() hands over to DrawCalibratePowerAuto() while
 * PowerCalAutoActive(). See code/docs/PowerCalibration/AutoCalibration.md.
 */

#include "SDT.h"
#include "MainBoard_Display.h"
#include <RA8875.h>

extern RA8875 tft;
float32_t GetPowerIncrement(void);  // MainBoard_DisplayCalibration_Power.cpp

/**
 * @brief Frequency the radio transmits on now, in Hz
 * @note CW points key the carrier with the CW tone offset (TuneCWTX); SSB tone
 *       points are near the dial frequency
 */
static int64_t AutoTransmitFrequency_Hz(void){
    if (autoCal.ssbMode)
        return GetTXRXFreq(ED.activeVFO);
    return GetCWTXFreq_cHz() / 100;
}

static const int8_t NUMBER_OF_AUTO_PANES = 4;
static void DrawAutoStatusPane(void);
static void DrawAutoReadingsPane(void);
static void DrawAutoTablePane(void);
static void DrawAutoPromptPane(void);

static Pane PaneAutoStatus =   {10,45,510,80,DrawAutoStatusPane,1};
static Pane PaneAutoReadings = {10,125,510,40,DrawAutoReadingsPane,1};
static Pane PaneAutoTable =    {10,170,515,305,DrawAutoTablePane,1};
// Out to the screen's edge, so nothing is left beyond it. Prompt lines are at
// most 33 characters in the small font (8 px each)
static Pane PaneAutoPrompt =   {530,7,270,470,DrawAutoPromptPane,1};

static Pane* AutoWindowPanes[NUMBER_OF_AUTO_PANES] = {&PaneAutoStatus, &PaneAutoReadings,
                                                      &PaneAutoTable, &PaneAutoPrompt};

static char buff[100];

/**
 * @brief Clear a pane and set the small font
 */
static void StartPane(const Pane *p){
    tft.fillRect(p->x0, p->y0, p->width, p->height, RA8875_BLACK);
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);
    tft.setTextColor(RA8875_WHITE);
}

static void PrintAt(int16_t x, int16_t y, const char *text, uint16_t color = RA8875_WHITE){
    tft.setTextColor(color);
    tft.setCursor(x, y);
    tft.print(text);
    tft.setTextColor(RA8875_WHITE);
}

/**
 * @brief True in the states that measure a band, where the band, point and
 *        drive mean something
 */
static bool Measuring(void){
    switch (powerSM.state_id){
        case PowerCalSm_StateId_BAND:
        case PowerCalSm_StateId_POINT:
        case PowerCalSm_StateId_KEYDOWN:
        case PowerCalSm_StateId_METER_ENTRY:
        case PowerCalSm_StateId_METER_PAUSED:
        case PowerCalSm_StateId_COLLECT:
        case PowerCalSm_StateId_STORE:
        case PowerCalSm_StateId_REST:
        case PowerCalSm_StateId_FIT_FORWARD:
        case PowerCalSm_StateId_BAND_DONE:
            return true;
        default:
            return false;
    }
}

static const char *StandardName(void){
    switch (autoCal.stage){
        case AUTO_STAGE_LOAD:  return autoCal.level > 1 ? "wattmeter + 50 Ohm load" : "50 Ohm load";
        case AUTO_STAGE_OPEN:  return "open";
        default:               return "short";
    }
}

static uint32_t oldStatusKey = 0;
/**
 * @brief Level, standard, band, point and the drive; KEYED while transmitting
 */
static void DrawAutoStatusPane(void){
    uint32_t key = powerSM.state_id + 31u * (autoCal.stage + 7u * (autoCal.band + 17u * (autoCal.point
                   + 19u * (autoCal.keyed + 2u * (autoCal.level + 5u * (uint32_t)(autoCal.drive_dB * 4.0f + 200.0f))))));
    key += 0x9E3779B9u * autoCal.resultsVersion;
    key += 0x85EBCA6Bu * (uint32_t)(AutoTransmitFrequency_Hz() / 100) + 0xC2B2AE35u * autoCal.ssbMode;
    if (key != oldStatusKey)
        PaneAutoStatus.stale = true;
    oldStatusKey = key;
    if (!PaneAutoStatus.stale) return;
    const Pane *p = &PaneAutoStatus;
    StartPane(p);

    sprintf(buff, "Level %u   Standard: %s", autoCal.level, StandardName());
    PrintAt(p->x0, p->y0, buff);
    if (Measuring()){
        // The load has the SSB tone after its CW points
        uint32_t total = (autoCal.stage == AUTO_STAGE_LOAD) ? autoCal.nPoints + 1 : autoCal.nPoints;
        uint32_t shown = autoCal.ssbPoint ? autoCal.nPoints + 1 : autoCal.point + 1;
        if (shown > total) shown = total;
        sprintf(buff, "Band %s   point %lu of %lu%s", bands[autoCal.band <= LAST_BAND ? autoCal.band : LAST_BAND].name,
                (unsigned long)shown, (unsigned long)total, autoCal.ssbPoint ? " (SSB tone)" : "");
        PrintAt(p->x0, p->y0 + 20, buff);
        if (autoCal.ssbMode)
            sprintf(buff, "Drive: DSP gain %.1f dB", autoCal.drive_dB);
        else
            sprintf(buff, "Drive: attenuation %.1f dB", autoCal.drive_dB);
        PrintAt(p->x0, p->y0 + 40, buff);
        sprintf(buff, "Frequency: %.4f MHz%s", (double)AutoTransmitFrequency_Hz() / 1e6,
                autoCal.ssbMode ? " (SSB tone)" : "");
        PrintAt(p->x0, p->y0 + 60, buff, RA8875_YELLOW);
    } else if ((powerSM.state_id == PowerCalSm_StateId_REVIEW) || (powerSM.state_id == PowerCalSm_StateId_DONE)){
        uint32_t passed = 0, failed = 0;
        for (int32_t band = BAND_160M; band <= BAND_6M; band++){
            const AutoCalBandResult *r = GetAutoCalResult(band);
            if (!r->inSession || !r->checked) continue;
            if (r->failBits) failed++; else passed++;
        }
        sprintf(buff, "%lu bands passed, %lu failed", (unsigned long)passed, (unsigned long)failed);
        PrintAt(p->x0, p->y0 + 20, buff);
    } else if (powerSM.state_id == PowerCalSm_StateId_SETUP){
        PrintAt(p->x0, p->y0 + 20, "Setup");
    }
    sprintf(buff, "State: %s", PowerCalSm_state_id_to_string((PowerCalSm_StateId)powerSM.state_id));
    PrintAt(p->x0 + 300, p->y0 + 20, buff, RA8875_LIGHT_GREY);
    if (autoCal.keyed)
        PrintAt(p->x0 + 300, p->y0 + 40, "KEYED", RA8875_RED);
    PaneAutoStatus.stale = false;
}

static uint32_t oldReadingsN = 0xFFFFFFFF;
static float32_t oldReadingsVf = -1;
static bool oldReadingsShown = false;
/**
 * @brief The last point's bridge readings
 */
static void DrawAutoReadingsPane(void){
    // Only while measuring: elsewhere the last point belongs to a finished stage
    bool shown = Measuring() && (autoCal.lastN > 0);
    if ((oldReadingsN != autoCal.lastN) || (oldReadingsVf != autoCal.lastVf_mV) || (oldReadingsShown != shown))
        PaneAutoReadings.stale = true;
    oldReadingsN = autoCal.lastN;
    oldReadingsVf = autoCal.lastVf_mV;
    oldReadingsShown = shown;
    if (!PaneAutoReadings.stale) return;
    const Pane *p = &PaneAutoReadings;
    StartPane(p);
    if (shown){
        sprintf(buff, "Last point: Vf %.1f +/- %.1f mV   Vr %.1f +/- %.1f mV   n %lu",
                autoCal.lastVf_mV, autoCal.lastVfSD_mV, autoCal.lastVr_mV, autoCal.lastVrSD_mV,
                (unsigned long)autoCal.lastN);
        PrintAt(p->x0, p->y0, buff, RA8875_LIGHT_GREY);
    }
    PaneAutoReadings.stale = false;
}

static uint32_t oldResultsVersion = 0xFFFFFFFF;
/**
 * @brief One row per band: status, the four bridge values, the PA curve's
 *        P_sat, k and shape, and S50. The pane is 64 characters wide in the
 *        small font, which this fills, so a failed band shows its first fail
 *        reason in red in the status column; the review lists them all
 */
static void DrawAutoTablePane(void){
    if (oldResultsVersion != autoCal.resultsVersion)
        PaneAutoTable.stale = true;
    oldResultsVersion = autoCal.resultsVersion;
    if (!PaneAutoTable.stale) return;
    const Pane *p = &PaneAutoTable;
    StartPane(p);
    // Columns (characters of 8 px): band 0, status 6, o_f 17, s_f 23, o_r 29,
    // s_r 35, Psat 41, k 47, shape 53, S50 58
    sprintf(buff, "%-5s %-10s %5s %5s %5s %5s %5s %5s %4s %4s",
            "Band", "Status", "o_f", "s_f", "o_r", "s_r", "Psat", "k", "shp", "S50");
    PrintAt(p->x0, p->y0, buff, RA8875_CYAN);
    int16_t y = p->y0 + 22;
    for (int32_t band = BAND_160M; band <= BAND_6M; band++){
        const AutoCalBandResult *r = GetAutoCalResult(band);
        const char *status = "-";
        char failStatus[12];
        uint16_t color = RA8875_LIGHT_GREY;
        if (r->inSession){
            if (!r->checked){
                status = "measuring";
                color = RA8875_WHITE;
            } else if (r->failBits){
                // The first reason, and a + when there are more
                uint32_t k = AutoCalFirstFailBit(r->failBits);
                snprintf(failStatus, sizeof(failStatus), "%s%s", AutoCalFailName(k, true),
                         (r->failBits & ~(1u << k)) ? "+" : "");
                status = failStatus;
                color = RA8875_RED;
            } else {
                status = r->openOnly ? "pass(open)" : (r->shortOnly ? "pass(shrt)" : "pass");
                color = RA8875_GREEN;
            }
        }
        sprintf(buff, "%-5s %-10s", bands[band].name, status);
        PrintAt(p->x0, y, buff, color);
        if (r->hasForward){
            sprintf(buff, "%5.2f %5.2f", r->b_f + SWRCAL_OFFSET_CONSTANT_DB, r->m_f - SWRCAL_NOMINAL_SLOPE_MV_PER_DB);
            PrintAt(p->x0 + 136, y, buff);
        }
        if (r->hasReflected){
            sprintf(buff, "%5.2f %5.2f", r->b_r + SWRCAL_OFFSET_CONSTANT_DB, r->m_r - SWRCAL_NOMINAL_SLOPE_MV_PER_DB);
            PrintAt(p->x0 + 232, y, buff);
        }
        if (r->hasPACurve){
            sprintf(buff, "%5.1f %5.1f %4.2f", r->Psat_mW / 1000.0f, r->kindex,
                    r->shape > 0.0f ? r->shape : 1.0f);
            PrintAt(p->x0 + 328, y, buff);
        }
        if (r->hasS50){
            sprintf(buff, "%4.2f", r->S50);
            PrintAt(p->x0 + 464, y, buff);
        }
        y += 22;
    }
    PrintAt(p->x0, y + 4, "o: offset dB, s: slope adjustment mV/dB", RA8875_LIGHT_GREY);
    PrintAt(p->x0, y + 22, "P = Psat tanh((k x)^shp)^(1/shp) W, x = drive", RA8875_LIGHT_GREY);
    PaneAutoTable.stale = false;
}

static uint32_t oldPromptKey = 0;
static float32_t oldPromptPower = -1;
/**
 * @brief What the operator should do now
 */
static void DrawAutoPromptPane(void){
    uint32_t key = powerSM.state_id + 31u * (autoCal.stage + 7u * (autoCal.level + 5u * (ED.SWR_CouplerTurns
                   + 64u * (autoCal.failedMask != 0))));
    key += 0x9E3779B9u * autoCal.resultsVersion;
    key += 0x85EBCA6Bu * (uint32_t)(GetPowerIncrement() * 1000.0f) + 0xC2B2AE35u * GetPowerUnit();
    key += 0x27D4EB2Fu * (uint32_t)(AutoTransmitFrequency_Hz() / 100);
    if ((key != oldPromptKey) || (oldPromptPower != GetMeasuredPower()))
        PaneAutoPrompt.stale = true;
    oldPromptKey = key;
    oldPromptPower = GetMeasuredPower();
    if (!PaneAutoPrompt.stale) return;
    const Pane *p = &PaneAutoPrompt;
    StartPane(p);
    tft.setFontScale((enum RA8875tsize)1);
    PrintAt(p->x0, p->y0, "Automatic");
    tft.setFontScale((enum RA8875tsize)0);

    int16_t x = p->x0;
    int16_t y = p->y0 + 45;
    const int16_t line = 20;
    switch (powerSM.state_id){
        case PowerCalSm_StateId_SETUP:{
            sprintf(buff, "Level: %u  (volume encoder)", autoCal.level);
            PrintAt(x, y, buff, RA8875_YELLOW); y += line;
            sprintf(buff, "L44 turns: %u  (filter encoder)", ED.SWR_CouplerTurns);
            PrintAt(x, y, buff, RA8875_YELLOW); y += 2*line;
            if (autoCal.level == 1){
                PrintAt(x, y, "1: no wattmeter. Needs a 50 Ohm"); y += line;
                PrintAt(x, y, "   load, an open and a short."); y += line;
            } else if (autoCal.level == 2){
                PrintAt(x, y, "2: two wattmeter readings a band,"); y += line;
                PrintAt(x, y, "   near 2 W and at full power,"); y += line;
                PrintAt(x, y, "   then open and short."); y += line;
            } else {
                PrintAt(x, y, "3: 8 wattmeter readings a band,"); y += line;
                PrintAt(x, y, "   then open and short."); y += line;
            }
            y += line;
            PrintAt(x, y, "Fit everything directly on the"); y += line;
            PrintAt(x, y, "antenna connector: no cables."); y += 2*line;
            PrintAt(x, y, "SELECT starts, ZOOM cancels."); y += line;
            break;
        }
        case PowerCalSm_StateId_PROMPT:{
            PrintAt(x, y, "Fit on the antenna connector:"); y += line;
            PrintAt(x, y, StandardName(), RA8875_YELLOW); y += 2*line;
            PrintAt(x, y, "then press SELECT."); y += 2*line;
            if ((autoCal.level == 1) && (autoCal.stage == AUTO_STAGE_LOAD)){
                PrintAt(x, y, "Button 12 skips the load: SWR"); y += line;
                PrintAt(x, y, "only, power calibration kept."); y += line;
            }
            break;
        }
        case PowerCalSm_StateId_METER_ENTRY:{
            sprintf(buff, "Wattmeter at %.4f MHz", (double)AutoTransmitFrequency_Hz() / 1e6);
            PrintAt(x, y, buff, RA8875_YELLOW); y += line;
            PrintAt(x, y, "Read the wattmeter. Turn the"); y += line;
            PrintAt(x, y, "filter encoder until this shows"); y += line;
            PrintAt(x, y, "the same, then press SELECT."); y += 2*line;
            PrintAt(x, y, "Measured:"); y += line;
            // The large font is 16 px a character: at most 16 of them
            if (GetPowerUnit())
                sprintf(buff, "%.2f W", GetMeasuredPower());
            else
                sprintf(buff, "%.1f dBm", GetMeasuredPower());
            tft.setFontScale((enum RA8875tsize)1);
            PrintAt(x, y, buff, RA8875_YELLOW); y += 2*line;
            tft.setFontScale((enum RA8875tsize)0);
            if (GetPowerUnit())
                sprintf(buff, "Step: %g W", (double)GetPowerIncrement());
            else
                sprintf(buff, "Step: %g dB", (double)GetPowerIncrement());
            PrintAt(x, y, buff); y += line;
            PrintAt(x, y, "Button 14: W/dBm  15: step"); y += line;
            sprintf(buff, "Unkeys after %lu s.", (unsigned long)(powerSM.vars.meterTimeout_ms / 1000));
            PrintAt(x, y, buff); y += line;
            break;
        }
        case PowerCalSm_StateId_METER_PAUSED:{
            sprintf(buff, "No reading for %lu s: unkeyed.", (unsigned long)(powerSM.vars.meterTimeout_ms / 1000));
            PrintAt(x, y, buff, RA8875_YELLOW); y += 2*line;
            PrintAt(x, y, "Press SELECT to key again."); y += line;
            break;
        }
        case PowerCalSm_StateId_FIT_ALL:{
            PrintAt(x, y, "Fitting and checking..."); y += line;
            break;
        }
        case PowerCalSm_StateId_REVIEW:{
            PrintAt(x, y, "SELECT stores the bands that"); y += line;
            PrintAt(x, y, "passed. The others keep their"); y += line;
            PrintAt(x, y, "previous calibration."); y += 2*line;
            if (autoCal.failedMask){
                PrintAt(x, y, "Button 14 repeats the session", RA8875_YELLOW); y += line;
                PrintAt(x, y, "for the failed bands.", RA8875_YELLOW); y += 2*line;
            }
            PrintAt(x, y, "ZOOM discards everything."); y += 2*line;
            // Why each band failed, as far as the pane goes
            const int16_t last = p->y0 + p->height - line;
            for (int32_t band = BAND_160M; band <= BAND_6M; band++){
                const AutoCalBandResult *r = GetAutoCalResult(band);
                if (!r->inSession || !r->checked || !r->failBits) continue;
                for (uint32_t k = 0; k < AUTO_FAIL_BIT_COUNT; k++){
                    if (!(r->failBits & (1u << k))) continue;
                    if (y > last) break;
                    sprintf(buff, "%-4s %s", bands[band].name, AutoCalFailName(k, false));
                    PrintAt(x, y, buff, RA8875_RED); y += line;
                }
            }
            break;
        }
        case PowerCalSm_StateId_DONE:{
            PrintAt(x, y, "Calibration stored.", RA8875_GREEN); y += 2*line;
            PrintAt(x, y, "HOME leaves calibration."); y += line;
            PrintAt(x, y, "ZOOM returns to the manual"); y += line;
            PrintAt(x, y, "power calibration."); y += line;
            break;
        }
        default:{
            PrintAt(x, y, "Measuring: leave the connector"); y += line;
            PrintAt(x, y, "alone."); y += 2*line;
            PrintAt(x, y, "ZOOM aborts and unkeys."); y += line;
            break;
        }
    }
    PaneAutoPrompt.stale = false;
}

/**
 * @brief Screen of the automatic power and SWR calibration
 * @note Called by DrawCalibratePower() while PowerCalAutoActive()
 */
void DrawCalibratePowerAuto(void){
    if (uiSM.vars.clearScreen){
        tft.writeTo(L2);
        tft.fillWindow();
        tft.writeTo(L1);
        tft.fillWindow();
        uiSM.vars.clearScreen = false;
        tft.setFontDefault();
        tft.setFontScale((enum RA8875tsize)1);
        tft.setCursor(10,10);
        tft.print("Power and SWR calibration");
        for (size_t i = 0; i < NUMBER_OF_AUTO_PANES; i++){
            AutoWindowPanes[i]->stale = true;
        }
    }
    for (size_t i = 0; i < NUMBER_OF_AUTO_PANES; i++){
        AutoWindowPanes[i]->DrawFunction();
    }
}
