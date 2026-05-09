#ifndef DSP_FT8_H
#define DSP_FT8_H
#include "SDT.h"

#include <time.h>

/*
 * FT8 receive + transmit (faithful port from T41_SDR/ft8.cpp + ft8.h).
 *
 * This module exposes Phoenix's FT8 surface. The upstream KGOBA ft8_lib
 * library (the actual signal-processing algorithm) is vendored at
 * PhoenixSketch/src/ft8_lib/ verbatim.
 *
 * What works in this port:
 *   - ft8_lib is fully built and linked (decode, encode, ldpc, crc, KISS FFT,
 *     real-time monitor)
 *   - Selecting modulation FT8_INTERNAL routes incoming audio through the
 *     FT8 buffering pipeline (ft8InternalDemod -> BufferFT8Data ->
 *     RunFT8DecoderLoop -> ft8_lib)
 *   - Time-slot tracking using TimeLib (Phoenix already includes it)
 *   - Full FT8 state-machine bookkeeping (ft8DecoderState, ft8SyncState,
 *     ft8TxState, ft8IntState, ft8CqState, message-list maintenance)
 *   - ft8_lib glue (ft8lib_InitDecoder, ft8lib_BufferSignal,
 *     ft8lib_ProcessFrame, ft8lib_Decode, ft8lib_GenFT8)
 *   - Callsign/grid sourced from MY_CALL in Config.h (placeholder default
 *     "ABCDE")
 *
 * What is intentionally stubbed (Phoenix has no equivalent yet):
 *   - All Display* functions are no-ops. Phoenix has no FT8 message panes
 *     today; future work needs a MainBoard_DisplayFT8.cpp matching the
 *     existing 12-pane layout.
 *   - Transmit-side modulator (PrepareFT8ExciterIQData) is a stub. No FT8
 *     transmit-chain wiring exists in Phoenix.
 *   - QSO automation triggers are present in code but not wired to any UI.
 *   - Audio bandpass / FT8-tuned filtering: Phoenix's filters are SSB-shaped.
 *   - WAV-file test path is fully stubbed (T41 had it commented out).
 *
 * Ported from T41_SDR/ft8.h (data structures) and ft8.cpp (functions).
 */

/* ------------------ Window IDs (matches T41 ft8.h) ------------------ */
#define FT8_ALL_WINDOW 0
#define FT8_CQ_WINDOW  1
#define FT8_RX_WINDOW  2
#define FT8_INFO_BOX   3

/* ------------------ TX message status (matches T41 ft8.h) ------------------ */
#define FT8_MSG_WAITING     0
#define FT8_MSG_NEXT        1
#define FT8_MSG_SENT        2
#define FT8_MSG_ACK         3
#define FT8_MSG_TIMEOUT     4
#define FT8_MSG_COMPLETED   5

/* ------------------ FT8 received message ------------------ */
typedef struct {
    char  msg[35];          /**< FTX_MAX_MESSAGE_LENGTH = call[13] sp call[13] sp report[6] terminator */
    char  field1[20];       /**< Three parts of an FT8 message; assemble with sprintf("%.13s %.13s %.6s",...) */
    char  field2[20];
    char  field3[20];
    float freq;             /**< Audio frequency (Hz) */
    uint8_t hour, min, sec;
    float    time_sec;
    struct tm slot_time;
    bool  evenInterval;
    int   sync_score;
    float snr;
} RxMsg;

/* ------------------ FT8 transmit message ------------------ */
typedef struct {
    char  msg[35];
    char  field1[20];
    char  field2[20];
    char  field3[20];
    float freq;
    struct tm slot_time;
    int   status;           /**< 0:wait 1:next 2:sent 3:ack 4:timeout 5:done (FT8_MSG_*) */
    int   tries;
} TxMsg;

/* ------------------ QSO summary view ------------------ */
typedef struct {
    int  type;              /**< 0:CQ, 1:CQ reply */
    char call[20];
    int  msg[6];            /**< Indices: 0,2,4 calling-CQ; 1,3,5 reply */
    int  status;            /**< 0:waiting 1:in-progress 2:completed 3:abandoned */
} QsoView;

/* ------------------ FT8 state (extern, defined in DSP_FT8.cpp) ------------------ */
extern int  ft8SyncState;
extern int  ft8TxFreq;
extern int  ft8RxFreq;
extern int  ft8TxState;
extern int  ft8IntState;
extern int  ft8CqState;
extern bool txEqualsRx;
extern float *ft8TxSignalBuf;

/* ------------------ Decoded message storage exposed for display ------------------ *
 * These were file-static in the initial port; exposing them so MainBoard_DisplayFT8
 * (the FT8 message pane) can read message contents and per-window head/count. */
#define FT8_MAX_DECODED_MESSAGES 50
extern RxMsg rxBuf[FT8_MAX_DECODED_MESSAGES];
extern int   decodedMsgs;            /**< Current write index into rxBuf (0 .. FT8_MAX_DECODED_MESSAGES) */

/* Current QSO target callsign. Auto-updated from the most-recent received CQ
 * (sender of any message starting with "CQ "), can be set explicitly via
 * FT8SetTargetCall() / FT8TargetLatestCQ() / FT8ClearTarget(). Used by
 * ExpandFT8Template's <TARGET> placeholder when queueing reply templates. */
extern char  ft8TargetCall[14];

extern int   allList[FT8_MAX_DECODED_MESSAGES];  /**< Indices into rxBuf, newest at allHead */
extern int   allMsgs;                /**< Number of messages currently in allList */
extern int   allHead;                /**< Most-recent index in allList (-1 if empty) */

extern int   cqList[FT8_MAX_DECODED_MESSAGES];   /**< Subset of allList: messages starting with "CQ " */
extern int   cqMsgs;
extern int   cqHead;

extern int   rxList[FT8_MAX_DECODED_MESSAGES];   /**< Subset of allList: messages within +/-50 Hz of ft8RxFreq */
extern int   rxMsgs;
extern int   rxHead;

/* ============================================================
 * Lifecycle
 * ============================================================ */

/**
 * @brief Initialize the FT8 subsystem (allocates buffers, primes ft8_lib).
 * @return true on success, false on allocation failure.
 * @note Safe to call once at startup. Idempotent on second call.
 *       Phoenix integration: call from setup() (Globals.cpp) AFTER ED is loaded.
 */
bool InitializeFT8(void);

/**
 * @brief Tear down the FT8 subsystem.
 */
void ExitFT8(void);

/**
 * @brief Initialize the ft8_lib decoder with operator's callsign and grid.
 * @param call  Operator callsign (max 13 chars). NULL falls back to MY_CALL from Config.h.
 * @param grid  Maidenhead grid square (4 or 6 chars). NULL falls back to "AA00".
 * @return true on success.
 */
bool InitializeFT8Decoder(const char *call, const char *grid);

/** @brief Tear down the ft8_lib decoder. */
void ExitFT8Decoder(void);

/* ============================================================
 * Audio buffering and decoder loop
 * ============================================================ */

/**
 * @brief Push a chunk of demodulated audio into the FT8 15-second slot buffer.
 * @param buf       Pointer to mono audio samples (12 kHz expected).
 * @param sizeBuf   Number of samples in buf.
 * @note Called by ft8InternalDemod() in DSP.cpp on each I/Q block, after
 *       decimation to 12 kHz. Triggers RunFT8DecoderLoop() at slot boundaries.
 */
void BufferFT8Data(float *buf, int sizeBuf);

/**
 * @brief Run one tick of the FT8 decoder state machine.
 * @note Phoenix integration: call from the main loop. Idempotent if no slot
 *       boundary has elapsed since the previous call.
 */
void RunFT8DecoderLoop(void);

/* ============================================================
 * Transmit
 *
 * FT8 TX in Phoenix has three pieces:
 *   1. Encoding -- ft8_lib's ftx_message_encode + ft8_encode + synth_gfsk
 *      (wrapped by ft8lib_GenFT8) build a 12 kHz audio signal for the
 *      message into ft8_lib's txSignal buffer. ~12.6 seconds of audio.
 *   2. Audio injection -- DSP.cpp::TransmitProcessing checks
 *      FT8IsTxInProgress() at the top; if true, it pulls FT8 audio via
 *      FT8GetNextTxAudioChunk() instead of reading the microphone.
 *   3. Slot timing -- RunFT8DecoderLoop()'s TX state checks the UTC clock,
 *      starts TX at the right moment in even/odd slots, dispatches the
 *      ModeSm PTT event, runs ~12.6 seconds, then releases PTT.
 * ============================================================ */

/**
 * @brief Encode a message into FT8 audio and queue it for transmission.
 * @param message ASCII FT8 message (e.g. "CQ K1ABC FN42").
 * @param frequency Audio carrier frequency in Hz (typically ft8TxFreq).
 * @return true on successful encode + queue, false on error.
 * @note Calls ft8lib_GenFT8 internally. After success, FT8IsTxInProgress()
 *       returns true until the full audio has been pulled via
 *       FT8GetNextTxAudioChunk(). Caller is also responsible for dispatching
 *       the ModeSm PTT_PRESSED event if Phoenix's TX hardware needs it.
 */
bool FT8QueueAndStartTx(const char *message, float frequency);

/**
 * @brief Whether an FT8 message is currently being transmitted.
 * @return true if FT8 audio is queued and not fully consumed yet.
 */
bool FT8IsTxInProgress(void);

/**
 * @brief Pull the next chunk of FT8 audio (already decimation-rate matched).
 * @param[out] out      Buffer to fill with audio samples (interleaved upsample
 *                      to 24 kHz from ft8_lib's 12 kHz internal rate).
 * @param[in]  nSamples Number of samples requested.
 * @return Number of samples actually written. 0 means no TX in progress.
 *         If less than nSamples returned, the message is finished and the
 *         caller should drop back to mic audio.
 * @note Called by DSP.cpp::TransmitProcessing during FT8 TX.
 */
int FT8GetNextTxAudioChunk(float *out, int nSamples);

/**
 * @brief Cancel any in-progress FT8 transmission immediately.
 * @note Useful when the operator releases PTT mid-message or a higher-priority
 *       event (e.g. mode change) needs to abort.
 */
void FT8CancelTx(void);

/* ============================================================
 * Preset TX message slots
 *
 * Five user-editable templates that the FT8 menu page can queue with one
 * click. Templates support two placeholder tokens that are substituted at
 * queue time:
 *   <CALL>  -> ED.callsign (defaults to MY_CALL)
 *   <GRID>  -> ED.grid     (defaults to "AA00")
 *
 * Default templates: a CQ template, two QSO closers, a signal report, and
 * one user-customizable slot. Operators can overwrite any of these in code
 * (or via a future CAT/menu editor).
 * ============================================================ */
#define FT8_NUM_TX_PRESETS  5

/**
 * @brief Get the human-readable label for a preset slot (for menu display).
 * @param slot 0 .. FT8_NUM_TX_PRESETS-1
 * @return Static string label, or "?" if slot is out of range.
 */
const char *FT8GetPresetLabel(int slot);

/**
 * @brief Queue a preset template into the TX slot for the next FT8 cycle.
 * @param slot 0 .. FT8_NUM_TX_PRESETS-1
 * @return true on success (txBuf[0] populated, status = FT8_MSG_WAITING),
 *         false on out-of-range slot or expansion failure.
 * @note Substitutes <CALL> and <GRID> placeholders from ED.callsign and
 *       ED.grid at queue time. Overwrites whatever was previously in
 *       txBuf[0]; subsequent FT8 slot boundaries will pick this up.
 */
bool FT8QueueMessageSlot(int slot);

/* ============================================================
 * Per-band FT8 calling-frequency presets
 *
 * Standard FT8 dial frequencies for each of Phoenix's bands, indexed by
 * BAND_160M..BAND_4M. A value of 0 means "no FT8 preset for this band"
 * (e.g., the BAND_GENERAL catch-all slot).
 * ============================================================ */

/**
 * @brief Get the FT8 calling dial frequency for a band.
 * @param band Band index (BAND_160M .. BAND_4M, etc.)
 * @return Frequency in Hz, or 0 if no FT8 preset exists for this band.
 */
int64_t FT8GetBandFreqHz(int band);

/**
 * @brief Retune the active VFO to the current band's standard FT8 frequency.
 * @note No-op if the current band has no FT8 preset (FT8GetBandFreqHz returns 0).
 *       Does NOT change modulation -- the operator can use DEMODULATION to
 *       cycle into FT8_INTERNAL separately. This separation keeps the tune
 *       useful for monitoring FT8 traffic in plain USB before committing to
 *       internal decode.
 */
void FT8TuneToBandFreq(void);

/* ============================================================
 * QSO target tracking
 *
 * The "target" is the callsign Phoenix is currently in QSO with (or about
 * to call). Reply templates substitute <TARGET> with this value; without
 * it, replies fall back to <CALL> for the target field (which is wrong
 * but still produces a syntactically-valid FT8 message).
 *
 * The target auto-updates whenever a CQ message is decoded: AddDecodedMessage
 * extracts the sender (field2) of any message starting with "CQ " and
 * stores it. Operators can also set the target explicitly from the menu.
 * ============================================================ */

/**
 * @brief Set the QSO target callsign explicitly.
 * @param call ASCII callsign (max 13 chars). NULL or empty clears.
 */
void FT8SetTargetCall(const char *call);

/**
 * @brief Set the target to the most-recent CQ caller (head of cqList).
 * @return true if a target was set, false if no CQ has been received.
 */
bool FT8TargetLatestCQ(void);

/**
 * @brief Clear the QSO target.
 */
void FT8ClearTarget(void);

/**
 * @brief One-click "go to FT8 mode + tune": switch the active VFO to
 *        FT8_INTERNAL modulation AND retune to the current band's FT8
 *        calling frequency in a single operation.
 * @note Tune is silently skipped when the current band has no FT8 preset;
 *       the modulation switch still applies. Mirrors the CAT MD_write
 *       pattern (sets bands[..].mode + ED.modulation + fires iMODE) so
 *       the FT8 dispatcher in Demodulate() and the FT8 pane both pick up
 *       the new state immediately.
 */
void FT8GoToModeAndTune(void);

/* ============================================================
 * UI / control entry points (currently stubs)
 * ============================================================ */

void ChangeFT8TxFreq(int wheel);
void ChangeFT8RxFreq(int wheel);
void ChangeFT8TxInterval(int wheel);
void ChangeFT8CqState(int wheel);
void ChangeFT8TxState(int wheel);
void ScrollFT8MsgWindow(int xcol, int wheel);
void FT8MsgWindowClick(int x, int y, int button);

#endif /* DSP_FT8_H */
