#include "SDT.h"
#include "DSP_CWMessageKeyer.h"

#include <ctype.h>
#include <math.h>

/*
 * CW Message Keyer -- plumbing only (first-pass port from T41_SDR/keyer.cpp).
 *
 * What's implemented:
 *   - Stored message storage and free-typed buffer (data only)
 *   - Three Morse encoding tables (letters, digits, punctuation)
 *   - MorseCodeForChar() ASCII -> sentinel-encoded Morse byte
 *   - KeyerSetup() builds the raised-cosine envelope ramps
 *
 * What's stubbed (see follow-up TODO comments):
 *   - SendMessage(const char*) -- needs an FSM-driving non-blocking sender
 *   - SendMessage(int)          -- convenience wrapper, also stubbed
 *   - KeyerLoop()               -- needs a USB keyboard input source
 */

/* ---- Stored messages ---- */
char keyerMessages[MAX_MESSAGES][MAX_MESSAGE_LENGTH + 1] = {
    "CQ CQ CQ DE <CALL>",
    "DE <CALL>",
    "RIG T41 PHOENIX",
    "73",
    "599",
    "",
    "",
    "",
    "",
    "",
};

int  selectedMsg          = 0;
bool keyerMessagesActive  = true;
bool keyerMessageEditMode = false;
int  keyerEditIndex       = 0;

/* ---- Free-typed message buffer ---- */
uint8_t msgBuffer[KEYER_FREE_BUFFER_SIZE] = {0};
int     msgIndexIn        = 0;

/* ---- Raised-cosine envelope ramps (filled in by KeyerSetup) ---- */
float cwRampUp[CW_RAMP_LENGTH];
float cwRampDown[CW_RAMP_LENGTH];

/* =================================================================
 * Morse code encoding tables.
 *
 * Each byte uses the "sentinel" encoding from T41_SDR: the highest
 * set bit marks the start of the symbol; bits below it are read
 * MSB-first as dit (0) / dah (1).
 *
 * Example: 'A' = 0b101  -> sentinel(1) | dit(0) | dah(1)  -> ".-"
 *          'B' = 0b11000 -> sentinel(1) | dah dit dit dit -> "-..."
 * =================================================================
 */
static const uint8_t letterTable[26] = {
    0b101,        // A
    0b11000,      // B
    0b11010,      // C
    0b1100,       // D
    0b10,         // E
    0b10010,      // F
    0b1110,       // G
    0b10000,      // H
    0b100,        // I
    0b10111,      // J
    0b1101,       // K
    0b10100,      // L
    0b111,        // M
    0b110,        // N
    0b1111,       // O
    0b10110,      // P
    0b11101,      // Q
    0b1010,       // R
    0b1000,       // S
    0b11,         // T
    0b1001,       // U
    0b10001,      // V
    0b1011,       // W
    0b11001,      // X
    0b11011,      // Y
    0b11100       // Z
};

static const uint8_t numberTable[10] = {
    0b111111,     // 0
    0b101111,     // 1
    0b100111,     // 2
    0b100011,     // 3
    0b100001,     // 4
    0b100000,     // 5
    0b110000,     // 6
    0b111000,     // 7
    0b111100,     // 8
    0b111110      // 9
};

/*
 * Punctuation: parallel arrays. punctuationASCII[i] is the ASCII code,
 * punctuationCode[i] is the sentinel-encoded Morse byte.
 *
 * Note: T41's table reuses the same code for '(' and ')' (both 0b01011110).
 * Ported as-is.
 */
static const uint8_t punctuationASCII[] = {
    '!', '"', '$', '\'', '(', ')', ',', '-', '.', '/', ':', ';', '?', '_'
};
static const uint8_t punctuationCode[] = {
    0b01101011,   // ! 33
    0b01010010,   // " 34
    0b10001001,   // $ 36
    0b01011110,   // ' 39
    0b01011110,   // ( 40 (same as apostrophe per T41)
    0b01011110,   // ) 41 (same as apostrophe per T41)
    0b01110011,   // , 44
    0b00100001,   // - 45
    0b01010101,   // . 46
    0b00110010,   // / 47
    0b01111000,   // : 58
    0b01101010,   // ; 59
    0b01001100,   // ? 63
    0b01001101    // _ 95
};
static const size_t punctuationCount = sizeof(punctuationASCII) / sizeof(punctuationASCII[0]);

/* =================================================================
 * Public functions
 * =================================================================
 */

void KeyerSetup(void) {
    msgIndexIn = 0;

    // Raised-cosine ramps for ~5 ms key-edge shaping.
    // cwRampUp goes 0 -> 1 over CW_RAMP_LENGTH samples.
    // cwRampDown goes 1 -> 0 over CW_RAMP_LENGTH samples.
    // Matches T41_SDR/keyer.cpp::KeyerSetup.
    for (int i = 0; i < CW_RAMP_LENGTH; i++) {
        cwRampUp[i]   = 0.5f * (1.0f + cosf((1.0f + (float)i / (float)CW_RAMP_LENGTH) * (float)PI));
        cwRampDown[i] = 0.5f * (1.0f + cosf(((float)i / (float)CW_RAMP_LENGTH) * (float)PI));
    }
}

int8_t MorseCodeForChar(char chr) {
    if (isalpha((unsigned char)chr)) {
        char up = (char)toupper((unsigned char)chr);
        return (int8_t)letterTable[up - 'A'];
    }
    if (isdigit((unsigned char)chr)) {
        return (int8_t)numberTable[chr - '0'];
    }
    for (size_t i = 0; i < punctuationCount; i++) {
        if (punctuationASCII[i] == (uint8_t)chr) {
            return (int8_t)punctuationCode[i];
        }
    }
    return 0;  // No Morse encoding (e.g. space, or unknown character)
}

/* ---------------------------------------------------------------- *
 * STUB IMPLEMENTATIONS
 *
 * These are no-ops in the plumbing-only port. Calling them is
 * harmless. Real implementations belong in a follow-up pass once:
 *   (a) Phoenix has a USB keyboard input source (next planned port),
 *       so KeyerLoop has something to read; AND
 *   (b) we've designed the FSM-driving sender that injects synthetic
 *       iDIT_PRESSED / iDAH_PRESSED interrupts into ModeSm at the
 *       right times instead of T41's blocking busy-wait loop.
 * ---------------------------------------------------------------- */

void SendMessage(const char *msg) {
    (void)msg;
    // TODO(cw-keyer): non-blocking message sender. Decompose `msg` into a
    // queue of (element_type, duration_ms) tuples using MorseCodeForChar(),
    // then in KeyerLoop() advance through the queue by injecting
    // SetInterrupt(iDIT_PRESSED) / iDAH_PRESSED / iKEY1_RELEASED into
    // Phoenix's ModeSm at the dit/dah/space boundaries. Use ED.currentWPM
    // for ditDuration_ms (= 1200 / wpm). See ModeSm.drawio for the full
    // CW transmit state diagram.
}

void SendMessage(int messageIndex) {
    if (messageIndex < 0 || messageIndex >= MAX_MESSAGES) {
        return;
    }
    SendMessage(&keyerMessages[messageIndex][0]);
}

void KeyerLoop(void) {
    // TODO(cw-keyer): once a USB keyboard input source exists, port T41's
    // KeyerLoop input dispatch (selectedMsg navigation with arrow keys,
    // edit mode toggle with insert key, free-typing into msgBuffer, etc.).
    // Until then this is a no-op so it's safe to call from Loop.cpp.
    //
    // This function will also need to advance any in-progress message
    // transmission queued by SendMessage() above.
}
