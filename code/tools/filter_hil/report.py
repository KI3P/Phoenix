"""Result assembly and rendering for the filter HIL suite.

Three tiers, matching the convention the repo's other tools already follow:
a human summary on stdout, a machine-readable JSON document, and a Markdown
report that stitches the numbers together with the generated figures.

The JSON keeps every raw sweep point, so the report and all its plots can be
regenerated later without the hardware.
"""

from __future__ import annotations

import json
import math
import os
import platform
import subprocess
import sys
import time
from datetime import datetime, timezone
from typing import Optional, Sequence

import numpy as np

from . import bandtable as bt

SCHEMA = "phoenix.filter_hil/1"


def _run(cmd: Sequence[str], cwd: Optional[str] = None) -> str:
    try:
        return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                              timeout=10).stdout.strip()
    except Exception:
        return ""


def collect_provenance(argv: Sequence[str], repo_root: str,
                       dwf_version: str = "") -> dict:
    """Record what was tested and with what, so a result can be traced back."""
    commit = _run(["git", "rev-parse", "--short", "HEAD"], repo_root)
    dirty = bool(_run(["git", "status", "--porcelain"], repo_root))

    build_info = ""
    path = os.path.join(repo_root, "code", "src", "PhoenixSketch", "BuildInfo.h")
    try:
        with open(path) as fh:
            for line in fh:
                if "GIT_COMMIT_HASH" in line or "BUILD_TIMESTAMP" in line:
                    build_info += line.strip() + "  "
    except OSError:
        pass

    versions = {"python": platform.python_version()}
    for mod in ("numpy", "scipy", "matplotlib", "serial"):
        try:
            versions[mod] = __import__(mod).__version__
        except Exception:
            versions[mod] = "not installed"

    return {
        "git_commit": commit,
        "git_dirty": dirty,
        "build_info": build_info.strip(),
        "dwf_version": dwf_version,
        "versions": versions,
        "command_line": " ".join(argv),
        "host": platform.node(),
    }


def build_document(*, provenance: dict, config: dict, rig: dict,
                   baseline: dict, final: dict, state_restored: bool,
                   residual_diff: dict, per_rate: dict, comparison: dict,
                   checks: Sequence, duration_s: float,
                   warnings: Sequence[str]) -> dict:
    """Assemble the full result document."""
    check_dicts = [c.as_dict() if hasattr(c, "as_dict") else c for c in checks]

    groups: dict[str, str] = {}
    for c in check_dicts:
        g = c["group"]
        if c["skipped"]:
            groups.setdefault(g, "SKIPPED")
        elif not c["passed"]:
            groups[g] = "FAIL"
        elif groups.get(g) in (None, "SKIPPED"):
            groups[g] = "PASS"

    passed = sum(1 for c in check_dicts if c["passed"] and not c["skipped"])
    failed = sum(1 for c in check_dicts if not c["passed"] and not c["skipped"])
    skipped = sum(1 for c in check_dicts if c["skipped"])

    overall = "PASS" if failed == 0 and passed > 0 else "FAIL" if failed else "PARTIAL"

    return {
        "schema": SCHEMA,
        "generated_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "duration_s": round(duration_s, 1),
        "provenance": provenance,
        "config": config,
        "rig": rig,
        "radio_baseline": baseline,
        "radio_final": final,
        "state_restored": state_restored,
        "state_residual_diff": residual_diff,
        "rates": per_rate,
        "comparison": comparison,
        "checks": check_dicts,
        "warnings": list(warnings),
        "summary": {
            "overall": overall,
            "passed": passed,
            "failed": failed,
            "skipped": skipped,
            "groups": groups,
        },
        "artifacts": {},
    }


def _jsonable(o):
    """Convert numpy scalars, which json does not recognise.

    Comparisons on numpy floats produce numpy bools, and those leak into the
    pass/fail fields from every ``a <= b`` written against a fitted value.
    """
    if isinstance(o, np.generic):
        item = o.item()
        return None if isinstance(item, float) and not math.isfinite(item) else item
    if isinstance(o, np.ndarray):
        return o.tolist()
    if isinstance(o, float) and not math.isfinite(o):
        return None
    raise TypeError(f"cannot serialise {type(o).__name__}")


def write_json(doc: dict, path: str) -> str:
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w") as fh:
        json.dump(doc, fh, indent=1, default=_jsonable)
    return path


# --- stdout ----------------------------------------------------------------

def print_summary(doc: dict, stream=sys.stdout) -> None:
    """Human-readable result, in the shape the other tools use."""
    s = doc["summary"]
    w = stream.write

    w(f"\n[{s['overall']}] Receive filter rate-independence, "
      f"{doc['duration_s']:.0f} s\n")

    for warning in doc.get("warnings", []):
        w(f"  WARNING: {warning}\n")

    rig = doc.get("rig", {})
    if rig:
        w(f"  Wiring          : {rig.get('wiring', '?')}\n")
        if rig.get("image_rejection_db") is not None:
            w(f"  Image rejection : {rig['image_rejection_db']:.1f} dB\n")
        if rig.get("drive_amplitude_v") is not None:
            w(f"  Drive level     : {rig['drive_amplitude_v']*1000:.1f} mV\n")

    comp = doc.get("comparison", {})
    rates = sorted(doc.get("rates", {}), key=lambda r: -int(r))

    def table(title: str, rows: list, key: str) -> None:
        if not rows:
            return
        w(f"\n  {title}\n")
        head = "    {:>10}".format("nominal")
        for r in rates:
            head += " {:>11}".format(f"{int(r)//1000}k")
        head += " {:>9} {:>7}".format("delta", "verdict")
        w(head + "\n")
        for row in rows:
            line = "    {:>10.1f}".format(row["nominal_hz"])
            for r in rates:
                v = row.get(key, {}).get(str(r))
                line += " {:>11}".format(f"{v:.1f}" if v is not None else "-")
            d = row.get("delta_pct")
            line += " {:>8}%".format(f"{d:+.2f}" if d is not None else "-")
            line += " {:>7}".format(row["verdict"])
            if row.get("legacy_consistent"):
                line += "  <- matches the old frozen-table shift"
            w(line + "\n")

    table("CW audio filters (-3 dB corner, Hz)", comp.get("cw_filters", []), "corner_3db_hz")
    table("Equaliser cells (peak, Hz)", comp.get("eq_cells", []), "peak_hz")
    table("SSB filter [control] (-6 dB edge, Hz)", comp.get("ssb_filter", []), "hi_edge_6db_hz")

    w("\n  Checks:\n")
    for c in doc["checks"]:
        mark = "SKIP" if c["skipped"] else ("OK  " if c["passed"] else "BAD ")
        w(f"   [{mark}] {c['id']:<32} {c['message']}\n")

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

def _md_table(headers: Sequence[str], rows: Sequence[Sequence]) -> str:
    out = ["| " + " | ".join(headers) + " |",
           "|" + "|".join("---" for _ in headers) + "|"]
    for r in rows:
        out.append("| " + " | ".join(str(c) for c in r) + " |")
    return "\n".join(out) + "\n"


def write_markdown(doc: dict, path: str, png_paths: Sequence[str] = ()) -> str:
    """Render the report a human reads."""
    s = doc["summary"]
    rates = sorted(doc.get("rates", {}), key=lambda r: -int(r))
    md: list[str] = []
    a = md.append

    a(f"# Receive filter rate-independence, hardware in the loop\n")
    a(f"**{s['overall']}** - {s['passed']} passed, {s['failed']} failed, "
      f"{s['skipped']} skipped, in {doc['duration_s']:.0f} s.\n")
    a(f"Generated {doc['generated_utc']}.\n")

    for warning in doc.get("warnings", []):
        a(f"> **Warning:** {warning}\n")

    a("\n## What this shows\n")
    a("Each filter's characteristic frequency is measured on the real radio at "
      "every sample rate under test, by injecting a quadrature tone into the I/Q "
      "receive inputs and reading the demodulated audio back off the speaker "
      "output.\n")
    a(f"\nThese filters used to carry frozen coefficient tables, designed offline "
      f"for one audio rate. Run at any other rate, every corner and centre "
      f"frequency scaled by the ratio of the rates - **{bt.LEGACY_DELTA_PCT:.3f}%** "
      f"between 176.4 and 192 ksps. They are now generated from an analog design "
      f"spec on each rate change, so the measured frequencies should not move. "
      f"A result column reading close to {bt.LEGACY_DELTA_PCT:.3f}% means the "
      f"frozen tables are back.\n")
    a("\nResponses are measured as the difference between two captures at the same "
      "injected frequency - filter engaged and bypassed - so the AWG, the codec, "
      "the decimation rolloff, the volume setting and the speaker amplifier all "
      "cancel out.\n")

    a("\n## Rig\n")
    rig = doc.get("rig", {})
    a(_md_table(["Property", "Value"], [
        ["Wiring (detected)", rig.get("wiring", "?")],
        ["Image rejection", f"{rig.get('image_rejection_db', float('nan')):.1f} dB"],
        ["Drive amplitude", f"{rig.get('drive_amplitude_v', 0)*1000:.1f} mV"],
        ["Scope rate", f"{rig.get('scope_rate_hz', 0)/1000:.1f} kHz"],
        ["Capture length", f"{rig.get('capture_s', 0)*1000:.0f} ms"],
        ["WaveForms SDK", doc["provenance"].get("dwf_version", "?")],
    ]))

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
        ["AGC", "off" if base.get("agc") == 0 else f"mode {base.get('agc')}"],
        ["Volume", base.get("audio_volume", "?")],
        ["CW filter", base.get("cw_filter_index", "?")],
    ]))
    a(f"\nState restored afterwards: **{'yes' if doc.get('state_restored') else 'NO'}**")
    if doc.get("state_residual_diff"):
        a(f" - these did not return to baseline: `{doc['state_residual_diff']}`")
    a("\n")

    a("\n## Headline result\n")
    for title, key, rows in (
            ("CW audio filters (-3 dB corner)", "corner_3db_hz",
             doc["comparison"].get("cw_filters", [])),
            ("Equaliser cells (peak)", "peak_hz",
             doc["comparison"].get("eq_cells", [])),
            ("SSB convolution filter (-6 dB edge) - control", "hi_edge_6db_hz",
             doc["comparison"].get("ssb_filter", []))):
        if not rows:
            continue
        a(f"\n### {title}\n")
        headers = ["Nominal Hz"] + [f"{int(r)//1000}k" for r in rates] + \
                  ["Delta %", "If still broken", "Verdict"]
        table_rows = []
        for row in rows:
            cells = [f"{row['nominal_hz']:.1f}"]
            for r in rates:
                v = row.get(key, {}).get(str(r))
                cells.append(f"{v:.1f}" if v is not None else "-")
            d = row.get("delta_pct")
            cells.append(f"{d:+.2f}" if d is not None else "-")
            cells.append(f"{row['legacy_predicted_delta_pct']:.3f}")
            cells.append(f"**{row['verdict']}**")
            table_rows.append(cells)
        a(_md_table(headers, table_rows))

    if doc["comparison"].get("ssb_filter"):
        a("\nThe SSB filter is the control. It has always derived its coefficients "
          "from the true sample rate, so it was never affected by the bug being "
          "hunted. If it shows a shift, the rig or the analysis is wrong rather "
          "than the firmware.\n")

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
    a("- Equaliser cells 0, 1 and 13 are marked edge limited. The first two sit "
      "at or below the SSB filter's low cut and the last sits in the decimation "
      "skirt, whose corner is a fraction of the sample rate and is *meant* to "
      "move with it. Their absolute accuracy is checked loosely; their rate "
      "invariance is not, since the same skirt applies at both rates.\n")
    a("- Absolute frequencies can be biased by a smooth tilt anywhere in the "
      "analog path. The rate comparison cannot: it divides that tilt out.\n")
    a("- Do not touch the front panel during a run. It triggers a settings save "
      "and would persist the test configuration.\n")

    a("\n## Reproduce\n")
    a(f"```bash\n{p.get('command_line', '')}\n```\n")
    a("\nRegenerate the figures from the saved results without the hardware:\n")
    a(f"```bash\npython3 plot_filter_hil.py "
      f"{os.path.basename(doc.get('artifacts', {}).get('json', 'results.json'))}\n```\n")

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w") as fh:
        fh.write("\n".join(md))
    return path
