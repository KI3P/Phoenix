#include "SDT.h"
#include "DSP.h"                  // for ApproxAtan2
#include "DSP_PSK31.h"
#include "MainBoard_Display.h"    // for the Pane struct used to flag PaneSpectrum stale

/*
 * PSK31 primitives (data tables + clean DBPSK demod).
 *
 * Ported from T41_SDR/psk31.cpp -- the legitimately reusable subset:
 *   - Standard PSK31 varicode lookup tables
 *   - Varicode encoder/decoder
 *   - DBPSK symbol decoder (adapted to Phoenix's separate I/Q DataBlock)
 *
 * Excluded: T41's experimental phase-shift detectors, commented-out timing
 * recovery, WAV-file test path, and Serial.print debug spam.
 *
 * Reference: G3PLX, "PSK31: A new radio-teletype mode" (1998).
 * Original source: libcsdr (https://github.com/ha7ilm/csdr).
 */

/* =================================================================
 * Varicode tables
 *
 * Each entry is { codeword, bitcount, ascii }. Codewords are read MSB-first
 * and are guaranteed to never contain two consecutive zero bits, so a
 * "00" sequence is the inter-character delimiter.
 *
 * Table is the standard G3PLX varicode (128 ASCII characters). Copied
 * verbatim from T41_SDR/psk31.cpp.
 * =================================================================
 */

typedef struct {
    unsigned long long code;
    int                bitcount;
    unsigned char      ascii;
} psk31_varicode_item_t;

static const psk31_varicode_item_t psk31_varicode_items[] = {
    { 0b1010101011, 10, 0x00 }, /* NUL */    { 0b1011011011, 10, 0x01 }, /* SOH */
    { 0b1011101101, 10, 0x02 }, /* STX */    { 0b1101110111, 10, 0x03 }, /* ETX */
    { 0b1011101011, 10, 0x04 }, /* EOT */    { 0b1101011111, 10, 0x05 }, /* ENQ */
    { 0b1011101111, 10, 0x06 }, /* ACK */    { 0b1011111101, 10, 0x07 }, /* BEL */
    { 0b1011111111, 10, 0x08 }, /* BS  */    { 0b11101111,    8, 0x09 }, /* TAB */
    { 0b11101,       5, 0x0a }, /* LF  */    { 0b1101101111, 10, 0x0b }, /* VT  */
    { 0b1011011101, 10, 0x0c }, /* FF  */    { 0b11111,       5, 0x0d }, /* CR  */
    { 0b1101110101, 10, 0x0e }, /* SO  */    { 0b1110101011, 10, 0x0f }, /* SI  */
    { 0b1011110111, 10, 0x10 }, /* DLE */    { 0b1011110101, 10, 0x11 }, /* DC1 */
    { 0b1110101101, 10, 0x12 }, /* DC2 */    { 0b1110101111, 10, 0x13 }, /* DC3 */
    { 0b1101011011, 10, 0x14 }, /* DC4 */    { 0b1101101011, 10, 0x15 }, /* NAK */
    { 0b1101101101, 10, 0x16 }, /* SYN */    { 0b1101010111, 10, 0x17 }, /* ETB */
    { 0b1101111011, 10, 0x18 }, /* CAN */    { 0b1101111101, 10, 0x19 }, /* EM  */
    { 0b1110110111, 10, 0x1a }, /* SUB */    { 0b1101010101, 10, 0x1b }, /* ESC */
    { 0b1101011101, 10, 0x1c }, /* FS  */    { 0b1110111011, 10, 0x1d }, /* GS  */
    { 0b1011111011, 10, 0x1e }, /* RS  */    { 0b1101111111, 10, 0x1f }, /* US  */
    { 0b1,           1, 0x20 }, /* sp  */    { 0b111111111,   9, 0x21 }, /* !   */
    { 0b101011111,   9, 0x22 }, /* "   */    { 0b111110101,   9, 0x23 }, /* #   */
    { 0b111011011,   9, 0x24 }, /* $   */    { 0b1011010101, 10, 0x25 }, /* %   */
    { 0b1010111011, 10, 0x26 }, /* &   */    { 0b101111111,   9, 0x27 }, /* '   */
    { 0b11111011,    8, 0x28 }, /* (   */    { 0b11110111,    8, 0x29 }, /* )   */
    { 0b101101111,   9, 0x2a }, /* *   */    { 0b111011111,   9, 0x2b }, /* +   */
    { 0b1110101,     7, 0x2c }, /* ,   */    { 0b110101,      6, 0x2d }, /* -   */
    { 0b1010111,     7, 0x2e }, /* .   */    { 0b110101111,   9, 0x2f }, /* /   */
    { 0b10110111,    8, 0x30 }, /* 0   */    { 0b10111101,    8, 0x31 }, /* 1   */
    { 0b11101101,    8, 0x32 }, /* 2   */    { 0b11111111,    8, 0x33 }, /* 3   */
    { 0b101110111,   9, 0x34 }, /* 4   */    { 0b101011011,   9, 0x35 }, /* 5   */
    { 0b101101011,   9, 0x36 }, /* 6   */    { 0b110101101,   9, 0x37 }, /* 7   */
    { 0b110101011,   9, 0x38 }, /* 8   */    { 0b110110111,   9, 0x39 }, /* 9   */
    { 0b11110101,    8, 0x3a }, /* :   */    { 0b110111101,   9, 0x3b }, /* ;   */
    { 0b111101101,   9, 0x3c }, /* <   */    { 0b1010101,     7, 0x3d }, /* =   */
    { 0b111010111,   9, 0x3e }, /* >   */    { 0b1010101111, 10, 0x3f }, /* ?   */
    { 0b1010111101, 10, 0x40 }, /* @   */    { 0b1111101,     7, 0x41 }, /* A   */
    { 0b11101011,    8, 0x42 }, /* B   */    { 0b10101101,    8, 0x43 }, /* C   */
    { 0b10110101,    8, 0x44 }, /* D   */    { 0b1110111,     7, 0x45 }, /* E   */
    { 0b11011011,    8, 0x46 }, /* F   */    { 0b11111101,    8, 0x47 }, /* G   */
    { 0b101010101,   9, 0x48 }, /* H   */    { 0b1111111,     7, 0x49 }, /* I   */
    { 0b111111101,   9, 0x4a }, /* J   */    { 0b101111101,   9, 0x4b }, /* K   */
    { 0b11010111,    8, 0x4c }, /* L   */    { 0b10111011,    8, 0x4d }, /* M   */
    { 0b11011101,    8, 0x4e }, /* N   */    { 0b10101011,    8, 0x4f }, /* O   */
    { 0b11010101,    8, 0x50 }, /* P   */    { 0b111011101,   9, 0x51 }, /* Q   */
    { 0b10101111,    8, 0x52 }, /* R   */    { 0b1101111,     7, 0x53 }, /* S   */
    { 0b1101101,     7, 0x54 }, /* T   */    { 0b101010111,   9, 0x55 }, /* U   */
    { 0b110110101,   9, 0x56 }, /* V   */    { 0b101011101,   9, 0x57 }, /* W   */
    { 0b101110101,   9, 0x58 }, /* X   */    { 0b101111011,   9, 0x59 }, /* Y   */
    { 0b1010101101, 10, 0x5a }, /* Z   */    { 0b111110111,   9, 0x5b }, /* [   */
    { 0b111101111,   9, 0x5c }, /* \   */    { 0b111111011,   9, 0x5d }, /* ]   */
    { 0b1010111111, 10, 0x5e }, /* ^   */    { 0b101101101,   9, 0x5f }, /* _   */
    { 0b1011011111, 10, 0x60 }, /* `   */    { 0b1011,        4, 0x61 }, /* a   */
    { 0b1011111,     7, 0x62 }, /* b   */    { 0b101111,      6, 0x63 }, /* c   */
    { 0b101101,      6, 0x64 }, /* d   */    { 0b11,          2, 0x65 }, /* e   */
    { 0b111101,      6, 0x66 }, /* f   */    { 0b1011011,     7, 0x67 }, /* g   */
    { 0b101011,      6, 0x68 }, /* h   */    { 0b1101,        4, 0x69 }, /* i   */
    { 0b111101011,   9, 0x6a }, /* j   */    { 0b10111111,    8, 0x6b }, /* k   */
    { 0b11011,       5, 0x6c }, /* l   */    { 0b111011,      6, 0x6d }, /* m   */
    { 0b1111,        4, 0x6e }, /* n   */    { 0b111,         3, 0x6f }, /* o   */
    { 0b111111,      6, 0x70 }, /* p   */    { 0b110111111,   9, 0x71 }, /* q   */
    { 0b10101,       5, 0x72 }, /* r   */    { 0b10111,       5, 0x73 }, /* s   */
    { 0b101,         3, 0x74 }, /* t   */    { 0b110111,      6, 0x75 }, /* u   */
    { 0b1111011,     7, 0x76 }, /* v   */    { 0b1101011,     7, 0x77 }, /* w   */
    { 0b11011111,    8, 0x78 }, /* x   */    { 0b1011101,     7, 0x79 }, /* y   */
    { 0b111010101,   9, 0x7a }, /* z   */    { 0b1010110111, 10, 0x7b }, /* {   */
    { 0b110111011,   9, 0x7c }, /* |   */    { 0b1010110101, 10, 0x7d }, /* }   */
    { 0b1011010111, 10, 0x7e }, /* ~   */    { 0b1110110101, 10, 0x7f }, /* DEL */
};
static const int N_PSK31_VARICODE_ITEMS =
    (int)(sizeof(psk31_varicode_items) / sizeof(psk31_varicode_items[0]));

/*
 * Bitmask helper: psk31_varicode_masklen_helper[n] = (1ULL << n) - 1, for
 * n in [0, 63]. Used to mask the low n bits of the decoder's shift register
 * when comparing against a candidate codeword.
 *
 * This is precomputed (rather than (1ULL<<n)-1 at runtime) to match T41's
 * structure exactly. Total size: 64 * 8 = 512 bytes.
 */
static const unsigned long long psk31_varicode_masklen_helper[64] = {
    0x0000000000000000ULL, 0x0000000000000001ULL, 0x0000000000000003ULL, 0x0000000000000007ULL,
    0x000000000000000fULL, 0x000000000000001fULL, 0x000000000000003fULL, 0x000000000000007fULL,
    0x00000000000000ffULL, 0x00000000000001ffULL, 0x00000000000003ffULL, 0x00000000000007ffULL,
    0x0000000000000fffULL, 0x0000000000001fffULL, 0x0000000000003fffULL, 0x0000000000007fffULL,
    0x000000000000ffffULL, 0x000000000001ffffULL, 0x000000000003ffffULL, 0x000000000007ffffULL,
    0x00000000000fffffULL, 0x00000000001fffffULL, 0x00000000003fffffULL, 0x00000000007fffffULL,
    0x0000000000ffffffULL, 0x0000000001ffffffULL, 0x0000000003ffffffULL, 0x0000000007ffffffULL,
    0x000000000fffffffULL, 0x000000001fffffffULL, 0x000000003fffffffULL, 0x000000007fffffffULL,
    0x00000000ffffffffULL, 0x00000001ffffffffULL, 0x00000003ffffffffULL, 0x00000007ffffffffULL,
    0x0000000fffffffffULL, 0x0000001fffffffffULL, 0x0000003fffffffffULL, 0x0000007fffffffffULL,
    0x000000ffffffffffULL, 0x000001ffffffffffULL, 0x000003ffffffffffULL, 0x000007ffffffffffULL,
    0x00000fffffffffffULL, 0x00001fffffffffffULL, 0x00003fffffffffffULL, 0x00007fffffffffffULL,
    0x0000ffffffffffffULL, 0x0001ffffffffffffULL, 0x0003ffffffffffffULL, 0x0007ffffffffffffULL,
    0x000fffffffffffffULL, 0x001fffffffffffffULL, 0x003fffffffffffffULL, 0x007fffffffffffffULL,
    0x00ffffffffffffffULL, 0x01ffffffffffffffULL, 0x03ffffffffffffffULL, 0x07ffffffffffffffULL,
    0x0fffffffffffffffULL, 0x1fffffffffffffffULL, 0x3fffffffffffffffULL, 0x7fffffffffffffffULL,
};

/* =================================================================
 * Decoder state (persists across calls)
 * =================================================================
 */
static unsigned long long s_status_shr   = 0;     /* varicode shift register */
static float              s_last_phase   = 0.0f;  /* DBPSK previous-sample phase */

/* =================================================================
 * Public API
 * =================================================================
 */

void ResetPSK31Decoder(void) {
    s_status_shr = 0;
    s_last_phase = 0.0f;
}

char PSK31VaricodeDecoderPush(uint8_t symbol) {
    s_status_shr = (s_status_shr << 1) | (symbol ? 1ULL : 0ULL);

    /* If the low 12 bits are all zero we can't yet have matched a codeword
     * (the shortest codeword is 1 bit + 2 trailing zeros = 3 bits, so 12
     * zeros means we haven't seen any signal yet). Bail early. */
    if ((s_status_shr & 0xFFFULL) == 0ULL) {
        return 0;
    }

    for (int i = 0; i < N_PSK31_VARICODE_ITEMS; i++) {
        unsigned long long candidate = psk31_varicode_items[i].code << 2;
        unsigned long long mask = psk31_varicode_masklen_helper[
            (psk31_varicode_items[i].bitcount + 4) & 63];
        unsigned long long got = s_status_shr & mask;
        if (candidate == got) {
            s_status_shr = 0;  /* reset shift register on match */
            return (char)psk31_varicode_items[i].ascii;
        }
    }
    return 0;
}

void PSK31VaricodeEncode(const uint8_t *input, uint8_t *output,
                         int input_size, int output_max_size,
                         int *input_processed, int *output_size) {
    *output_size = 0;
    for (*input_processed = 0; *input_processed < input_size; (*input_processed)++) {
        for (int ci = 0; ci < N_PSK31_VARICODE_ITEMS; ci++) {
            const psk31_varicode_item_t *cv = &psk31_varicode_items[ci];
            if (input[*input_processed] != cv->ascii) continue;

            /* Each output is bitcount data bits + 2 trailing zero bits. */
            const int total_bits = cv->bitcount + 2;
            if (output_max_size < total_bits) return;
            for (int bi = 0; bi < total_bits; bi++) {
                output[*output_size] = (bi < cv->bitcount)
                    ? (uint8_t)((cv->code >> (cv->bitcount - bi - 1)) & 1ULL)
                    : 0;
                (*output_size)++;
                output_max_size--;
            }
            break;
        }
    }
}

void PSK31DBPSKDecode(const DataBlock *data, uint8_t *output) {
    if (data == nullptr || output == nullptr || data->N == 0) return;

    for (unsigned i = 0; i < data->N; i++) {
        float phase = ApproxAtan2(data->Q[i], data->I[i]);
        float dphase = phase - s_last_phase;
        /* Wrap into (-PI, PI]. */
        while (dphase < -(float)PI) dphase += 2.0f * (float)PI;
        while (dphase >=  (float)PI) dphase -= 2.0f * (float)PI;

        /* Differential bit: 1 if no significant phase change, 0 if ~PI flip. */
        if (dphase > ((float)PI / 2.0f) || dphase < -((float)PI / 2.0f)) {
            output[i] = 0;
        } else {
            output[i] = 1;
        }
        s_last_phase = phase;
    }
}

/* ============================================================
 * Phoenix-native PSK31 decoder pipeline.
 *
 * Operating sample rate: 24 kHz (Phoenix's audio rate after the post-IFFT
 * decimation chain in DSP.cpp). PSK31 baud is 31.25; we decimate to a
 * working rate of 24000/96 = 250 sps (= 8x oversampling per symbol).
 *
 * Pipeline stages:
 *   1. NCO mixer at psk31RxFreq -> shifts the audio carrier to baseband.
 *      Out: complex baseband at 24 kHz.
 *   2. Single-pole low-pass filter on each of I, Q (RC ~50 Hz cutoff).
 *      Out: narrow-band complex baseband.
 *   3. 96:1 decimator (counter-driven). Out: complex baseband at 250 sps.
 *   4. Gardner symbol-clock recovery on the decimated stream. Tracks a
 *      fractional symbol position mu in [0, T_symbol_samples). When mu
 *      crosses, output one symbol and update the clock-error-tracking PI
 *      loop.
 *   5. DBPSK on consecutive symbols: |delta_phase| > PI/2 -> bit 0, else 1.
 *   6. PSK31VaricodeDecoderPush() bit-by-bit -> ASCII chars.
 *   7. Push chars to the text ring buffer.
 * ============================================================ */

#define PSK31_AUDIO_RATE_HZ      24000
#define PSK31_DECIM              96
#define PSK31_BASEBAND_RATE_HZ   (PSK31_AUDIO_RATE_HZ / PSK31_DECIM)  /* 250 */
#define PSK31_SYMBOLS_PER_SEC    31.25f
#define PSK31_SAMPLES_PER_SYMBOL (PSK31_BASEBAND_RATE_HZ / PSK31_SYMBOLS_PER_SEC) /* 8.0 */

/* Public state */
int psk31RxFreq = 1000;  /* default 1 kHz audio carrier */

/* Module-private pipeline state */
static float s_nco_phase = 0.0f;       /* NCO phase accumulator (radians) */
static float s_lp_i = 0.0f;            /* low-pass state, I channel */
static float s_lp_q = 0.0f;            /* low-pass state, Q channel */
static int   s_decim_count = 0;        /* 0..PSK31_DECIM-1 */

/* Gardner clock-recovery state. Buffer holds the last few decimated samples
 * so we can compute Gardner's timing-error metric e = (early - late) * mid.
 * We use a 3-tap window (early, mid, late) sliding across the ring. */
#define PSK31_GARDNER_BUF_SIZE 16  /* > 2 * samples_per_symbol */
static float s_gardner_i[PSK31_GARDNER_BUF_SIZE];
static float s_gardner_q[PSK31_GARDNER_BUF_SIZE];
static int   s_gardner_widx = 0;       /* write index into gardner_i/q */
static float s_gardner_mu = 0.0f;      /* fractional symbol position [0, T) */
static float s_gardner_loop_gain = 0.05f;  /* PI loop gain on timing error */

/* DBPSK previous-symbol phase for the runtime decoder (separate from the
 * primitive PSK31DBPSKDecode's s_last_phase). */
static float s_dbpsk_last_phase = 0.0f;

/* ----- Decoder status / telemetry -----
 * Updated on every emitted symbol; consumed by the decoder-status sub-page
 * (toggled via FINETUNE_BUTTON in PSK31 mode). Telemetry tracks the values
 * a Gardner clock-recovery loop should converge: mu near 0, dphase
 * concentrated at 0 or +/-PI for clean BPSK, |err| trending small. */
static bool  s_psk31StatusPageActive   = false;  /* true => pane shows status, not text */
static float s_last_mu_at_symbol       = 0.0f;   /* mu sampled at symbol emit */
static float s_last_dphase             = 0.0f;   /* DBPSK delta-phase, last symbol */
static float s_last_timing_err_smoothed = 0.0f;  /* smoothed |gardner err| */
#define PSK31_ERR_SMOOTH_ALPHA  0.10f            /* IIR alpha on |err| */

/* Decoded-text ring buffer */
static char s_text_buf[PSK31_TEXT_BUFFER_SIZE];
static int  s_text_head = 0;           /* next-write index */
static int  s_text_count = 0;          /* total chars written, capped at buffer size */

static void PushTextChar(char c) {
    s_text_buf[s_text_head] = c;
    s_text_head = (s_text_head + 1) % PSK31_TEXT_BUFFER_SIZE;
    if (s_text_count < PSK31_TEXT_BUFFER_SIZE) s_text_count++;
}

void PSK31ClearText(void) {
    s_text_head = 0;
    s_text_count = 0;
    s_text_buf[0] = '\0';
}

int PSK31GetText(char *out, int outMax) {
    if (out == NULL || outMax <= 0) return 0;
    int n = (s_text_count < outMax - 1) ? s_text_count : outMax - 1;
    /* Linearise from oldest to newest. The oldest is at s_text_head when full,
     * or at index 0 when not yet full. */
    int start = (s_text_count < PSK31_TEXT_BUFFER_SIZE)
                ? 0
                : s_text_head;
    for (int i = 0; i < n; i++) {
        out[i] = s_text_buf[(start + i) % PSK31_TEXT_BUFFER_SIZE];
    }
    out[n] = '\0';
    return n;
}

void ResetPSK31Pipeline(void) {
    s_nco_phase = 0.0f;
    s_lp_i = 0.0f;
    s_lp_q = 0.0f;
    s_decim_count = 0;
    s_gardner_widx = 0;
    s_gardner_mu = 0.0f;
    s_dbpsk_last_phase = 0.0f;
    /* Don't clear the text buffer here -- ChangePSK31RxFreq might be called
     * mid-decode and we want recent text preserved. PSK31ClearText() is the
     * separate operation to wipe text. */
    /* Reset varicode shift register so we don't carry partial codeword bits
     * across a freq change. */
    ResetPSK31Decoder();
}

/* Forward decl of the pane-stale helper (defined elsewhere) -- declared as
 * extern via MainBoard_Display.h above. We re-extern here to keep the
 * function self-contained for readers. */
extern struct Pane PaneSpectrum;

void ChangePSK31RxFreq(int wheel) {
    psk31RxFreq += wheel * 5;  /* 5 Hz step per encoder click */
    if (psk31RxFreq < 200)  psk31RxFreq = 200;
    if (psk31RxFreq > 2700) psk31RxFreq = 2700;
    /* The new carrier means our NCO + filter state are stale; reset so the
     * next sample mixes against a phase-aligned NCO. */
    ResetPSK31Pipeline();
    PaneSpectrum.stale = true;
}

/*
 * Single-pole IIR low-pass filter:
 *   y[n] = alpha * x[n] + (1 - alpha) * y[n-1]
 *
 * For PSK31 we want a cutoff well above the symbol rate (31.25 Hz) but
 * below half the carrier offset so we don't pull in adjacent signals. With
 * alpha = 0.05 at 24 kHz, the -3 dB cutoff is approximately
 *   fc ~= alpha * fs / (2 * pi) ~= 191 Hz
 * which is wide enough to pass the PSK31 spectrum (occupies ~62 Hz) and
 * narrow enough to reject most adjacent QSO-distance interference.
 */
#define PSK31_LP_ALPHA  0.05f

/*
 * Gardner timing-error metric:
 *   e = (sample at mu + T/2) * (sample at mu - sample at mu + T)
 * where T is one symbol period in samples.
 *
 * For 8 samples/symbol with mu in [0, 8), early = mu (current symbol point),
 * mid = mu + 4, late = mu + 8 (the next symbol point). We compute e and use
 * it to nudge mu's fractional advance.
 *
 * A successful decode produces |e| -> 0 and mu -> 0 (centered on symbols).
 */
static float gardner_error(float i_early, float q_early,
                           float i_mid,   float q_mid,
                           float i_late,  float q_late) {
    /* Error metric uses real component only (BPSK is real-axis modulation
     * after carrier alignment). */
    return (i_late - i_early) * i_mid + (q_late - q_early) * q_mid;
}

void psk31Demod(DataBlock *data) {
    if (data == NULL || data->N == 0) return;

    const float dphase_per_sample =
        2.0f * (float)PI * (float)psk31RxFreq / (float)PSK31_AUDIO_RATE_HZ;

    for (unsigned i = 0; i < data->N; i++) {
        /* --- 1. NCO mixer: bring carrier to baseband by multiplying by
         * exp(-j * 2*pi*f*n/fs). New IQ at the mixer output. */
        float c = cosf(s_nco_phase);
        float s = sinf(s_nco_phase);
        float audio_i = data->I[i];
        float audio_q = data->Q[i];
        float bb_i = audio_i * c + audio_q * s;
        float bb_q = -audio_i * s + audio_q * c;

        s_nco_phase += dphase_per_sample;
        if (s_nco_phase > 2.0f * (float)PI) s_nco_phase -= 2.0f * (float)PI;

        /* --- 2. Single-pole low-pass on each channel. */
        s_lp_i = PSK31_LP_ALPHA * bb_i + (1.0f - PSK31_LP_ALPHA) * s_lp_i;
        s_lp_q = PSK31_LP_ALPHA * bb_q + (1.0f - PSK31_LP_ALPHA) * s_lp_q;

        /* --- 3. Decimator: emit one decimated sample every PSK31_DECIM input
         * samples. */
        s_decim_count++;
        if (s_decim_count < PSK31_DECIM) continue;
        s_decim_count = 0;

        /* The decimated sample is the current low-pass output. */
        float dec_i = s_lp_i;
        float dec_q = s_lp_q;

        /* --- 4. Gardner symbol-clock recovery. Push into ring; once we have
         * enough samples (>= 2 symbol periods), check if mu has crossed a
         * symbol boundary and emit a symbol. */
        s_gardner_i[s_gardner_widx] = dec_i;
        s_gardner_q[s_gardner_widx] = dec_q;
        s_gardner_widx = (s_gardner_widx + 1) % PSK31_GARDNER_BUF_SIZE;

        /* Advance fractional position by one decimated sample. */
        s_gardner_mu += 1.0f;
        if (s_gardner_mu < PSK31_SAMPLES_PER_SYMBOL) continue;

        /* mu crossed a symbol boundary; emit one symbol and adjust clock. */
        s_gardner_mu -= PSK31_SAMPLES_PER_SYMBOL;

        /* Look back 2 symbols, 1 symbol, and 0 symbols (the current point).
         * "Late" = current; "early" = 2 symbols back; "mid" = 1 symbol back. */
        const int sps = (int)PSK31_SAMPLES_PER_SYMBOL;
        int idx_late  = (s_gardner_widx - 1 + PSK31_GARDNER_BUF_SIZE) % PSK31_GARDNER_BUF_SIZE;
        int idx_mid   = (s_gardner_widx - 1 - sps + PSK31_GARDNER_BUF_SIZE) % PSK31_GARDNER_BUF_SIZE;
        int idx_early = (s_gardner_widx - 1 - 2*sps + PSK31_GARDNER_BUF_SIZE) % PSK31_GARDNER_BUF_SIZE;

        float i_e = s_gardner_i[idx_early], q_e = s_gardner_q[idx_early];
        float i_m = s_gardner_i[idx_mid],   q_m = s_gardner_q[idx_mid];
        float i_l = s_gardner_i[idx_late],  q_l = s_gardner_q[idx_late];

        float err = gardner_error(i_e, q_e, i_m, q_m, i_l, q_l);
        /* Bound the error so a single bad sample doesn't slew mu wildly. */
        if (err >  1.0f) err =  1.0f;
        if (err < -1.0f) err = -1.0f;
        s_gardner_mu += s_gardner_loop_gain * err;

        /* --- Telemetry: track |err| as a smoothed magnitude for the
         * decoder-status sub-page. */
        float abs_err = err < 0.0f ? -err : err;
        s_last_timing_err_smoothed =
            PSK31_ERR_SMOOTH_ALPHA * abs_err +
            (1.0f - PSK31_ERR_SMOOTH_ALPHA) * s_last_timing_err_smoothed;

        /* --- 5. DBPSK on the symbol point (use mid-symbol since that's where
         * Gardner is centered after convergence). */
        float phase = ApproxAtan2(q_m, i_m);
        float dphase = phase - s_dbpsk_last_phase;
        while (dphase < -(float)PI)  dphase += 2.0f * (float)PI;
        while (dphase >=  (float)PI) dphase -= 2.0f * (float)PI;
        s_dbpsk_last_phase = phase;

        /* --- Telemetry: snapshot mu and dphase at this symbol emit. */
        s_last_mu_at_symbol = s_gardner_mu;
        s_last_dphase       = dphase;

        uint8_t bit;
        if (dphase > ((float)PI / 2.0f) || dphase < -((float)PI / 2.0f)) {
            bit = 0;
        } else {
            bit = 1;
        }

        /* --- If the decoder-status sub-page is active, every emitted symbol
         * is a reason to repaint (the user wants live telemetry). */
        if (s_psk31StatusPageActive) {
            PaneSpectrum.stale = true;
        }

        /* --- 6. Push the bit to the varicode decoder. On a complete codeword
         * match it returns the ASCII char; otherwise 0. */
        char ch = PSK31VaricodeDecoderPush(bit);
        if (ch != 0) {
            /* Filter out non-printable control chars except CR/LF. */
            if (ch >= 32 || ch == '\n' || ch == '\r') {
                PushTextChar(ch);
                /* Pane needs repaint (in text mode this paints the new char;
                 * in status mode this is harmless extra staleness). */
                PaneSpectrum.stale = true;
            }
        }
    }
}

/* ============================================================
 * Decoder-status sub-page accessors
 * ============================================================ */

void PSK31ToggleStatusPage(void) {
    s_psk31StatusPageActive = !s_psk31StatusPageActive;
    PaneSpectrum.stale = true;
}

bool PSK31IsStatusPageActive(void) {
    return s_psk31StatusPageActive;
}

void PSK31GetDecoderStatus(float *mu, float *dphase, float *err_smoothed) {
    if (mu)           *mu           = s_last_mu_at_symbol;
    if (dphase)       *dphase       = s_last_dphase;
    if (err_smoothed) *err_smoothed = s_last_timing_err_smoothed;
}
