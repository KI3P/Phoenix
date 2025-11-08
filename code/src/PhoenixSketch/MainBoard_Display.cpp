/**
 * @file MainBoard_Display.cpp
 * @brief Core display infrastructure for Phoenix SDR
 *
 * This module provides the foundational display infrastructure:
 * - TFT display object and hardware initialization
 * - Pane definitions and management structures
 * - Display state routing (HOME, SPLASH, MENU screens)
 * - Core helper functions and constants
 *
 * Display rendering is now organized into specialized modules:
 * - MainBoard_DisplayHome.cpp: Home screen, splash, and parameter displays
 * - MainBoard_DisplayMenus.cpp: Menu system and navigation
 *
 * @see MainBoard_DisplayHome.cpp for pane rendering functions
 * @see MainBoard_DisplayMenus.cpp for menu system
 * @see RA8875 library documentation for low-level display control
 */

#include "SDT.h"
#include "MainBoard_Display.h"
#include <RA8875.h>

// TFT display hardware configuration
#define TFT_CS 10
#define TFT_RESET 9
RA8875 tft = RA8875(TFT_CS, TFT_RESET);

///////////////////////////////////////////////////////////////////////////////
// DISPLAY SCALE AND COLOR STRUCTURES
///////////////////////////////////////////////////////////////////////////////

struct dispSc displayScale[] = {
    { "20 dB/", 10.0, 2, 24, 1.00 },
    { "10 dB/", 20.0, 4, 10, 0.50 },
    { "5 dB/", 40.0, 8, 58, 0.25 },
    { "2 dB/", 100.0, 20, 120, 0.10 },
    { "1 dB/", 200.0, 40, 200, 0.05 }
};

extern const uint16_t gradient[] = {
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

// Shared display state variables
bool redrawParameter = true;

///////////////////////////////////////////////////////////////////////////////
// FREQUENCY HELPER FUNCTIONS
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
// HELPER FUNCTIONS
///////////////////////////////////////////////////////////////////////////////

/**
 * Calculate the bounding rectangle for a text string.
 */
void CalculateTextCorners(int16_t x0,int16_t y0,Rectangle *rect,int16_t Nchars,
                        uint16_t charWidth,uint16_t charHeight){
    rect->x0 = x0;
    rect->y0 = y0;
    rect->width = Nchars*charWidth;
    rect->height = charHeight;
}

/**
 * Fill a rectangular area with black pixels.
 */
void BlankBox(Rectangle *rect){
    tft.fillRect(rect->x0, rect->y0, rect->width, rect->height, RA8875_BLACK);
}

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
    extern Pane PaneSettings;
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
// PANE DEFINITIONS
///////////////////////////////////////////////////////////////////////////////

// Forward declarations for pane drawing functions
// (Implemented in MainBoard_DisplayHome.cpp)
void DrawVFOPanes(void);
void DrawFreqBandModPane(void);
void DrawSpectrumPane(void);
void DrawStateOfHealthPane(void);
void DrawTimePane(void);
void DrawSWRPane(void);
void DrawTXRXStatusPane(void);
void DrawSMeterPane(void);
void DrawAudioSpectrumPane(void);
void DrawSettingsPane(void);
void DrawNameBadgePane(void);

// Pane instances
Pane PaneVFOA =        {5,5,280,50,DrawVFOPanes,1};
Pane PaneVFOB =        {300,5,220,40,DrawVFOPanes,1};
Pane PaneFreqBandMod = {5,60,310,30,DrawFreqBandModPane,1};
Pane PaneSpectrum =    {5,95,520,345,DrawSpectrumPane,1};
Pane PaneStateOfHealth={5,445,260,30,DrawStateOfHealthPane,1};
Pane PaneTime =        {270,445,260,30,DrawTimePane,1};
Pane PaneSWR =         {535,15,150,40,DrawSWRPane,1};
Pane PaneTXRXStatus =  {710,20,60,30,DrawTXRXStatusPane,1};
Pane PaneSMeter =      {515,60,260,50,DrawSMeterPane,1};
Pane PaneAudioSpectrum={535,115,260,150,DrawAudioSpectrumPane,1};
Pane PaneSettings =    {535,270,260,170,DrawSettingsPane,1};
Pane PaneNameBadge =   {535,445,260,30,DrawNameBadgePane,1};

// Array of all panes for iteration
Pane* WindowPanes[NUMBER_OF_PANES] = {&PaneVFOA,&PaneVFOB,&PaneFreqBandMod,
                                    &PaneSpectrum,&PaneStateOfHealth,
                                    &PaneTime,&PaneSWR,&PaneTXRXStatus,
                                    &PaneSMeter,&PaneAudioSpectrum,&PaneSettings,
                                    &PaneNameBadge};

///////////////////////////////////////////////////////////////////////////////
// DISPLAY INITIALIZATION AND ROUTING
///////////////////////////////////////////////////////////////////////////////

/**
 * Initialize the RA8875 TFT display hardware and configure layers.
 */
void InitializeDisplay(void){
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    tft.begin(RA8875_800x480, 8, 20000000UL, 4000000UL);
    tft.setRotation(0);
    tft.useLayers(true);
    tft.layerEffect(OR);
    tft.clearMemory();
    tft.writeTo(L2);
    tft.clearMemory();
    tft.writeTo(L1);
    DrawDisplay();
}

UISm_StateId oldstate = UISm_StateId_ROOT;

/**
 * Main display rendering function - routes to appropriate screen based on UI state.
 *
 * Dispatches to specialized rendering functions in other modules:
 * - DrawSplash() in MainBoard_DisplayHome.cpp
 * - DrawHome() in MainBoard_DisplayHome.cpp
 * - DrawMainMenu() in MainBoard_DisplayMenus.cpp
 * - DrawSecondaryMenu() in MainBoard_DisplayMenus.cpp
 * - DrawParameter() in MainBoard_DisplayHome.cpp
 */
void DrawDisplay(void){
    switch (uiSM.state_id){
        case (UISm_StateId_SPLASH):{
            DrawSplash();
            break;
        }
        case (UISm_StateId_HOME):{
            DrawHome();
            break;
        }
        case (UISm_StateId_MAIN_MENU):{
            DrawMainMenu();
            break;
        }
        case (UISm_StateId_SECONDARY_MENU):{
            DrawSecondaryMenu();
            break;
        }
        case (UISm_StateId_UPDATE):{
            extern size_t primaryMenuIndex;
            extern size_t secondaryMenuIndex;
            extern struct PrimaryMenuOption primaryMenu[6];

            if (primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].action == variableOption){
                if (uiSM.vars.clearScreen)
                    redrawParameter = true;
                DrawHome();
                DrawParameter();
            } else {
                UISm_dispatch_event(&uiSM,UISm_EventId_HOME);
                void (*funcPtr)(void) = (void (*)(void))primaryMenu[primaryMenuIndex].secondary[secondaryMenuIndex].func;
                if (funcPtr != NULL) {
                    funcPtr();
                }
            }
            break;
        }
        default:
            break;
    }
    oldstate = uiSM.state_id;
}
