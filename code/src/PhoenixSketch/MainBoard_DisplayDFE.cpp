/**
 * @file MainBoard_DisplayDFE.cpp
 * @brief Direct frequency entry rendering
 *
 * This module displays the number pad to allow direct frequency entry
 *
 * @see window_panes.drawio, DFE tab for layout
 * @see MainBoard_Display.cpp for core display infrastructure
 * @see MainBoard_DisplayMenus.cpp for menu system
 */

#include "SDT.h"
#include "MainBoard_Display.h"
#include <RA8875.h>

static const int8_t NUMBER_OF_PANES = 4;

// Forward declaration of the pane drawing functions
static void DrawFreqLabelPane(void);
static void DrawFreqEntryPane(void);
static void DrawNumberPadPane(void);
static void DrawInstructionsPane(void);

// Pane instances
Pane PaneFreqLabel =    {60,40,480,30,DrawFreqLabelPane,1};
Pane PaneFreqEntry =    {550,40,90,30,DrawFreqEntryPane,1};
Pane PaneNumberPad =    {60,80,210,360,DrawNumberPadPane,1};
Pane PaneInstructions = {290,80,320,360,DrawInstructionsPane,1};

// Array of all panes for iteration
static Pane* WindowPanes[NUMBER_OF_PANES] = {&PaneFreqLabel,&PaneFreqEntry,
                                    &PaneNumberPad,&PaneInstructions};

extern RA8875 tft;

// Number pad button mapping and configuration
// Grid layout: 6 rows x 3 columns (18 buttons total)
int32_t numKeys[] = { 0x0D, 0x7F, 0x7F,  // values to be allocated to each key push
                0x37, 0x38, 0x39,
                0x34, 0x35, 0x36,
                0x31, 0x32, 0x33,
                0x30, 0x58, 0x7F,
                0x7F, 0x7F, 0x99 };
int32_t keyCol[] = {RA8875_YELLOW, RA8875_RED,  RA8875_RED,
                RA8875_BLUE,   RA8875_GREEN,RA8875_GREEN,
                RA8875_BLUE,   RA8875_BLUE, RA8875_BLUE,
                RA8875_RED,    RA8875_RED,  RA8875_RED,
                RA8875_RED,    RA8875_BLACK,RA8875_BLACK,
                RA8875_YELLOW, RA8875_YELLOW,RA8875_BLACK };
int32_t textCol[] = { RA8875_BLACK, RA8875_WHITE, RA8875_WHITE,
                RA8875_WHITE, RA8875_BLACK, RA8875_BLACK,
                RA8875_WHITE, RA8875_WHITE, RA8875_WHITE,
                RA8875_WHITE, RA8875_WHITE, RA8875_WHITE,
                RA8875_WHITE, RA8875_WHITE, RA8875_WHITE,
                RA8875_BLACK, RA8875_BLACK, RA8875_WHITE };
const char *key_labels[] = { "<", "", "",
                            "7", "8", "9",
                            "4", "5", "6",
                            "1", "2", "3",
                            "0", "D", "",
                            "",  "",  "X" };

// Frequency entry state variables
char strF[6] = { ' ', ' ', ' ', ' ', ' ' };  // Container for frequency string during entry (5 digits max)
String stringF;                               // Arduino String version of frequency entry
static long enteredF = 0L;                    // Parsed frequency value in Hz
static int8_t numdigits = 0;                  // Number of digits entered so far

// Number pad button rendering parameters
#define TEXT_OFFSET      -8   // Text centering offset within button circles
#define BUTTONS_SPACE    60   // Pixel spacing between button centers
#define BUTTONS_OFFSET_X 40   // Initial X offset from pane edge
#define BUTTONS_OFFSET_Y 30   // Initial Y offset from pane edge
#define BUTTONS_RADIUS   20   // Circle radius for each button
#define BUTTONS_LEFT (PaneNumberPad.x0 + BUTTONS_OFFSET_X)
#define BUTTONS_TOP  (PaneNumberPad.y0 + BUTTONS_OFFSET_Y)

/**
 * @brief Main frequency entry screen rendering function
 * @note Called from DrawDisplay() when in FREQ_ENTRY UI state
 * @note Displays numeric keypad for direct frequency entry
 * @note Accepts 1-2 digit MHz entry or 4-5 digit kHz entry
 */
void DrawFrequencyEntryPad(void){
    if (!(uiSM.state_id == UISm_StateId_FREQ_ENTRY) )
        return;
    tft.writeTo(L1);
    if (uiSM.vars.clearScreen){
        tft.fillWindow();
        tft.writeTo(L2);
        tft.fillWindow();
        tft.writeTo(L1);
        uiSM.vars.clearScreen = false;
        
        // clear the freq entry, if any
        stringF = "     ";  // 5 spaces
        stringF.toCharArray(strF, stringF.length());
        numdigits = 0;
        
        for (size_t i = 0; i < NUMBER_OF_PANES; i++){
            WindowPanes[i]->stale = true;
        }
    }
    for (size_t i = 0; i < NUMBER_OF_PANES; i++){
        WindowPanes[i]->DrawFunction();
    }
}

/**
 * @brief Render the numeric keypad pane with labeled button circles
 * @note Displays 6x3 grid of circular buttons (0-9, delete, clear, enter, exit)
 * @note Button colors and labels defined in keyCol[], textCol[], and key_labels[] arrays
 */
void DrawNumberPadPane(void) {
    if (!PaneNumberPad.stale) return;
    PaneNumberPad.stale = false;
    tft.fillRect(PaneNumberPad.x0, PaneNumberPad.y0, PaneNumberPad.width, PaneNumberPad.height, DARKGREY);
    tft.drawRect(PaneNumberPad.x0, PaneNumberPad.y0, PaneNumberPad.width, PaneNumberPad.height, RA8875_YELLOW);

    // Draw the labeled circles
    tft.setFontScale((enum RA8875tsize)1);
    for (unsigned i = 0; i < 6; i++) {
        for (unsigned j = 0; j < 3; j++) {
            tft.fillCircle(BUTTONS_LEFT + j * BUTTONS_SPACE, BUTTONS_TOP + i * BUTTONS_SPACE, BUTTONS_RADIUS, keyCol[j + 3 * i]);
            tft.setCursor(BUTTONS_LEFT + j * BUTTONS_SPACE + TEXT_OFFSET, BUTTONS_TOP + i * BUTTONS_SPACE - 18);
            tft.setTextColor(textCol[j + 3 * i]);
            tft.print(key_labels[j + 3 * i]);
        }
    }
}

/**
 * @brief Render the frequency entry prompt label pane
 * @note Displays instruction text "Enter Frequency (kHz or MHz):"
 */
void DrawFreqLabelPane(void) {
    if (!PaneFreqLabel.stale) return;
    PaneFreqLabel.stale = false;
    tft.fillRect(PaneFreqLabel.x0, PaneFreqLabel.y0, PaneFreqLabel.width, PaneFreqLabel.height, RA8875_BLACK);
    //tft.drawRect(PaneFreqLabel.x0, PaneFreqLabel.y0, PaneFreqLabel.width, PaneFreqLabel.height, RA8875_YELLOW);

    tft.setFontScale((enum RA8875tsize)1);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(PaneFreqLabel.x0, PaneFreqLabel.y0);
    tft.print("Enter Frequency (kHz or MHz):");
}

/**
 * @brief Render the frequency entry instructions pane
 * @note Displays user instructions for keypad operation
 * @note Explains enter (<), exit (X), and delete (D) functions
 */
void DrawInstructionsPane(void) {
    if (!PaneInstructions.stale) return;
    PaneInstructions.stale = false;
    tft.fillRect(PaneInstructions.x0, PaneInstructions.y0, PaneInstructions.width, PaneInstructions.height, RA8875_BLACK);
    tft.drawRect(PaneInstructions.x0, PaneInstructions.y0, PaneInstructions.width, PaneInstructions.height, RA8875_YELLOW);

    tft.setFontScale((enum RA8875tsize)0);
    tft.setCursor(PaneInstructions.x0 + 20,PaneInstructions.y0 + 50);
    tft.setTextColor(RA8875_WHITE);
    tft.print("Direct Frequency Entry");
    tft.setCursor(PaneInstructions.x0 + 20, PaneInstructions.y0 + 100);
    tft.print("<   Apply entered frequency");
    tft.setCursor(PaneInstructions.x0 + 20, PaneInstructions.y0 + 130);
    tft.print("X   Exit without changing frequency");
    tft.setCursor(PaneInstructions.x0 + 20, PaneInstructions.y0 + 160);
    tft.print("D   Delete last digit");
}

/**
 * @brief Render the frequency digits display pane
 * @note Shows currently entered frequency digits (up to 5 digits)
 * @note Updates as user enters each digit
 */
void DrawFreqEntryPane(void) {
    if (!PaneFreqEntry.stale) return;
    PaneFreqEntry.stale = false;
    tft.fillRect(PaneFreqEntry.x0, PaneFreqEntry.y0, PaneFreqEntry.width, PaneFreqEntry.height, RA8875_BLACK);
    //tft.drawRect(PaneFreqEntry.x0, PaneFreqEntry.y0, PaneFreqEntry.width, PaneFreqEntry.height, RA8875_YELLOW);

    tft.setTextColor(RA8875_WHITE);
    tft.setFontScale((enum RA8875tsize)1);
    tft.setCursor(PaneFreqEntry.x0, PaneFreqEntry.y0);
    tft.print(strF);
}

/**
 * @brief Process keypad button press and update frequency entry
 * @param button Button number (0-17) from keypad grid
 * @note Handles digit entry, delete (0x58/'X'), and enter (0x0D)
 * @note Accepts 1-2 digits for MHz or 4-5 digits for kHz
 * @note Validates frequency range (250 kHz to 125 MHz)
 * @note On valid entry: tunes to frequency and returns to home screen
 * @note On invalid entry: clears entry buffer
 * @note Called from Loop.cpp button handler
 */
void InterpretFrequencyEntryButtonPress(int32_t button){
    if ((button > 17) || (button < 0)) return;

    int32_t key = 0;
    key = numKeys[button];
    switch (key) {
        case 0x7F:{ // ignore button press
            break;
        }
        case 0x58:{  // delete last digit
            if (numdigits != 0) {
                numdigits--;
                strF[numdigits] = ' ';
            }
            break;
        }
        case 0x0D:{  // Apply the entered frequency (if valid) =13
            stringF = String(strF);
            enteredF = stringF.toInt();
            if ((numdigits == 1) || (numdigits == 2)) {
                enteredF = enteredF * 1000000; // entered MHz
            }
            if ((numdigits == 4) || (numdigits == 5)) {
                enteredF = enteredF * 1000; // entered kHz
            }
            if ((enteredF > 125000000) || (enteredF < 250000)) {
                // invalid frequency
                stringF = "     ";  // 5 spaces
                stringF.toCharArray(strF, stringF.length());
                numdigits = 0;
            } else {
                // Tune to this new frequency!
                ED.centerFreq_Hz[ED.activeVFO] = (int64_t)enteredF + SR[SampleRate].rate/4;
                ED.fineTuneFreq_Hz[ED.activeVFO] = 0.0;
                UpdateRFHardwareState();

                // Go back to the home screen
                UISm_dispatch_event(&uiSM,UISm_EventId_HOME);
            }
            break;
        }
        default:{
            if ((numdigits == 5) || ((key == 0x30) & (numdigits == 0))) {
            } else {
                strF[numdigits] = char(key);
                numdigits++;
            }
            PaneFreqEntry.stale = true;
            break;
        }
    }

}

///////////////////////////////////////////////////////////////////////////////
// UNIT TEST HELPER FUNCTIONS
///////////////////////////////////////////////////////////////////////////////

/**
 * @brief Get current number of digits entered (for unit testing)
 * @return Current digit count (0-5)
 */
int8_t DFEGetNumDigits(void){
    return numdigits;
}

/**
 * @brief Get frequency entry string buffer (for unit testing)
 * @return Pointer to strF character array
 */
char * DFEGetFString(void){
    return strF;
}

