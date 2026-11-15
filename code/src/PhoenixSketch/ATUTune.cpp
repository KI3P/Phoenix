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

/**
 * @file ATUTune.cpp
 * @brief The ATU tuning engine (see ATUTune.h)
 *
 * Phases of one reading, all timed from millis() so the main loop never waits:
 *
 *   PLAN -- write relays --> RELAY (15 ms) -- ATU_CARRIER_ON --> CARRIER (25 ms)
 *     ^                                                              |
 *     |                                            ClearSWRBridgeStats()
 *     +---- report |Gamma| <-- ATU_CARRIER_OFF <-- COLLECT (>= 60 ms, >= 5 readings)
 *
 * The relays only ever switch in ATU_TUNE_SPACE, with the carrier off. The T/R
 * relay stays in TX for the whole program, so it switches twice per program, not
 * twice per reading.
 */

#include "SDT.h"

#ifdef ATU_PRESENT

#include "ATUTune.h"

enum EnginePhase { PH_IDLE, PH_ENTER, PH_PLAN, PH_RELAY, PH_CARRIER, PH_COLLECT };

static struct {
    EnginePhase phase;
    const ATUPlanner *planner;
    uint32_t operatorSetting;   // relays before the program, restored on failure
    uint32_t pending;           // setting being read
    uint32_t start_ms;
    uint32_t t0_ms;             // start of the current phase
} eng = {PH_IDLE, NULL, 0, 0, 0, 0};

static ATUTuneRecord record;

///////////////////////////////////////////////////////////////////////////////
// The READ program: bypass three times, then the operator's setting three times
///////////////////////////////////////////////////////////////////////////////

static struct { uint32_t operatorSetting; uint8_t done; } readProg;

static void ReadBegin(ATUTuneRecord *rec, uint32_t operatorSetting, double f_Hz){
    readProg.operatorSetting = operatorSetting;
    readProg.done = 0;
}

static ATUStep ReadNext(ATUTuneRecord *rec){
    if (readProg.done < 3) return {ATU_STEP_MEASURE, ATU_PACKED_BYPASS, ATU_OK};
    if (readProg.done < 6) return {ATU_STEP_MEASURE, readProg.operatorSetting, ATU_OK};
    rec->finalGamma = rec->gamma[rec->n - 1];
    return {ATU_STEP_DONE, readProg.operatorSetting, ATU_OK};
}

static void ReadReport(ATUTuneRecord *rec, uint32_t setting, float gamma){
    readProg.done++;
}

static const ATUPlanner ReadPlanner = {ReadBegin, ReadNext, ReadReport};

///////////////////////////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////////////////////////

static inline bool InTuneStates(void){
    return (modeSM.state_id == ModeSm_StateId_ATU_TUNE_SPACE) ||
           (modeSM.state_id == ModeSm_StateId_ATU_TUNE_MARK);
}

bool ATUTuneRelaysAllowed(void){
    return modeSM.state_id == ModeSm_StateId_ATU_TUNE_SPACE;
}

float ATUTunePower_W(void){ return ED.ATU_TunePower_W; }

void SetATUTunePower_W(float power_W){
    if (power_W < 0.5f) power_W = 0.5f;
    if (power_W > 10.0f) power_W = 10.0f;
    ED.ATU_TunePower_W = power_W;
}

float ATUTuneAttenuation_dB(void){
    float p = ED.ATU_TunePower_W;
    if ((p < 0.5f) || (p > 10.0f)) p = ATU_DEFAULT_TUNE_POWER_W;
    return CalculateCWAttenuation(p, &ED.PA100Wactive);
}

///////////////////////////////////////////////////////////////////////////////
// Memory
///////////////////////////////////////////////////////////////////////////////

static inline bool MemoryIndexOK(int32_t band, int32_t antenna){
    return (band >= 0) && (band < NUMBER_OF_BANDS) && (antenna >= 0) && (antenna < ATU_NUM_ANTENNAS);
}

int32_t ATUCurrentBand(void){ return ED.currentBand[ED.activeVFO]; }

int32_t ATUCurrentAntenna(void){
    int32_t b = ATUCurrentBand();
    return ((b >= 0) && (b < NUMBER_OF_BANDS)) ? ED.antennaSelection[b] : 0;
}

bool ATUMemoryGet(int32_t band, int32_t antenna, uint32_t *state, int32_t *f_kHz, float *swr){
    if (!MemoryIndexOK(band, antenna)) return false;
    uint32_t s = ED.ATU_State[band][antenna];
    if (!(s & ATU_MEMORY_VALID)) return false;
    if (state) *state = s & ~ATU_MEMORY_VALID;
    if (f_kHz) *f_kHz = ED.ATU_Freq_kHz[band][antenna];
    if (swr) *swr = ED.ATU_SWR[band][antenna];
    return true;
}

void ATUMemoryStore(int32_t band, int32_t antenna, uint32_t state, int32_t f_kHz, float swr){
    if (!MemoryIndexOK(band, antenna)) return;
    ED.ATU_State[band][antenna] = (state & 0x7FFFFu) | ATU_MEMORY_VALID;
    ED.ATU_Freq_kHz[band][antenna] = f_kHz;
    ED.ATU_SWR[band][antenna] = swr;
    SaveDataToStorage(false);
}

void ATUMemoryForget(int32_t band, int32_t antenna){
    if (!MemoryIndexOK(band, antenna)) return;
    ED.ATU_State[band][antenna] = 0;
    ED.ATU_Freq_kHz[band][antenna] = 0;
    ED.ATU_SWR[band][antenna] = 0;
    SaveDataToStorage(false);
}

static int32_t appliedBand = -1;
static int32_t appliedAntenna = -1;

void ATUMemoryResetHook(void){
    appliedBand = -1;
    appliedAntenna = -1;
}

void ATUApplyStoredIfChanged(int32_t band, int32_t antenna){
    if ((band == appliedBand) && (antenna == appliedAntenna)) return;
    if (ATUTuneActive() || PowerCalAutoActive()) return;
    if (!ED.ATU_AutoApply || !MemoryIndexOK(band, antenna) ||
        !(bit_results.ATU_MCP23017_present && bit_results.ATU_MCP23008_present)){
        appliedBand = band;
        appliedAntenna = antenna;
        return;
    }
    if (!ATUChangeAllowed()) return;            // transmitting: try again in receive
    uint32_t s;
    SetATUPacked(ATUMemoryGet(band, antenna, &s, NULL, NULL) ? s : ATU_PACKED_BYPASS);
    appliedBand = band;
    appliedAntenna = antenna;
}

bool ATUTuneActive(void){ return eng.phase != PH_IDLE; }

const ATUTuneRecord *ATUTuneLastRecord(void){ return &record; }

bool ATUTuneAbortsOn(int interrupt){
    switch (interrupt){
        case iPTT_PRESSED:
        case iKEY1_PRESSED:
        case iKEY2_PRESSED:
        case iBUTTON_PRESSED:
        case iCENTERTUNE_INCREASE:
        case iCENTERTUNE_DECREASE:
        case iFINETUNE_INCREASE:
        case iFINETUNE_DECREASE:
        case iFILTER_INCREASE:
        case iFILTER_DECREASE:
        case iVFO_CHANGE:
        case iMODE:
        case iMODE_CHANGE:
        case iCALIBRATE_POWER:
        case iCALIBRATE_FREQUENCY:
        case iCALIBRATE_RX_IQ:
        case iCALIBRATE_TX_IQ:
            return true;
        default:
            return false;   // volume, and internal events, go on as normal
    }
}

/**
 * PRIVATE: End the program: carrier off, relays set, back to receive.
 *
 * @param code Result
 * @param setting Relays to leave in place (the program's answer, or the
 *        operator's setting on failure)
 */
static void Finish(ATUResultCode code, uint32_t setting){
    if (modeSM.state_id == ModeSm_StateId_ATU_TUNE_MARK)
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_ATU_CARRIER_OFF);
    if (modeSM.state_id == ModeSm_StateId_ATU_TUNE_SPACE)
        SetATUPacked(setting);
    if (InTuneStates())
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_ATU_TUNE_END);
    record.code = code;
    record.finalSetting = GetATURegisterState();
    record.active = false;
    record.elapsed_ms = millis() - eng.start_ms;
    eng.phase = PH_IDLE;
    Debug("ATU: program " + String((int)record.program) + " ended, code " + String((int)code)
          + ", " + String(record.n) + " readings, " + String(record.elapsed_ms) + " ms");
}

/**
 * PRIVATE: Convert the bridge statistics collected during a reading to |Gamma|.
 *
 * @return |Gamma|, or a negative value if the reading is not valid
 */
static float ReadingToGamma(const SWRBridgeStats &st){
    if (st.n < ATU_COLLECT_MIN_SAMPLES) return -1.0f;
    int32_t band = ED.currentBand[ED.activeVFO];
    float m_f = SWRCAL_NOMINAL_SLOPE_MV_PER_DB + ED.SWR_F_SlopeAdj[band];
    float b_f = ED.SWR_F_Offset[band] - SWRCAL_OFFSET_CONSTANT_DB;
    float m_r = SWRCAL_NOMINAL_SLOPE_MV_PER_DB + ED.SWR_R_SlopeAdj[band];
    float b_r = ED.SWR_R_Offset[band] - SWRCAL_OFFSET_CONSTANT_DB;
    float pf_dBm = SWRCalPower_dBm(st.fwdMean_mV, m_f, b_f);
    record.fwd_W = powf(10.0f, (pf_dBm - 30.0f) / 10.0f);
    if (record.fwd_W < ATU_MIN_FWD_W) return -1.0f;
    return SWRCalGamma(st.fwdMean_mV, st.refMean_mV, m_f, b_f, m_r, b_r);
}

///////////////////////////////////////////////////////////////////////////////
// Public engine functions
///////////////////////////////////////////////////////////////////////////////

ATUResultCode ATUTuneStart(ATUProgram program){
    ATUResultCode refuse = ATU_OK;
    const ATUPlanner *planner = NULL;
    switch (program){
        case ATU_PROG_READ: planner = &ReadPlanner; break;
        case ATU_PROG_TUNE: planner = &ATUTunePlanner; break;
        default: refuse = ATU_REFUSED_MODE; break;
    }
    int32_t band = ED.currentBand[ED.activeVFO];
    if (refuse != ATU_OK){
        // unknown program
    } else if (ATUTuneActive() || PowerCalAutoActive()){
        refuse = ATU_REFUSED_BUSY;
    } else if (!(bit_results.ATU_MCP23017_present && bit_results.ATU_MCP23008_present)){
        refuse = ATU_REFUSED_NO_BOARD;
    } else if ((modeSM.state_id != ModeSm_StateId_SSB_RECEIVE) &&
               (modeSM.state_id != ModeSm_StateId_CW_RECEIVE)){
        refuse = ATU_REFUSED_MODE;
    } else if (!IsTxAllowed()){
        refuse = ATU_REFUSED_BAND;
    } else if ((band < 0) || (band >= NUMBER_OF_BANDS) || ((ED.SWR_CalLevel[band] & 0x0F) == 0)){
        refuse = ATU_REFUSED_UNCALIBRATED;
    }
    if (refuse != ATU_OK){
        // Leave the last program's record alone unless nothing has run yet
        if (!ATUTuneActive()){
            record.program = program;
            record.code = refuse;
        }
        Debug("ATU: program " + String((int)program) + " refused, code " + String((int)refuse));
        return refuse;
    }

    memset(&record, 0, sizeof(record));
    record.program = program;
    record.active = true;
    record.finalGamma = -1.0f;
    record.predictedGamma = -1.0f;
    eng.planner = planner;
    eng.operatorSetting = GetATURegisterState();
    eng.start_ms = millis();
    eng.t0_ms = eng.start_ms;
    eng.phase = PH_ENTER;

    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_ATU_TUNE_START);
    if (modeSM.state_id != ModeSm_StateId_ATU_TUNE_SPACE){
        Finish(ATU_FAIL_STATE, eng.operatorSetting);
        return ATU_FAIL_STATE;
    }
    planner->begin(&record, eng.operatorSetting, (double)GetTXRXFreq_cHz() / 100.0);
    return ATU_OK;
}

void ATUTuneAbort(void){
    if (!ATUTuneActive()) return;
    Finish(ATU_ABORTED, eng.operatorSetting);
}

void ATUTuneService(void){
    if (eng.phase == PH_IDLE) return;
    uint32_t now = millis();

    if (!InTuneStates()){
        // Something else moved ModeSm; the relays were never switched hot, so
        // just record it. Restore the operator's setting if we are back in receive.
        if (ATUChangeAllowed()) SetATUPacked(eng.operatorSetting);
        Finish(ATU_FAIL_STATE, eng.operatorSetting);
        return;
    }
    if (now - eng.start_ms > ATU_TUNE_TIMEOUT_MS){
        Finish(ATU_FAIL_TIMEOUT, eng.operatorSetting);
        return;
    }

    switch (eng.phase){
        case PH_ENTER:{
            if (now - eng.t0_ms >= ATU_TR_SETTLE_MS) eng.phase = PH_PLAN;
            break;
        }
        case PH_PLAN:{
            ATUStep step = eng.planner->next(&record);
            switch (step.kind){
                case ATU_STEP_MEASURE:{
                    if (!ATUTuneRelaysAllowed()){
                        Finish(ATU_FAIL_STATE, eng.operatorSetting);
                        return;
                    }
                    SetATUPacked(step.setting);
                    eng.pending = step.setting;
                    eng.t0_ms = now;
                    eng.phase = PH_RELAY;
                    break;
                }
                case ATU_STEP_BUSY:
                    break;
                case ATU_STEP_DONE:
                    Finish(step.code, step.setting);   // ATU_FAIL_NO_MATCH keeps its best setting too
                    break;
                case ATU_STEP_FAIL:
                    Finish(step.code, eng.operatorSetting);
                    break;
            }
            break;
        }
        case PH_RELAY:{
            if (now - eng.t0_ms < ATU_RELAY_SETTLE_MS) break;
            ModeSm_dispatch_event(&modeSM, ModeSm_EventId_ATU_CARRIER_ON);
            if (modeSM.state_id != ModeSm_StateId_ATU_TUNE_MARK){
                Finish(ATU_FAIL_STATE, eng.operatorSetting);
                return;
            }
            eng.t0_ms = now;
            eng.phase = PH_CARRIER;
            break;
        }
        case PH_CARRIER:{
            if (now - eng.t0_ms < ATU_CARRIER_SETTLE_MS) break;
            ClearSWRBridgeStats();
            eng.t0_ms = now;
            eng.phase = PH_COLLECT;
            break;
        }
        case PH_COLLECT:{
            SWRBridgeStats st;
            GetSWRBridgeStats(&st);
            uint32_t elapsed = now - eng.t0_ms;
            bool enough = (st.n >= ATU_COLLECT_MIN_SAMPLES) && (elapsed >= ATU_COLLECT_MIN_MS);
            if (!enough && (elapsed < ATU_COLLECT_TIMEOUT_MS)) break;
            ModeSm_dispatch_event(&modeSM, ModeSm_EventId_ATU_CARRIER_OFF);
            float gamma = ReadingToGamma(st);
            if (gamma < 0.0f){
                Finish(ATU_FAIL_NO_RF, eng.operatorSetting);
                return;
            }
            if (record.n < ATU_MAX_READINGS){
                record.setting[record.n] = eng.pending;
                record.gamma[record.n] = gamma;
                record.n++;
            }
            eng.planner->report(&record, eng.pending, gamma);
            eng.phase = PH_PLAN;
            break;
        }
        default:
            break;
    }
}

#endif // ATU_PRESENT
