"""Result assembly and rendering for the transmit filter HIL suite.

Three tiers, matching the convention the repo's other tools follow: a human
summary on stdout, a machine-readable JSON document, and a Markdown report that
stitches the numbers together with the generated figures.

The document assembly, provenance collection and JSON encoding are shared with
:mod:`filter_hil.report` - they say nothing about which chain was measured. The
stdout summary and the Markdown are written here, because what a reader needs to
be told about a transmit result is different.
"""

from __future__ import annotations

import os
import sys
from typing import Sequence

from filter_hil.report import (_jsonable, _md_table, build_document,  # noqa: F401
                               collect_provenance, write_json)

from . import bandtable as bt

SCHEMA = "phoenix.tx_filter_hil/1"


def build_tx_document(**kwargs) -> dict:
    """Assemble the result document, stamped with this suite's schema."""
    doc = build_document(**kwargs)
    doc["schema"] = SCHEMA
    return doc


# --- stdout ----------------------------------------------------------------

def print_summary(doc: dict, stream=sys.stdout) -> None:
    """Human-readable result, in the shape the other tools use."""
    s = doc["summary"]
    w = stream.write

    w(f"\n[{s['overall']}] Transmit filter rate-independence, "
      f"{doc['duration_s']:.0f} s\n")

    for warning in doc.get("warnings", []):
        w(f"  WARNING: {warning}\n")

    rig = doc.get("rig", {})
    if rig:
        w(f"  Wiring          : {rig.get('wiring', '?')}\n")
        if rig.get("suppression_at_1k_db") is not None:
            w(f"  Suppression @1k : {rig['suppression_at_1k_db']:.1f} dB\n")
        if rig.get("drive_amplitude_v") is not None:
            w(f"  Mic drive       : {rig['drive_amplitude_v']*1000:.1f} mV\n")
        if rig.get("key_down_s") is not None:
            w(f"  Key-down time   : {rig['key_down_s']:.0f} s\n")

    comp = doc.get("comparison", {})
    rates = sorted(doc.get("rates", {}), key=lambda r: -int(r))

    rows = comp.get("corners", [])
    if rows:
        w("\n  Transmit audio corners (Hz)\n")
        head = "    {:<34}{:>9}".format("feature", "nominal")
        for r in rates:
            head += " {:>10}".format(f"{int(r)//1000}k")
        head += " {:>9} {:>8}".format("delta", "verdict")
        w(head + "\n")
        for row in rows:
            nom = row.get("nominal_hz")
            line = "    {:<34}{:>9}".format(row["label"][:34],
                                            f"{nom:.0f}" if nom else "-")
            for r in rates:
                v = row.get("value_hz", {}).get(str(r))
                line += " {:>10}".format(f"{v:.1f}" if v is not None else "-")
            d = row.get("delta_pct")
            line += " {:>8}%".format(f"{d:+.2f}" if d is not None else "-")
            line += " {:>8}".format(row["verdict"])
            if row.get("legacy_consistent"):
                line += "  <- matches the old frozen-table shift"
            w(line + "\n")

    def per_rate_line(title: str, block: dict, key: str, unit: str) -> None:
        if not block:
            return
        values = block.get(key, {})
        cells = " ".join(f"{int(r)//1000}k {values.get(str(r)):.1f} {unit}"
                         if values.get(str(r)) is not None else f"{int(r)//1000}k -"
                         for r in rates)
        w(f"  {title:<16}: {cells}\n")

    w("\n")
    per_rate_line("Sideband worst", comp.get("sideband_suppression", {}),
                  "worst_db", "dB")
    per_rate_line("Fold-back worst", comp.get("fold_back", {}), "worst_dbc", "dBc")
    per_rate_line("Carrier worst", comp.get("carrier", {}), "worst_dbc", "dBc")

    w("\n  Checks:\n")
    for c in doc["checks"]:
        mark = "SKIP" if c["skipped"] else ("OK  " if c["passed"] else "BAD ")
        w(f"   [{mark}] {c['id']:<34} {c['message']}\n")

    w(f"\n  {s['passed']} passed, {s['failed']} failed, {s['skipped']} skipped\n")
    if not doc.get("state_restored"):
        w("  WARNING: radio state was NOT fully restored\n")
    if doc.get("state_residual_diff"):
        w(f"  Radio settings that did not return to baseline: "
          f"{', '.join(doc['state_residual_diff'])}\n")
    for name, path in doc.get("artifacts", {}).items():
        if isinstance(path, str):
            w(f"  {name.capitalize():<9}: {path}\n")
        elif isinstance(path, list) and path:
            w(f"  {name.capitalize():<9}: {len(path)} files in "
              f"{os.path.dirname(path[0])}\n")
    w("\n")


# --- Markdown --------------------------------------------------------------

def write_markdown(doc: dict, path: str, png_paths: Sequence[str] = ()) -> str:
    """Render the report a human reads."""
    s = doc["summary"]
    rates = sorted(doc.get("rates", {}), key=lambda r: -int(r))
    md: list[str] = []
    a = md.append

    a("# Transmit filter rate-independence, hardware in the loop\n")
    a(f"**{s['overall']}** - {s['passed']} passed, {s['failed']} failed, "
      f"{s['skipped']} skipped, in {doc['duration_s']:.0f} s.\n")
    a(f"Generated {doc['generated_utc']}.\n")

    for warning in doc.get("warnings", []):
        a(f"> **Warning:** {warning}\n")

    a("\n## What this shows\n")
    a("A tone is injected into the radio's microphone input and the exciter's I "
      "and Q outputs are captured together, at every sample rate under test. "
      "Because both outputs are sampled synchronously, the complex signal "
      "`I + jQ` can be reconstructed, and with it the thing a single-sideband "
      "transmitter is actually judged on: how much energy landed on the wrong "
      "side of DC.\n")
    a("\nThree stages of the transmit chain are specified in hertz and so have to "
      "be regenerated whenever the sample rate changes - the equaliser cells, "
      "`TX_DECIMATE3_FC_HZ` and `TX_AUDIO_LPF_FC_HZ`. Everything else is "
      "specified as a fraction of Fs and is *meant* to scale with the rate. "
      f"These filters used to carry frozen tables designed for one audio rate; "
      f"run at another, every corner scaled by the ratio of the rates - "
      f"**{bt.LEGACY_DELTA_PCT:.3f}%** between 176.4 and 192 ksps. A result "
      f"column close to that figure means the frozen tables are back.\n")
    a("\nUnlike the receive suite, nothing here can be bypassed over CAT: there "
      "is no command for the transmit equaliser, and the two generated FIR "
      "stages are unconditional. So every response below is a **composite** of "
      "the whole path, microphone input to exciter output, including the codec's "
      "own front end. That costs absolute accuracy, which is why the absolute "
      "checks are loose. It costs the rate comparison nothing, because no part "
      "of the analog path changes when the sample rate does.\n")

    a("\n## Rig\n")
    rig = doc.get("rig", {})
    rig_rows = [
        ["Wiring (detected)", rig.get("wiring", "?")],
        ["Suppression at 1 kHz",
         f"{rig.get('suppression_at_1k_db', float('nan')):.1f} dB"],
        ["Mic drive amplitude", f"{rig.get('drive_amplitude_v', 0)*1000:.1f} mV"],
        ["Scope rate", f"{rig.get('scope_rate_hz', 0)/1000:.1f} kHz"],
        ["Scope range / offset",
         f"{rig.get('scope_range_v', 0):.1f} V pp / "
         f"{rig.get('scope_offset_v', 0):+.2f} V"],
        ["Capture length", f"{rig.get('capture_s', 0)*1000:.0f} ms"],
        ["WaveForms SDK", doc["provenance"].get("dwf_version", "?")],
    ]
    if rig.get("key_down_s") is not None:
        rig_rows.append(["Total key-down time", f"{rig['key_down_s']:.0f} s"])
    a(_md_table(["Property", "Value"], rig_rows))
    a("\nThe scope sample rate is deliberately not a divisor of the radio's "
      "192 kHz output rate. The exciter DAC leaves residual images near its own "
      "sample rate, and at 96 ksps an image at `192000 - f` would alias to "
      "exactly `-f` - straight onto the mirror frequency the sideband "
      "suppression reading uses.\n")

    a("\n## Firmware under test\n")
    p = doc["provenance"]
    a(_md_table(["Property", "Value"], [
        ["Git commit", f"`{p.get('git_commit', '?')}`"
                       + (" (working tree dirty)" if p.get("git_dirty") else "")],
        ["BuildInfo.h", f"`{p.get('build_info', '?')}`"],
        ["Radio ID", doc.get("radio_baseline", {}).get("id", "?")],
    ]))

    a("\n## Radio state\n")
    base = doc.get("radio_baseline", {})
    a(_md_table(["Setting", "At start"], [
        ["Sample rate", f"{base.get('sample_rate_hz', '?')} sps"],
        ["Band", base.get("current_band", ["?"])[base.get("active_vfo", 0)]],
        ["Modulation", base.get("modulation", ["?"])[base.get("active_vfo", 0)]],
        ["Mic gain", f"{base.get('mic_gain_db', '?')} dB"],
        ["Transmit equaliser", str(base.get("equalizer_xmt", "?"))],
        ["PA 100 W active", base.get("pa_100w", "?")],
    ]))
    a(f"\nState restored afterwards: **{'yes' if doc.get('state_restored') else 'NO'}**")
    if doc.get("state_residual_diff"):
        a(f" - these did not return to baseline: `{doc['state_residual_diff']}`")
    a("\n")

    a("\n## Headline result\n")
    rows = doc.get("comparison", {}).get("corners", [])
    if rows:
        headers = ["Feature", "Nominal Hz"] + [f"{int(r)//1000}k" for r in rates] + \
                  ["Delta %", "If still broken", "Verdict"]
        table_rows = []
        for row in rows:
            nom = row.get("nominal_hz")
            cells = [row["label"], f"{nom:.0f}" if nom else "-"]
            for r in rates:
                v = row.get("value_hz", {}).get(str(r))
                cells.append(f"{v:.1f}" if v is not None else "-")
            d = row.get("delta_pct")
            cells.append(f"{d:+.2f}" if d is not None else "-")
            cells.append(f"{row['legacy_predicted_delta_pct']:.3f}")
            cells.append(f"**{row['verdict']}**")
            table_rows.append(cells)
        a(_md_table(headers, table_rows))
        a("\nThe high corner is the one that carries the result. It is set by "
          "`TXInterpolateBy2Again`'s filter, the stage that limits the "
          "transmitted audio bandwidth, and it is regenerated per rate. The low "
          "corner is set by the equaliser bank's lowest cell at 198 Hz, with the "
          "microphone input's AC coupling in series, so it is a weaker but still "
          "useful witness.\n")

    a("\n## Per-rate detail\n")
    for r in rates:
        bucket = doc["rates"][r]
        a(f"\n### {int(r)} sps\n")
        if bucket.get("skipped"):
            a(f"\nSkipped: `{bucket['skipped']}`\n")
            continue
        pb = bucket.get("passband", {})
        sb = bucket.get("sideband_suppression", {})
        al = bucket.get("alias", {})
        ca = bucket.get("carrier", {})
        detail = [
            ["Audio rate", f"{bucket.get('audio_rate_hz', 0):.1f} Hz"],
            ["Hilbert rate", f"{bucket.get('hilbert_rate_hz', 0):.1f} Hz"],
            ["Passband reference", f"{pb.get('reference_dbv', float('nan')):.1f} dBV"],
            ["Passband ripple", f"{pb.get('ripple_db', float('nan')):.2f} dB"],
            ["-3 dB corners",
             f"{pb.get('corner_lo_3db_hz', float('nan')):.0f} Hz to "
             f"{pb.get('corner_hi_3db_hz', float('nan')):.0f} Hz"],
        ]
        if sb.get("worst_db") is not None:
            detail.append(["Sideband suppression, worst",
                           f"{sb['worst_db']:.1f} dB at {sb['worst_at_hz']:.0f} Hz"])
        if al:
            detail.append(["Fold point (audio rate / 4)",
                           f"{al.get('nyquist_after_fold_hz', float('nan')):.0f} Hz"])
            if al.get("noise_floor_dbc") is not None:
                detail.append(["Rig noise floor",
                               f"{al['noise_floor_dbc']:.1f} dBc"])
            if al.get("worst_direct_dbc") is not None:
                detail.append([
                    "Out-of-band rejection, worst",
                    f"{al['worst_direct_dbc']:.1f} dBc"
                    + (" (at the noise floor)"
                       if al.get("worst_direct_floor_limited") else "")])
            if al.get("worst_fold_dbc") is not None:
                detail.append([
                    "Fold-back product, worst",
                    f"{al['worst_fold_dbc']:.1f} dBc from "
                    f"{al.get('worst_fold_at_hz', float('nan')):.0f} Hz"
                    + (" (at the noise floor, so nothing measurable folded)"
                       if al.get("worst_fold_floor_limited") else "")])
        if ca.get("worst_dbc") is not None:
            detail.append(["Carrier residue, worst", f"{ca['worst_dbc']:.1f} dBc"])
        a(_md_table(["Measurement", "Value"], detail))

    if png_paths:
        a("\n## Figures\n")
        for png in png_paths:
            name = os.path.basename(png)
            a(f"\n### {name}\n\n![{name}]({name})\n")

    a("\n## All checks\n")
    a(_md_table(["Check", "Result", "Value", "Limit", "Notes"], [
        [f"`{c['id']}`",
         "SKIP" if c["skipped"] else ("PASS" if c["passed"] else "**FAIL**"),
         f"{c['value']:.3f} {c['units']}" if c["value"] is not None else "-",
         f"{c['limit']:.3f}" if c["limit"] is not None else "-",
         c["message"]]
        for c in doc["checks"]]))

    a("\n## Caveats\n")
    a("- **Sideband suppression is checked against a floor, not for rate "
      "invariance.** The Hilbert table is not regenerated per rate, and that is "
      "correct: a Hilbert transformer's usable band is a fraction of its sample "
      "rate, so one fixed table is the same design at any rate and its edges are "
      "*meant* to move with Fs.\n")
    a("- **A clean fold-back result does not on its own vindicate the "
      "decimator.** The equaliser bank runs before `TXDecimateBy2Again` and its "
      "highest cell is at 4 kHz, so it already attenuates an 8 kHz input "
      "substantially. The result proves nothing folds into the transmitted "
      "audio, which is what matters operationally; "
      "`TransmitChain176k.DecimatorStopsBeforeTheFoldPoint` is what proves the "
      "decimator's own stopband, by evaluating its taps at the fold point.\n")
    a("- Absolute corner frequencies carry the whole analog path, including the "
      "codec front end and the equaliser bank, neither of which can be removed "
      "over CAT. The rate comparison divides all of it out.\n")
    a("- The transmit DSP gain scales with the requested output power "
      "(`TXGain`), so the power setting must not change during a run. The suite "
      "reads it at the start and reports it.\n")
    a("- Do not touch the front panel during a run. It triggers a settings save "
      "and would persist the test configuration.\n")

    a("\n## Reproduce\n")
    a(f"```bash\n{p.get('command_line', '')}\n```\n")
    a("\nRegenerate the figures from the saved results without the hardware:\n")
    a(f"```bash\npython3 plot_tx_filter_hil.py "
      f"{os.path.basename(doc.get('artifacts', {}).get('json', 'results.json'))}\n```\n")

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w") as fh:
        fh.write("\n".join(md))
    return path
