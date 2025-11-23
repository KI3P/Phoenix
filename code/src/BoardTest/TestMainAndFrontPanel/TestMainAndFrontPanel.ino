/*
 * Written by Russ Bruhnke NQ0U
 */
#include <Wire.h> //include Wire.h library
#include <SPI.h>
#include <SD.h>
#include <RA8875.h>
#include <Audio.h>


#include "G0ORX_FrontPanel.h"

// Pins for Shutdown
#define BEGIN_SHUTDOWN 0
#define SHUTDOWN_DONE 1

// location of switch matrix on display
#define MATRIX_X    50
#define MATRIX_X_INC    100
#define MATRIX_Y    40
#define MATRIX_R    20      // radious of matrix "button"

// encoder "button" start location on display
#define ENCODER_X   450
#define ENCODER_Y   120
#define ENCODER_R   40      // radius of the "button"

#define ENCODER_CLEAR_TIME 2000     // clear the encoder values after displaying for two seconds
// pins for the display CS and reset
#define TFT_CS 10
#define TFT_RESET 9 // any pin or nothing!

// screen size
#define XPIXELS 800
#define YPIXELS 480

AudioInputI2SQuad i2s_quadIn; // just need one Audio interface to get Audio Board initialized
TwoWire *_wire=&Wire;

RA8875 tft = RA8875(TFT_CS, TFT_RESET);  // Instantiate the display object

IntervalTimer myTimer;
 
enum
{
    NONE,
    TEST_MATRIX,
    TEST_ENCODERS
};

// vars
    byte error, address; //variable for error and I2C address
    int nDevices;
    bool display_init;
    bool K9HZ_panel;
    uint8_t test_step;

    //uint8_t switch_states[18];
    int oldbutton=-1;
    int oldswitch=-1;



 /**********************************************************************/   
static void shutdown()
{
    Serial.println("Shutting down...");
    delay(500);
    digitalWrite(SHUTDOWN_DONE,HIGH);
}

/**********************************************************************/

uint16_t cleartimer = 0;
bool cleartimeexp=false;

// Interrupt routines located here
// My timer routine This will handle all timing events. It runs every mS */
void Timer(void)
{

// clear the encoder values, if needed
    if(cleartimer>0)
    {
        cleartimer--;
        if(cleartimer==0)
            cleartimeexp=true;
    }

}


/************************************************************************


************************************************************************/
void Splash() 
{
    int centerCall;
    tft.clearScreen(RA8875_BLACK);

    tft.setTextColor(RA8875_MAGENTA);
    tft.setCursor(50, YPIXELS / 10);
    tft.setFontScale(2);
    tft.print("K9HZ Boards Display Test Code");

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
    tft.setCursor((XPIXELS / 2) - (38 * tft.getFontWidth() / 2) + 15, YPIXELS / 4 + 80);  // 38 = letters in string
    tft.print("Al Peter, AC8GY     Jack Purdum, W8TEE");
//    tft.setCursor((XPIXELS / 2) - (12 * tft.getFontWidth()) / 2, YPIXELS / 2 + 110);  // 12 = letters in "Property of:"
    
    //tft.setFontScale(2);
    tft.setTextColor(RA8875_GREEN);
    centerCall = (XPIXELS - 25 * tft.getFontWidth()) / 2;
    tft.setCursor(centerCall, YPIXELS / 2 + 160);
    tft.print("Code by Russ Bruhnke NQ0U");

    tft.drawCircle(400,350, 30, RA8875_YELLOW);
    tft.drawCircle(400,350, 28, RA8875_YELLOW);
    tft.fillCircle(400,350, 26, RA8875_PINK);
}


/*************************************************************/
/************************************************************************


************************************************************************/
void setup() 
{
    Serial.begin(38400);

    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);

    pinMode(BEGIN_SHUTDOWN,INPUT);
    pinMode(SHUTDOWN_DONE,OUTPUT);

    attachInterrupt(digitalPinToInterrupt(BEGIN_SHUTDOWN), shutdown, HIGH);
  
    while (!Serial); // Waiting for Serial Monitor
    Serial.println("\n\rK9HZ Main board test code\n\r");
    Serial.println("Russ Bruhnke NQ0U 07/20/2024\n\r");
    display_init = false;

    K9HZ_panel = false;
    test_step = NONE;

    display_help();

    // start the timer
    myTimer.begin(Timer, 1000);  // Timer to run every millisecond

}


/************************************************************************


************************************************************************/
void loop() 
{
    uint8_t encoder_num;
    int enc_val;

    if(Serial.available())
    {
        char ch = Serial.read();
//        Serial.printf("%c\n\r",ch);
    

        switch(ch)
        {
            case 'h':       // help
            case 'H':
                    display_help();
            break;

            case 'S':       // I2S Scan
            case 's':
                Serial.println("\n\rI2C Scanner\n\r");
                for(int i=0;i<3;i++) 
                {
                    switch(i) 
                    {
                        case 0:
                            Serial.println("Scanning Wire ...");
                            _wire=&Wire;
                        break;

                        case 1:
                            Serial.println("\n\rScanning Wire1 ...");
                            _wire=&Wire1;
                        break;

                        case 2:
                                Serial.println("\n\rScanning Wire2 ...");
                            _wire=&Wire2;
                        break;

                        default :break;
                    }

                    _wire->begin();
                    nDevices = 0;
                    for (address = 1; address < 127; address++ )
                    {
                        // The i2c_scanner uses the return value of
                        // the Write.endTransmisstion to see if
                        // a device did acknowledge to the address.
                        _wire->beginTransmission(address);
                        error = _wire->endTransmission();

                        if (error == 0)
                        {
                            Serial.print("I2C device found at address 0x");
                            if (address < 16)
                                Serial.print("0");
                            Serial.print(address, HEX);
                            Serial.println(".");
                            if(i==0)
                            {
                                if(address == 0x0A)
                                {
                                    Serial.println("Found TEENSY Audio Hat");
                                }
                            }
                            else
                            if(i==1)
                            {
                                if(address == 0x21)
                                {
                                    Serial.println("K9HZ or G0ORX front panel found");
                                    K9HZ_panel = true;
                                }
                            }
                        
                            nDevices++;
                        }
                        else if (error == 4)
                        {
                            Serial.print("Unknown error at address 0x");
                            if (address < 16)
                                Serial.print("0");
                            Serial.println(address, HEX);
                        }
                    }
                    if (nDevices == 0)
                        Serial.println("No I2C devices found for this Wire\n\r");
            
                }
                Serial.println("I2C scan complete\n\r");
            break;

            case 'M':
            case 'm':
                Serial.println("\n\rTesting Matrix...\n\r");
                if(display_init==false)
                {
                    Serial.println("Display MUST be tested first!");
                    Serial.println("Do Test 'D'\n\r");
                    Serial.println("Test failed\n\r");
                }
                else
                if(K9HZ_panel == false)
                {
                    Serial.println("Scan for K9HZ panel MUST be done first!");
                    Serial.println("Do test 'S'\n\r");
                    Serial.println("Test failed\n\r");
                }
                else
                {
                    FrontPanelInit();
                    Serial.println("Press X and Enter to end test");

                    tft.clearScreen(RA8875_BLACK);
                    tft.setFontScale(1);
                    tft.setTextColor(RA8875_YELLOW);
                    tft.setCursor(XPIXELS / 2 - (2 * tft.getFontWidth() / 2), YPIXELS-tft.getFontHeight()-4);
                    tft.print("Press X to end test");
                    show_switch_matrix(-1);
                    show_encoders(0,0);
                    test_step =TEST_MATRIX;
                    
                }
            break;

           

            case 'd':   // display test
            case 'D':
                    Serial.println("\n\rTesting Display...\n\r");
                    tft.begin(RA8875_800x480, 8, 20000000UL, 4000000UL);  // parameter list from library code
                    tft.setRotation(0);
                    display_init=true;
                    Splash();
                    Serial.println("Display Test Complete...\n\r");
            break;

            case 'x':
            case 'X':
                Serial.println("Ending Previous Test...\n\r");
                if(display_init)
                {
                    tft.clearScreen();
                    tft.setFontScale(2);
                    tft.setTextColor(RA8875_YELLOW);
                    tft.setCursor(XPIXELS / 2 - (5 * tft.getFontWidth()), YPIXELS / 3);
                    tft.print("Test Ended");
                }
                test_step = NONE;

            break;

            default: break;
         }// end switch(ch)
    }// if(Serial.available())

    
    // check the tests that require interaction by the user....  x will end that test
        switch(test_step)
        {
            case NONE:
            break;

            case TEST_MATRIX:
            if(oldbutton != G0ORXButtonPressed)
            {
                oldbutton = G0ORXButtonPressed;
                if(oldbutton!=-1)
                {
                    Serial.printf("Switch %d Pushed\n\r",oldbutton);
                }
                else
                    Serial.println("Switch Released\n\r");

                show_switch_matrix(oldbutton);
            }

            if(oldswitch != G0ORXSwitchPressed)
            {
                oldswitch = G0ORXSwitchPressed;
                if(oldswitch != -1)
                {
                    Serial.printf("Encoder Switch %d Pushed\n\r",oldswitch);
                }
                else
                    Serial.println("Encoder Switch Released\n\r");

            }
            enc_val = FrontPanelCheck(&encoder_num);
            if(enc_val!=0)
            {
                show_encoders(encoder_num,enc_val);
            }

            if(cleartimeexp)
            {
                cleartimeexp = false;
                clearEncoders();
            }
            break;


            default:break;
        }
 
}// end of loop


void show_switch_matrix(int switchnum)
{
    int16_t x,y,r,incx,incy,j;
    uint16_t color;
    j=0;
    r=MATRIX_R;   
    incx=MATRIX_X_INC;
    incy=480/6;

    for(int row=0;row<6;row++)
    {
        y=MATRIX_Y+incy*(row);
        for(int col=0;col<3;col++)
        {
            x=MATRIX_X+incx*(col);
            if(switchnum == j)
                color = RA8875_WHITE;
            else
                color = RA8875_RED;

            tft.fillCircle(x,y,r,color);
            j++;
            
        }
    }
}


/************************************************************************


************************************************************************/
FASTRUN
void show_encoders(uint8_t encnum,int encval)
{
    int16_t x,y,r;
    uint16_t color;

    r=ENCODER_R;   
    //incx=MATRIX_X_INC;
    //incy=480/6;

    tft.setTextColor(RA8875_BLACK);
    tft.setFontScale(2);

    x=ENCODER_X;
    y=ENCODER_Y;

    color = RA8875_GREEN;

    if(G0ORXSwitchPressed==0)
        color = RA8875_YELLOW;

    tft.fillCircle(x,y,r,color);
    tft.setCursor(x-10,y-30);
    tft.print("1");
    tft.setCursor(x-30,y+r+5);
    tft.setTextColor(RA8875_YELLOW);

    clearEncoders();

    if(encnum==1)
    {
        if(encval == 1)
        {
            tft.print("CW ");
            cleartimer = ENCODER_CLEAR_TIME;
        }
        else
        if(encval == -1)
        {
            tft.print("CCW");
            cleartimer = ENCODER_CLEAR_TIME;
        }
    }

    x=x+160;
    color = RA8875_GREEN;
    tft.setTextColor(RA8875_BLACK);

    if(G0ORXSwitchPressed==1)
        color = RA8875_YELLOW;

    tft.fillCircle(x,y,r,color);
    tft.setCursor(x-10,y-30);
    tft.print("2");
    tft.setCursor(x-30,y+r+5);
    tft.setTextColor(RA8875_YELLOW);

    if(encnum==2)
    {
        if(encval == 1)
        {
            tft.print("CW ");
            cleartimer = ENCODER_CLEAR_TIME;
        }
        else
        if(encval == -1)
        {
            tft.print("CCW");
            cleartimer = ENCODER_CLEAR_TIME;
        }
        
    }

// start of second row

    x=ENCODER_X;
    y+=180;
    color = RA8875_GREEN;
    tft.setTextColor(RA8875_BLACK);

    if(G0ORXSwitchPressed==2)
        color = RA8875_YELLOW;

    tft.fillCircle(x,y,r,color);
    tft.setCursor(x-10,y-30);
    tft.print("3");

    tft.setCursor(x-30,y+r+5);
    tft.setTextColor(RA8875_YELLOW);

    // clear out the last letters
    tft.fillRect(x-32, y+r+5, 240,60, RA8875_BLACK);

    if(encnum==3)
    {
        if(encval == 1)
        {
            tft.print("CW ");
            cleartimer = ENCODER_CLEAR_TIME;
        }
        else
        if(encval == -1)
        {
            tft.print("CCW");
            cleartimer = ENCODER_CLEAR_TIME;
        }
    }

    x+=160;
    color = RA8875_GREEN;
    tft.setTextColor(RA8875_BLACK);

    if(G0ORXSwitchPressed==3)
        color = RA8875_YELLOW;

    tft.fillCircle(x,y,r,color);
    tft.setCursor(x-10,y-30);
    tft.print("4");

    tft.setCursor(x-30,y+r+5);
    tft.setTextColor(RA8875_YELLOW);

    if(encnum==4)
    {
        if(encval == 1)
        {
            tft.print("CW ");
            cleartimer = ENCODER_CLEAR_TIME;
        }
        else
        if(encval == -1)
        {
            tft.print("CCW");
            cleartimer = ENCODER_CLEAR_TIME;
        }
    }

}


/************************************************************************


************************************************************************/
void display_help(void)
{
    Serial.println("\n\rK9HZ Main board Front Panel test code");
    Serial.println("Press 'key' + Enter\n\r");
    Serial.println("keys:");
    Serial.println("S or s for I2C scan");

    Serial.println("D or d for Display test");

    Serial.println("M or m for digital switch matrix and encoders test");

    Serial.println("H or h for this help");
}


void clearEncoders(void)
{
    // clear out the last letters
    tft.fillRect(ENCODER_X-32, ENCODER_Y+ENCODER_R+5, 240,60, RA8875_BLACK);
     tft.fillRect(ENCODER_X-32, ENCODER_Y+ENCODER_R+185, 240,60, RA8875_BLACK);

}


// *******END OF FILE!*****************


