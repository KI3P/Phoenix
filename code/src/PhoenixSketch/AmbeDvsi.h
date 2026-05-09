#ifndef AMBE_DVSI_H
#define AMBE_DVSI_H
#include "SDT.h"

/**
 * @file AmbeDvsi.h
 * @brief DMR voice codec via a DVSI ThumbDV (AMBE3000R) USB dongle.
 *
 * Important framing: this is the *codec* stage only. DMR's over-the-air
 * waveform is 4FSK (4800 sym/s, ~9600 bps) carried inside a narrow FM
 * channel (12.5 kHz NFM, or FM_WIDE for relaxed setups). DMR is therefore
 * not a peer of NFM/FM_WIDE in ModulationType -- it is a digital coding
 * applied on top of one of them (see DigitalCoding in SDT.h). AmbeDvsi
 * sits between the audio path and the 4FSK/framing/FEC layer:
 *
 *   TX path:
 *     mic PCM (8 kHz mono int16)
 *       └─► AmbeEncodePcm20ms() ─► [AMBE3000 encode] ─► AmbePopChannel()
 *                                                        (49-bit AMBE+2 frame)
 *           ─► DmrTxBuildVoiceFrame() (CC + srcId + dstId/TG + FEC)
 *           ─► DmrModem 4FSK + TS1/TS2 burst gating
 *           ─► NFM/FM_WIDE TX
 *
 *   RX path:
 *     NFM/FM_WIDE RX ─► DmrModem 4FSK demod ─► slot bytes
 *           ─► DmrRxParseFrame() (CC check, FEC, TG whitelist)
 *                                                        (49-bit AMBE+2 frame)
 *       └─► AmbeDecodeFrame() ─► [AMBE3000 decode] ─► AmbePopPcm20ms()
 *           ─► speaker PCM (8 kHz mono int16)
 *
 * Talk Group / Color Code / Source / Dest ID addressing lives in
 * DmrFraming.{h,cpp}. TS1/TS2 burst gating lives in DmrModem.{h,cpp}
 * (TBD). Until those land, AmbeDvsi is exercisable on its own (mic PCM
 * in → AMBE bytes out, AMBE bytes in → speaker PCM out) for codec
 * testing.
 *
 * Gated by DMR_THUMBDV_ENABLED in Config.h. When undefined, every entry
 * point compiles to a no-op stub that returns false / 0 so callers compile
 * unchanged.
 *
 * Vocoder configuration: AMBE+2, rate index 33 (DMR / MotoTRBO),
 * 49 bits per 20 ms frame ⇒ 7 channel bytes per frame, 160 PCM samples per
 * frame at 8 kHz.
 *
 * Phoenix integration:
 *   InitializeAmbeDvsi()  -- called from InitializeUSBHost() in setup()
 *   TickAmbeDvsi()        -- called from TickUSBHost() each main-loop pass
 *
 * Thread safety: the FIFOs are sized so that one producer (loop()) and one
 * consumer (loop()) can use them without locks; both ends are called from
 * the main loop, never from ISR context.
 */

/* AMBE+2 DMR frame geometry (AMBE_DMR_BITS_PER_FRAME / _BYTES_PER_FRAME)
 * lives in SDT.h so the framing layer can use it without a header-level
 * dependency on this codec module. */

/** PCM frame: 8 kHz mono, 16-bit linear, 20 ms ⇒ 160 samples. */
#define AMBE_PCM_SAMPLES_PER_FRAME   160
#define AMBE_PCM_SAMPLE_RATE_HZ      8000

/** AMBE3000 USB CDC-ACM line setting on the ThumbDV. */
#define AMBE_USB_BAUD                460800

/** Codec readiness states surfaced via AmbeGetState(). */
typedef enum {
    AMBE_STATE_DISABLED = 0,    /**< Module disabled at compile time. */
    AMBE_STATE_DISCONNECTED,    /**< No ThumbDV detected on USBHOST port. */
    AMBE_STATE_HANDSHAKING,     /**< Enumerated; running PRODID/RATEP handshake. */
    AMBE_STATE_READY,           /**< Handshake complete; encode/decode usable. */
    AMBE_STATE_ERROR            /**< Protocol error; module is reconnecting. */
} AmbeState;

/**
 * @brief Initialize the ThumbDV codec driver.
 * @note Idempotent. Called from InitializeUSBHost() after usbHost.begin().
 *       Does not block waiting for enumeration -- AmbeGetState() reports
 *       progress and TickAmbeDvsi() advances the handshake when the device
 *       arrives.
 */
void InitializeAmbeDvsi(void);

/**
 * @brief Service the ThumbDV codec driver.
 * @note Non-blocking; designed for ~10 ms main-loop budgets. Drains any
 *       inbound bytes from USBSerial, advances the handshake state machine,
 *       parses framed AMBE3000 packets, and pushes results into the
 *       PCM/channel FIFOs.
 */
void TickAmbeDvsi(void);

/**
 * @brief Submit one 20 ms frame of PCM to be encoded into an AMBE+2 frame.
 * @param pcm 160 samples of int16 PCM at 8 kHz, mono.
 * @return true on success; false if the codec is not ready or the inbound
 *         queue is full (caller should retry next loop pass).
 */
bool AmbeEncodePcm20ms(const int16_t pcm[AMBE_PCM_SAMPLES_PER_FRAME]);

/**
 * @brief Submit one AMBE+2 channel frame to be decoded into PCM.
 * @param frame 7 packed bytes carrying 49 bits of AMBE+2 channel data.
 * @return true on success; false if the codec is not ready or the inbound
 *         queue is full (caller should retry next loop pass).
 */
bool AmbeDecodeFrame(const uint8_t frame[AMBE_DMR_BYTES_PER_FRAME]);

/**
 * @brief Pop the next ready encoded channel frame.
 * @param out destination for 7 packed bytes (49 bits AMBE+2).
 * @return true if a frame was returned; false if no encoded frame is ready.
 */
bool AmbePopChannel(uint8_t out[AMBE_DMR_BYTES_PER_FRAME]);

/**
 * @brief Pop the next ready decoded PCM frame.
 * @param out destination for 160 samples of int16 PCM at 8 kHz.
 * @return true if a frame was returned; false if no decoded frame is ready.
 */
bool AmbePopPcm20ms(int16_t out[AMBE_PCM_SAMPLES_PER_FRAME]);

/**
 * @brief Convenience: is the codec ready for encode/decode calls?
 */
bool AmbeIsReady(void);

/**
 * @brief Current state of the codec driver.
 */
AmbeState AmbeGetState(void);

/**
 * @brief Last reported AMBE3000 product/version string from PKT_PRODID,
 *        nul-terminated; empty until handshake completes. Useful for
 *        logging at boot.
 */
const char *AmbeGetProductId(void);

#endif /* AMBE_DVSI_H */
