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
 * Automatic power and SWR bridge calibration: the AUTO branch of PowerCalSm.
 *
 * One session calibrates the PA power curve, the SSB gain and the SWR bridge on
 * every band. The operator fits each standard once: the 50 Ohm load (with the
 * wattmeter at levels 2 and 3), then the open, then the short. The load stage
 * sweeps the CW carrier (CALIBRATE_POWER) and ends each band with an SSB tone
 * point; the open and short are driven with the SSB tone (CALIBRATE_OFFSET), whose
 * level DSP gain sets, so that the radio is in true receive between their points.
 * See code/docs/PowerCalibration/AutoCalibration.md and
 * code/docs/swr_calibration_approach.md; the calculations are in SWRCalibration.cpp.
 *
 * The functions named Auto* are the state machine's actions and guards. They run
 * in the 1 ms timer interrupt, so they only record what they want: keyed or not,
 * CW or SSB, which band, and the work to do. PowerCalAutoService(), called from
 * the main loop, does it, and the state machine waits with the AutoServiceIdle()
 * guard. The service also restarts the state machine's count_ms, so the settle,
 * collect and rest times count from when the hardware has actually changed.
 */

#include "SDT.h"

#define AUTO_DEFAULT_BAND_MASK  (((1u << (BAND_6M + 1)) - 1) & ~((1u << BAND_160M) - 1))
#define AUTO_LOAD_SWEEP_POINTS  12    // levels 1 and 2: the attenuator sweep
#define AUTO_LEVEL3_POINTS       8    // level 3: 7 wattmeter levels plus the drift repeat
#define AUTO_LEVEL3_FIT_POINTS   7    // level 3: the points the forward line is fitted to
#define AUTO_LEVEL3_DRIFT_FIRST  1    // level 3: the point the drift repeat repeats
#define AUTO_MAX_LOAD_POINTS    14    // level 2: the sweep, the 2 W wattmeter point and the ceiling's
#define AUTO_LEVEL2_METER_POINT 12    // level 2: the wattmeter point near 2 W
#define AUTO_LEVEL2_CEILING_POINT 13  // level 2: the wattmeter point at 0 dB, the PA's ceiling
#define AUTO_REFLECTION_POINTS   6    // open and short sweeps
#define AUTO_REFLECTION_EXTRA_POINTS 3 // open and short: points a sweep may add for compressed ones left out
static_assert(SWRCAL_OSC_MAX_SHORT_FALLS == 3, "the log's oscillation reason says 3 points running");
#define AUTO_MIN_READINGS       20    // fewer bridge readings in a point fails the band (about 0.3 s)
#define AUTO_LEVEL2_TARGET_W     2.0f // level 2: wattmeter point near this power
#define AUTO_REFLECTION_START_W  1.2f // open and short: first point near this power
#define AUTO_REFLECTION_STEP_DB  2.0f // open and short: drive step (10 dB span over 6 points)
#define AUTO_S50_MIN_POWER_W     0.1f // S50: only load points at least this strong
#define AUTO_PSAT_MIN_MW      1000.0f // a 20 W PA curve outside these is not believed
#define AUTO_PSAT_MAX_MW     60000.0f
#define AUTO_SSB_CHANGE_MAX_DB  10.0f // nor an SSB correction this far from the stored one

// Acceptance limits (swr_calibration_approach.md Section 8)
#define AUTO_FORWARD_RMS_MAX_DB      0.3f
#define AUTO_FORWARD_WORST_MAX_DB    0.6f
#define AUTO_FORWARD_SLOPE_MIN       22.0f
#define AUTO_FORWARD_SLOPE_MAX       28.0f
#define AUTO_FORWARD_SLOPE_SPREAD    0.05f
#define AUTO_DRIFT_MAX_DB            0.2f
#define AUTO_LEVEL2_OFFSET_MAX_DB    3.0f  // 160-20 m
#define AUTO_LEVEL2_OFFSET_MAX_HI_DB 4.0f  // above 20 m
#define AUTO_REFLECTED_RMS_MAX_DB    0.2f
#define AUTO_REFLECTED_SLOPE_MAX     1.5f  // |m_r - m_f| in mV/dB
#define AUTO_GAP_MAX_DB              0.5f  // 160-20 m
#define AUTO_GAP_MAX_HI_DB           1.0f  // above 20 m
#define AUTO_S50_MAX                 1.3f  // below 20 MHz
#define AUTO_S50_MAX_HI              1.5f  // above 20 MHz

static const float32_t sweepAtten_dB[AUTO_LOAD_SWEEP_POINTS] = {0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 31.5};
static const float32_t level3Atten_dB[AUTO_LEVEL3_POINTS] = {0, 5, 10, 15, 20, 25, 31.5, 5};

// One calibration point: the drive and the averaged bridge readings
struct AutoPoint {
    float32_t drive_dB;   // attenuation (CW) or DSP gain (SSB)
    float32_t meter_W;    // wattmeter reading, or 0 if none
    float32_t vf, vfSD;   // forward detector mean and standard deviation, mV
    float32_t vr, vrSD;   // reflected detector mean and standard deviation, mV
    uint32_t n;           // number of readings
};

// Everything measured on one band this session
struct AutoBandData {
    AutoPoint load[AUTO_MAX_LOAD_POINTS];
    uint32_t nLoad;
    AutoPoint ssb;        // the load's SSB tone point
    bool hasSSB;
    AutoPoint open[AUTO_REFLECTION_POINTS];
    uint32_t nOpen;
    AutoPoint shrt[AUTO_REFLECTION_POINTS];
    uint32_t nShort;
    bool fewReadings;     // a point had fewer than AUTO_MIN_READINGS readings
    uint32_t noLoadPoints;// load points that reflected everything: no 50 Ohm load
};

// In RAM2 (DMAMEM), which is not cleared at start-up: AutoCalEnter() clears the
// results and AutoBeginSession() the data of each band before they are used
static AutoBandData DMAMEM bandData[NUMBER_OF_BANDS];
static AutoCalBandResult DMAMEM results[NUMBER_OF_BANDS];

// The operator's settings, restored when AUTO ends
static int32_t savedBand;
static int64_t savedCenterFreq_Hz;
static int64_t savedFineTuneFreq_Hz;
static ModulationType savedModulation;
static float32_t savedXAttenCW[NUMBER_OF_BANDS];

// Fields in declaration order: active, level, bandMask; the rest start at zero
AutoCalSession autoCal = {false, 1, AUTO_DEFAULT_BAND_MASK};

extern float32_t targetPower;  // HardwareSm_PowerCalibration.cpp
void ResetPowerTarget(void);   // HardwareSm_PowerCalibration.cpp
bool AutoNeedsMeterEntry(void);

const AutoCalBandResult *GetAutoCalResult(int32_t band){
    if ((band < FIRST_BAND) || (band > LAST_BAND))
        band = FIRST_BAND;
    return &results[band];
}

static void RequestAutoService(void){
    autoCal.servicePending = true;
}

static bool CalibrationKeyed(void){
    return (modeSM.state_id == ModeSm_StateId_CALIBRATE_POWER_MARK) ||
           (modeSM.state_id == ModeSm_StateId_CALIBRATE_OFFSET_MARK);
}

static bool CalibrationSSB(void){
    return (modeSM.state_id == ModeSm_StateId_CALIBRATE_OFFSET_SPACE) ||
           (modeSM.state_id == ModeSm_StateId_CALIBRATE_OFFSET_MARK);
}

static const char *StageName(AutoCalStage stage){
    switch (stage){
        case AUTO_STAGE_LOAD:  return "load";
        case AUTO_STAGE_OPEN:  return "open";
        default:               return "short";
    }
}

/**
 * @brief The forward line the level 1 defaults give for a band
 */
static float32_t Level1Intercept(int32_t band){
    return SWRCalLevel1ForwardOffset(band, ED.SWR_CouplerTurns) - SWRCAL_OFFSET_CONSTANT_DB;
}

/**
 * @brief Level 2: the attenuation, among the sweep's, that gave nearest 2 W
 * @note Read through the level 1 defaults, which are close enough for this
 */
static float32_t Level2MeterAttenuation(int32_t band){
    const AutoBandData *bd = &bandData[band];
    float32_t target_dBm = 10.0f * log10f(1000.0f * AUTO_LEVEL2_TARGET_W);
    float32_t best = sweepAtten_dB[AUTO_LOAD_SWEEP_POINTS / 2];
    float32_t bestError = 1e9f;
    for (uint32_t k = 0; (k < bd->nLoad) && (k < AUTO_LOAD_SWEEP_POINTS); k++){
        float32_t P_dBm = SWRCalPower_dBm(bd->load[k].vf, SWRCAL_NOMINAL_SLOPE_MV_PER_DB, Level1Intercept(band));
        if (fabsf(P_dBm - target_dBm) < bestError){
            bestError = fabsf(P_dBm - target_dBm);
            best = bd->load[k].drive_dB;
        }
    }
    return best;
}

/**
 * @brief SSB correction that the open and short sweeps start from
 * @note The band's new one from the load stage, or the stored one if the load
 *       stage was skipped
 */
static float32_t ReflectionBaseCorrection(int32_t band){
    if (results[band].hasPACurve)
        return results[band].ssbCorrection_dB;
    return ED.PowerCal_20W_DSP_Gain_correction_dB[band];
}

/**
 * @brief Drive of the current point (Section 6, point lists in AutoCalibration.md)
 * @return Attenuation in dB for a CW point, DSP gain in dB for an SSB point
 */
static float32_t PointDrive(void){
    int32_t band = autoCal.band;
    if (autoCal.stage == AUTO_STAGE_LOAD){
        if (autoCal.ssbPoint)
            // The stored correction makes the tone about SSB_20W_CAL_POWER_POINT_W
            return ED.PowerCal_20W_DSP_Gain_correction_dB[band];
        if (autoCal.level == 3)
            return level3Atten_dB[autoCal.point < AUTO_LEVEL3_POINTS ? autoCal.point : 0];
        if (autoCal.point < AUTO_LOAD_SWEEP_POINTS)
            return sweepAtten_dB[autoCal.point];
        if (autoCal.point == AUTO_LEVEL2_METER_POINT)
            return Level2MeterAttenuation(band);
        return 0.0f; // the ceiling
    }
    return ReflectionBaseCorrection(band)
           + 10.0f * log10f(AUTO_REFLECTION_START_W / SSB_20W_CAL_POWER_POINT_W)
           - AUTO_REFLECTION_STEP_DB * autoCal.point;
}

static void ApplyDrive(void){
    autoCal.drive_dB = PointDrive();
    if (autoCal.ssbMode){
        SetSSBGainOverride(autoCal.drive_dB);
    } else {
        // RFCWMark does not re-apply ED.XAttenCW while it stays keyed, so set
        // the attenuator directly as well
        ED.XAttenCW[autoCal.band] = autoCal.drive_dB;
        SetTXAttenuation(autoCal.drive_dB);
    }
}

/**
 * @brief Wattmeter reading the operator entered, in W
 */
static float32_t MeterWatts(void){
    float32_t p = GetMeasuredPower();
    if (GetPowerUnit() == 0)
        p = powf(10.0f, p / 10.0f) / 1000.0f;
    return p;
}

static void LogPoint(const char *kind, const AutoPoint *p){
    char line[160];
    snprintf(line, sizeof(line), "AUTOCAL,%s,%s,%s,%.2f,%.4f,%.1f,%.1f,%.1f,%.1f,%lu",
             bands[autoCal.band].name, StageName(autoCal.stage), kind, p->drive_dB, p->meter_W,
             p->vf, p->vfSD, p->vr, p->vrSD, (unsigned long)p->n);
    Debug(line);
}

/**
 * @brief Log a decision to leave a point or a result out, or to fail a band
 * @param band Band
 * @param stage Standard ("load", "open", "short"), or "band" for the band as a whole
 * @param what The point or result, such as "cw 3.00 dB" or "PA curve"
 * @param reason Why, with the units of value and limit in brackets
 * @param value The measured value
 * @param limit The rule's limit
 * @note One CSV row: AUTOCAL-SKIP,<band>,<stage>,<what>,<reason>,<value>,<limit>.
 *       Runs in the main loop only, from PowerCalAutoService().
 */
static void LogSkip(int32_t band, const char *stage, const char *what, const char *reason,
                    float32_t value, float32_t limit){
    char line[220];
    snprintf(line, sizeof(line), "AUTOCAL-SKIP,%s,%s,%s,%s,%.3f,%.3f",
             bands[band].name, stage, what, reason, value, limit);
    Debug(line);
}

// Names of the AUTO_FAIL_* bits, short for the screen's table and long for the review and the log
static const char *failShort[] = {"few rdg", "fwd fit", "fwd slp", "drift", "L2 offs", "no refl",
                                  "ref fit", "ref slp", "gap", "S50", "PA fit", "no load"};
static const char *failLong[] = {"too few readings", "forward fit", "forward slope", "drift",
                                 "level 2 offset", "open and short abandoned", "reflected fit",
                                 "reflected slope", "open/short gap", "load SWR too high",
                                 "PA curve fit", "50 Ohm load missing"};
static_assert(sizeof(failShort) / sizeof(failShort[0]) == AUTO_FAIL_BIT_COUNT, "a short name per fail bit");
static_assert(sizeof(failLong) / sizeof(failLong[0]) == AUTO_FAIL_BIT_COUNT, "a long name per fail bit");

const char *AutoCalFailName(uint32_t bit, bool shortName){
    if (bit >= AUTO_FAIL_BIT_COUNT)
        return "?";
    return shortName ? failShort[bit] : failLong[bit];
}

uint32_t AutoCalFirstFailBit(uint32_t failBits){
    for (uint32_t k = 0; k < AUTO_FAIL_BIT_COUNT; k++)
        if (failBits & (1u << k)) return k;
    return 0;
}

/** Append a value to a table row, or a dash in its place when the band has none */
static void AppendCell(char *row, size_t size, bool has, const char *format, float32_t value, int width){
    size_t used = strlen(row);
    if (used >= size - 1)
        return;
    if (has)
        snprintf(row + used, size - used, format, value);
    else
        snprintf(row + used, size - used, " %*s", width, "-");
}

void PrintAutoCalTable(void){
    char row[256];
    snprintf(row, sizeof(row), "Auto calibration results, level %u:", autoCal.level);
    Debug(row);
    snprintf(row, sizeof(row), "%-5s %-6s %6s %7s %6s %7s %7s %7s %6s %5s %7s  %-10s  %s",
             "Band", "Status", "o_f", "s_f", "o_r", "s_r", "Psat W", "k", "shape", "S50", "SSB dB",
             "Reflected", "Fails");
    Debug(row);
    for (int32_t band = FIRST_BAND; band <= LAST_BAND; band++){
        const AutoCalBandResult *r = &results[band];
        if (!r->inSession)
            continue;
        const char *status = !r->checked ? "-" : (r->failBits ? "FAIL" : "pass");
        snprintf(row, sizeof(row), "%-5s %-6s", bands[band].name, status);
        AppendCell(row, sizeof(row), r->hasForward, " %6.2f", r->b_f + SWRCAL_OFFSET_CONSTANT_DB, 6);
        AppendCell(row, sizeof(row), r->hasForward, " %7.3f", r->m_f - SWRCAL_NOMINAL_SLOPE_MV_PER_DB, 7);
        AppendCell(row, sizeof(row), r->hasReflected, " %6.2f", r->b_r + SWRCAL_OFFSET_CONSTANT_DB, 6);
        AppendCell(row, sizeof(row), r->hasReflected, " %7.3f", r->m_r - SWRCAL_NOMINAL_SLOPE_MV_PER_DB, 7);
        AppendCell(row, sizeof(row), r->hasPACurve, " %7.2f", r->Psat_mW / 1000.0f, 7);
        AppendCell(row, sizeof(row), r->hasPACurve, " %7.2f", r->kindex, 7);
        AppendCell(row, sizeof(row), r->hasPACurve, " %6.3f", r->shape > 0.0f ? r->shape : 1.0f, 6);
        AppendCell(row, sizeof(row), r->hasS50, " %5.2f", r->S50, 5);
        AppendCell(row, sizeof(row), r->hasPACurve, " %7.2f", r->ssbCorrection_dB, 7);
        size_t used = strlen(row);
        snprintf(row + used, sizeof(row) - used, "  %-10s ",
                 !r->hasReflected ? "-" : (r->openOnly ? "open only" : (r->shortOnly ? "short only" : "open+short")));
        // Every reason the band failed, in full
        const char *separator = " ";
        for (uint32_t k = 0; k < AUTO_FAIL_BIT_COUNT; k++){
            if (!(r->failBits & (1u << k)))
                continue;
            used = strlen(row);
            snprintf(row + used, sizeof(row) - used, "%s%s", separator, failLong[k]);
            separator = ", ";
        }
        Debug(row);
    }
    Debug("o: offset dB; s_f, s_r: slope adjustment mV/dB; P = Psat tanh((k x)^shape)^(1/shape), x = drive");
}

/** A point's name for the log, such as "cw 3.00 dB" */
static const char *PointName(const char *kind, float32_t drive_dB){
    static char name[32];
    snprintf(name, sizeof(name), "%s %.2f dB", kind, drive_dB);
    return name;
}

/**
 * @brief STORE: keep the point's readings and run the oscillation check
 */
static void StorePoint(void){
    int32_t band = autoCal.band;
    AutoBandData *bd = &bandData[band];
    SWRBridgeStats st;
    GetSWRBridgeStats(&st);
    AutoPoint p = {autoCal.drive_dB, 0, st.fwdMean_mV, st.fwdSD_mV, st.refMean_mV, st.refSD_mV, st.n};
    autoCal.lastVf_mV = p.vf;
    autoCal.lastVfSD_mV = p.vfSD;
    autoCal.lastVr_mV = p.vr;
    autoCal.lastVrSD_mV = p.vrSD;
    autoCal.lastN = p.n;
    autoCal.oscillation = false;
    const char *stage = StageName(autoCal.stage);
    const char *kind = (autoCal.stage != AUTO_STAGE_LOAD) ? "tone"
                     : (autoCal.ssbPoint ? "ssb" : (AutoNeedsMeterEntry() ? "meter" : "cw"));
    if (st.rejected > 0)
        LogSkip(band, stage, PointName(kind, p.drive_dB),
                "readings rejected: outside 0-4096 mV (a failed I2C read) (count)", st.rejected, 0);
    if (p.n < autoCal.minReadings){
        bd->fewReadings = true;
        LogSkip(band, stage, PointName(kind, p.drive_dB), "too few bridge readings: the band fails (count)",
                p.n, autoCal.minReadings);
    }

    if (autoCal.stage == AUTO_STAGE_LOAD){
        // An open, a short or a loose connector reflects everything: V_r then
        // reads like V_f, and the band's power curve would come from a mismatch
        if (!SWRCalLoadPresent(p.vf, p.vr)){
            if (bd->noLoadPoints == 0)
                LogSkip(band, stage, PointName(kind, p.drive_dB),
                        "no load: V_f minus V_r below the limit so the band fails: is the 50 Ohm load fitted? (mV)",
                        p.vf - p.vr, SWRCAL_LOAD_MIN_GAP_MV);
            bd->noLoadPoints++;
        }
        if (autoCal.ssbPoint){
            bd->ssb = p;
            bd->hasSSB = true;
            LogPoint("ssb", &p);
            return;
        }
        if (AutoNeedsMeterEntry())
            p.meter_W = MeterWatts();
        if (autoCal.point < AUTO_MAX_LOAD_POINTS){
            bd->load[autoCal.point] = p;
            bd->nLoad = autoCal.point + 1;
        }
        LogPoint(p.meter_W > 0 ? "meter" : "cw", &p);
        autoCal.point++;
        return;
    }

    // Open or short: SSB tone. Each point's V_f must fall with the drive. The
    // first point has nothing to fall from, so it is checked against the load's
    // SSB point scaled to its drive. Only the first: the PA puts several dB more
    // or less into an open or short than into 50 Ohm, differently on each band
    // and radio, so the falls are the dependable test.
    // A fall that is too small is not by itself an oscillation: near its ceiling
    // the PA compresses into an open or short (radio 2's 20, 17 and 12 m open on
    // 2026-11-02 fell 21, 14 and 5 mV for 2 dB). The previous point is left out
    // and the sweep goes one point lower; only a rise, a jump in the spread (an
    // oscillation starting during the point, whose mean can still look like a
    // short fall), or short falls running abandon it (SWRCalSweepStep()).
    AutoPoint *sweep = (autoCal.stage == AUTO_STAGE_OPEN) ? bd->open : bd->shrt;
    uint32_t *count = (autoCal.stage == AUTO_STAGE_OPEN) ? &bd->nOpen : &bd->nShort;
    bool hasPrevious = (*count > 0);
    float32_t Vf_previous = hasPrevious ? sweep[*count - 1].vf : 0;
    float32_t VfSD_previous = hasPrevious ? sweep[*count - 1].vfSD : 0;
    float32_t drop_dB = hasPrevious ? sweep[*count - 1].drive_dB - p.drive_dB : 0;
    bool hasReference = !hasPrevious && bd->hasSSB;
    float32_t Vf_reference = hasReference
        ? bd->ssb.vf + (p.drive_dB - bd->ssb.drive_dB) * SWRCAL_NOMINAL_SLOPE_MV_PER_DB : 0;
    enum SWRCalOscillationRule rule =
        SWRCalOscillationCheck(hasPrevious, Vf_previous, drop_dB, p.vf, hasReference, Vf_reference,
                               VfSD_previous, p.vfSD);
    float32_t fall = Vf_previous - p.vf;
    float32_t needed = SWRCAL_OSC_DROP_FRACTION * drop_dB * SWRCAL_NOMINAL_SLOPE_MV_PER_DB;
    switch (SWRCalSweepStep(rule, &autoCal.shortFalls)){
        case SWRCAL_SWEEP_ABANDON:
            autoCal.oscillation = true;
            LogPoint("oscillation", &p);
            if (rule == SWRCAL_OSC_RISE)
                LogSkip(band, stage, PointName(kind, p.drive_dB), "oscillation: V_f rose as the drive fell (mV)",
                        -fall, SWRCAL_OSC_RISE_DB * SWRCAL_NOMINAL_SLOPE_MV_PER_DB);
            else if (rule == SWRCAL_OSC_UNSTEADY)
                LogSkip(band, stage, PointName(kind, p.drive_dB), "oscillation: V_f unsteady as its spread jumped (mV)",
                        p.vfSD, fmaxf(SWRCAL_OSC_SD_MIN_MV, SWRCAL_OSC_SD_JUMP * VfSD_previous));
            else if (rule == SWRCAL_OSC_SHORT_FALL)
                LogSkip(band, stage, PointName(kind, p.drive_dB),
                        "oscillation: V_f fell too little at 3 points running (mV)", fall, needed);
            else
                LogSkip(band, stage, PointName(kind, p.drive_dB), "oscillation: V_f above the 50 Ohm reference (mV)",
                        p.vf - Vf_reference, SWRCAL_OSC_MARGIN_DB * SWRCAL_NOMINAL_SLOPE_MV_PER_DB);
            LogSkip(band, stage, "sweep", (autoCal.stage == AUTO_STAGE_OPEN)
                        ? "abandoned: reflected line from the short alone (points kept)"
                        : "abandoned: reflected line from the open alone (points kept)",
                    *count, 0);
            break;
        case SWRCAL_SWEEP_DROP_PREVIOUS:
            (*count)--;
            LogSkip(band, stage, PointName(kind, sweep[*count].drive_dB),
                    "compression: the next point fell too little so this one is left out (mV)", fall, needed);
            if (autoCal.extraPoints < AUTO_REFLECTION_EXTRA_POINTS){
                autoCal.extraPoints++;
                autoCal.nPoints++;
            }
            // fall through: keep this point
        case SWRCAL_SWEEP_KEEP:
            if (*count < AUTO_REFLECTION_POINTS){
                sweep[(*count)++] = p;
                LogPoint("tone", &p);
            }
            break;
    }
    autoCal.point++;
}

/**
 * @brief FIT_FORWARD: the band's forward line for the session's level (Section 7)
 */
static void FitForward(int32_t band){
    AutoCalBandResult *r = &results[band];
    const AutoBandData *bd = &bandData[band];
    r->hasForward = false;
    switch (autoCal.level){
        case 2:{
            const AutoPoint *p = &bd->load[AUTO_LEVEL2_METER_POINT];
            if ((bd->nLoad > AUTO_LEVEL2_METER_POINT) && (p->meter_W > 0)){
                r->m_f = SWRCAL_NOMINAL_SLOPE_MV_PER_DB;
                r->b_f = SWRCalForwardInterceptLevel2(p->meter_W, p->vf);
                r->level2Delta_dB = r->b_f - Level1Intercept(band);
                r->hasForward = true;
            } else {
                LogSkip(band, "load", "forward line", "no wattmeter reading near 2 W (W)",
                        (bd->nLoad > AUTO_LEVEL2_METER_POINT) ? p->meter_W : 0.0f, 0);
            }
            break;
        }
        case 3:{
            float32_t V[AUTO_LEVEL3_FIT_POINTS], P[AUTO_LEVEL3_FIT_POINTS], att[AUTO_LEVEL3_FIT_POINTS];
            uint32_t n = 0;
            for (uint32_t k = 0; (k < bd->nLoad) && (k < AUTO_LEVEL3_FIT_POINTS); k++){
                if (bd->load[k].meter_W <= 0){
                    LogSkip(band, "load", PointName("meter", bd->load[k].drive_dB),
                            "no wattmeter reading: left out of the forward fit and PA curve (W)",
                            bd->load[k].meter_W, 0);
                    continue;
                }
                V[n] = bd->load[k].vf;
                P[n] = bd->load[k].meter_W;
                att[n] = bd->load[k].drive_dB;
                n++;
            }
            bool dropped[AUTO_LEVEL3_FIT_POINTS];
            float32_t residual[AUTO_LEVEL3_FIT_POINTS];
            struct SWRLineFit f = SWRCalFitForwardLevel3(V, P, att, n, &r->ceilingDropped, dropped, residual);
            for (uint32_t k = 0; k < n; k++){
                if (dropped[k])
                    LogSkip(band, "load", PointName("meter", att[k]),
                            "ceiling point off the line: left out of the forward fit (dB)",
                            residual[k], SWRCAL_CEILING_RESIDUAL_DB);
            }
            if (!f.valid)
                LogSkip(band, "load", "forward line", "fit failed: too few points (count)", n, 2);
            if (f.valid){
                r->m_f = f.m;
                r->b_f = f.b;
                r->forwardRms_dB = f.rms_dB;
                r->forwardWorst_dB = f.worst_dB;
                r->hasForward = true;
            }
            // Drift: how far the repeated point's offset from the line moved
            if (f.valid && (bd->nLoad == AUTO_LEVEL3_POINTS)){
                const AutoPoint *a = &bd->load[AUTO_LEVEL3_DRIFT_FIRST];
                const AutoPoint *z = &bd->load[AUTO_LEVEL3_POINTS - 1];
                if ((a->meter_W > 0) && (z->meter_W > 0))
                    r->drift_dB = (10.0f * log10f(1000.0f * z->meter_W) - z->vf / f.m)
                                - (10.0f * log10f(1000.0f * a->meter_W) - a->vf / f.m);
            }
            break;
        }
        default:
            r->m_f = SWRCAL_NOMINAL_SLOPE_MV_PER_DB;
            r->b_f = Level1Intercept(band);
            r->hasForward = true;
            break;
    }
    autoCal.resultsVersion++;
}

/**
 * @brief BAND_DONE on the load: PA curve and SSB gain correction
 */
static void FinishLoadBand(int32_t band){
    AutoCalBandResult *r = &results[band];
    const AutoBandData *bd = &bandData[band];
    r->hasPACurve = false;
    // The open and short are driven from this band's SSB correction, so it must
    // come from real readings: a dead bridge readout would otherwise key the tone
    // at a huge DSP gain into the open
    if (!r->hasForward){
        LogSkip(band, "load", "PA curve and SSB gain", "no forward line", 0, 0);
        return;
    }
    if (!bd->hasSSB || (bd->ssb.n < autoCal.minReadings)){
        LogSkip(band, "load", "PA curve and SSB gain", "SSB point missing or too few readings (count)",
                bd->hasSSB ? bd->ssb.n : 0, autoCal.minReadings);
        return;
    }
    for (uint32_t k = 0; k < bd->nLoad; k++){
        if (bd->load[k].n < autoCal.minReadings){
            LogSkip(band, "load", "PA curve and SSB gain", "a load point had too few readings (count)",
                    bd->load[k].n, autoCal.minReadings);
            return;
        }
    }
    float32_t att[AUTO_MAX_LOAD_POINTS], P_mW[AUTO_MAX_LOAD_POINTS];
    uint32_t n = 0;
    if (autoCal.level == 3){
        // The wattmeter readings directly, without the drift repeat
        for (uint32_t k = 0; (k < bd->nLoad) && (k < AUTO_LEVEL3_FIT_POINTS); k++){
            if (bd->load[k].meter_W <= 0) continue;
            att[n] = bd->load[k].drive_dB;
            P_mW[n] = 1000.0f * bd->load[k].meter_W;
            n++;
        }
    } else {
        // The sweep read through the forward line
        for (uint32_t k = 0; (k < bd->nLoad) && (k < AUTO_LOAD_SWEEP_POINTS); k++){
            att[n] = bd->load[k].drive_dB;
            P_mW[n] = powf(10.0f, SWRCalPower_dBm(bd->load[k].vf, r->m_f, r->b_f) / 10.0f);
            n++;
        }
        // Level 2: the PA's ceiling from the wattmeter. A forward detector can
        // over-read all through saturation (radio 1's 10 m: up to 2.3 dB from 6 dB
        // attenuation down), so no point may read above the measured ceiling, and
        // the 0 dB point is the wattmeter's.
        const AutoPoint *c = &bd->load[AUTO_LEVEL2_CEILING_POINT];
        if ((autoCal.level == 2) && (bd->nLoad > AUTO_LEVEL2_CEILING_POINT) && (c->meter_W > 0)){
            float32_t ceiling_mW = 1000.0f * c->meter_W;
            r->ceilingOverRead_dB = SWRCalPower_dBm(c->vf, r->m_f, r->b_f) - 10.0f * log10f(ceiling_mW);
            for (uint32_t k = 0; k < n; k++){
                if (att[k] <= 0.0f){
                    LogSkip(band, "load", PointName("cw", att[k]),
                            "PA curve uses the wattmeter's ceiling instead: detector minus wattmeter (dB)",
                            10.0f * log10f(P_mW[k] / ceiling_mW), 0);
                    P_mW[k] = ceiling_mW;
                } else if (P_mW[k] > ceiling_mW){
                    LogSkip(band, "load", PointName("cw", att[k]),
                            "above the wattmeter ceiling: PA curve uses the ceiling (dB)",
                            10.0f * log10f(P_mW[k] / ceiling_mW), 0);
                    P_mW[k] = ceiling_mW;
                }
            }
        }
    }
    if (n < 3){
        LogSkip(band, "load", "PA curve and SSB gain", "too few points (count)", n, 3);
        return;
    }
    struct FitResult f = FitPowerCurve(att, P_mW, n, 15000.0f, 6.0f);
    if (!isfinite(f.P_sat) || !isfinite(f.k) || (f.P_sat < AUTO_PSAT_MIN_MW) ||
        (f.P_sat > AUTO_PSAT_MAX_MW) || (f.k <= 0)){
        LogSkip(band, "load", "PA curve and SSB gain", "P_sat out of range (W)", f.P_sat / 1000.0f,
                (f.P_sat < AUTO_PSAT_MIN_MW) ? AUTO_PSAT_MIN_MW / 1000.0f : AUTO_PSAT_MAX_MW / 1000.0f);
        return;
    }
    // The tone was sent with DSP gain ssb.drive_dB; scale it to the setpoint
    float32_t Pssb_dBm = SWRCalPower_dBm(bd->ssb.vf, r->m_f, r->b_f);
    float32_t correction = bd->ssb.drive_dB + 10.0f * log10f(1000.0f * SSB_20W_CAL_POWER_POINT_W) - Pssb_dBm;
    if (!isfinite(correction) ||
        (fabsf(correction - ED.PowerCal_20W_DSP_Gain_correction_dB[band]) > AUTO_SSB_CHANGE_MAX_DB)){
        LogSkip(band, "load", "PA curve and SSB gain", "SSB correction too far from the stored one (dB)",
                correction - ED.PowerCal_20W_DSP_Gain_correction_dB[band], AUTO_SSB_CHANGE_MAX_DB);
        return;
    }
    r->Psat_mW = f.P_sat;
    r->kindex = f.k;
    r->shape = f.shape;
    r->ssbCorrection_dB = correction;
    r->hasPACurve = true;
    autoCal.resultsVersion++;
}

/**
 * @brief FIT_ALL: reflected lines, S50 and the acceptance checks for every band
 */
static void FitAndCheckAll(void){
    float32_t slopes[NUMBER_OF_BANDS];
    uint32_t nSlopes = 0;
    for (int32_t band = FIRST_BAND; band <= LAST_BAND; band++){
        AutoCalBandResult *r = &results[band];
        AutoBandData *bd = &bandData[band];
        if (!r->inSession)
            continue;
        uint32_t bit = 1u << band;
        uint32_t fail = 0;
        bool loadRan = !autoCal.loadSkipped;
        bool high = (band > BAND_20M);
        // Fail a check, and log which one, with its value and limit
        auto Fail = [&](uint32_t check, const char *reason, float32_t value, float32_t limit){
            fail |= check;
            LogSkip(band, "band", "band fails", reason, value, limit);
        };

        if (!r->hasForward && autoCal.loadSkipped){
            // Level 1 without the load stage: the defaults
            r->m_f = SWRCAL_NOMINAL_SLOPE_MV_PER_DB;
            r->b_f = Level1Intercept(band);
            r->hasForward = true;
        }
        if (!r->hasForward)
            Fail(AUTO_FAIL_FORWARD_FIT, "no forward line", 0, 0);
        bool openFailed = (autoCal.openFailMask & bit) != 0;
        bool shortFailed = (autoCal.shortFailMask & bit) != 0;
        if (shortFailed && !openFailed)
            LogSkip(band, "short", "short sweep", "abandoned: reflected line from the open alone (points)",
                    bd->nShort, 0);
        if (openFailed && !shortFailed)
            LogSkip(band, "open", "open sweep", "abandoned: reflected line from the short alone (points)",
                    bd->nOpen, 0);

        // Reflected line: the open and short, or whichever of them was not abandoned
        r->hasReflected = false;
        uint32_t nOpen = openFailed ? 0 : bd->nOpen;
        uint32_t nShort = shortFailed ? 0 : bd->nShort;
        if (r->hasForward){
            float32_t Vfo[AUTO_REFLECTION_POINTS], Vro[AUTO_REFLECTION_POINTS];
            float32_t Vfs[AUTO_REFLECTION_POINTS], Vrs[AUTO_REFLECTION_POINTS];
            for (uint32_t k = 0; k < nOpen; k++){ Vfo[k] = bd->open[k].vf; Vro[k] = bd->open[k].vr; }
            for (uint32_t k = 0; k < nShort; k++){ Vfs[k] = bd->shrt[k].vf; Vrs[k] = bd->shrt[k].vr; }
            struct SWRReflectedFit f = SWRCalFitReflected(Vfo, Vro, nOpen, Vfs, Vrs, nShort, r->m_f, r->b_f);
            if (f.valid){
                r->hasReflected = true;
                r->openOnly = f.openOnly;
                r->shortOnly = f.shortOnly;
                r->m_r = f.m;
                r->b_r = f.b;
                r->gap_dB = f.b_open - f.b_short;
                r->reflectedRms_dB = f.rms_dB;
            }
        }
        if (!r->hasReflected && r->hasForward){
            if (openFailed && shortFailed)
                Fail(AUTO_FAIL_OPEN, "no reflected line: open and short sweeps both abandoned (sweeps)", 2, 1);
            else
                Fail(AUTO_FAIL_OPEN, "no reflected line: too few points on the open and short (points)",
                     nOpen + nShort, 2);
        } else if (!r->hasReflected){
            Fail(AUTO_FAIL_OPEN, "no reflected line: no forward line to fit it to", 0, 0);
        } else {
            if (r->reflectedRms_dB > AUTO_REFLECTED_RMS_MAX_DB)
                Fail(AUTO_FAIL_REFLECTED_FIT, "reflected fit rms (dB)", r->reflectedRms_dB, AUTO_REFLECTED_RMS_MAX_DB);
            if (fabsf(r->m_r - r->m_f) > AUTO_REFLECTED_SLOPE_MAX)
                Fail(AUTO_FAIL_REFLECTED_SLOPE, "reflected slope minus forward slope (mV/dB)",
                     r->m_r - r->m_f, AUTO_REFLECTED_SLOPE_MAX);
            if (!r->openOnly && !r->shortOnly && (fabsf(r->gap_dB) > (high ? AUTO_GAP_MAX_HI_DB : AUTO_GAP_MAX_DB)))
                Fail(AUTO_FAIL_GAP, "open-short gap (dB)", r->gap_dB, high ? AUTO_GAP_MAX_HI_DB : AUTO_GAP_MAX_DB);
        }

        // S50: the load's CW points through the new calibration
        r->hasS50 = false;
        if (loadRan && r->hasReflected){
            float32_t swr[AUTO_MAX_LOAD_POINTS];
            uint32_t n = 0;
            float32_t minPower_dBm = 10.0f * log10f(1000.0f * AUTO_S50_MIN_POWER_W);
            uint32_t weak = 0;
            for (uint32_t k = 0; k < bd->nLoad; k++){
                if (SWRCalPower_dBm(bd->load[k].vf, r->m_f, r->b_f) < minPower_dBm){
                    weak++;
                    continue;
                }
                swr[n++] = SWRCalSWR(bd->load[k].vf, bd->load[k].vr, r->m_f, r->b_f, r->m_r, r->b_r);
            }
            if (weak > 0)
                LogSkip(band, "load", "S50", "load points below 0.1 W left out (count)", weak, 0);
            if (n > 0){
                r->S50 = SWRCalMedian(swr, n);
                r->hasS50 = true;
                float32_t limit = (SWRCalBandFrequency_MHz(band) < 20.0f) ? AUTO_S50_MAX : AUTO_S50_MAX_HI;
                if (r->S50 > limit)
                    Fail(AUTO_FAIL_S50, "SWR of the load", r->S50, limit);
            }
        }

        if (loadRan && !r->hasPACurve)
            Fail(AUTO_FAIL_PA_CURVE, "no PA curve or SSB gain (see the load's lines above)", 0, 0);
        if (loadRan && (bd->noLoadPoints > 0))
            Fail(AUTO_FAIL_NO_LOAD, "no 50 Ohm load: load points that reflected everything (count)",
                 bd->noLoadPoints, 0);
        if (bd->fewReadings)
            Fail(AUTO_FAIL_FEW_READINGS, "a point had too few bridge readings (minimum)", 0, autoCal.minReadings);
        if (autoCal.level == 2 && r->hasForward &&
            (fabsf(r->level2Delta_dB) > (high ? AUTO_LEVEL2_OFFSET_MAX_HI_DB : AUTO_LEVEL2_OFFSET_MAX_DB)))
            Fail(AUTO_FAIL_LEVEL2_OFFSET, "forward offset minus the level 1 default (dB)", r->level2Delta_dB,
                 high ? AUTO_LEVEL2_OFFSET_MAX_HI_DB : AUTO_LEVEL2_OFFSET_MAX_DB);
        if (autoCal.level == 3 && r->hasForward){
            if (r->forwardRms_dB > AUTO_FORWARD_RMS_MAX_DB)
                Fail(AUTO_FAIL_FORWARD_FIT, "forward fit rms (dB)", r->forwardRms_dB, AUTO_FORWARD_RMS_MAX_DB);
            if (r->forwardWorst_dB > AUTO_FORWARD_WORST_MAX_DB)
                Fail(AUTO_FAIL_FORWARD_FIT, "forward fit worst point (dB)", r->forwardWorst_dB, AUTO_FORWARD_WORST_MAX_DB);
            if (r->m_f < AUTO_FORWARD_SLOPE_MIN)
                Fail(AUTO_FAIL_FORWARD_SLOPE, "forward slope below the minimum (mV/dB)", r->m_f, AUTO_FORWARD_SLOPE_MIN);
            if (r->m_f > AUTO_FORWARD_SLOPE_MAX)
                Fail(AUTO_FAIL_FORWARD_SLOPE, "forward slope above the maximum (mV/dB)", r->m_f, AUTO_FORWARD_SLOPE_MAX);
            if (fabsf(r->drift_dB) > AUTO_DRIFT_MAX_DB)
                Fail(AUTO_FAIL_DRIFT, "drift of the repeated point (dB)", r->drift_dB, AUTO_DRIFT_MAX_DB);
            slopes[nSlopes++] = r->m_f;
        }
        r->failBits = fail;
        r->checked = true;
    }

    // Level 3: each band's slope within 5 % of the median of this radio's bands
    if (nSlopes >= 3){
        float32_t median = SWRCalMedian(slopes, nSlopes);
        for (int32_t band = FIRST_BAND; band <= LAST_BAND; band++){
            AutoCalBandResult *r = &results[band];
            if (r->inSession && r->hasForward && (fabsf(r->m_f - median) > AUTO_FORWARD_SLOPE_SPREAD * median)){
                r->failBits |= AUTO_FAIL_FORWARD_SLOPE;
                LogSkip(band, "band", "band fails", "forward slope minus the median of the bands (mV/dB)",
                        r->m_f - median, AUTO_FORWARD_SLOPE_SPREAD * median);
            }
        }
    }

    autoCal.failedMask = 0;
    for (int32_t band = FIRST_BAND; band <= LAST_BAND; band++){
        const AutoCalBandResult *r = &results[band];
        if (!r->inSession)
            continue;
        if (r->failBits)
            autoCal.failedMask |= (1u << band);
        char line[220];
        snprintf(line, sizeof(line),
                 "AUTOCAL-BAND,%s,level %u,fail 0x%03lx,m_f %.2f,o_f %.2f,m_r %.2f,o_r %.2f,gap %.2f,"
                 "rms_r %.2f,S50 %.2f,Psat %.0f,k %.2f,s %.3f,ssb %.2f,%s",
                 bands[band].name, autoCal.level, (unsigned long)r->failBits,
                 r->m_f, r->b_f + SWRCAL_OFFSET_CONSTANT_DB, r->m_r, r->b_r + SWRCAL_OFFSET_CONSTANT_DB,
                 r->gap_dB, r->reflectedRms_dB, r->hasS50 ? r->S50 : 0.0f,
                 r->hasPACurve ? r->Psat_mW : 0.0f, r->hasPACurve ? r->kindex : 0.0f,
                 r->hasPACurve ? r->shape : 0.0f, r->hasPACurve ? r->ssbCorrection_dB : 0.0f, r->openOnly ? "open only" : (r->shortOnly ? "short only" : "open+short"));
        Debug(line);
    }
    PrintAutoCalTable();
    autoCal.resultsVersion++;
}

/**
 * @brief REVIEW -> DONE: store the bands that passed, then save
 */
static void CommitResults(void){
    for (int32_t band = FIRST_BAND; band <= LAST_BAND; band++){
        const AutoCalBandResult *r = &results[band];
        if (!r->inSession || !r->checked)
            continue;
        if (r->failBits){
            LogSkip(band, "band", "not stored", "failed its checks: previous calibration kept (fail bits)",
                    r->failBits, 0);
            continue;
        }
        ED.SWR_F_SlopeAdj[band] = r->m_f - SWRCAL_NOMINAL_SLOPE_MV_PER_DB;
        ED.SWR_F_Offset[band] = r->b_f + SWRCAL_OFFSET_CONSTANT_DB;
        ED.SWR_R_SlopeAdj[band] = r->m_r - SWRCAL_NOMINAL_SLOPE_MV_PER_DB;
        ED.SWR_R_Offset[band] = r->b_r + SWRCAL_OFFSET_CONSTANT_DB;
        ED.SWR_CalLevel[band] = autoCal.level | (r->openOnly ? SWR_CAL_OPEN_ONLY : 0)
                                              | (r->shortOnly ? SWR_CAL_SHORT_ONLY : 0);
        ED.SWR_CalDate[band] = (uint32_t)Teensy3Clock.get();
        if (r->hasPACurve){
            ED.PowerCal_20W_Psat_mW[band] = r->Psat_mW;
            ED.PowerCal_20W_kindex[band] = r->kindex;
            ED.PowerCal_20W_shape[band] = r->shape;
            ED.PowerCal_20W_DSP_Gain_correction_dB[band] = r->ssbCorrection_dB;
        }
    }
    // Save the operator's CW attenuations, not the ones calibration has been using
    float32_t calXAttenCW[NUMBER_OF_BANDS];
    memcpy(calXAttenCW, ED.XAttenCW, sizeof(calXAttenCW));
    RestoreArray(0, ED.XAttenCW, sizeof(ED.XAttenCW));
    SaveDataToStorage(false);
    memcpy(ED.XAttenCW, calXAttenCW, sizeof(calXAttenCW));
    Debug("Auto calibration: results stored");
}

/**
 * @brief Carry out what the AUTO state machine has asked for
 * @note Called from the main loop. Returns at once when there is nothing to do.
 *       Unkeys before any change of mode or band, and keys last.
 */
void PowerCalAutoService(void){
    if (!autoCal.servicePending)
        return;
    bool modeChange = (autoCal.ssbMode != CalibrationSSB());
    bool bandChange = (autoCal.band >= FIRST_BAND) && (autoCal.band <= LAST_BAND) &&
                      (autoCal.band != ED.currentBand[ED.activeVFO]);

    // Unkey when asked to, and before changing the mode or the band
    if (CalibrationKeyed() && (!autoCal.keyed || modeChange || bandChange))
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_RELEASED);
    // CW carrier (CALIBRATE_POWER) or SSB tone (CALIBRATE_OFFSET)
    if (modeChange)
        ModeSm_dispatch_event(&modeSM, autoCal.ssbMode ? ModeSm_EventId_OFFSET_START
                                                       : ModeSm_EventId_OFFSET_END);
    if (bandChange)
        SetCalibrationBand(autoCal.band);
    if (autoCal.setDrive){
        ApplyDrive();
        autoCal.setDrive = false;
    }
    if (autoCal.clearStats){
        ClearSWRBridgeStats();
        autoCal.clearStats = false;
    }
    if (autoCal.storePoint){
        StorePoint();
        autoCal.storePoint = false;
    }
    if (autoCal.fitForward){
        FitForward(autoCal.band);
        autoCal.fitForward = false;
    }
    if (autoCal.finishBand){
        FinishLoadBand(autoCal.band);
        autoCal.finishBand = false;
    }
    if (autoCal.fitAll){
        FitAndCheckAll();
        autoCal.fitAll = false;
    }
    if (autoCal.commit){
        CommitResults();
        autoCal.commit = false;
    }
    if (autoCal.keyed && !CalibrationKeyed())
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_PRESSED);

    if ((CalibrationKeyed() != autoCal.keyed) || (CalibrationSSB() != autoCal.ssbMode)){
        // The mode state machine is not where we asked it to be. Stay pending, so
        // the calibration waits; ZOOM aborts it.
        Debug("Auto calibration: mode is " + String(ModeSm_state_id_to_string(modeSM.state_id)));
        return;
    }
    // The settle, collect and rest times count from now
    powerSM.vars.count_ms = 0;
    autoCal.servicePending = false;
}

/**
 * @brief True while the power calibration state machine is in its AUTO branch
 */
bool PowerCalAutoActive(void){
    return autoCal.active;
}

/**
 * @brief Number of CW points to measure on the current band in the current stage
 * @note The load stage's SSB tone point comes after these
 */
static uint32_t AutoPointCount(void){
    if (autoCal.stage != AUTO_STAGE_LOAD)
        return AUTO_REFLECTION_POINTS;
    switch (autoCal.level){
        case 3:
            return AUTO_LEVEL3_POINTS;
        case 2:
            return AUTO_LOAD_SWEEP_POINTS + 2; // the sweep, then wattmeter points near 2 W and at 0 dB
        default:
            return AUTO_LOAD_SWEEP_POINTS;
    }
}

/**
 * @brief The first band in the session's band mask at or above band
 * @return The band, or LAST_BAND+1 if there is none
 */
static int32_t AutoBandFrom(int32_t band){
    while ((band <= LAST_BAND) && !(autoCal.bandMask & (1u << band)))
        band++;
    return band;
}

/**
 * @brief Start the automatic calibration
 * @note Called by Loop.cpp when button 1 is pressed in power calibration
 */
void StartPowerAutoCal(void){
    PowerCalDispatch(PowerCalSm_EventId_AUTO);
}

/**
 * @brief Skip the current standard (level 1 only: the 50 Ohm load)
 * @note Called by Loop.cpp when button 12 is pressed during the automatic calibration
 */
void SkipPowerCalStage(void){
    PowerCalDispatch(PowerCalSm_EventId_SKIP);
}

/**
 * @brief Repeat the session for the bands that failed
 * @note Called by Loop.cpp when button 14 is pressed on the review screen
 */
void RetryPowerCal(void){
    PowerCalDispatch(PowerCalSm_EventId_RETRY);
}

/**
 * @brief Change the calibration level on the setup screen
 * @param delta +1 or -1; the level is kept within 1 to 3
 */
void ChangeAutoCalLevel(int32_t delta){
    if (powerSM.state_id != PowerCalSm_StateId_SETUP)
        return;
    int32_t level = (int32_t)autoCal.level + delta;
    if (level < 1) level = 1;
    if (level > 3) level = 3;
    autoCal.level = (uint8_t)level;
}

/**
 * @brief Change the L44 turns per core on the setup screen
 * @param delta +1 or -1; the turns are kept within 5 to 40
 * @note Selects the level 1 forward offsets; saved with the configuration
 */
void ChangeAutoCalTurns(int32_t delta){
    if (powerSM.state_id != PowerCalSm_StateId_SETUP)
        return;
    int32_t turns = (int32_t)ED.SWR_CouplerTurns + delta;
    if (turns < 5) turns = 5;
    if (turns > 40) turns = 40;
    ED.SWR_CouplerTurns = (uint8_t)turns;
}

/**
 * @brief Guard: the automatic calibration runs with the 20 W PA only
 */
bool AutoCalAllowed(void){
    return !ED.PA100Wactive;
}

/**
 * @brief Entry action of AUTO: set the timing and start a new session
 */
void AutoCalEnter(void){
    autoCal.active = true;
    autoCal.stage = AUTO_STAGE_LOAD; // the setup screen names the first standard
    autoCal.keyed = false;
    autoCal.ssbMode = CalibrationSSB();
    autoCal.band = ED.currentBand[ED.activeVFO];
    autoCal.setDrive = false;
    autoCal.clearStats = false;
    autoCal.storePoint = false;
    autoCal.fitForward = false;
    autoCal.finishBand = false;
    autoCal.fitAll = false;
    autoCal.commit = false;
    autoCal.servicePending = false;
    autoCal.failedMask = 0;
    autoCal.bandMask = AUTO_DEFAULT_BAND_MASK;
    autoCal.collectLoad_ms = 1000; // about 75 readings; shorter key-downs keep the load cooler
    autoCal.collectReflection_ms = 1500;
    autoCal.minReadings = AUTO_MIN_READINGS;
    powerSM.vars.bandSettle_ms = 300;
    powerSM.vars.settle_ms = 500;
    powerSM.vars.collect_ms = autoCal.collectLoad_ms;
    powerSM.vars.rest_ms = 1000;
    powerSM.vars.meterTimeout_ms = 30000;
    memset(results, 0, sizeof(results));
    autoCal.resultsVersion++;
    savedBand = ED.currentBand[ED.activeVFO];
    savedCenterFreq_Hz = ED.centerFreq_Hz[ED.activeVFO];
    savedFineTuneFreq_Hz = ED.fineTuneFreq_Hz[ED.activeVFO];
    savedModulation = ED.modulation[ED.activeVFO];
    memcpy(savedXAttenCW, ED.XAttenCW, sizeof(savedXAttenCW));
    Debug("Auto calibration: choose the level and press SELECT");
}

/**
 * @brief Exit action of AUTO: always leaves the transmitter unkeyed
 * @note Runs on every way out, including RESET in the middle of a point. RESET is
 *       dispatched from the main loop only, so the hardware is changed here
 *       directly rather than through the service.
 */
void AutoCalExit(void){
    // Leaving right after SELECT on REVIEW: store before the session is lost
    if (autoCal.commit){
        CommitResults();
        autoCal.commit = false;
    }
    autoCal.keyed = false;
    autoCal.servicePending = false;
    if (CalibrationKeyed())
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_RELEASED);
    ClearSSBGainOverride();
    autoCal.active = false;
    // Back to the operator's band, frequency, mode and CW attenuations. PowerPoint1's
    // entry, which follows, returns the mode to CALIBRATE_POWER_SPACE.
    memcpy(ED.XAttenCW, savedXAttenCW, sizeof(savedXAttenCW));
    ED.currentBand[ED.activeVFO] = savedBand;
    ED.centerFreq_Hz[ED.activeVFO] = savedCenterFreq_Hz;
    ED.fineTuneFreq_Hz[ED.activeVFO] = savedFineTuneFreq_Hz;
    ED.modulation[ED.activeVFO] = savedModulation;
    UpdateRFHardwareState();
}

/**
 * @brief SETUP -> PROMPT: start measuring with the first standard
 */
void AutoBeginSession(void){
    autoCal.stage = AUTO_STAGE_LOAD;
    autoCal.loadSkipped = false;
    autoCal.openFailMask = 0;
    autoCal.shortFailMask = 0;
    autoCal.failedMask = 0;
    for (int32_t band = FIRST_BAND; band <= LAST_BAND; band++){
        if (!(autoCal.bandMask & (1u << band)))
            continue;
        memset(&bandData[band], 0, sizeof(bandData[band]));
        memset(&results[band], 0, sizeof(results[band]));
        results[band].inSession = true;
    }
    autoCal.resultsVersion++;
    Debug("Auto calibration level " + String(autoCal.level));
}

/**
 * @brief Entry action of PROMPT: ask the operator to fit the next standard
 */
void AutoPromptStandard(void){
    switch (autoCal.stage){
        case AUTO_STAGE_LOAD:
            if (autoCal.level > 1)
                Debug("Fit the wattmeter and its 50 Ohm load, then press SELECT");
            else
                Debug("Fit a 50 Ohm load, then press SELECT (button 12 skips it)");
            break;
        case AUTO_STAGE_OPEN:
            Debug("Remove everything from the antenna connector, then press SELECT");
            break;
        case AUTO_STAGE_SHORT:
            Debug("Fit the short on the antenna connector, then press SELECT");
            break;
    }
}

/**
 * @brief Guard: only the level 1 load stage may be skipped
 */
bool AutoCanSkipStage(void){
    return (autoCal.level == 1) && (autoCal.stage == AUTO_STAGE_LOAD);
}

/**
 * @brief Skip the load stage: keep the stored PA curves, no S50 check
 */
void AutoSkipStage(void){
    autoCal.loadSkipped = true;
    autoCal.stage = AUTO_STAGE_OPEN;
}

/**
 * @brief PROMPT -> BAND: start with the first band in the mask
 */
void AutoFirstBand(void){
    autoCal.band = AutoBandFrom(FIRST_BAND);
}

/**
 * @brief Entry action of BAND: tune to the band and plan its points
 */
void AutoSelectBand(void){
    AutoBandData *bd = &bandData[autoCal.band];
    autoCal.point = 0;
    autoCal.nPoints = AutoPointCount();
    autoCal.forwardFitDone = false;
    autoCal.ssbPoint = false;
    autoCal.oscillation = false;
    autoCal.shortFalls = 0;
    autoCal.extraPoints = 0;
    switch (autoCal.stage){
        case AUTO_STAGE_LOAD:
            bd->nLoad = 0;
            bd->hasSSB = false;
            powerSM.vars.collect_ms = autoCal.collectLoad_ms;
            break;
        case AUTO_STAGE_OPEN:
            bd->nOpen = 0;
            powerSM.vars.collect_ms = autoCal.collectReflection_ms;
            break;
        case AUTO_STAGE_SHORT:
            bd->nShort = 0;
            powerSM.vars.collect_ms = autoCal.collectReflection_ms;
            break;
    }
    RequestAutoService();
}

/**
 * @brief Entry action of POINT: set the drive for the next point
 * @note The load stage's CW points use CALIBRATE_POWER; its SSB point and every
 *       open and short point use the SSB tone in CALIBRATE_OFFSET
 */
void AutoSetupPoint(void){
    autoCal.ssbMode = (autoCal.stage != AUTO_STAGE_LOAD) || autoCal.ssbPoint;
    autoCal.setDrive = true;
    RequestAutoService();
}

/**
 * @brief Key the transmitter for a calibration point
 * @note Does nothing if the calibration has already keyed it
 */
void AutoKey(void){
    if (autoCal.keyed)
        return;
    autoCal.keyed = true;
    RequestAutoService();
}

/**
 * @brief Unkey the transmitter if the calibration keyed it
 * @note Unkeyed in CALIBRATE_OFFSET, the open and short sweeps' mode, is true
 *       receive: T/R relay in RX, PA out of circuit
 */
void AutoUnkey(void){
    if (!autoCal.keyed)
        return;
    autoCal.keyed = false;
    RequestAutoService();
}

/**
 * @brief Guard: this point needs a wattmeter reading from the operator
 * @note Level 3: every CW point. Level 2: the two points after the sweep, near
 *       2 W and at the PA's ceiling.
 */
bool AutoNeedsMeterEntry(void){
    if ((autoCal.stage != AUTO_STAGE_LOAD) || autoCal.ssbPoint)
        return false;
    if (autoCal.level == 3)
        return true;
    return (autoCal.level == 2) && (autoCal.point >= AUTO_LEVEL2_METER_POINT);
}

/**
 * @brief Entry action of METER_ENTRY: start the operator's wattmeter entry at
 *        the power the stored PA curve expects, so the operator only adjusts it
 */
void AutoStartMeterEntry(void){
    float32_t expected_W = CalculateCWPowerLevel(autoCal.drive_dB, 0) / 1000.0f;
    if (!(expected_W > 0)){
        ResetPowerTarget();
        return;
    }
    targetPower = (GetPowerUnit() == 0) ? 10.0f * log10f(1000.0f * expected_W) : expected_W;
    SetMeasuredPower(targetPower);
}

/**
 * @brief Entry action of COLLECT: start a fresh set of bridge statistics
 */
void AutoStartCollect(void){
    autoCal.clearStats = true;
    RequestAutoService();
}

/**
 * @brief Entry action of STORE: keep the point's readings and check them
 */
void AutoStorePoint(void){
    autoCal.storePoint = true;
    RequestAutoService();
}

/**
 * @brief Guard: the last open or short point shows the PA oscillating
 */
bool AutoOscillationDetected(void){
    return autoCal.oscillation;
}

/**
 * @brief Abandon this band's sweep on this standard after an oscillation
 * @note The band is then calibrated from the other standard alone; it fails
 *       only if both were abandoned
 */
void AutoAbortBandStage(void){
    if (autoCal.stage == AUTO_STAGE_OPEN)
        autoCal.openFailMask |= (1u << autoCal.band);
    else if (autoCal.stage == AUTO_STAGE_SHORT)
        autoCal.shortFailMask |= (1u << autoCal.band);
    autoCal.oscillation = false;
}

/**
 * @brief Guard: CW points remain on this band and standard
 */
bool AutoMorePoints(void){
    return !autoCal.ssbPoint && (autoCal.point < autoCal.nPoints);
}

/**
 * @brief Guard: stay keyed between points (load stage only; the open and
 *        short sweeps rest in receive between points to protect the PA)
 */
bool AutoKeepKeyed(void){
    return autoCal.stage == AUTO_STAGE_LOAD;
}

/**
 * @brief Guard: the load stage's CW points are done, fit the forward line next
 */
bool AutoForwardFitDue(void){
    return (autoCal.stage == AUTO_STAGE_LOAD) && !autoCal.forwardFitDone;
}

/**
 * @brief Entry action of FIT_FORWARD: fit the band's forward line, then
 *        measure the SSB tone point with it
 */
void AutoFitForwardLine(void){
    autoCal.forwardFitDone = true;
    autoCal.ssbPoint = true;
    autoCal.fitForward = true;
    RequestAutoService();
}

/**
 * @brief Entry action of BAND_DONE: finish the band on this standard
 */
void AutoFinishBand(void){
    if ((autoCal.stage == AUTO_STAGE_LOAD) && autoCal.ssbPoint)
        autoCal.finishBand = true;
    autoCal.ssbPoint = false;
    RequestAutoService();
}

/**
 * @brief Guard: more bands to measure on this standard
 */
bool AutoMoreBands(void){
    return AutoBandFrom(autoCal.band + 1) <= LAST_BAND;
}

/**
 * @brief Move on to the next band in the mask
 */
void AutoNextBand(void){
    autoCal.band = AutoBandFrom(autoCal.band + 1);
}

/**
 * @brief Guard: another standard follows this one
 */
bool AutoMoreStages(void){
    return autoCal.stage != AUTO_STAGE_SHORT;
}

/**
 * @brief Move on to the next standard
 */
void AutoNextStage(void){
    autoCal.stage = (AutoCalStage)(autoCal.stage + 1);
}

/**
 * @brief Entry action of FIT_ALL: fit the reflected lines and check every band
 */
void AutoFitAndCheckAll(void){
    autoCal.fitAll = true;
    RequestAutoService();
}

/**
 * @brief REVIEW -> DONE: store the bands that passed
 */
void AutoCommitResults(void){
    autoCal.commit = true;
    RequestAutoService();
}

/**
 * @brief Guard: at least one band failed
 */
bool AutoAnyFailed(void){
    return autoCal.failedMask != 0;
}

/**
 * @brief REVIEW -> SETUP: run the session again for the failed bands only
 */
void AutoRetryFailed(void){
    autoCal.bandMask = autoCal.failedMask;
    autoCal.failedMask = 0;
    autoCal.stage = AUTO_STAGE_LOAD;
}

/**
 * @brief Guard: the main loop has done what the state machine asked, and the
 *        mode state machine is keyed or not, in CW or SSB, as requested
 */
bool AutoServiceIdle(void){
    return !autoCal.servicePending;
}
