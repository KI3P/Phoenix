#include "SDT.h"


/*
 * Functions in this file do two things only: 
 * 1) They read the state of the radio, 
 * 2) They draw on the display.
 * They do not change the value of any variable that is declared outside of this file.
 */

#include <RA8875.h>


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

void Splash(){
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

static void DrawSplash(){
    Splash();
    // Display the image at bottom middle
    drawArray(phoenix_image, 65536, 256, 400-128, 480-256);

}

static void DrawHome(){}

void InitializeDisplay(void){
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    tft.begin(RA8875_800x480, 8, 20000000UL, 4000000UL);
    tft.setRotation(0);
    DrawDisplay();
}
//void InitializeDisplay(void){};
//void DrawHome(void){};
//void DrawSplash(void){};

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
    if (uiSM.state_id == oldstate) return;
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

