#ifndef DSP_CWMESSAGEKEYER_H
#define DSP_CWMESSAGEKEYER_H
#include "SDT.h"

/*
 * CW Message Keyer
 *
 * Stored Morse code "macro" messages plus the bookkeeping needed to send them.
 * Ported from T41_SDR/keyer.cpp + keyer.h (per-feature port; first pass is
 * plumbing only -- data tables, message storage, raised-cosine ramp tables,
 * and the per-character Morse encoder. The actual message-sending engine is
 * left as stubs that will drive Phoenix's existing CW transmit FSM (ModeSm)
 * by injecting iDIT_PRESSED / iDAH_PRESSED interrupts at dit/dah/space
 * boundaries -- to be implemented in a follow-up pass once a trigger source
 * exists (USB keyboard or CAT command or menu action).
 *
 * NOTE on architectural mismatch with T41: T41's keyer is blocking (busy-
 * waits inside SendMessage) and assumes a USB keyboard is the input source.
 * Phoenix's CW transmit is non-blocking and FSM-driven (CW_TRANSMIT_DIT_MARK,
 * CW_TRANSMIT_KEYER_SPACE, CW_TRANSMIT_KEYER_WAIT in ModeSm). The follow-up
 * implementation must drive the FSM, not bypass it.
 */

#define MAX_MESSAGE_LENGTH       33     /**< Max characters per stored message (excluding null) */
#define MAX_MESSAGES             10     /**< Max number of stored messages */
#define KEYER_FREE_BUFFER_SIZE   50     /**< Size of the free-typed message buffer */
#define CW_RAMP_LENGTH           128    /**< Length of the raised-cosine envelope ramps */

/* ---- Stored messages ---- */
extern char keyerMessages[MAX_MESSAGES][MAX_MESSAGE_LENGTH + 1];
extern int  selectedMsg;            /**< Index of the currently selected stored message */
extern bool keyerMessagesActive;    /**< True: stored-msg navigation mode; False: free-typing */
extern bool keyerMessageEditMode;   /**< True while user is editing a stored message */
extern int  keyerEditIndex;         /**< Cursor position within the message being edited */

/* ---- Free-typed message buffer ---- */
extern uint8_t msgBuffer[KEYER_FREE_BUFFER_SIZE];
extern int     msgIndexIn;          /**< Write index into msgBuffer */

/* ---- Raised-cosine envelope ramps for CW key shaping (5 ms at 25.6 kHz) ---- */
extern float cwRampUp[CW_RAMP_LENGTH];
extern float cwRampDown[CW_RAMP_LENGTH];

/**
 * @brief Initialize the CW message keyer.
 * @note Resets the free-typed buffer index and builds the raised-cosine ramp
 *       tables used to shape the leading/trailing edge of each CW element
 *       (https://en.wikipedia.org/wiki/Raised-cosine_filter).
 */
void KeyerSetup(void);

/**
 * @brief Look up the Morse code byte for an ASCII character.
 * @param chr ASCII character (case-insensitive for letters).
 * @return Sentinel-encoded Morse byte (leading 1 marks start, then dit=0/dah=1),
 *         or 0 if no Morse encoding exists for the character.
 * @note Encoding matches T41_SDR/keyer.cpp letterTable/numberTable/punctuationTable.
 *       Example: 'A' -> 0b101 (sentinel | dit | dah).
 */
int8_t MorseCodeForChar(char chr);

/**
 * @brief STUB: Send a free-form message in CW.
 * @param msg Null-terminated ASCII message to send.
 * @note Not yet implemented for Phoenix. The follow-up implementation will
 *       walk the message character-by-character and inject synthetic
 *       iDIT_PRESSED / iDAH_PRESSED interrupts into Phoenix's ModeSm FSM
 *       at the appropriate dit/dah/inter-element/inter-letter/inter-word
 *       boundaries (1 / 3 / 1 / 3 / 7 dit-lengths respectively).
 */
void SendMessage(const char *msg);

/**
 * @brief STUB: Send a stored message by index.
 * @param messageIndex Index into keyerMessages[] (0 .. MAX_MESSAGES-1).
 */
void SendMessage(int messageIndex);

/**
 * @brief STUB: Per-loop tick for the CW message keyer.
 * @note Will (a) drive in-progress message transmission by checking elapsed
 *       time against the next dit/dah/space threshold and dispatching the
 *       next FSM event, and (b) consume keyboard input for stored-message
 *       navigation/editing. Currently a no-op until a USB keyboard input
 *       source exists in Phoenix.
 */
void KeyerLoop(void);

#endif /* DSP_CWMESSAGEKEYER_H */
