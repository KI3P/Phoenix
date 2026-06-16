---
title: Development Backlog
type: roadmap
status: draft
created: 2026-06-09
updated: 2026-06-09
tags: [development, backlog, features, github-issues, index]
source_refs: []
related: ["[[documentation-todos]]", "[[transmit-spectrum]]", "[[manual-notch]]", "[[usb-audio]]", "[[openaudio-library]]"]
---

# Development Backlog

Software-development tasks (features/proposals) for Phoenix, mirroring the open
[GitHub issues](https://github.com/KI3P/Phoenix/issues). Each has its own scoping page
describing **what needs to be done and the current state** — these are *task descriptions,
not solution designs*. The [[documentation-todos]] page tracks wiki/documentation work
separately.

## Open issues (as of 2026-06-09)

| Issue | Task | Status in code | Effort | Page |
|---|---|---|---|---|
| [#10](https://github.com/KI3P/Phoenix/issues/10) | See the transmit spectrum | **Largely built** (SSB + dual-VFO); needs verify + edge cases | Low–Med | [[transmit-spectrum]] |
| [#11](https://github.com/KI3P/Phoenix/issues/11) | Manual notch filter | Auto-notch scaffolding exists; manual mode unimplemented | Med | [[manual-notch]] |
| [#13](https://github.com/KI3P/Phoenix/issues/13) | Bi-directional USB audio | Under active debugging (in-tree docs); not yet wired in | Med–High | [[usb-audio]] |
| [#14](https://github.com/KI3P/Phoenix/issues/14) | Change OpenAudio library | Foundational; proposal to evaluate | High | [[openaudio-library]] |

## Two natural tracks

- **DSP/UI feature track** — [[transmit-spectrum]] (#10) and [[manual-notch]] (#11). Both
  *extend existing partial backends*, are independent, and are lower-risk. Good near-term work.
- **Audio-plumbing track** — [[openaudio-library]] (#14) → [[usb-audio]] (#13). Foundational and
  coupled; #14 likely enables/eases #13. Higher risk; design carefully and sequence together.

## Notes
- The wiki documentation effort surfaced that **#10 and #11 already have partial backends** —
  worth confirming scope before treating either as greenfield.
- Code/comment discrepancies and possible bugs found while documenting live in
  [[documentation-todos]] (e.g. the LMS NR channel-routing note, the unit mislabels). Some may
  deserve their own issues.
