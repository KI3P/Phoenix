"""Hardware-in-the-loop measurement of the Phoenix transmit DSP filters.

Companion to :mod:`filter_hil`, which does the same job for the receive chain.
The two suites share the CAT plumbing in ``filter_hil.radio`` and the scalar
curve-fitting helpers in ``filter_hil.measure``; everything else differs, because
the transmit chain is measured the other way round - one real tone in at the
microphone, a complex I/Q pair out at the exciter.
"""
