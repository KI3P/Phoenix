#ifndef DSP_PSK31_H
#define DSP_PSK31_H
#include "SDT.h"

/*
 * PSK31 primitives (data tables + clean DBPSK demod) -- ported from
 * T41_SDR/psk31.cpp + psk31.h.
 *
 * Scope of this port (intentional): the legitimately reusable parts of T41's
 * PSK31 code -- the standard PSK31 varicode lookup tables, the varicode
 * encoder/decoder, and the differential-BPSK symbol decoder -- ported into
 * Phoenix's DataBlock layout and stripped of Serial.print debug spam.
 *
 * What this port deliberately does NOT include:
 *   - T41's five experimental Psk31PhaseShiftDetector* implementations.
 *     They use ad-hoc thresholds with hand-tuned magic constants and no
 *     symbol-clock synchronization. A proper Phoenix implementation should
 *     be written from scratch using Phoenix's FFT/Goertzel infrastructure.
 *   - Symbol-clock recovery. T41's timing_recovery_cc() is commented out
 *     in the upstream source; without it neither codebase can reliably
 *     decode real-world PSK31 signals.
 *   - WAV-file test path (commented out in T41's source).
 *   - UI / display surface for decoded text. Phoenix has no decoded-text
 *     pane today; that's a separate UI port.
 *
 * Phoenix integration: nothing currently calls into this module. It builds
 * the primitives (varicode tables, DBPSK demod, decoder state) so a future
 * Phoenix-native PSK31 implementation has them ready. To make PSK31
 * end-to-end, future work will need:
 *   (a) A new modulation enum value (PSK31 = 6) in SDT.h
 *   (b) A dispatcher case in Demodulate() in DSP.cpp
 *   (c) A narrow CW-style audio bandpass filter at the PSK31 audio center
 *   (d) Symbol-clock synchronization (Gardner / early-late / Mueller-Muller)
 *   (e) A decoded-text display pane
 */

/**
 * @brief Push one demodulated PSK31 symbol bit into the varicode decoder.
 * @param symbol  The next demodulated bit (treated as nonzero = 1, zero = 0).
 * @return ASCII character if a complete varicode codeword was matched on
 *         this push, otherwise 0.
 * @note Maintains internal shift-register state across calls. Call
 *       ResetPSK31Decoder() between independent decode sessions.
 *       Ported from T41_SDR/psk31.cpp::psk31_varicode_decoder_push (Serial
 *       debug removed). Standard PSK31 varicode (G3PLX / 1998).
 */
char PSK31VaricodeDecoderPush(uint8_t symbol);

/**
 * @brief Encode an ASCII string as a sequence of PSK31 varicode bits.
 * @param[in]  input            Pointer to ASCII input bytes.
 * @param[out] output           Buffer to receive encoded bits (one bit per byte, 0 or 1).
 * @param[in]  input_size       Number of input bytes available.
 * @param[in]  output_max_size  Maximum number of output bits the caller will accept.
 * @param[out] input_processed  Number of input bytes actually consumed.
 * @param[out] output_size      Number of output bits actually produced.
 * @note Each input character expands to bitcount + 2 bits (codeword + 2 zero
 *       inter-character bits). Stops early if output_max_size is exhausted.
 *       Ported from T41_SDR/psk31.cpp::psk31_varicode_encoder_u8_u8 unchanged.
 */
void PSK31VaricodeEncode(const uint8_t *input, uint8_t *output,
                         int input_size, int output_max_size,
                         int *input_processed, int *output_size);

/**
 * @brief Differential-BPSK symbol decoder.
 * @param[in]  data        Pointer to a DataBlock containing complex baseband
 *                         samples. data->I[i] is the real component, data->Q[i]
 *                         the imaginary component, for i in [0, data->N).
 * @param[out] output      Buffer to receive decoded bits (one bit per sample,
 *                         0 if differential phase change > +/-PI/2 else 1).
 *                         Must be at least data->N bytes.
 * @note Maintains last-phase state across calls (each block is differentially
 *       decoded relative to the previous block's last sample). Call
 *       ResetPSK31Decoder() between independent decode sessions.
 *       Adapted from T41_SDR/psk31.cpp::dbpsk_decoder_c_u8 (which used
 *       interleaved I/Q) to Phoenix's separate I[]/Q[] DataBlock layout.
 *       This is *symbol* decoding only -- there is no symbol-clock recovery,
 *       so callers must downsample data->N to one sample per PSK31 symbol
 *       (~31.25 baud) before invoking this. See header comment.
 */
void PSK31DBPSKDecode(const DataBlock *data, uint8_t *output);

/**
 * @brief Reset the varicode decoder shift register and DBPSK last-phase state.
 * @note Call between independent decode sessions to avoid spurious matches
 *       at the start of a new burst.
 */
void ResetPSK31Decoder(void);

/* ============================================================
 * Phoenix-native end-to-end decoder.
 *
 * Built on top of the primitives above. Implements the full receive chain:
 *   audio in -> NCO mix to baseband -> low-pass -> decimate -> Gardner
 *   symbol-clock recovery -> DBPSK -> varicode -> decoded-text ring buffer.
 *
 * Used via the FT8-style mode dispatch: when ED.modulation == PSK31, the
 * Demodulate() dispatcher in DSP.cpp calls psk31Demod(data) per audio block;
 * the display layer reads the ring buffer to render decoded text.
 * ============================================================ */

/* PSK31 audio-carrier frequency (Hz). Adjustable via the fine-tune encoder
 * in PSK31 mode and via the PSK31 menu page. Range 200-2700 Hz. */
extern int psk31RxFreq;

/**
 * @brief Phoenix dispatcher entry. Run one audio block through the PSK31
 *        decode chain.
 * @param data  DataBlock with the I/Q output of the IFFT in data->I/data->Q
 *              at audio rate (typically 24 kHz).
 * @note Maintains internal pipeline state across calls (NCO phase, low-pass
 *       state, decimator counter, Gardner mu, varicode shift register, ring
 *       buffer). Decoded characters land in the text ring buffer for the
 *       display layer to consume.
 */
void psk31Demod(DataBlock *data);

/**
 * @brief Adjust the PSK31 audio receive frequency by a fixed step.
 * @param wheel Encoder ticks; positive = up, negative = down. 5 Hz per tick.
 * @note Bounds-checked to [200, 2700] Hz. Resets the decoder pipeline so
 *       the new carrier doesn't get glitched samples from the old one.
 *       Marks the PSK31 pane stale.
 */
void ChangePSK31RxFreq(int wheel);

/* Decoded-text ring buffer size (chars). Older characters are overwritten. */
#define PSK31_TEXT_BUFFER_SIZE  256

/**
 * @brief Read a contiguous snapshot of the decoded-text ring buffer.
 * @param[out] out      Caller-provided buffer to fill.
 * @param[in]  outMax   Size of caller's buffer.
 * @return Number of chars written (always nul-terminated; never exceeds outMax-1).
 * @note Returns the buffer linearised so callers don't have to handle the ring.
 *       Newest character is at index (returned_length - 1).
 */
int PSK31GetText(char *out, int outMax);

/**
 * @brief Clear the decoded-text ring buffer.
 */
void PSK31ClearText(void);

/**
 * @brief Reset the full decoder pipeline (mixer + filter + clock + varicode).
 * @note Same as ChangePSK31RxFreq's reset side-effect, but without changing
 *       the frequency. Useful when entering PSK31 mode from another mode.
 */
void ResetPSK31Pipeline(void);

/* ============================================================
 * Decoder-status sub-page accessors.
 *
 * The PSK31 pane has an alternate "decoder status" view, toggled by the
 * fine-tune encoder push switch in PSK31 mode (handled in Loop.cpp). When
 * active, the pane shows live Gardner clock-recovery telemetry instead of
 * decoded text:
 *   - Gardner mu (fractional symbol position; should converge near 0)
 *   - Latest dphase (radians; should be near 0 or +/-PI for solid BPSK)
 *   - Smoothed |Gardner timing error| magnitude
 *
 * The decoder updates these counters on every emitted symbol; the pane
 * polls them via PSK31GetDecoderStatus and re-renders on its stale flag.
 * ============================================================ */

/**
 * @brief Toggle the PSK31 decoder-status sub-page on or off.
 * @note Marks the PSK31 pane stale so the new view paints on the next
 *       DrawDisplay tick. Has no effect outside PSK31 mode.
 */
void PSK31ToggleStatusPage(void);

/**
 * @brief Query whether the decoder-status sub-page is currently active.
 * @return true if the pane should render the status view, false for the
 *         decoded-text view.
 */
bool PSK31IsStatusPageActive(void);

/**
 * @brief Snapshot the latest decoder telemetry.
 * @param[out] mu             Fractional Gardner symbol position [0, T).
 * @param[out] dphase         Latest differential phase at the symbol point
 *                            (radians, wrapped to (-PI, PI]).
 * @param[out] err_smoothed   Smoothed |Gardner timing error| magnitude.
 * @note Any output pointer may be NULL. Values are updated on every emitted
 *       symbol inside psk31Demod.
 */
void PSK31GetDecoderStatus(float *mu, float *dphase, float *err_smoothed);

#endif /* DSP_PSK31_H */
