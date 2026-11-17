/* Rotary encoder decoder for the front-panel encoders.
 *
 * Based on Ben Buxton's full-step state table (rotary encoder handler for
 * Arduino, copyright 2011 Ben Buxton, GPL version 3, bb@cactii.net), as adapted
 * by John Melton (G0ORX) to be fed pin states from an MCP23017.
 *
 * The earlier decoder here armed a direction on one contact and counted a step
 * each time the other contact read closed, and stayed armed while steps kept
 * coming. A contact that chatters as it closes then counted once per chatter:
 * a single detent of the Filter knob was measured counting +19. The state table
 * below counts only a complete sequence of contact states, so chatter on one
 * contact cannot add steps.
 */

#include "FrontPanel_Rotary.h"

// Decoder states. The encoder rests in R_START with both contacts open (11).
// "A" states belong to a detent where contact A leads, "B" states to B leading.
#define R_START   0x0
#define R_A_BEGIN 0x1   // 10: A closed first
#define R_A_NEXT  0x2   // 00: both closed
#define R_A_FINAL 0x3   // 01: A still closed, B open again
#define R_B_BEGIN 0x4   // 01: B closed first
#define R_B_NEXT  0x5   // 00: both closed
#define R_B_FINAL 0x6   // 10: B still closed, A open again
#define R_STATE_MASK 0x0F
// Flags added to a transition that completes a detent
#define EMIT_A 0x10
#define EMIT_B 0x20

// Next state, indexed by [current state][contacts], contacts = (B << 1) | A.
// Adjacent states (one contact changed) follow Buxton's table. A jump where both
// contacts changed means a state was skipped between readings; it is resolved
// in the direction already in progress. From R_START that direction is not
// known yet, and update() handles the jump (see there).
static const unsigned char rotaryTable[7][4] = {
    //              00                 01                 10                 11
    /* START   */ { R_START,           R_B_BEGIN,         R_A_BEGIN,         R_START },
    /* A_BEGIN */ { R_A_NEXT,          R_A_FINAL,         R_A_BEGIN,         R_START },
    /* A_NEXT  */ { R_A_NEXT,          R_A_FINAL,         R_A_BEGIN,         R_START | EMIT_A },
    /* A_FINAL */ { R_A_NEXT,          R_A_FINAL,         R_A_BEGIN | EMIT_A, R_START | EMIT_A },
    /* B_BEGIN */ { R_B_NEXT,          R_B_BEGIN,         R_B_FINAL,         R_START },
    /* B_NEXT  */ { R_B_NEXT,          R_B_BEGIN,         R_B_FINAL,         R_START | EMIT_B },
    /* B_FINAL */ { R_B_NEXT,          R_B_BEGIN | EMIT_B, R_B_FINAL,         R_START | EMIT_B },
};

Rotary_V12::Rotary_V12(bool reversed) {
    _reversed = reversed;
    _state = R_START;
    _lastNext = R_START;
    value = 0;
}

FASTRUN
void Rotary_V12::update(unsigned char state) {
    state &= 0x03;
    if ((_state == R_START) && (state == 0b00)) {
        // Both contacts closed with no first contact seen: the edge that began
        // the detent was missed. Assume the knob is still turning the way it
        // last turned. If it is not, the rest of the sequence runs backwards
        // through the table and returns to R_START without counting, so a wrong
        // guess loses that step rather than counting one the wrong way (unless
        // the next reading also skips a state).
        _state = _lastNext;
        return;
    }
    unsigned char next = rotaryTable[_state][state];
    _state = next & R_STATE_MASK;
    if (next & EMIT_A) {
        _lastNext = R_A_NEXT;
        value += _reversed ? -1 : +1;
    }
    if (next & EMIT_B) {
        _lastNext = R_B_NEXT;
        value += _reversed ? +1 : -1;
    }
}

FASTRUN
int Rotary_V12::process() {
    __disable_irq();
    int result = value;
    value = 0;
    __enable_irq();
    return result;
}
