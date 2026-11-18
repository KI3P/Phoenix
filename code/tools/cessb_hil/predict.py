"""What the offline model expects the radio to do with each stimulus.

Runs the stimulus through code/tools/cessb_model (the firmware's own transmit
filter coefficients plus the centred CESSB port) at the radio's sample rate,
and measures the result with the same :mod:`analysis` functions the bench
captures go through, so model and bench numbers are like for like.

Limits of the comparison: the model has no BandEQ (the firmware default is not
flat), no codec, no analog exciter. A gap between the two is therefore a
measurement of those, not necessarily a fault.
"""

from __future__ import annotations

import os
import sys

import numpy as np

_HERE = os.path.dirname(os.path.abspath(__file__))
_MODEL = os.path.join(os.path.dirname(_HERE), "cessb_model")
if _MODEL not in sys.path:
    sys.path.insert(0, _MODEL)

import cessb as model_cessb  # noqa: E402
import phoenix_tx as model_tx  # noqa: E402

from . import analysis as A  # noqa: E402
from .stimuli import Stimulus  # noqa: E402


def predict(stim: Stimulus, rate_hz: float, drives_db, seconds: float = 1.5) -> dict:
    """Model results for each drive, keyed by drive in dB.

    Levels follow the bench's definition: drive 0 dB puts the stimulus's linear
    peak envelope exactly on the ceiling.
    """
    flt = model_tx.TxFilters.for_rate(float(rate_hz))
    sign = model_tx.wanted_sign(flt)
    x = stim.at_rate(float(rate_hz), seconds)
    z = model_tx.mic_to_cessb_node(x, flt)
    skip = int(0.25 * flt.fs24)
    z = z[skip:] / np.max(np.abs(z[skip:]))
    ref = A.envelope_stats(model_tx.cessb_node_to_dac(z, flt), flt.fs, sign)
    out = {"reference": _stats_dict(ref)}
    for d in drives_db:
        y = model_cessb.cessb_c(z * 10 ** (d / 20), flt.fs24, sign)
        st = A.envelope_stats(model_tx.cessb_node_to_dac(y, flt), flt.fs, sign)
        row = _stats_dict(st)
        row["talk_power_gain_db"] = ref.wanted_papr_db - st.wanted_papr_db
        out[str(d)] = row
    return out


def _stats_dict(st: A.EnvelopeStats) -> dict:
    return {"peak": st.peak, "papr_db": st.papr_db, "wanted_papr_db": st.wanted_papr_db,
            "opposite_dbc": st.opposite_dbc, "out_of_channel_dbc": st.out_of_channel_dbc}
