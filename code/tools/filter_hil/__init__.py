"""Hardware-in-the-loop verification of the Phoenix receive DSP filters.

Injects known quadrature signals into the radio's I/Q receive inputs with an
Analog Discovery 2, measures the demodulated audio, and checks that each filter
lands on its labelled frequency at both supported sample rates.

See README.md for the wiring and how to run it.
"""

__all__ = ["ad2", "radio", "measure", "bandtable", "tests", "report"]
