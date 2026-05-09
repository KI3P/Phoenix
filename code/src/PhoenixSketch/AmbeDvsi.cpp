/**
 * @file AmbeDvsi.cpp
 * @brief DVSI ThumbDV (AMBE3000R) USB codec driver -- DMR voice encode/decode.
 *
 * Codec stage only: DMR rides on a 4FSK baseband inside an NFM (or FM_WIDE)
 * RF channel. This file does not synthesize 4FSK; it converts between PCM
 * and AMBE+2 49-bit/20 ms frames so the future 4FSK/framing layer has
 * something to feed and consume. See AmbeDvsi.h and SDT.h::DigitalCoding.
 *
 * Protocol summary (AMBE-3000R Vocoder Chip, DVSI; also ThumbDV reference):
 *
 *   Every host<->vocoder packet is framed as:
 *     0x61 | LEN_HI | LEN_LO | TYPE | PAYLOAD ...
 *   LEN is big-endian and counts TYPE + PAYLOAD bytes (i.e. everything after
 *   the length itself). TYPE: 0x00 control, 0x01 channel, 0x02 speech.
 *
 *   Control packets (TYPE 0x00) carry one or more field tuples:
 *     0x30 PKT_PRODID                              -- query: 0 args; reply: ASCII string (nul terminated)
 *     0x31 PKT_VERSTRING                           -- similar
 *     0x09 PKT_RATET <rate_index>                  -- preset rate from internal table
 *     0x33 PKT_INIT  <flags>                       -- soft init (0x03 = encoder+decoder)
 *
 *   For DMR we use rate index 33 (0x21): AMBE+2, 49 bits / 20 ms / 7 bytes
 *   channel data, 160 PCM samples / 20 ms.
 *
 *   Speech packets (TYPE 0x02): one CHAND-style field
 *     0x00 SPCHD <num_samples_byte> <samples BE int16 ...>
 *
 *   Channel packets (TYPE 0x01): one CHAND field
 *     0x01 CHAND <num_bits_byte> <ceil(num_bits/8) bytes>
 *
 * The handshake we run on enumeration is:
 *   1. PKT_PRODID query   -> store reply for boot log
 *   2. PKT_RATET 33       -> select DMR
 *   3. PKT_INIT 0x03      -> reset encoder + decoder state
 * After step 3, the chip is ready: we send speech packets to encode and
 * channel packets to decode, and the chip replies symmetrically.
 *
 * USB transport: PJRC USBHost_t36 USBSerial_BigBuffer (4 KB rings) on the
 * Teensy 4.1 USBHOST port @ 460 800 8N1. Sharing the usbHost singleton with
 * FrontPanel_USBHost.cpp.
 */

#include "SDT.h"
#include "AmbeDvsi.h"

#ifdef DMR_THUMBDV_ENABLED

#include <USBHost_t36.h>

// usbHost is owned by FrontPanel_USBHost.cpp (we are gated by the shared
// USB_HOST_STACK_ENABLED flag). Bind to it via extern.
extern USBHost usbHost;

// 4 KB buffers are plenty: one speech packet is 323 bytes (160 samples * 2 +
// 5-byte header + 1-byte SPCHD count). We sustain 50 packets/sec each way.
static USBSerial_BigBuffer userial(usbHost, 1);

// ----------------------------------------------------------------------------
// AMBE3000 packet protocol constants
// ----------------------------------------------------------------------------
static constexpr uint8_t  AMBE_START_BYTE     = 0x61;
static constexpr uint8_t  AMBE_TYPE_CONTROL   = 0x00;
static constexpr uint8_t  AMBE_TYPE_CHANNEL   = 0x01;
static constexpr uint8_t  AMBE_TYPE_SPEECH    = 0x02;
static constexpr uint8_t  AMBE_FID_SPCHD      = 0x00; // speech-data field (in TYPE 0x02 packets)
static constexpr uint8_t  AMBE_FID_CHAND      = 0x01; // channel-data field (in TYPE 0x01 packets)
static constexpr uint8_t  AMBE_FID_RATET      = 0x09;
static constexpr uint8_t  AMBE_FID_PRODID     = 0x30;
static constexpr uint8_t  AMBE_FID_VERSTR     = 0x31;
static constexpr uint8_t  AMBE_FID_INIT       = 0x33;
static constexpr uint8_t  AMBE_RATET_DMR      = 33;   // AMBE+2 49-bit / 20 ms (DMR, MotoTRBO)

// ----------------------------------------------------------------------------
// Lock-free single-producer / single-consumer ring buffers.
// Both ends run on the main loop -- no ISR concurrency -- so simple indexed
// rings are sufficient. Sized one above the working set so head==tail is
// always "empty".
// ----------------------------------------------------------------------------
template <typename T, size_t N>
struct Ring {
    T   data[N];
    size_t head = 0;  // next write
    size_t tail = 0;  // next read
    bool empty() const { return head == tail; }
    bool full()  const { return ((head + 1) % N) == tail; }
    bool push(const T &v) {
        if (full()) return false;
        data[head] = v;
        head = (head + 1) % N;
        return true;
    }
    bool pop(T &v) {
        if (empty()) return false;
        v = data[tail];
        tail = (tail + 1) % N;
        return true;
    }
};

struct PcmFrame    { int16_t s[AMBE_PCM_SAMPLES_PER_FRAME]; };
struct ChanFrame   { uint8_t b[AMBE_DMR_BYTES_PER_FRAME]; };

// 8 frames = 160 ms of buffering each direction. Generous but cheap on
// Teensy 4.1: 8 * 320 + 8 * 7 ≈ 2.6 KB total.
static Ring<PcmFrame,  8>  pcmInQueue;    // PCM submitted to encode
static Ring<PcmFrame,  8>  pcmOutQueue;   // PCM produced by decode
static Ring<ChanFrame, 8>  chanInQueue;   // channel frames submitted to decode
static Ring<ChanFrame, 8>  chanOutQueue;  // channel frames produced by encode

// ----------------------------------------------------------------------------
// Handshake / connection state machine
// ----------------------------------------------------------------------------
static AmbeState  state = AMBE_STATE_DISCONNECTED;
static uint32_t   stateEnteredMs = 0;
static char       prodId[64] = {0};
static bool       prodIdReceived = false;
static bool       rateAcked      = false;
static bool       initAcked      = false;

// Inbound packet parser
static constexpr size_t AMBE_RX_MAX = 1024;
static uint8_t    rxBuf[AMBE_RX_MAX];
static size_t     rxLen = 0;

static void enterState(AmbeState s) {
    state = s;
    stateEnteredMs = millis();
}

static bool isUserialReady() {
    // USBSerial::operator bool() in USBHost_t36 returns true when an
    // appropriate device is enumerated and the CDC channel is open.
    return (bool)userial;
}

// ----------------------------------------------------------------------------
// Low-level packet TX
// ----------------------------------------------------------------------------
static bool writeAll(const uint8_t *p, size_t n) {
    // USBSerial.write returns the byte count written; for the BigBuffer
    // variant a single 1-2 KB packet is comfortably below the buffer size,
    // but we still loop in case we hit a backpressure split.
    size_t off = 0;
    uint32_t startMs = millis();
    while (off < n) {
        if (!isUserialReady()) return false;
        size_t w = userial.write(p + off, n - off);
        if (w == 0) {
            // Yield to let usbHost.Task() drain the host stack; bail after
            // 50 ms so we never block the audio loop.
            if (millis() - startMs > 50) return false;
            yield();
            continue;
        }
        off += w;
    }
    return true;
}

static bool sendPacket(uint8_t type, const uint8_t *body, uint16_t bodyLen) {
    // Frame:  0x61 | LEN_HI | LEN_LO | TYPE | body
    //   LEN counts TYPE + body bytes.
    uint16_t lenField = (uint16_t)(bodyLen + 1);
    uint8_t  hdr[4]   = { AMBE_START_BYTE,
                          (uint8_t)(lenField >> 8),
                          (uint8_t)(lenField & 0xFF),
                          type };
    if (!writeAll(hdr, sizeof(hdr))) return false;
    if (bodyLen && !writeAll(body, bodyLen)) return false;
    return true;
}

static bool sendProdIdQuery() {
    uint8_t body[1] = { AMBE_FID_PRODID };
    return sendPacket(AMBE_TYPE_CONTROL, body, sizeof(body));
}
static bool sendRatetDmr() {
    uint8_t body[2] = { AMBE_FID_RATET, AMBE_RATET_DMR };
    return sendPacket(AMBE_TYPE_CONTROL, body, sizeof(body));
}
static bool sendInitBoth() {
    uint8_t body[2] = { AMBE_FID_INIT, 0x03 }; // 0x01 enc | 0x02 dec
    return sendPacket(AMBE_TYPE_CONTROL, body, sizeof(body));
}

static bool sendSpeechFrame(const int16_t pcm[AMBE_PCM_SAMPLES_PER_FRAME]) {
    // body: 0x00 SPCHD, count, then 160 BE-int16 samples.
    uint8_t body[2 + AMBE_PCM_SAMPLES_PER_FRAME * 2];
    body[0] = AMBE_FID_SPCHD;
    body[1] = (uint8_t)AMBE_PCM_SAMPLES_PER_FRAME;
    for (int i = 0; i < AMBE_PCM_SAMPLES_PER_FRAME; ++i) {
        uint16_t s = (uint16_t)pcm[i];
        body[2 + 2*i + 0] = (uint8_t)(s >> 8);
        body[2 + 2*i + 1] = (uint8_t)(s & 0xFF);
    }
    return sendPacket(AMBE_TYPE_SPEECH, body, sizeof(body));
}

static bool sendChannelFrame(const uint8_t frame[AMBE_DMR_BYTES_PER_FRAME]) {
    // body: 0x01 CHAND, num_bits, packed bits.
    uint8_t body[2 + AMBE_DMR_BYTES_PER_FRAME];
    body[0] = AMBE_FID_CHAND;
    body[1] = (uint8_t)AMBE_DMR_BITS_PER_FRAME; // 49
    for (int i = 0; i < AMBE_DMR_BYTES_PER_FRAME; ++i) body[2+i] = frame[i];
    return sendPacket(AMBE_TYPE_CHANNEL, body, sizeof(body));
}

// ----------------------------------------------------------------------------
// Inbound packet handling
// ----------------------------------------------------------------------------
static void handleControlPacket(const uint8_t *body, uint16_t len) {
    // Walk the field list. We only care about a few replies for now.
    uint16_t i = 0;
    while (i < len) {
        uint8_t fid = body[i++];
        switch (fid) {
        case AMBE_FID_PRODID: {
            // Reply payload is a nul-terminated ASCII string.
            size_t k = 0;
            while (i < len && body[i] != 0 && k < sizeof(prodId) - 1) {
                prodId[k++] = (char)body[i++];
            }
            prodId[k] = '\0';
            if (i < len && body[i] == 0) i++; // consume nul
            prodIdReceived = true;
            Serial.print("ThumbDV: ");
            Serial.println(prodId);
            break;
        }
        case AMBE_FID_RATET:
            // ACK byte (0x00 = ok). Skip it.
            if (i < len) i++;
            rateAcked = true;
            break;
        case AMBE_FID_INIT:
            if (i < len) i++;
            initAcked = true;
            break;
        case AMBE_FID_VERSTR:
            // Skip nul-terminated string; we don't use it but must consume it.
            while (i < len && body[i] != 0) i++;
            if (i < len) i++;
            break;
        default:
            // Unknown field. The DVSI framing doesn't carry per-field
            // length, so we cannot safely skip; bail out and let the caller
            // resync on the next start byte.
            return;
        }
    }
}

static void handleSpeechPacket(const uint8_t *body, uint16_t len) {
    // Expect: 0x00 SPCHD, count, then count BE-int16 samples.
    if (len < 2 || body[0] != AMBE_FID_SPCHD) return;
    uint8_t count = body[1];
    if (count != AMBE_PCM_SAMPLES_PER_FRAME) return;
    if (len < 2u + 2u * count) return;
    PcmFrame f;
    for (int i = 0; i < count; ++i) {
        uint16_t hi = body[2 + 2*i + 0];
        uint16_t lo = body[2 + 2*i + 1];
        f.s[i] = (int16_t)((hi << 8) | lo);
    }
    pcmOutQueue.push(f);  // dropped silently if full -- caller is too slow
}

static void handleChannelPacket(const uint8_t *body, uint16_t len) {
    // Expect: 0x01 CHAND, num_bits, ceil(num_bits/8) bytes.
    if (len < 2 || body[0] != AMBE_FID_CHAND) return;
    uint8_t bits = body[1];
    if (bits != AMBE_DMR_BITS_PER_FRAME) return;
    if (len < 2u + AMBE_DMR_BYTES_PER_FRAME) return;
    ChanFrame f;
    for (int i = 0; i < AMBE_DMR_BYTES_PER_FRAME; ++i) f.b[i] = body[2+i];
    chanOutQueue.push(f);
}

static void dispatchPacket(uint8_t type, const uint8_t *body, uint16_t len) {
    switch (type) {
    case AMBE_TYPE_CONTROL: handleControlPacket(body, len); break;
    case AMBE_TYPE_SPEECH:  handleSpeechPacket (body, len); break;
    case AMBE_TYPE_CHANNEL: handleChannelPacket(body, len); break;
    default: /* unknown -- drop */ break;
    }
}

// Parse as much as we can from rxBuf and consume it. Frame layout:
//   0x61 LEN_HI LEN_LO TYPE PAYLOAD...
// LEN counts TYPE + PAYLOAD.
static void drainRx() {
    // Resync to a start byte.
    size_t i = 0;
    while (i < rxLen) {
        if (rxBuf[i] != AMBE_START_BYTE) { i++; continue; }
        if (rxLen - i < 4) break;                          // not enough header yet
        uint16_t lenField = ((uint16_t)rxBuf[i+1] << 8) | rxBuf[i+2];
        if (lenField == 0 || lenField > AMBE_RX_MAX - 3) {
            // Bogus; skip past start byte.
            i++;
            continue;
        }
        size_t total = 3 + lenField;                       // 0x61 + LEN_HI + LEN_LO + (lenField bytes)
        if (rxLen - i < total) break;                      // partial; wait for more
        uint8_t  type = rxBuf[i+3];
        uint16_t bodyLen = lenField - 1;                   // strip type
        const uint8_t *body = &rxBuf[i+4];
        dispatchPacket(type, body, bodyLen);
        i += total;
    }
    if (i > 0) {
        if (i < rxLen) memmove(rxBuf, rxBuf + i, rxLen - i);
        rxLen -= i;
    }
}

// ----------------------------------------------------------------------------
// Driver entry points
// ----------------------------------------------------------------------------
void InitializeAmbeDvsi(void) {
    rxLen = 0;
    prodIdReceived = rateAcked = initAcked = false;
    prodId[0] = '\0';
    enterState(AMBE_STATE_DISCONNECTED);
    Serial.println("...AmbeDvsi (DMR codec) ready, waiting for ThumbDV");
}

void TickAmbeDvsi(void) {
    // 1. Track USB enumeration.
    if (!isUserialReady()) {
        if (state != AMBE_STATE_DISCONNECTED) {
            Serial.println("ThumbDV: disconnected");
            // Reset everything so the next plug-in re-runs the handshake.
            rxLen = 0;
            prodIdReceived = rateAcked = initAcked = false;
            prodId[0] = '\0';
            enterState(AMBE_STATE_DISCONNECTED);
        }
        return;
    }
    if (state == AMBE_STATE_DISCONNECTED) {
        userial.begin(AMBE_USB_BAUD);
        Serial.println("ThumbDV: enumerated, starting handshake");
        enterState(AMBE_STATE_HANDSHAKING);
        sendProdIdQuery();
    }

    // 2. Drain the USB RX FIFO into our buffer.
    while (userial.available() > 0) {
        if (rxLen >= AMBE_RX_MAX) {
            // Shouldn't happen with well-formed traffic; drop.
            rxLen = 0;
            enterState(AMBE_STATE_ERROR);
            break;
        }
        rxBuf[rxLen++] = (uint8_t)userial.read();
    }
    drainRx();

    // 3. Advance the handshake.
    if (state == AMBE_STATE_HANDSHAKING) {
        if (prodIdReceived && !rateAcked) {
            sendRatetDmr();
        } else if (rateAcked && !initAcked) {
            sendInitBoth();
        } else if (initAcked) {
            enterState(AMBE_STATE_READY);
            Serial.println("ThumbDV: ready (DMR/AMBE+2 rate 33)");
        } else if (millis() - stateEnteredMs > 2000) {
            // Handshake stuck; retry.
            Serial.println("ThumbDV: handshake timeout, retrying");
            enterState(AMBE_STATE_HANDSHAKING);
            sendProdIdQuery();
        }
    } else if (state == AMBE_STATE_ERROR) {
        if (millis() - stateEnteredMs > 250) {
            // Give the parser breathing room, then try the handshake again.
            prodIdReceived = rateAcked = initAcked = false;
            enterState(AMBE_STATE_HANDSHAKING);
            sendProdIdQuery();
        }
    }

    // 4. While ready, push pending work down to the chip.
    if (state == AMBE_STATE_READY) {
        // One frame each per tick keeps loop time bounded; the codec
        // sustains 50 frames/s comfortably either direction.
        PcmFrame  pf;
        if (pcmInQueue.pop(pf)) {
            if (!sendSpeechFrame(pf.s)) enterState(AMBE_STATE_ERROR);
        }
        ChanFrame cf;
        if (chanInQueue.pop(cf)) {
            if (!sendChannelFrame(cf.b)) enterState(AMBE_STATE_ERROR);
        }
    }
}

bool AmbeEncodePcm20ms(const int16_t pcm[AMBE_PCM_SAMPLES_PER_FRAME]) {
    if (state != AMBE_STATE_READY) return false;
    PcmFrame f;
    memcpy(f.s, pcm, sizeof(f.s));
    return pcmInQueue.push(f);
}

bool AmbeDecodeFrame(const uint8_t frame[AMBE_DMR_BYTES_PER_FRAME]) {
    if (state != AMBE_STATE_READY) return false;
    ChanFrame f;
    memcpy(f.b, frame, sizeof(f.b));
    return chanInQueue.push(f);
}

bool AmbePopChannel(uint8_t out[AMBE_DMR_BYTES_PER_FRAME]) {
    ChanFrame f;
    if (!chanOutQueue.pop(f)) return false;
    memcpy(out, f.b, sizeof(f.b));
    return true;
}

bool AmbePopPcm20ms(int16_t out[AMBE_PCM_SAMPLES_PER_FRAME]) {
    PcmFrame f;
    if (!pcmOutQueue.pop(f)) return false;
    memcpy(out, f.s, sizeof(f.s));
    return true;
}

bool AmbeIsReady(void)              { return state == AMBE_STATE_READY; }
AmbeState AmbeGetState(void)        { return state; }
const char *AmbeGetProductId(void)  { return prodId; }

#else  /* DMR_THUMBDV_ENABLED */

// Stubs so callers compile unchanged when DMR support is left out.
void InitializeAmbeDvsi(void)                                                  {}
void TickAmbeDvsi(void)                                                        {}
bool AmbeEncodePcm20ms(const int16_t [AMBE_PCM_SAMPLES_PER_FRAME])             { return false; }
bool AmbeDecodeFrame(const uint8_t [AMBE_DMR_BYTES_PER_FRAME])                 { return false; }
bool AmbePopChannel(uint8_t [AMBE_DMR_BYTES_PER_FRAME])                        { return false; }
bool AmbePopPcm20ms(int16_t [AMBE_PCM_SAMPLES_PER_FRAME])                      { return false; }
bool AmbeIsReady(void)                                                         { return false; }
AmbeState AmbeGetState(void)                                                   { return AMBE_STATE_DISABLED; }
const char *AmbeGetProductId(void)                                             { return ""; }

#endif /* DMR_THUMBDV_ENABLED */
