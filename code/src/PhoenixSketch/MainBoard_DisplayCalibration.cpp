/**
 * @file MainBoard_DisplayCalibration.cpp
 * @brief Calibration screens for Phoenix SDR radio calibration
 *
 * @see MainBoard_Display.h for menu structure definitions
 * @see MainBoard_Display.cpp for display infrastructure
 */

#include "SDT.h"
#include "MainBoard_Display.h"
#include <RA8875.h>

// External references to objects defined in MainBoard_Display.cpp
extern RA8875 tft;

void DrawCalibrateFrequency(void){
    if (uiSM.vars.clearScreen){
        Debug("Entry to CALIBRATE_FREQUENCY state");
        tft.writeTo(L2);
        tft.fillWindow();
        tft.writeTo(L1);
        tft.fillWindow();
        uiSM.vars.clearScreen = false;
    }
    tft.setCursor(10,10);
    tft.print("Frequency calibration");
}

extern struct dispSc displayScale[];

/**
 * Calculate vertical pixel position for a spectrum FFT bin.
 */
FASTRUN int16_t pixeln(uint32_t i){
    int16_t result = displayScale[0].baseOffset +
                    20 + // pixeloffset
                    (int16_t)(displayScale[0].dBScale * psdnew[i]);
    return result;
}

static const uint16_t MAX_WATERFALL_WIDTH = SPECTRUM_RES;
static const uint16_t SPECTRUM_LEFT_X = 5;
static const uint16_t SPECTRUM_TOP_Y  = 95;
static const uint16_t SPECTRUM_HEIGHT = 150;

static uint16_t pixelold[MAX_WATERFALL_WIDTH];
static int16_t x1 = 0;
static int16_t y_left;
static int16_t y_prev = pixelold[0];
static int16_t offset = (SPECTRUM_TOP_Y+SPECTRUM_HEIGHT-30);
static int16_t y_current = offset;
#define WIN_WIDTH 20
#define DARK_RED tft.Color565(64, 0, 0)
static float32_t sideband_separation = 0.0;
char buff[10];
int16_t centerLine = (MAX_WATERFALL_WIDTH + SPECTRUM_LEFT_X) / 2;

FASTRUN void PlotSpectrum(void){
    offset = (SPECTRUM_TOP_Y+SPECTRUM_HEIGHT-ED.spectrumNoiseFloor[ED.currentBand[ED.activeVFO]]);

    x1 = MAX_WATERFALL_WIDTH/4-WIN_WIDTH/2;
    if (bands[ED.currentBand[ED.activeVFO]].mode == LSB)
        tft.fillRect(SPECTRUM_LEFT_X+x1,SPECTRUM_TOP_Y,WIN_WIDTH,SPECTRUM_HEIGHT,DARK_RED);
    else
        tft.fillRect(SPECTRUM_LEFT_X+x1,SPECTRUM_TOP_Y,WIN_WIDTH,SPECTRUM_HEIGHT,RA8875_BLUE);
    float32_t lowerSBmax = -1000.0;
    for (int j = 0; j < WIN_WIDTH; j++){
        y_left = y_current;
        y_current = offset - pixeln(x1);
        if (y_current > SPECTRUM_TOP_Y+SPECTRUM_HEIGHT) y_current = SPECTRUM_TOP_Y+SPECTRUM_HEIGHT;
        if (y_current < SPECTRUM_TOP_Y) y_current = SPECTRUM_TOP_Y;
        tft.drawLine(SPECTRUM_LEFT_X+x1, y_prev, SPECTRUM_LEFT_X+x1, pixelold[x1], RA8875_BLACK);
        tft.drawLine(SPECTRUM_LEFT_X+x1, y_left, SPECTRUM_LEFT_X+x1, y_current, RA8875_YELLOW);
        y_prev = pixelold[x1];
        pixelold[x1] = y_current;
        if (psdnew[x1] > lowerSBmax) lowerSBmax = psdnew[x1]; 
        x1++;
    }

    x1 = MAX_WATERFALL_WIDTH*3/4-WIN_WIDTH/2;
    float32_t upperSBmax = -1000.0;
    if (bands[ED.currentBand[ED.activeVFO]].mode == LSB)
        tft.fillRect(SPECTRUM_LEFT_X+x1,SPECTRUM_TOP_Y,WIN_WIDTH,SPECTRUM_HEIGHT,RA8875_BLUE);
    else
        tft.fillRect(SPECTRUM_LEFT_X+x1,SPECTRUM_TOP_Y,WIN_WIDTH,SPECTRUM_HEIGHT,DARK_RED);
    for (int j = 0; j < WIN_WIDTH; j++){
        y_left = y_current;
        y_current = offset - pixeln(x1);
        if (y_current > SPECTRUM_TOP_Y+SPECTRUM_HEIGHT) y_current = SPECTRUM_TOP_Y+SPECTRUM_HEIGHT;
        if (y_current < SPECTRUM_TOP_Y) y_current = SPECTRUM_TOP_Y;

        tft.drawLine(SPECTRUM_LEFT_X+x1, y_prev, SPECTRUM_LEFT_X+x1, pixelold[x1], RA8875_BLACK);
        tft.drawLine(SPECTRUM_LEFT_X+x1, y_left, SPECTRUM_LEFT_X+x1, y_current, RA8875_YELLOW);
        y_prev = pixelold[x1];
        pixelold[x1] = y_current;
        if (psdnew[x1] > upperSBmax) upperSBmax = psdnew[x1]; 
        x1++;
    }

    if (bands[ED.currentBand[ED.activeVFO]].mode == LSB)
        sideband_separation = (upperSBmax - lowerSBmax)*10;
    else
        sideband_separation = (lowerSBmax - upperSBmax)*10;
    sprintf(buff,"%2.1fdB",sideband_separation);
    tft.setCursor(centerLine - 50,  SPECTRUM_TOP_Y+SPECTRUM_HEIGHT/2);
    tft.fillRect(centerLine - 50,  SPECTRUM_TOP_Y+SPECTRUM_HEIGHT/2, 6*tft.getFontWidth(),tft.getFontHeight(),RA8875_BLACK);
    tft.print(buff);

    psdupdated = false;
}

void DrawCalibrateRXIQ(void){
    if (uiSM.vars.clearScreen){
        Debug("Entry to CALIBRATE_RXIQ state");
        tft.writeTo(L2);
        tft.fillWindow();
        tft.writeTo(L1);
        tft.fillWindow();
        uiSM.vars.clearScreen = false;
        tft.setCursor(10,10);
        tft.print("Receive IQ calibration");
        tft.drawRect(5-2,95,MAX_WATERFALL_WIDTH+5,SPECTRUM_HEIGHT,RA8875_YELLOW);

        tft.setCursor(centerLine - 50,  SPECTRUM_TOP_Y+SPECTRUM_HEIGHT/2-40);
        tft.print("Delta:");

    }
    if (psdupdated){
        PlotSpectrum();
    }
}

void DrawCalibrateTXIQ(void){
    if (uiSM.vars.clearScreen){
        Debug("Entry to CALIBRATE_TXIQ state");
        tft.writeTo(L2);
        tft.fillWindow();
        tft.writeTo(L1);
        tft.fillWindow();
        uiSM.vars.clearScreen = false;
    }
    tft.setCursor(10,10);
    tft.print("Transmit IQ calibration");
}

void DrawCalibratePower(void){
    if (uiSM.vars.clearScreen){
        Debug("Entry to CALIBRATE_POWER state");
        tft.writeTo(L2);
        tft.fillWindow();
        tft.writeTo(L1);
        tft.fillWindow();
        uiSM.vars.clearScreen = false;
    }
    tft.setCursor(10,10);
    tft.print("Power calibration");
}
