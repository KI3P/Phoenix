#ifndef DMR_FRAMING_H
#define DMR_FRAMING_H
#include "SDT.h"

/**
 * @file DmrFraming.h
 * @brief DMR voice frame builder/parser -- Talk Group encoding/decoding,
 *        Color Code, FEC, and slot-timing helpers.
 *
 * Architectural placement:
 *
 *   AmbeDvsi  (PCM <-> AMBE+2 49-bit)
 *      ↕                                  this module
 *   DmrFraming (LC header / VOICE A..F / EMB+SYNC / Hamming + BPTC)
 *      ↕
 *   DmrModem  (4FSK 4800 sym/s, TS1/TS2 burst gating)
 *      ↕
 *   NFM / FM_WIDE RF (existing Phoenix DSP)
 *
 * Responsibilities of this layer:
 *
 *   TX side -- DmrTxBuildVoiceFrame()
 *     - Wrap a 49-bit AMBE+2 frame in DMR voice payload structure
 *     - Insert Color Code, Source ID, Destination ID (Talk Group or
 *       private radio ID), call type (group/private) into LC headers
 *     - Apply BPTC(196,96) FEC on LC, Hamming(15,11) on EMB
 *     - Hand the resulting bit/byte buffer to DmrModem for 4FSK + TS
 *       burst gating
 *
 *   RX side -- DmrRxParseFrame()
 *     - Validate Color Code (drop foreign-network bursts)
 *     - Run inverse FEC on LC and EMB
 *     - Extract Source ID, Destination ID, call type
 *     - Apply RX Talk Group whitelist (dmrConfig.rxTgFilter)
 *     - On match, hand the embedded 49-bit AMBE+2 frame to AmbeDvsi for
 *       decode and audio out
 *
 *   Slot timing -- DmrSlotIsOurs()
 *     - DMR is TDMA. Each 60 ms frame is split into two 30 ms slots; the
 *       modem layer asks this module which slot it should TX into.
 *
 * State: implementation deferred. Every entry point in DmrFraming.cpp is
 * currently a stub that returns false / 0. This header exists so other
 * modules (DmrModem, future menu code, unit tests) have a stable API to
 * compile against while the implementation is being written.
 */

/**
 * Bytes in one full DMR voice burst payload (264 bits = 33 bytes per slot,
 * less the slot's sync/EMB depending on which voice frame within the
 * superframe). Sized to the upper bound; producers/consumers pass actual
 * lengths via the size_t arguments below.
 */
#define DMR_BURST_BYTES_MAX  33

/**
 * @brief Configure the framing layer with current Talk Group / slot / IDs.
 * @param cfg Pointer to live config; the framing layer holds a copy.
 * @note Safe to call any time; takes effect on the next frame.
 */
void DmrConfigure(const DmrConfig *cfg);

/**
 * @brief FEC correction count from the most recently parsed burst.
 * @return Number of single-bit corrections the BPTC + Hamming decoders
 *         applied during the last DmrRxParseLcHeaderBurst /
 *         DmrRxParseFrame / DmrRxParseEmbeddedLc call. 0 means a clean
 *         channel; rising values indicate the link is approaching the
 *         FEC's error-correction limit. Surfaced by the home pane as a
 *         crude BER proxy.
 */
int DmrLastBurstCorrections(void);

/**
 * @brief Read the most recently observed received-call addressing.
 * @param outType   [out, optional] Group / Private.
 * @param outSrcId  [out, optional] Source DMR Radio ID.
 * @param outDstId  [out, optional] Destination (TG number for Group calls).
 * @param outAgeMs  [out, optional] Milliseconds since the last successful
 *                  DmrRxParseLcHeaderBurst or DmrRxParseFrame, or
 *                  UINT32_MAX if no call has ever been seen.
 * @return true if at least one call has been observed since boot.
 *
 * Used by the home screen to render an "active DMR call" status pane.
 */
bool DmrLastCallSeen(DmrCallType *outType,
                     uint32_t    *outSrcId,
                     uint32_t    *outDstId,
                     uint32_t    *outAgeMs);

/**
 * @brief Sync the live `dmrConfig` global from persistent ED fields and
 *        re-DmrConfigure the framing layer. Call from setup() after
 *        Storage loads ED, and from each DMR menu post-update callback.
 *
 * Mapping:
 *   ED.dmrEnabled    → dmrConfig.enabled
 *   ED.dmrTalkGroup  → dmrConfig.dstId      (group calls)
 *   ED.dmrSrcId      → dmrConfig.srcId
 *   ED.dmrTimeSlot   → dmrConfig.timeSlot   (1 or 2)
 *   ED.dmrColorCode  → dmrConfig.colorCode  (clamped 0..15)
 *   dmrConfig.callType is fixed at DmrCallGroup for now (private call
 *   dialing UI lands later).
 */
void SyncDmrConfigFromED(void);

/**
 * @brief Build a TX DMR voice burst around 3 AMBE+2 frames.
 * @param ambeFrames Three 7-byte AMBE+2 channel frames (49 bits each, 20 ms
 *                   each ⇒ 60 ms of voice carried by the resulting burst).
 *                   Bottom 3 bits of each frame (bits 46..48) are dropped to
 *                   fit the ETSI 72-bit-per-frame budget; the AMBE+2 codec
 *                   ranks those as the least perceptually significant.
 * @param outBurst   Destination buffer; must hold at least DMR_BURST_BYTES_MAX.
 * @param outLen     [out] Number of bytes written to outBurst (always 33).
 * @return true on success; false if the layer is unconfigured.
 *
 * FEC layering per AMBE frame (49 → 72 bits, before the bottom 3 are dropped):
 *   bits 0..11   (u_0) → Golay(23,12) → 23 bits
 *   bits 12..23  (u_1) → Golay(23,12) → 23 bits
 *   bits 24..34  (u_2) → Hamming(15,11) → 15 bits
 *   bits 35..45  (u_3) → unprotected   → 11 bits
 * The middle of the burst carries the BS-sourced voice SYNC pattern (this
 * commit handles voice frame F only; voice frames A..E with their EMB
 * field land later).
 *
 * Talk Group / src / dst addressing is NOT carried in voice bursts -- it is
 * carried in the LC header burst at the start of the call (see
 * DmrTxBuildLcHeaderBurst). Voice bursts only carry voice payload.
 */
bool DmrTxBuildVoiceFrame(const uint8_t ambeFrames[DMR_AMBE_FRAMES_PER_BURST][AMBE_DMR_BYTES_PER_FRAME],
                          uint8_t        outBurst[DMR_BURST_BYTES_MAX],
                          size_t        *outLen);

/**
 * @brief Parse a received DMR voice burst.
 * @param inBurst        Bytes recovered by the modem from one TDMA slot.
 * @param inLen          Number of bytes in inBurst (<= DMR_BURST_BYTES_MAX).
 * @param outAmbeFrames  [out] Three 7-byte AMBE+2 channel frames extracted
 *                       from the burst. Bits 46..48 of each frame are
 *                       always zero (dropped by TX, cannot be recovered).
 * @param outType        [out] Most-recently-seen call type (cached from
 *                       the last DmrRxParseLcHeaderBurst call).
 * @param outSrcId       [out] Most-recently-seen DMR source ID (cached).
 * @param outDstId       [out] Most-recently-seen destination (cached).
 * @return true if the FEC validates AND the cached destination passes the
 *         Talk Group whitelist; false otherwise (caller should drop the
 *         frame and not invoke AmbeDecodeFrame).
 *
 * If no LC header has yet been parsed, the cached addressing is zero and
 * DmrShouldDeliver() will not match (returns false).
 */
bool DmrRxParseFrame(const uint8_t      *inBurst,
                     size_t              inLen,
                     uint8_t             outAmbeFrames[DMR_AMBE_FRAMES_PER_BURST][AMBE_DMR_BYTES_PER_FRAME],
                     DmrCallType        *outType,
                     uint32_t           *outSrcId,
                     uint32_t           *outDstId);

/**
 * @brief Talk Group whitelist check.
 * @return true if dstId / type should be delivered to audio under the
 *         current dmrConfig.rxTgFilter, false to drop. Empty filter
 *         (rxTgCount == 0) means "deliver everything".
 */
bool DmrShouldDeliver(DmrCallType type, uint32_t dstId);

/**
 * @brief Slot-timing helper for the modem layer.
 * @param nowMs Current millis() reading (or any monotonic ms counter).
 * @return true if our configured time slot owns the 30 ms window
 *         starting at nowMs, false otherwise. The 60 ms TDMA frame is
 *         derived from the system clock; production builds will sync to
 *         a network-supplied epoch when one is available.
 */
bool DmrSlotIsOurs(uint32_t nowMs);

/**
 * @brief Microsecond-resolution slot-timing predicate.
 * @param nowUs Current micros() reading (or any monotonic µs counter).
 * @return true if our configured time slot owns the 30 ms window
 *         containing nowUs.
 *
 * Used by DSP.cpp::TransmitProcessing for per-sample TS gating at the
 * 30 ms slot boundary -- without it, a 256-sample (~10.7 ms) audio
 * block straddling a slot transition would be classified as a single
 * unit and we'd lose up to 10.7 ms of accuracy at the boundary.
 */
bool DmrSlotIsOursUs(uint32_t nowUs);

/* ------------------------------------------------------------------------
 * Voice LC header burst (TX/RX) -- carries Talk Group / src / dst across
 * BPTC(196,96) FEC. This is the burst that announces the call: a real DMR
 * receiver displays its src/dst from this. Voice payload bursts come
 * after the header and use a different FEC layering (Golay over AMBE
 * frames + EMB), implemented in a follow-up.
 * ---------------------------------------------------------------------- */

/**
 * @brief Build a complete 33-byte DMR Voice LC Header burst from dmrConfig.
 * @param outBurst  Destination buffer (must hold at least DMR_BURST_BYTES_MAX).
 * @param outLen    [out] Number of bytes written (always 33 on success).
 * @return true on success; false if the framing layer is unconfigured.
 *
 * Encoding chain:
 *   1. Pack a 72-bit Full LC: PF=0, FLCO derived from callType
 *      (0 = Group Voice Channel User, 3 = Unit-to-Unit Voice Channel
 *      User), FID=0, ServiceOptions=0, dst, src.
 *   2. Append a 24-bit CRC over the 72-bit LC, yielding 96 bits.
 *   3. BPTC(196,96): row-Hamming(15,11) and column-Hamming(13,9) over a
 *      13x15 matrix, plus a single reserved bit, producing 196 bits of
 *      protected payload.
 *   4. Build the 264-bit burst:
 *        bits   0..97   info first half  (BPTC bits   0..97)
 *        bits  98..107  slot type half 1 (zeros for now)
 *        bits 108..155  SYNC (BS sourced voice 0x755FD7DF75F7)
 *        bits 156..165  slot type half 2 (zeros for now)
 *        bits 166..263  info second half (BPTC bits 98..195)
 */
bool DmrTxBuildLcHeaderBurst(uint8_t outBurst[DMR_BURST_BYTES_MAX],
                             size_t  *outLen);

/* ------------------------------------------------------------------------
 * Embedded LC (EMB) -- carried 32 bits at a time in the middle of voice
 * frames A..D over a 360 ms superframe. Provides addressing (src / dst /
 * Talk Group) for receivers that tune in mid-call and miss the initial
 * Voice LC Header.
 * ---------------------------------------------------------------------- */

/**
 * @brief Reset the embedded-LC accumulator. Call when leaving DMR mode
 *        or when a call clearly ends (e.g. modem dropped lock). Idempotent.
 */
void DmrEmbReset(void);

/**
 * @brief Feed the next received voice burst into the embedded-LC pipeline.
 * @param burst   33-byte burst as recovered by DmrModemRxPopBurst().
 * @param outType  [out, optional] Group / Private call.
 * @param outSrcId [out, optional] 24-bit DMR source ID.
 * @param outDstId [out, optional] 24-bit destination (TG number for
 *                 Group calls, radio ID for Private calls).
 * @return true iff this burst completed an embedded LC sequence (last
 *         fragment + valid CRC after BPTC(128,77) decode), in which case
 *         the cached addressing has been updated and the out parameters
 *         hold the recovered values. false on every other (non-final)
 *         burst, including: SYNC-bearing voice frame F bursts,
 *         partial-fragment intermediate frames, and FEC failures.
 *
 * The function is stateful -- it tracks the LCSS field across calls
 * to assemble the BPTC(128,77) codeword from voice frames A..D and
 * runs the decode when the last fragment arrives.
 */
bool DmrRxParseEmbeddedLc(const uint8_t *burst,
                          DmrCallType   *outType,
                          uint32_t      *outSrcId,
                          uint32_t      *outDstId);

/**
 * @brief Parse a received DMR Voice LC Header burst.
 * @param inBurst   33-byte burst as recovered by DmrModemRxPopBurst().
 * @param inLen     Number of valid bytes (>= 33).
 * @param outType   [out] Group vs Private call.
 * @param outSrcId  [out] 24-bit DMR source ID.
 * @param outDstId  [out] 24-bit destination (Talk Group number or unit ID).
 * @return true if BPTC + CRC validate AND DmrShouldDeliver() accepts the
 *         destination; false otherwise.
 */
bool DmrRxParseLcHeaderBurst(const uint8_t *inBurst,
                             size_t         inLen,
                             DmrCallType   *outType,
                             uint32_t      *outSrcId,
                             uint32_t      *outDstId);

#endif /* DMR_FRAMING_H */
