#include "SDT.h"
#include <RA8875.h>
#include <TimeLib.h>                   // Part of Teensy Time library
#include "FreeSansBold24pt7b.h"
#include "FreeSansBold18pt7b.h"

/*
 * Functions in this file do two things only: 
 * 1) They read the state of the radio, 
 * 2) They draw on the display.
 * They do not change the value of any variable that is declared outside of this file.
 */

//#include "phoenix.cpp"

// pins for the display CS and reset
#define TFT_CS 10
#define TFT_RESET 9 // any pin or nothing!
RA8875 tft = RA8875(TFT_CS, TFT_RESET);  // Instantiate the display object

// screen size
#define WINDOW_WIDTH    800
#define WINDOW_HEIGHT   480
#define NUMBER_OF_PANES 12
#define SPECTRUM_REFRESH_MS 200

struct dispSc {
    const char *dbText;
    float32_t dBScale;
    uint16_t pixelsPerDB;
    uint16_t baseOffset;
    float32_t offsetIncrement;
};

struct dispSc displayScale[] = // *dbText,dBScale, pixelsPerDB, baseOffset, offsetIncrement
    {
        { "20 dB/", 10.0, 2, 24, 1.00 },
        { "10 dB/", 20.0, 4, 10, 0.50 },
        { "5 dB/", 40.0, 8, 58, 0.25 },
        { "2 dB/", 100.0, 20, 120, 0.10 },
        { "1 dB/", 200.0, 40, 200, 0.05 }
    };

const uint16_t gradient[] = {  // Color array for waterfall background
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

static bool redrawSpectrum = false;
static float32_t audioMaxSquaredAve = 0;

int64_t GetCenterFreq_Hz(){
    if (ED.spectrum_zoom == 0)
        return ED.centerFreq_Hz[ED.activeVFO];
    else
        return ED.centerFreq_Hz[ED.activeVFO] - SR[SampleRate].rate/4;
}

int64_t GetLowerFreq_Hz(){
    return GetCenterFreq_Hz()-SR[SampleRate].rate/(2*(1<<ED.spectrum_zoom));
}

int64_t GetUpperFreq_Hz(){
    return GetCenterFreq_Hz()+SR[SampleRate].rate/(2*(1<<ED.spectrum_zoom));
}

struct Pane {
    uint16_t x0;     /**< top left corner, horizontal coordinate */
    uint16_t y0;     /**< top left corner, vertical coordinate */
    uint16_t width;  /**< horizontal left to right size */
    uint16_t height; /**< vertical top to bottom size */
    void (*DrawFunction)(void); /**< Pointer to function that draws the pane */
    bool stale;      /**< true if the pane information needs to be updated */
};

struct Rectangle {
    uint16_t x0;     /**< top left corner, horizontal coordinate */
    uint16_t y0;     /**< top left corner, vertical coordinate */
    uint16_t width;  /**< horizontal left to right size */
    uint16_t height; /**< vertical top to bottom size */  
};

void CalculateTextCorners(int16_t x0,int16_t y0,Rectangle *rect,int16_t Nchars,
                        uint16_t charWidth,uint16_t charHeight){
    rect->x0 = x0;
    rect->y0 = y0;
    rect->width = Nchars*charWidth;
    rect->height = charHeight;
}

void BlankBox(Rectangle *rect){
    tft.fillRect(rect->x0, rect->y0, rect->width, rect->height, RA8875_BLACK);
}

void DrawVFOPanes(void);
void DrawFreqBandModPane(void);
void DrawSpectrumPane(void);
//void DrawWaterfallPane(void);
void DrawStateOfHealthPane(void);
void DrawTimePane(void);
void DrawSWRPane(void);
void DrawTXRXStatusPane(void);
void DrawSMeterPane(void);
void DrawAudioSpectrumPane(void);
void DrawSettingsPane(void);
void DrawNameBadgePane(void);

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


// An array of pointers to all the panes, useful for iterating over
Pane* WindowPanes[NUMBER_OF_PANES] = {&PaneVFOA,&PaneVFOB,&PaneFreqBandMod,
                                    &PaneSpectrum,&PaneStateOfHealth,
                                    &PaneTime,&PaneSWR,&PaneTXRXStatus,
                                    &PaneSMeter,&PaneAudioSpectrum,&PaneSettings,
                                    &PaneNameBadge};

void FormatFrequency(long freq, char *freqBuffer) {
    if (freq >= 1000000) {
        sprintf(freqBuffer, "%3ld.%03ld.%03ld", freq / (long)1000000, (freq % (long)1000000) / (long)1000, freq % (long)1000);
    } else {
        sprintf(freqBuffer, "    %03ld.%03ld", freq % (long)1000000 / (long)1000, freq % (long)1000);
    }
}

static int64_t TxRxFreq_old = 0;   // invalid at init, force a screen update
static uint8_t activeVFO_old = 10; // invalid at init, force a screen update
/**
 * Update both VFO panes
 */
void DrawVFOPanes(void) {
    int64_t TxRxFreq = GetTXRXFreq_dHz()/100;
    if ((TxRxFreq == TxRxFreq_old) && (ED.activeVFO == activeVFO_old) && 
        (!PaneVFOA.stale) && (!PaneVFOB.stale))
        return; // don't update
    // Which VFO needs to be updated?
    if ((ED.activeVFO != activeVFO_old) || (PaneSettings.stale)){ // both!
        PaneVFOA.stale = 1;
        PaneVFOB.stale = 1;
    } else {
        // the frequency changed, so update the active VFO
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
        // Update VFO A pane
        // Black out the prior data
        tft.fillRect(PaneVFOA.x0, PaneVFOA.y0, PaneVFOA.width, PaneVFOA.height, RA8875_BLACK);
        // Draw a box around the borders
        //tft.drawRect(PaneVFOA.x0, PaneVFOA.y0, PaneVFOA.width, PaneVFOA.height, RA8875_YELLOW);

        TxRxFreq = GetTXRXFreq(0);
        if (TxRxFreq < bands[ED.currentBand[0]].fBandLow_Hz || 
            TxRxFreq > bands[ED.currentBand[0]].fBandHigh_Hz) {
            tft.setTextColor(RA8875_RED);  // Out of band
        } else {
            tft.setTextColor(RA8875_GREEN);  // In band
        }
        if (ED.activeVFO == 1)
            tft.setTextColor(RA8875_LIGHT_GREY);  // this band isn't active
        pixelOffset = 0;
        if (TxRxFreq < 10000000L)
            pixelOffset = 13;

        tft.setFont(&FreeSansBold24pt7b);       // Large font
        tft.setCursor(PaneVFOA.x0+pixelOffset, PaneVFOA.y0+10);
        FormatFrequency(TxRxFreq, freqBuffer);
        tft.print(freqBuffer);  // Show VFO_A
        // Mark the pane as no longer stale
        PaneVFOA.stale = false;
    }

    if (PaneVFOB.stale){
        // Update VFO B pane
        // Black out the prior data
        tft.fillRect(PaneVFOB.x0, PaneVFOB.y0, PaneVFOB.width, PaneVFOB.height, RA8875_BLACK);
        // Draw a box around the borders
        //tft.drawRect(PaneVFOB.x0, PaneVFOB.y0, PaneVFOB.width, PaneVFOB.height, RA8875_YELLOW);
        
        TxRxFreq = GetTXRXFreq(1);
        if (TxRxFreq < bands[ED.currentBand[1]].fBandLow_Hz || 
            TxRxFreq > bands[ED.currentBand[1]].fBandHigh_Hz) {
            tft.setTextColor(RA8875_RED);  // Out of band
        } else {
            tft.setTextColor(RA8875_GREEN);  // In band
        }
        if (ED.activeVFO == 0)
            tft.setTextColor(RA8875_LIGHT_GREY);  // this band isn't active

        pixelOffset = 0;
        if (TxRxFreq < 10000000L)
            pixelOffset = 8;

        tft.setFont(&FreeSansBold18pt7b);       // Medium font
        tft.setCursor(PaneVFOB.x0+pixelOffset, PaneVFOB.y0+10);
        FormatFrequency(TxRxFreq, freqBuffer);
        tft.print(freqBuffer);  // Show VFO_B
        PaneVFOB.stale = false;
    }
}

int64_t oldCenterFreq = 0;
int32_t oldBand = -1;
ModeSm_StateId oldState = ModeSm_StateId_ROOT;
ModulationType oldModulation = DCF77;
void DrawFreqBandModPane(void) {
    // Only update if information is stale
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
    // Black out the prior data
    tft.fillRect(PaneFreqBandMod.x0, PaneFreqBandMod.y0, PaneFreqBandMod.width, PaneFreqBandMod.height, RA8875_BLACK);
    // Draw a box around the borders and put some text in the middle
    //tft.drawRect(PaneFreqBandMod.x0, PaneFreqBandMod.y0, PaneFreqBandMod.width, PaneFreqBandMod.height, RA8875_YELLOW);
    
    tft.setFontScale((enum RA8875tsize)0);
    tft.setTextColor(RA8875_CYAN);
    tft.setCursor(PaneFreqBandMod.x0,PaneFreqBandMod.y0);
    tft.print("LO Freq:");
    tft.setTextColor(RA8875_LIGHT_ORANGE);
    //if (ED.spectrum_zoom == SPECTRUM_ZOOM_1) {
    //    tft.print(ED.centerFreq_Hz[ED.activeVFO] + 48000);
    //} else {
        tft.print(ED.centerFreq_Hz[ED.activeVFO]);
    //}

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

    // Mark the pane as no longer stale
    PaneFreqBandMod.stale = false;
}

const uint16_t MAX_WATERFALL_WIDTH = SPECTRUM_RES;
const uint16_t SPECTRUM_LEFT_X = PaneSpectrum.x0; // 3
const uint16_t SPECTRUM_TOP_Y  = PaneSpectrum.y0; //100
const uint16_t SPECTRUM_HEIGHT = 150;
const uint16_t WATERFALL_LEFT_X = SPECTRUM_LEFT_X;
const uint16_t WATERFALL_TOP_Y = (SPECTRUM_TOP_Y + SPECTRUM_HEIGHT + 5);
const uint16_t FIRST_WATERFALL_LINE = (WATERFALL_TOP_Y + 20); // 255 + 35 = 290
const uint16_t MAX_WATERFALL_ROWS = 170;   // Waterfall rows
#define AUDIO_SPECTRUM_BOTTOM (PaneAudioSpectrum.y0+PaneAudioSpectrum.height-30)
float xExpand = 1.4;
uint16_t spectrum_x = 10;


/*****
  Purpose: Draw Tuned Bandwidth on Spectrum Plot

  Parameter list:

  Return value;
    void
*****/
int32_t newCursorPosition = 0;
float32_t pixel_per_khz;
int32_t filterWidth;
int32_t oldCursorPosition = 256;
int32_t centerLine = (MAX_WATERFALL_WIDTH + SPECTRUM_LEFT_X) / 2;
int32_t h = 135;
#define FILTER_WIN 0x10  // Color of SSB filter width
FASTRUN void DrawBandWidthIndicatorBar(void)
{
    float zoomMultFactor = 0.0;
    float Zoom1Offset = 0.0;

    // erase the previous bandwidth indicator bar:
    tft.fillRect(0, SPECTRUM_TOP_Y + 20, MAX_WATERFALL_WIDTH+PaneSpectrum.x0, SPECTRUM_HEIGHT - 20, RA8875_BLACK);

    switch (ED.spectrum_zoom) {
        case 0:
            zoomMultFactor = 0.5;
            Zoom1Offset = 24000 * 0.0053333;
            break;
        case 1:
            zoomMultFactor = 1.0;
            Zoom1Offset = 0;
            break;
        case 2:
            zoomMultFactor = 2.0;
            Zoom1Offset = 0;
            break;
        case 3:
            zoomMultFactor = 4.0;
            Zoom1Offset = 0;
            break;
        case 4:
            zoomMultFactor = 8.0;
            Zoom1Offset = 0;
            break;
    }
    newCursorPosition = (int32_t)(-ED.fineTuneFreq_Hz[ED.activeVFO] * 0.0053333) * zoomMultFactor - Zoom1Offset;

    tft.writeTo(L2);

    pixel_per_khz = ((1 << ED.spectrum_zoom) * SPECTRUM_RES * 1000.0 / SR[SampleRate].rate);
    filterWidth = (int32_t)(((bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz - 
                              bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz) / 1000.0) * pixel_per_khz * 1.06);

    switch (ED.modulation[ED.activeVFO]) {
        case LSB:
            tft.fillRect(centerLine - filterWidth + newCursorPosition, SPECTRUM_TOP_Y + 20, filterWidth, SPECTRUM_HEIGHT - 20, FILTER_WIN);
            break;

        case USB:
            tft.fillRect(centerLine + newCursorPosition, SPECTRUM_TOP_Y + 20, filterWidth, SPECTRUM_HEIGHT - 20, FILTER_WIN);
            break;

        case AM:
            tft.fillRect(centerLine - (filterWidth ) * 0.93 + newCursorPosition, SPECTRUM_TOP_Y + 20, 2*filterWidth * 0.95, SPECTRUM_HEIGHT - 20, FILTER_WIN);
            break;

        case SAM:
            tft.fillRect(centerLine - (filterWidth ) * 0.93 + newCursorPosition, SPECTRUM_TOP_Y + 20, 2*filterWidth * 0.95, SPECTRUM_HEIGHT - 20, FILTER_WIN);
            break;
        default:
            break;
    }
    tft.drawFastVLine(centerLine + newCursorPosition, SPECTRUM_TOP_Y + 20, h - 10, RA8875_CYAN);
    oldCursorPosition = newCursorPosition;
}

uint16_t FILTER_PARAMETERS_X = PaneSpectrum.x0 + PaneSpectrum.width / 3;// (XPIXELS * 0.22); // 800 * 0.22 = 176
uint16_t FILTER_PARAMETERS_Y = PaneSpectrum.y0+1; //(YPIXELS * 0.213); // 480 * 0.23 = 110
void ShowBandwidth() {
    char buff[10];
    int centerLine = (MAX_WATERFALL_WIDTH + SPECTRUM_LEFT_X) / 2; // 257
    int pos_left;
    float32_t pixel_per_khz;

    pixel_per_khz = 0.0055652173913043;  // Al: I factored this constant: 512/92000;
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

void DrawFrequencyBarValue(void) {
    char txt[16];

    int bignum;
    int centerIdx;
    int pos_help;
    float disp_freq;
    float freq_calc;
    float grat;
    int centerLine = MAX_WATERFALL_WIDTH / 2 + SPECTRUM_LEFT_X;
    // positions for graticules: first for spectrum_zoom < 3, then for spectrum_zoom > 2
    const static int idx2pos[2][9] = {
        { -43, 21, 50, 250, 140, 250, 232, 250, 315 },
        { -43, 21, 50, 85, 200, 200, 232, 218, 315 }
    };

    grat = (float)(SR[SampleRate].rate / 8000.0) / (float)(1 << ED.spectrum_zoom);  // 1, 2, 4, 8, 16, 32, 64 . . . 4096

    tft.setTextColor(RA8875_WHITE);
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)0);
    tft.fillRect(0, WATERFALL_TOP_Y, MAX_WATERFALL_WIDTH + PaneSpectrum.x0 + 10, tft.getFontHeight(), RA8875_BLACK);

    // get current frequency in Hz
    freq_calc = (float)GetCenterFreq_Hz();  

    if (ED.spectrum_zoom < 5) {
        freq_calc = roundf(freq_calc / 1000);  // round graticule frequency to the nearest kHz
    }

    if (ED.spectrum_zoom != 0)
        centerIdx = 0;
    else
        centerIdx = -2;

    /**************************************************************************************************
        CENTER FREQUENCY PRINT
    **************************************************************************************************/
    ultoa((freq_calc + (centerIdx * grat)), txt, DEC);
    disp_freq = freq_calc + (centerIdx * grat);
    bignum = (int)disp_freq;
    itoa(bignum, txt, DEC);  // Make into a string
    tft.setTextColor(RA8875_GREEN);

    if (ED.spectrum_zoom == 0) {
        tft.setCursor(centerLine - 140, WATERFALL_TOP_Y);
    } else {
        tft.setCursor(centerLine - 20, WATERFALL_TOP_Y);
    }
    tft.print(txt);
    tft.setTextColor(RA8875_WHITE);
    /**************************************************************************************************
         PRINT ALL OTHER FREQUENCIES (NON-CENTER)
    **************************************************************************************************/
    // snprint() extremely memory inefficient. replaced with simple str??
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
                tft.drawFastVLine((WATERFALL_LEFT_X + pos_help * xExpand + 60), WATERFALL_TOP_Y - 5, 7, RA8875_YELLOW);  // Tick marks depending on zoom
            } else {
                tft.drawFastVLine((WATERFALL_LEFT_X + (pos_help + 9) * xExpand + 60), WATERFALL_TOP_Y - 5, 7, RA8875_YELLOW);
            }
        }
        if (ED.spectrum_zoom > 2 || freq_calc > 1000) {
            idx++;
        }
    }    
}

FASTRUN int16_t pixelnew(uint32_t i){
    int16_t result = displayScale[ED.spectrumScale].baseOffset + 
                    bands[ED.currentBand[ED.activeVFO]].pixel_offset + 
                    (int16_t)(displayScale[ED.spectrumScale].dBScale * psdnew[i]);
    if (result < 220) 
        return result;
    else 
        return 220;
}
#define TCVSDR_SMETER
#define SMETER_X PaneSMeter.x0+20
#define SMETER_Y PaneSMeter.y0+24
#define SMETER_BAR_LENGTH 180
#define SMETER_BAR_HEIGHT 18
uint16_t pixels_per_s = 12;
void DisplaydbM() {
    char buff[10];
    int16_t smeterPad;
    const float32_t slope = 10.0;
    const float32_t cons = -92;
    float32_t dbm;

    tft.fillRect(SMETER_X + 1, SMETER_Y + 1, SMETER_BAR_LENGTH, SMETER_BAR_HEIGHT, RA8875_BLACK);  // Erase old bar
    // dbm_calibration set to 22 in SDT.ino; gainCorrection is a value between -2 and +6 to compensate the frequency dependant pre-Amp gain
    // RFgain is initialized to 1 in the bands[] init; cons=-92; slope=10
    dbm = ED.dbm_calibration + bands[ED.currentBand[ED.activeVFO]].gainCorrection 
            + slope * log10f_fast(audioMaxSquaredAve) + cons 
            - (float32_t)bands[ED.currentBand[ED.activeVFO]].RFgain_dB * 1.5 
            - ED.rfGainAllBands_dB;
    dbm = dbm + ED.RAtten[ED.currentBand[ED.activeVFO]] - 31.0;  // input RF attenuator and PSA-8+ amplifier

    // determine length of S-meter bar, limit it to the box and draw it
    smeterPad = map(dbm, -73.0 - 9 * 6.0 /*S1*/, -73.0 /*S9*/, 0, 9 * pixels_per_s);
    // make sure that it does not extend beyond the field
    smeterPad = max(0, smeterPad);
    smeterPad = min(SMETER_BAR_LENGTH, smeterPad);
    tft.fillRect(SMETER_X + 1, SMETER_Y + 2, smeterPad, SMETER_BAR_HEIGHT - 2, RA8875_RED);  //bar 2*1 pixel smaller than the field

    tft.setFontDefault();
    tft.setTextColor(RA8875_WHITE);
    tft.setFontScale((enum RA8875tsize)0);
    tft.fillRect(SMETER_X + 185, SMETER_Y, 80, tft.getFontHeight(), RA8875_BLACK);  // The dB figure at end of S
    sprintf(buff,"%2.1fdBm",dbm);
    tft.setCursor(SMETER_X + 184, SMETER_Y);
    tft.print(buff);
}

uint16_t pixelold[MAX_WATERFALL_WIDTH];
uint16_t waterfall[MAX_WATERFALL_WIDTH];
static int16_t x1 = 0;
static int16_t y_left;
static int16_t y_prev = pixelold[0];
static int16_t offset = (SPECTRUM_TOP_Y+SPECTRUM_HEIGHT-ED.spectrumNoiseFloor);//-currentNoiseFloor[currentBand];
static int16_t y_current = offset;
static int16_t smeterLength;
#define NCHUNKS 4
#define CLIP_AUDIO_PEAK 115  // The pixel value where audio peak overwrites S-meter

FASTRUN  // Place in tightly-coupled memory
         /*****
  Purpose: Show Spectrum display
            Note that this routine calls the Audio process Function during each display cycle,
            for each of the 512 display frequency bins.  This means that the audio is refreshed at the maximum rate
            and does not have to wait for the display to complete drawinf the full spectrum.
            However, the display data are only updated ONCE during each full display cycle,
            ensuring consistent data for the erase/draw cycle at each frequency point.

  Parameter list:
    void

  Return value;
    void
*****/
void ShowSpectrum(void){
    Flag(2);
    int16_t centerLine = (MAX_WATERFALL_WIDTH + SPECTRUM_LEFT_X) / 2;
    int16_t middleSlice = centerLine / 2;  // Approximate center element
    offset = (SPECTRUM_TOP_Y+SPECTRUM_HEIGHT-ED.spectrumNoiseFloor);
    for (int j = 0; j < MAX_WATERFALL_WIDTH/NCHUNKS; j++){
        y_left = y_current; // use the value we calculated the last time through this loop
        y_current = offset - pixelnew(x1);
        // Prevent spectrum from going below the bottom of the spectrum area
        if (y_current > SPECTRUM_TOP_Y+SPECTRUM_HEIGHT) y_current = SPECTRUM_TOP_Y+SPECTRUM_HEIGHT;
        // Prevent spectrum from going above the top of the spectrum area
        if (y_current < SPECTRUM_TOP_Y) y_current = SPECTRUM_TOP_Y;

        tft.drawLine(SPECTRUM_LEFT_X+x1, y_prev, SPECTRUM_LEFT_X+x1, pixelold[x1], RA8875_BLACK);   // Erase the old spectrum
        tft.drawLine(SPECTRUM_LEFT_X+x1, y_left, SPECTRUM_LEFT_X+x1, y_current, RA8875_YELLOW);
        y_prev = pixelold[x1]; 
        pixelold[x1] = y_current;
        x1++;
        // Audio spectrum
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
            // Draw the S-meter bar
            // Running averaged values
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
        x1 = 0;
        y_prev = pixelold[0];
        y_current = offset;
        psdupdated = false; // draw spectrum once then wait until it is updated
        redrawSpectrum = false;
    
        // Draw the waterfall
        // Use the Block Transfer Engine (BTE) to move waterfall down a line. Take it from layer 1,
        // our active layer, and place it on layer 2
        tft.BTE_move(WATERFALL_LEFT_X, FIRST_WATERFALL_LINE, MAX_WATERFALL_WIDTH, MAX_WATERFALL_ROWS - 2, WATERFALL_LEFT_X, FIRST_WATERFALL_LINE + 1, 1, 2);
        while (tft.readStatus()) ;  // Make sure it is done.  Memory moves can take time.
        // Now bring waterfall back to the beginning of the 2nd row on layer 1.
        tft.BTE_move(WATERFALL_LEFT_X, FIRST_WATERFALL_LINE + 1, MAX_WATERFALL_WIDTH, MAX_WATERFALL_ROWS - 2, WATERFALL_LEFT_X, FIRST_WATERFALL_LINE + 1, 2, 1);
        while (tft.readStatus()) ; // Make sure it's done.
        // Then write new row data into the missing top row on layer 1 to get a scroll effect using display hardware, not the CPU.
        tft.writeRect(WATERFALL_LEFT_X, FIRST_WATERFALL_LINE, MAX_WATERFALL_WIDTH, 1, waterfall);
    }
    Flag(0);
}

uint32_t oz = 8;
int64_t ocf = 0;
int64_t oft = 0;
ModulationType omd = IQ;
void DrawSpectrumPane(void) {
    // Only update if information is stale
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
        tft.writeTo(L1);  // Always leave on layer 1
        return;
    }
    oz = ED.spectrum_zoom;
    ocf = ED.centerFreq_Hz[ED.activeVFO];
    oft = ED.fineTuneFreq_Hz[ED.activeVFO];
    omd = ED.modulation[ED.activeVFO];
    // Black out the prior data
    tft.writeTo(L2);
    DrawFrequencyBarValue();
    DrawBandWidthIndicatorBar();
    ShowBandwidth();
    tft.drawRect(PaneSpectrum.x0-2,PaneSpectrum.y0,MAX_WATERFALL_WIDTH+5,SPECTRUM_HEIGHT,RA8875_YELLOW);
    tft.writeTo(L1);  // Always leave on layer 1

    // Mark the pane as no longer stale
    PaneSpectrum.stale = false;
}

void DrawStateOfHealthPane(void) {
    // Only update if information is stale
    if (!PaneStateOfHealth.stale) return;
    tft.setFontDefault();
    // Black out the prior data
    tft.fillRect(PaneStateOfHealth.x0, PaneStateOfHealth.y0, PaneStateOfHealth.width, PaneStateOfHealth.height, RA8875_BLACK);
    // Draw a box around the borders and put some text in the middle
    //tft.drawRect(PaneStateOfHealth.x0, PaneStateOfHealth.y0, PaneStateOfHealth.width, PaneStateOfHealth.height, RA8875_YELLOW);
        
    char buff[10];
    int valueColor = RA8875_GREEN;
    double block_time;
    double processor_load;
    elapsed_micros_mean = elapsed_micros_sum / elapsed_micros_idx_t;

    block_time = 128.0 / (double)SR[SampleRate].rate;  // one audio block is 128 samples and uses this in seconds
    block_time = block_time * N_BLOCKS;

    block_time *= 1000000.0;                                  // now in µseconds
    processor_load = elapsed_micros_mean / block_time * 100;  // take audio processing time divide by block_time, convert to %

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
    //tft.setCursor(PaneStateOfHealth.x0 + 5 + tft.getFontWidth() * 3, 
    //                PaneStateOfHealth.y0);
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

    // Mark the pane as no longer stale
    PaneStateOfHealth.stale = false;
}

void DrawTimePane(void) {
    // Only update if information is stale
    if (!PaneTime.stale) return;
    tft.setFontDefault();
    // Black out the prior data
    tft.fillRect(PaneTime.x0, PaneTime.y0, PaneTime.width, PaneTime.height, RA8875_BLACK);
    // Draw a box around the borders and put some text in the middle
    //tft.drawRect(PaneTime.x0, PaneTime.y0, PaneTime.width, PaneTime.height, RA8875_YELLOW);
    
    char timeBuffer[15];
    char temp[5];
    temp[0] = '\0';
    timeBuffer[0] = '\0';
    strcpy(timeBuffer, MY_TIMEZONE);  // e.g., EST
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
        
    // Mark the pane as no longer stale
    PaneTime.stale = false;
}

void DrawSWRPane(void) {
    // Only update if information is stale
    if (!PaneSWR.stale) return;
    tft.setFontDefault();
    // Black out the prior data
    tft.fillRect(PaneSWR.x0, PaneSWR.y0, PaneSWR.width, PaneSWR.height, RA8875_BLACK);
    // Draw a box around the borders and put some text in the middle
    tft.drawRect(PaneSWR.x0, PaneSWR.y0, PaneSWR.width, PaneSWR.height, RA8875_YELLOW);
    // Put some text in it
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(PaneSWR.x0, PaneSWR.y0);
    tft.print("SWR");
    // Mark the pane as no longer stale
    PaneSWR.stale = false;
}

ModeSm_StateId oldMState = ModeSm_StateId_ROOT;
void DrawTXRXStatusPane(void) {
    // Only update if information is stale
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
    // Black out the prior data
    tft.fillRect(PaneTXRXStatus.x0, PaneTXRXStatus.y0, PaneTXRXStatus.width, PaneTXRXStatus.height, RA8875_BLACK);
    // Draw a box around the borders and put some text in the middle
    //tft.drawRect(PaneTXRXStatus.x0, PaneTXRXStatus.y0, PaneTXRXStatus.width, PaneTXRXStatus.height, RA8875_YELLOW);
    
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
    // Mark the pane as no longer stale
    PaneTXRXStatus.stale = false;
}

void DrawSMeterContainer(void) {
    int32_t i;
    tft.setFontDefault();
    // the white line must only go till S9
    tft.drawFastHLine(SMETER_X, SMETER_Y - 1, 9 * pixels_per_s, RA8875_WHITE);
    tft.drawFastHLine(SMETER_X, SMETER_Y + SMETER_BAR_HEIGHT + 2, 9 * pixels_per_s, RA8875_WHITE);  // changed 6 to 20

    for (i = 0; i < 10; i++) {  // Draw tick marks for S-values
        tft.drawRect(SMETER_X + i * pixels_per_s, SMETER_Y - 6 - (i % 2) * 2, 2, 6 + (i % 2) * 2, RA8875_WHITE);
    
    }

    // the green line must start at S9
    tft.drawFastHLine(SMETER_X + 9 * pixels_per_s, SMETER_Y - 1, SMETER_BAR_LENGTH + 2 - 9 * pixels_per_s, RA8875_GREEN);
    tft.drawFastHLine(SMETER_X + 9 * pixels_per_s, SMETER_Y + SMETER_BAR_HEIGHT + 2, SMETER_BAR_LENGTH + 2 - 9 * pixels_per_s, RA8875_GREEN);

    for (i = 1; i <= 3; i++) {  // Draw tick marks for s9+ values in 10dB steps
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


void DrawSMeterPane(void) {
    // Only update if information is stale
    if (!PaneSMeter.stale) return;
    tft.setFontDefault();
    // Black out the prior data
    tft.fillRect(PaneSMeter.x0, PaneSMeter.y0, PaneSMeter.width, PaneSMeter.height, RA8875_BLACK);  
    DrawSMeterContainer();
    // Mark the pane as no longer stale
    PaneSMeter.stale = false;
}

int32_t ohi = 0;
int32_t olo = 0;
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
    // erase the old lines
    int16_t fLo = map(olo, 0, 6000, 0, 256);
    int16_t fHi = map(ohi, 0, 6000, 0, 256);
    tft.drawFastVLine(PaneAudioSpectrum.x0 + 2 + abs(fLo), PaneAudioSpectrum.y0+2, AUDIO_SPECTRUM_BOTTOM-PaneAudioSpectrum.y0-3, RA8875_BLACK);
    tft.drawFastVLine(PaneAudioSpectrum.x0 + 2 + abs(fHi), PaneAudioSpectrum.y0+2, AUDIO_SPECTRUM_BOTTOM-PaneAudioSpectrum.y0-3, RA8875_BLACK);

    // The following lines calculate the position of the Filter bar below the spectrum display
    // and then draw the Audio spectrum in its own container to the right of the Main spectrum display
    int16_t filterLoPositionMarker = map(bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz, 0, 6000, 0, 256);
    int16_t filterHiPositionMarker = map(bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz, 0, 6000, 0, 256);
    // Draw Filter indicator lines on audio plot
    tft.drawFastVLine(PaneAudioSpectrum.x0 + 2 + abs(filterLoPositionMarker), PaneAudioSpectrum.y0+2, AUDIO_SPECTRUM_BOTTOM-PaneAudioSpectrum.y0-3, RA8875_LIGHT_GREY);
    tft.drawFastVLine(PaneAudioSpectrum.x0 + 2 + abs(filterHiPositionMarker), PaneAudioSpectrum.y0+2, AUDIO_SPECTRUM_BOTTOM-PaneAudioSpectrum.y0-3, RA8875_LIGHT_GREY);
    tft.writeTo(L1);
}

void DrawAudioSpectrumPane(void) {
    // Only update if information is stale
    if ((ohi != bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz) ||
        (olo != bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz))
        PaneAudioSpectrum.stale = true;
    if (!PaneAudioSpectrum.stale) return;
     // Black out the prior data
    tft.fillRect(PaneAudioSpectrum.x0, PaneAudioSpectrum.y0, PaneAudioSpectrum.width, PaneAudioSpectrum.height, RA8875_BLACK);
    DrawAudioSpectContainer();
    ohi = bands[ED.currentBand[ED.activeVFO]].FHiCut_Hz;
    olo = bands[ED.currentBand[ED.activeVFO]].FLoCut_Hz;
    // Mark the pane as no longer stale
    PaneAudioSpectrum.stale = false;
}

/////////////////////////////////////////////////////////////////
// These functions all have to do with updating the settings pane
uint16_t column1x = 0;
uint16_t column2x = 0;

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

VolumeFunction oldVolumeFunction = InvalidVolumeFunction;
int32_t oldVolumeSetting = 0;
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
        // has the value changed?
        if ((value == oldVolumeSetting) && (!PaneSettings.stale)){
            redrawValue = false;
        }
    }
    if (!redrawFunction && !redrawValue) return;

    oldVolumeSetting = value;
    oldVolumeFunction = volumeFunction;

    // There has been a change, redraw
    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);
    // Update the volume setting
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
                  1,redrawFunction,redrawValue); // yoffset = 1 to avoid smudging border box

}

AGCMode oldAGC = AGCInvalid;
void UpdateAGCSetting(void){
    if ((oldAGC == ED.agc) && (!PaneSettings.stale))
        return;
    oldAGC = ED.agc;

    tft.setFontScale((enum RA8875tsize)1);
    // Update the AGC setting
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
                  1,true,true); // yoffset = 1 to avoid smudging border box

}

// Main tune and fine tune increment
int32_t oldFreqIncrement = 0;
int64_t oldStepFineTune = 0;
void UpdateIncrementSetting(void) {
    if ((oldFreqIncrement != ED.freqIncrement) || (PaneSettings.stale)){
        tft.setFontDefault();
        tft.setFontScale((enum RA8875tsize)0);
        char valueText[8];
        sprintf(valueText,"%ld",ED.freqIncrement);
        UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column1x,
                    (char *)"Tune Inc:", 9, 
                    valueText, 7, // longest length for this field
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

uint8_t oldANR_notchOn = 8;
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
        valueText, 3, // longest length for this field
        PaneSettings.height/5 + tft.getFontHeight() + 1,true,true);
}

float32_t oldRAtten = -70;
float32_t oldTAtten = -70;
void UpdateRFGainSetting(void){
    if ((oldRAtten != ED.RAtten[ED.currentBand[ED.activeVFO]]) || (PaneSettings.stale)){
        oldRAtten = ED.RAtten[ED.currentBand[ED.activeVFO]];
        tft.setFontScale((enum RA8875tsize)0);
        char valueText[5];
        sprintf(valueText,"%2.1f",ED.RAtten[ED.currentBand[ED.activeVFO]]);
        UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column1x,
            (char *)"RX Atten:", 9, 
            valueText, 4, // longest length for this field
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
            valueText, 4, // longest length for this field
            PaneSettings.height/5 + 2*tft.getFontHeight() + 1,true,true);
    }
}

NoiseReductionType oldNR = NRInvalid;
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
        valueText, 4, // longest length for this field
        PaneSettings.height/5 + 2*tft.getFontHeight() + 1,true,true);
}

uint32_t oldZoom = 10000;
void UpdateZoomSetting(void){
    if ((oldZoom == ED.spectrum_zoom) && (!PaneSettings.stale))
        return;
    oldZoom = ED.spectrum_zoom;
    tft.setFontScale((enum RA8875tsize)0);
    char valueText[4];
    sprintf(valueText,"%dx",(uint8_t)(1<<ED.spectrum_zoom));
    UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column2x,
        (char *)"Zoom:", 5, 
        valueText, 3, // longest length for this field
        PaneSettings.height/5 + 4*tft.getFontHeight() + 1,true,true);
}

KeyTypeId oldKeyType = KeyTypeId_Invalid;
int32_t oldWPM = -1;
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
        valueText, 15, // longest length for this field
        PaneSettings.height/5 + 5*tft.getFontHeight() + 1,true,true);
}

int32_t oldDecoderFlag = -1;
void UpdateDecoderSetting(void){
    if ((oldDecoderFlag == ED.decoderFlag) && (!PaneSettings.stale)) 
        return;
    oldDecoderFlag = ED.decoderFlag;
    tft.setFontScale((enum RA8875tsize)0);
    char valueText[4];
    if (ED.decoderFlag){
        sprintf(valueText,"On");
    } else {
        sprintf(valueText,"Off");
    }
    UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column2x,
        (char *)"Decoder:", 8, 
        valueText, 3, // longest length for this field
        PaneSettings.height/5 + 3*tft.getFontHeight() + 1,true,true);
}

float32_t oldrfGainAllBands_dB = -1000;
void UpdateDSPGainSetting(void){
    if ((oldrfGainAllBands_dB == ED.rfGainAllBands_dB) && (!PaneSettings.stale)) 
        return;
    oldrfGainAllBands_dB = ED.rfGainAllBands_dB;
    tft.setFontScale((enum RA8875tsize)0);
    char valueText[5];
    sprintf(valueText,"%2.1f",ED.rfGainAllBands_dB);
    UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column1x,
        (char *)"DSP Gain:", 9, 
        valueText, 4, // longest length for this field
        PaneSettings.height/5 + 3*tft.getFontHeight() + 1,true,true);
}

int32_t oldAntennaSelection = -1;
void UpdateAntennaSetting(void){
    if ((oldAntennaSelection == ED.antennaSelection[ED.currentBand[ED.activeVFO]]) && (!PaneSettings.stale)) 
        return;
    oldAntennaSelection = ED.antennaSelection[ED.currentBand[ED.activeVFO]];
    tft.setFontScale((enum RA8875tsize)0);
    char valueText[2];
    sprintf(valueText,"%ld",ED.antennaSelection[ED.currentBand[ED.activeVFO]]);
    UpdateSetting(tft.getFontWidth(), tft.getFontHeight(), column1x,
        (char *)"Antenna:", 8, 
        valueText, 2, // longest length for this field
        PaneSettings.height/5 + 4*tft.getFontHeight() + 1,true,true);
}

void DrawSettingsPane(void) {
    // Only update if information is stale
    if (PaneSettings.stale){
        // Black out the prior data
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
        // Draw a box around the borders
        tft.drawRect(PaneSettings.x0, PaneSettings.y0, PaneSettings.width, PaneSettings.height, RA8875_WHITE);
        // Mark the pane as no longer stale
        PaneSettings.stale = false;
    }
}
/////////////////////////////////////////////////////////////////

void DrawNameBadgePane(void) {
    // Only update if information is stale
    if (!PaneNameBadge.stale) return;
    tft.setFontDefault();
    // Black out the prior data
    tft.fillRect(PaneNameBadge.x0, PaneNameBadge.y0, PaneNameBadge.width, PaneNameBadge.height, RA8875_BLACK);
    // Draw a box around the borders and put some text in the middle
    //tft.drawRect(PaneNameBadge.x0, PaneNameBadge.y0, PaneNameBadge.width, PaneNameBadge.height, RA8875_YELLOW);
    
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_YELLOW);
    tft.setCursor(PaneNameBadge.x0, PaneNameBadge.y0);
    tft.print(RIGNAME);

    tft.setFontScale(0);
    tft.print(" ");
    tft.setTextColor(RA8875_RED);
    tft.setCursor(PaneNameBadge.x0+2*PaneNameBadge.width/3, PaneNameBadge.y0+tft.getFontHeight()/2);
    tft.print(VERSION);

    // Mark the pane as no longer stale
    PaneNameBadge.stale = false;
}


void drawArray(const uint32_t * image, uint32_t isize, uint16_t iwidth, uint16_t x, uint16_t y){
    uint16_t pixels[iwidth]; // container
    uint32_t i, idx;
    for (idx = 0; idx < isize/iwidth; idx++){
        for (i = (iwidth*idx); i < iwidth*(idx+1); i++){
            pixels[i - (iwidth*idx)] = tft.Color24To565(image[i]);
        }
        tft.drawPixels(pixels, iwidth, x, idx+y);
    }
}

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
    tft.setCursor((WINDOW_WIDTH / 2) - (38 * tft.getFontWidth() / 2) + 0, WINDOW_HEIGHT / 4 + 70);  // 38 = letters in string
    tft.print("           Oliver King, KI3P");
}

static bool alreadyDrawn = false;
static void DrawSplash(){
    if (alreadyDrawn) return;
    Splash();
    alreadyDrawn = true;
    // Display the image at bottom middle
    //drawArray(phoenix_image, 65536, 256, 400-128, 480-256);
}

uint32_t timer_ms = 0;
uint32_t timerDisplay_ms = 0;
static void DrawHome(){
    // Only draw if we're on the HOME screen
    if (!(uiSM.state_id == UISm_StateId_HOME)) return;
    // Clear the screen whenever we enter this state from another one.
    // When we enter the UISm_StateId_HOME state, clearScreen is set to true.
    tft.writeTo(L1);
    if (uiSM.vars.clearScreen){
        Debug("Clearing the screen upon entry to HOME state");
        tft.fillWindow();
        uiSM.vars.clearScreen = false;
        // Mark all the panes as stale so that they're redrawn
        for (size_t i = 0; i < NUMBER_OF_PANES; i++){
            WindowPanes[i]->stale = true;
        }
    }
    // Trigger a redraw of some of the panes
    if (millis()-timer_ms > 1000) {
        timer_ms = millis();
        PaneStateOfHealth.stale = true;
        PaneTime.stale = true;
    }

    if (millis()-timerDisplay_ms > SPECTRUM_REFRESH_MS) {
        timerDisplay_ms = millis();
        if (redrawSpectrum == false)
            redrawSpectrum = true;
    }
    // Draw each of the panes
    for (size_t i = 0; i < NUMBER_OF_PANES; i++){
        WindowPanes[i]->DrawFunction();
    }
}

void DrawMainMenu(void){
    // Only draw if we're on the MAIN_MENU screen
    if (!(uiSM.state_id == UISm_StateId_MAIN_MENU)) return;
    // Clear the screen whenever we enter this state from another one.
    if (uiSM.vars.clearScreen){
        Debug("Clearing the screen upon entry to MAIN_MENU state");
        tft.writeTo(L2);
        tft.fillWindow();
        tft.writeTo(L1);
        tft.fillWindow();        
        uiSM.vars.clearScreen = false;
    }
    tft.setTextColor(RA8875_MAGENTA);
    tft.setCursor(50, WINDOW_HEIGHT/ 10);
    tft.setFontScale(2);
    tft.print("Main menu");
}

void DrawSecondaryMenu(void){
    // Only draw if we're on the SECONDARY_MENU screen
    if (!(uiSM.state_id == UISm_StateId_SECONDARY_MENU)) return;
    // Clear the screen whenever we enter this state from another one.
    if (uiSM.vars.clearScreen){
        Debug("Clearing the screen upon entry to SECONDARY_MENU state");
        tft.fillWindow();
        uiSM.vars.clearScreen = false;
    }
    tft.setTextColor(RA8875_MAGENTA);
    tft.setCursor(50, WINDOW_HEIGHT/ 10);
    tft.setFontScale(2);
    tft.print("Secondary menu");
}

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

/**
 * Show the value of a parameter. 
 */
static void DrawValue(void){
    Base *base = (Base *)uiSM.vars.uiUp;
    switch (base->type){
        case TYPE_INT32: {
            UIValueUpdateInt *foo = (UIValueUpdateInt *)uiSM.vars.uiUp;
            int32_t current_value = foo->getValueFunction();
            Debug(current_value);
            break;
        }
        case TYPE_FLOAT: {
            UIValueUpdateFloat *foo = (UIValueUpdateFloat *)uiSM.vars.uiUp;
            float current_value = foo->getValueFunction();
            Debug(current_value);
            break;
        }
    }
}

UISm_StateId oldstate = UISm_StateId_ROOT;
void DrawDisplay(void){
    //if (uiSM.state_id == oldstate) return;
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
        default:
            break;
    }
    oldstate = uiSM.state_id;
}

