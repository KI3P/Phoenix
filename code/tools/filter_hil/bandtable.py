"""Design constants mirrored from the firmware.

The suite needs to know what the firmware is aiming at in order to say whether it
got there. These values are copied by hand from the sources named against each
block; ``test_filter_hil.py`` asserts the shapes still line up, but nothing can
detect a value that was edited in the firmware and not here. Re-check this file
whenever the corresponding firmware constants change.
"""

from __future__ import annotations

from dataclasses import dataclass

# --- Sample rates ----------------------------------------------------------
# Globals.cpp SR[] and the SAMPLE_RATE_* macros in SDT.h. Only these two are
# reachable from the front panel menu or the SR CAT command.
SAMPLE_RATES_HZ = (192000, 176400)

#: Combined decimation of the receive chain: DF1 * DF2 in ReceiveFilterConfig.
DECIMATION_FACTOR = 8

#: The ratio the frozen-table bug used to scale every corner by.
LEGACY_RATE_RATIO = 176400.0 / 192000.0          # 0.91875
LEGACY_DELTA_PCT = 100.0 * (LEGACY_RATE_RATIO - 1.0)   # -8.125


def audio_rate_hz(sample_rate_hz: float) -> float:
    """Audio sample rate after the receive chain's decimate-by-8."""
    return sample_rate_hz / DECIMATION_FACTOR


def fs_over_4_hz(sample_rate_hz: float) -> float:
    """The FreqShiftFs4 offset: an input tone at Fs/4 demodulates to DC."""
    return sample_rate_hz / 4.0


# --- CW audio filters ------------------------------------------------------
# DSP_FIR.cpp CW_AUDIO_RIPPLE_EDGE_HZ and the CW_AUDIO_FILTER_* macros.
# The nominal figures are what the filters are labelled with in the UI; the
# ripple edge is the Chebyshev design parameter and sits a few percent lower.
CW_FILTER_NOMINAL_HZ = (840.0, 1080.0, 1320.0, 1800.0, 2000.0)
CW_FILTER_RIPPLE_EDGE_HZ = (807.1, 1038.0, 1269.0, 1731.5, 1963.2)
CW_FILTER_ORDER = 12
CW_FILTER_RIPPLE_DB = 0.02
#: ED.CWFilterIndex value that bypasses the filter.
CW_FILTER_OFF = 5


# --- Equaliser -------------------------------------------------------------
# DSP_FIR.cpp EQ_BAND_FC_HZ. Q values are derived from EQ_BAND_PROTO: the
# cascade's -3 dB bandwidth, not any single section's.
EQ_CELL_COUNT = 14
EQ_CENTRE_HZ = (198.425, 250.0, 314.98, 400.0, 500.0, 630.0, 793.0,
                1000.0, 1259.0, 1587.0, 2000.0, 2500.0, 3150.0, 4000.0)
#: -3 dB bandwidth of each cell at 24 ksps, measured from the shipped tables.
EQ_BANDWIDTH_HZ = (49.4, 59.6, 76.0, 98.0, 118.5, 159.7, 189.0,
                   243.0, 300.8, 362.3, 476.3, 598.3, 789.9, 1043.5)

#: Cells whose measured shape is distorted by something other than the cell.
#: 0 and 1 sit at or below the 200 Hz SSB low cut; 13 sits in the decimation
#: skirt, whose corner is a fraction of Fs and so is meant to move with the rate.
EQ_EDGE_LIMITED_CELLS = (0, 1, 13)


def eq_q(index: int) -> float:
    """Nominal Q of an equaliser cell."""
    return EQ_CENTRE_HZ[index] / EQ_BANDWIDTH_HZ[index]


# --- AM DC blocker ---------------------------------------------------------
# ReceiveFilterConfig::amDCBlockCorner_Hz in SDT.h.
AM_DC_BLOCKER_CORNER_HZ = 38.0


# --- Bands -----------------------------------------------------------------
# Globals.cpp bands[], ITU region 2. Only the fields the suite uses.

@dataclass(frozen=True)
class Band:
    index: int
    name: str
    mode: int          # ModulationType: 0=USB 1=LSB 2=AM 3=SAM
    hi_cut_hz: int
    lo_cut_hz: int


BANDS: tuple[Band, ...] = (
    Band(0, "160M", 1, -200, -3000),
    Band(1, "80M", 1, -200, -3000),
    Band(2, "60M", 0, 3000, 200),
    Band(3, "40M", 1, -200, -3000),
    Band(4, "30M", 0, 3000, 200),
    Band(5, "20M", 0, 3000, 200),
    Band(6, "17M", 0, 3000, 200),
    Band(7, "15M", 0, 3000, 200),
    Band(8, "12M", 0, 3000, 200),
    Band(9, "10M", 0, 3000, 200),
    Band(10, "6M", 0, 3000, 200),
    Band(11, "4M", 0, 3000, 200),
)


def band(index: int) -> Band:
    """Look up a band by its ED.currentBand index."""
    if 0 <= index < len(BANDS):
        return BANDS[index]
    raise IndexError(f"band index {index} is outside the {len(BANDS)}-band table")
