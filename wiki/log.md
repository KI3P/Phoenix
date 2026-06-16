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
