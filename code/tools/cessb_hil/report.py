"""Markdown report and plots for a CESSB bench run (from the run's JSON document)."""

from __future__ import annotations

import os

import numpy as np


def _f(x, fmt="{:.2f}"):
    return "—" if x is None else fmt.format(x)


def markdown(doc: dict, pngs: list) -> str:
    L = []
    w = L.append
    r = doc.get("radio", {})
    w(f"# CESSB bench test — {doc['started']}\n")
    w(f"**Overall: {doc.get('overall', '?')}** · SSB power {r.get('power_w')} W · "
      f"{r.get('rate_hz')} sps · key-down {doc.get('key_down_s', 0):.0f} s\n")
    c = doc.get("ceiling", {})
    if c.get("found"):
        w(f"Ceiling: **{c['ceiling_v']:.4f} V** envelope at the exciter; knee at "
          f"{doc['knee_amplitude_v']*1000:.1f} mV into the microphone input. "
          f"Held level spread {c['limited_spread_db']:.2f} dB over {c['limited_points']} points.\n")
    pt = doc.get("power_tracking")
    if pt:
        w(f"Power tracking: {pt['p_full_w']} W → {pt['p_half_w']} W moved the ceiling "
          f"{pt['measured_db']:+.2f} dB (expected {pt['expected_db']:+.2f} dB).\n")
    for name, sd in doc.get("stimuli", {}).items():
        ref = sd["reference"]
        w(f"\n## {name}\n")
        w(f"Linear reference (CESSB off, below the ceiling): PAPR {ref['papr_db']:.2f} dB, "
          f"opposite sideband {ref['opposite_dbc']:.1f} dBc, out of channel "
          f"{ref['out_of_channel_dbc']:.1f} dBc.\n")
        tone = any("imd3_low_dbc" in row["on"] for row in sd["rows"])
        hdr = "| drive dB | CESSB | peak × ceiling | talk-power gain dB | opposite SB dBc | out of channel dBc |"
        if tone:
            hdr += " 2f1−f2 dBc | 2f2−f1 dBc | +500 Hz mirror dBc |"
        w(hdr)
        w("|" + "---:|" * (hdr.count("|") - 1))
        for row in sd["rows"]:
            m = row.get("model", {})
            for label, x in (("off", row["off"]), ("on", row["on"])):
                line = (f"| {row['drive_db']:g} | {label} | {x['peak_x_ceiling']:.3f}"
                        f"{' (clipped)' if x['scope_clipped'] else ''} | "
                        f"{x['talk_power_gain_db']:+.2f} | {x['opposite_dbc']:.1f} | "
                        f"{x['out_of_channel_dbc']:.1f} |")
                if tone:
                    line += (f" {x['imd3_low_dbc']:.1f} | {x['imd3_high_dbc']:.1f} | "
                             f"{x['imd3_low_mirror_dbc']:.1f} |")
                w(line)
            line = (f"| {row['drive_db']:g} | model | {_f(m.get('peak'), '{:.3f}')} | "
                    f"{_f(m.get('talk_power_gain_db'), '{:+.2f}')} | "
                    f"{_f(m.get('opposite_dbc'), '{:.1f}')} | "
                    f"{_f(m.get('out_of_channel_dbc'), '{:.1f}')} |")
            if tone:
                line += " — | — | — |"
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
    C = doc.get("ceiling", {}).get("ceiling_v")

    sweep = doc.get("tone_sweep", [])
    if sweep and C:
        a = np.array([p["amplitude_v"] for p in sweep]) * 1000
        fig, ax = plt.subplots(figsize=(7, 4.5))
        ax.loglog(a, [p["off_v"] for p in sweep], "o-", label="CESSB off")
        ax.loglog(a, [p["on_v"] for p in sweep], "s-", label="CESSB on")
        ax.axhline(C, color="k", ls="--", lw=0.8, label=f"ceiling {C:.3f} V")
        ax.set(xlabel="1 kHz tone into the mic input (mV peak)",
               ylabel="exciter envelope (V)", title="Single-tone transfer")
        ax.grid(True, which="both", alpha=0.3)
        ax.legend()
        fig.tight_layout()
        p = base + "_transfer.png"
        fig.savefig(p, dpi=110)
        plt.close(fig)
        out.append(p)

    stim = doc.get("stimuli", {})
    if stim:
        fig, axs = plt.subplots(1, 2, figsize=(12, 4.5))
        for name, sd in stim.items():
            d = [row["drive_db"] for row in sd["rows"]]
            l, = axs[0].plot(d, [row["on"]["talk_power_gain_db"] for row in sd["rows"]],
                             "o-", label=f"{name} on")
            axs[0].plot(d, [row["model"].get("talk_power_gain_db", np.nan) for row in sd["rows"]],
                        ":", color=l.get_color(), label=f"{name} model")
            axs[0].plot(d, [row["off"]["talk_power_gain_db"] for row in sd["rows"]],
                        "x--", color=l.get_color(), alpha=0.6, label=f"{name} off")
            axs[1].plot(d, [row["on"]["peak_x_ceiling"] for row in sd["rows"]], "o-",
                        color=l.get_color(), label=f"{name} on")
            axs[1].plot(d, [row["off"]["peak_x_ceiling"] for row in sd["rows"]], "x--",
                        color=l.get_color(), alpha=0.6, label=f"{name} off")
        axs[0].set(xlabel="drive above ceiling (dB)", ylabel="talk-power gain at equal peak (dB)",
                   title="Average power gained")
        axs[1].axhline(1.0, color="k", ls="--", lw=0.8)
        axs[1].set(xlabel="drive above ceiling (dB)", ylabel="peak envelope / ceiling",
                   title="Peak envelope")
        for ax in axs:
            ax.grid(True, alpha=0.3)
            ax.legend(fontsize=7)
        fig.tight_layout()
        p = base + "_drive.png"
        fig.savefig(p, dpi=110)
        plt.close(fig)
        out.append(p)
    return out
