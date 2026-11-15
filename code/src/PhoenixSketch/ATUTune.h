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
#define ATU_CARRIER_SETTLE_MS   15      // carrier on to the first bridge reading
#define ATU_COLLECT_MIN_MS      40      // collect for at least this long...
#define ATU_COLLECT_MIN_SAMPLES 4       // ...and at least this many bridge readings
#define ATU_COLLECT_TIMEOUT_MS  300     // give up on a reading after this long keyed
#define ATU_TUNE_TIMEOUT_MS     20000   // whole program (a search is at most 120 readings)
#define ATU_MIN_FWD_W           0.1f    // below this the reading is not valid
#define ATU_DEFAULT_TUNE_POWER_W 2.0f

#define ATU_MAX_READINGS        128     // readings kept in the record

typedef enum {
    ATU_PROG_NONE = 0,
    ATU_PROG_READ = 2,      // diagnostic: read bypass and the current setting, 3 times each
    ATU_PROG_TUNE = 3,      // the automatic tuner
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
    float predictedGamma;                       // unused by the search tuner (-1); kept so
    float zRe, zIm;                             // the AX; reply keeps its format (0)
    float fwd_W;                                // forward power of the last reading
    uint32_t elapsed_ms;
    uint32_t end_ms;                            // millis() when it ended or was refused
} ATUTuneRecord;

/// One step a program asks the engine for
typedef enum {
    ATU_STEP_MEASURE,   // read this setting
    ATU_STEP_BUSY,      // still computing; ask again next pass
    ATU_STEP_DONE,      // finished: leave this setting on the relays, with step.code
                        // (ATU_OK, or ATU_FAIL_NO_MATCH for the best of a poor match)
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

///////////////////////////////////////////////////////////////////////////////
// Memory: one setting per band and antenna, in ED (ATU_State, ATU_Freq_kHz, ATU_SWR)
///////////////////////////////////////////////////////////////////////////////

/**
 * @brief The stored setting for a band and antenna
 * @return false if nothing is stored (or band/antenna out of range)
 */
bool ATUMemoryGet(int32_t band, int32_t antenna, uint32_t *state, int32_t *f_kHz, float *swr);

/**
 * @brief Store a setting for a band and antenna
 * @param swr SWR measured with it, or 0 if not measured
 * @note Saves the configuration to flash
 */
void ATUMemoryStore(int32_t band, int32_t antenna, uint32_t state, int32_t f_kHz, float swr);

/// Forget the stored setting for a band and antenna (saves the configuration)
void ATUMemoryForget(int32_t band, int32_t antenna);

/// The current band and antenna (ED.currentBand, ED.antennaSelection)
int32_t ATUCurrentBand(void);
int32_t ATUCurrentAntenna(void);

/// The ATU is in use on this antenna (ED.ATU_Enabled); a disabled antenna is always bypassed
bool ATUAntennaEnabled(int32_t antenna);

/// This antenna is on the balun output (ED.ATU_BalunOut); every setting keeps K18 so
bool ATUAntennaBalun(int32_t antenna);

/// The network bypassed, on this antenna's output connector
uint32_t ATUBypassFor(int32_t antenna);

/**
 * @brief The setting a band and antenna should have: the stored one if the antenna is
 *        enabled and something is stored, else bypass; always on the antenna's output
 */
uint32_t ATUSettingFor(int32_t band, int32_t antenna);

/**
 * @brief Put ATUSettingFor(band, antenna) on the relays now
 * @return false if not done: tuning, transmitting, or no board
 */
bool ATUApplyForAntenna(int32_t band, int32_t antenna);

/**
 * @brief Apply ATUSettingFor() if the band or antenna has changed
 *
 * Called from HandleTuneState() on every tune update. It acts only when the band or
 * antenna differs from the last one applied, so a manual change made over CAT stays
 * until the next band or antenna change. It only switches relays in receive
 * (ATUChangeAllowed()); a change seen while transmitting is applied on the next call
 * in receive. Does nothing while a tuner program runs.
 */
void ATUApplyStoredIfChanged(int32_t band, int32_t antenna);

/**
 * @brief Enable or bypass the ATU on an antenna (saved), applied at once if it is the
 *        current antenna
 * @return false if refused: tuning, transmitting, or antenna out of range
 */
bool ATUSetEnabled(int32_t antenna, bool on);

/// Put an antenna on the balun (true) or coax output (saved, applied at once); as ATUSetEnabled
bool ATUSetBalunOut(int32_t antenna, bool on);

/// Forget the stored setting of every band for an antenna (saved; the current one is bypassed)
bool ATUMemoryClearAntenna(int32_t antenna);

/**
 * @brief Retune (ATU_PROG_TUNE): front-panel button 16, the menu, CAT AC and AX3
 * @return As ATUTuneStart(). If it started, the ATU is enabled on this antenna.
 */
ATUResultCode ATURetune(void);

/// Forget which band and antenna were last applied, so the next call applies (tests)
void ATUMemoryResetHook(void);

///////////////////////////////////////////////////////////////////////////////
// Home-screen status (the two lines to the right of SWR and PWR)
///////////////////////////////////////////////////////////////////////////////

/// How long a refusal, failure or poor match stays on the home screen
#define ATU_STATUS_HOLD_MS 3000

typedef enum { ATU_TEXT_NORMAL, ATU_TEXT_BUSY, ATU_TEXT_ALERT } ATUTextColour;

/**
 * @brief The ATU's two status lines for the home screen
 *
 * - network bypassed: "ATU BYP", or "ATU OFF" if the ATU is bypassed on this antenna;
 *   line 2 "BALUN" if the balun output is selected
 * - network in line: topology and nominal values, "LC 2.35uH" / "   340pF"
 * - tuning: "TUNING" / "n 23" (readings so far), ATU_TEXT_BUSY
 * - for ATU_STATUS_HOLD_MS after a refusal or failure: "TUNE" / the reason, ATU_TEXT_ALERT
 * - for ATU_STATUS_HOLD_MS after ATU_FAIL_NO_MATCH: the setting kept, ATU_TEXT_BUSY
 * - for ATU_STATUS_HOLD_MS after an antenna's memory is cleared: "MEM" / "CLEARED"
 *
 * @param line1 Receives the first line (at least 12 characters)
 * @param line2 Receives the second line (at least 12 characters)
 * @param n Size of each buffer
 * @return How to colour the lines
 */
ATUTextColour ATUStatusText(char *line1, char *line2, size_t n);

/**
 * @brief One row of the tuning status table: what is stored for a band and antenna
 *
 * "20M    14200 kHz  CL 0.82uH  157pF  1.25", "40M     7100 kHz  bypass        1.18",
 * or "15M    --" when nothing is stored. The SWR is "--" if it was not measured.
 *
 * @param n Size of buf (at least 48)
 */
void ATUStatusTableRow(int32_t band, int32_t antenna, char *buf, size_t n);

/// Planner for ATU_PROG_TUNE, in ATUTuneAlgo.cpp
extern const ATUPlanner ATUTunePlanner;

#endif // ATUTUNE_H
