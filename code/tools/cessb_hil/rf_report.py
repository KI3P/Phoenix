"""Markdown report and plots for a CESSB RF run (from the run's JSON document)."""

from __future__ import annotations

import os

import numpy as np


def _f(x, fmt="{:.2f}"):
    return "—" if x is None else fmt.format(x)


def markdown(doc: dict, pngs: list) -> str:
    L = []
    w = L.append
    r = doc.get("radio", {})
    a = doc.get("args", {})
    w(f"# CESSB RF test — {doc['started']}\n")
    w(f"**Overall: {doc.get('overall', '?')}** · {r.get('carrier_hz', 0)/1e6:.6f} MHz "
      f"MD{r.get('md')} · SSB power {r.get('power_w')} W · {r.get('rate_hz')} sps · "
      f"tap {a.get('tap_db')} dB into CMU200 {a.get('rf_input')} · key-down "
      f"{doc.get('key_down_s', 0):.0f} s\n")
    c = doc.get("ceiling")
    if c:
        w(f"Ceiling: PEP **{c['pep_dbm']:.2f} dBm** at the tap = **{c['pep_w']:.2f} W** "
          f"(setting {r.get('power_w')} W = {c['setting_dbm_at_tap']:.2f} dBm at the tap; "
          f"{c['error_db']:+.2f} dB). Held spread {c['spread_db']:.2f} dB. Knee "
          f"{c['knee_v']*1000:.1f} mV into the microphone input.\n")
    pt = doc.get("power_tracking")
    if pt:
        w(f"Power tracking: {pt['p_full_w']} W → {pt['p_low_w']} W moved the held PEP "
          f"{pt['measured_db']:+.2f} dB (expected {pt['expected_db']:+.2f} dB).\n")
    for name, sd in doc.get("stimuli", {}).items():
        w(f"\n## {name}\n")
        rows = [("ref (off)", sd["reference"])] + [(f"{x['drive_db']:g} dB on", x)
                                                   for x in sd["rows"]]
        tone = "imd3_low_dbc" in sd["reference"]
        hdr = ("| | PEP re ceiling dB | PAPR dB | talk-power gain dB | model dB | avg W | "
               "opposite dBc | out of channel dBc | out of channel dB re PEP | carrier dBc |")
        if tone:
            hdr += " 2f1−f2 dBc | 2f2−f1 dBc | 3f1−2f2 dBc | 3f2−2f1 dBc |"
        w(hdr)
        w("|" + "---:|" * (hdr.count("|") - 1))
        for label, x in rows:
            line = (f"| {label} | {x['pep_re_ceiling_db']:+.2f} | {x['papr_db']:.2f} | "
                    f"{_f(x.get('talk_power_gain_db'), '{:+.2f}')} | "
                    f"{_f(x.get('model_talk_power_gain_db'), '{:+.2f}')} | {x['avg_w']:.2f} | "
                    f"{_f(x.get('opposite_dbc'), '{:.1f}')} | "
                    f"{_f(x.get('out_of_channel_dbc'), '{:.1f}')} | "
                    f"{_f(x.get('ooc_re_pep_db'), '{:.1f}')} | "
                    f"{_f(x.get('carrier_dbc'), '{:.1f}')} |")
            if tone:
                line += "".join(f" {_f(x.get(k), '{:.1f}')} |" for k in
                                ("imd3_low_dbc", "imd3_high_dbc", "imd5_low_dbc", "imd5_high_dbc"))
            w(line)
    w("\n## Checks\n")
    w("| | check | value | limit |")
    w("|---|---|---:|---:|")
    for ch in doc.get("checks", []):
        mark = "✅" if ch["passed"] else "❌"
        w(f"| {mark} | {ch['description']} | {_f(ch['value'], '{:.3g}')} {ch['units']} | "
          f"{_f(ch['limit'], '{:.3g}')} |")
    if doc.get("warnings"):
        w("\n## Warnings\n")
        for x in doc["warnings"]:
            w(f"- {x}")
    if pngs:
        w("\n## Plots\n")
        for p in pngs:
            w(f"![{os.path.basename(p)}]({os.path.basename(p)})")
    return "\n".join(L) + "\n"


def plots(doc: dict, base: str) -> list:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    out = []
    c = doc.get("ceiling")
    if not c:
        return out
    C = c["pep_dbm"]

    sweep = doc.get("tone_sweep", [])
    if sweep:
        fig, ax = plt.subplots(figsize=(7, 4.5))
        a = np.array([p["amplitude_v"] for p in sweep]) * 1000
        ax.semilogx(a, [p["on_max_dbm"] for p in sweep], "s-", label="CESSB on, PEP")
        off = [(p["amplitude_v"] * 1000, p["off_max_dbm"]) for p in sweep if "off_max_dbm" in p]
        if off:
            ax.semilogx(*zip(*off), "o--", label="CESSB off, PEP (below the ceiling only)")
        ax.axhline(c["setting_dbm_at_tap"], color="k", ls=":", lw=0.8,
                   label=f"power setting ({doc['radio']['power_w']} W)")
        ax.set(xlabel="1 kHz tone into the mic input (mV peak)", ylabel="PEP at the tap (dBm)",
               title="RF transfer, single tone")
        ax.grid(True, which="both", alpha=0.3)
        ax.legend()
        fig.tight_layout()
        p = base + "_transfer.png"
        fig.savefig(p, dpi=110)
        plt.close(fig)
        out.append(p)

    for name, sd in doc.get("stimuli", {}).items():
        sp = sd.get("spectra", {})
        narrow = [k for k in sp if not k.endswith("_wide")]
        wide = [k for k in sp if k.endswith("_wide")]
        n = 1 + bool(wide)
        fig, axs = plt.subplots(1, n, figsize=(7 * n, 4.5), squeeze=False)
        for k in narrow:
            axs[0][0].plot(np.array(sp[k]["f"]) / 1e3, np.array(sp[k]["p"]) - C, lw=0.9,
                           label="CESSB off, linear" if k == "ref" else
                           f"CESSB on, {k[3:]} dB drive")
        axs[0][0].set(xlabel="offset from carrier, wanted side positive (kHz)",
                      ylabel="dB re ceiling PEP (per RBW)", title=f"{name}: RF spectrum")
        for k in wide:
            axs[0][1].plot(np.array(sp[k]["f"]) / 1e3, np.array(sp[k]["p"]) - C, lw=0.9,
                           label="CESSB off, linear" if k == "ref_wide" else
                           f"CESSB on, {k[3:-5]} dB drive")
        if wide:
            axs[0][1].set(xlabel="offset from carrier (kHz)", ylabel="dB re ceiling PEP (max hold)",
                          title=f"{name}: splatter, wide")
        for ax in axs[0]:
            ax.grid(True, alpha=0.3)
            ax.legend(fontsize=8)
            ax.set_ylim(bottom=-110)
        fig.tight_layout()
        p = base + f"_{name}_spectrum.png"
        fig.savefig(p, dpi=110)
        plt.close(fig)
        out.append(p)

    stim = doc.get("stimuli", {})
    if stim:
        fig, ax = plt.subplots(figsize=(7, 4.5))
        for name, sd in stim.items():
            d = [x["drive_db"] for x in sd["rows"]]
            l, = ax.plot(d, [x["talk_power_gain_db"] for x in sd["rows"]], "o-", label=f"{name} RF")
            ax.plot(d, [x.get("model_talk_power_gain_db") or np.nan for x in sd["rows"]], ":",
                    color=l.get_color(), label=f"{name} model")
        ax.set(xlabel="drive above ceiling (dB)", ylabel="talk-power gain at equal PEP (dB)",
               title="Average power gained, at RF")
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=8)
        fig.tight_layout()
        p = base + "_gain.png"
        fig.savefig(p, dpi=110)
        plt.close(fig)
        out.append(p)
    return out
