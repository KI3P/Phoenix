"""Transmit-chain design constants mirrored from the firmware.

Copied by hand from the sources named against each block. ``test_tx_filter_hil.py``
checks the internal consistency of what is here, but nothing can detect a value
edited in the firmware and not here, so re-check this file whenever the transmit
filter design constants change.

The transmit chain, from ``TransmitProcessing`` in ``DSP.cpp``:

    mic  ->  TXDecimateBy4      Fs      -> Fs/4     anti-alias, fraction of Fs
         ->  TXDecimateBy2      Fs/4    -> Fs/8     anti-alias, fraction of Fs
         ->  BandEQ(TX)         Fs/8                14 bandpass cells, Hz-specified
         ->  TXGain                                 power-dependent scalar
         ->  Q := I                                 chain is mono from here
         ->  TXDecimateBy2Again Fs/8    -> Fs/16    TX_DECIMATE3_FC_HZ, Hz-specified
         ->  HilbertTransform   Fs/16              fixed 100-tap table
         ->  TXInterpolateBy2Again Fs/16 -> Fs/8    TX_AUDIO_LPF_FC_HZ, Hz-specified
         ->  ApplyIQCorrection, SidebandSelection
         ->  TXInterpolateBy2   Fs/8    -> Fs/4     anti-image, fraction of Fs
         ->  TXInterpolateBy4   Fs/4    -> Fs       anti-image, fraction of Fs
         ->  exciter I/Q DAC

Three of those stages are specified in **hertz** and so have to be regenerated
when the sample rate changes: the equaliser cells, ``TX_DECIMATE3_FC_HZ`` and
``TX_AUDIO_LPF_FC_HZ``. Everything else is specified as a fraction of Fs and is
*meant* to scale with the rate. That distinction is what this suite tests.
"""

from __future__ import annotations

# --- Sample rates ----------------------------------------------------------
# Globals.cpp SR[] and the SAMPLE_RATE_* macros in SDT.h. Only these two are
# reachable from the front panel menu or the SR CAT command.
SAMPLE_RATES_HZ = (192000, 176400)

#: ReceiveFilterConfig::DF. The transmit chain decimates to the same audio rate
#: the receive chain uses, and BandEQ runs there.
AUDIO_DECIMATION = 8

#: Further decimation into the Hilbert stage (TXDecimateBy2Again).
HILBERT_DECIMATION = 16

#: The ratio the frozen-table bug used to scale every Hz-specified corner by.
LEGACY_RATE_RATIO = 176400.0 / 192000.0                 # 0.91875
LEGACY_DELTA_PCT = 100.0 * (LEGACY_RATE_RATIO - 1.0)    # -8.125


def audio_rate_hz(sample_rate_hz: float) -> float:
    """Rate the equaliser and the transmit audio filters run at."""
    return sample_rate_hz / AUDIO_DECIMATION


def hilbert_rate_hz(sample_rate_hz: float) -> float:
    """Rate the Hilbert transform runs at: 12 ksps at 192 ksps."""
    return sample_rate_hz / HILBERT_DECIMATION


def fold_frequency_hz(sample_rate_hz: float) -> float:
    """Input frequency that maps to DC when TXDecimateBy2Again folds.

    ``TXDecimateBy2Again`` halves the audio rate, so its output Nyquist is
    ``audio_rate/4`` and an audio-rate component at ``f`` above that folds back
    to ``|audio_rate/2 - f|``. This returns that ``audio_rate/2``: 12 kHz at
    192 ksps, 11.025 kHz at 176.4 ksps.
    """
    return audio_rate_hz(sample_rate_hz) / 2.0


def nyquist_after_fold_hz(sample_rate_hz: float) -> float:
    """Highest frequency that survives the decimation without folding.

    6 kHz at 192 ksps, 5.5125 kHz at 176.4 ksps. ``TransmitChain176k.
    DecimatorStopsBeforeTheFoldPoint`` requires the decimator to be 60 dB down
    here.
    """
    return audio_rate_hz(sample_rate_hz) / 4.0


def alias_of_hz(f_in_hz: float, sample_rate_hz: float) -> float:
    """Where an input at f_in_hz lands after the decimate-by-2 folds it.

    Returns f_in_hz itself when it is below the fold point - nothing happens to
    it there.
    """
    fold = fold_frequency_hz(sample_rate_hz)
    if f_in_hz <= fold / 2.0:
        return float(f_in_hz)
    return float(abs(fold - f_in_hz))


# --- Transmit audio bandwidth ----------------------------------------------
# DSP_FIR.cpp. CalcFIRCoeffs takes its fc argument as the -6 dB point, so these
# are not the -3 dB figures the stages are described by.

#: -6 dB corner of TXInterpolateBy2Again's filter (FIR_int3_12ksps_48tap_2k7).
#: This is the stage that sets the transmitted audio bandwidth.
TX_AUDIO_LPF_FC_HZ = 3039.6

#: -6 dB corner of TXDecimateBy2Again's filter (coeffs12K_8K_LPF_FIR). Its job
#: is to stop anything folding when the rate halves; it sits above the audio
#: bandwidth so it costs no wanted signal.
TX_DECIMATE3_FC_HZ = 3500.0

#: Taps in each of the two generated transmit FIR stages.
GENERATED_FIR_TAPS = 48

#: -3 dB corner of the transmit audio path, measured offline through the real
#: stage functions by ``TransmitChain176k.AudioBandwidthHoldsAcrossRates`` in
#: code/test/TransmitChain_test.cpp, which asserts 2760 +/- 150 Hz.
#:
#: That measurement covers TXDecimateBy2Again, the Hilbert transform and
#: TXInterpolateBy2Again only. On real hardware the equaliser bank is also in
#: the path and cannot be taken out over CAT, so the measured corner can sit a
#: little below this. The absolute tolerance is loose for that reason; the rate
#: comparison is not, because the equaliser is regenerated per rate too.
TX_AUDIO_CORNER_3DB_HZ = 2760.0

#: How far the -3 dB corner moves between the two rates on a *correct* radio.
#:
#: Not zero, and this is the one number in this file that is easy to get wrong.
#: Both stages are 48-tap Kaiser-Bessel windowed sincs, and a 48-tap design does
#: not scale exactly when its normalised cutoff changes: the tap set is quantised
#: to the same 48 positions at both rates, so the realised corner lands in a
#: slightly different place. Evaluating the two generated tap sets directly
#: (Kaiser beta from a 90 dB stopband, cutoffs 3500 and 3039.6 Hz) gives a
#: cascade -3 dB corner of 2726 Hz at 24 ksps and 2759 Hz at 22.05 ksps: +1.2 %.
#:
#: ``TransmitChain176k.AudioBandwidthHoldsAcrossRates`` allows 2 % for the same
#: reason. A rate tolerance tighter than this is a tolerance on the design's own
#: reproducibility, not on the firmware, and would cry wolf. The bug being hunted
#: is still -8.125 %, so there is better than a factor of three in hand.
TX_AUDIO_CORNER_RATE_SPREAD_PCT = 1.2

#: Where the transmit audio filters are genuinely in their stopband.
#:
#: From the same tap-set evaluation, the cascade reaches -30 dB at 3.68 kHz,
#: -40 dB at 3.84 kHz and -52 dB by 4.0 kHz, beyond which it falls off a cliff
#: (better than 90 dB above 4.5 kHz). Below this frequency the response is on the
#: transition skirt, where demanding tens of decibels of rejection would be
#: demanding the wrong thing - at 3.45 kHz the cascade is only 20 dB down, and
#: correctly so.
TX_STOPBAND_FROM_HZ = 4000.0

#: Cascade attenuation predicted at TX_STOPBAND_FROM_HZ, worse rate. The
#: out-of-band check is set well inside this so it has margin against a
#: regression without running into the rig's own noise floor.
TX_STOPBAND_PREDICTED_DB = 52.0

#: Nominal -3 dB low corner of the transmit audio path.
#:
#: Nothing in the chain is a deliberate high pass. What rolls the bottom off is
#: the equaliser bank: ``BandEQ`` replaces the audio with the sum of 14 bandpass
#: cells, and the lowest sits at 198.4 Hz, so there is no reconstruction below
#: it. The microphone input's own AC coupling is in series with that and cannot
#: be separated, so this figure is a sanity bound rather than a specification.
TX_AUDIO_LOW_CORNER_HZ = 200.0


# --- Equaliser -------------------------------------------------------------
# DSP_FIR.cpp EQ_BAND_FC_HZ. The transmit and receive equalisers share one set
# of coefficient tables - DSP_FFT.cpp sets S_Xmt[i].pCoeffs and S_Rec[i].pCoeffs
# to the same array - and both run at the audio rate. The cells are summed with
# alternating signs, which reconstructs a roughly flat response from the lowest
# centre to the highest when every cell is at 100.
EQ_CELL_COUNT = 14
EQ_CENTRE_HZ = (198.425, 250.0, 314.98, 400.0, 500.0, 630.0, 793.0,
                1000.0, 1259.0, 1587.0, 2000.0, 2500.0, 3150.0, 4000.0)


# --- Hilbert transform -----------------------------------------------------
# DSP_FIR.cpp FIR_Hilbert_coeffs_45 / _neg_45, 100 taps, labelled "12K SPS
# BW 5400". This table is *not* regenerated per rate, and that is correct: a
# Hilbert transformer's usable band is a fraction of its sample rate, so a fixed
# table is the same design at any rate. Its band edges therefore move with Fs by
# design - which is why the sideband suppression checks below are expressed
# against a floor rather than against rate invariance in hertz.
HILBERT_TAPS = 100
HILBERT_NOMINAL_RATE_HZ = 12000.0
HILBERT_NOMINAL_BW_HZ = 5400.0


def hilbert_band_hz(sample_rate_hz: float) -> tuple[float, float]:
    """Approximate band over which the Hilbert pair holds quadrature.

    Scaled from the table's nominal 5.4 kHz bandwidth at 12 ksps. A Hilbert
    transformer is symmetric about the quarter-rate point, so the lower edge sits
    as far above DC as the upper edge sits below Nyquist.
    """
    fs = hilbert_rate_hz(sample_rate_hz)
    half_bw = 0.5 * HILBERT_NOMINAL_BW_HZ * fs / HILBERT_NOMINAL_RATE_HZ
    centre = fs / 4.0
    return centre - half_bw, centre + half_bw


# --- Modulation ------------------------------------------------------------
# ModulationType in SDT.h. SidebandSelection() inverts I for USB and leaves LSB
# alone, so these two are the only modulations the transmit chain distinguishes.
MOD_USB, MOD_LSB = 0, 1
