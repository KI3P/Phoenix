#include "SDT.h"
#include <RA8875.h>

// Create an IntervalTimer object for driving the state machines
IntervalTimer timer1ms;

// pins for the display CS and reset
#define TFT_CS 10
#define TFT_RESET 9 // any pin or nothing!

// screen size
#define XPIXELS 800
#define YPIXELS 480

RA8875 tft = RA8875(TFT_CS, TFT_RESET);  // Instantiate the display object

//extern const uint32_t phoenix_image[65536];
#include "phoenix.cpp"

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

void Splash() 
{
    int centerCall;
    tft.clearScreen(RA8875_BLACK);

    tft.setTextColor(RA8875_MAGENTA);
    tft.setCursor(50, YPIXELS / 10);
    tft.setFontScale(2);
    tft.print("Experimental Phoenix Code Base");

    tft.setFontScale(3);
    tft.setTextColor(RA8875_GREEN);
    tft.setCursor(XPIXELS / 3 - 120, YPIXELS / 10 + 53);
    tft.print("T41-EP SDR Radio");
    
    tft.setFontScale(1);
    tft.setTextColor(RA8875_YELLOW);
    tft.setCursor(XPIXELS / 2 - (2 * tft.getFontWidth() / 2), YPIXELS / 3);
    tft.print("By");
    tft.setFontScale(1);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor((XPIXELS / 2) - (38 * tft.getFontWidth() / 2) + 0, YPIXELS / 4 + 70);  // 38 = letters in string
    tft.print("           Oliver King, KI3P");
    
}

/**
 * This is run every 1ms. It dispatches do events to the state machines
 */
void tick1ms(void){
    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_DO);
    UISm_dispatch_event(&uiSM, UISm_EventId_DO);
}

void setup(void){
    Serial.begin(115200);
    delay(1000); //testcode
    Serial.println("T41 SDT Setup");
    //Configure the pins for the auto shutdown
    pinMode(BEGIN_TEENSY_SHUTDOWN, INPUT);  // HI received here tells Teensy to start shutdown routine
    pinMode(SHUTDOWN_COMPLETE, OUTPUT);     // HI sent here tells ATTiny85 to cut the power
    digitalWrite(SHUTDOWN_COMPLETE, 0);  
    
    // Start the mode state machines
    ModeSm_start(&modeSM);
    modeSM.vars.waitDuration_ms = CW_TRANSMIT_SPACE_TIMEOUT_MS;
    modeSM.vars.ditDuration_ms = DIT_DURATION_MS;
    UISm_start(&uiSM);
    
    pinMode(31,OUTPUT); //testcode
    digitalWrite(31, 0);  //testcode

    FrontPanelInit();

    modeSM.state_id = ModeSm_StateId_SSB_RECEIVE;
    ED.agc = AGCOff;
    ED.nrOptionSelect = NROff;
    SetupAudio();
    UpdateAudioIOState();

    InitializeRFHardware(); // RF board, LPF board, and BPF board
    InitializeSignalProcessing();
    // If our input tone was at 1 kHz, then it will appear at 49 kHz after Fs/4
    // So make our VFO frequency the negative of this to shift it back to 1 kHz
    ED.fineTuneFreq_Hz[ED.activeVFO] = -48000L; //testcode

    // Display code
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);

    tft.begin(RA8875_800x480, 8, 20000000UL, 4000000UL);  // parameter list from library code
    tft.setRotation(0);
    Splash();

    // Display the image at bottom middle
    drawArray(phoenix_image, 65536, 256, 400-128, 480-256);
    timer1ms.begin(tick1ms, 1000);  // run tick1ms every 1ms
}