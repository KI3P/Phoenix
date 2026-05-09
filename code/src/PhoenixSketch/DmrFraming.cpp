/**
 * @file DmrFraming.cpp
 * @brief DMR voice LC header + voice payload bursts with BPTC + Golay FEC.
 *
 * What works in this revision:
 *
 *   - Hamming(15,11) and Hamming(13,9) primitives (encode + 1-bit-correct
 *     decode) with g(x) = x^4 + x + 1.
 *   - BPTC(196,96): 96 information bits laid into a 13x15 matrix with row
 *     Hamming(15,11) and column Hamming(13,9), one extra reserved bit
 *     R(0) at the head. Encode + iterative-decode (row pass then column
 *     pass).
 *   - Golay(23,12): perfect 3-error-correcting code with g(x) =
 *     x^11 + x^9 + x^7 + x^6 + x^5 + x + 1. Encode + decode via a 2048-
 *     entry syndrome table built lazily on first use (~6 KB static).
 *   - Voice LC header (96 bits): PF (1) + Reserved (1) + FLCO (6) +
 *     FID (8) + Service Options (8) + Destination ID (24) + Source ID
 *     (24) = 72 bits, plus 24 bits CRC. Group vs Private is encoded in
 *     FLCO (0 vs 3).
 *   - DmrTxBuildLcHeaderBurst / DmrRxParseLcHeaderBurst: end-to-end
 *     pipeline that produces the 33-byte LC-header burst the modem ships
 *     and parses it back into Talk Group / src / dst.
 *   - Voice burst (3 AMBE+2 frames per 60 ms slot): each AMBE frame is
 *     compressed 49 → 72 bits via [Golay(23,12) | Golay(23,12) |
 *     Hamming(15,11) | raw], dropping the bottom 3 LSB-est AMBE bits per
 *     ETSI; the three 72-bit frames fill the 216-bit voice info area
 *     around the SYNC. DmrTxBuildVoiceFrame / DmrRxParseFrame are live.
 *   - Cached addressing: voice bursts inherit Talk Group / src / dst
 *     from the most recent LC header parse so DmrRxParseFrame can
 *     return them.
 *
 * What is NOT yet here:
 *   - Voice frames A..E with Embedded Signaling Block (EMB) replacing
 *     SYNC in the middle of the burst -- only voice frame F (full SYNC)
 *     is encoded/decoded for now. The modem's SYNC detector finds
 *     exactly that.
 *   - Slot Type field with Golay(20,8) on data bursts (zero-padded).
 *   - ETSI-spec CRC mask (0x96 0x96 0x96 XOR for voice LC header). The
 *     generic CRC-24 here makes TX → RX bit-exact between Phoenix and
 *     Phoenix; a non-Phoenix DMR receiver will reject it until the
 *     spec mask is applied.
 *
 * Reference materials for future tightening:
 *   - ETSI TS 102 361-1 v2.4.1 Annex B (FEC + interleaving)
 *   - ETSI TS 102 361-2 v2.3.1 Tables 8.x  (LC opcodes / FIDs)
 *   - MMDVMHost (open-source reference)
 */

#include "SDT.h"
#include "DmrFraming.h"
#include "DmrModem.h"   // DmrModemGetSyncRef -- Tier-II frame anchor
#include <string.h>

/* Arduino's wiring.h declares `bitSet(value, bit)` as a 2-argument macro,
 * which collides with the 3-argument `bitSet(buf, idx, v)` helper this
 * file uses inside its anonymous namespace. The macros are not used by
 * any DMR code; drop them so the helpers compile. */
#ifdef bitSet
#undef bitSet
#endif
#ifdef bitGet
#undef bitGet
#endif

/* ====================================================================== */
/*                        Configuration / globals                         */
/* ====================================================================== */

namespace {

DmrConfig localCfg;
bool      configured = false;

/* Cached addressing from the most recent successful LC header parse.
 * Voice bursts on their own do not carry full addressing -- callers of
 * DmrRxParseFrame want the src/dst from the LC that opened the call. */
DmrCallType lastCallType = DmrCallGroup;
uint32_t    lastSrcId    = 0;
uint32_t    lastDstId    = 0;
bool        lastValid    = false;
uint32_t    lastSeenMs   = 0;     /* millis() of most recent successful parse */

/* FEC correction counters. fecCounter accumulates inside BPTC + Hamming
 * decoders during a parse; lastBurstCorrections snapshots it at the end
 * of each parse function so the home pane sees a stable per-burst
 * value. */
int         fecCounter            = 0;
int         lastBurstCorrections  = 0;

/* SYNC pattern (BS-sourced voice). Must match the modem's sync detector. */
constexpr uint8_t BS_VOICE_SYNC[6] = { 0x75, 0x5F, 0xD7, 0xDF, 0x75, 0xF7 };

/* DMR FLCO opcodes used here. */
constexpr uint8_t FLCO_GROUP   = 0;
constexpr uint8_t FLCO_PRIVATE = 3;

/* ====================================================================== */
/*                       Bit-buffer helpers (MSB-first)                   */
/* ====================================================================== */

inline bool bitGet(const uint8_t *buf, int bitIdx) {
    return (buf[bitIdx >> 3] >> (7 - (bitIdx & 7))) & 1;
}
inline void bitSet(uint8_t *buf, int bitIdx, bool v) {
    const uint8_t m = (uint8_t)(1u << (7 - (bitIdx & 7)));
    if (v) buf[bitIdx >> 3] |=  m;
    else   buf[bitIdx >> 3] &= ~m;
}

/* ====================================================================== */
/*                            Hamming primitives                          */
/* ====================================================================== */

/* Hamming(15,11) systematic with g(x) = x^4 + x + 1 (= 0x13).
 * Codeword layout: [I10 I9 ... I0 P3 P2 P1 P0] (data MSB-first, parity
 * appended low).  hamming_15_11_parity returns the 4-bit P field. */
uint8_t hamming_15_11_parity(uint16_t data) {
    uint16_t cw = (uint16_t)((data & 0x7FF) << 4);
    for (int i = 14; i >= 4; --i) {
        if (cw & (uint16_t)(1u << i)) cw ^= (uint16_t)(0x13u << (i - 4));
    }
    return (uint8_t)(cw & 0xFu);
}

uint16_t hamming_15_11_encode(uint16_t data) {
    return (uint16_t)(((data & 0x7FFu) << 4) | hamming_15_11_parity(data));
}

/* Decode: returns true if the codeword was clean or correctable, with
 * the recovered 11-bit data in *out. Tries flipping each bit to find a
 * single-bit error (15 trials -- cheap). */
bool hamming_15_11_decode(uint16_t cw, uint16_t *out) {
    auto syndrome = [](uint16_t x) {
        for (int i = 14; i >= 4; --i)
            if (x & (uint16_t)(1u << i)) x ^= (uint16_t)(0x13u << (i - 4));
        return (uint8_t)(x & 0xFu);
    };
    if (syndrome(cw) == 0) {
        *out = (uint16_t)((cw >> 4) & 0x7FFu);
        return true;
    }
    for (int b = 0; b < 15; ++b) {
        uint16_t test = (uint16_t)(cw ^ (1u << b));
        if (syndrome(test) == 0) {
            *out = (uint16_t)((test >> 4) & 0x7FFu);
            return true;
        }
    }
    *out = (uint16_t)((cw >> 4) & 0x7FFu);
    return false;
}

/* Hamming(13,9) systematic with g(x) = x^4 + x + 1. Same shape as the
 * (15,11) code; just shorter. */
uint8_t hamming_13_9_parity(uint16_t data) {
    uint16_t cw = (uint16_t)((data & 0x1FFu) << 4);
    for (int i = 12; i >= 4; --i) {
        if (cw & (uint16_t)(1u << i)) cw ^= (uint16_t)(0x13u << (i - 4));
    }
    return (uint8_t)(cw & 0xFu);
}

bool hamming_13_9_decode(uint16_t cw, uint16_t *out) {
    auto syndrome = [](uint16_t x) {
        for (int i = 12; i >= 4; --i)
            if (x & (uint16_t)(1u << i)) x ^= (uint16_t)(0x13u << (i - 4));
        return (uint8_t)(x & 0xFu);
    };
    if (syndrome(cw) == 0) {
        *out = (uint16_t)((cw >> 4) & 0x1FFu);
        return true;
    }
    for (int b = 0; b < 13; ++b) {
        uint16_t test = (uint16_t)(cw ^ (1u << b));
        if (syndrome(test) == 0) {
            *out = (uint16_t)((test >> 4) & 0x1FFu);
            return true;
        }
    }
    *out = (uint16_t)((cw >> 4) & 0x1FFu);
    return false;
}

/* ====================================================================== */
/*                            BPTC(196, 96)                               */
/* ====================================================================== */

/* Layout (per ETSI TS 102 361-1 Annex B.2.3):
 *   - One reserved bit R(0) at output bit 0 (always 0 here)
 *   - 13x15 matrix M[r][c] follows in row-major order (output bits 1..195)
 *   - Row Hamming(15,11): for r in 0..8, M[r][11..14] = parity over M[r][0..10]
 *   - Column Hamming(13,9): for c in 0..14, M[9..12][c] = parity over M[0..8][c]
 *   - Info bits in M:
 *       row 0:  M[0][0..2] = R(3,2,1) = 0  (3 reserved bits)
 *               M[0][3..10] = I(0..7)      (8 info bits)
 *       rows 1..8: M[r][0..10] = next 11 info bits each
 *       (8 + 88 = 96 info bits total)
 */

constexpr int BPTC_OUT_BYTES = 25;       /* 200 bits to hold 196, low 4 unused */

void bptc_196_96_encode(const uint8_t info[12], uint8_t out[BPTC_OUT_BYTES]) {
    uint8_t M[13][15];
    memset(M, 0, sizeof(M));

    int infoIdx = 0;
    for (int c = 3; c < 11; ++c) {
        M[0][c] = bitGet(info, infoIdx) ? 1 : 0;
        ++infoIdx;
    }
    for (int r = 1; r < 9; ++r) {
        for (int c = 0; c < 11; ++c) {
            M[r][c] = bitGet(info, infoIdx) ? 1 : 0;
            ++infoIdx;
        }
    }

    /* Row Hamming(15,11) parity for rows 0..8. */
    for (int r = 0; r < 9; ++r) {
        uint16_t data = 0;
        for (int c = 0; c < 11; ++c) data |= (uint16_t)(M[r][c] & 1) << (10 - c);
        const uint8_t p = hamming_15_11_parity(data);
        M[r][11] = (uint8_t)((p >> 3) & 1);
        M[r][12] = (uint8_t)((p >> 2) & 1);
        M[r][13] = (uint8_t)((p >> 1) & 1);
        M[r][14] = (uint8_t)((p >> 0) & 1);
    }

    /* Column Hamming(13,9) parity for cols 0..14. */
    for (int c = 0; c < 15; ++c) {
        uint16_t data = 0;
        for (int r = 0; r < 9; ++r) data |= (uint16_t)(M[r][c] & 1) << (8 - r);
        const uint8_t p = hamming_13_9_parity(data);
        M[9][c]  = (uint8_t)((p >> 3) & 1);
        M[10][c] = (uint8_t)((p >> 2) & 1);
        M[11][c] = (uint8_t)((p >> 1) & 1);
        M[12][c] = (uint8_t)((p >> 0) & 1);
    }

    memset(out, 0, BPTC_OUT_BYTES);
    /* Bit 0 = R(0) = 0; matrix follows row-major in bits 1..195 */
    int bitIdx = 1;
    for (int r = 0; r < 13; ++r) {
        for (int c = 0; c < 15; ++c) {
            bitSet(out, bitIdx, M[r][c] != 0);
            ++bitIdx;
        }
    }
}

bool bptc_196_96_decode(const uint8_t in[BPTC_OUT_BYTES], uint8_t out[12]) {
    uint8_t M[13][15];
    int bitIdx = 1;
    for (int r = 0; r < 13; ++r) {
        for (int c = 0; c < 15; ++c) {
            M[r][c] = (uint8_t)(bitGet(in, bitIdx) ? 1 : 0);
            ++bitIdx;
        }
    }

    /* One iteration: rows then columns. Two passes are sufficient for the
     * single-error-per-axis correction capability of (15,11) + (13,9). */
    auto h15_11_syn = [](uint16_t x) {
        for (int i = 14; i >= 4; --i)
            if (x & (uint16_t)(1u << i)) x ^= (uint16_t)(0x13u << (i - 4));
        return (uint16_t)(x & 0xFu);
    };
    auto h13_9_syn = [](uint16_t x) {
        for (int i = 12; i >= 4; --i)
            if (x & (uint16_t)(1u << i)) x ^= (uint16_t)(0x13u << (i - 4));
        return (uint16_t)(x & 0xFu);
    };
    for (int pass = 0; pass < 2; ++pass) {
        for (int r = 0; r < 9; ++r) {
            uint16_t cw = 0;
            for (int c = 0; c < 15; ++c) cw |= (uint16_t)(M[r][c] & 1) << (14 - c);
            uint16_t data;
            if (pass == 0 && h15_11_syn(cw) != 0) ++fecCounter;
            hamming_15_11_decode(cw, &data);
            for (int c = 0; c < 11; ++c) M[r][c] = (uint8_t)((data >> (10 - c)) & 1);
            /* Re-derive parity bits from corrected data so column pass sees clean cells. */
            const uint8_t p = hamming_15_11_parity(data);
            M[r][11] = (uint8_t)((p >> 3) & 1);
            M[r][12] = (uint8_t)((p >> 2) & 1);
            M[r][13] = (uint8_t)((p >> 1) & 1);
            M[r][14] = (uint8_t)((p >> 0) & 1);
        }
        for (int c = 0; c < 15; ++c) {
            uint16_t cw = 0;
            for (int r = 0; r < 13; ++r) cw |= (uint16_t)(M[r][c] & 1) << (12 - r);
            uint16_t data;
            if (pass == 0 && h13_9_syn(cw) != 0) ++fecCounter;
            hamming_13_9_decode(cw, &data);
            for (int r = 0; r < 9; ++r) M[r][c] = (uint8_t)((data >> (8 - r)) & 1);
            const uint8_t p = hamming_13_9_parity(data);
            M[9][c]  = (uint8_t)((p >> 3) & 1);
            M[10][c] = (uint8_t)((p >> 2) & 1);
            M[11][c] = (uint8_t)((p >> 1) & 1);
            M[12][c] = (uint8_t)((p >> 0) & 1);
        }
    }

    /* Extract 96 info bits: M[0][3..10] (8) + M[1..8][0..10] (88). */
    memset(out, 0, 12);
    int idx = 0;
    for (int c = 3; c < 11; ++c) { bitSet(out, idx, M[0][c] != 0); ++idx; }
    for (int r = 1; r < 9; ++r) {
        for (int c = 0; c < 11; ++c) { bitSet(out, idx, M[r][c] != 0); ++idx; }
    }
    return true;
}

/* ====================================================================== */
/*                       GF(2^8) and RS(12, 9)                            */
/* ====================================================================== */
/* ETSI TS 102 361-1 Annex B.3.11 specifies a Reed-Solomon (12,9) code
 * over GF(2^8) with primitive polynomial p(x) = x^8 + x^4 + x^3 + x^2 + 1
 * (= 0x11D), primitive element α = 2, and generator polynomial
 *     g(x) = (x + α)(x + α²)(x + α³)
 *          = x³ + 14·x² + 56·x + 64       (computed in GF(2^8))
 *
 * After encoding, the 3 RS check bytes are XORed with a fixed 3-byte
 * CRC mask whose value selects the burst type:
 *     Voice LC Header     : 0x96 0x96 0x96
 *     Terminator with LC  : 0x99 0x99 0x99
 *     CSBK                : 0xA5 0xA5 0xA5
 * The mask is XORed off again on RX before syndrome check, so a
 * mismatched mask shows up as a non-zero syndrome and the burst is
 * dropped. */

uint8_t gfExp[512];   /* α^i, with i=0..510 (255-period repeated for safety) */
uint8_t gfLog[256];   /* gfLog[α^i] = i */
bool    gfReady = false;

void gf_init(void) {
    if (gfReady) return;
    uint16_t x = 1;
    for (int i = 0; i < 255; ++i) {
        gfExp[i] = (uint8_t)x;
        gfLog[x] = (uint8_t)i;
        x <<= 1;
        if (x & 0x100u) x ^= 0x11Du;
    }
    /* Duplicate the table to avoid % 255 in the hot path. */
    for (int i = 255; i < 512; ++i) gfExp[i] = gfExp[i - 255];
    gfLog[0] = 0;     /* undefined; writes that hit it are ignored elsewhere */
    gfReady = true;
}

inline uint8_t gfMul(uint8_t a, uint8_t b) {
    if (a == 0 || b == 0) return 0;
    return gfExp[(int)gfLog[a] + (int)gfLog[b]];
}
inline uint8_t gfDiv(uint8_t a, uint8_t b) {
    /* Caller must ensure b != 0; we return 0 defensively otherwise. */
    if (a == 0 || b == 0) return 0;
    return gfExp[(int)gfLog[a] + 255 - (int)gfLog[b]];
}

/* g(x) = x³ + 14·x² + 56·x + 64 -- coefficients of x², x¹, x⁰ (highest of
 * the non-monic terms first). */
constexpr uint8_t RS_12_9_GEN[3] = { 14, 56, 64 };

/* Voice LC header CRC mask per ETSI Table B.27. */
constexpr uint8_t RS_MASK_VOICE_LC[3] = { 0x96, 0x96, 0x96 };

/* RS(12,9) systematic encode: 9 info bytes → 3 parity bytes (highest-
 * order coefficient first in the parity[] array). Caller XORs the mask
 * onto parity[] before transmitting the codeword. */
void rs_12_9_encode(const uint8_t info[9], uint8_t parity[3]) {
    gf_init();
    /* Build cw[0..11] in highest-coefficient-first order:
     *   cw[0..8]   = info[0..8]   (x^11 .. x^3)
     *   cw[9..11]  = parity slot  (x^2  .. x^0)
     */
    uint8_t cw[12];
    for (int i = 0; i < 9; ++i) cw[i] = info[i];
    cw[9] = cw[10] = cw[11] = 0;

    /* Polynomial long division by g(x). */
    for (int i = 0; i < 9; ++i) {
        const uint8_t lead = cw[i];
        if (lead == 0) continue;
        cw[i + 1] ^= gfMul(lead, RS_12_9_GEN[0]);  /* × x²-coefficient (14) */
        cw[i + 2] ^= gfMul(lead, RS_12_9_GEN[1]);  /* × x¹-coefficient (56) */
        cw[i + 3] ^= gfMul(lead, RS_12_9_GEN[2]);  /* × x⁰-coefficient (64) */
        /* (cw[i] gets XOR'd with itself, becoming 0 -- equivalent to
         * subtracting lead·g(x) shifted to position i.) */
    }
    parity[0] = cw[9];
    parity[1] = cw[10];
    parity[2] = cw[11];
}

/* RS(12,9) syndromes: evaluate cw at α^1, α^2, α^3. */
void rs_12_9_syndromes(const uint8_t cw[12], uint8_t syn[3]) {
    gf_init();
    for (int i = 0; i < 3; ++i) {
        uint8_t s = 0;
        for (int j = 0; j < 12; ++j) {
            const int exp = ((i + 1) * (11 - j)) % 255;
            s ^= gfMul(cw[j], gfExp[exp]);
        }
        syn[i] = s;
    }
}

/* RS(12,9) decode in place. Corrects up to 1 byte error; multi-byte
 * errors are reported as failure. Returns true if syndromes are zero
 * after correction (i.e. valid codeword). Caller XORs the mask off
 * parity bytes before calling. */
bool rs_12_9_decode(uint8_t cw[12]) {
    gf_init();
    uint8_t syn[3];
    rs_12_9_syndromes(cw, syn);
    if ((syn[0] | syn[1] | syn[2]) == 0) return true;

    /* Single-byte error: e_pos at coefficient α^(11 - p), e_val s.t.
     *   S0 = e_val · α^(11-p)
     *   S1 = e_val · α^(2(11-p))   ⇒ S1/S0 = α^(11-p)
     *   S2 = e_val · α^(3(11-p))   ⇒ verifies as S2 = (S1/S0) · S1 */
    if (syn[0] == 0) return false;       /* would require divide by 0 */
    const uint8_t locRatio = gfDiv(syn[1], syn[0]);
    const int     alphaExp = (locRatio == 0) ? -1 : (int)gfLog[locRatio];
    if (alphaExp < 0 || alphaExp >= 12)  return false;
    const int     pos      = 11 - alphaExp;

    /* Cross-check with S2. */
    if (gfMul(locRatio, syn[1]) != syn[2]) return false;

    const uint8_t e_val = gfDiv(syn[0], gfExp[alphaExp]);
    cw[pos] ^= e_val;
    return true;
}


/* ====================================================================== */
/*           Voice-LC CRC: ETSI RS(12,9) + 0x96 0x96 0x96 mask            */
/* ====================================================================== */
/* The 9-byte LC followed by the 3 RS check bytes XORed with the burst-
 * type mask (0x96 for Voice LC Header) is a valid RS(12,9) codeword. A
 * stock DMR receiver runs the inverse and rejects bursts that fail.
 *
 * Encode side: append 3 mask-XORed RS check bytes.
 * Decode side: XOR mask off the 3 trailing bytes, then run RS decode. */
void lc_crc_apply(uint8_t lc12[12]) {
    /* lc12[0..8] hold the 9 LC bytes; the next 3 will hold the masked
     * parity. */
    uint8_t parity[3];
    rs_12_9_encode(lc12, parity);
    lc12[9]  = parity[0] ^ RS_MASK_VOICE_LC[0];
    lc12[10] = parity[1] ^ RS_MASK_VOICE_LC[1];
    lc12[11] = parity[2] ^ RS_MASK_VOICE_LC[2];
}

bool lc_crc_check(uint8_t lc12[12]) {
    /* In-place: XOR the mask off, run RS decode (corrects ≤ 1 byte
     * error), then mask back on so the caller can tell that this is
     * the validated form. */
    lc12[9]  ^= RS_MASK_VOICE_LC[0];
    lc12[10] ^= RS_MASK_VOICE_LC[1];
    lc12[11] ^= RS_MASK_VOICE_LC[2];
    const bool ok = rs_12_9_decode(lc12);
    /* Re-mask so the caller's view of lc12 stays consistent. */
    lc12[9]  ^= RS_MASK_VOICE_LC[0];
    lc12[10] ^= RS_MASK_VOICE_LC[1];
    lc12[11] ^= RS_MASK_VOICE_LC[2];
    return ok;
}

/* ====================================================================== */
/*                             LC pack / unpack                           */
/* ====================================================================== */
/* 72-bit Full LC layout (byte 0 bit 7 = MSB of bit 0):
 *   bit  0     PF (Protect Flag)            -> 0
 *   bit  1     Reserved                     -> 0
 *   bits 2-7   FLCO (6)                     -> 0 group / 3 private
 *   bits 8-15  FID (8)                      -> 0 standard
 *   bits 16-23 Service Options (8)          -> 0
 *   bits 24-47 Destination Address (24)
 *   bits 48-71 Source Address (24)
 *
 * Followed by 24 bits of CRC over those 9 bytes => 12 bytes / 96 bits. */

void lc_pack(uint8_t flco, uint32_t dstId, uint32_t srcId, uint8_t out[12]) {
    out[0] = (uint8_t)(flco & 0x3F);     /* PF=0, Reserved=0, FLCO */
    out[1] = 0;                          /* FID = standard */
    out[2] = 0;                          /* Service Options */
    out[3] = (uint8_t)(dstId >> 16);
    out[4] = (uint8_t)(dstId >> 8);
    out[5] = (uint8_t)(dstId >> 0);
    out[6] = (uint8_t)(srcId >> 16);
    out[7] = (uint8_t)(srcId >> 8);
    out[8] = (uint8_t)(srcId >> 0);
    /* Append the ETSI RS(12,9) + 0x96 mask check trailer in [9..11]. */
    lc_crc_apply(out);
}

bool lc_unpack(const uint8_t in[12], uint8_t *flco, uint32_t *dstId, uint32_t *srcId) {
    uint8_t lc[12];
    memcpy(lc, in, 12);
    if (!lc_crc_check(lc)) return false;
    *flco = (uint8_t)(lc[0] & 0x3F);
    *dstId = ((uint32_t)lc[3] << 16) | ((uint32_t)lc[4] << 8) | lc[5];
    *srcId = ((uint32_t)lc[6] << 16) | ((uint32_t)lc[7] << 8) | lc[8];
    return true;
}

/* ====================================================================== */
/*                            Golay(23, 12)                               */
/* ====================================================================== */
/* Perfect binary cyclic code, corrects up to 3 bit errors per 23-bit
 * codeword. Generator polynomial g(x) = x^11 + x^9 + x^7 + x^6 + x^5 + x + 1
 * = 0xAE3 (12 bits). Systematic encoding: codeword = data*x^11 +
 * (data*x^11 mod g(x)), so data sits in the top 12 bits, parity in the
 * bottom 11 bits. */

constexpr uint32_t GOLAY_23_12_POLY = 0xAE3u;     /* g(x), 12 bits */

uint16_t golay_23_12_syndrome(uint32_t cw) {
    uint32_t x = cw;
    for (int i = 22; i >= 11; --i) {
        if (x & (1u << i)) x ^= GOLAY_23_12_POLY << (i - 11);
    }
    return (uint16_t)(x & 0x7FFu);
}

uint32_t golay_23_12_encode(uint16_t data) {
    uint32_t cw = ((uint32_t)(data & 0xFFFu)) << 11;
    return cw | golay_23_12_syndrome(cw);
}

/* Syndrome → most-likely-error-pattern table (lazy init).
 * 2048 entries × 32-bit pattern = 8 KB static.
 * Each syndrome corresponds to a unique error pattern of weight ≤ 3
 * (the perfect-code property of Golay(23,12)). */
uint32_t  golayErrorTable[2048];
bool      golayTableReady = false;

void golay_23_12_init(void) {
    if (golayTableReady) return;
    for (int i = 0; i < 2048; ++i) golayErrorTable[i] = 0;
    /* weight-1 errors */
    for (int a = 0; a < 23; ++a) {
        const uint32_t err = 1u << a;
        golayErrorTable[golay_23_12_syndrome(err)] = err;
    }
    /* weight-2 */
    for (int a = 0; a < 23; ++a) {
        for (int b = a + 1; b < 23; ++b) {
            const uint32_t err = (1u << a) | (1u << b);
            golayErrorTable[golay_23_12_syndrome(err)] = err;
        }
    }
    /* weight-3 */
    for (int a = 0; a < 23; ++a) {
        for (int b = a + 1; b < 23; ++b) {
            for (int c = b + 1; c < 23; ++c) {
                const uint32_t err = (1u << a) | (1u << b) | (1u << c);
                golayErrorTable[golay_23_12_syndrome(err)] = err;
            }
        }
    }
    golayTableReady = true;
}

uint16_t golay_23_12_decode(uint32_t cw) {
    golay_23_12_init();
    const uint16_t syn = golay_23_12_syndrome(cw);
    const uint32_t err = golayErrorTable[syn];
    const uint32_t corrected = cw ^ err;
    return (uint16_t)((corrected >> 11) & 0xFFFu);
}

/* ====================================================================== */
/*                Golay (24, 12) and shortened (20, 8)                    */
/* ====================================================================== */
/* Used by the Slot Type field on data-style bursts (Voice LC Header,
 * Terminator with LC, etc.). The 8-bit Slot Type is shortened from the
 * (24,12) Golay code by zero-padding the top 4 info bits, encoding to
 * 24 bits, and dropping those 4 known-zero info-bit positions to leave
 * 20 bits.
 *
 * Generator polynomial g(x) = x^12 + x^11 + x^10 + x^9 + x^8 + x^5 + x^2 + 1
 *                           = 0xC75. (24,12) corrects up to 3 bit errors. */

constexpr uint16_t GOLAY_24_12_POLY = 0xC75u;     /* g(x), 12 bits, leading
                                                     term x^11 at bit 11 */

uint16_t golay_24_12_syndrome(uint32_t cw) {
    /* Polynomial long division. The polynomial's leading bit is at
     * position 11 (since 0xC75 = degree-11), so shifting by (i - 11)
     * aligns it with codeword bit i. */
    uint32_t x = cw;
    for (int i = 23; i >= 12; --i) {
        if (x & (1u << i)) x ^= (uint32_t)GOLAY_24_12_POLY << (i - 11);
    }
    return (uint16_t)(x & 0xFFFu);
}

uint32_t golay_24_12_encode(uint16_t data) {
    /* Codeword: top 12 bits = data, low 12 bits = parity (data*x^12 mod g). */
    uint32_t cw = ((uint32_t)(data & 0xFFFu)) << 12;
    return cw | golay_24_12_syndrome(cw);
}

/* Lazy 4 KB syndrome table: every 12-bit syndrome maps to its weight-≤3
 * error pattern (weight-4 patterns share syndromes and become
 * uncorrectable, but the table at least picks one weight-≤3 candidate
 * for the closest-codeword guess). */
uint32_t golay24Err[4096];
bool     golay24TableReady = false;

void golay_24_12_init(void) {
    if (golay24TableReady) return;
    for (int i = 0; i < 4096; ++i) golay24Err[i] = 0;
    for (int a = 0; a < 24; ++a) {
        const uint32_t e = 1u << a;
        golay24Err[golay_24_12_syndrome(e)] = e;
    }
    for (int a = 0; a < 24; ++a)
        for (int b = a + 1; b < 24; ++b) {
            const uint32_t e = (1u << a) | (1u << b);
            golay24Err[golay_24_12_syndrome(e)] = e;
        }
    for (int a = 0; a < 24; ++a)
        for (int b = a + 1; b < 24; ++b)
            for (int c = b + 1; c < 24; ++c) {
                const uint32_t e = (1u << a) | (1u << b) | (1u << c);
                golay24Err[golay_24_12_syndrome(e)] = e;
            }
    golay24TableReady = true;
}

uint16_t golay_24_12_decode(uint32_t cw) {
    golay_24_12_init();
    const uint16_t syn = golay_24_12_syndrome(cw);
    const uint32_t err = golay24Err[syn];
    return (uint16_t)(((cw ^ err) >> 12) & 0xFFFu);
}

/* Shortened (20,8): pad input to 12 bits with 4 leading zeros, encode
 * with (24,12), drop the 4 zero-padded info-bit positions (bits 23..20
 * of the 24-bit codeword) to get a 20-bit codeword.
 *
 * The decoder needs its own syndrome table -- if we use the full (24,12)
 * table, syndromes that would correct an error at positions 20..23 (the
 * padded zeros, which can't actually be wrong on the wire) end up
 * mis-correcting genuine weight-3 errors in the 20-bit domain. We
 * restrict the lookup to weight-≤3 patterns at positions 0..19 only. */

uint32_t golay20Err[4096];
bool     golay20TableReady = false;

void golay_20_8_init(void) {
    if (golay20TableReady) return;
    for (int i = 0; i < 4096; ++i) golay20Err[i] = 0;
    for (int a = 0; a < 20; ++a) {
        const uint32_t e = 1u << a;
        golay20Err[golay_24_12_syndrome(e)] = e;
    }
    for (int a = 0; a < 20; ++a)
        for (int b = a + 1; b < 20; ++b) {
            const uint32_t e = (1u << a) | (1u << b);
            golay20Err[golay_24_12_syndrome(e)] = e;
        }
    for (int a = 0; a < 20; ++a)
        for (int b = a + 1; b < 20; ++b)
            for (int c = b + 1; c < 20; ++c) {
                const uint32_t e = (1u << a) | (1u << b) | (1u << c);
                golay20Err[golay_24_12_syndrome(e)] = e;
            }
    golay20TableReady = true;
}

uint32_t golay_20_8_encode(uint8_t data) {
    const uint32_t cw24 = golay_24_12_encode((uint16_t)data);
    return cw24 & 0xFFFFFu;            /* keep low 20 bits */
}

uint8_t golay_20_8_decode(uint32_t cw20) {
    golay_20_8_init();
    const uint32_t cw24 = cw20 & 0xFFFFFu;
    const uint16_t syn  = golay_24_12_syndrome(cw24);
    const uint32_t err  = golay20Err[syn];
    return (uint8_t)(((cw24 ^ err) >> 12) & 0xFFu);
}

/* ====================================================================== */
/*                     Hamming (11, 7) and BPTC(128, 77)                  */
/* ====================================================================== */
/* Hamming(11,7) is just (15,11) shortened to 11 codeword bits. Same
 * generator polynomial g(x) = x⁴ + x + 1 = 0x13. Used as the column code
 * inside BPTC(128, 77), the FEC that protects the embedded LC carried
 * 32 bits at a time across voice frames A..D. */

uint8_t hamming_11_7_parity(uint16_t data) {
    uint16_t cw = (uint16_t)((data & 0x7Fu) << 4);
    for (int i = 10; i >= 4; --i) {
        if (cw & (uint16_t)(1u << i)) cw ^= (uint16_t)(0x13u << (i - 4));
    }
    return (uint8_t)(cw & 0xFu);
}

uint16_t hamming_11_7_encode(uint16_t data) {
    return (uint16_t)(((data & 0x7Fu) << 4) | hamming_11_7_parity(data));
}

/* Decode + 1-bit-correct. */
bool hamming_11_7_decode(uint16_t cw, uint16_t *out) {
    auto syn = [](uint16_t x) {
        for (int i = 10; i >= 4; --i)
            if (x & (uint16_t)(1u << i)) x ^= (uint16_t)(0x13u << (i - 4));
        return (uint8_t)(x & 0xFu);
    };
    if (syn(cw) == 0) {
        *out = (uint16_t)((cw >> 4) & 0x7Fu);
        return true;
    }
    for (int b = 0; b < 11; ++b) {
        const uint16_t test = (uint16_t)(cw ^ (1u << b));
        if (syn(test) == 0) {
            *out = (uint16_t)((test >> 4) & 0x7Fu);
            return true;
        }
    }
    *out = (uint16_t)((cw >> 4) & 0x7Fu);
    return false;
}

/* BPTC(128, 77): 77 info bits in a 7-row × 11-column matrix; column
 * Hamming(11,7) adds 4 parity rows for a final 11×11 = 121-cell matrix.
 * Plus 7 leading reserved (zero) bits R(0..6) → 128-bit codeword.
 *
 * Used by DMR's Embedded Link Control: 72-bit LC + 5-bit CRC = 77 bits
 * info, BPTC-protected, transmitted 32 bits at a time in voice frames
 * A..D over the course of one 360 ms superframe. */

constexpr int BPTC_128_77_OUT_BYTES = 16;     /* 128 bits */
constexpr int BPTC_128_77_INFO_BYTES = 10;    /* 77 bits, MSB-aligned */

void bptc_128_77_encode(const uint8_t info[BPTC_128_77_INFO_BYTES],
                        uint8_t        out[BPTC_128_77_OUT_BYTES]) {
    uint8_t M[11][11];
    memset(M, 0, sizeof(M));

    /* Pour 77 info bits into the top 7 rows × 11 cols. */
    int idx = 0;
    for (int r = 0; r < 7; ++r) {
        for (int c = 0; c < 11; ++c) {
            M[r][c] = (uint8_t)(bitGet(info, idx) ? 1 : 0);
            ++idx;
        }
    }

    /* Column Hamming(11, 7) parity into rows 7..10. */
    for (int c = 0; c < 11; ++c) {
        uint16_t data = 0;
        for (int r = 0; r < 7; ++r) data |= (uint16_t)(M[r][c] & 1) << (6 - r);
        const uint8_t p = hamming_11_7_parity(data);
        M[7][c]  = (uint8_t)((p >> 3) & 1);
        M[8][c]  = (uint8_t)((p >> 2) & 1);
        M[9][c]  = (uint8_t)((p >> 1) & 1);
        M[10][c] = (uint8_t)((p >> 0) & 1);
    }

    /* Output: 7 R bits at bit positions 0..6 (zero), then 121 matrix bits
     * row-major starting at bit 7. */
    memset(out, 0, BPTC_128_77_OUT_BYTES);
    int bitIdx = 7;
    for (int r = 0; r < 11; ++r) {
        for (int c = 0; c < 11; ++c) {
            bitSet(out, bitIdx, M[r][c] != 0);
            ++bitIdx;
        }
    }
}

bool bptc_128_77_decode(const uint8_t in[BPTC_128_77_OUT_BYTES],
                        uint8_t       out[BPTC_128_77_INFO_BYTES]) {
    /* Reconstruct the 11×11 matrix from bits 7..127. */
    uint8_t M[11][11];
    int bitIdx = 7;
    for (int r = 0; r < 11; ++r) {
        for (int c = 0; c < 11; ++c) {
            M[r][c] = (uint8_t)(bitGet(in, bitIdx) ? 1 : 0);
            ++bitIdx;
        }
    }

    /* Column Hamming(11, 7) decode + correct (single-bit per column). */
    auto h11_7_syn = [](uint16_t x) {
        for (int i = 10; i >= 4; --i)
            if (x & (uint16_t)(1u << i)) x ^= (uint16_t)(0x13u << (i - 4));
        return (uint16_t)(x & 0xFu);
    };
    for (int c = 0; c < 11; ++c) {
        uint16_t cw = 0;
        for (int r = 0; r < 11; ++r) cw |= (uint16_t)(M[r][c] & 1) << (10 - r);
        uint16_t data;
        if (h11_7_syn(cw) != 0) ++fecCounter;
        hamming_11_7_decode(cw, &data);
        for (int r = 0; r < 7; ++r) M[r][c] = (uint8_t)((data >> (6 - r)) & 1);
    }

    /* Extract the 77 info bits from the top 7 rows. */
    memset(out, 0, BPTC_128_77_INFO_BYTES);
    int idx = 0;
    for (int r = 0; r < 7; ++r) {
        for (int c = 0; c < 11; ++c) {
            bitSet(out, idx, M[r][c] != 0);
            ++idx;
        }
    }
    return true;
}

/* ====================================================================== */
/*                            QR (16, 7)                                  */
/* ====================================================================== */
/* The Quadratic Residue (16, 7) code protects DMR's EMB header (Color
 * Code + PI + LCSS) per ETSI TS 102 361-1 Annex B.3.5. Used here as a
 * cyclic systematic (16, 7) code with degree-9 generator polynomial
 *     g(x) = x⁹ + x⁷ + x⁵ + x² + x + 1   (= 0x2A7)
 * which gives minimum distance 6 -- correcting all single- and double-
 * bit errors. The 7 info bits sit in codeword bits 15..9 and 9 parity
 * bits in 8..0. The decoder brute-forces the 137 weight-≤2 error
 * patterns (16 + 120 trials worst case); under a distance-6 code each
 * pattern has a unique syndrome.
 *
 * The polynomial was selected by exhaustive search over all degree-9
 * polynomials and verified to give 2 048/2 048 single-bit and
 * 15 360/15 360 double-bit error-correction success on a host harness. */

constexpr uint16_t QR_16_7_POLY = 0x2A7u;     /* g(x), 10 bits, distance 6 */

uint16_t qr_16_7_syndrome(uint16_t cw) {
    /* Polynomial long division. Leading bit at position 9 in g(x), so
     * shifting g by (i - 9) aligns it with codeword bit i. */
    uint16_t x = cw;
    for (int i = 15; i >= 9; --i) {
        if (x & (uint16_t)(1u << i)) x ^= (uint16_t)(QR_16_7_POLY << (i - 9));
    }
    return (uint16_t)(x & 0x1FFu);   /* 9-bit remainder */
}

uint16_t qr_16_7_encode(uint8_t info7) {
    const uint16_t cw = (uint16_t)((info7 & 0x7Fu) << 9);
    return (uint16_t)(cw | qr_16_7_syndrome(cw));
}

/* Decode + 2-bit-correct. Returns the recovered 7-bit info field. */
uint8_t qr_16_7_decode(uint16_t cw) {
    if (qr_16_7_syndrome(cw) == 0) {
        return (uint8_t)((cw >> 9) & 0x7Fu);
    }
    /* Single-bit errors: 16 trials. */
    for (int b = 0; b < 16; ++b) {
        const uint16_t test = (uint16_t)(cw ^ (1u << b));
        if (qr_16_7_syndrome(test) == 0) {
            return (uint8_t)((test >> 9) & 0x7Fu);
        }
    }
    /* Two-bit errors: 16·15/2 = 120 trials. (16,7) QR is distance 5
     * (2-error correcting), so any weight-2 error has a unique
     * correctable pattern. */
    for (int b1 = 0; b1 < 16; ++b1) {
        for (int b2 = b1 + 1; b2 < 16; ++b2) {
            const uint16_t test = (uint16_t)(cw ^ (1u << b1) ^ (1u << b2));
            if (qr_16_7_syndrome(test) == 0) {
                return (uint8_t)((test >> 9) & 0x7Fu);
            }
        }
    }
    /* Uncorrectable -- return the raw info field. */
    return (uint8_t)((cw >> 9) & 0x7Fu);
}

/* ====================================================================== */
/*                      AMBE frame FEC (49 → 72 bits)                     */
/* ====================================================================== */
/* Per-frame layering, dropping the bottom 3 AMBE bits to fit 72 bits:
 *     u_0 = AMBE bits  0..11 (12)  → Golay(23,12)  → 23 bits
 *     u_1 = AMBE bits 12..23 (12)  → Golay(23,12)  → 23 bits
 *     u_2 = AMBE bits 24..34 (11)  → Hamming(15,11)→ 15 bits
 *     u_3 = AMBE bits 35..45 (11)  → unprotected   → 11 bits
 *                                                    -----
 *                                                    72 bits */

constexpr int AMBE_PROTECTED_BITS = 72;

void ambe_frame_encode(const uint8_t ambe[AMBE_DMR_BYTES_PER_FRAME],
                       uint8_t       outBits[9]) {
    /* Extract 4 fields from the 7-byte AMBE buffer. */
    auto take = [&](int bitStart, int n) {
        uint16_t v = 0;
        for (int i = 0; i < n; ++i) v = (uint16_t)((v << 1) | (uint16_t)bitGet(ambe, bitStart + i));
        return v;
    };
    const uint16_t u0 = take(0,  12);
    const uint16_t u1 = take(12, 12);
    const uint16_t u2 = take(24, 11);
    const uint16_t u3 = take(35, 11);

    const uint32_t g0 = golay_23_12_encode(u0);
    const uint32_t g1 = golay_23_12_encode(u1);
    const uint16_t h2 = hamming_15_11_encode(u2);

    memset(outBits, 0, 9);     /* up to 72 bits */
    int p = 0;
    /* Write the 23-bit Golay codeword MSB-first (bits at indices 22..0). */
    for (int i = 22; i >= 0; --i) bitSet(outBits, p++, ((g0 >> i) & 1) != 0);
    for (int i = 22; i >= 0; --i) bitSet(outBits, p++, ((g1 >> i) & 1) != 0);
    /* Hamming(15,11): 15 bits MSB-first. */
    for (int i = 14; i >= 0; --i) bitSet(outBits, p++, ((h2 >> i) & 1) != 0);
    /* u3 raw, 11 bits MSB-first. */
    for (int i = 10; i >= 0; --i) bitSet(outBits, p++, ((u3 >> i) & 1) != 0);
    /* p == 72 here. */
}

void ambe_frame_decode(const uint8_t inBits[9],
                       uint8_t       ambe[AMBE_DMR_BYTES_PER_FRAME]) {
    auto readField = [&](int start, int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) v = (v << 1) | (uint32_t)bitGet(inBits, start + i);
        return v;
    };
    const uint32_t g0  = readField(0,  23);
    const uint32_t g1  = readField(23, 23);
    const uint32_t h2c = readField(46, 15);
    const uint16_t u3  = (uint16_t)readField(61, 11);

    const uint16_t u0 = golay_23_12_decode(g0);
    const uint16_t u1 = golay_23_12_decode(g1);
    uint16_t       u2;
    hamming_15_11_decode((uint16_t)h2c, &u2);

    /* Repack the 49-bit AMBE frame (bits 46..48 lost — fill with 0). */
    memset(ambe, 0, AMBE_DMR_BYTES_PER_FRAME);
    auto put = [&](int start, uint16_t v, int n) {
        for (int i = 0; i < n; ++i) {
            const bool b = ((v >> (n - 1 - i)) & 1) != 0;
            bitSet(ambe, start + i, b);
        }
    };
    put(0,  u0, 12);
    put(12, u1, 12);
    put(24, u2, 11);
    put(35, u3, 11);
}

/* ====================================================================== */
/*               Voice burst assemble / disassemble (3 frames)            */
/* ====================================================================== */
/* Voice burst layout (264 bits = 33 bytes):
 *   bits   0..107  info first half  (108 bits)
 *   bits 108..155  SYNC             (48 bits, BS_VOICE_SYNC)
 *   bits 156..263  info second half (108 bits)
 *   total info = 216 bits = 3 × 72 bits
 *
 * Frame layout in the info area:
 *   Frame 0: 72 bits → info bits   0..71  → burst bits   0..71
 *   Frame 1: 72 bits → info bits  72..143 → burst bits  72..107
 *                                          + burst bits 156..191
 *                                          (straddles SYNC)
 *   Frame 2: 72 bits → info bits 144..215 → burst bits 192..263 */

void voiceBurstAssemble(const uint8_t encoded[DMR_AMBE_FRAMES_PER_BURST][9],
                        uint8_t        burst[33]) {
    memset(burst, 0, 33);
    /* SYNC */
    for (int i = 0; i < 48; ++i) bitSet(burst, 108 + i, bitGet(BS_VOICE_SYNC, i));
    /* Frame 0: bits 0..71 of the info area → burst[0..71] */
    for (int i = 0; i < AMBE_PROTECTED_BITS; ++i) bitSet(burst, i, bitGet(encoded[0], i));
    /* Frame 1: bits 72..107 → burst[72..107]; bits 108..143 → burst[156..191] */
    for (int i = 0; i < 36; ++i) bitSet(burst, 72  + i, bitGet(encoded[1], i));
    for (int i = 0; i < 36; ++i) bitSet(burst, 156 + i, bitGet(encoded[1], 36 + i));
    /* Frame 2: bits 144..215 → burst[192..263] */
    for (int i = 0; i < AMBE_PROTECTED_BITS; ++i) bitSet(burst, 192 + i, bitGet(encoded[2], i));
}

void voiceBurstDisassemble(const uint8_t burst[33],
                           uint8_t       encoded[DMR_AMBE_FRAMES_PER_BURST][9]) {
    for (int f = 0; f < DMR_AMBE_FRAMES_PER_BURST; ++f) memset(encoded[f], 0, 9);
    /* Frame 0 */
    for (int i = 0; i < AMBE_PROTECTED_BITS; ++i) bitSet(encoded[0], i, bitGet(burst, i));
    /* Frame 1 */
    for (int i = 0; i < 36; ++i) bitSet(encoded[1], i,        bitGet(burst, 72  + i));
    for (int i = 0; i < 36; ++i) bitSet(encoded[1], 36 + i,   bitGet(burst, 156 + i));
    /* Frame 2 */
    for (int i = 0; i < AMBE_PROTECTED_BITS; ++i) bitSet(encoded[2], i, bitGet(burst, 192 + i));
}

/* ====================================================================== */
/*                       Burst assemble / disassemble                     */
/* ====================================================================== */
/* Burst layout (264 bits = 33 bytes):
 *   bits   0..97   info first half  (BPTC bits   0..97)
 *   bits  98..107  slot type half 1 (Golay(20,8) codeword bits 19..10)
 *   bits 108..155  SYNC (48 bits = BS_VOICE_SYNC)
 *   bits 156..165  slot type half 2 (Golay(20,8) codeword bits  9..0)
 *   bits 166..263  info second half (BPTC bits 98..195)
 *
 * Slot Type byte: high nibble = Color Code (0..15), low nibble = Data
 * Type (0x1 = Voice LC Header, 0x2 = Terminator with LC, 0x3 = CSBK,
 * 0x6 = MBC Header, 0x9 = Rate-1/2 Data, 0xA = Rate-3/4 Data, 0xB = Idle,
 * 0xC = Rate-1 Data). The 8-bit value is encoded with the shortened
 * (20,8) Golay code and split MSB-first across the two halves of the
 * burst around the SYNC. */

constexpr uint8_t DT_VOICE_LC_HEADER   = 0x1;
constexpr uint8_t DT_TERMINATOR_WITH_LC = 0x2;

inline uint8_t buildSlotTypeByte(uint8_t colorCode, uint8_t dataType) {
    return (uint8_t)(((colorCode & 0xF) << 4) | (dataType & 0xF));
}

void writeSlotTypeIntoBurst(uint8_t burst[33], uint8_t slotTypeByte) {
    const uint32_t cw20 = golay_20_8_encode(slotTypeByte);
    /* MSB-first into bits 98..107, then 156..165. */
    for (int i = 0; i < 10; ++i) {
        bitSet(burst,  98 + i, ((cw20 >> (19 - i)) & 1) != 0);
        bitSet(burst, 156 + i, ((cw20 >> ( 9 - i)) & 1) != 0);
    }
}

uint8_t readSlotTypeFromBurst(const uint8_t burst[33]) {
    uint32_t cw20 = 0;
    for (int i = 0; i < 10; ++i) {
        cw20 |= ((uint32_t)bitGet(burst,  98 + i)) << (19 - i);
        cw20 |= ((uint32_t)bitGet(burst, 156 + i)) << ( 9 - i);
    }
    return golay_20_8_decode(cw20);
}

void burstAssemble(const uint8_t bptc[BPTC_OUT_BYTES],
                   uint8_t       slotTypeByte,
                   uint8_t       burst[33]) {
    memset(burst, 0, 33);
    /* Info halves (BPTC). */
    for (int i = 0; i < 98;  ++i) bitSet(burst, i, bitGet(bptc, i));
    for (int i = 0; i < 98;  ++i) bitSet(burst, 166 + i, bitGet(bptc, 98 + i));
    /* SYNC (48 bits). */
    for (int i = 0; i < 48;  ++i) bitSet(burst, 108 + i, bitGet(BS_VOICE_SYNC, i));
    /* Slot Type Golay(20,8) split around SYNC. */
    writeSlotTypeIntoBurst(burst, slotTypeByte);
}

void burstDisassemble(const uint8_t burst[33],
                      uint8_t       bptc[BPTC_OUT_BYTES]) {
    memset(bptc, 0, BPTC_OUT_BYTES);
    for (int i = 0; i < 98;  ++i) bitSet(bptc, i, bitGet(burst, i));
    for (int i = 0; i < 98;  ++i) bitSet(bptc, 98 + i, bitGet(burst, 166 + i));
}


/* ====================================================================== */
/*                       Embedded LC (EMB) pipeline                       */
/* ====================================================================== */
/* The 48-bit middle of a non-SYNC voice burst is laid out as:
 *     bits 108..115   EMB1 (8 bits)
 *     bits 116..147   embedded-data (32 bits)
 *     bits 148..155   EMB2 (8 bits)
 *
 * EMB1 || EMB2 forms a 16-bit field carrying:
 *     CC(4) | PI(1) | LCSS(2) | (9 parity bits via QR(16,7))
 *
 * LCSS values:
 *     00 = single-fragment embedded LC
 *     01 = first fragment of multi-fragment LC
 *     10 = last fragment
 *     11 = continuation fragment
 *
 * For a 4-fragment Voice LC across A..D, the LCSS sequence is
 * 01 → 11 → 11 → 10. On RX we accumulate the 32-bit data fragments,
 * then on the last-fragment burst run BPTC(128, 77) decode + 5-bit
 * CRC verify to recover the 72-bit LC.
 *
 * The 16-bit EMB header itself is QR(16,7)-encoded per ETSI -- distance
 * 5, 2-error correcting -- so a noisy channel can still recover Color
 * Code, PI, and LCSS for proper accumulator state tracking.
 *
 * The 5-bit CRC over the 72-bit LC uses g(x) = x⁵ + x² + 1 = 0x25 per
 * ETSI's reference; matched in this firmware by crc5_lc(). */

constexpr int   EMB_DATA_BITS_PER_FRAME = 32;
constexpr int   EMB_TOTAL_DATA_BITS     = 4 * EMB_DATA_BITS_PER_FRAME;  /* 128 */
constexpr uint8_t LCSS_SINGLE       = 0b00;
constexpr uint8_t LCSS_FIRST        = 0b01;
constexpr uint8_t LCSS_LAST         = 0b10;
constexpr uint8_t LCSS_CONTINUATION = 0b11;

/* Persistent EMB accumulator state (RX side). */
uint8_t embAcc[BPTC_128_77_OUT_BYTES] = {0};   /* assembled 128-bit codeword */
int     embAccBits     = 0;     /* number of EMB-data bits accumulated */
bool    embAccActive   = false; /* true between LCSS_FIRST and LCSS_LAST */

/* TX-side superframe state: voice-frame index advances 0..5 across
 * successive DmrTxBuildVoiceFrame calls. Frame F (idx=5) carries the
 * SYNC pattern in its middle; frames A..E (idx=0..4) carry an EMB
 * instead. The 128-bit BPTC codeword for the embedded LC is computed
 * once at DmrTxBuildLcHeaderBurst time and then split 32 bits at a
 * time across voice frames A..D each superframe. */
uint8_t embTxCodeword[BPTC_128_77_OUT_BYTES] = {0};
bool    embTxReady     = false;
uint8_t voiceFrameIdx  = 5;     /* next call to BuildVoiceFrame → A */

/* Emit the 16-bit EMB1+EMB2 field from a burst -- bits 108..115 in the
 * top half and 148..155 in the bottom half of the 16-bit value. */
uint16_t readEmbHeader(const uint8_t burst[33]) {
    uint16_t v = 0;
    for (int i = 0; i < 8; ++i) v |= ((uint16_t)bitGet(burst, 108 + i)) << (15 - i);
    for (int i = 0; i < 8; ++i) v |= ((uint16_t)bitGet(burst, 148 + i)) << ( 7 - i);
    return v;
}

/* Extract the 7 info bits from a 16-bit EMB header via QR(16,7) decode
 * (corrects up to 2 bit errors). */
inline uint8_t embHeaderInfoBits(uint16_t emb16) {
    return qr_16_7_decode(emb16);
}
inline uint8_t embHeaderColorCode(uint16_t emb16) { return (embHeaderInfoBits(emb16) >> 3) & 0xF; }
inline uint8_t embHeaderPI       (uint16_t emb16) { return (embHeaderInfoBits(emb16) >> 2) & 0x1; }
inline uint8_t embHeaderLCSS     (uint16_t emb16) { return  embHeaderInfoBits(emb16)       & 0x3; }

/* TX-side: overwrite a voice burst's middle 48 bits with the EMB
 * (replacing the SYNC pattern that voiceBurstAssemble wrote). For
 * frames A..D (frameIdx 0..3) the 32-bit data field carries the
 * corresponding fragment of the BPTC(128,77)-encoded embedded LC.
 * Frame E (frameIdx=4) holds zero data for now (the spec uses E for
 * embedded CRC + reverse channel; that lives in a follow-up). */
void writeEmbIntoBurst(uint8_t burst[33], uint8_t frameIdx) {
    /* 7-bit info: CC(4) | PI(1) | LCSS(2). The QR(16,7) parity is
     * stubbed (zeros in the low 9 bits) to match the RX-side reader. */
    const uint8_t cc = (uint8_t)(localCfg.colorCode & 0xF);
    const uint8_t pi = 0;
    uint8_t       lcss;
    switch (frameIdx) {
    case 0: lcss = LCSS_FIRST;        break;  /* A */
    case 1: lcss = LCSS_CONTINUATION; break;  /* B */
    case 2: lcss = LCSS_CONTINUATION; break;  /* C */
    case 3: lcss = LCSS_LAST;         break;  /* D */
    default: lcss = LCSS_SINGLE;      break;  /* E or unexpected */
    }
    const uint8_t  info7 = (uint8_t)((cc << 3) | (pi << 2) | (lcss & 0x3));
    const uint16_t emb16 = qr_16_7_encode(info7);    /* 7-bit info + 9-bit QR parity */

    /* EMB1 = bits 15..8 of emb16 → burst bits 108..115 (MSB first). */
    for (int i = 0; i < 8; ++i)
        bitSet(burst, 108 + i, ((emb16 >> (15 - i)) & 1) != 0);
    /* EMB2 = bits 7..0  of emb16 → burst bits 148..155 (MSB first). */
    for (int i = 0; i < 8; ++i)
        bitSet(burst, 148 + i, ((emb16 >> ( 7 - i)) & 1) != 0);

    /* Embedded data (32 bits) → burst bits 116..147. */
    if (embTxReady && frameIdx <= 3) {
        for (int i = 0; i < EMB_DATA_BITS_PER_FRAME; ++i) {
            const int srcBit = frameIdx * EMB_DATA_BITS_PER_FRAME + i;
            bitSet(burst, 116 + i, bitGet(embTxCodeword, srcBit));
        }
    } else {
        for (int i = 0; i < EMB_DATA_BITS_PER_FRAME; ++i)
            bitSet(burst, 116 + i, false);
    }
}

/* 5-bit CRC over the 72-bit LC, polynomial g(x) = x⁵ + x² + 1 = 0x25
 * per ETSI's reference for the embedded LC. */
uint8_t crc5_lc(const uint8_t lc72bits[10]) {
    uint8_t crc = 0;
    for (int i = 0; i < 72; ++i) {
        const uint8_t bit = bitGet(lc72bits, i) ? 1 : 0;
        const uint8_t fb  = ((crc >> 4) ^ bit) & 1;
        crc = (uint8_t)((crc << 1) & 0x1F);
        if (fb) crc ^= 0x05;
    }
    return crc & 0x1F;
}

/* Pre-encode the embedded LC for the current call. Called from
 * DmrTxBuildLcHeaderBurst so the same 128-bit codeword stays consistent
 * across the LC Header burst and every following voice burst until the
 * next call. */
void embTxPrepareForCurrentCall(void) {
    /* Pack 72-bit LC (same field layout as lc_pack but no RS+mask). */
    uint8_t lc[10] = {0};
    const uint8_t flco =
        (localCfg.callType == DmrCallGroup) ? FLCO_GROUP : FLCO_PRIVATE;
    lc[0] = (uint8_t)(flco & 0x3F);
    lc[1] = 0;
    lc[2] = 0;
    lc[3] = (uint8_t)(localCfg.dstId >> 16);
    lc[4] = (uint8_t)(localCfg.dstId >> 8);
    lc[5] = (uint8_t)(localCfg.dstId);
    lc[6] = (uint8_t)(localCfg.srcId >> 16);
    lc[7] = (uint8_t)(localCfg.srcId >> 8);
    lc[8] = (uint8_t)(localCfg.srcId);
    lc[9] = 0;

    /* Append 5-bit CRC into bits 7..3 of byte 9, leaving 0..2 reserved
     * (consistent with the RX path's parser). */
    const uint8_t crc = crc5_lc(lc);
    uint8_t info[10];
    memcpy(info, lc, 10);
    info[9] = (uint8_t)((crc & 0x1Fu) << 3);

    bptc_128_77_encode(info, embTxCodeword);
    embTxReady    = true;
    voiceFrameIdx = 5;     /* next BuildVoiceFrame → idx 0 = frame A */
}

/* Append 32 bits of embedded-data from the burst into the accumulator. */
void embAppendDataFromBurst(const uint8_t burst[33]) {
    for (int i = 0; i < EMB_DATA_BITS_PER_FRAME; ++i) {
        const bool b = bitGet(burst, 116 + i);
        if (embAccBits < EMB_TOTAL_DATA_BITS) {
            bitSet(embAcc, embAccBits, b);
            ++embAccBits;
        }
    }
}

}  // namespace

/* ====================================================================== */
/*                              Public API                                */
/* ====================================================================== */

void DmrConfigure(const DmrConfig *cfg) {
    if (cfg == nullptr) {
        configured = false;
        return;
    }
    localCfg   = *cfg;
    configured = true;
}

void SyncDmrConfigFromED(void) {
    /* Mirror the persistent ED fields into the live runtime config. */
    dmrConfig.enabled   = ED.dmrEnabled;
    dmrConfig.dstId     = (uint32_t)(ED.dmrTalkGroup & 0x00FFFFFF);
    dmrConfig.srcId     = (uint32_t)(ED.dmrSrcId     & 0x00FFFFFF);
    dmrConfig.timeSlot  = (ED.dmrTimeSlot == 2) ? DmrTimeSlot2 : DmrTimeSlot1;
    dmrConfig.colorCode = (uint8_t)(ED.dmrColorCode & 0x0F);
    dmrConfig.callType  = DmrCallGroup;     /* private-call UI lands later */
    DmrConfigure(&dmrConfig);
}

bool DmrShouldDeliver(DmrCallType type, uint32_t dstId) {
    if (!configured) return false;
    if (type == DmrCallPrivate) return dstId == localCfg.srcId;
    if (localCfg.rxTgCount == 0) return true;
    for (uint8_t i = 0; i < localCfg.rxTgCount && i < DMR_RX_TG_FILTER_MAX; ++i) {
        if (localCfg.rxTgFilter[i] == dstId) return true;
    }
    return false;
}

bool DmrSlotIsOurs(uint32_t nowMs) {
    if (!configured) return false;
    const uint32_t phaseMs   = nowMs % 60u;
    const bool     evenSlot  = (phaseMs < 30u);
    return (localCfg.timeSlot == DmrTimeSlot1) ? evenSlot : !evenSlot;
}

bool DmrSlotIsOursUs(uint32_t nowUs) {
    if (!configured) return false;

    /* Tier-II frame anchor: if the modem has heard a SYNC recently,
     * compute the slot phase relative to that timestamp instead of
     * free-running off the local clock. SYNC sits at burst symbol 77
     * within voice frame F, ≈ 16 ms into its 30 ms slot. We subtract
     * that offset so the anchor's "phase 0" coincides with the start
     * of frame F's slot, which is exactly the slot we want to call
     * "ours" (whichever TS the user has configured matches the one
     * they were already listening to). */
    constexpr uint32_t SYNC_OFFSET_INTO_SLOT_US = 16000u;

    uint32_t refUs = 0;
    uint32_t phaseUs;
    if (DmrModemGetSyncRef(&refUs)) {
        const uint32_t elapsed = nowUs - refUs;     /* unsigned wrap is fine */
        /* Shift by (60000 - SYNC_OFFSET_INTO_SLOT_US) so SYNC time maps
         * to the slot's mid-point as the network sees it. */
        phaseUs = (elapsed + (60000u - SYNC_OFFSET_INTO_SLOT_US)) % 60000u;
    } else {
        phaseUs = nowUs % 60000u;
    }
    const bool evenSlot = (phaseUs < 30000u);
    return (localCfg.timeSlot == DmrTimeSlot1) ? evenSlot : !evenSlot;
}

/* Voice burst: 3 AMBE+2 frames around the burst middle. Frame F (every
 * 6th burst per superframe) carries the SYNC pattern; frames A..E carry
 * an EMB instead. The 128-bit embedded-LC codeword pre-computed by
 * DmrTxBuildLcHeaderBurst is split across A..D every superframe so a
 * mid-call receiver can recover the addressing within 360 ms. */
bool DmrTxBuildVoiceFrame(const uint8_t ambeFrames[DMR_AMBE_FRAMES_PER_BURST][AMBE_DMR_BYTES_PER_FRAME],
                          uint8_t        outBurst[DMR_BURST_BYTES_MAX],
                          size_t        *outLen) {
    if (!configured || outBurst == nullptr || outLen == nullptr) return false;

    uint8_t encoded[DMR_AMBE_FRAMES_PER_BURST][9];
    for (int f = 0; f < DMR_AMBE_FRAMES_PER_BURST; ++f) {
        ambe_frame_encode(ambeFrames[f], encoded[f]);
    }

    uint8_t burst[33];
    voiceBurstAssemble(encoded, burst);   /* writes SYNC in middle */

    /* Advance the superframe index (A → B → C → D → E → F → A → …). */
    voiceFrameIdx = (uint8_t)((voiceFrameIdx + 1) % 6);

    /* For non-F frames, overwrite the SYNC area with the EMB. */
    if (voiceFrameIdx != 5) {
        writeEmbIntoBurst(burst, voiceFrameIdx);
    }

    memcpy(outBurst, burst, 33);
    *outLen = 33;
    return true;
}

bool DmrRxParseFrame(const uint8_t      *inBurst,
                     size_t              inLen,
                     uint8_t             outAmbeFrames[DMR_AMBE_FRAMES_PER_BURST][AMBE_DMR_BYTES_PER_FRAME],
                     DmrCallType        *outType,
                     uint32_t           *outSrcId,
                     uint32_t           *outDstId) {
    if (inBurst == nullptr || inLen < 33) return false;

    uint8_t encoded[DMR_AMBE_FRAMES_PER_BURST][9];
    voiceBurstDisassemble(inBurst, encoded);

    for (int f = 0; f < DMR_AMBE_FRAMES_PER_BURST; ++f) {
        ambe_frame_decode(encoded[f], outAmbeFrames[f]);
    }

    /* Voice bursts inherit addressing from the most recent LC header.
     * Without one we have nothing to deliver to the user-facing audio path. */
    if (!lastValid) return false;
    if (!DmrShouldDeliver(lastCallType, lastDstId)) return false;

    /* Bump activity timestamp so the home screen knows the call is
     * still live even between LC header re-sends. */
    lastSeenMs = millis();

    if (outType  != nullptr) *outType  = lastCallType;
    if (outSrcId != nullptr) *outSrcId = lastSrcId;
    if (outDstId != nullptr) *outDstId = lastDstId;
    return true;
}


void DmrEmbReset(void) {
    memset(embAcc, 0, sizeof(embAcc));
    embAccBits   = 0;
    embAccActive = false;
}

bool DmrRxParseEmbeddedLc(const uint8_t *burst,
                          DmrCallType   *outType,
                          uint32_t      *outSrcId,
                          uint32_t      *outDstId) {
    if (burst == nullptr) return false;

    const uint16_t emb16 = readEmbHeader(burst);
    const uint8_t  cc    = embHeaderColorCode(emb16);
    const uint8_t  lcss  = embHeaderLCSS(emb16);

    /* Reject foreign-network bursts when we have a configured CC. */
    if (configured && cc != (localCfg.colorCode & 0xF)) return false;

    switch (lcss) {
    case LCSS_FIRST:
        DmrEmbReset();
        embAccActive = true;
        embAppendDataFromBurst(burst);
        return false;
    case LCSS_CONTINUATION:
        if (!embAccActive) return false;
        embAppendDataFromBurst(burst);
        return false;
    case LCSS_LAST: {
        if (!embAccActive) return false;
        embAppendDataFromBurst(burst);

        /* Should now have 128 bits = 4 fragments × 32 bits. */
        if (embAccBits != EMB_TOTAL_DATA_BITS) {
            DmrEmbReset();
            return false;
        }

        /* BPTC(128, 77) decode → 77 info bits = 72 LC + 5 CRC. */
        const int fecBefore = fecCounter;
        uint8_t info[BPTC_128_77_INFO_BYTES];
        if (!bptc_128_77_decode(embAcc, info)) { DmrEmbReset(); return false; }
        lastBurstCorrections = fecCounter - fecBefore;

        /* Verify the 5-bit CRC. The 72 LC bits sit in info[0..8] (bits
         * 0..71); the 5-bit CRC sits in info[9] bits 7..3 (the top 5 bits
         * of byte 9 in MSB-first packing). */
        const uint8_t got = (uint8_t)((info[9] >> 3) & 0x1F);
        const uint8_t exp = crc5_lc(info);
        if (got != exp) { DmrEmbReset(); return false; }

        /* Unpack LC. Layout matches the Voice LC Header (lc_pack), but
         * without the RS+mask trailer. */
        const uint8_t  flco = (uint8_t)(info[0] & 0x3F);
        const uint32_t dstId = ((uint32_t)info[3] << 16)
                             | ((uint32_t)info[4] << 8)
                             | info[5];
        const uint32_t srcId = ((uint32_t)info[6] << 16)
                             | ((uint32_t)info[7] << 8)
                             | info[8];
        const DmrCallType ct = (flco == FLCO_GROUP) ? DmrCallGroup : DmrCallPrivate;

        /* Cache as if a Voice LC Header had just been parsed -- this is
         * the whole point of the embedded LC: addressing recovery
         * mid-call. */
        lastCallType = ct;
        lastSrcId    = srcId;
        lastDstId    = dstId;
        lastValid    = true;
        lastSeenMs   = millis();

        DmrEmbReset();

        if (!DmrShouldDeliver(ct, dstId)) return false;
        if (outType  != nullptr) *outType  = ct;
        if (outSrcId != nullptr) *outSrcId = srcId;
        if (outDstId != nullptr) *outDstId = dstId;
        return true;
    }
    case LCSS_SINGLE:
    default:
        /* Single-fragment LC and unknown values fall outside this MVP. */
        DmrEmbReset();
        return false;
    }
}

int DmrLastBurstCorrections(void) {
    return lastBurstCorrections;
}

bool DmrLastCallSeen(DmrCallType *outType,
                     uint32_t    *outSrcId,
                     uint32_t    *outDstId,
                     uint32_t    *outAgeMs) {
    if (!lastValid) {
        if (outAgeMs != nullptr) *outAgeMs = 0xFFFFFFFFu;
        return false;
    }
    if (outType  != nullptr) *outType  = lastCallType;
    if (outSrcId != nullptr) *outSrcId = lastSrcId;
    if (outDstId != nullptr) *outDstId = lastDstId;
    if (outAgeMs != nullptr) *outAgeMs = millis() - lastSeenMs;
    return true;
}

/* --- Voice LC header burst: this is the live path --- */

bool DmrTxBuildLcHeaderBurst(uint8_t outBurst[DMR_BURST_BYTES_MAX],
                             size_t *outLen) {
    if (!configured || outBurst == nullptr || outLen == nullptr) return false;

    uint8_t lc[12];
    const uint8_t flco =
        (localCfg.callType == DmrCallGroup) ? FLCO_GROUP : FLCO_PRIVATE;
    lc_pack(flco, localCfg.dstId, localCfg.srcId, lc);

    uint8_t bptc[BPTC_OUT_BYTES];
    bptc_196_96_encode(lc, bptc);

    /* Slot Type: Color Code in high nibble, Voice LC Header data type
     * (0x1) in low nibble. ETSI receivers check this to know what kind
     * of data burst they're looking at. */
    const uint8_t slotType = buildSlotTypeByte(localCfg.colorCode,
                                               DT_VOICE_LC_HEADER);

    uint8_t burst[33];
    burstAssemble(bptc, slotType, burst);

    memcpy(outBurst, burst, 33);
    *outLen = 33;

    /* Pre-encode the embedded LC for the upcoming voice bursts. The same
     * 128-bit BPTC codeword is split 32 bits at a time across voice
     * frames A..D each superframe; resetting voiceFrameIdx here aligns
     * the next BuildVoiceFrame call with frame A. */
    embTxPrepareForCurrentCall();
    return true;
}

bool DmrRxParseLcHeaderBurst(const uint8_t *inBurst,
                             size_t         inLen,
                             DmrCallType   *outType,
                             uint32_t      *outSrcId,
                             uint32_t      *outDstId) {
    if (inBurst == nullptr || inLen < 33) return false;
    const int fecBefore = fecCounter;

    /* Drop bursts whose Slot Type doesn't decode as a Voice LC Header
     * (data type 0x1). The Color Code field is checked against
     * dmrConfig.colorCode to filter out other networks sharing the
     * channel. */
    const uint8_t slotType = readSlotTypeFromBurst(inBurst);
    const uint8_t dt       = (uint8_t)(slotType & 0xF);
    const uint8_t cc       = (uint8_t)((slotType >> 4) & 0xF);
    if (dt != DT_VOICE_LC_HEADER) return false;
    if (configured && cc != (localCfg.colorCode & 0xF)) return false;

    uint8_t bptc[BPTC_OUT_BYTES];
    burstDisassemble(inBurst, bptc);

    uint8_t lc[12];
    if (!bptc_196_96_decode(bptc, lc)) return false;

    uint8_t  flco;
    uint32_t dstId, srcId;
    if (!lc_unpack(lc, &flco, &dstId, &srcId)) return false;

    const DmrCallType ct = (flco == FLCO_GROUP) ? DmrCallGroup : DmrCallPrivate;

    /* Cache the addressing so DmrRxParseFrame() and DmrRxParseEmbeddedLc()
     * can return it on subsequent voice bursts within the same call.
     * We cache regardless of whether the TG filter would deliver this
     * call to audio -- the filter is applied at delivery time, not at
     * parse time. */
    lastCallType = ct;
    lastSrcId    = srcId;
    lastDstId    = dstId;
    lastValid    = true;
    lastSeenMs   = millis();

    lastBurstCorrections = fecCounter - fecBefore;

    if (!DmrShouldDeliver(ct, dstId)) return false;

    if (outType  != nullptr) *outType  = ct;
    if (outSrcId != nullptr) *outSrcId = srcId;
    if (outDstId != nullptr) *outDstId = dstId;
    return true;
}
