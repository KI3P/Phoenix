#include "SDT.h"
#include "DSP.h"
#include "DSP_FT8.h"
#include "MainBoard_Display.h"   // for the Pane struct
extern struct Pane PaneSpectrum; // defined in MainBoard_DisplayHome.cpp; we
                                 // flag it stale on FT8 state changes so the
                                 // FT8 message-list pane repaints

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <TimeLib.h>

/*
 * FT8 receive + transmit -- faithful port from T41_SDR/ft8.cpp.
 *
 * Architecture:
 *
 *   Phoenix audio chain
 *         |
 *         v
 *   ED.modulation == FT8_INTERNAL --> Demodulate() in DSP.cpp
 *         |
 *         v
 *   ft8InternalDemod(DataBlock*)        <-- Phoenix-side dispatcher (this file)
 *         |
 *         | decimate to ~12 kHz mono audio
 *         v
 *   BufferFT8Data(buf, size)            <-- accumulates a 15-second slot
 *         |
 *         v
 *   RunFT8DecoderLoop()                 <-- FSM tick (call from main loop)
 *         |
 *         v
 *   ft8lib_BufferSignal -> ft8lib_ProcessFrame -> ft8lib_Decode
 *         |
 *         v
 *   AddDecodedMessage()                 <-- callback from ft8_lib into this file
 *         |
 *         v
 *   rxBuf[] + AddMsgs() to window lists (storage works; window display stubbed)
 *
 * The vendored ft8_lib (PhoenixSketch/src/ft8_lib/) provides ft8lib_*
 * directly -- they live in src/ft8_lib/ft8/decode_ft8.cpp. We just provide
 * the integration layer Phoenix-side.
 */

/* ============================================================
 * Forward declarations of ft8_lib glue (provided by the vendored
 * decode_ft8.cpp under src/ft8_lib/). Linking here so DSP_FT8.cpp
 * doesn't need to include the ft8_lib headers directly.
 * ============================================================ */
/* Vendored decode_ft8.cpp defines these as plain C++ functions (no extern "C"
 * wrapper). Declarations here must match -- using extern "C" would produce
 * a linkage mismatch (linker would look for unmangled C symbols, but the
 * definitions are mangled C++ symbols). T41's ft8.cpp uses the same
 * plain-C++ form. */
bool  ft8lib_InitDecoder(void);
void  ft8lib_ExitDecoder(void);
bool  ft8lib_BufferSignal(float *buf, int sizeBuf, int offset);
bool  ft8lib_ProcessFrame(int frame);
void  ft8lib_Decode(struct tm *start);
bool  ft8lib_GenFT8(char *message, float frequency);
float *ft8lib_GetSignal(void);

/* Forward declarations for in-file functions used before their definitions. */
void DisplayAllMessages(void);

/* TX-state bookkeeping (definitions further down). */
#define FT8_TX_SAMPLE_RATE_HZ        12000   /* ft8_lib output rate */
#define FT8_TX_SECONDS               13      /* full-message duration with margin */
#define FT8_TX_TOTAL_12K_SAMPLES     (FT8_TX_SECONDS * FT8_TX_SAMPLE_RATE_HZ)
#define FT8_TX_OUTPUT_RATE_HZ        24000   /* Phoenix TX-pipeline audio rate */
#define FT8_TX_UPSAMPLE_RATIO        (FT8_TX_OUTPUT_RATE_HZ / FT8_TX_SAMPLE_RATE_HZ)
static bool  s_ft8TxInProgress = false;
static int   s_ft8TxSampleIdx12k = 0;     /* read index into ft8_lib's txSignal at 12 kHz */
static int   s_ft8TxLast12kIdx   = 0;     /* highest valid sample we've generated */

/* ---- 12 -> 24 kHz polyphase upsampler ----
 *
 * 32-tap Hamming-windowed sinc, cutoff at fs_out/4 = 6 kHz (the original
 * input Nyquist), Hamming window for ~50 dB stopband. Tap count must be
 * a multiple of L=2 (CMSIS-DSP requirement). Block size matches the
 * single existing caller (DSP.cpp::TransmitProcessing pulls 256 samples
 * per chunk -> 128 input samples per FIR call).
 *
 * Replaces the prior nearest-neighbor 2x duplication, which produced
 * spectral images centered at 12 kHz (the original Nyquist) that bled
 * back into the audio band when the SSB pipeline ran.
 */
#define FT8_TX_FIR_TAPS              32
#define FT8_TX_FIR_INPUT_BLOCK_12K   128
#define FT8_TX_FIR_OUTPUT_BLOCK_24K  (FT8_TX_FIR_INPUT_BLOCK_12K * FT8_TX_UPSAMPLE_RATIO)
#define FT8_TX_FIR_STATE_LEN \
    ((FT8_TX_FIR_TAPS / FT8_TX_UPSAMPLE_RATIO) + FT8_TX_FIR_INPUT_BLOCK_12K - 1)

static float32_t s_ft8TxFirCoeffs[FT8_TX_FIR_TAPS];
static float32_t s_ft8TxFirState [FT8_TX_FIR_STATE_LEN];
static arm_fir_interpolate_instance_f32 s_ft8TxFirInst;
static bool      s_ft8TxFirInitialized = false;

/*
 * Lazy first-call initializer for the polyphase FIR. We compute the
 * Hamming-windowed sinc coefficients in-place once, then call CMSIS-DSP's
 * arm_fir_interpolate_init_f32 to set up the instance. Coefficients are
 * normalized so their sum equals the upsample ratio L (=2), which gives
 * unity DC gain after the L-times zero stuffing step.
 */
static void EnsureFT8TxFirInit(void) {
    if (s_ft8TxFirInitialized) return;

    const int N = FT8_TX_FIR_TAPS;
    const float center = (float)(N - 1) * 0.5f;
    const float fc_norm = 0.25f;  /* cutoff at fs_out/4 = 6 kHz, the input Nyquist */
    float sum = 0.0f;
    for (int n = 0; n < N; n++) {
        float k = (float)n - center;
        float sinc;
        if (fabsf(k) < 1e-7f) {
            sinc = 1.0f;
        } else {
            float arg = 2.0f * (float)PI * fc_norm * k;
            sinc = sinf(arg) / arg;
        }
        /* Ideal LPF magnitude scaling for cutoff fc: amplitude is 2*fc */
        float h = 2.0f * fc_norm * sinc;
        /* Hamming window for ~50 dB stopband attenuation */
        float w = 0.54f - 0.46f * cosf(2.0f * (float)PI * (float)n / (float)(N - 1));
        s_ft8TxFirCoeffs[n] = h * w;
        sum += s_ft8TxFirCoeffs[n];
    }
    /* Normalize so sum == L (compensates for L-1 zeros inserted between
     * each input sample at upsample time). */
    if (sum > 1e-9f) {
        float scale = (float)FT8_TX_UPSAMPLE_RATIO / sum;
        for (int n = 0; n < N; n++) s_ft8TxFirCoeffs[n] *= scale;
    }

    arm_fir_interpolate_init_f32(&s_ft8TxFirInst,
                                 (uint8_t)FT8_TX_UPSAMPLE_RATIO,
                                 (uint16_t)FT8_TX_FIR_TAPS,
                                 s_ft8TxFirCoeffs,
                                 s_ft8TxFirState,
                                 (uint32_t)FT8_TX_FIR_INPUT_BLOCK_12K);
    s_ft8TxFirInitialized = true;
}

/* ============================================================
 * FT8 protocol / buffering constants
 * ============================================================ */

/* FT8 uses 12 kHz audio sample rate and 15-second slots.
 * 15 * 12000 = 180,000 samples per slot.                     */
#define FT8_SAMPLE_RATE_HZ   12000
#define FT8_SLOT_SECONDS     15
#define FT8_SLOT_SAMPLES     (FT8_SLOT_SECONDS * FT8_SAMPLE_RATE_HZ)
#define FT8_FRAMES_PER_SLOT  768   /* matches T41's frame slicing for ft8lib_ProcessFrame */

/* Max message storage. FT8_MAX_DECODED_MESSAGES is declared in DSP_FT8.h
 * (was MAX_DECODED_MESSAGES in T41) so MainBoard_DisplayFT8.cpp can use it
 * for the same array bounds. */
#define MAX_DECODED_MESSAGES  FT8_MAX_DECODED_MESSAGES
#define MAX_TX_MESSAGES        5
#define MAX_QSO_VIEWS         20

/* FT8 decoder state machine (matches T41 ft8.cpp). */
#define FT8_STATE_BUFFERING   0
#define FT8_STATE_PROCESSING  1
#define FT8_STATE_DECODING    2
#define FT8_STATE_RX_UPDATE   3
#define FT8_STATE_TX          4
#define FT8_STATE_TX_UPDATE   5

/* ============================================================
 * Public state (declared in DSP_FT8.h via extern)
 * ============================================================ */
int   ft8SyncState  = 0;
int   ft8TxFreq     = 1000;
int   ft8RxFreq     = 1000;
int   ft8TxState    = 0;     /* 0:off, 1:enabled */
int   ft8IntState   = 0;     /* 0:even, 1:odd */
int   ft8CqState    = 0;     /* 0:manual, 1:auto */
bool  txEqualsRx    = true;
float *ft8TxSignalBuf = NULL;

/* ============================================================
 * Module-private state
 * ============================================================ */
static bool       ft8Init        = false;
static int        ft8DecoderState = FT8_STATE_BUFFERING;
static int        bufCount       = 0;
static int        frameCount     = 0;
static uint32_t   slotStartMs    = 0;
static char       baseCall[14]   = MY_CALL;       /* sourced from Config.h */
static char       baseGrid[5]    = "AA00";        /* placeholder until UI/EEPROM exists */

/* 15-second audio slot buffer (720 KB at 12 kHz x 15 s x 4 bytes).
 * Lives in PSRAM on Teensy 4.1 to avoid SRAM pressure. Matches T41's pattern.
 *
 * Without PSRAM (HAS_PSRAM undefined in Config.h): the EXTMEM section
 * has no backing store on a stock Teensy 4.1, so writes either land
 * in OCRAM and overflow it or hit invalid memory at runtime. We
 * shrink the array to a 1-element stub that consumes no real memory
 * and is never written -- the FT8 menu and the FT8_INTERNAL
 * modulation are gated off when HAS_PSRAM is undefined (see
 * MainBoard_DisplayMenus.cpp and DSP.cpp::Demodulate / TransmitProcessing),
 * so nothing in the FT8 RX/TX path ever runs and no one writes to
 * this buffer. */
#ifdef HAS_PSRAM
EXTMEM float ft8SlotBuf[FT8_SLOT_SAMPLES];
#else
float        ft8SlotBuf[1] = {0.0f};
#endif
static int  ft8SlotWriteIdx = 0;

/* ============================================================
 * Decoded receive message storage
 * ============================================================ */
RxMsg rxBuf[MAX_DECODED_MESSAGES];
int   decodedMsgs = 0;
int   activeMsg   = 0;

/* QSO target callsign. Auto-populated from received CQ messages and used
 * by the <TARGET> placeholder in ExpandFT8Template. */
char  ft8TargetCall[14] = {0};

/* Window list bookkeeping mirrors T41 ft8.cpp.
 * Lists/counts/heads are exposed in DSP_FT8.h so the FT8 display pane
 * (MainBoard_DisplayFT8.cpp) can render them. The scroll/top vars stay
 * file-static -- they're internal pagination state. */
int  allList[MAX_DECODED_MESSAGES];
int  cqList[MAX_DECODED_MESSAGES];
int  rxList[MAX_DECODED_MESSAGES];
int  allMsgs = 0, cqMsgs = 0, rxMsgs = 0;
int  allHead = -1, cqHead = -1, rxHead = -1;
/* T41 had scroll/top pagination state vars here for its three windows.
 * Phoenix's pane renderer always shows newest-first up to a fixed row count
 * with no scrolling, so they're omitted -- removing them silences
 * -Wunused-variable warnings. Re-add if a scrolling UI is implemented. */

/* TX message queue */
TxMsg txBuf[MAX_TX_MESSAGES];
int   txMsgs = 0;

/* QSO view list */
QsoView qsoBuf[MAX_QSO_VIEWS];
int     qsos = 0;
int     activeQSO = -1;
bool    qsoViewActive = false;

/* ============================================================
 * Internal helpers
 * ============================================================ */

static void AddMsgToList(int *list, int *count, int *head, int newIdx) {
    if (*count < MAX_DECODED_MESSAGES) (*count)++;
    *head = (*head + 1) % MAX_DECODED_MESSAGES;
    list[*head] = newIdx;
}

static void AddMsgs(int newMsgIdx) {
    /* Always add to the All list */
    AddMsgToList(allList, &allMsgs, &allHead, newMsgIdx);

    /* Heuristic: messages starting with "CQ " go on the CQ list. */
    if (strncmp(rxBuf[newMsgIdx].field1, "CQ", 2) == 0) {
        AddMsgToList(cqList, &cqMsgs, &cqHead, newMsgIdx);
    }

    /* Anything within +/- 50 Hz of the RX frequency goes on the RX list. */
    if (fabsf(rxBuf[newMsgIdx].freq - (float)ft8RxFreq) <= 50.0f) {
        AddMsgToList(rxList, &rxMsgs, &rxHead, newMsgIdx);
    }
}

/* ============================================================
 * AddDecodedMessage
 *
 * This is the callback invoked by the vendored ft8_lib decoder
 * (see PhoenixSketch/src/ft8_lib/ft8/decode_ft8.cpp:213) every time it
 * successfully decodes a message in the current 15-second slot. We must
 * provide it for the link to succeed.
 * Ported from T41_SDR/ft8.cpp (FLASHMEM AddDecodedMessage).
 * ============================================================ */
/* Plain C++ (NOT extern "C") so it matches the forward declaration in
 * the vendored src/ft8_lib/ft8/decode_ft8.cpp which calls into us. */
void AddDecodedMessage(struct tm *tmSlot, int16_t score,
                       float time_sec, float freq, char *msg) {
    if (decodedMsgs < 0 || decodedMsgs >= MAX_DECODED_MESSAGES) decodedMsgs = 0;

    strncpy(rxBuf[decodedMsgs].msg, msg, 34);
    rxBuf[decodedMsgs].msg[34] = '\0';
    rxBuf[decodedMsgs].freq = freq;
    rxBuf[decodedMsgs].slot_time.tm_hour = tmSlot->tm_hour;
    rxBuf[decodedMsgs].slot_time.tm_min  = tmSlot->tm_min;
    rxBuf[decodedMsgs].slot_time.tm_sec  = tmSlot->tm_sec;
    rxBuf[decodedMsgs].evenInterval =
        ((tmSlot->tm_sec / FT8_SLOT_SECONDS) * FT8_SLOT_SECONDS) % 2 == 0;
    rxBuf[decodedMsgs].time_sec   = time_sec;
    rxBuf[decodedMsgs].hour       = (uint8_t)hour();
    rxBuf[decodedMsgs].min        = (uint8_t)minute();
    rxBuf[decodedMsgs].sec        = (uint8_t)second();
    rxBuf[decodedMsgs].sync_score = score;
    /* T41's empirical SNR mapping. */
    rxBuf[decodedMsgs].snr        = (score - 160.0f) / 6.0f;

    /* Split msg into 3 fields. strtok mutates msg; if T41 callers don't
     * want that, copy first. We follow T41's convention exactly. */
    char *f1 = strtok(msg, " ");
    char *f2 = (f1 != NULL) ? strtok(NULL, " ") : NULL;
    char *f3 = (f2 != NULL) ? strtok(NULL, " ") : NULL;
    rxBuf[decodedMsgs].field1[0] = '\0';
    rxBuf[decodedMsgs].field2[0] = '\0';
    rxBuf[decodedMsgs].field3[0] = '\0';
    if (f1) { strncpy(rxBuf[decodedMsgs].field1, f1, 19); rxBuf[decodedMsgs].field1[19] = '\0'; }
    if (f2) { strncpy(rxBuf[decodedMsgs].field2, f2, 19); rxBuf[decodedMsgs].field2[19] = '\0'; }
    if (f3) { strncpy(rxBuf[decodedMsgs].field3, f3, 19); rxBuf[decodedMsgs].field3[19] = '\0'; }

    AddMsgs(decodedMsgs);

    /* Target auto-update: if this is a CQ ("CQ <sender> <grid>"), capture
     * the sender (field2) as the QSO target. Operators can override later
     * via the menu, but this keeps "Queue: 73" et al. usable as soon as
     * any CQ has been received without explicit operator action. */
    if (strncmp(rxBuf[decodedMsgs].field1, "CQ", 2) == 0
        && rxBuf[decodedMsgs].field2[0] != '\0'
        /* Don't overwrite an explicit operator target. Heuristic: only
         * auto-update when target is empty. If you want auto-update to
         * always follow latest CQ, drop this guard. */
        && ft8TargetCall[0] == '\0') {
        strncpy(ft8TargetCall, rxBuf[decodedMsgs].field2,
                sizeof(ft8TargetCall) - 1);
        ft8TargetCall[sizeof(ft8TargetCall) - 1] = '\0';
        PaneSpectrum.stale = true;  /* status line shows target */
    }

    decodedMsgs = (decodedMsgs + 1) % MAX_DECODED_MESSAGES;
}

/* ============================================================
 * Target tracking accessors
 * ============================================================ */

void FT8SetTargetCall(const char *call) {
    if (call == NULL) {
        ft8TargetCall[0] = '\0';
    } else {
        strncpy(ft8TargetCall, call, sizeof(ft8TargetCall) - 1);
        ft8TargetCall[sizeof(ft8TargetCall) - 1] = '\0';
    }
    PaneSpectrum.stale = true;
}

bool FT8TargetLatestCQ(void) {
    if (cqMsgs <= 0 || cqHead < 0) return false;
    int idx = cqList[cqHead];
    if (idx < 0 || idx >= MAX_DECODED_MESSAGES) return false;
    if (rxBuf[idx].field2[0] == '\0') return false;
    strncpy(ft8TargetCall, rxBuf[idx].field2, sizeof(ft8TargetCall) - 1);
    ft8TargetCall[sizeof(ft8TargetCall) - 1] = '\0';
    PaneSpectrum.stale = true;
    return true;
}

void FT8ClearTarget(void) {
    ft8TargetCall[0] = '\0';
    PaneSpectrum.stale = true;
}

/* ============================================================
 * Lifecycle
 * ============================================================ */

bool InitializeFT8(void) {
    if (ft8Init) return true;

    /* Reset state */
    ft8DecoderState  = FT8_STATE_BUFFERING;
    bufCount         = 0;
    frameCount       = 0;
    ft8SlotWriteIdx  = 0;
    decodedMsgs      = 0;
    activeMsg        = 0;
    txMsgs           = 0;
    qsos             = 0;
    activeQSO        = -1;
    allMsgs = cqMsgs = rxMsgs = 0;
    allHead = cqHead = rxHead = -1;
    slotStartMs      = millis();

    /* Source callsign + grid from ED (persisted) -- defaults are MY_CALL and "AA00",
     * which fold in via the ED struct's in-class member initializers. */
    strncpy(baseCall, ED.callsign, sizeof(baseCall) - 1); baseCall[sizeof(baseCall) - 1] = '\0';
    strncpy(baseGrid, ED.grid,     sizeof(baseGrid) - 1); baseGrid[sizeof(baseGrid) - 1] = '\0';

    if (!InitializeFT8Decoder(baseCall, baseGrid)) {
        return false;
    }
    ft8Init = true;
    return true;
}

void ExitFT8(void) {
    if (!ft8Init) return;
    ExitFT8Decoder();
    ft8Init = false;
}

bool InitializeFT8Decoder(const char *call, const char *grid) {
    if (call != NULL) { strncpy(baseCall, call, 13); baseCall[13] = '\0'; }
    if (grid != NULL) { strncpy(baseGrid, grid, 4);  baseGrid[4]  = '\0'; }
    return ft8lib_InitDecoder();
}

void ExitFT8Decoder(void) {
    ft8lib_ExitDecoder();
}

/* ============================================================
 * Audio buffering
 *
 * BufferFT8Data accumulates samples into ft8SlotBuf (ring buffer over the
 * 15-second slot window). When the slot fills we hand the buffer to
 * RunFT8DecoderLoop which transitions through the FSM.
 * ============================================================ */

void BufferFT8Data(float *buf, int sizeBuf) {
    if (!ft8Init || buf == NULL || sizeBuf <= 0) return;

    for (int i = 0; i < sizeBuf; i++) {
        if (ft8SlotWriteIdx < FT8_SLOT_SAMPLES) {
            ft8SlotBuf[ft8SlotWriteIdx++] = buf[i];
        } else {
            /* Slot full -- drop excess until the FSM tick rolls us over */
            return;
        }
    }
}

/* ============================================================
 * FSM tick. Mirrors T41's FT8DecoderLoop in ft8.cpp.
 * Should be called once per main-loop iteration from Loop.cpp.
 * ============================================================ */

void RunFT8DecoderLoop(void) {
    if (!ft8Init) return;

    /* PTT release-on-completion: when FT8 finishes pushing the message audio,
     * drop the radio back to receive. Tracked across calls so we only dispatch
     * once on the falling edge. */
    static bool s_lastTxInProgress = false;
    bool nowTxInProgress = FT8IsTxInProgress();
    if (s_lastTxInProgress && !nowTxInProgress) {
        ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_RELEASED);
    }
    s_lastTxInProgress = nowTxInProgress;

    /* Slot-aligned TX gate (UTC subsecond aligned).
     *
     * FT8 transmits in 15-second UTC slots starting at second N where
     * N % 15 == 0. Even slots: {00, 30}. Odd slots: {15, 45}. The operator
     * picks even or odd via ft8IntState (0 = even, 1 = odd). The WSJT-X
     * convention is to start TX +500 ms into the slot, leaving 0.5 s of
     * margin for relay settling and clean GFSK ramp-up. Receivers tolerate
     * a few hundred ms of slop, so we fire anywhere in the [500, 800) ms
     * window of the slot.
     *
     * Subsecond calibration: now() returns whole seconds; we snapshot
     * millis() at the moment now() ticks to derive ms-within-second.
     * Calibration becomes valid after the first observed second-tick
     * (which on a freshly-booted radio happens within 1 s).
     *
     * Per-slot edge detector marks "TX pending" the first time the slot
     * index increments. It then waits until the [500, 800) ms window in
     * the slot and either fires TX (if conditions met) or marks "expired"
     * (if conditions not met or window closes). Either way the pending
     * flag clears so we don't re-fire mid-slot. */
    {
        static time_t   s_calibrationSec    = 0;
        static uint32_t s_calibrationMillis = 0;
        static bool     s_calibrationValid  = false;
        static uint32_t s_lastTxSlotIdx     = 0;
        static bool     s_txPendingThisSlot = false;

        time_t   t          = now();
        uint32_t nowMillis  = millis();
        if (t != s_calibrationSec) {
            s_calibrationSec    = t;
            s_calibrationMillis = nowMillis;
            s_calibrationValid  = true;
        }
        uint32_t msInSecond = s_calibrationValid
                              ? (nowMillis - s_calibrationMillis)
                              : 0;
        if (msInSecond > 1000) msInSecond = 999;  /* cap if we missed a tick */

        uint32_t currentSlotIdx = (uint32_t)(t / 15);
        uint32_t secInSlot      = (uint32_t)(t % 15);
        uint32_t msInSlot       = secInSlot * 1000u + msInSecond;

        /* Mark TX pending on the rising edge of the slot index. */
        if (currentSlotIdx != s_lastTxSlotIdx) {
            s_lastTxSlotIdx = currentSlotIdx;
            s_txPendingThisSlot = true;
        }

        #define FT8_TX_OFFSET_MS_MIN  500u
        #define FT8_TX_OFFSET_MS_MAX  800u

        /* Once we're in the +500 ms window, evaluate gating. Either fire
         * or expire the pending flag; in both cases we don't re-evaluate
         * until the next slot. */
        if (s_txPendingThisSlot
            && msInSlot >= FT8_TX_OFFSET_MS_MIN
            && msInSlot <  FT8_TX_OFFSET_MS_MAX) {

            s_txPendingThisSlot = false;  /* one-shot per slot */

            bool intervalMatches = ((currentSlotIdx % 2u) == (uint32_t)ft8IntState);
            if (ft8TxState != 0
                && !nowTxInProgress
                && intervalMatches
                && txMsgs > 0
                && txBuf[0].msg[0] != '\0'
                && txBuf[0].status == FT8_MSG_WAITING) {

                if (FT8QueueAndStartTx(txBuf[0].msg, (float)ft8TxFreq)) {
                    txBuf[0].status = FT8_MSG_SENT;
                    txBuf[0].tries++;
                    /* Tell ModeSm we're transmitting; TransmitProcessing in
                     * DSP.cpp will pull FT8 audio via FT8GetNextTxAudioChunk
                     * for the next ~12.6 seconds. */
                    ModeSm_dispatch_event(&modeSM, ModeSm_EventId_PTT_PRESSED);
                    /* Status line in the FT8 pane will show TXMODE so refresh. */
                    PaneSpectrum.stale = true;
                }
            }
        } else if (s_txPendingThisSlot && msInSlot >= FT8_TX_OFFSET_MS_MAX) {
            /* Window closed before we could fire (loop was busy or freshly
             * booted past the window); skip this slot and wait for the next. */
            s_txPendingThisSlot = false;
        }
    }

    switch (ft8DecoderState) {
        case FT8_STATE_BUFFERING: {
            /* Wait until we have a full slot's worth of samples */
            if (ft8SlotWriteIdx < FT8_SLOT_SAMPLES) return;
            bufCount   = 0;
            frameCount = 0;
            ft8DecoderState = FT8_STATE_PROCESSING;
        } break;

        case FT8_STATE_PROCESSING: {
            /* Hand a chunk of the slot to ft8_lib's monitor. T41 splits the
             * slot into FT8_FRAMES_PER_SLOT frames; we follow that pattern. */
            int frameSize = FT8_SLOT_SAMPLES / FT8_FRAMES_PER_SLOT;
            if (frameSize < 1) frameSize = 1;
            int offset = bufCount * frameSize;

            if (offset + frameSize > FT8_SLOT_SAMPLES) {
                ft8DecoderState = FT8_STATE_DECODING;
                return;
            }
            ft8lib_BufferSignal(ft8SlotBuf, frameSize, offset);
            ft8lib_ProcessFrame(frameCount);
            bufCount++;
            frameCount++;

            if (frameCount >= FT8_FRAMES_PER_SLOT) {
                ft8DecoderState = FT8_STATE_DECODING;
            }
        } break;

        case FT8_STATE_DECODING: {
            /* Run the actual ft8_lib decoder over the accumulated frames.
             * Each successful decode triggers AddDecodedMessage above. */
            time_t t = now();
            struct tm slot;
            slot.tm_hour = hour(t);
            slot.tm_min  = minute(t);
            slot.tm_sec  = second(t);
            slot.tm_mday = day(t);
            slot.tm_mon  = month(t) - 1;
            slot.tm_year = year(t) - 1900;
            ft8lib_Decode(&slot);
            ft8DecoderState = FT8_STATE_RX_UPDATE;
        } break;

        case FT8_STATE_RX_UPDATE: {
            /* T41 redraws the RX window here. Phoenix has no FT8 panes yet
             * (TODO: MainBoard_DisplayFT8.cpp). Just advance to next state. */
            DisplayAllMessages();  /* no-op stub */
            ft8DecoderState = FT8_STATE_TX;
        } break;

        case FT8_STATE_TX: {
            /* TX trigger logic moved to the per-tick slot-aligned gate at
             * the top of this function (so transmissions key up at slot
             * boundaries rather than wherever the decode FSM happens to
             * land). This state is now just a placeholder advance. */
            ft8DecoderState = FT8_STATE_TX_UPDATE;
        } break;

        case FT8_STATE_TX_UPDATE: {
            /* Roll over to the next slot. Reset the buffer write index. */
            ft8SlotWriteIdx = 0;
            slotStartMs     = millis();
            ft8DecoderState = FT8_STATE_BUFFERING;
        } break;

        default:
            ft8DecoderState = FT8_STATE_BUFFERING;
            break;
    }
}

/* ============================================================
 * ft8InternalDemod -- the Phoenix dispatcher entry called from
 * Demodulate() in DSP.cpp when ED.modulation == FT8_INTERNAL.
 *
 * Phoenix's DSP runs at much higher rates than FT8's 12 kHz target.
 * We do a simple integer decimation here: assume the audio output of
 * the prior demod stage is already mono in data->I[]. Decimation factor
 * is computed from the DataBlock's sampleRate. If the sample rate is
 * already 12 kHz, we pass straight through.
 *
 * The caller must have configured the SSB filter / bandwidth so the
 * audio centered on ft8RxFreq actually reaches us. That's not done
 * automatically yet (deferred -- see DSP_FT8.h header note).
 * ============================================================ */

void ft8InternalDemod(DataBlock *data) {
    if (!ft8Init || data == NULL || data->N == 0) return;

    /* Decimation factor: sample_rate / 12000, rounded to nearest int. */
    int decim = 1;
    if (data->sampleRate_Hz >= FT8_SAMPLE_RATE_HZ) {
        decim = (int)((data->sampleRate_Hz + FT8_SAMPLE_RATE_HZ / 2) / FT8_SAMPLE_RATE_HZ);
        if (decim < 1) decim = 1;
    }

    /* Assemble decimated mono into a small stack buffer and push. */
    float tmp[256];
    int outIdx = 0;
    for (unsigned i = 0; i < data->N; i += decim) {
        if (outIdx >= (int)(sizeof(tmp) / sizeof(tmp[0]))) {
            BufferFT8Data(tmp, outIdx);
            outIdx = 0;
        }
        /* Use I[] as the audio source (matches AM/SAM/NFM convention -- the
         * upstream demod stage writes mono audio into I[] before the
         * dispatcher case). */
        tmp[outIdx++] = data->I[i];
    }
    if (outIdx > 0) {
        BufferFT8Data(tmp, outIdx);
    }
}

/* ============================================================
 * Transmit
 *
 * FT8 audio is generated by ft8_lib's synth_gfsk into the txSignal buffer
 * (allocated inside the vendored decode_ft8.cpp; size = FTX_TX_NUM_SAMPLES).
 * The ft8_lib generator runs at 12 kHz; Phoenix's TX pipeline expects audio
 * at 24 kHz post-decimation. We do nearest-neighbor 2x upsampling here --
 * a follow-up could use proper polyphase filtering for cleaner spectra.
 * ============================================================ */

bool FT8QueueAndStartTx(const char *message, float frequency) {
    if (message == NULL || message[0] == '\0') return false;
    if (!ft8Init) return false;

    /* ft8lib_GenFT8 takes a non-const char* (it doesn't mutate, but the
     * upstream API is permissive). Copy into a local buffer to be safe. */
    char buf[40];
    strncpy(buf, message, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    if (!ft8lib_GenFT8(buf, frequency)) {
        return false;
    }
    s_ft8TxSampleIdx12k = 0;
    s_ft8TxLast12kIdx   = FT8_TX_TOTAL_12K_SAMPLES;
    s_ft8TxInProgress   = true;
    /* Cache the generated signal pointer for fast subsequent access. */
    ft8TxSignalBuf = ft8lib_GetSignal();
    /* Make sure the polyphase FIR is initialized and clear its delay line
     * so the previous transmission's tail doesn't bleed into this one. */
    EnsureFT8TxFirInit();
    arm_fill_f32(0.0f, s_ft8TxFirState, FT8_TX_FIR_STATE_LEN);
    return true;
}

bool FT8IsTxInProgress(void) {
    return s_ft8TxInProgress;
}

void FT8CancelTx(void) {
    s_ft8TxInProgress = false;
    s_ft8TxSampleIdx12k = 0;
    /* FIR state will be re-zeroed at the start of the next FT8QueueAndStartTx. */
}

/* ============================================================
 * Preset TX message templates
 *
 * Five templates with optional <CALL> and <GRID> placeholders. Slot 0 is the
 * standard "CQ <CALL> <GRID>"; the rest are common QSO closers and a free slot.
 * Substitution happens at queue time so user changes to ED.callsign / ED.grid
 * take effect on the next queue without restart.
 * ============================================================ */
/*
 * Standard FT8 message templates. The <TARGET> placeholder expands to
 * ft8TargetCall (the QSO partner's callsign, auto-tracked from received
 * CQ messages or set explicitly via menu). When ft8TargetCall is empty,
 * <TARGET> falls back to <CALL> so the message is still syntactically
 * valid (just useless as a real reply).
 *
 * Standard FT8 QSO sequence (G3PLX 2017 spec, used by WSJT-X):
 *   1. CQ <CALL> <GRID>          (calling CQ)
 *   2. <TARGET> <CALL> <GRID>    (reply to a CQ; usually first message)
 *   3. <TARGET> <CALL> -10       (signal report)
 *   4. <TARGET> <CALL> R-10      (Rogered, returning report)
 *   5. <TARGET> <CALL> RR73      (Rogered, end of QSO)
 *   6. <TARGET> <CALL> 73        (alternative end)
 *
 * Slot mapping: CQ, 73, RR73, signal report (-10), R-report (R-10).
 */
static const char *s_ft8PresetTemplates[FT8_NUM_TX_PRESETS] = {
    "CQ <CALL> <GRID>",          /* 0: standard CQ */
    "<TARGET> <CALL> 73",        /* 1: end-of-QSO closer */
    "<TARGET> <CALL> RR73",      /* 2: Rogered + closer */
    "<TARGET> <CALL> -10",       /* 3: signal report (-10 dB placeholder) */
    "<TARGET> <CALL> R-10",      /* 4: Rogered, returning report */
};

/* Short labels used by the menu UI. Kept separate from the templates so the
 * UI doesn't show the raw template syntax. */
static const char *s_ft8PresetLabels[FT8_NUM_TX_PRESETS] = {
    "Queue: CQ",
    "Queue: 73",
    "Queue: RR73",
    "Queue: 599",
    "Queue: ID",
};

const char *FT8GetPresetLabel(int slot) {
    if (slot < 0 || slot >= FT8_NUM_TX_PRESETS) return "?";
    return s_ft8PresetLabels[slot];
}

/*
 * Expand a template by substituting <CALL> and <GRID> tokens. Writes up to
 * outMax-1 chars (always nul-terminates). Returns the number of output
 * chars written (excluding the nul).
 */
static int ExpandFT8Template(const char *tmpl, char *out, int outMax) {
    if (tmpl == NULL || out == NULL || outMax <= 0) return 0;
    int o = 0;
    int i = 0;
    while (tmpl[i] != '\0' && o < outMax - 1) {
        if (strncmp(&tmpl[i], "<CALL>", 6) == 0) {
            const char *src = ED.callsign;
            while (*src && o < outMax - 1) out[o++] = *src++;
            i += 6;
        } else if (strncmp(&tmpl[i], "<GRID>", 6) == 0) {
            const char *src = ED.grid;
            while (*src && o < outMax - 1) out[o++] = *src++;
            i += 6;
        } else if (strncmp(&tmpl[i], "<TARGET>", 8) == 0) {
            /* Fall back to ED.callsign when no target is set so the
             * message remains syntactically valid (operator can fix
             * it later via menu Edit Callsign on the queued message --
             * not implemented yet, see deferred follow-ups). */
            const char *src = (ft8TargetCall[0] != '\0')
                              ? ft8TargetCall : ED.callsign;
            while (*src && o < outMax - 1) out[o++] = *src++;
            i += 8;
        } else {
            out[o++] = tmpl[i++];
        }
    }
    out[o] = '\0';
    return o;
}

/* ============================================================
 * Per-band FT8 calling frequencies (Hz, dial / suppressed-carrier).
 *
 * Indexed by BAND_xxxM (defined in SDT.h). 0 means "no FT8 preset".
 * Standard worldwide FT8 calling frequencies as of 2024; some regional
 * variations exist (e.g., 60 m channel selection, 4 m allocation).
 * ============================================================ */
static const int64_t s_ft8BandFreqsHz[NUMBER_OF_BANDS] = {
    1840000LL,    /* BAND_160M */
    3573000LL,    /* BAND_80M  */
    5357000LL,    /* BAND_60M  (channel 4) */
    7074000LL,    /* BAND_40M  */
    10136000LL,   /* BAND_30M  */
    14074000LL,   /* BAND_20M  */
    18100000LL,   /* BAND_17M  */
    21074000LL,   /* BAND_15M  */
    24915000LL,   /* BAND_12M  */
    28074000LL,   /* BAND_10M  */
    50313000LL,   /* BAND_6M   */
    70100000LL,   /* BAND_4M   (region 1; regions 2/3 lack a 4 m allocation) */
    0LL,          /* BAND_GENERAL -- no FT8 preset */
};

int64_t FT8GetBandFreqHz(int band) {
    if (band < 0 || band >= NUMBER_OF_BANDS) return 0LL;
    return s_ft8BandFreqsHz[band];
}

/* set_vfo lives in CAT.cpp. It performs the same band-aware frequency-set
 * machinery the CAT FA/FB write handlers use (saves last-frequency cache,
 * adjusts band, applies the SR offset, dispatches iUPDATE_TUNE). Reuse it
 * here so the tune behavior is identical to "just send a CAT FA command". */
extern void set_vfo(int64_t freq, uint8_t vfo);

void FT8TuneToBandFreq(void) {
    int band = ED.currentBand[ED.activeVFO];
    int64_t freq = FT8GetBandFreqHz(band);
    if (freq == 0LL) {
        /* No preset for this band (e.g., BAND_GENERAL). Bail silently. */
        return;
    }
    set_vfo(freq, ED.activeVFO);
}

void FT8GoToModeAndTune(void) {
    /* 1) Switch to FT8_INTERNAL modulation. Mirror the CAT MD_write
     *    pattern: set both ED.modulation (used by Demodulate dispatcher
     *    in DSP.cpp) AND bands[..].mode (used by IF/MD/SF readbacks
     *    and various display code paths), then fire iMODE so the
     *    hardware state machine refreshes. */
    bands[ ED.currentBand[ED.activeVFO] ].mode = FT8_INTERNAL;
    ED.modulation[ED.activeVFO] = FT8_INTERNAL;
    SetInterrupt(iMODE);

    /* 2) Tune to the current band's FT8 calling frequency. Silently
     *    skips if the band has no preset (modulation switch still
     *    applies). */
    FT8TuneToBandFreq();

    /* 3) Pane/status invalidation -- the iMODE handler doesn't directly
     *    flag the spectrum pane stale, but switching into FT8 mode means
     *    the pane needs to swap from spectrum to FT8 message list. */
    PaneSpectrum.stale = true;
}

bool FT8QueueMessageSlot(int slot) {
    if (slot < 0 || slot >= FT8_NUM_TX_PRESETS) return false;

    /* Expand the template into txBuf[0].msg in place. */
    int n = ExpandFT8Template(s_ft8PresetTemplates[slot],
                              txBuf[0].msg, (int)sizeof(txBuf[0].msg));
    if (n <= 0) return false;

    /* Copy the call/grid into the field-1/field-3 positions for completeness
     * (some downstream code might inspect fields rather than the raw msg). */
    strncpy(txBuf[0].field1, ED.callsign, sizeof(txBuf[0].field1) - 1);
    txBuf[0].field1[sizeof(txBuf[0].field1) - 1] = '\0';
    strncpy(txBuf[0].field3, ED.grid, sizeof(txBuf[0].field3) - 1);
    txBuf[0].field3[sizeof(txBuf[0].field3) - 1] = '\0';
    txBuf[0].field2[0] = '\0';

    txBuf[0].freq   = (float)ft8TxFreq;
    txBuf[0].status = FT8_MSG_WAITING;
    txBuf[0].tries  = 0;
    if (txMsgs < 1) txMsgs = 1;

    /* Status line in the FT8 pane could show the queued message in a future
     * follow-up; flag the pane stale to be safe. */
    PaneSpectrum.stale = true;
    return true;
}

int FT8GetNextTxAudioChunk(float *out, int nSamples) {
    if (!s_ft8TxInProgress || out == NULL || nSamples <= 0) return 0;
    if (ft8TxSignalBuf == NULL) {
        s_ft8TxInProgress = false;
        return 0;
    }
    EnsureFT8TxFirInit();

    /*
     * The CMSIS FIR interpolator is initialized with a fixed block size
     * (FT8_TX_FIR_INPUT_BLOCK_12K = 128 in -> 256 out). The single existing
     * caller (DSP.cpp::TransmitProcessing) requests 256 samples per call,
     * which fits exactly one block. We process whole blocks only and return
     * the number of samples actually written.
     *
     * End-of-message handling: when we run past s_ft8TxLast12kIdx we feed
     * zeros into the FIR. This produces a graceful fade-out over the FIR's
     * group delay (~16 input samples / ~1.3 ms). After ~FIR_TAPS samples
     * of zeros the output is back to silence and we mark TX complete.
     */
    int written = 0;
    while ((written + FT8_TX_FIR_OUTPUT_BLOCK_24K) <= nSamples) {
        /* Build one input block of 128 samples; zero-pad past end-of-message. */
        float32_t inputBlock[FT8_TX_FIR_INPUT_BLOCK_12K];
        for (int i = 0; i < FT8_TX_FIR_INPUT_BLOCK_12K; i++) {
            int srcIdx = s_ft8TxSampleIdx12k + i;
            inputBlock[i] = (srcIdx < s_ft8TxLast12kIdx) ? ft8TxSignalBuf[srcIdx] : 0.0f;
        }

        arm_fir_interpolate_f32(&s_ft8TxFirInst,
                                inputBlock,
                                &out[written],
                                (uint32_t)FT8_TX_FIR_INPUT_BLOCK_12K);

        s_ft8TxSampleIdx12k += FT8_TX_FIR_INPUT_BLOCK_12K;
        written            += FT8_TX_FIR_OUTPUT_BLOCK_24K;

        /* TX is complete once we've fed a full FIR length of zeros past
         * the message end -- that's enough to drain the delay line. */
        if (s_ft8TxSampleIdx12k >= s_ft8TxLast12kIdx + FT8_TX_FIR_TAPS) {
            s_ft8TxInProgress = false;
            break;
        }
    }
    return written;
}

/* ============================================================
 * UI / control entry points
 *
 * These mirror T41's ft8.cpp public API (so callers compile against
 * either side) but currently no-op pending Phoenix UI integration.
 * ============================================================ */

void ChangeFT8TxFreq(int wheel) {
    ft8TxFreq += wheel * 5;
    if (ft8TxFreq < 200)  ft8TxFreq = 200;
    if (ft8TxFreq > 2700) ft8TxFreq = 2700;
    if (txEqualsRx) ft8RxFreq = ft8TxFreq;
    /* Status line in MainBoard_DisplayFT8 shows the current TX/RX freqs;
     * flag the pane stale so the next display refresh repaints. */
    PaneSpectrum.stale = true;
}

void ChangeFT8RxFreq(int wheel) {
    ft8RxFreq += wheel * 5;
    if (ft8RxFreq < 200)  ft8RxFreq = 200;
    if (ft8RxFreq > 2700) ft8RxFreq = 2700;
    if (txEqualsRx) ft8TxFreq = ft8RxFreq;
    PaneSpectrum.stale = true;
}

void ChangeFT8TxInterval(int wheel) {
    (void)wheel;
    ft8IntState = (ft8IntState == 0) ? 1 : 0;
    PaneSpectrum.stale = true;  /* INT field shown in pane status line */
}

void ChangeFT8CqState(int wheel) {
    (void)wheel;
    ft8CqState = (ft8CqState == 0) ? 1 : 0;
    PaneSpectrum.stale = true;  /* CQ field shown in pane status line */
}

void ChangeFT8TxState(int wheel) {
    (void)wheel;
    ft8TxState = (ft8TxState == 0) ? 1 : 0;
    PaneSpectrum.stale = true;  /* TXMODE field shown in pane status line */
}

void ScrollFT8MsgWindow(int xcol, int wheel) {
    /* TODO(ft8): scroll the appropriate message window. Needs Phoenix UI
     * integration to know which window contains xcol. */
    (void)xcol;
    (void)wheel;
}

void FT8MsgWindowClick(int x, int y, int button) {
    /* TODO(ft8): handle clicks in CQ/RX/All windows -- select active msg,
     * launch QSO, etc. Needs Phoenix UI integration. */
    (void)x;
    (void)y;
    (void)button;
}

/* ============================================================
 * Display invalidation
 *
 * The actual FT8 pane rendering lives in MainBoard_DisplayFT8.cpp. From
 * here we just flag PaneSpectrum.stale = true so the next DrawDisplay()
 * cycle repaints. PaneSpectrum's DrawSpectrumPane checks the modulation
 * at the top and dispatches to DrawFT8MessageList when in FT8 mode.
 * ============================================================ */

/* MainBoard_Display.h + extern PaneSpectrum moved to file scope at the top
 * of this file so all FT8 functions can flag the pane stale. */

void DisplayAllMessages(void) {
    /* Flag the spectrum pane stale so the next display refresh repaints
     * the FT8 message list (when modulation == FT8_INTERNAL). When the
     * modulation is anything else this is harmless -- the spectrum will
     * just redraw normally. */
    PaneSpectrum.stale = true;
}

/* ============================================================
 * Transmit modulator stub
 *
 * T41's PrepareFT8ExciterIQData synthesizes the FT8 8-FSK signal
 * into IQ samples and feeds them to the exciter. Phoenix has no FT8
 * transmit-chain wiring today. Stubbed.
 * ============================================================ */

void PrepareFT8ExciterIQData(float *sig) {
    /* TODO(ft8): generate FT8 8-FSK exciter IQ for ft8TxFreq, push through
     * Phoenix's TX path (RFBoard / MainBoard_AudioIO). */
    (void)sig;
}
