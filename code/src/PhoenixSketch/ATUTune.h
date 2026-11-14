/*
Copyright (C) 2026 T41 EP Software Contributors
See Contributors.txt for list of known authors.

This file is part of Phoenix.

Phoenix is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Phoenix is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Phoenix.
If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef ATUTUNE_H
#define ATUTUNE_H

/**
 * @file ATUTune.h
 * @brief The ATU tuning engine: keys a low-power carrier and takes |Gamma| readings
 *
 * The engine runs from the main loop (ATUTuneService()) and never blocks. It puts
 * ModeSm in ATU_TUNE_SPACE (T/R relay in TX, carrier off), and for each reading a
 * program asks for:
 *   1. writes the ATU relays (carrier off) and waits for them to settle
 *   2. turns the carrier on (ATU_TUNE_MARK) and waits for it to settle
 *   3. collects SWR bridge readings and turns the carrier off
 *   4. converts them to |Gamma| with the band's bridge calibration
 * A program (a "planner") decides which setting to read next and when it is done.
 * Compiled only when ATU_PRESENT is defined.
 */

#include <stdint.h>

// Packed relay state, the format of GetATURegisterState()
#define ATU_PACK(net, bal, capsIn, l, c) \
    ((uint32_t)(l) | ((uint32_t)(c) << 8) | ((uint32_t)((capsIn) ? 1 : 0) << 16) | \
     ((uint32_t)((bal) ? 1 : 0) << 17) | ((uint32_t)((net) ? 1 : 0) << 18))
#define ATU_PACKED_L(s)       ((uint8_t)((s) & 0xFF))
#define ATU_PACKED_C(s)       ((uint8_t)(((s) >> 8) & 0xFF))
#define ATU_PACKED_CAPSIN(s)  (((s) >> 16) & 1)
#define ATU_PACKED_BAL(s)     (((s) >> 17) & 1)
#define ATU_PACKED_NET(s)     (((s) >> 18) & 1)
#define ATU_PACKED_BYPASS     0u

// Timing (ms)
#define ATU_TR_SETTLE_MS        30      // T/R relay, on entering ATU_TUNE_SPACE
#define ATU_RELAY_SETTLE_MS     15      // ATU relays (HF41F operate time <= 10 ms)
#define ATU_CARRIER_SETTLE_MS   25      // carrier on to the first bridge reading
#define ATU_COLLECT_MIN_MS      60      // collect for at least this long...
#define ATU_COLLECT_MIN_SAMPLES 5       // ...and at least this many bridge readings
#define ATU_COLLECT_TIMEOUT_MS  300     // give up on a reading after this long keyed
#define ATU_TUNE_TIMEOUT_MS     15000   // whole program
#define ATU_MIN_FWD_W           0.1f    // below this the reading is not valid
#define ATU_DEFAULT_TUNE_POWER_W 2.0f

#define ATU_MAX_READINGS        64      // readings kept in the record

typedef enum {
    ATU_PROG_NONE = 0,
    ATU_PROG_READ = 2,      // diagnostic: read bypass and the current setting, 3 times each
    ATU_PROG_TUNE = 3,      // the automatic tuner
    ATU_PROG_CALIBRATE = 4, // per-build coil calibration into a dummy load
} ATUProgram;

typedef enum {
    ATU_OK = 0,
    ATU_ABORTED = 1,                // operator or CAT stopped it
    ATU_REFUSED_BUSY = 2,           // already running, or the power calibration is
    ATU_REFUSED_MODE = 3,           // only from SSB or CW receive
    ATU_REFUSED_BAND = 4,           // transmit not allowed on this frequency
    ATU_REFUSED_UNCALIBRATED = 5,   // the SWR bridge is not calibrated on this band
    ATU_REFUSED_NO_BOARD = 6,       // the ATU expanders did not answer at startup
    ATU_FAIL_NO_RF = 7,             // forward power below ATU_MIN_FWD_W, or no readings
    ATU_FAIL_STATE = 8,             // ModeSm was not where the engine expected
    ATU_FAIL_TIMEOUT = 9,
    ATU_FAIL_ANTENNA = 10,          // every probe nearly totally reflected
    ATU_FAIL_NO_MATCH = 11,
} ATUResultCode;

/// What the last program did (CAT AX;)
typedef struct {
    ATUProgram program;
    ATUResultCode code;
    bool active;
    uint8_t n;                                  // readings taken
    uint32_t setting[ATU_MAX_READINGS];         // packed relay state of each reading
    float gamma[ATU_MAX_READINGS];              // |Gamma| of each reading
    uint32_t finalSetting;                      // the setting left on the relays
    float finalGamma;                           // its measured |Gamma| (-1 if not read)
    float predictedGamma;                       // the model's |Gamma| for it (-1 if none)
    float zRe, zIm;                             // load estimate (0 if none)
    float fwd_W;                                // forward power of the last reading
    uint32_t elapsed_ms;
} ATUTuneRecord;

/// One step a program asks the engine for
typedef enum {
    ATU_STEP_MEASURE,   // read this setting
    ATU_STEP_BUSY,      // still computing; ask again next pass
    ATU_STEP_DONE,      // finished: leave this setting on the relays
    ATU_STEP_FAIL,      // finished with an error: restore the operator's setting
} ATUStepKind;

typedef struct {
    ATUStepKind kind;
    uint32_t setting;
    ATUResultCode code;
} ATUStep;

/// A program run by the engine. next() is only called while the carrier is off.
typedef struct {
    void (*begin)(ATUTuneRecord *rec, uint32_t operatorSetting, double f_Hz);
    ATUStep (*next)(ATUTuneRecord *rec);
    void (*report)(ATUTuneRecord *rec, uint32_t setting, float gamma);
} ATUPlanner;

/**
 * @brief Start a program
 * @return ATU_OK if it started, otherwise why it was refused (also in the record)
 */
ATUResultCode ATUTuneStart(ATUProgram program);

/// Run the engine; call every main-loop pass
void ATUTuneService(void);

/// True while a program runs
bool ATUTuneActive(void);

/**
 * @brief Stop the running program now
 * @note Turns the carrier off, restores the operator's relay setting and returns
 *       ModeSm to receive before it returns
 */
void ATUTuneAbort(void);

/// The record of the running or last program
const ATUTuneRecord *ATUTuneLastRecord(void);

/// True if the engine may switch the ATU relays now (carrier off in ATU_TUNE_SPACE)
bool ATUTuneRelaysAllowed(void);

float ATUTunePower_W(void);
void SetATUTunePower_W(float power_W);

/**
 * @brief Transmit attenuation for the tune power
 * @note Used by HardwareSm on entering ATU_TUNE_SPACE / ATU_TUNE_MARK. Also selects
 *       the PA, as normal CW does.
 */
float ATUTuneAttenuation_dB(void);

/**
 * @brief True if this front-panel event should stop a running program
 * @param interrupt An InterruptType
 */
bool ATUTuneAbortsOn(int interrupt);

/// Planner for ATU_PROG_TUNE, in ATUTuneAlgo.cpp
extern const ATUPlanner ATUTunePlanner;

#endif // ATUTUNE_H
