/**
 * @file MainBoard_DisplayHome.cpp
 * @brief Home screen, splash screen, and parameter update display rendering
 *
 * This module contains all display pane rendering functions for the main
 * operating screen (HOME), startup splash screen, and parameter update overlay.
 *
 * Functions in this file follow a strict read-only pattern:
 * - Read the state of the radio from global variables
 * - Draw on the display based on that state
 * - DO NOT modify any external state variables
 *
 * @see MainBoard_Display.cpp for core display infrastructure
 * @see MainBoard_DisplayMenus.cpp for menu system
 */

#include "SDT.h"
#include "MainBoard_Display.h"
#include <RA8875.h>
#include <TimeLib.h>
#include "FreeSansBold24pt7b.h"
#include "FreeSansBold18pt7b.h"

// External references to objects and variables defined in MainBoard_Display.cpp
extern RA8875 tft;
#define SPECTRUM_REFRESH_MS 200

// Shared display state variables
bool redrawParameter = true;

void FormatFrequency(long freq, char *freqBuffer);
void UpdateSetting(uint16_t charWidth, uint16_t charHeight, uint16_t xoffset,
                   char *labelText, uint8_t NLabelChars,
                   char *valueText, uint8_t NValueChars,
                   uint16_t yoffset, bool redrawFunction, bool redrawValue);
int64_t GetCenterFreq_Hz();
int64_t GetLowerFreq_Hz();
int64_t GetUpperFreq_Hz();

///////////////////////////////////////////////////////////////////////////////
// PANE DEFINITIONS (HOME SCREEN SPECIFIC)
///////////////////////////////////////////////////////////////////////////////

static const int8_t NUMBER_OF_PANES = 13;

// Forward declarations for pane drawing functions (implemented below in this file)
static void DrawVFOPanes(void);
static void DrawFreqBandModPane(void);
static void DrawSpectrumPane(void);
static void DrawStateOfHealthPane(void);
static void DrawTimePane(void);
static void DrawSWRPane(void);
static void DrawTXRXStatusPane(void);
static void DrawSMeterPane(void);
static void DrawAudioSpectrumPane(void);
static void DrawSettingsPane(void);
static void DrawNameBadgePane(void);
static void DrawSAMOffsetPane(void);

// Pane instances
static Pane PaneVFOA =        {5,5,280,50,DrawVFOPanes,1};
static Pane PaneVFOB =        {300,5,220,40,DrawVFOPanes,1};
static Pane PaneFreqBandMod = {5,60,310,30,DrawFreqBandModPane,1};
Pane PaneSpectrum =    {5,95,520,345,DrawSpectrumPane,1}; // this one is updated by menus
static Pane PaneStateOfHealth={5,445,260,30,DrawStateOfHealthPane,1};
static Pane PaneTime =        {270,445,260,30,DrawTimePane,1};
static Pane PaneSWR =         {535,15,150,40,DrawSWRPane,1};
static Pane PaneTXRXStatus =  {710,20,60,30,DrawTXRXStatusPane,1};
static Pane PaneSMeter =      {515,60,260,50,DrawSMeterPane,1};
static Pane PaneAudioSpectrum={535,115,260,150,DrawAudioSpectrumPane,1};
static Pane PaneSettings =    {535,270,260,170,DrawSettingsPane,1};
static Pane PaneNameBadge =   {535,445,260,30,DrawNameBadgePane,1};
static Pane PaneSAMOffset =   {320,60,180,30,DrawSAMOffsetPane,1};

// Array of all panes for iteration
static Pane* WindowPanes[NUMBER_OF_PANES] = {&PaneVFOA,&PaneVFOB,&PaneFreqBandMod,
                                    &PaneSpectrum,&PaneStateOfHealth,
                                    &PaneTime,&PaneSWR,&PaneTXRXStatus,
                                    &PaneSMeter,&PaneAudioSpectrum,&PaneSettings,
                                    &PaneNameBadge, &PaneSAMOffset};

///////////////////////////////////////////////////////////////////////////////
// DISPLAY SCALE AND COLOR STRUCTURES (HOME SCREEN SPECIFIC)
///////////////////////////////////////////////////////////////////////////////
// dbText, dBScale, pixelsPerDB
struct dispSc displayScale[] = {
    { "20 dB/", 10.0, 2},
    { "10 dB/", 20.0, 4},
    { "5 dB/",  40.0, 8},
    { "2 dB/", 100.0, 20},
    { "1 dB/", 200.0, 40}
};

const uint16_t gradient[] = {
  0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9,
  0x10, 0x1F, 0x11F, 0x19F, 0x23F, 0x2BF, 0x33F, 0x3BF, 0x43F, 0x4BF,
  0x53F, 0x5BF, 0x63F, 0x6BF, 0x73F, 0x7FE, 0x7FA, 0x7F5, 0x7F0, 0x7EB,
  0x7E6, 0x7E2, 0x17E0, 0x3FE0, 0x67E0, 0x8FE0, 0xB7E0, 0xD7E0, 0xFFE0, 0xFFC0,
  0xFF80, 0xFF20, 0xFEE0, 0xFE80, 0xFE40, 0xFDE0, 0xFDA0, 0xFD40, 0xFD00, 0xFCA0,
  0xFC60, 0xFC00, 0xFBC0, 0xFB60, 0xFB20, 0xFAC0, 0xFA80, 0xFA20, 0xF9E0, 0xF980,
  0xF940, 0xF8E0, 0xF8A0, 0xF840, 0xF800, 0xF802, 0xF804, 0xF806, 0xF808, 0xF80A,
  0xF80C, 0xF80E, 0xF810, 0xF812, 0xF814, 0xF816, 0xF818, 0xF81A, 0xF81C, 0xF81E,
  0xF81E, 0xF81E, 0xF81E, 0xF83E, 0xF83E, 0xF83E, 0xF83E, 0xF85E, 0xF85E, 0xF85E,
  0xF85E, 0xF87E, 0xF87E, 0xF83E, 0xF83E, 0xF83E, 0xF83E, 0xF85E, 0xF85E, 0xF85E,
  0xF85E, 0xF87E, 0xF87E, 0xF87E, 0xF87E, 0xF87E, 0xF87E, 0xF87E, 0xF87E, 0xF87E,
  0xF87E, 0xF87E, 0xF87E, 0xF87E, 0xF88F, 0xF88F, 0xF88F
};


///////////////////////////////////////////////////////////////////////////////
// FREQUENCY HELPER FUNCTIONS (HOME SCREEN SPECIFIC)
///////////////////////////////////////////////////////////////////////////////

/**
 * Get the center frequency for the spectrum display.
 */
int64_t GetCenterFreq_Hz(){
    if (ED.spectrum_zoom == 0)
        return ED.centerFreq_Hz[ED.activeVFO];
    else
        return ED.centerFreq_Hz[ED.activeVFO] - SR[SampleRate].rate/4;
}

/**
 * Get the lower edge frequency of the spectrum display.
 */
int64_t GetLowerFreq_Hz(){
    return GetCenterFreq_Hz()-SR[SampleRate].rate/(2*(1<<ED.spectrum_zoom));
}

/**
 * Get the upper edge frequency of the spectrum display.
 */
int64_t GetUpperFreq_Hz(){
    return GetCenterFreq_Hz()+SR[SampleRate].rate/(2*(1<<ED.spectrum_zoom));
}


///////////////////////////////////////////////////////////////////////////////
// HELPER FUNCTIONS (HOME SCREEN SPECIFIC)
///////////////////////////////////////////////////////////////////////////////

/**
 * Format a frequency value as a human-readable string with thousands separators.
 */
void FormatFrequency(long freq, char *freqBuffer) {
    if (freq >= 1000000) {
        sprintf(freqBuffer, "%3ld.%03ld.%03ld", freq / (long)1000000, (freq % (long)1000000) / (long)1000, freq % (long)1000);
    } else {
        sprintf(freqBuffer, "    %03ld.%03ld", freq % (long)1000000 / (long)1000, freq % (long)1000);
    }
}

/**
 * Update a single setting display line in the settings pane.
 */
void UpdateSetting(uint16_t charWidth, uint16_t charHeight, uint16_t xoffset,
                   char *labelText, uint8_t NLabelChars,
                   char *valueText, uint8_t NValueChars,
                   uint16_t yoffset, bool redrawFunction, bool redrawValue){
    int16_t x = PaneSettings.x0 + xoffset - NLabelChars*charWidth;
    int16_t y = PaneSettings.y0 + yoffset;
    Rectangle box;
    if (redrawFunction){
        CalculateTextCorners(x,y,&box,NLabelChars,charWidth,charHeight);
        BlankBox(&box);
        tft.setCursor(x, y);
        tft.setTextColor(RA8875_WHITE);
        tft.print(labelText);
    }

    if (redrawValue){
        x = PaneSettings.x0 + xoffset + 1*charWidth;
        CalculateTextCorners(x,y,&box,NValueChars,charWidth,charHeight);
        BlankBox(&box);
        tft.setCursor(x, y);
        tft.setTextColor(RA8875_GREEN);
        tft.print(valueText);
    }
}

///////////////////////////////////////////////////////////////////////////////
// PANE-SPECIFIC HELPER FUNCTIONS
///////////////////////////////////////////////////////////////////////////////

/**
 * Convert a frequency in Hz to spectrum bin number
 */
FASTRUN int16_t FreqToBin(int64_t freq_Hz){
    int16_t val = SPECTRUM_RES*((float32_t)(freq_Hz - GetLowerFreq_Hz()) / (float32_t)(SR[SampleRate].rate/(1<<ED.spectrum_zoom)));
    if (val < 0) val = 0;
    if (val > SPECTRUM_RES) val = SPECTRUM_RES;
    return val;
}

///////////////////////////////////////////////////////////////////////////////
// VFO PANES
///////////////////////////////////////////////////////////////////////////////

// State tracking for VFO display updates
static int64_t TxRxFreq_old = 0;
static uint8_t activeVFO_old = 10;

/**
 * Render both VFO A and VFO B frequency displays.
 */
void DrawVFOPanes(void) {
    int64_t TxRxFreq = GetTXRXFreq_dHz()/100;
    if ((TxRxFreq == TxRxFreq_old) && (ED.activeVFO == activeVFO_old) &&
        (!PaneVFOA.stale) && (!PaneVFOB.stale))
        return;
    if ((ED.activeVFO != activeVFO_old) || (PaneSettings.stale)){
        PaneVFOA.stale = 1;
        PaneVFOB.stale = 1;
    } else {
        if (ED.activeVFO == 0){
            PaneVFOA.stale = 1;
            PaneVFOB.stale = 0;
        } else {
            PaneVFOA.stale = 0;
            PaneVFOB.stale = 1;
        }
    }
    TxRxFreq_old = TxRxFreq;
    activeVFO_old = ED.activeVFO;

    int16_t pixelOffset;
    char freqBuffer[15];

    if (PaneVFOA.stale){
        tft.fillRect(PaneVFOA.x0, PaneVFOA.y0, PaneVFOA.width, PaneVFOA.height, RA8875_BLACK);

        TxRxFreq = GetTXRXFreq(0);
        if (TxRxFreq < bands[ED.currentBand[0]].fBandLow_Hz ||
            TxRxFreq > bands[ED.currentBand[0]].fBandHigh_Hz) {
            tft.setTextColor(RA8875_RED);
        } else {
            tft.setTextColor(RA8875_GREEN);
        }
        if (ED.activeVFO == 1)
            tft.setTextColor(RA8875_LIGHT_GREY);
        pixelOffset = 0;
        if (TxRxFreq < 10000000L)
            pixelOffset = 13;

        tft.setFont(&FreeSansBold24pt7b);
        tft.setCursor(PaneVFOA.x0+pixelOffset, PaneVFOA.y0+10);
        FormatFrequency(TxRxFreq, freqBuffer);
        tft.print(freqBuffer);
        PaneVFOA.stale = false;
    }

    if (PaneVFOB.stale){
        tft.fillRect(PaneVFOB.x0, PaneVFOB.y0, PaneVFOB.width, PaneVFOB.height, RA8875_BLACK);

        TxRxFreq = GetTXRXFreq(1);
        if (TxRxFreq < bands[ED.currentBand[1]].fBandLow_Hz ||
            TxRxFreq > bands[ED.currentBand[1]].fBandHigh_Hz) {
            tft.setTextColor(RA8875_RED);
        } else {
            tft.setTextColor(RA8875_GREEN);
        }
        if (ED.activeVFO == 0)
            tft.setTextColor(RA8875_LIGHT_GREY);

        pixelOffset = 0;
        if (TxRxFreq < 10000000L)
            pixelOffset = 8;

        tft.setFont(&FreeSansBold18pt7b);
        tft.setCursor(PaneVFOB.x0+pixelOffset, PaneVFOB.y0+10);
        FormatFrequency(TxRxFreq, freqBuffer);
        tft.print(freqBuffer);
        PaneVFOB.stale = false;
    }
}

///////////////////////////////////////////////////////////////////////////////
// FREQUENCY/BAND/MODE PANE
///////////////////////////////////////////////////////////////////////////////

// State tracking for frequency/band/mode display
static int64_t oldCenterFreq = 0;
static int32_t oldBand = -1;
static ModeSm_StateId oldState = ModeSm_StateId_ROOT;
static ModulationType oldModulation = DCF77;

/**
 * Render the frequency, band name, and modulation mode pane.
 */
void DrawFreqBandModPane(void) {
    if ((oldCenterFreq != ED.centerFreq_Hz[ED.activeVFO]) ||
        (oldBand != ED.currentBand[ED.activeVFO]) ||
        (oldState != modeSM.state_id) ||
        (oldModulation != ED.modulation[ED.activeVFO])){
        PaneFreqBandMod.stale = true;
    }
    if (!PaneFreqBandMod.stale) return;

    oldCenterFreq = ED.centerFreq_Hz[ED.activeVFO];
    oldBand = ED.currentBand[ED.activeVFO];
    oldState = modeSM.state_id;
    oldModulation = ED.modulation[ED.activeVFO];

    tft.setFontDefault();
    tft.fillRect(PaneFreqBandMod.x0, PaneFreqBandMod.y0, PaneFreqBandMod.width, PaneFreqBandMod.height, RA8875_BLACK);

    tft.setFontScale((enum RA8875tsize)0);
    tft.setTextColor(RA8875_CYAN);
    tft.setCursor(PaneFreqBandMod.x0,PaneFreqBandMod.y0);
    tft.print("LO Freq:");
    tft.setTextColor(RA8875_LIGHT_ORANGE);
    tft.print(ED.centerFreq_Hz[ED.activeVFO]);

    tft.setTextColor(RA8875_CYAN);
    tft.setCursor(PaneFreqBandMod.x0+PaneFreqBandMod.width/2+20, PaneFreqBandMod.y0);
    tft.print(bands[ED.currentBand[ED.activeVFO]].name);

    tft.setTextColor(RA8875_GREEN);
    tft.setCursor(PaneFreqBandMod.x0+3*PaneFreqBandMod.width/4, PaneFreqBandMod.y0);

    if (modeSM.state_id == ModeSm_StateId_CW_RECEIVE) {
        tft.print("CW ");
    } else {
        tft.print("SSB ");
    }

    tft.setTextColor(RA8875_CYAN);
    switch (ED.modulation[ED.activeVFO]) {
        case LSB:
            tft.print("(LSB)");
            break;
        case USB:
            tft.print("(USB)");
            break;
        case AM:
            tft.print("(AM)");
            break;
        case SAM:
            tft.print("(SAM)");
            break;
        case IQ:
            tft.print("(IQ)");
            break;
        case DCF77:
            tft.print("(DCF77)");
            break;
    }

    PaneFreqBandMod.stale = false;
}

static float32_t ooff = 0.0;

void DrawSAMOffsetPane(void){
    if (ED.modulation[ED.activeVFO] != SAM){
        tft.fillRect(PaneSAMOffset.x0, PaneSAMOffset.y0, PaneSAMOffset.width, PaneSAMOffset.height, RA8875_BLACK);
        return;
    }
    float32_t SAMoff = GetSAMCarrierOffset();
    if (ooff != SAMoff){
        PaneSAMOffset.stale = true;
    }
    if (!PaneSAMOffset.stale) return;

    ooff = SAMoff;

    tft.setFontDefault();
    tft.fillRect(PaneSAMOffset.x0, PaneSAMOffset.y0, PaneSAMOffset.width, PaneSAMOffset.height, RA8875_BLACK);

    tft.setFontScale((enum RA8875tsize)0);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(PaneSAMOffset.x0,PaneSAMOffset.y0);
    tft.print("Err: ");
    tft.setTextColor(RA8875_LIGHT_ORANGE);
    char buff[10];
    sprintf(buff,"%2.1f",SAMoff);
    tft.print(SAMoff);

    PaneSAMOffset.stale = false;
}

///////////////////////////////////////////////////////////////////////////////
// SPECTRUM PANE FUNCTIONS
///////////////////////////////////////////////////////////////////////////////

// Spectrum display geometry constants
const uint16_t MAX_WATERFALL_WIDTH = SPECTRUM_RES;
const uint16_t SPECTRUM_LEFT_X = PaneSpectrum.x0;
const uint16_t SPECTRUM_TOP_Y  = PaneSpectrum.y0;
const uint16_t SPECTRUM_HEIGHT = 150;
const uint16_t WATERFALL_LEFT_X = SPECTRUM_LEFT_X;
const uint16_t WATERFALL_TOP_Y = (SPECTRUM_TOP_Y + SPECTRUM_HEIGHT + 5);
const uint16_t FIRST_WATERFALL_LINE = (WATERFALL_TOP_Y + 20);
const uint16_t MAX_WATERFALL_ROWS = 170;

// Spectrum display rendering parameters
float xExpand = 1.4;
uint16_t spectrum_x = 10;
uint16_t FILTER_PARAMETERS_X = PaneSpectrum.x0 + PaneSpectrum.width / 3;
uint16_t FILTER_PARAMETERS_Y = PaneSpectrum.y0+1;
#define FILTER_WIN 0x10

// Spectrum data buffers
uint16_t pixelold[MAX_WATERFALL_WIDTH];
uint16_t waterfall[MAX_WATERFALL_WIDTH];
#define NCHUNKS 4

// S-meter constants (used by DisplaydbM function within spectrum rendering)
#define SMETER_X PaneSMeter.x0+20
#define SMETER_Y PaneSMeter.y0+24
#define SMETER_BAR_LENGTH 180
#define SMETER_BAR_HEIGHT 18
uint16_t pixels_per_s = 12;

// Audio spectrum constants (used by ShowSpectrum function)
#define AUDIO_SPECTRUM_BOTTOM (PaneAudioSpectrum.y0+PaneAudioSpectrum.height-30)
#define CLIP_AUDIO_PEAK 115

/**
 * Draw Tuned Bandwidth on Spectrum Plot
 */
FASTRUN void DrawBandWidthIndicatorBar(void){
    tft.fillRect(0, SPECTRUM_TOP_Y + 20, MAX_WATERFALL_WIDTH+PaneSpectrum.x0, SPECTRUM_HEIGHT - 20, RA8875_BLACK);
    tft.writeTo(L2);
    float32_t pixel_per_khz = ((1 << ED.spectrum_zoom) * SPECTRUM_RES * 1000.0 / SR[SampleRate].rate);
    int16_t filterWidth = (int16_t)(((bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz -
                              bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz) / 1000.0) * pixel_per_khz * 1.06);
    int16_t vline = SPECTRUM_LEFT_X + FreqToBin(GetTXRXFreq(ED.activeVFO));
    switch (ED.modulation[ED.activeVFO]) {
        case LSB:
            tft.fillRect(vline - filterWidth, SPECTRUM_TOP_Y + 20, filterWidth, SPECTRUM_HEIGHT - 20, FILTER_WIN);
            break;

        case USB:
            tft.fillRect(vline, SPECTRUM_TOP_Y + 20, filterWidth, SPECTRUM_HEIGHT - 20, FILTER_WIN);
            break;

        case AM:
            tft.fillRect(vline - (filterWidth ) * 0.93, SPECTRUM_TOP_Y + 20, 2*filterWidth * 0.95, SPECTRUM_HEIGHT - 20, FILTER_WIN);
            break;

        case SAM:
            tft.fillRect(vline - (filterWidth ) * 0.93, SPECTRUM_TOP_Y + 20, 2*filterWidth * 0.95, SPECTRUM_HEIGHT - 20, FILTER_WIN);
            break;
        default:
            break;
    }
    tft.drawFastVLine(vline, SPECTRUM_TOP_Y + 20, SPECTRUM_HEIGHT-25, RA8875_CYAN);
}

/**
 * Display filter bandwidth and dB scale information on the spectrum pane.
 */
void ShowBandwidth() {
    char buff[10];
    int centerLine = (MAX_WATERFALL_WIDTH + SPECTRUM_LEFT_X) / 2;
    int pos_left;
    float32_t pixel_per_khz;

    pixel_per_khz = 0.0055652173913043;
    pos_left = centerLine + ((int)(bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz / 1000.0 * pixel_per_khz));
    if (pos_left < spectrum_x) {
        pos_left = spectrum_x;
    }

    tft.writeTo(L2);
    tft.setFontScale((enum RA8875tsize)0);
    tft.setTextColor(RA8875_WHITE);

    tft.setCursor(PaneSpectrum.x0+5, FILTER_PARAMETERS_Y);
    tft.fillRect(PaneSpectrum.x0+5,FILTER_PARAMETERS_Y,8*tft.getFontWidth(),tft.getFontHeight(),RA8875_BLACK);
    tft.print(displayScale[ED.spectrumScale].dbText);

    sprintf(buff,"%2.1fkHz",(float32_t)(bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz / 1000.0f));
    tft.setCursor(FILTER_PARAMETERS_X, FILTER_PARAMETERS_Y);
    tft.fillRect(FILTER_PARAMETERS_X,FILTER_PARAMETERS_Y,8*tft.getFontWidth(),tft.getFontHeight(),RA8875_BLACK);
    tft.print(buff);

    tft.setTextColor(RA8875_LIGHT_GREY);
    sprintf(buff,"%2.1fkHz",(float32_t)(bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz / 1000.0f));
    tft.setCursor(FILTER_PARAMETERS_X+80, FILTER_PARAMETERS_Y);
    tft.fillRect(FILTER_PARAMETERS_X+80,FILTER_PARAMETERS_Y,8*tft.getFontWidth(),tft.getFontHeight(),RA8875_BLACK);
    tft.print(buff);

}

/**
 * Draw frequency labels along the bottom of the spectrum display.
 */
void DrawFrequencyBarValue(void) {
    char txt[16];

    int bignum;
    int centerIdx;
    int pos_help;
    float disp_freq;
    float freq_calc;
    float grat;
    int centerLine = MAX_WATERFALL_WIDTH / 2 + SPECTRUM_LEFT_X;
    const static int idx2pos[2][9] = {
        { -43, 21, 50, 250, 140, 250, 232, 250, 315 },
        { -43, 21, 50, 85, 200, 200, 232, 218, 315 }
    };

    grat = (float)(SR[SampleRate].rate / 8000.0) / (float)(1 << ED.spectrum_zoom);

    tft.setTextColor(RA8875_WHITE);
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);
    tft.fillRect(0, WATERFALL_TOP_Y, MAX_WATERFALL_WIDTH + PaneSpectrum.x0 + 10, tft.getFontHeight(), RA8875_BLACK);

    freq_calc = (float)GetCenterFreq_Hz();

    if (ED.spectrum_zoom < 5) {
        freq_calc = roundf(freq_calc / 1000);
    }

    if (ED.spectrum_zoom != 0)
        centerIdx = 0;
    else
        centerIdx = -2;

    ultoa((freq_calc + (centerIdx * grat)), txt, DEC);
    disp_freq = freq_calc + (centerIdx * grat);
    bignum = (int)disp_freq;
    itoa(bignum, txt, DEC);
    tft.setTextColor(RA8875_GREEN);

    if (ED.spectrum_zoom == 0) {
        tft.setCursor(centerLine - 140, WATERFALL_TOP_Y);
    } else {
        tft.setCursor(centerLine - 20, WATERFALL_TOP_Y);
    }
    tft.print(txt);
    tft.setTextColor(RA8875_WHITE);

    for (int idx = -4; idx < 5; idx++) {
        pos_help = idx2pos[ED.spectrum_zoom < 3 ? 0 : 1][idx + 4];
        if (idx != centerIdx) {
            ultoa((freq_calc + (idx * grat)), txt, DEC);
            if (ED.spectrum_zoom == 0) {
                tft.setCursor(WATERFALL_LEFT_X + pos_help * xExpand + 40, WATERFALL_TOP_Y);
            } else {
                tft.setCursor(WATERFALL_LEFT_X + pos_help * xExpand + 40, WATERFALL_TOP_Y);
            }
            tft.print(txt);
            if (idx < 4) {
                tft.drawFastVLine((WATERFALL_LEFT_X + pos_help * xExpand + 60), WATERFALL_TOP_Y - 5, 7, RA8875_YELLOW);
            } else {
                tft.drawFastVLine((WATERFALL_LEFT_X + (pos_help + 9) * xExpand + 60), WATERFALL_TOP_Y - 5, 7, RA8875_YELLOW);
            }
        }
        if (ED.spectrum_zoom > 2 || freq_calc > 1000) {
            idx++;
        }
    }
}

// State tracking for S-meter display
static float32_t audioMaxSquaredAve = 0;

/**
 * Calculate and display the S-meter reading with dBm value.
 */
void DisplaydbM() {
    char buff[10];
    int16_t smeterPad;
    float32_t dbm;

    tft.fillRect(SMETER_X + 1, SMETER_Y + 1, SMETER_BAR_LENGTH, SMETER_BAR_HEIGHT, RA8875_BLACK);
    //dbm = 10.0 * log10f_fast(audioMaxSquaredAve) 
    //        + ED.RAtten[ED.currentBand[ED.activeVFO]]
    //        - ED.rfGainAllBands_dB
    //        + RECEIVE_POWER_OFFSET     // the notional scaling factor for the receive chain
    //        + RECEIVE_SMETER_PSD_DELTA // the level difference between the PSD and audio chains
    //        + ED.dbm_calibration[ED.currentBand[ED.activeVFO]]; // correction factor
    dbm = AudioToDBM(audioMaxSquaredAve);
    smeterPad = map(dbm, -73.0 - 9 * 6.0 /*S1*/, -73.0 /*S9*/, 0, 9 * pixels_per_s);
    smeterPad = max(0, smeterPad);
    smeterPad = min(SMETER_BAR_LENGTH, smeterPad);
    tft.fillRect(SMETER_X + 1, SMETER_Y + 2, smeterPad, SMETER_BAR_HEIGHT - 2, RA8875_RED);

    tft.setFontDefault();
    tft.setTextColor(RA8875_WHITE);
    tft.setFontScale((enum RA8875tsize)0);
    tft.fillRect(SMETER_X + 185, SMETER_Y, 80, tft.getFontHeight(), RA8875_BLACK);
    sprintf(buff,"%2.1fdBm",dbm);
    tft.setCursor(SMETER_X + 184, SMETER_Y);
    tft.print(buff);
}

// State tracking for spectrum line rendering
static int16_t x1 = 0;
static int16_t y_left;
static int16_t y_prev = pixelold[0];
static int16_t offset = (SPECTRUM_TOP_Y+SPECTRUM_HEIGHT-ED.spectrumNoiseFloor[ED.currentBand[ED.activeVFO]]);
static int16_t y_current = offset;
static int16_t smeterLength;
static bool redrawSpectrum = false;
static int16_t centerLine = (MAX_WATERFALL_WIDTH + SPECTRUM_LEFT_X) / 2;
static int16_t middleSlice = centerLine / 2;
static float32_t adjustment = 0.0f; // used by ED.spectrumFloorAuto
static float32_t newadjust = 0.0f; // used by ED.spectrumFloorAuto
static int16_t pixelmax = 0; // used by ED.spectrumFloorAuto

/**
 * Calculate vertical pixel position for a spectrum FFT bin. This is an 
 * amplitude in pixels such that -124 dBm is at 0 and higher powers are
 * positive. 
 * 
 * Power to zero point pixel location is calculated as follows:
 *   zeroPoint = (Power [dBm] - RECEIVE_POWER_OFFSET)/10 * dBScale
 *             = (-124 + 93.15)/10 * 20 = -61.74
 * 
 * PSD value at a power of -124 dBm should be roughly:
 *  (-124+RECEIVE_POWER_OFFSET)/10 = -3.087
 */
FASTRUN int16_t pixelnew(uint32_t i){
    int16_t zeroPoint = -1*(int16_t)((-124.0 - RECEIVE_POWER_OFFSET)/10.0*displayScale[ED.spectrumScale].dBScale);
    int16_t result = zeroPoint+(int16_t)(displayScale[ED.spectrumScale].dBScale*psdnew[i]);
    return result;
}

/**
 * Render the real-time spectrum line display (FASTRUN - executes from RAM).
 */
FASTRUN void ShowSpectrum(void){
    for (int j = 0; j < MAX_WATERFALL_WIDTH/NCHUNKS; j++){
        y_left = y_current;
        y_current = offset - pixelnew(x1); // offset is line on screen where -124 dBm is located
        if (ED.spectrumFloorAuto){
            if (y_current > pixelmax) pixelmax = y_current;
        }
        y_current = y_current + (int16_t)adjustment;
        if (y_current > SPECTRUM_TOP_Y+SPECTRUM_HEIGHT) y_current = SPECTRUM_TOP_Y+SPECTRUM_HEIGHT;
        if (y_current < SPECTRUM_TOP_Y) y_current = SPECTRUM_TOP_Y;

        tft.drawLine(SPECTRUM_LEFT_X+x1, y_prev, SPECTRUM_LEFT_X+x1, pixelold[x1], RA8875_BLACK);
        tft.drawLine(SPECTRUM_LEFT_X+x1, y_left, SPECTRUM_LEFT_X+x1, y_current, RA8875_YELLOW);
        y_prev = pixelold[x1];
        pixelold[x1] = y_current;
        x1++;
        if (x1 < 128) {
            tft.drawFastVLine(PaneAudioSpectrum.x0 + 2 + 2*x1+0, PaneAudioSpectrum.y0+2, AUDIO_SPECTRUM_BOTTOM-PaneAudioSpectrum.y0-3, RA8875_BLACK);
            if (audioYPixel[x1] != 0) {
                if (audioYPixel[x1] > CLIP_AUDIO_PEAK)
                    audioYPixel[x1] = CLIP_AUDIO_PEAK;
                if (x1 == middleSlice) {
                    smeterLength = y_current;
                }
                tft.drawFastVLine(PaneAudioSpectrum.x0 + 2 + 2*x1+0, AUDIO_SPECTRUM_BOTTOM - audioYPixel[x1] - 1, audioYPixel[x1] - 2, RA8875_MAGENTA);
            }
        }
        if (x1 == 128){
            audioMaxSquaredAve = .5 * GetAudioPowerMax() + .5 * audioMaxSquaredAve;
            DisplaydbM();
        }
        int test1 = -y_current + 230;
        if (test1 < 0)
            test1 = 0;
        if (test1 > 117)
            test1 = 117;
        waterfall[x1] = gradient[test1];
    }
    
    if (x1 >= MAX_WATERFALL_WIDTH){
        // if we're shifting the spectrum automatically, update the adjustment
        if (ED.spectrumFloorAuto){
            // our adjustment puts pixelmax (which is minimum power) at the bottom
            newadjust = float32_t(SPECTRUM_TOP_Y + SPECTRUM_HEIGHT - pixelmax);
            adjustment = 0.8*adjustment + 0.2*newadjust;
            pixelmax = 0;
        } else {
            adjustment = 0.0;
        }
        // In case spectrumNoiseFloor was changed
        offset = (SPECTRUM_TOP_Y+SPECTRUM_HEIGHT-ED.spectrumNoiseFloor[ED.currentBand[ED.activeVFO]]);
        
        x1 = 0;
        y_prev = pixelold[0];
        y_current = offset;
        psdupdated = false;
        redrawSpectrum = false;

        tft.BTE_move(WATERFALL_LEFT_X, FIRST_WATERFALL_LINE, MAX_WATERFALL_WIDTH, MAX_WATERFALL_ROWS - 2, WATERFALL_LEFT_X, FIRST_WATERFALL_LINE + 1, 1, 2);
        while (tft.readStatus()) ;
        tft.BTE_move(WATERFALL_LEFT_X, FIRST_WATERFALL_LINE + 1, MAX_WATERFALL_WIDTH, MAX_WATERFALL_ROWS - 2, WATERFALL_LEFT_X, FIRST_WATERFALL_LINE + 1, 2, 1);
        while (tft.readStatus()) ;
        tft.writeRect(WATERFALL_LEFT_X, FIRST_WATERFALL_LINE, MAX_WATERFALL_WIDTH, 1, waterfall);
    }
}

// State tracking for spectrum pane updates
static uint32_t oz = 8;
static int64_t ocf = 0;
static int64_t oft = 0;
static ModulationType omd = IQ;

/**
 * Render the RF spectrum display pane with waterfall.
 */
void DrawSpectrumPane(void) {
    if ((oz != ED.spectrum_zoom) ||
        (ocf != ED.centerFreq_Hz[ED.activeVFO]) ||
        (oft != ED.fineTuneFreq_Hz[ED.activeVFO]) ||
        (omd != ED.modulation[ED.activeVFO])){
        PaneSpectrum.stale = true;
    }

    if (psdupdated && redrawSpectrum){
        tft.writeTo(L1);
        ShowSpectrum();
    }

    if (!PaneSpectrum.stale) {
        tft.writeTo(L1);
        return;
    }
    oz = ED.spectrum_zoom;
    ocf = ED.centerFreq_Hz[ED.activeVFO];
    oft = ED.fineTuneFreq_Hz[ED.activeVFO];
    omd = ED.modulation[ED.activeVFO];
    tft.writeTo(L2);
    DrawFrequencyBarValue();
    DrawBandWidthIndicatorBar();
    ShowBandwidth();
    tft.drawRect(PaneSpectrum.x0-2,PaneSpectrum.y0,MAX_WATERFALL_WIDTH+5,SPECTRUM_HEIGHT,RA8875_YELLOW);
    tft.writeTo(L1);

    PaneSpectrum.stale = false;
}

///////////////////////////////////////////////////////////////////////////////
// STATE OF HEALTH PANE
///////////////////////////////////////////////////////////////////////////////


float32_t GetMicLRMS(void);
float32_t GetMicRRMS(void);
/**
 * Render the state of health pane showing DSP load and system status.
 */
void DrawStateOfHealthPane(void) {
    if ((modeSM.state_id == ModeSm_StateId_SSB_TRANSMIT) && PaneStateOfHealth.stale){
        // Print the RMS values
        tft.fillRect(PaneStateOfHealth.x0, PaneStateOfHealth.y0, PaneStateOfHealth.width, PaneStateOfHealth.height, RA8875_BLACK);
        tft.setFontDefault();
        tft.setFontScale((enum RA8875tsize)0);
        tft.setTextColor(RA8875_WHITE);

        tft.setCursor(PaneStateOfHealth.x0+15, PaneStateOfHealth.y0+5);
        tft.print("Lrms=");
        tft.print(GetMicLRMS()*1000);

        tft.setCursor(PaneStateOfHealth.x0+PaneStateOfHealth.width/2, PaneStateOfHealth.y0+5);
        tft.print("Rrms=");
        tft.print(GetMicRRMS()*1000);
        PaneStateOfHealth.stale = false;
        return;
    }

    return; // Remove this line to enable this pane in all other modes

    if (!PaneStateOfHealth.stale) return;
    if ((modeSM.state_id == ModeSm_StateId_CW_RECEIVE) && (ED.decoderFlag))
        return;
    tft.setFontDefault();
    tft.fillRect(PaneStateOfHealth.x0, PaneStateOfHealth.y0, PaneStateOfHealth.width, PaneStateOfHealth.height, RA8875_BLACK);

    char buff[10];
    int valueColor = RA8875_GREEN;
    double block_time;
    double processor_load;
    elapsed_micros_mean = elapsed_micros_sum / elapsed_micros_idx_t;

    block_time = 128.0 / (double)SR[SampleRate].rate;
    block_time = block_time * N_BLOCKS;

    block_time *= 1000000.0;
    processor_load = elapsed_micros_mean / block_time * 100;

    if (processor_load >= 100.0) {
        processor_load = 100.0;
        valueColor = RA8875_RED;
    }

    tft.setFontScale((enum RA8875tsize)0);

    float32_t CPU_temperature = TGetTemp();

    tft.setCursor(PaneStateOfHealth.x0+15, PaneStateOfHealth.y0+5);
    tft.setTextColor(RA8875_WHITE);
    tft.print("Temp:");
    tft.setTextColor(valueColor);
    sprintf(buff,"%2.1f",CPU_temperature);
    tft.print(buff);
    tft.drawCircle(PaneStateOfHealth.x0+18+tft.getFontWidth()*9,
                    PaneStateOfHealth.y0+7, 2, valueColor);

    tft.setCursor(PaneStateOfHealth.x0+PaneStateOfHealth.width/2, PaneStateOfHealth.y0+5);
    tft.setTextColor(RA8875_WHITE);
    tft.print("Load:");
    tft.setTextColor(valueColor);
    sprintf(buff,"%2.1f%%",processor_load);
    tft.print(buff);
    elapsed_micros_idx_t = 0;
    elapsed_micros_sum = 0;
    elapsed_micros_mean = 0;

    PaneStateOfHealth.stale = false;
}

///////////////////////////////////////////////////////////////////////////////
// TIME PANE
///////////////////////////////////////////////////////////////////////////////

/**
 * Render the time pane showing current time and date.
 */
void DrawTimePane(void) {
    if (!PaneTime.stale) return;
    tft.setFontDefault();
    tft.fillRect(PaneTime.x0, PaneTime.y0, PaneTime.width, PaneTime.height, RA8875_BLACK);

    char timeBuffer[15];
    char temp[5];
    temp[0] = '\0';
    timeBuffer[0] = '\0';
    strcpy(timeBuffer, MY_TIMEZONE);
    #ifdef TIME_24H
    itoa(hour(), temp, DEC);
    #else
    itoa(hourFormat12(), temp, DEC);
    #endif
    if (strlen(temp) < 2) {
        strcat(timeBuffer, "0");
    }
    strcat(timeBuffer, temp);
    strcat(timeBuffer, ":");

    itoa(minute(), temp, DEC);
    if (strlen(temp) < 2) {
        strcat(timeBuffer, "0");
    }
    strcat(timeBuffer, temp);
    strcat(timeBuffer, ":");

    itoa(second(), temp, DEC);
    if (strlen(temp) < 2) {
        strcat(timeBuffer, "0");
    }
    strcat(timeBuffer, temp);

    tft.setFontScale( (enum RA8875tsize) 1);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(PaneTime.x0, PaneTime.y0);
    tft.print(timeBuffer);

    PaneTime.stale = false;
}

///////////////////////////////////////////////////////////////////////////////
// SWR PANE
///////////////////////////////////////////////////////////////////////////////

/**
 * Render the SWR (Standing Wave Ratio) pane (currently disabled/placeholder).
 */
void DrawSWRPane(void) {
    // Currently disabled
}

///////////////////////////////////////////////////////////////////////////////
// TX/RX STATUS PANE
///////////////////////////////////////////////////////////////////////////////

// State tracking for TX/RX status display
static ModeSm_StateId oldMState = ModeSm_StateId_ROOT;

/**
 * Render the TX/RX status indicator pane.
 */
void DrawTXRXStatusPane(void) {
    TXRXType state;
    if (oldMState != modeSM.state_id){
        switch (modeSM.state_id){
            case ModeSm_StateId_CW_RECEIVE:
            case ModeSm_StateId_SSB_RECEIVE:
                PaneTXRXStatus.stale = true;
                state = RX;
                break;
            case ModeSm_StateId_SSB_TRANSMIT:
            case ModeSm_StateId_CW_TRANSMIT_KEYER_WAIT:
            case ModeSm_StateId_CW_TRANSMIT_DAH_MARK:
            case ModeSm_StateId_CW_TRANSMIT_DIT_MARK:
            case ModeSm_StateId_CW_TRANSMIT_KEYER_SPACE:
            case ModeSm_StateId_CW_TRANSMIT_MARK:
            case ModeSm_StateId_CW_TRANSMIT_SPACE:
                PaneTXRXStatus.stale = true;
                state = TX;
                break;
            default:
                PaneTXRXStatus.stale = false;
                oldMState = modeSM.state_id;
                break;
        }
    }
    if (!PaneTXRXStatus.stale) return;

    oldMState = modeSM.state_id;
    tft.fillRect(PaneTXRXStatus.x0, PaneTXRXStatus.y0, PaneTXRXStatus.width, PaneTXRXStatus.height, RA8875_BLACK);

    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_BLACK);
    switch (state) {
        case RX:{
            tft.fillRect(PaneTXRXStatus.x0, PaneTXRXStatus.y0, PaneTXRXStatus.width, PaneTXRXStatus.height, RA8875_GREEN);
            tft.setCursor(PaneTXRXStatus.x0 + 4, PaneTXRXStatus.y0 - 5);
            tft.print("REC");
            FrontPanelSetLed(0, 1);
            FrontPanelSetLed(1, 0);
            break;
        }
        case TX:{
            tft.fillRect(PaneTXRXStatus.x0, PaneTXRXStatus.y0, PaneTXRXStatus.width, PaneTXRXStatus.height, RA8875_RED);
            tft.setCursor(PaneTXRXStatus.x0 + 4, PaneTXRXStatus.y0 - 5);
            tft.print("XMT");
            FrontPanelSetLed(0, 0);
            FrontPanelSetLed(1, 1);
            break;
        }
    }
    PaneTXRXStatus.stale = false;
}

///////////////////////////////////////////////////////////////////////////////
// S-METER PANE
///////////////////////////////////////////////////////////////////////////////

// S-meter feature flag
#define TCVSDR_SMETER

/**
 * Draw the S-meter container with scale markings (one-time initialization).
 */
void DrawSMeterContainer(void) {
    int32_t i;
    tft.setFontDefault();
    tft.drawFastHLine(SMETER_X, SMETER_Y - 1, 9 * pixels_per_s, RA8875_WHITE);
    tft.drawFastHLine(SMETER_X, SMETER_Y + SMETER_BAR_HEIGHT + 2, 9 * pixels_per_s, RA8875_WHITE);

    for (i = 0; i < 10; i++) {
        tft.drawRect(SMETER_X + i * pixels_per_s, SMETER_Y - 6 - (i % 2) * 2, 2, 6 + (i % 2) * 2, RA8875_WHITE);

    }

    tft.drawFastHLine(SMETER_X + 9 * pixels_per_s, SMETER_Y - 1, SMETER_BAR_LENGTH + 2 - 9 * pixels_per_s, RA8875_GREEN);
    tft.drawFastHLine(SMETER_X + 9 * pixels_per_s, SMETER_Y + SMETER_BAR_HEIGHT + 2, SMETER_BAR_LENGTH + 2 - 9 * pixels_per_s, RA8875_GREEN);

    for (i = 1; i <= 3; i++) {
        tft.drawRect(SMETER_X + 9 * pixels_per_s + i * pixels_per_s * 10.0 / 6.0, SMETER_Y - 8 + (i % 2) * 2, 2, 8 - (i % 2) * 2, RA8875_GREEN);
    }

    tft.drawFastVLine(SMETER_X, SMETER_Y - 1, SMETER_BAR_HEIGHT + 3, RA8875_WHITE);
    tft.drawFastVLine(SMETER_X + SMETER_BAR_LENGTH + 2, SMETER_Y - 1, SMETER_BAR_HEIGHT + 3, RA8875_GREEN);

    tft.setFontScale((enum RA8875tsize)0);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(SMETER_X - 8, SMETER_Y - 25);
    tft.print("S");
    tft.setCursor(SMETER_X + 8, SMETER_Y - 25);
    tft.print("1");
    tft.setCursor(SMETER_X + 32, SMETER_Y - 25);
    tft.print("3");
    tft.setCursor(SMETER_X + 56, SMETER_Y - 25);
    tft.print("5");
    tft.setCursor(SMETER_X + 80, SMETER_Y - 25);
    tft.print("7");
    tft.setCursor(SMETER_X + 104, SMETER_Y - 25);
    tft.print("9");
    tft.setCursor(SMETER_X + 133, SMETER_Y - 25);
    tft.print("+20dB");

}

/**
 * Render the S-meter pane (container and dynamic signal level).
 */
void DrawSMeterPane(void) {
    if (!PaneSMeter.stale) return;
    tft.setFontDefault();
    tft.fillRect(PaneSMeter.x0, PaneSMeter.y0, PaneSMeter.width, PaneSMeter.height, RA8875_BLACK);
    DrawSMeterContainer();
    PaneSMeter.stale = false;
}

///////////////////////////////////////////////////////////////////////////////
// AUDIO SPECTRUM PANE
///////////////////////////////////////////////////////////////////////////////

// State tracking for audio spectrum filter markers (used by container and pane functions)
static int32_t ohi = 0;
static int32_t olo = 0;
static int32_t ofi = 0;

/**
 * Draw the audio spectrum pane container with frequency scale and filter markers.
 */
void DrawAudioSpectContainer() {
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);
    tft.setTextColor(RA8875_WHITE);
    tft.drawRect(PaneAudioSpectrum.x0, PaneAudioSpectrum.y0, PaneAudioSpectrum.width, AUDIO_SPECTRUM_BOTTOM-PaneAudioSpectrum.y0, RA8875_GREEN);
    for (int k = 0; k < 6; k++) {
        tft.drawFastVLine(PaneAudioSpectrum.x0 + k * 43.8, AUDIO_SPECTRUM_BOTTOM, 15, RA8875_GREEN);
        tft.setCursor(PaneAudioSpectrum.x0 - 4 + k * 43.8, AUDIO_SPECTRUM_BOTTOM + 16);
        tft.print(k);
        tft.print("k");
    }
    tft.writeTo(L2);
    int16_t fLo = map(olo, 0, 6000, 0, 256);
    int16_t fHi = map(ohi, 0, 6000, 0, 256);
    tft.drawFastVLine(PaneAudioSpectrum.x0 + 2 + abs(fLo), PaneAudioSpectrum.y0+2, AUDIO_SPECTRUM_BOTTOM-PaneAudioSpectrum.y0-3, RA8875_BLACK);
    tft.drawFastVLine(PaneAudioSpectrum.x0 + 2 + abs(fHi), PaneAudioSpectrum.y0+2, AUDIO_SPECTRUM_BOTTOM-PaneAudioSpectrum.y0-3, RA8875_BLACK);

    int16_t filterLoPositionMarker = map(bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz, 0, 6000, 0, 256);
    int16_t filterHiPositionMarker = map(bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz, 0, 6000, 0, 256);
    tft.drawFastVLine(PaneAudioSpectrum.x0 + 2 + abs(filterLoPositionMarker), PaneAudioSpectrum.y0+2, AUDIO_SPECTRUM_BOTTOM-PaneAudioSpectrum.y0-3, RA8875_LIGHT_GREY);
    tft.drawFastVLine(PaneAudioSpectrum.x0 + 2 + abs(filterHiPositionMarker), PaneAudioSpectrum.y0+2, AUDIO_SPECTRUM_BOTTOM-PaneAudioSpectrum.y0-3, RA8875_LIGHT_GREY);

    if (modeSM.state_id == ModeSm_StateId_CW_RECEIVE){
        int16_t fcutoffs[] = {840,1080,1320,1800,2000,0};

        int16_t fcw = map(fcutoffs[ofi], 0, 6000, 0, 256);
        tft.drawFastVLine(PaneAudioSpectrum.x0 + 2 + fcw, PaneAudioSpectrum.y0+2, AUDIO_SPECTRUM_BOTTOM-PaneAudioSpectrum.y0-3, RA8875_BLACK);
        int16_t cwFilterPosition = map(fcutoffs[ED.CWFilterIndex], 0, 6000, 0, 256);
        if (cwFilterPosition > 0)
            tft.drawFastVLine(PaneAudioSpectrum.x0 + 2 + cwFilterPosition, PaneAudioSpectrum.y0+2, AUDIO_SPECTRUM_BOTTOM-PaneAudioSpectrum.y0-3, RA8875_YELLOW);
    }

    tft.writeTo(L1);
}

/**
 * Render the audio spectrum pane showing demodulated audio frequency content.
 */
void DrawAudioSpectrumPane(void) {
    if ((ohi != bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz) ||
        (olo != bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz) ||
        (ofi != ED.CWFilterIndex))
        PaneAudioSpectrum.stale = true;
    if (!PaneAudioSpectrum.stale) return;
    tft.fillRect(PaneAudioSpectrum.x0, PaneAudioSpectrum.y0, PaneAudioSpectrum.width, PaneAudioSpectrum.height, RA8875_BLACK);
    DrawAudioSpectContainer();
    ohi = bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz;
    olo = bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz;
    ofi = ED.CWFilterIndex;
    PaneAudioSpectrum.stale = false;
}

///////////////////////////////////////////////////////////////////////////////
// SETTINGS PANE
///////////////////////////////////////////////////////////////////////////////

// Settings pane column positions (shared across all update functions)
static uint16_t column1x = 0;
static uint16_t column2x = 0;

// State tracking for volume setting display
static VolumeFunction oldVolumeFunction = InvalidVolumeFunction;
static int32_t oldVolumeSetting = 0;

void UpdateVolumeSetting(void) {
    int32_t value;
    switch (volumeFunction) {
        case AudioVolume:
            value = ED.audioVolume;
            break;
        case AGCGain:
            value = bands[ED.currentBand[ED.activeVFO]].AGC_thresh;
            break;
        case MicGain:
            value = ED.currentMicGain;
            break;
        case SidetoneVolume:
            value = (int32_t)ED.sidetoneVolume;
            break;
        default:
            value = -1;
            Debug("Invalid volume function!");
            break;
    }
    bool redrawFunction = true;
    bool redrawValue = true;
    if ((volumeFunction == oldVolumeFunction) && (!PaneSettings.stale)){
        redrawFunction = false;
        if ((value == oldVolumeSetting) && (!PaneSettings.stale)){
            redrawValue = false;
        }
    }
    if (!redrawFunction && !redrawValue) return;

    oldVolumeSetting = value;
    oldVolumeFunction = volumeFunction;

    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    char settingText[5];
    switch (volumeFunction) {
        case AudioVolume:
            sprintf(settingText,"Vol:");
            break;
        case AGCGain:
            sprintf(settingText,"AGC:");
            break;
        case MicGain:
            sprintf(settingText,"Mic:");
            break;
        case SidetoneVolume:
            sprintf(settingText,"STn:");
            break;
        default:
            sprintf(settingText,"Err:");
            Debug("Invalid volume function!");
            break;
    }
    char valueText[5];
    sprintf(valueText,"%ld",value);
    UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column1x,
                  settingText, 4,
                  valueText, 3,
                  1,redrawFunction,redrawValue);

}

// State tracking for AGC setting display
static AGCMode oldAGC = AGCInvalid;

void UpdateAGCSetting(void){
    if ((oldAGC == ED.agc) && (!PaneSettings.stale))
        return;
    oldAGC = ED.agc;

    tft.setFontScale((enum RA8875tsize)1);
    char valueText[5];
    switch (ED.agc) {
        case AGCOff:
            sprintf(valueText,"0");
            break;
        case AGCLong:
            sprintf(valueText,"L");
            break;
        case AGCSlow:
            sprintf(valueText,"S");
            break;
        case AGCMed:
            sprintf(valueText,"M");
            break;
        case AGCFast:
            sprintf(valueText,"F");
            break;
        default:
            sprintf(valueText,"E");
            Debug("Invalid AGC choice");
            break;
    }
    UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column2x,
                  (char *)"AGC:", 4,
                  valueText, 4,
                  1,true,true);

}

// State tracking for increment setting display
static int32_t oldFreqIncrement = 0;
static int64_t oldStepFineTune = 0;

void UpdateIncrementSetting(void) {
    if ((oldFreqIncrement != ED.freqIncrement) || (PaneSettings.stale)){
        tft.setFontDefault();
        tft.setFontScale((enum RA8875tsize)0);
        char valueText[8];
        sprintf(valueText,"%ld",ED.freqIncrement);
        UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column1x,
                    (char *)"Tune Inc:", 9,
                    valueText, 7,
                    PaneSettings.height/5,true,true);
        oldFreqIncrement = ED.freqIncrement;
    }

    if ((oldStepFineTune != ED.stepFineTune) || (PaneSettings.stale)){
        tft.setFontDefault();
        tft.setFontScale((enum RA8875tsize)0);
        char valueText[8];
        sprintf(valueText,"%lld",ED.stepFineTune);
        UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column2x,
                    (char *)"FT Inc:", 7,
                    valueText, 4,
                    PaneSettings.height/5,true,true);

        oldStepFineTune = ED.stepFineTune;
    }
}

// State tracking for autonotch setting display
static uint8_t oldANR_notchOn = 8;

void UpdateAutonotchSetting(void){
    if ((ED.ANR_notchOn == oldANR_notchOn) && (!PaneSettings.stale))
        return;
    oldANR_notchOn = ED.ANR_notchOn;

    tft.setFontScale((enum RA8875tsize)0);
    char valueText[4];
    if (ED.ANR_notchOn){
        sprintf(valueText,"On");
    } else {
        sprintf(valueText,"Off");
    }
    UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column2x,
        (char *)"AutoNotch:", 10,
        valueText, 3,
        PaneSettings.height/5 + tft.getFontHeight() + 1,true,true);
}

// State tracking for RF gain setting display
static float32_t oldRAtten = -70;
static float32_t oldTAtten = -70;

void UpdateRFGainSetting(void){
    if ((oldRAtten != ED.RAtten[ED.currentBand[ED.activeVFO]]) || (PaneSettings.stale)){
        oldRAtten = ED.RAtten[ED.currentBand[ED.activeVFO]];
        tft.setFontScale((enum RA8875tsize)0);
        char valueText[5];
        sprintf(valueText,"%2.1f",ED.RAtten[ED.currentBand[ED.activeVFO]]);
        UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column1x,
            (char *)"RX Atten:", 9,
            valueText, 4,
            PaneSettings.height/5 + tft.getFontHeight() + 1,true,true);
    }

    float32_t comp;
    switch (modeSM.state_id){
        case ModeSm_StateId_CW_RECEIVE:
            comp = ED.XAttenCW[ED.currentBand[ED.activeVFO]];
            break;
        case ModeSm_StateId_SSB_RECEIVE:
            comp = ED.XAttenSSB[ED.currentBand[ED.activeVFO]];
            break;
        default:
            comp = oldTAtten;
            break;
    }
    if ((oldTAtten != comp) || (PaneSettings.stale)){
        oldTAtten = comp;
        tft.setFontScale((enum RA8875tsize)0);
        char valueText[5];
        sprintf(valueText,"%2.1f",comp);
        UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column1x,
            (char *)"TX Atten:", 9,
            valueText, 4,
            PaneSettings.height/5 + 2*tft.getFontHeight() + 1,true,true);
    }
}

// State tracking for noise reduction setting display
static NoiseReductionType oldNR = NRInvalid;

void UpdateNoiseSetting(void){
    if ((oldNR == ED.nrOptionSelect) && (!PaneSettings.stale))
        return;
    oldNR = ED.nrOptionSelect;
    tft.setFontScale((enum RA8875tsize)0);
    char valueText[9];
    switch (ED.nrOptionSelect){
        case NROff:
            sprintf(valueText,"Off");
            break;
        case NRKim:
            sprintf(valueText,"Kim");
            break;
        case NRSpectral:
            sprintf(valueText,"Spec");
            break;
        case NRLMS:
            sprintf(valueText,"LMS");
            break;
        default:
            sprintf(valueText,"err");
            Debug("Invalid noise reduction type selection!");
            break;
    }
    UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column2x,
        (char *)"Noise:", 6,
        valueText, 4,
        PaneSettings.height/5 + 2*tft.getFontHeight() + 1,true,true);
}

// State tracking for zoom setting display
static uint32_t oldZoom = 10000;

void UpdateZoomSetting(void){
    if ((oldZoom == ED.spectrum_zoom) && (!PaneSettings.stale))
        return;
    oldZoom = ED.spectrum_zoom;
    tft.setFontScale((enum RA8875tsize)0);
    char valueText[4];
    sprintf(valueText,"%dx",(uint8_t)(1<<ED.spectrum_zoom));
    UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column2x,
        (char *)"Zoom:", 5,
        valueText, 3,
        PaneSettings.height/5 + 4*tft.getFontHeight() + 1,true,true);
}

// State tracking for key type setting display
static KeyTypeId oldKeyType = KeyTypeId_Invalid;
static int32_t oldWPM = -1;

void UpdateKeyTypeSetting(void){
    if ((oldKeyType == ED.keyType) && (oldWPM == ED.currentWPM) && (!PaneSettings.stale))
        return;
    oldKeyType = ED.keyType;
    oldWPM = ED.currentWPM;
    tft.setFontScale((enum RA8875tsize)0);
    char valueText[16];
    switch (ED.keyType){
        case KeyTypeId_Straight:
            sprintf(valueText,"Straight key");
            break;
        case KeyTypeId_Keyer:
            sprintf(valueText,"Keyer (%ld WPM)",ED.currentWPM);
            break;
        default:
            sprintf(valueText,"err");
            Debug("Invalid key type selection");
            break;
    }

    UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column1x,
        (char *)"Key Type:", 9,
        valueText, 15,
        PaneSettings.height/5 + 5*tft.getFontHeight() + 1,true,true);
}

// State tracking for decoder setting display
static int32_t oldDecoderFlag = -1;
static bool oldLockStatus = false;

void UpdateDecoderSetting(void){
    if ((oldDecoderFlag == ED.decoderFlag) &&
        (!PaneSettings.stale) &&
        (oldLockStatus == IsCWDecodeLocked()))
        return;
    oldDecoderFlag = ED.decoderFlag;
    tft.setFontScale((enum RA8875tsize)0);

    int16_t yoff = PaneSettings.height/5 + 3*tft.getFontHeight() + 1;
    int16_t boxw = 12;
    int16_t boxy = PaneSettings.y0+yoff+4;
    int16_t boxx = PaneSettings.x0+PaneSettings.width-boxw-4;
    char valueText[4];
    if (ED.decoderFlag){
        sprintf(valueText,"On");
    } else {
        sprintf(valueText,"Off");
        tft.fillRect(boxx,boxy,boxw,boxw,RA8875_BLACK);
    }
    UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column2x,
        (char *)"Decoder:", 8,
        valueText, 3,
        yoff,true,true);
    if (ED.decoderFlag){
        if (IsCWDecodeLocked())
            tft.fillRect(boxx,boxy,boxw,boxw,RA8875_GREEN);
        else
            tft.fillRect(boxx,boxy,boxw,boxw,RA8875_RED);
    }
}

// State tracking for DSP gain setting display
static float32_t oldrfGainAllBands_dB = -1000;

void UpdateDSPGainSetting(void){
    if ((oldrfGainAllBands_dB == ED.rfGainAllBands_dB) && (!PaneSettings.stale))
        return;
    oldrfGainAllBands_dB = ED.rfGainAllBands_dB;
    tft.setFontScale((enum RA8875tsize)0);
    char valueText[5];
    sprintf(valueText,"%2.1f",ED.rfGainAllBands_dB);
    UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column1x,
        (char *)"DSP Gain:", 9,
        valueText, 4,
        PaneSettings.height/5 + 3*tft.getFontHeight() + 1,true,true);
}

// State tracking for antenna setting display
static int32_t oldAntennaSelection = -1;

void UpdateAntennaSetting(void){
    if ((oldAntennaSelection == ED.antennaSelection[ED.currentBand[ED.activeVFO]]) && (!PaneSettings.stale))
        return;
    oldAntennaSelection = ED.antennaSelection[ED.currentBand[ED.activeVFO]];
    tft.setFontScale((enum RA8875tsize)0);
    char valueText[2];
    sprintf(valueText,"%ld",ED.antennaSelection[ED.currentBand[ED.activeVFO]]);
    UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column1x,
        (char *)"Antenna:", 8,
        valueText, 2,
        PaneSettings.height/5 + 4*tft.getFontHeight() + 1,true,true);
}

void DrawSettingsPane(void) {
    if (PaneSettings.stale){
        tft.fillRect(PaneSettings.x0, PaneSettings.y0, PaneSettings.width, PaneSettings.height, RA8875_BLACK);
        tft.setFontDefault();
        tft.setFontScale((enum RA8875tsize)1);
        column1x = 5.5*tft.getFontWidth();
        column2x = 13.5*tft.getFontWidth();
    }

    UpdateVolumeSetting();
    UpdateAGCSetting();
    UpdateIncrementSetting();
    UpdateAutonotchSetting();
    UpdateRFGainSetting();
    UpdateNoiseSetting();
    UpdateZoomSetting();
    UpdateKeyTypeSetting();
    UpdateDecoderSetting();
    UpdateDSPGainSetting();
    UpdateAntennaSetting();

    if (PaneSettings.stale){
        tft.drawRect(PaneSettings.x0, PaneSettings.y0, PaneSettings.width, PaneSettings.height, RA8875_WHITE);
        PaneSettings.stale = false;
    }
}

///////////////////////////////////////////////////////////////////////////////
// MORSE CHARACTER DISPLAY
///////////////////////////////////////////////////////////////////////////////

void MorseCharacterDisplay() {
    if ((modeSM.state_id != ModeSm_StateId_CW_RECEIVE) || (!ED.decoderFlag))
        return;
    if (!IsMorseCharacterBufferUpdated())
        return;
    tft.fillRect(PaneStateOfHealth.x0, PaneStateOfHealth.y0, PaneStateOfHealth.width, PaneStateOfHealth.height+2, RA8875_BLACK);
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(PaneStateOfHealth.x0, PaneStateOfHealth.y0);
    tft.print(GetMorseCharacterBuffer());
}

///////////////////////////////////////////////////////////////////////////////
// NAME BADGE PANE
///////////////////////////////////////////////////////////////////////////////

void DrawNameBadgePane(void) {
    if (!PaneNameBadge.stale) return;
    tft.setFontDefault();
    tft.fillRect(PaneNameBadge.x0, PaneNameBadge.y0, PaneNameBadge.width, PaneNameBadge.height, RA8875_BLACK);

    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_YELLOW);
    tft.setCursor(PaneNameBadge.x0, PaneNameBadge.y0);
    tft.print(RIGNAME);

    tft.setFontScale(0);
    tft.print(" ");
    tft.setTextColor(RA8875_RED);
    tft.setCursor(PaneNameBadge.x0+2*PaneNameBadge.width/3, PaneNameBadge.y0+tft.getFontHeight()/2);
    tft.print(VERSION);

    PaneNameBadge.stale = false;
}

///////////////////////////////////////////////////////////////////////////////
// HOME SCREEN
///////////////////////////////////////////////////////////////////////////////

// State tracking for periodic display updates
static uint32_t timer_ms = 0;
static uint32_t timerDisplay_ms = 0;

/**
 * Render the main operating screen with all 12 display panes.
 */
void DrawHome(){
    if (!((uiSM.state_id == UISm_StateId_HOME) || (uiSM.state_id == UISm_StateId_UPDATE)
        || (uiSM.state_id == UISm_StateId_CALIBRATE_RX_IQ))) // temporary case
        return;
    tft.writeTo(L1);
    if (uiSM.vars.clearScreen){
        tft.fillWindow();
        uiSM.vars.clearScreen = false;
        for (size_t i = 0; i < NUMBER_OF_PANES; i++){
            WindowPanes[i]->stale = true;
        }
    }
    if (millis()-timer_ms > 1000) {
        timer_ms = millis();
        PaneStateOfHealth.stale = true;
        PaneTime.stale = true;
    }

    if (millis()-timerDisplay_ms > SPECTRUM_REFRESH_MS) {
        timerDisplay_ms = millis();
        if (redrawSpectrum == false)
            redrawSpectrum = true;
        if (modeSM.state_id == ModeSm_StateId_SSB_TRANSMIT)
            PaneStateOfHealth.stale = true;
    }
    for (size_t i = 0; i < NUMBER_OF_PANES; i++){
        WindowPanes[i]->DrawFunction();
    }
    MorseCharacterDisplay();
}

///////////////////////////////////////////////////////////////////////////////
// SPLASH SCREEN
///////////////////////////////////////////////////////////////////////////////

void Splash(){
    tft.clearScreen(RA8875_BLACK);

    tft.setTextColor(RA8875_MAGENTA);
    tft.setCursor(50, WINDOW_HEIGHT/ 10);
    tft.setFontScale(2);
    tft.print("Experimental Phoenix Code Base");

    tft.setFontScale(3);
    tft.setTextColor(RA8875_GREEN);
    tft.setCursor(WINDOW_WIDTH / 3 - 120, WINDOW_HEIGHT / 10 + 53);
    tft.print("T41-EP SDR Radio");

    tft.setFontScale(1);
    tft.setTextColor(RA8875_YELLOW);
    tft.setCursor(WINDOW_WIDTH / 2 - (2 * tft.getFontWidth() / 2), WINDOW_HEIGHT / 3);
    tft.print("By");
    tft.setFontScale(1);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor((WINDOW_WIDTH / 2) - (38 * tft.getFontWidth() / 2) + 0, WINDOW_HEIGHT / 4 + 70);
    tft.print("           Oliver King, KI3P");
}

static bool alreadyDrawn = false;
void DrawSplash(){
    if (alreadyDrawn) return;
    Splash();
    alreadyDrawn = true;
}

///////////////////////////////////////////////////////////////////////////////
// PARAMETER UPDATE SCREEN
///////////////////////////////////////////////////////////////////////////////
extern uint8_t oavfo;
extern int32_t oband;

void DrawParameter(void){
    // Update the array variables if the active VFO or band have changed:
    if ((oavfo != ED.activeVFO) || (oband != ED.currentBand[ED.activeVFO])){
        oavfo = ED.activeVFO;
        oband = ED.currentBand[ED.activeVFO];
        UpdateArrayVariables();
        redrawParameter = true;
    }
    if (redrawParameter){
        tft.fillRect(PaneNameBadge.x0, PaneNameBadge.y0, PaneNameBadge.width, PaneNameBadge.height, RA8875_BLACK);
        tft.drawRect(PaneNameBadge.x0, PaneNameBadge.y0, PaneNameBadge.width, PaneNameBadge.height, RA8875_RED);

        tft.setFontDefault();
        tft.setFontScale((enum RA8875tsize)0.5);
        tft.setCursor(PaneNameBadge.x0+5, PaneNameBadge.y0+5);
        tft.setTextColor(RA8875_WHITE);
        tft.print(primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].label);
        tft.print(": ");
        tft.print(GetVariableValueAsString(primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].varPam));
        redrawParameter = false;
    }
}
