#ifndef DMR_MODEM_H
#define DMR_MODEM_H
#include "SDT.h"

/**
 * @file DmrModem.h
 * @brief DMR 4FSK modulator / demodulator with TDMA slot gating.
 *
 * Architectural placement:
 *
 *   AmbeDvsi   (PCM <-> AMBE+2 49-bit)
 *      ↕
 *   DmrFraming (LC + FEC + Talk Group / Color Code addressing)
 *      ↕                                                  this module
 *   DmrModem   (4FSK 4800 sym/s, RRC shaping, TS1/TS2 burst gating)
 *      ↕
 *   NFM / FM_WIDE audio path (existing Phoenix DSP)
 *
 * On Phoenix the DMR 4FSK baseband rides as audio-rate samples in/out of
 * the existing FM modulator and FM demodulator. The modem is therefore a
 * pure DSP block that consumes and produces audio-rate float samples;
 * routing into and out of the FM chain is done in DSP.cpp /
 * MainBoard_AudioIO.cpp when DigitalCoding == DigitalCodingDMR.
 *
 * Modulation:
 *   - Symbol rate    : 4800 sym/s (fixed)
 *   - Symbols/bit    : 2 bits per symbol  ⇒  9600 bps
 *   - Symbol mapping : ETSI TS 102 361-1 §6.1.1
 *                          dibit 01 → +3   (+1.944 kHz)
 *                          dibit 00 → +1   (+0.648 kHz)
 *                          dibit 10 → −1   (−0.648 kHz)
 *                          dibit 11 → −3   (−1.944 kHz)
 *                      The modem produces a normalized symbol amplitude
 *                      sequence in {−3, −1, +1, +3}; the FM modulator
 *                      gain converts that to deviation kHz.
 *   - Pulse shape    : root-raised-cosine, roll-off α = 0.2, span 8 sym
 *   - Bandwidth      : ~5.76 kHz two-sided baseband; fits NFM (12.5 kHz)
 *                      and trivially FM_WIDE (25 kHz).
 *
 * TDMA timing:
 *   - Frame  = 60 ms (two slots of 30 ms each)
 *   - Slot   = 30 ms = 144 symbols at 4800 sym/s
 *   - Burst  = 264 bits payload + sync + EMB; rendered as ~132 symbols
 *              of voice/data plus CACH and short guard intervals.
 *   - On TX the modem emits real samples only during the configured slot
 *     (dmrConfig.timeSlot) and silence/zeros otherwise, so the FM
 *     transmitter unkeys cleanly between slots.
 *   - On RX the modem applies the matched RRC filter, runs symbol-timing
 *     recovery, slices to symbols, and reassembles bytes for the framing
 *     layer to validate.
 *
 * Sample rate handling:
 *   The modem is initialized at the audio sample rate it shares with the
 *   FM mod/demod path. Samples-per-symbol is sampleRate / 4800 -- it
 *   need not be an integer at construction time but must be a positive
 *   real value > 2 for Nyquist. Recommended rates: 24 kHz (5 sps),
 *   48 kHz (10 sps), 96 kHz (20 sps). 8 kHz is too low (1.67 sps) and is
 *   rejected with DMR_MODEM_ERR_RATE.
 *
 * State: API-only. DmrModem.cpp ships as stubs that return failure /
 * silence so the build remains green; implementation lands as the DSP
 * primitives (RRC shaper, timing recovery loop, slot scheduler) come
 * online.
 */

/** Fixed DMR symbol rate. */
#define DMR_MODEM_SYMBOL_RATE_HZ      4800

/** Two bits per 4FSK symbol. */
#define DMR_MODEM_BITS_PER_SYMBOL     2

/** RRC pulse-shape parameters. */
#define DMR_MODEM_RRC_ALPHA           0.2f
#define DMR_MODEM_RRC_SPAN_SYMBOLS    8

/** Symbol-amplitude alphabet: {−3, −1, +1, +3}. */
#define DMR_MODEM_SYMBOL_LEVELS       4

/** TDMA timing in milliseconds. */
#define DMR_MODEM_FRAME_MS            60u
#define DMR_MODEM_SLOT_MS             30u

/** Driver result codes. */
typedef enum {
    DMR_MODEM_OK         = 0,
    DMR_MODEM_ERR_RATE,        /**< Sample rate < 2 * symbol rate, or non-positive. */
    DMR_MODEM_ERR_NOT_INIT,    /**< API called before DmrModemInit(). */
    DMR_MODEM_ERR_NOT_READY,   /**< Modem busy (TX queue full / RX not locked). */
    DMR_MODEM_ERR_BUSY,        /**< Underlying DSP block is mid-burst. */
    DMR_MODEM_ERR_INTERNAL
} DmrModemResult;

/** Top-level run state. */
typedef enum {
    DMR_MODEM_STATE_IDLE = 0,        /**< Initialized, no activity. */
    DMR_MODEM_STATE_TX_PENDING,      /**< Burst queued, waiting for our slot. */
    DMR_MODEM_STATE_TX_RENDERING,    /**< Emitting modulated samples this slot. */
    DMR_MODEM_STATE_RX_SEARCHING,    /**< Listening; no symbol-timing lock yet. */
    DMR_MODEM_STATE_RX_LOCKED,       /**< Locked; emitting decoded burst bytes. */
    DMR_MODEM_STATE_ERROR
} DmrModemState;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

/**
 * @brief Initialize the modem for a given audio sample rate.
 * @param sampleRateHz Audio sample rate of the shared FM-mod/demod path
 *                     (e.g. 48000). Must satisfy
 *                     sampleRateHz > 2 * DMR_MODEM_SYMBOL_RATE_HZ.
 * @return DMR_MODEM_OK on success, DMR_MODEM_ERR_RATE if too low.
 *
 * Allocates the RRC FIR taps and the symbol-rate timing recovery state.
 * Idempotent: calling again with a different rate tears down and rebuilds
 * the DSP state.
 */
DmrModemResult DmrModemInit(float sampleRateHz);

/** @brief Tear down all DSP state. Safe to call from a deinit path. */
void DmrModemShutdown(void);

/**
 * @brief Apply the active dmrConfig (slot, color code, IDs).
 *
 * The modem only consumes timeSlot here; addressing fields are read by
 * DmrFraming. Call this after editing dmrConfig at runtime.
 */
void DmrModemApplyConfig(void);

/** @brief Current modem state (cheap; safe to poll from the main loop). */
DmrModemState DmrModemGetState(void);

/* -------------------------------------------------------------------------
 * TX side -- bytes from DmrFraming flow in here, audio samples flow out
 * ---------------------------------------------------------------------- */

/**
 * @brief Queue one DMR burst (already framed + FEC'd) for transmission.
 * @param burst Burst bytes from DmrTxBuildVoiceFrame().
 * @param len   Number of valid bytes in @p burst (0 < len ≤ DMR_BURST_BYTES_MAX).
 * @return DMR_MODEM_OK if accepted; DMR_MODEM_ERR_BUSY if a previous
 *         burst is still rendering; DMR_MODEM_ERR_NOT_INIT if the modem
 *         has not been initialized.
 *
 * The modem holds the burst until DmrSlotIsOurs(millis()) becomes true,
 * at which point DmrModemRender() begins emitting modulated samples.
 */
DmrModemResult DmrModemTxQueueBurst(const uint8_t *burst, size_t len);

/**
 * @brief Render @p numSamples of TX baseband audio into @p audioOut.
 * @param audioOut    Destination buffer.
 * @param numSamples  Length of @p audioOut in samples.
 * @return Number of samples actually written. The remainder (if any) is
 *         filled with 0.0f so the caller can sum the buffer into the
 *         FM modulator input unconditionally.
 *
 * Drives slot gating internally: outside our TS, this returns 0 (and
 * fills with zeros) so the FM transmitter naturally unkeys.
 *
 * Safe to call once per audio block from the FM-modulator producer.
 */
size_t DmrModemRender(float *audioOut, size_t numSamples);

/** @brief True iff the TX path can accept another burst right now. */
bool DmrModemTxBufferAvailable(void);

/* -------------------------------------------------------------------------
 * RX side -- audio samples from FM demod flow in, decoded bytes flow out
 * ---------------------------------------------------------------------- */

/**
 * @brief Push @p numSamples of FM-demodulated audio into the modem.
 * @param audioIn    Audio samples (typically the FM discriminator output,
 *                   already band-limited to ~5 kHz).
 * @param numSamples Length of @p audioIn in samples.
 *
 * Runs the matched RRC filter, symbol-timing recovery, and 4FSK slicing.
 * On a successful burst boundary, the decoded bytes are made available
 * via DmrModemRxPopBurst().
 *
 * Non-blocking; designed to be called once per audio block from the FM
 * demodulator consumer.
 */
void DmrModemRxFeed(const float *audioIn, size_t numSamples);

/**
 * @brief Pop the next decoded burst, if one is ready.
 * @param burstOut Destination buffer, must hold ≥ DMR_BURST_BYTES_MAX.
 * @param lenOut   [out] Number of valid bytes written to @p burstOut.
 * @return true if a burst was returned (caller hands it to
 *         DmrRxParseFrame()); false if no burst is ready.
 */
bool DmrModemRxPopBurst(uint8_t *burstOut, size_t *lenOut);

/** @brief True iff the RX side has acquired symbol-timing lock. */
bool DmrModemRxIsLocked(void);

/* -------------------------------------------------------------------------
 * Diagnostics (read-only)
 * ---------------------------------------------------------------------- */

/**
 * @brief Last measured RX symbol-clock offset, in ppm relative to the
 *        configured sample rate. Useful for tuning the audio sample-rate
 *        conversion that bridges this modem to the FM chain.
 */
float DmrModemRxClockPpm(void);

/**
 * @brief Last RX RSSI proxy: mean absolute amplitude of the matched-
 *        filter output, normalized so a clean ±3 outer symbol reads ~1.0.
 */
float DmrModemRxLevel(void);

/**
 * @brief Most recent SYNC-detection timestamp.
 * @param outRefUs [out] micros() reading at the moment SYNC was last
 *                       confirmed (acquisition or per-burst re-validation).
 * @return true if a SYNC has been seen during the current lock; false
 *         if the modem is searching or has timed out.
 *
 * Used by the framing layer to anchor TDMA slot timing to the network's
 * frame phase (Tier-II repeater operation) instead of free-running off
 * the local micros() clock.
 */
bool DmrModemGetSyncRef(uint32_t *outRefUs);

#endif /* DMR_MODEM_H */
