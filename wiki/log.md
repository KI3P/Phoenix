# Phoenix Wiki — Log

Append-only chronological record. Each entry: `## [YYYY-MM-DD] <op> | <subject>`
where `<op>` ∈ `ingest | query | lint | decision`.

Last 5 entries: `grep "^## \[" log.md | tail -5`

## [2026-06-08] decision | Wiki initialized
Created the Phoenix wiki skeleton (schema, index, log, and empty category folders:
firmware/, theory/, roadmap/, sources/, raw/). No sources ingested yet — schema-first
setup per the LLM Wiki pattern. Categories chosen: firmware, theory, roadmap.

## [2026-06-08] decision | Seeded firmware architecture map
Built out the firmware/ category from a survey of code/src/PhoenixSketch/ (source tree +
the PhoenixSketch.ino architecture header, Loop.h, SDT.h struct list, Contributors.txt).
Created 14 pages:
- overview (draft) — layered architecture, execution flow, full module map, SM table.
- code-heritage (draft) — DD4WH Teensy Convolution SDR lineage, 13-person contributor list,
  GPLv3.
- main-loop (draft) — loop() sequence, the 16-slot InterruptType FIFO, CW key/PTT plumbing.
- state-machine-architecture, real-time-constraints (concept stubs).
- Module stubs: mode/ui/tune/hardware state machines, rf-board, filter-boards, dsp-chain,
  display-subsystem, front-panel, cat-control, persistent-config.
All cross-linked; index.md firmware section populated. Discovered the real architecture is
much richer than CLAUDE.md states: 7 StateSmith machines (not 3), board-abstraction layer
(RF/BPF/LPF/Main), large display subsystem, AD7991 power sensing. Linked the existing
[[spectrum-refresh-floor]] memory finding into real-time-constraints + dsp-chain.
Stubs carry explicit "To flesh out" checklists; theory pages (ssb-generation,
fft-convolution-filtering, agc-design, noise-reduction-algorithms) referenced but not yet
written. No raw source ingested — this is a code-survey seeding, not an ingest.

## [2026-06-08] decision | Seeded DSP/SDR theory spine
Built the theory/ category from a code survey of DSP.cpp, DSP_FFT.cpp, and the SDT.h filter
structs. Confirmed and grounded (with file:line citations) the radio's defining DSP choices:
- I/O at 192 kHz, 2048-sample blocks; RX decimates ÷8 → 24 kHz, TX ÷16 → 12 kHz.
- SSB by the **phasing method**: TX uses a ±45° 100-tap Hilbert FIR pair
  (FIR_Hilbert_coeffs_45/_neg_45, DSP_FFT.cpp:851) + 1-line SidebandSelection sign flip; RX
  does sideband selection in the **frequency-domain convolution mask**, so Demodulate() just
  copies the real part for USB/LSB (DSP.cpp:629). NOT Weaver, NOT filter method.
- Channel filter = **FFT overlap-save fast convolution** (ConvolutionFilter, DSP_FFT.cpp:685;
  m_NumTaps = FFT_LENGTH/2+1 = 257) — the "Convolution SDR" namesake. Captured the
  static-DMAMEM cold-boot NaN hazard in the overlap buffers.
- Three NR algorithms (Kim / spectral / LMS-Xanr); wdsp-derived AGC (tau_attack 1ms / decay 250ms).
Created 9 pages: theory/overview + draft pages iq-quadrature-sampling, ssb-phasing-method,
fast-convolution-filtering; stubs multirate-decimation, iq-imbalance-correction, agc-design,
noise-reduction, zoom-fft. Convention adopted: code facts cited file:line, general DSP marked
"(general DSP)". Updated firmware/dsp-chain related-theory links and index.md theory section.
Established for the record: no electronic copy of the Purdum & Peter T41 book exists for this
project — theory is reconstructed from code + DSP fundamentals, not sourced from the book.

## [2026-06-08] decision | Promoted 3 theory stubs to drafts (IQ cal, multirate, NR)
Took iq-imbalance-correction, multirate-decimation, noise-reduction from stub → draft by
reading the implementations:
- IQ imbalance: ApplyIQCorrection (DSP.cpp:206) is a simplified single-axis model — scale I
  only, then IQPhaseCorrection (DSP.cpp:186) cross-mixes a fraction of one channel into the
  other (small-angle shear). Per-band factors in ED (SDT.h:302-305). Calibration
  (HardwareSm_ReceiveIQCalibration.cpp) injects a CW tone, measures sideband separation
  = psdnew[384]-psdnew[128], and runs a 3-pass coarse/medium/fine coordinate sweep
  (amp on even iters, phase on odd), re-centering each pass on the prior optimum. Added IRR
  vs amp/phase-error table. Carrier-null flagged as a distinct problem.
- Multirate: InitializeDecimationFilter (DSP_FIR.cpp:1284) sizes taps by the harris formula
  N≈A_stop/(22·Δf). Added worked single-÷8 (~132 taps) vs two-stage (~27+~33 taps) cost
  comparison showing why staging wins. Noted TX uses fixed 48-tap coeff sets while RX
  designs taps at runtime.
- NR: three families — Kim1_NR (256-pt FFT spectral subtraction, Hann, VAD-limited bins,
  ×30 post-scale), SpectralNoiseReduction (decision-directed MMSE / Ephraim-Malah; code
  cites Romanin 2009, Schmitt 2002, Gerkmann-Hendriks 2002), and Xanr (64-tap leaky-NLMS ALE
  that is both NR and auto-notch via prediction-vs-residual). Tied the NaN-latch / DMAMEM
  cold-boot robustness back to [[fast-convolution-filtering]].
Each page logs open questions (e.g. the "48 kHz" tone-offset comment vs the 6 kHz bins; the
DSP.cpp:687 Q→I 2× scale; NR sample-rate comment discrepancy). agc-design and zoom-fft remain
stubs. Updated index.md status markers.

## [2026-06-08] decision | Added roadmap/documentation-todos
Created roadmap/documentation-todos.md as the central planning surface for doc gaps and
code/comment discrepancies found while writing the wiki. Seeded it with the open questions
from the three theory drafts, the firmware/theory stub backlog, and pages-referenced-but-not-
written. Flagged one ⚠ possible bug: DSP.cpp:687 `arm_scale_f32(data->Q, 2, data->I, …)` after
the LMS NR call may be a Q/I channel mix-up. Linked from index.md roadmap section.

## [2026-06-08] decision | Completed theory spine (agc-design, zoom-fft drafts)
Promoted the last two theory stubs to drafts:
- agc-design: confirmed it's the wdsp (NR0V) look-ahead AGC ported via DD4WH — variable names
  match exactly. Documented the ring-buffer delay-line that lets attack clamp peaks with zero
  overshoot, complex-envelope magnitude (pmode=1), fast/hang/normal-decay state machine
  (DSP.cpp:368+), pop_ratio transient handling, and the 5 user modes (Off/Long/Slow/Med/Fast)
  with their hangtime/tau_decay (DSP.cpp:243-270). max_gain from per-band AGC_thresh.
- zoom-fft: corrected the earlier stub's error — the band-spectrum zoom FFT (ZoomFFTExe →
  psdnew) runs on the RAW 192 kHz I/Q, not the 24 kHz baseband; base bin width is 375 Hz, not
  46.875 Hz (that's the separate audio-passband FFT in ConvolutionFilter). Documented the
  IIR-biquad-then-decimate path (IIR chosen because phase doesn't matter for a magnitude PSD),
  zoomMultiplierCoeff amplitude compensation, and the ring-buffer accumulation for zoom>=8 that
  slows effective refresh — tying back to [[spectrum-refresh-floor]].
All 9 theory spine pages are now drafts. Added new discrepancies to roadmap/documentation-todos:
Config.h ZOOM 3 vs ED.spectrum_zoom=1 default; AGC soft-knee curve; n_tau vs MAX_N_TAU.

## [2026-06-08] decision | Investigated suspected NR bug (DSP.cpp:687) — false alarm, fixed wiki errors
Ran down the ⚠ possible bug flagged in roadmap/documentation-todos: the
`arm_scale_f32(data->Q, 2, data->I, …)` after the LMS NR call. VERDICT: not a bug.
- Xanr (DSP_Noise.cpp:302) reads data->I, writes its result to data->Q, never modifies I.
- Downstream InterpolateReceiveData (DSP.cpp:700) reads data->I as source.
- So line 687 correctly propagates the denoised Q back into I (with a 2× makeup gain, cf the
  ×30 for Kim). The commented-out `//arm_scale_f32(data->I,1.5,data->I,…)` is the abandoned
  buggy in-place variant. The notch block (DSP.cpp:930-933) uses the same Xanr→copy(Q,I)
  pattern. Downgraded to a clarity nit (document the "Xanr outputs to Q" gotcha).
Investigation also exposed errors in MY earlier theory drafts, now fixed:
- Real RX order is ConvolutionFilter → AGC → Demodulate → BandEQ → NoiseReduction
  (DSP.cpp:911-927). I had written "NR runs before AGC" in agc-design and noise-reduction —
  WRONG. AGC actually runs BEFORE demodulation, on the complex SSB-filtered baseband, using
  sqrt(I²+Q²) envelope; NR runs AFTER demod+EQ on the audio. Corrected both pages + the
  dsp-chain RX sketch (which was also mis-ordered).
- Resolved two open questions in noise-reduction: the Q→I scale (not a typo) and auto-notch
  location (Xanr(&data,1) at DSP.cpp:930, gated by ED.ANR_notchOn). Marked resolved in roadmap.
Lesson: verify chain ORDER against ReceiveProcessing directly, not the DSP.h header doc
(whose summary listing "AGC → filter → demodulate → noise reduction" is itself loosely
ordered).

## [2026-06-08] decision | Promoted mode-state-machine to full draft
Read ModeSm.h (state/event enums), Mode.cpp (actions/guards), and the dispatch sites + keyer
handling in Loop.cpp to document ModeSm accurately. Key facts:
- 20 states under ROOT split into NORMAL_STATES (SSB RX/TX, CW_RECEIVE, straight-key
  MARK/SPACE, iambic DIT_MARK/DAH_MARK/KEYER_SPACE/KEYER_WAIT) and CALIBRATION_STATES
  (frequency, offset MARK/SPACE, power MARK/SPACE, RX_IQ, TX_IQ MARK/SPACE). 16 events.
- Keyer timing: DO event from the 1 ms TimerInterrupt (Loop.cpp:355) increments
  markCount/spaceCount; dit=ditDuration_ms, dah=3×, inter-element space=1×;
  ditDuration_ms = 1200/WPM (Globals.cpp:131). Standard PARIS timing.
- Iambic paddle memory implemented in Loop.cpp:914-947 by PrependInterrupt-ing a paddle press
  to the FIFO head during an active element; keyerFlip swaps dit/dah.
- TX guarded by IsTxAllowed() = HAM_BAND only (Mode.cpp:39). Buttons locked out during TX
  (Loop.cpp:395-403). Mode changes run UpdateHardwareState → RF + audio reconfigure.
Found a stale-source-comment discrepancy: PhoenixSketch.ino lists ModeSm states as
"...CW_TRANSMIT, TUNE" — there is no TUNE state and CW transmit is six sub-states. Logged to
roadmap/documentation-todos. Located the StateSmith source diagrams (ModeSm.drawio et al.).
First firmware state-machine stub fully promoted; ui/tune/hardware SM stubs remain.

## [2026-06-08] decision | Retired "tune-state-machine" myth → tune-frequency-control
Reading Tune.cpp/Tune.h revealed there is NO TuneSm state machine: Tune.cpp is a pure-function
library (frequency math + PA power-cal math). The TuneReceive/TuneSSBTX/TuneCWTX "states" the
CLAUDE.md/.ino describe actually live in HardwareSm.cpp as a TuneState enum dispatched by
HandleTuneState(). Renamed the stub firmware/tune-state-machine.md → tune-frequency-control.md
and rewrote it as a module page:
- Two-stage tuning: hardware Si5351 VFO = coarse band-center; digital software mixer = fine
  tune; effective freq = centerFreq − fineTune − Fs/4 (GetTXRXFreq, Tune.cpp:121). Documented
  the −Fs/4 quarter-shift rationale (dodge the DC hole / 1-f / LO leakage; cheapest mix).
- Functions: GetTXRXFreq(_dHz), GetCWTXFreq_dHz (LSB−/USB+ tone offset), ResetTuning, GetBand;
  deci-Hertz units for Si5351 resolution. AdjustFineTune limits (zoom window, band edge) +
  FAST_TUNE acceleration (4 quick steps → 1000 Hz step).
- HandleTuneState (HardwareSm.cpp:639) is the real per-mode VFO dispatcher: sets PA/LPF/BPF/
  antenna/FIR mask then programs the VFO; SSB-TX dual-VFO sets RX to freq/3 to watch the 3rd
  harmonic for the TX spectrum display.
- PA power-cal math (also in Tune.cpp): tanh model Pout=Psat·tanh(k·10^(−A/10)),
  attenToPower/powerToAtten, CalculateCWAttenuation/CalculateSSBTXGain, Gauss-Newton
  fitTanhModel/FitPowerCurve — backs PowerCalSm.
Updated ALL inbound links (overview, state-machine-architecture, rf-board, filter-boards,
mode-state-machine, index) — overview SM table now correctly says SEVEN generated machines, no
TuneSm. Logged the TuneSm myth to roadmap stale-source-comments. Verified no dangling
[[tune-state-machine]] links remain.

## [2026-06-08] decision | Promoted hardware-state-machine to draft; corrected SM count
Read HardwareSm.h/.cpp + the 4 cal partials. Two structural corrections to the wiki:
- HardwareSm is HAND-WRITTEN, not StateSmith-generated (no HardwareSm.drawio, no banner).
  Only SIX machines are generated: ModeSm, UISm, PowerCalSm, ReceiveIQCalSm,
  TransmitCarrierCalSm, TransmitIQCalSm. Fixed the overview SM table ("6 generated + 1
  hand-written coordinator") and state-machine-architecture accordingly.
- HardwareSm is the mode→hardware bridge built on two hand-coded enums:
  RFHardwareState (RFReceive/RFTransmit/RFCWMark/RFCWSpace/RFCalReceiveIQ/RFCalTransmitIQ[
  SingleVFO]) and TuneState. UpdateRFHardwareState (HardwareSm.cpp:531) maps modeSM.state_id
  → RF state (many-to-one) then HandleRFHardwareStateChange does relay/path switching w/
  ~6 ms settling; UpdateTuneState → HandleTuneState programs PA/LPF/BPF/antenna/FIR + VFO.
  CW QSK uses the RFCWMark/RFCWSpace pair to avoid full T/R cycling; calibration reuses the
  normal TX/RX hardware states. previousRadioState guard + ForceUpdateRFHardwareState.
- Calibration orchestration: 4 hand-written partials each wrap a generated sub-SM, dispatching
  events (PowerCalSm_EventId_AUTO/NEXT_POINT/RECORD_DATA_POINT…) and bridging results into ED.
  Power-cal fits the tanh PA model (Tune.cpp); IQ uses the sideband-sep sweep.
Single funnel: UpdateRFHardwareState called from Mode.cpp:24 (every ModeSm entry) + Loop.cpp
on freq/band/mode changes. Logged the "HardwareSm is generated" myth to roadmap. Firmware SM
pages now all drafts except ui-state-machine.

## [2026-06-08] decision | Promoted ui-state-machine to draft (SM set complete)
Read UISm.h + the UISm.cpp dispatch/transitions + Loop.cpp dispatch sites. UISm = 13-state
screen/menu navigator (generated, UISm.drawio):
- SPLASH →[DO, splashCount≥splashDuration]→ HOME.
- Menu drill-down stack: HOME →MENU→ MAIN_MENU →SELECT→ SECONDARY_MENU →SELECT→ UPDATE
  →SELECT→ HOME; HOME event pops to HOME from any level.
- Tool screens hanging off HOME (leaf, return on HOME): FREQ_ENTRY, EQUALIZER, BIT, and the 4
  CALIBRATE_* screens.
- Events DO/MENU/SELECT/HOME/DFE/EQUALIZER/BIT/CALIBRATE_* all from Loop.cpp; button routing
  is state-aware (HandleButtonPress switches on uiSM.state_id) and locked out during TX.
- CALIBRATE_* is dual-dispatched to UISm (screen) AND ModeSm (hardware) — the sibling SMs.
Resolved the display-subsystem "what is DFE?" question: DFE = Direct Frequency Entry
(FREQ_ENTRY state, MainBoard_DisplayDFE). Updated display-subsystem + index + roadmap.
All 7 firmware state-machine pages (Mode, UI, Hardware coordinator, + the architecture/tune
pages) are now drafts; remaining firmware stubs are the board/IO modules (rf-board,
filter-boards, front-panel, cat-control, persistent-config, display-subsystem) and the 4
generated cal sub-SM internal state maps.

## [2026-06-08] decision | Promoted rf-board to draft (Si5351 quadrature VFO)
Read RFBoard.h (API) + RFBoard.cpp/RFBoard_si5351.cpp (impl). Documented the RF hardware-
abstraction layer:
- Three VFOs on one Si5351: RX = CLK0/CLK1 on PLLA, TX = CLK4/CLK5 on PLLB (independent freqs),
  CW = CLK6 (or CLK2 single-VFO). Single-VFO boards fall back SetTXVFO→SetRXVFO.
- Quadrature 90° generation, two regimes (SetRXVFOFrequency, RFBoard.cpp:483): ≥3.2 MHz uses
  the phase-offset register = multisynth divider ("multiple") for a digital 90° delay; <3.2 MHz
  uses the TJ-Labs timed-delay trick (set clocks 4 Hz low, wait ~58.5 ms = quarter beat, cli/sei
  for timing). EvenDivisor forces even dividers; PLL-only retune within a multiple range
  minimizes I2C per encoder step.
- Attenuators (TX/RX, 0-31.5 dB / 0.5 dB via MCP23017 I2C), VFO drive/enable, T/R relays,
  SSB/CW modulation routing, CW keying (CWon/CWoff), and EnableCalFeedback loopback for IQ cal.
- Frequency correction (ppb) from freq calibration.
Found a units discrepancy: RFBoard.h docstrings say VFO freq is "decihertz (Hz×10)" but the
real unit is Hz×100 (centi-Hz) per RFBoard.cpp:481 + SI5351_FREQ_MULT=100 + HandleTuneState
*100; the _dHz suffix is itself a centi-/deci- misnomer. Logged to roadmap units section.
Updated index/roadmap. Remaining firmware module stubs: filter-boards, front-panel, cat-control,
persistent-config, display-subsystem panes.

## [2026-06-08] decision | Promoted filter-boards to draft (BPF/LPF/back-end + SWR)
Read BPFBoard.h, LPFBoard.h, LPFBoard_AD7991.h + the SWR math in LPFBoard.cpp. Key findings:
- The "LPF board" is really the RF BACK-END: LPF harmonic suppression + BPF path routing +
  transverter (XVTR) + 100W PA select + antenna matrix (0-3) + SWR/power metering. BPF board
  is just RX front-end selectivity.
- Documented the shared 64-bit `hardwareRegister` model (Globals.cpp:112, bit map SDT.h:69-83):
  all three RF boards write bitfields into one register, flushed to MCP23017 GPIO expanders via
  buffer_add()/UpdateMCPRegisters(). This is a cross-cutting architectural detail.
- BPF band→relay uses a byte-swap encoding (BPF_WORD macro, full table in BPFBoard.h); bands
  0-10 + 15=BYPASS. LPF uses BandToBCD (4-bit BCD), bands 0-10.
- SWR/power (PerformSWRBridgeReading): AD7991 CH0=fwd/CH1=refl (4.096V ref) or analog pins
  26/27. Math: counts→V, P=(V·10)²/50 (20 dB coupler), |Γ|=√(Pr/Pf), SWR=(1+|Γ|)/(1−|Γ|), with
  guard rails. Feeds the display meter + PowerCalSm.
Open Qs logged: full LPF band↔BCD table, coupler 20 dB assumption / per-band power cal, and the
AD7991 REGISTER_SETUP channel-nibble=0 vs the CH0+CH1 comment. Updated index/roadmap.
Remaining firmware module stubs: front-panel, cat-control, persistent-config, display panes.

## [2026-06-08] decision | Promoted front-panel to draft (documents encoder-i2c-speed design)
Read FrontPanel.h/.cpp + FrontPanel_Rotary.h. Front panel = 2×MCP23017 on Wire1: mcp1 = 16
button switches, mcp2 = 6 switches + 4 rotary encoders + 2 LEDs. Encoders are Rotary_V12 (Ben
Buxton state machine adapted for MCP23017, per-encoder reversal, BOURN A/B-swap flag) → emit
iVOLUME/iFILTER/iCENTERTUNE/iFINETUNE ± events; buttons → iBUTTON_PRESSED + GetButton().
Documented the encoder-i2c-speed two-tier design (commit 6977fa7):
- GPIO ISR (fpInterrupt1/2ISR) sets a flag only — NO blocking I2C in the high-priority ISR.
- 1 ms IntervalTimer fpServiceTimer runs ServiceFrontPanel at NVIC priority 240, BELOW the
  OpenAudio update ISR (208), so encoder/button I2C can never delay audio. It does the deferred
  interrupt1()/interrupt2() I2C drain when a flag is set OR the INT pin still reads LOW (level
  recovery for missed edges). interrupt2 uses readGPIOAB() single-read optimization.
- Legacy CheckForFrontPanelInterrupts() (main-loop poll) remains as an idempotent backstop.
Rationale: decouples tuning responsiveness from the slow main-loop display/DSP cadence while
preserving audio via priority ordering — ties to real-time-constraints + spectrum-refresh-floor.
Caveat noted: all IntervalTimers share IRQ_PIT so priority(240) is global. Updated index/roadmap.
Remaining firmware module stubs: cat-control, persistent-config, display panes.

## [2026-06-08] decision | Promoted persistent-config to draft (ED struct + JSON storage)
Read SDT.h config_t/ED (264-331), ParamSave.h, Storage.h/.cpp. ED is one global config_t = the
single source of truth: ~50 fields (many per-band, NUMBER_OF_BANDS=13) spanning audio/DSP,
tuning, modulation/CW, spectrum/display, equalizer, power/PA, calibration (RX/TX IQ, carrier-
null DCOffset, atten, SWR cal), antenna. Documented field groups + which module owns each.
Persistence (Storage.cpp): NOT a raw struct dump — ED is serialized field-by-field as
ArduinoJson and written to LittleFS_Program (~1 MB Teensy program flash) + optional SD
(BUILTIN_SDCARD). API: InitializeStorage / SaveDataToStorage(savetosd) / RestoreDataFromStorage
/ RestoreDataFromSDCard / PrintEDToSerial. Field-named JSON = robust to struct-layout changes.
ParamSave.cpp is a separate small RAM scratch (5 var + 3 array slots ≤256B) = transient undo
buffer for calibration twiddling, not persistence. Config.h = compile-time constants; Globals.cpp
defines the externs (ED, SampleRate, hardwareRegister, SMs, SR[], bands[]).
Discrepancy logged: ED="EEPROM Data" / .ino says "EEPROM" but it's really LittleFS+SD JSON
(Teensy 4.1 has no true EEPROM) — added to roadmap stale-comments. Updated index/roadmap.
Remaining firmware stubs: cat-control, display panes.

## [2026-06-08] decision | Promoted cat-control to draft (Kenwood CAT command table)
Read CAT.h + CAT.cpp. CAT = Kenwood 2-letter command emulation over SerialUSB1 (2nd USB serial,
Dual Serial USB type) @ 38400 baud, ';'-terminated, polled by CheckForCATSerialEvents from the
main loop. Architecture = a 25-entry valid_command[] dispatch table {name, set_len, read_len,
write_fn, read_fn}; command_parser matches the 2-letter code and dispatches to write (set) vs
read (query) by length. Commands: FA/FB/FR/FT/FW (freq/VFO), MD/IF/ID (mode/status/id), BU/BD
(band), AG/MG/PC (gains/power), NR/NT/VX (DSP), KS (keyer WPM), TX/RX (PTT), AI/PS/PD/DB, plus
two CUSTOM non-Kenwood: ED (dump config) and PR (dump hardwareRegister). Apply paths: CAT feeds
the SAME channels as the front panel — writes enqueue InterruptType events (iUPDATE_TUNE on
freq, iBUTTON_PRESSED on band) AND/OR write ED directly (KS→ED.currentWPM); TX_write dispatches
to ModeSm. Reads serialize ED/hardware state back (IF = full status string).
Discrepancy logged: CAT.cpp says TS-480 but CAT.h says TS-2000 (docs has ts_480_pc.pdf) — added
to roadmap stale-comments. Updated index/roadmap. This was the last self-contained firmware
module stub; only display-pane internals remain.

## [2026-06-08] decision | Promoted display-subsystem to draft — firmware modules COMPLETE
Read MainBoard_Display.h + DisplayHome.cpp + the DrawDisplay router. Documented:
- Pane model: struct Pane {x,y,w,h, DrawFunction, stale}; dirty-flag rendering — a pane redraws
  only when stale=true (set by modules on data change), the efficiency mechanism for the 10ms
  budget. Home screen = WindowPanes[NUMBER_OF_PANES] = 13 panes (VFO A/B, Freq/Band/Mod,
  Spectrum, StateOfHealth, Time, SWR, TXRXStatus, SMeter, AudioSpectrum, Settings, NameBadge,
  SAMOffset) — enumerated with rects. (The .ino "12-pane" is approximate; actually 13.)
- DrawDisplay() (MainBoard_Display.cpp:244) = top-level render router switching on uiSM.state_id
  → per-screen draw fns. Confirms the UISm↔display mapping. Each non-home screen has its own
  pane set.
- VariableParameter type-safe editor (union i8..f32/bool/KeyTypeId + min/max/step) drives the
  UPDATE state; menus = primaryMenu[8] + SecondaryMenuOption (variableOption→editor or func ptr→
  action). Spectrum pane fed by zoom-fft psdnew; audio-spectrum by convolution-filter audioYPixel.
Resolved the "enumerate the 12 panes" open question. Updated index/roadmap.
MILESTONE: all self-contained firmware MODULE stubs are now drafts. Remaining wiki work is
deeper-dive content (4 cal sub-SM state maps) + unwritten theory/feature pages (CW processing,
equalizer, SAM detection, TX carrier-null).

## [2026-06-09] decision | Consolidated undone doc-work + seeded development backlog from GitHub issues
Two parts.
(1) Recorded remaining documentation work in roadmap/documentation-todos: added a status header
(all firmware module + 9 theory pages are drafts) and a consolidated "Not yet written" list —
CW processing, equalizer, SAM detection, TX carrier-null, BIT, and the 4 generated cal sub-SM
state maps (PowerCalSm/ReceiveIQCalSm/TransmitCarrierCalSm/TransmitIQCalSm) — plus a per-page
"Remaining gaps" list. Removed the now-duplicate "referenced but not yet created" section.
(2) Pulled the 4 open GitHub issues (KI3P/Phoenix, via REST API since gh not installed) and
created the roadmap development backlog: one scoping page per issue + an index. IMPORTANT framing
per the human: these document WHAT NEEDS DOING and current state, NOT solution designs.
- development-backlog.md (index: status/effort/links, two tracks).
- transmit-spectrum.md (#10): found it's LARGELY BUILT — TransmitReceiveProcessing() runs during
  SSB_TRANSMIT on dual-VFO (DSP.cpp:55) computing the PSD via ZoomFFTExe, and HandleTuneState
  tunes RX VFO to freq/3 to watch the 3rd harmonic. Gaps: verify rendering, CW-TX, single-VFO, UX.
- manual-notch.md (#11): auto-notch scaffolding exists (notch button toggles ANR_notchOn 0↔1,
  Xanr engine, display hook, CAT NT) but NO manual mode-2 path. Remaining: 3-way cycle, fixed-freq
  notch DSP, spectrum line, filter-knob binding, ED storage/migration.
- usb-audio.md (#13): no USB-audio endpoints wired in MainBoard_AudioIO yet; lots of in-tree
  USB_Audio_*.md debug docs = the starting material. Couples to #14.
- openaudio-library.md (#14): foundational OpenAudio change for audio quality; highest blast
  radius; sequence with #13.
Two tracks identified: DSP/UI features (#10,#11 — extend partial backends, lower risk) vs
audio-plumbing (#14→#13 — foundational, coupled). Updated index.md roadmap section.

## [2026-06-09] decision | Wrote cw-processing page (biggest remaining gap)
Read DSP_CWProcessing.h/.cpp. Created firmware/cw-processing.md documenting the RX-side CW:
- CW audio filter (CWAudioFilter): 5 selectable biquad cascades S1_CW_AudioFilter1..5 by
  ED.CWFilterIndex (0.8/1.0/1.3/1.8/2.0 kHz) + off.
- Tone detection (DoCWReceiveProcessing): hybrid correlation (reference sine) × Goertzel mag at
  CWToneOffsetsHz[CWToneIndex]; combinedCoeff thresholded → mark/space envelope.
- Adaptive Morse decoder (DoCWDecoding): MorseStates timing SM; standard dit/dah/space timing
  (ditLength=1200/WPM); adaptive via signal+gap histograms (HISTOGRAM_ELEMENTS=750,
  ADAPTIVE_SCALE_FACTOR=0.8) + JackClusteredArrayMax + thresholdGeometricMean; IsCWDecodeLocked.
  Decode via flat-array Morse tree (bigMorseCodeTree): dit→index++, dah→index+=jump, jump>>=1;
  emit at char end. Output buffer → display (decoderFlag).
- Sidetone (TX) phase precomputed in InitializeCWProcessing.
Contextualized within full CW path (TX keyer=ModeSm, CW VFO=rf-board/tune). Added inbound links
from dsp-chain + mode-state-machine (no longer orphaned). Logged a discrepancy: header says
"50–500 Hz typical" filter but code labels are 0.8–2.0 kHz. Updated index + documentation-todos.
Firmware page count now 17; CW was the largest "not yet written" gap. Remaining new pages:
equalizer, SAM detection, TX carrier-null, BIT, 4 cal sub-SM state maps.

## [2026-06-09] decision | Wrote audio-equalizer + synchronous-am-detection pages
Two theory/ concept pages from DSP_FFT.cpp (EQ) and DSP.cpp (SAM):
- theory/audio-equalizer.md: 14-band graphic EQ = parallel filterbank. BandEQ/ApplyEQBandFilter
  (DSP_FFT.cpp:787): 14 fixed biquad-cascade bands (EQ_Coeffs[14], 4 stages each), separate
  RX/TX filter instances (S_Rec/S_Xmt) sharing coeffs, per-band gains ED.equalizerRec/Xmt
  (/100, 100=flat); each band filtered→scaled by sign×gain→accumulated into eqSumBuffer→copied
  back. Alternating sign (even bands negated) gives flat reconstruction of overlapping bands.
  RX after demod (DSP.cpp:924); TX on mic audio. Edited via MainBoard_DisplayEqualizer (UISm
  EQUALIZER). Open Qs: band freq/Q table, gain range.
- theory/synchronous-am-detection.md: SAM = DD4WH/wdsp synchronous AM (AMDecodeSAM, DSP.cpp:567).
  2nd-order PLL (phase det atan2 → loop filter g1/g2 from omegaN=200/zeta → NCO phzerror; freq
  est omega2 clamped ±pll_fmax=4000Hz) recovers a clean carrier → fade-robust coherent demod,
  vs envelope AM (AlphaBetaMag). Fade leveler (tauR/tauI). Carrier offset → GetSAMCarrierOffset
  → home-screen SAM-offset pane. Open Qs: zeta/tau values, the ×10 scaling.
Added backlinks (dsp-chain related-theory, display-subsystem panes/files, theory/overview
concept list 8-10, index). documentation-todos: both marked done. Theory now 11 pages.
Remaining new pages: TX carrier-null, BIT, 4 cal sub-SM state maps.

## [2026-06-09] decision | Wrote tx-carrier-null, built-in-test, calibration-state-machines
Three pages, clearing the last "not yet written" backlog.
- theory/tx-carrier-null.md: per-band DC offset added to TX I/Q before the DAC
  (arm_offset_q15, DSP.cpp:1065-1067) cancels LO leakage (DC on baseband → carrier tone after
  mixer). TransmitCarrierCalSm sweeps DCOffsetI/Q to maximize dBc. Contrasted the 3 TX cals
  (image vs carrier vs power). Distinct from iq-imbalance (image).
- firmware/built-in-test.md: BIT struct (bit_results, Globals.cpp:69) = boolean I2C-presence
  flags per subsystem (RF MCP/Si5351, BPF, LPF MCP/AD7991, front panel) set as a side effect of
  each board's Initialize* (e.g. BPFBoard.cpp:30/41). DrawBIT "I2C Status Report" screen (UISm
  BIT state), reached via iBITDISPLAY (Loop.cpp:1354). Connectivity test only, startup snapshot.
- firmware/calibration-state-machines.md: consolidated the 4 generated cal SMs. KEY FINDING:
  ReceiveIQCalSm/TransmitIQCalSm/TransmitCarrierCalSm are STRUCTURALLY IDENTICAL (7 states:
  STANDBY/BAND_ADJUST/FIND_MINIMUM/ADJUST/WAIT/READ; 7 events) — one minimize-sweep machine for
  three metrics. PowerCalSm differs (POWERPOINT1/2/3, SSBPOINT, ACQUISITION, READ_AND_ADJUST,
  POWERCOMPLETE; RECORD_DATA_POINT/CURVE_COMPLETE) because it's curve-fitting, not minimum-finding.
  Mapped the WAIT→READ→ADJUST/NEXT_POINT loop + BAND_ADJUST/MIN_EXIT/AUTO_COMPLETE band stepping.
Added backlinks (hardware-state-machine, tx-carrier-null, iq-imbalance-correction; resolved the
HSM "full cal sub-SM state map" open Q). Updated index + documentation-todos. MILESTONE: every
previously-unwritten page is now a draft. Totals ~firmware 19, theory 13, roadmap 6.

## [2026-06-09] lint | full wiki health-check (38 pages)
First formal lint. Built a link graph (Python) over all pages. Findings + fixes:
- BROKEN LINKS (10 instances): [[spectrum-refresh-floor]] was referenced from 6 pages but had
  NO wiki page (only a memory note) → CREATED firmware/spectrum-refresh-floor.md (type:decision).
  [[roadmap]] (real-time-constraints) and [[theory]] (code-heritage) pointed at directories, not
  pages → re-pointed to [[front-panel]] and [[theory-overview]] respectively. [[tune-state-machine]]
  in log.md left as-is (historical record).
- STALENESS (contradiction): the new spectrum page revealed the refresh work SHIPPED 2026-05-30
  (11.7→18.8 fps, NCHUNKS 8→5, REFRESH 80→50) but 4 pages (real-time-constraints, zoom-fft,
  display-subsystem, main-loop) still asserted the OLD "~80 ms floor / NCHUNKS=8" framing →
  reconciled all 4 to "redraw ≈ NCHUNKS×T_loop, cut draw cost" and defer to the page.
- AMBIGUOUS WIKILINKS: two overview.md files (firmware/ + theory/) made [[overview]] ambiguous
  (basename-resolved per schema) → renamed theory/overview.md → theory-overview.md, updated all
  11 theory pages + index (sed). Now 0 duplicate basenames; firmware [[overview]] unambiguous.
- STALE STATUS: dsp-chain, real-time-constraints, state-machine-architecture were status:stub but
  had draft-level content → promoted to draft (updated 2026-06-09).
- ORPHANS: none (llm-wiki.md is the meta originating-idea doc, referenced by CLAUDE.md; not a
  catalog page). INDEX COVERAGE: complete after adding spectrum-refresh-floor.
RESULT: lint clean — 0 orphans, 0 broken links (excl. log history), 0 missing-from-index, 0 dup
basenames. Totals: firmware 20, theory 12, roadmap 6 = 38 content pages, all draft.

## [2026-06-14] query | Resolved RX I/Q calibration tone-offset question
Traced the RXIQ cal signal path. CW tone is programmed to `band_center + SR[SampleRate].rate/4`
(`HardwareSm.cpp:613`) = 48 kHz at the default 192 kHz raw rate — the code comment is correct.
The "±6 kHz at 24 kHz" premise was wrong: the measured PSD is the 192 kHz raw, 1×-zoom 512-bin
display FFT (cal forces `spectrum_zoom = 0`, `HardwareSm.cpp:498`) → 375 Hz/bin, bin 256 = LO,
so ±Fs/4 = bins 128/384. Wanted = bin 384, image = bin 128. Also resolved the `×10` metric:
`psdnew` is log10 magnitude (`DSP_FFT.cpp:144,186`), so ×10 = dB. Updated
[[iq-imbalance-correction]] (calibration section + Resolved block) and struck two items from
[[documentation-todos]]. Remaining IQ open question: single-axis vs 2×2 residual / achieved IRR
(needs hardware measurement).

## [2026-06-14] query | Resolved NR sample-rate comment question
`Kim1_NR` runs at the 24 kHz decimated RX rate (raw 192 kHz ÷ DF1·DF2 = 4·2 = 8,
`SDT.h:409-410`) → 24000/256 = 93.75 Hz/bin. The `46.9Hz [12000Hz/256] @96kHz` comment
(`DSP_Noise.cpp:141,155`) is stale DD4WH heritage text (old 96 kHz input / 12 kHz NR rate);
the code is rate-agnostic — it divides by the live `data->sampleRate_Hz / NR_FFT_L`, so it is
correct despite the comment. Updated [[noise-reduction]] (Kim1_NR section + Resolved note) and
struck the item from [[documentation-todos]].

## [2026-06-14] query | Resolved zoom default "mismatch" (false alarm)
`Config.h:78 #define ZOOM 3` is a front-panel **button-ID** macro (the "Front panel button
functions" block, `Config.h:74-96`), dispatched as `case ZOOM:` in `Loop.cpp:457,818` where it
does `ED.spectrum_zoom++`. It is unrelated to the zoom *level*. Power-on zoom is governed solely
by `ED.spectrum_zoom` (default 1 = 2×, `SDT.h:274`; or a restored JSON value, `Storage.cpp`).
No real discrepancy. Updated [[zoom-fft]] (Defaults + Resolved) and struck the item from
[[documentation-todos]].

## [2026-06-14] query | Resolved TX fixed-taps vs RX runtime-harris design question
Conclusion: primarily heritage, not a deliberate determinism/latency choice. The fixed 48-tap
TX tables are legacy DD4WH coefficient sets — the arrays are named/commented "RXfilters Dec and
Interpolation" (`DSP_FIR.cpp:174-175`) yet consumed by the TX chain (`DSP_FFT.cpp:425-442`).
RX *decimation* was refactored into the parametrized harris-formula design
(`InitializeDecimationFilter`, `DSP_FIR.cpp:1284-1300`, malloc). The runtime path's only
structural advantage — sample-rate independence via `SR[SampleRate].rate` — is dormant because
`SampleRate` is fixed at SAMPLE_RATE_192K (`Globals.cpp:106`) and never reassigned, so RX
recomputes identical coeffs every boot. Nuance: RX is itself a hybrid — decimation runtime-sized,
interpolation fixed 48/32 taps with runtime coeffs (`DSP_FFT.cpp:401-403`). Updated
[[multirate-decimation]] (new "Why RX designs taps at runtime" subsection + Resolved) and struck
the item from [[documentation-todos]].

## [2026-06-14] query | Resolved AGC soft-knee curve + n_tau/MAX_N_TAU questions
Soft-knee: the per-sample gain `mult = (out_target - slope_constant*fmin(0, log10(volts/max_input)))/volts`
(`DSP.cpp:454`) yields a 3-region transfer curve — flat limiting at out_target (volts≥max_input);
linear-in-log soft knee falling slope_constant/decade (min_volts..max_input); constant max_gain
below min_volts. var_gain is the wdsp slope knob: total output rise = out_target·(1−1/var_gain)
≈ +3.5 dB at the default 1.5. n_tau/MAX_N_TAU: MAX_N_TAU=8 is a commented-out worst-case constant
(`SDT.h:615-617`) used only to hand-size the static ring_buffsize=24000·8·0.01+1=1921; operating
n_tau=4 → 96-sample (~4 ms) look-ahead at 24 kHz. Added a "gain law / soft-knee transfer curve"
section + look-ahead/n_tau detail + Resolved block to [[agc-design]]; struck both items from
[[documentation-todos]].

## [2026-06-14] decision | Fixed stale source comments to match the wiki (6 findings)
Comment-only edits on branch encoder-i2c-speed (not committed). Corrected the long-standing
code/comment discrepancies the wiki had documented:
- VFO unit: RFBoard.h/Tune.h docstrings "decihertz/deci-Hertz" → "centi-Hz (Hz × 100; _dHz
  suffix is a historical misnomer)". Symbol names left unchanged (invasive refactor).
- CAT: CAT.h:25 "TS-2000" → "TS-480" (consistent with CAT.cpp + ts_480_pc.pdf; ID020=TS-480).
- EEPROM myth: PhoenixSketch.ino header + Storage.cpp:675 now say flash/SD (LittleFS+SD JSON),
  ED blurb notes "EEPROMData" is a historical name.
- TuneSm myth: PhoenixSketch.ino (philosophy bullet, ASCII diagram, SM list, module list) and
  CLAUDE.md ("Frequency Control State" section + lines) → HardwareSm owns the tune state;
  Tune.cpp is a pure-function library.
- HardwareSm hand-written: made explicit in PhoenixSketch.ino + HardwareSm.cpp header.
- ModeSm state list: .ino "…CW_TRANSMIT, TUNE" → real states (no TUNE; six CW_TRANSMIT_* +
  CALIBRATE_*).
Struck all six from [[documentation-todos]]. Wiki was already correct; this aligns the source.

## [2026-06-14] decision | Added hardware/ as a 4th wiki subject area
Physical-hardware documentation had no natural home (firmware/ = code, theory/ = DSP why).
Amended wiki/CLAUDE.md: added hardware/ to Scope (4 areas), directory layout, and a `hardware`
type value. Created hardware/hardware-platform.md (T41-EP overview: Teensy 4.1, board stack,
dual-SGTL5000 + PCM1808/PCM5102 audio, RF chain, power/PA/XVTR options) and hardware/i2c-bus-map.md
(Wire/Wire1/Wire2 topology, every device+address from Config.h, why the front panel gets its own
400 kHz bus). Facts derived from Config.h + board drivers (no schematic ingested yet; flagged).
Updated index.md (new Hardware section, count 38→40) and added inbound [[hardware-platform]]/
[[i2c-bus-map]] links from overview, rf-board, filter-boards, front-panel, built-in-test.

## [2026-06-14] ingest | T41-EP KiCad schematics (6 boards)
First physical-hardware source. Wrote sources/t41-ep-schematics.md summarizing all six files
(main / RF / attenuator-daughter / dual-clock-LO / BPF-exciter / LPF-100W-PA). Integrated
confirmed facts into [[hardware-platform]], [[i2c-bus-map]], [[rf-board]], [[filter-boards]].
Key resolutions: ONE physical SGTL5000 (RX path is PCM1808 ADC + PCM5102 DAC) — flagged the
code's vestigial second-"SGTL5000" object as a discrepancy; RA8875 is off-board (J7); RX/TX
attenuators = PE4312 digital step attenuators (31.5dB/0.5dB) on a daughterboard; dual Si5351
(25 MHz ref) confirmed; three I2C buses confirmed by SDA/SCL, SDA1/SCL1, SDA2/SCL2 nets.
New discrepancy flagged: LPF board uses AD8307 *log* power detectors, but PerformSWRBridgeReading
computes linear P=(V*10)^2/50 — needs reconciling (which detector feeds AD7991). Updated index
(Sources section populated; count 40 pages + 1 source).

=== orphan/link check ===

## [2026-06-14] decision | Centralized schematic-ingest discrepancies in the tracker
The two new findings from the schematics (AD8307-log-vs-linear power math; phantom second
SGTL5000) and the net-tracing leads were recorded on their pages + log.md during ingest but not
in roadmap/documentation-todos.md (the central tracker). Added a "Hardware (from T41-EP
schematics)" subsection there so they're discoverable from the start-here hub.

## [2026-06-14] decision | Net-traced AD8307 power chain — resolved the log-vs-linear question (NOT a bug)
Traced the LPF/PA board (K9HZ_100W_11band_LPF-Control) via union-find over wires/pins/labels,
confirmed with the schematic owner: coupler → 2× AD8307 log detectors (U14 forward, U19
reflected) → AD7991 CH0/CH1; net REF = REF3440 4.096V reference on AD7991 Vin3/Vref (not
"reflected"); only 2 AD8307 placed. Read LPFBoard.cpp:589-649: the DEFAULT digital path already
does the correct AD8307 log conversion (mV/25 − 84 → dBm → W). The linear (V·10)²/50 formula is
the separate USE_ANALOG_SWR branch (linear diode detectors on Teensy pins 26/27, off by default).
So the earlier "possible bug" was a WIKI mis-attribution, now corrected. Updated [[filter-boards]]
(rewrote SWR section into two correct branches + Resolved), [[t41-ep-schematics]],
[[hardware-platform]], and struck the item in [[documentation-todos]].

## [2026-06-14] decision | Resolved the "phantom second SGTL5000" (schematic owner)
Audio architecture confirmed: ONE SGTL5000 (Teensy hat, sgtl5000_teensy) doing double-duty
(mic in + TX I/Q out); PCM1808 (U6) = RX I/Q in; PCM5102 (U5) = speaker out. The firmware's
pcm5102_mainBoard object corresponds to the PCM5102 DAC but is mistyped AudioControlSGTL5000 —
harmless because PCM5102/PCM1808 are control-less I2S parts (its .enable()/.inputSelect() calls
are no-ops; the I2S data path carries audio). So it's a code clarity fix (retype/rename, drop
"two SGTL5000s" comment), not a functional bug. Updated [[hardware-platform]], [[t41-ep-schematics]],
and struck the item in [[documentation-todos]].

## [2026-06-14] decision | Documented ATtiny85 (U4) soft-power controller — open question resolved
The main-board ATtiny85 is a dedicated power controller (separate sketch
code/src/ATTiny85_On_Off/ATTiny85_On_Off.ino, K9HZ/KI3P). 3-state machine: OFF → button →
FET on (boot Teensy) → ON → button → assert START_SHUTDOWN → SHUTDOWN (power held) while Teensy
saves params → Teensy raises SHUTDOWN_COMPLETE → FET off. Two-wire handshake (not I2C/sensor).
Teensy side: Loop.cpp:1499 polls BEGIN_TEENSY_SHUTDOWN → ShutdownTeensy() (Loop.cpp:1414) saves
state then digitalWrite(SHUTDOWN_COMPLETE,1); CAT 'PS' triggers same path. Firmware depends on
it for orderly power-down (enables save-on-power-off). Added a "Power control & graceful shutdown"
section to [[hardware-platform]], struck the ATtiny item from [[documentation-todos]].

## [2026-06-14] decision | Corrected PE4312 designators for board-revision differences
Owner clarified the U3/U12 confusion stems from RF-board revisions: V12.8 puts the PE4312s on
removable daughterboards (RX=U11, TX=U3; U12=dual-clock daughter); PRIOR revisions placed PE4312
directly on the board (RX=U3, TX=U12). Wiring + firmware unchanged across revisions. Reworked
[[rf-board]] and [[t41-ep-schematics]] to lead with the revision-independent ELECTRICAL mapping
(GPIOA→RX, GPIOB→TX; bit→pin→dB table) and added a per-revision designator table; added a
"multiple revisions" caveat to the source preamble. The raw/ files are the V12.8/"V012" gen.

## [2026-06-15] decision | Traced LPF/control board band-code → decoder → relay chain (owner)
Full decode chain documented in [[filter-boards]] + [[t41-ep-schematics]]:
- Band LPF: MCP23017 U15(0x25) GPIOB[0:3] (4-bit BCD) → U12 CD74HC4514 4→16 (A0-A3 pins 2,3,21,22)
  → inverting U9/U10 ULN2803 → EXTERNAL band-LPF relays via J24/J25 (selected line LOW; NF=Y15
  bypass). BCD table per SDT.h:95-106.
- Antenna: GPIOB[4:5] → U2 74HC237 3→8 → U4 ULN2803 → on-board relays K4-K7 → ANT-1..4.
- 100W PA: GPIOB[7] → spare U4 channel → K2/K3.
- BPF RX/TX routing: GPIOA[0:1] (TXBPF/RXBPF) → U5/U6 → external BPF between J15-J18.
CORRECTION: earlier wiki said on-board K2-K7 were the band-LPF relays; actually K2/K3=PA route,
K4-K7=antenna, and the band-LPF relays are on a separate EXTERNAL board. Fixed in filter-boards
physical-hw bullet + source page. Struck the LPF band-code lead in documentation-todos. (Minor:
owner wrote "CD74HC4515" but schematic value field + active-high behavior = CD74HC4514; used 4514.)

## [2026-06-15] ingest | LPF filter board (K9HZ_100W_11band_LPF-Filter) — band-LPF loop closed
Added the 7th schematic. 24× HF41F relays = 2 per band; ONE dedicated 7th-order LPF per band
(11 bands) + BNF straight-through bypass (NOT band-groups). Band-named control nets
B160M…B6M,BNF arrive on J26/J27 (←control board J24/J25); each net = 2 relay coils + 1 conn pin.
33 inductors (3/band, symmetric ladder), 5.21/4.73µH (160m) → 0.188/0.170µH (6m); caps 27-2200pF.
Full select loop: GPIOB[0:3] BCD → U12 CD74HC4514 Yn(=BCD) → U9/U10 ULN2803 → J24/J25→J27/J26 →
B<band> → 2 relays → band LPF. Updated [[filter-boards]] (decode chain + resolved open Q),
[[t41-ep-schematics]] (file table + new section, 6→7 files), index.md. Only LPF item left:
tabulate per-band L/C→cutoff (values present in schematic, not yet tabulated).

## [2026-06-15] decision | U21 upper bits unused; per-band L/C table out of scope
Owner-confirmed: RF-board MCP23017 (U21) upper bits GPA6/7 and GPB6/7 are unused / not connected
— each bank carries only the 6-bit attenuator word. Resolved in [[rf-board]]. Also: per-band LPF
L/C→cutoff tabulation is intentionally out of scope (more detail than the wiki needs) — removed
from the leads list, not a gap. Only remaining hardware net-tracing lead: I²S/MCLK routing.

## [2026-06-15] decision | Traced I²S routing — last hardware net-trace closed
Owner + main-board schematic: shared I²S clocks MCLK=Teensy GPIO23 (net CLK), LRCLK=GPIO20 (LRCK),
BCLK=GPIO21 (BCK) to U5 PCM5102, U6 PCM1808, U7 SGTL5000 adapter. DAUDIO_OUT=GPIO32 (quad-I2S
OUT1B) → PCM5102 speaker DAC; ADC_IN ← PCM1808 RX I/Q. SGTL5000 uses standard audio-shield data
pins (OUT1A=GPIO7 TX I/Q + mic in), inferred. Documented in [[hardware-platform]] audio section +
Resolved list; cleaned the stale open-questions bullet (PE4312/band-code already resolved). All
schematic net-tracing leads now closed; struck the leads line in [[documentation-todos]]. Only
non-schematic hardware items remain (PA bias/rails; per-band SWR cal = bench).

## [2026-06-15] lint | full wiki health-check (41 content pages incl. 1 source)
Structural: no duplicate basenames; no orphan content pages; frontmatter complete on all; every
content page linked from index.md. Broken wikilinks only in CLAUDE.md (schema examples
[[page-name]]/[[other-page]]) and log.md (historical [[tune-state-machine]] + stray [[roadmap]]/
[[theory]] in old entries) — left as-is (doc/history, not navigational).

Fixed contradictions/stale claims superseded by the schematic ingest:
- "3× AD8307" → "2× AD8307" (U14 fwd/U19 rev) in filter-boards + hardware-platform table; AD7991
  count corrected to 1 (U20 only placed).
- i2c-bus-map listed a phantom 2nd SGTL5000 (main board @ HIGH) + "two codecs" note — removed the
  bogus bus row and rewrote the note (one SGTL5000; PCM1808/PCM5102 are control-less, not on I2C).
- tune-frequency-control called the ×100 unit "deci-Hz/deci-Hertz" — corrected to centi-Hz with a
  misnomer note, consistent with rf-board.

Recommendation (not yet actioned): no firmware **module** page exists for MainBoard_AudioIO.cpp
(the OpenAudio quad-I2S graph, mixers/queues, UpdateAudioIOState mode routing, sidetone). The
audio *hardware* is in [[hardware-platform]] but the audio *firmware module* is a content gap —
candidate new page firmware/audio-io. Also: hardware/ pages are now schematic-grounded and
candidates for draft→stable.

## [2026-06-15] decision | Wrote firmware/audio-io (closed the lint-flagged content gap)
New module page for MainBoard_AudioIO.cpp: OpenAudio quad-I2S graph (i2s_quadIn/Out channel map),
the 4 input mixers + 4 record queues / 4 play queues + 4 output mixers (mixers as on/off
selectors), sidetone + TX-IQ-cal oscillators, UpdateAudioIOState() per-ModeSm-state routing table,
the queue ISR↔main-loop hand-off to DSP (Q_in_*/Q_out_* with DSP.cpp citations), init/AudioMemory/
SetI2SFreq (48/96/192k), and the codec objects (sgtl5000_teensy real; pcm5102_mainBoard = PCM5102
DAC, mistyped AudioControlSGTL5000, no-op control — cross-ref hardware-platform). Linked from index
+ inbound from dsp-chain, mode-state-machine, cw-processing, hardware-platform. firmware 20→21.

## [2026-06-15] decision | Promoted hardware/ pages draft→stable
[[hardware-platform]] and [[i2c-bus-map]] are now schematic-grounded (sourced from
[[t41-ep-schematics]]) and lint-clean, with all open hardware net-traces resolved → promoted to
status: stable (updated 2026-06-15). Index draft markers updated; also corrected a stale index
blurb ("dual-SGTL5000" → single SGTL5000 + PCM1808/PCM5102). The backing source page
[[t41-ep-schematics]] and the firmware module pages [[rf-board]]/[[filter-boards]] remain draft
(other unrelated sections in those still maturing).

## [2026-06-15] lint | verification pass before human correctness review
Cross-page consistency: AD8307=2 everywhere, AD7991=1×(U20), no stray "two SGTL5000" (only the
quoted code-comment inside the discrepancy box), no stale "deci-" outside misnomer-flagging
context, attenuator designators all revision-qualified. Citation spot-check (all exact):
MainBoard_AudioIO.cpp:121/140/198/280/446/452/488; DSP.cpp:134/730/988/1061;
LPFBoard.cpp:640-642 (AD8307 log→dBm); RFBoard.cpp:276/67/102 (attenuator word→GPIOA). No edits
needed. Flagged for human verification: inferred (non-net-labelled) claims — SGTL5000 data-pin
assignment (OUT1A=GPIO7 + mic-in) and the 74CBTLV3253 I/Q-mux role; and the raw/ schematic = the
"V12.8/V012" revision assumption behind the attenuator designator table.

## [2026-06-16] decision | rf-board.md owner corrections
Owner proofreading corrections applied to `firmware/rf-board.md`:
1. VFO/LO discussion — added board-revision note that RF boards prior to V12.8 had a single
   Si5351 with one quadrature pair (CLK0/CLK1) **shared** between RX and TX mixers (no
   independent RX/TX LO), while still providing the CW carrier on CLK2.
2. Frequency units — renamed `_dHz`/decihertz → `_cHz`/centi-Hz throughout the section to match
   the firmware rename; dropped the now-resolved "RFBoard.h says decihertz" wrong-unit flag.
3. Attenuators — corrected "GPIO value is simply attenuation ÷ 0.5 dB" to "the attenuation in
   dB × 2" (matches `round(2·atten_dB) & 0x3F`).

## [2026-06-16] decision | split board electronics firmware/ → hardware/
Owner question surfaced that `firmware/rf-board.md` and `firmware/filter-boards.md` carried
substantial *electronics* content (part numbers, pinout tables, schematic-traced decode chains)
that the wiki schema assigns to `hardware/` (code-vs-electronics split). Resolution: moved that
content into two new pages —
- `hardware/rf-board-electronics.md` (type: hardware): dual/single Si5351 LO + board-revision
  history, PE4312 attenuators (GPIO-bit→pin table + per-revision designators), ADT/PSA/MAR/MASWSS
  mixer front end, I/Q baseband op-amps.
- `hardware/filter-board-electronics.md` (type: hardware): BPF/exciter board, LPF/PA control
  board decode chain (CD74HC4514/74HC237 + ULN2803 + HF41F relays), external per-band 7-element
  LPF board with inductor values, AD8307/AD7991/REF3440 SWR-sensing front end.
The two `firmware/` module pages now describe the **code/API** only (functions, the
`hardwareRegister` model, the SWR math, the band-BCD encoding the firmware writes, test hooks)
and point to the electronics pages. Cross-links added both ways; `hardware-platform.md` and
`index.md` updated (hardware page count 2→4, 41→43 content pages). No content lost — moved, not
duplicated.

## [2026-06-16] lint | full wiki health-check
Structural: 44 content pages (43 + 1 source). **No broken inter-page links** (only schema-example
`[[page-name]]`/`[[other-page]]` in CLAUDE.md and historical entries in log.md, which are
expected/immutable). **No orphans.** **No duplicate basenames** among content pages (`raw/log.md`
shares the `log` basename but is an immutable source, out of namespace). New pages
rf-board-electronics / filter-board-electronics are reciprocally linked and indexed.
Stale-claim scan clean for: 3×AD8307 (correctly 2×), phantom 2nd SGTL5000 (only in the
discrepancy note), attenuator ÷0.5 phrasing (gone).
⚠ **CONTRADICTION FOUND (open):** wiki now says the VFO-frequency symbols were renamed `_dHz`→
`_cHz` (rf-board.md "Frequency units"; documentation-todos "FULLY RESOLVED"), but the working-tree
source on branch `encoder-i2c-speed` still has **51 `_dHz` occurrences and zero `_cHz`** (RFBoard
.cpp/.h, Tune.cpp/.h, HardwareSm.cpp, MainBoard_DisplayHome.cpp). The docstrings note the misnomer
("centi-Hz … the _dHz suffix is a historical misnomer") but the identifiers are unchanged. Flagged
to owner: either the rename is uncommitted/on another branch, or the wiki overstated it. Awaiting
clarification before reconciling the two pages. tune-frequency-control.md still uses `_dHz`, which
currently *matches* source.

## [2026-06-16] decision | dHz/cHz contradiction — owner ruling
Owner: the `_dHz`→`_cHz` rename is real but **not on `encoder-i2c-speed`** (uncommitted / other
branch). Resolution: **keep the wiki at `_cHz`** and add a branch-caveat note to rf-board.md
"Frequency units" and the documentation-todos entry, flagging that this working tree still shows
`_dHz` and the pages will reconcile once the rename merges. Lint contradiction closed.

## [2026-06-16] lint | full wiki health-check (post dHz→cHz merge)
Triggered after the `_dHz`→`_cHz` rename (`65e6bba`) and the doc-fix commit (`c95f932`) were
rebased/merged onto `encoder-i2c-speed`. **Structural:** 44 content pages — no orphans, no broken
inter-page links (only the schema-example `[[page-name]]`/`[[other-page]]` in CLAUDE.md and
historical `[[tune-state-machine]]`/`[[roadmap]]`/`[[theory]]` mentions inside log.md prose, all
expected/immutable). Frontmatter complete on every content page.
**Contradiction RESOLVED (was open from the two 2026-06-16 entries above):** source now shows
**0 `_dHz` / 46 `_cHz`** in `code/src/PhoenixSketch/`, so the rename the wiki described has
landed on this branch. Reconciled the three caveat-bearing pages:
- rf-board.md "Frequency units" — dropped the "not yet present on encoder-i2c-speed" branch note;
  now states source & wiki agree.
- tune-frequency-control.md — renamed `GetTXRXFreq_dHz`/`GetCWTXFreq_dHz` → `_cHz` (4 refs) and
  rewrote the "historical misnomer" prose to past tense.
- documentation-todos.md — "VFO frequency unit" item marked reconciled; "Stale source comments"
  blockquote updated from "not yet committed" → committed & merged (`c95f932`).
`updated:` bumped to 2026-06-16 on all three. No remaining stale `_dHz` identifier claims.
**Suggested next:** promote the now-verified firmware pages from `draft`→`stable` (most are
draft); consider a dedicated power-calibration theory page (flagged in tune-frequency-control).

## [2026-06-16] decision | rapid-tune audio mute & spectrum freeze (encoder-i2c-speed)
Documented a new `encoder-i2c-speed` feature, compile-gated by `MUTE_ON_RAPID_TUNE` (Config.h):
when a tune encoder is spun fast, mute the output audio and freeze/decouple the spectrum redraw,
resuming automatically. The two-tier encoder speedup ([[front-panel]]) made fast spins enqueue a
tune event almost every loop, which surfaced audio glitches + a jerky sweep; this masks both
without changing per-event tuning.
**New page:** firmware/rapid-tune-mute-freeze.md (`type: decision`). Covers detection in Loop.cpp
(`NoteTuneActivity(bool)`, `IsRapidTuning`/`IsRapidCenterTuning`/`IsRapidFineTuning`; uint32 millis
wrap fix for the host mock), audio mute in `AdjustVolume()` (DSP.cpp), and the display split —
Center Tune fully freezes; Fine Tune holds the trace but keeps the blue filter bar + cyan marker
tracking via a one-time bar-less L2 backdrop (`BuildFrozenBackdrop`) + per-freq-change L2→L1 BTE
blit (`RedrawFrozenSpectrumWithBar`) + `StampTuningBar` (refactored out of
`DrawBandWidthIndicatorBar`). Noted the inverse relationship to [[spectrum-refresh-floor]] (pause
vs speed-up; same draw-cost lesson) and the trace-over-highlight trade-off of the fast path.
**Cross-links added:** front-panel, tune-frequency-control, display-subsystem, main-loop,
spectrum-refresh-floor (all in `related` + inline). **index.md** bumped firmware 21→22 (44 content
pages). Status `draft` — display fast-path is hardware-exercised, not unit-tested; thresholds
(`RAPID_TUNE_ENGAGE_MS=60`/`RAPID_TUNE_RELEASE_MS=180`) pending bench tuning.

## [2026-07-29] ingest | V1.4RC merge — runtime sample rate, rate-independent filters, HIL suite
Brought the wiki current with the `V1.4RC` merge (`rx-dsp-176k-stage-test` + `fast_encoder`).
The wiki had been written entirely on `fast_encoder` and knew nothing of the sample-rate work,
so this is a content ingest from source rather than from a `raw/` document.

**Three new pages** (all `type: decision`, `status: draft`):
- **firmware/sample-rate-switching.md** — 192 / 176.4 ksps selectable at run time and persisted.
  `SampleRate` is now `uint8_t& = ED.sampleRate` (`Globals.cpp:106-109`), which is how a value
  read by hundreds of `SR[SampleRate].rate` call sites moved into the saved JSON untouched.
  Covers `ChangeSampleRate()` (`MainBoard_AudioIO.cpp:519`) step by step and why the order
  matters — in particular the `centerFreq_Hz` compensation for the change in the Fs/4 IF offset
  (3.9 kHz between the rates, applied to both VFOs, computed *before* `SampleRate` is updated),
  and the `InitializeDecimationFilter` free-before-malloc that had to land first. Menu (primary
  menu 8→9), `SR` CAT command, and the boot path (`InitializeStorage()` precedes
  `InitializeSignalProcessing()`, so there is no "apply saved rate" step).
- **firmware/runtime-filter-design.md** — the filters that had to be fixed before any of that
  worked. Frozen tables designed for 24 ksps audio scaled by `176400/192000 = 0.91875`, so a
  "2.0 kHz" CW filter cut at 1.84 kHz and the 4000 Hz EQ cell peaked at 3675 Hz. Now generated
  from analog prototypes *recovered* from the shipped tables (CW: 0.0200 dB ripple fits
  `cheby1(12, 0.02)` to 0.013 dB RMSE; EQ: every biquad has `b1=0`, `b2=-b0`, i.e. zeros at
  `z=±1`, the bilinear image of an analog bandpass). Prewarping is the load-bearing part.
  Recorded the framing that actually predicts behaviour: **Hz-specified → must be regenerated**
  vs **fraction-of-Fs → already correct**. Also the three bugs found in passing, of which one is
  user-visible: the TX decimate-by-2 was flat to 0.425*Fs, folding 6-9.5 kHz into transmitted
  audio, so **TX audio differs from previous releases even at 192 ksps**.
- **firmware/filter-hil-test.md** — the AD2 bench suite. Kept to the durable parts: the
  differential (engaged-minus-bypassed) method and why it makes a speaker-terminal measurement
  sufficient; the SSB filter as control; the two traps — injection at `|Fs/4 + fineTuneFreq_Hz|`
  (omitting the fine tune gives *silence*, not a small error) and AGC-off (no CAT command
  exists, so the suite refuses to run); rate-invariance-not-absolute-accuracy as the criterion;
  95 checks passed / 0 failed, corners within 0.3%.

**Contradiction resolved (house rule: flag, don't overwrite).** theory/multirate-decimation said
the runtime-design path's rate independence was "**dormant** — `SampleRate` is set once and
**never reassigned** anywhere in the firmware". That was true on `fast_encoder` and is now false.
Rewrote the section with a dated correction, kept the heritage explanation for the RX/TX
asymmetry (still valid), and marked the "dormant" clause of the 2026-06-14 *Resolved* entry as
superseded rather than deleting it. Same treatment in roadmap/documentation-todos.

**Two open questions closed from the new code:**
- audio-equalizer's "tabulate the 14 band centre frequencies" — the generator exposed them as
  `EQ_BAND_FC_HZ[]` (`DSP_FIR.cpp:1247-1251`), roughly 1/3-octave / R40. Tabulated.
- cw-processing's "confirm the sample rate the decoder/Goertzel operate at" — the **audio** rate,
  `SR[SampleRate].rate / RXfilters->DF`. The "12 kHz the sidetone phase implies" that prompted
  the question came from a **stale comment** (`DSP_CWProcessing.cpp:64` still says `/ 24000`
  while `:65-66` divides by the live rate). An earlier revision of the page had quoted that
  comment as if it were the code; corrected in place and logged as a comment-only source fix.

**Pages updated:** cat-control (25→29 commands; the new SR/CF/EQ/FL group; the parser rule that
`set_len` must differ from `read_len` or the read handler is unreachable — the bug that made
`FW;` write-only; and the `MD` fix, which did not merely fail to change the demodulator but
**mirrored the receive passband**), multirate-decimation, theory-overview, zoom-fft,
audio-equalizer, cw-processing, dsp-chain, audio-io, persistent-config, display-subsystem (the
`EffectivePassbandEdges_Hz` normalization — display-side counterpart of the same `MD` bug — plus
the three rate-derived axes), ui-state-machine (menu content is data, not states: the new menu
needed no StateSmith regeneration), mode-state-machine (`TO_SSB_MODE` was front-panel-only, so
CAT could enter CW and never leave), tune-frequency-control, iq-imbalance-correction,
hardware-platform (I2S clock is fractional; HIL measures ~-180 ppm), overview, usb-audio,
documentation-todos. index.md: 44→47 content pages (firmware 22→25).

**Suggested next:** the "why 176.4 ksps" question is unanswered anywhere in the tree — worth
asking the owner, because if the answer is USB-audio resampling (22.05 kHz being an exact 1:2 of
44.1 where 24 kHz needs 147:160) it belongs on roadmap/usb-audio and changes how that feature is
scoped. Also worth a bench follow-up: loop-budget and group delay at 176.4 vs 192 ksps, which
[[real-time-constraints]] currently only characterises at 192.

---

## [2026-07-29] decision | transmit filter HIL suite + the FIR rate-independence limit

**Trigger:** the human asked for a transmit-side equivalent of the receive filter HIL test
(`code/tools/filter_hil/`), with the AD2's W1 on the microphone input and Ch1/Ch2 on the exciter
I/Q outputs in unknown order. Built `code/tools/tx_filter_hil/` (10 modules, README, and a
`/transmit-filter-test` skill). This entry records what the work *taught*, not the tool inventory.

**New page:** [[tx-filter-hil-test]].

### The load-bearing finding: rate invariance is not one number

Replicating `CalcFIRCoeffs` in Python and evaluating the generated tap sets directly shows the
two Hz-specified **transmit** stages (`TX_DECIMATE3_FC_HZ` 3500, `TX_AUDIO_LPF_FC_HZ` 3039.6)
give a cascade −3 dB corner of **2726 Hz at 24 ksps but 2759 Hz at 22.05 ksps — +1.2 %**, on
correct firmware. Cause: both are **48-tap** Kaiser windowed sincs, and a fixed tap count cannot
place a corner exactly when the normalised cutoff moves; the taps quantise onto the same 48
positions at both rates.

This bounds any rate-invariance tolerance. The receive suite's 1.5 % is affordable only because
those stages are **IIR**, bilinear-transformed from prewarped analog prototypes — a continuous
parameter, hence the measured 0.3 %. Applying 1.5 % to the transmit stages would fail correct
firmware, so the transmit suite uses **2.5 %**; `TransmitChain176k.AudioBandwidthHoldsAcrossRates`
already allowed 2 % for the same reason, which corroborates it. The bug signature is still
−8.125 %, leaving better than 3×.

Filed into [[runtime-filter-design]] (new section, with the table), and referenced from
[[filter-hil-test]] (qualifying *why* 1.5 % is affordable there) and [[multirate-decimation]].

The same evaluation gave the transmit stopband profile — −21.6 dB at 3.5 kHz, **−52 dB at 4 kHz**,
>90 dB above 4.5 kHz. That corrected a mistake in the suite: an early draft demanded 30 dB of
out-of-band rejection at 3450 Hz, which is on the **transition skirt** and only ~20 dB down
*correctly*. The stopband boundary is now taken as 4 kHz.

### Other findings recorded

- **`TX;` / `RX;` are parameterless *writes*** — three characters, no reply (handlers return
  `empty_string_p`, which `command_parser` suppresses). Any CAT client inferring "3 chars ⇒ read"
  blocks for a full read timeout on every key/unkey. → [[cat-control]], with the corollary that
  `TX_write` silently does nothing outside `SSB_RECEIVE`/`CW_RECEIVE`, so the CAT reply cannot tell
  a client whether the radio keyed.
- **The Hilbert table is correctly *not* regenerated per rate.** Its usable band is a fraction of
  Fs, so one fixed table is the same design at any rate and its edges are *meant* to scale. Hence
  sideband suppression is checked against a floor, not for rate invariance — requiring it to hold
  still in hertz would fail correct firmware. → [[ssb-phasing-method]].
- **Sideband suppression is directly measurable** from a synchronous I/Q capture (`I + jQ`, ratio
  of `+f` to `−f`); and `SidebandSelection()`'s USB inversion doubles as the *only* way to resolve
  which scope probe is on I, since swapping probes and switching sideband both conjugate the
  signal and one capture cannot distinguish them. → [[ssb-phasing-method]].
- **`BandEQ` runs before `TXDecimateBy2Again`**, so it attenuates high mic input before the stage
  whose stopband prevents fold-back — which makes an end-to-end fold-back test weaker evidence
  about the decimator than it appears. Also `Q := I` after `TXGain` (chain is mono from there,
  only mic-left matters) and `TXGain` scaling with requested power (exciter amplitude only
  comparable at fixed power). → [[dsp-chain]].
- **The transmit EQ cells are not independently verifiable**, because `S_Xmt[i].pCoeffs` and
  `S_Rec[i].pCoeffs` are the *same array* — a transmit instance's only own state is `pState`. So
  no `equalizerXmt` CAT command was added, and the cells are exercised via the composite passband
  instead, where the ~200 Hz low corner is cell 0's. → [[audio-equalizer]], [[cat-control]].
- **Measurement trap worth keeping:** the scope rate must not divide the radio's output rate. At
  96 ksps a DAC image at `192000 − f` aliases to exactly `−f`, onto the mirror frequency the
  suppression reading uses. 100 kHz is the default for that reason.

**Pages updated:** runtime-filter-design (new FIR-vs-IIR section + stopband profile + bench
verification split), filter-hil-test (companion box, tolerance qualified), cat-control (TX/RX
write trap, missing `equalizerXmt`, `TX_write` state guard), ssb-phasing-method (Hilbert rationale
+ bench suppression measurement), multirate-decimation (FIR caveat + per-rate fold-point table),
audio-equalizer (testability corollary), dsp-chain (TransmitProcessing ordering). index.md:
47→48 content pages (firmware 25→26).

**Open / next:** the suite has **not been run on the bench** — everything above is from tap-set
evaluation, self-tests (48, incl. a simulated −8.125 % shift that must FAIL and must be *named*),
and an end-to-end simulation driven by the replicated FIR cascade (correct → PASS at +1.23 %,
frozen → FAIL). Two things to watch on the first real run: `--scope-offset` defaults to 1.6 V from
the older `transmit_test.py`, which assumed the exciter loaded by the RF board, so the bias may
differ with it disconnected; and the composite low corner mixes cell 0 with the mic input's AC
coupling, which is the one case where an `equalizerXmt` command would earn its keep.

---

## [2026-07-29] lint | README version history — two wiki figures corrected

**Trigger:** asked to audit the top-level README, then to write the missing V1.3 / V1.4.0 release
notes. Sourcing numbers for those notes from the code and from the HIL result JSON turned up two
errors in wiki pages.

### 1. Shipped spectrum rate is 15.6 fps, not 18.8

[[spectrum-refresh-floor]] recorded "**SHIPPED outcome: 11.7 → 18.8 fps**" with `NCHUNKS = 5`.
`NCHUNKS` is **6** in the tree (`MainBoard_DisplayHome.cpp:430`) → ≈64 ms → **15.6 fps**.

Traced with `git log -L`: 4 → 8 (`53ccfa9`) → 6 (`744d61d`, "15.6 fps") → 5 (`d15b17e`, "18.8 fps")
→ **6** (`f56c8c7`). That last commit is the **V1.3 release stamp** — it also bumped `VERSION` to
`"Phx V1.3"` — but its subject is "Add serial time sync over USB Serial port 0", and `b63f090`
carries an identical subject the same day, so the revert rode in on what looks like a rebase
artefact. Intent is not recoverable from the history; recorded as an open question for the owner,
since if accidental it is a one-line change to recover 18.8 fps. The page now states the shipped
figure and keeps the developed figure as such.

### 2. HIL result summary was imprecise, and the SSB control figure was wrong

[[filter-hil-test]] summarised the bench run as "every CW corner and equaliser centre within
**0.3 %**" and the SSB control as "**0.04 %**". Recomputed from
`results/filter_hil_20260728_204015.json` (the 95-pass run):

| Group | Worst \|Δ\| |
|---|---|
| CW audio filters (5) | 0.033 % |
| EQ cells excluding edge limited (11) | 0.160 % |
| EQ cells edge limited (0, 1, 13) | 0.711 / 0.367 / 0.206 % |
| SSB control (3 widths) | **0.093 %** |

So "0.3 %" holds *only* once the edge-limited cells are set aside — and pleasingly, the three worst
cells are exactly the three the suite already flags as edge limited, which corroborates that
classification rather than undermining it. The SSB figure was simply wrong. Replaced the prose
summary with the table.

Deliberately **not** propagated: five other pages quote 0.3 % for receive rate invariance
([[runtime-filter-design]], [[audio-equalizer]], [[cw-processing]], [[multirate-decimation]],
[[tx-filter-hil-test]]). All are consistent with the 0.160 % measurable-cell figure, so they stand;
churning them would add noise. The FIR-vs-IIR contrast in [[runtime-filter-design]] is unaffected —
the TX ≈1.2 % is an exact tap-set evaluation, the RX 0.16 % a measurement, and the gap is real.

**README changes (not wiki, recorded for traceability):** wrote the missing V1.3 and V1.4.0 release
notes; fixed `code/Contributors.txt` → `code/src/PhoenixSketch/Contributors.txt`; removed the
non-existent `TuneSm` from the architecture section (the tune state is `HandleTuneState` in
`HardwareSm.cpp`); "160m to 6m" → "160m to 4m plus general coverage" (13 bands incl. GEN);
feature list gained all four auto-calibration routines and the selectable sample rate; added
`code/tools` and `wiki/` to File Organization; refreshed the ctest sample output 657 → 740 from a
real run.

**Open:** the `NCHUNKS` intent question above is the one item needing the owner.

## [2026-07-30] decision | `TX` must accept its optional parameter (`TX0;`/`TX1;`/`TX2;`)

Started as a bug in `code/tools/serial_diag.py`, which sent `TX1;` and got `?;`. The first fix
changed the tool to send `TX;`. Checking `code/docs/ts_480_pc.pdf` p.21 showed the firmware was
the wrong side: the set form is `TX P1;`, P1 is 0 (normal/MIC), 1 (DTS via ANI) or 2 (TX Tune),
and **"if no P1 parameter is specified, P1=0 is used"** — so all four forms are legal.

Not pedantic: Hamlib (WSJT-X, fldigi, Pat) picks the form from the PTT type — `TX;` for
`RIG_PTT_ON`, `TX0;` for `RIG_PTT_ON_MIC`, `TX1;` for `RIG_PTT_ON_DATA` — so Phoenix keyed for
some clients and refused for others depending on the user's data-mode settings. That matches the
owner's recollection of it working on one OS but not another.

**Decisions:** accept every P1 value and ignore it (Phoenix has neither a data input nor a tune
mode to distinguish); also accept TS-2000-style `RX0;`/`RX1;`, which `CAT_test.cpp` already
exercised.

**Implementation:** `{ "TX", 3+1, 3, TX_write, TX_write }` and the same shape for `RX`. The idiom
for an optional parameter is the *write* function in both slots — `set_len` the parameterised
form, `read_len` the bare one. No `TX_read`: it would make `TX;` read instead of key, breaking
`RIG_PTT_ON` and both HIL suites. Also guarded `command_parser`'s two length tests with `> 0`,
closing a latent `command[-1]` out-of-bounds read affecting the nine entries with a zero length
field (`ID`, `IF`, `PD`, `ED`, `PR` set_len 0; `BD`, `BU` read_len 0).

**Method note worth keeping:** `CAT_test.cpp` already asserted TX accepted all three forms — but
called `TX_write()` directly, and `TX_write` ignores its argument. The suite passed for the whole
life of the bug because it never exercised the dispatcher that was rejecting them. New tests go
through `command_parser()`; they were confirmed failing before the fix. 745/745 pass after.

Updated [[cat-control]]. **Open:** the rest of the command table has never been audited against
the spec, and `TX`/`RX` were wrong, so others may be. Noted on the page.

## [2026-07-30] feature | DIGITAL (USB audio) mode

Added a third operating mode alongside SSB and CW, for digital modes over USB audio. Closes the
scoping on [[usb-audio]] (#13); design and code map written up in [[digital-mode]].

**The load-bearing insight.** Teensy USB audio is a fixed 44.1 kHz endpoint, and at 176.4 ksps
three things line up exactly: the receive chain's Fs/4 tap *is* 44,100 Hz; one DSP block there
*is* 512 samples (86.1328 blocks/s × 512 = 44,100/s); and the AudioStream graph clock (Fs/128 =
1378.125 Hz) is exactly 4× the USB audio block rate. So the mode forces 176.4 ksps and needs no
resampler and no modified Teensy core file. The rate switch lives on the enter/exit actions of a
`DIGITAL_STATES` composite, which is what makes it restore on every exit path including
calibration.

**Reused rather than rebuilt.** The stock `AudioInputUSB`/`AudioOutputUSB` (what OpenAudio's
`USB_Audio_F32.h` wraps) are kept, with all the core descriptor/DMA/ISR/feedback code. They
cannot go in the AudioConnection graph — it is clocked by I2S, 4× too fast — so they are taken
off `update_all()` via the protected `AudioStream::active` flag and driven by a 4:1 pacer.
~120 lines of adapter instead of a reimplemented transport.

**Two corrections recorded.**
1. `code/docs/USB_Audio_TX_Diagnostic_Plan.md:152-158` claims a 0.27 %/s structural sample
   deficit at 192 ksps. It is an arithmetic error — 94 blocks/s used where the true rate is
   93.75, and 93.75 × 512 = 48,000 exactly. Noted on [[usb-audio]] so it is not chased again.
2. [[usb-audio]]'s "possible shortcut" reasoned from the Fs/8 audio rate (22.05 kHz, 1:2 to
   44.1). Right instinct, wrong tap point: Fs/4 needs no ratio at all.

**Bug the new tests caught.** Because `EnterDigitalMode()`/`ExitDigitalMode()` are the
*composite's* actions, they run while `state_id` is still `DIGITAL_STATES` — before the leaf is
entered. `ChangeSampleRate()` ends in `UpdateTuneState()`, which fell through to `default:` and
reprogrammed the VFO from a stale tune state on every entry and exit. Fixed by handling the
composite in `UpdateTuneState()` and `UpdateRFHardwareState()`; pinned by
`ModeSm.DigitalModeTransitionsHandleTheCompositeState`, which asserts against the mock Serial's
captured lines.

**Build change with a user-visible consequence:** `usb=serialmidiaudio` has only one CDC port,
so CAT moves to the primary `Serial` (new `CATSerial` alias in `SDT.h`) and `Debug()` compiles
out. CAT clients must be repointed. `MD` is deliberately inert on the mode — WSJT-X sends `MD2;`
constantly and would otherwise knock the radio out of digital mode.

765/765 unit tests pass (745 before, 20 new); both `usb=serialmidiaudio` and `usb=serial2`
firmware builds compile. **Nothing is verified on hardware yet** — see the open items on
[[digital-mode]].

## [2026-07-31] bench | Digital mode verified on hardware; two USB-audio faults and a CAT sideband bug fixed

First hardware run of [[digital-mode]]. Enumerates as `Teensy MIDI/Audio` at 44100 Hz both
directions with the async feedback endpoint live. Injected 7075 kHz carrier, dial 7074 kHz.

**Headline result: no pitch error.** Tone measured 999.57 Hz; the −0.43 Hz offset is 0.06 ppm
of the carrier, i.e. generator-vs-radio calibration. First and last 5 s of a 20 s capture
differ by 0.009 Hz, below the measurement floor. The 4:1 pacer holds exact rate — a divisor
error would have shown as a 4× or 25 % ratio.

**Fault 1 — play-queue underrun.** 147 dropouts / 903 ms in 12 s (7.55 %). Every zero run was
an exact multiple of AUDIO_BLOCK_SAMPLES, spaced ~133 ms: `AudioOutputUSB::update()`
substitutes a *silence block* when its input queue is empty, so an empty queue reads as clean
digital silence rather than a glitch. The queue was capped at 8 blocks and never prefilled, so
it lived at depth 0–4 while the DSP inherently swings it by 4 blocks every 11.6 ms — no
underrun margin at all. Display refreshes are the likely trigger. Now 24 blocks prefilled to
half: **0 dropouts in 20 s**.

**Fault 2 — no headroom.** Stream sat at 98 % FS. This is *not* AGC: `ED.agc` was already
`AGCOff`, and that path applies `fixed_gain = 20.0`, a compile-time constant, so the USB level
is RF input × chain gain × 20 with nothing regulating it. Added `ED.digitalRxLevel` (default
25 = −12 dB, Microphone menu + CAT `DR`), scaled into scratch because the caller still needs
that buffer for the speaker. Peak −0.2 → −12.3 dBFS and 2nd harmonic −58.8 → −64.5 dB, so it
*was* mildly clipping.

**Pre-existing bug found and fixed: CAT sideband selection.** `MD2;` on 40 m tuned the radio to
LSB. `SetModulation()` wrote `bands[].mode`, which is the band *default* and the reference the
passband mirroring is measured against; making it agree with the request switches the mirroring
off. `MD_read`/`IF_read` read the same field and misreported the mode in use. Details and the
corrected model on [[cat-control]]. Shared SSB/CW code, unrelated to digital mode.

**Method notes.** (1) A test asserting `bands[].mode` after a mode change was asserting the
bug, and passed for its whole life; the replacement asserts the effective passband. (2) I
diagnosed the sideband from a single `MD;` read and then *wrote* `MD2;` to "fix" it, which
broke a working bench setup mid-test — the read was itself the buggy one. Capturing 2 s of
audio would have answered the question directly and without side effects.

776/776 unit tests pass. Transmit, end-to-end FT8, and long-run clock drift remain unverified —
see the open list on [[digital-mode]].

## [2026-07-31] bench | Transmit verified; record queue was starving the audio pool

Transmit works: sideband sense flips correctly (USB -89.99 deg / +41.0 dB image, LSB
+90.12 deg / -41.2 dB), imbalance -0.16 dB, IMD3 -51 dB, stable over a 12 s key-down.

**Amplitude response** answered the "why do two tones differ" question: a real SSB passband,
peak 800 Hz, -3 dB at 2243 Hz, 2.8 dB spread over 200-2200 Hz. It is the cascade of the TX
decimate-by-2 and TX audio bandwidth filters, both by design. Not the Hilbert transform -
computed from its coefficient tables it is flat to 0.01 dB above 200 Hz.

**The real find:** a 344.53 Hz (= 44100/128) envelope modulation, proven radio-side by varying
the host ALSA period size and watching it not move. New `DS` CAT counters showed underruns and
trims were zero but `depthMax = 208` - the transmit record queue is begun for all of digital
mode yet only drained while transmitting, so it sat at AudioRecordQueue's 209-block ceiling and
held ~418 of the 500 audio blocks. Bounding it in the pacer took +/-344 Hz sidebands from
-27 dBc to -74.5 dBc, the broadband floor from -65 to -84.6 dBc, and envelope ripple from
0.53 dB to 0.06 dB.

**Method notes.** Two of my own measurements were wrong before they were right. (1) The first
frequency sweep evaluated the DTFT at the *assumed* tone frequency; as the frequency estimate
drifted it walked out of the Hann mainlobe and read as deep notches, inventing a 17 dB ragged
response. Re-analysing identical raw data both ways reproduced the artifact exactly. Save raw
traces. (2) From the same artifact I claimed a 0.6 % transmit pitch shift; zero-crossing
analysis gives 2000.27 Hz for 2000 Hz in, and the I2S clock computes to exactly 176400.0000 Hz,
so there is none. (3) An early "29 ms" dip period was 2.9 ms aliased by a 1 ms-per-character
ASCII plot.

776/776 unit tests pass. Open: residual +/-86 Hz sidebands at -27 dBc, end-to-end FT8, transmit
long-run drift, and the single-VFO radio.

## [2026-07-31] bench | The +/-86 Hz sidebands were a resolution artifact; queue fix also improved receive

Chased the residual +/-86 Hz transmit sidebands reported in the previous entry. They do not
exist. Those captures were 40.96 ms -> 24.4 Hz bins, putting +/-86 Hz only 3.5 bins from the
carrier, i.e. inside its skirt. At 170 ms (5.88 Hz bins, 14.6 bins out) the same measurement
reads -93 dBc rather than -27.

Plotting the raw envelope showed what was really there before the record-queue fix: discrete
ringing transients at irregular intervals, flat between them - not a modulation. Impulsive
events smear the carrier into a broadband pedestal (+/-600 Hz at -40 to -60 dBc), and probing
that pedestal at +/-86 Hz just sampled it somewhere. The 344.53 Hz result from the previous
entry stands - that was 683 ms / 1.5 Hz bins and independently confirmed by varying the ALSA
period size.

Also re-ran the receive comparison once the signal generator was back on. The record-queue fix,
made for transmit, improved receive by **17.5 dB in 2nd harmonic (-64.5 -> -82.0 dB) and 16 dB
in noise floor (-119.3 -> -135.4 dB)**, with 0 dropouts in 60 s and L/R bit-identical. Starving
the audio pool was degrading both directions.

**Method note:** before believing a close-in spur, count the FFT bins. Anything within ~5 bins
of the carrier is skirt until proven otherwise. I asserted a spur at 3.5 bins twice.

## [2026-07-31] lint | Pre-release consistency pass (wiki + README)

Checked the wiki and the README release notes against the tree ahead of making the repository
public. 49 pages, no orphans, no broken navigational wikilinks (the four unresolved ones are
historical prose inside this log and the schema examples in [[CLAUDE]]).

**Found stale and fixed.** [[digital-mode]] was written as a page but not integrated into the
pages whose *code* it changed:

- [[development-backlog]] still listed #13 as "under active debugging; not yet wired in" and
  sequenced it behind [[openaudio-library]] (#14). #13 shipped first and without #14, so the
  two-item "audio-plumbing track" no longer exists.
- [[hardware-state-machine]] omitted the DIGITAL states from both switch tables — including the
  `DIGITAL_STATES` composite case, which [[digital-mode]] itself calls the load-bearing gotcha.
- [[persistent-config]] was missing `digitalDriveLevel` / `digitalRxLevel`.
- [[overview]] had no `USBAudio.cpp` in the module map and described ModeSm as SSB/CW only.

**Lesson for the next feature page.** Writing the feature's own page and updating the pages it
*touches* are separate jobs, and the second is the one that gets skipped: the 2026-07-30/31
commit updated 8 wiki pages, all of them ones that mention digital mode by name, and none of the
four above, which describe code the same commit edited. Diffing the commit's source-file list
against the pages that document those files finds this in a minute.

**README.** The V1.4.0 release notes predated digital mode entirely — no feature entry, CAT
table still at 29 commands (it is 32), test count at 740 (776), and the build instructions named
Dual Serial without mentioning that digital mode needs Serial + MIDI + Audio and moves CAT to the
first port. Also missing from the bug list: the `bands[].mode` sideband inversion, the optional
`TX;` parameter, and the two digital-mode buffering fixes.

Verified while checking: both firmware configurations compile clean (`usb=serial2` and
`usb=serialmidiaudio`, 600 MHz, o1lto), and 779/779 unit tests pass — 776 committed plus 3
uncommitted transmit-chain tests in the working tree.
