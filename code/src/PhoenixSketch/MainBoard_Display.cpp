#include "SDT.h"
#include <RA8875.h>
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
#define NUMBER_OF_PANES 13

struct Pane {
    uint16_t x0;     /**< top left corner, horizontal coordinate */
    uint16_t y0;     /**< top left corner, vertical coordinate */
    uint16_t width;  /**< horizontal left to right size */
    uint16_t height; /**< vertical top to bottom size */
    void (*DrawFunction)(void); /**< Pointer to function that draws the pane */
    bool stale;      /**< true if the pane information needs to be updated */
};

Pane PaneVFOA =        {5,5,280,50,DrawVFOPanes,1};
Pane PaneVFOB =        {300,5,220,40,DrawVFOPanes,1};
Pane PaneFreqBandMod = {5,60,310,30,DrawFreqBandModPane,1};
Pane PaneSpectrum =    {5,95,520,170,DrawSpectrumPane,1};
Pane PaneWaterfall =   {5,270,520,170,DrawWaterfallPane,1};
Pane PaneStateOfHealth={5,445,260,30,DrawStateOfHealthPane,1};
Pane PaneTime =        {270,445,260,30,DrawTimePane,1};
Pane PaneSWR =         {535,15,150,40,DrawSWRPane,1};
Pane PaneTXRXStatus =  {710,20,60,30,DrawTXRXStatusPane,1};
Pane PaneSMeter =      {535,60,260,50,DrawSMeterPane,1};
Pane PaneAudioSpectrum={535,115,260,150,DrawAudioSpectrumPane,1};
Pane PaneSettings =    {535,270,260,170,DrawSettingsPane,1};
Pane PaneNameBadge =   {535,445,260,30,DrawNameBadgePane,1};


// An array of all the panes, useful for iterating over
Pane WindowPanes[NUMBER_OF_PANES] = {PaneVFOA,PaneVFOB,PaneFreqBandMod,
                                    PaneSpectrum,PaneWaterfall,PaneStateOfHealth,
                                    PaneTime,PaneSWR,PaneTXRXStatus,
                                    PaneSMeter,PaneAudioSpectrum,PaneSettings,
                                    PaneNameBadge};

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
    if ((TxRxFreq == TxRxFreq_old) && (ED.activeVFO == activeVFO_old))
        return; // don't update
    // Which VFO needs to be updated?
    if (ED.activeVFO != activeVFO_old){ // both!
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
        tft.drawRect(PaneVFOA.x0, PaneVFOA.y0, PaneVFOA.width, PaneVFOA.height, RA8875_YELLOW);

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
        tft.drawRect(PaneVFOB.x0, PaneVFOB.y0, PaneVFOB.width, PaneVFOB.height, RA8875_YELLOW);
        
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

void DrawFreqBandModPane(void) {
            tft.setFontDefault();

    // Only update if information is stale
    if (!PaneFreqBandMod.stale) return;
    // Black out the prior data
    tft.fillRect(PaneFreqBandMod.x0, PaneFreqBandMod.y0, PaneFreqBandMod.width, PaneFreqBandMod.height, RA8875_BLACK);
    // Draw a box around the borders and put some text in the middle
    tft.drawRect(PaneFreqBandMod.x0, PaneFreqBandMod.y0, PaneFreqBandMod.width, PaneFreqBandMod.height, RA8875_YELLOW);
    // Put some text in it
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(PaneFreqBandMod.x0, PaneFreqBandMod.y0);
    tft.print("Freq Band Mod");
    // Mark the pane as no longer stale
    PaneFreqBandMod.stale = false;
}

void DrawSpectrumPane(void) {
    // Only update if information is stale
    if (!PaneSpectrum.stale) return;
    // Black out the prior data
    tft.fillRect(PaneSpectrum.x0, PaneSpectrum.y0, PaneSpectrum.width, PaneSpectrum.height, RA8875_BLACK);
    // Draw a box around the borders and put some text in the middle
    tft.drawRect(PaneSpectrum.x0, PaneSpectrum.y0, PaneSpectrum.width, PaneSpectrum.height, RA8875_YELLOW);
    // Put some text in it
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(PaneSpectrum.x0, PaneSpectrum.y0);
    tft.print("Spectrum");
    // Mark the pane as no longer stale
    PaneSpectrum.stale = false;
}

void DrawWaterfallPane(void) {
    // Only update if information is stale
    if (!PaneWaterfall.stale) return;
    // Black out the prior data
    tft.fillRect(PaneWaterfall.x0, PaneWaterfall.y0, PaneWaterfall.width, PaneWaterfall.height, RA8875_BLACK);
    // Draw a box around the borders and put some text in the middle
    tft.drawRect(PaneWaterfall.x0, PaneWaterfall.y0, PaneWaterfall.width, PaneWaterfall.height, RA8875_YELLOW);
    // Put some text in it
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(PaneWaterfall.x0, PaneWaterfall.y0);
    tft.print("Waterfall");
    // Mark the pane as no longer stale
    PaneWaterfall.stale = false;
}

void DrawStateOfHealthPane(void) {
    // Only update if information is stale
    if (!PaneStateOfHealth.stale) return;
    // Black out the prior data
    tft.fillRect(PaneStateOfHealth.x0, PaneStateOfHealth.y0, PaneStateOfHealth.width, PaneStateOfHealth.height, RA8875_BLACK);
    // Draw a box around the borders and put some text in the middle
    tft.drawRect(PaneStateOfHealth.x0, PaneStateOfHealth.y0, PaneStateOfHealth.width, PaneStateOfHealth.height, RA8875_YELLOW);
    // Put some text in it
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(PaneStateOfHealth.x0, PaneStateOfHealth.y0);
    tft.print("State Of Health");
    // Mark the pane as no longer stale
    PaneStateOfHealth.stale = false;
}

void DrawTimePane(void) {
    // Only update if information is stale
    if (!PaneTime.stale) return;
    // Black out the prior data
    tft.fillRect(PaneTime.x0, PaneTime.y0, PaneTime.width, PaneTime.height, RA8875_BLACK);
    // Draw a box around the borders and put some text in the middle
    tft.drawRect(PaneTime.x0, PaneTime.y0, PaneTime.width, PaneTime.height, RA8875_YELLOW);
    // Put some text in it
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(PaneTime.x0, PaneTime.y0);
    tft.print("Time");
    // Mark the pane as no longer stale
    PaneTime.stale = false;
}

void DrawSWRPane(void) {
    // Only update if information is stale
    if (!PaneSWR.stale) return;
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

void DrawTXRXStatusPane(void) {
    // Only update if information is stale
    if (!PaneTXRXStatus.stale) return;
    // Black out the prior data
    tft.fillRect(PaneTXRXStatus.x0, PaneTXRXStatus.y0, PaneTXRXStatus.width, PaneTXRXStatus.height, RA8875_BLACK);
    // Draw a box around the borders and put some text in the middle
    tft.drawRect(PaneTXRXStatus.x0, PaneTXRXStatus.y0, PaneTXRXStatus.width, PaneTXRXStatus.height, RA8875_YELLOW);
    // Put some text in it
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(PaneTXRXStatus.x0, PaneTXRXStatus.y0);
    tft.print("TXRX");
    // Mark the pane as no longer stale
    PaneTXRXStatus.stale = false;
}

void DrawSMeterPane(void) {
    // Only update if information is stale
    if (!PaneSMeter.stale) return;
    // Black out the prior data
    tft.fillRect(PaneSMeter.x0, PaneSMeter.y0, PaneSMeter.width, PaneSMeter.height, RA8875_BLACK);
    // Draw a box around the borders and put some text in the middle
    tft.drawRect(PaneSMeter.x0, PaneSMeter.y0, PaneSMeter.width, PaneSMeter.height, RA8875_YELLOW);
    // Put some text in it
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(PaneSMeter.x0, PaneSMeter.y0);
    tft.print("S Meter");
    // Mark the pane as no longer stale
    PaneSMeter.stale = false;
}

void DrawAudioSpectrumPane(void) {
    // Only update if information is stale
    if (!PaneAudioSpectrum.stale) return;
    // Black out the prior data
    tft.fillRect(PaneAudioSpectrum.x0, PaneAudioSpectrum.y0, PaneAudioSpectrum.width, PaneAudioSpectrum.height, RA8875_BLACK);
    // Draw a box around the borders and put some text in the middle
    tft.drawRect(PaneAudioSpectrum.x0, PaneAudioSpectrum.y0, PaneAudioSpectrum.width, PaneAudioSpectrum.height, RA8875_YELLOW);
    // Put some text in it
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(PaneAudioSpectrum.x0, PaneAudioSpectrum.y0);
    tft.print("Audio Spectrum");
    // Mark the pane as no longer stale
    PaneAudioSpectrum.stale = false;
}

void DrawSettingsPane(void) {
    // Only update if information is stale
    if (!PaneSettings.stale) return;
    // Black out the prior data
    tft.fillRect(PaneSettings.x0, PaneSettings.y0, PaneSettings.width, PaneSettings.height, RA8875_BLACK);
    // Draw a box around the borders and put some text in the middle
    tft.drawRect(PaneSettings.x0, PaneSettings.y0, PaneSettings.width, PaneSettings.height, RA8875_YELLOW);
    // Put some text in it
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(PaneSettings.x0, PaneSettings.y0);
    tft.print("Settings");
    // Mark the pane as no longer stale
    PaneSettings.stale = false;
}

void DrawNameBadgePane(void) {
    // Only update if information is stale
    if (!PaneNameBadge.stale) return;
    // Black out the prior data
    tft.fillRect(PaneNameBadge.x0, PaneNameBadge.y0, PaneNameBadge.width, PaneNameBadge.height, RA8875_BLACK);
    // Draw a box around the borders and put some text in the middle
    tft.drawRect(PaneNameBadge.x0, PaneNameBadge.y0, PaneNameBadge.width, PaneNameBadge.height, RA8875_YELLOW);
    // Put some text in it
    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(PaneNameBadge.x0, PaneNameBadge.y0);
    tft.print("Name Badge");
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

static void DrawHome(){
    // Only draw if we're on the HOME screen
    if (!(uiSM.state_id == UISm_StateId_HOME)) return;
    // Clear the screen whenever we enter this state from another one.
    // When we enter the UISm_StateId_HOME state, clearScreen is set to true.
    if (uiSM.vars.clearScreen){
        Debug("Clearing the screen upon entry to HOME state");
        tft.fillWindow();
        uiSM.vars.clearScreen = false;
    }
    // Draw each of the panes
    for (size_t i = 0; i < NUMBER_OF_PANES; i++){
        WindowPanes[i].DrawFunction();
    }
}

void InitializeDisplay(void){
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    tft.begin(RA8875_800x480, 8, 20000000UL, 4000000UL);
    tft.setRotation(0);
    DrawDisplay();
}

struct dispSc displayScale[] = // *dbText,dBScale, pixelsPerDB, baseOffset, offsetIncrement
    {
        { "20 dB/", 10.0, 2, 24, 1.00 },
        { "10 dB/", 20.0, 4, 10, 0.50 },
        { "5 dB/", 40.0, 8, 58, 0.25 },
        { "2 dB/", 100.0, 20, 120, 0.10 },
        { "1 dB/", 200.0, 40, 200, 0.05 }
    };

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
        default:
            break;
    }
    oldstate = uiSM.state_id;
}

