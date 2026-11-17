#!/usr/bin/env python3
"""Record and summarise the carrier null's performance log.

The carrier null (Transmit IQ screen, button 14, DIRECT_COUPLED_TX builds) prints
one CARRCAL line per step on the radio's diagnostics port: the first port of a
Dual Serial build, /dev/ttyACM0. The format is documented at the top of
code/src/PhoenixSketch/HardwareSm_TransmitCarrierCalibration.cpp:

    CARRCAL,BAND,<band>,<I before>,<Q before>
    CARRCAL,READ,<band>,<pass>,<step>,<I>,<Q>,<tone dB>,<carrier dB>,<raw dBc>,<dBc>,<best dBc>,<best value>
    CARRCAL,PASS,<band>,<pass>,<I|Q>,<value kept>,<best dBc>,<valid 0|1>
    CARRCAL,DONE,<band>,<I>,<Q>,<dBc>
    CARRCAL,ABORT,<band>,<I restored>,<Q restored>
    CARRCAL,END

Passes 0, 2, 4 sweep the I offset and 1, 3, 5 the Q offset: a coarse pass over
-5000..+4900 in steps of 100, then +/-12 steps of 10 and of 1 around the value
kept. "dBc" is how far the carrier is below the tone (the smoothed figure the
algorithm compares); "raw dBc" is the same from the latest spectrum alone.

Usage:
    carrier_cal_log.py record [--port /dev/ttyACM0] [--out carrier.csv] [--plot carrier.png]
        Start this, then press 14 on the radio's Transmit IQ screen. Records
        until the run ends (or Ctrl-C), then prints the summary.
    carrier_cal_log.py summary carrier.csv [--plot carrier.png]
        Summarise a recorded log (or any text containing CARRCAL lines).

The summary flags, per band:
    invalid     a pass with no reading above 0 dB kept the old offset
    edge        a fine pass kept a value at the edge of its window: the null
                may lie outside it
    lag         the smoothed reading the algorithm compares differs from the
                raw one by more than 3 dB at the kept value
    shallow     the final carrier is less than 30 dB below the tone
"""

from __future__ import annotations

import argparse
import sys
import time
from collections import OrderedDict, defaultdict
from dataclasses import dataclass, field

DIAG_PORT = "/dev/ttyACM0"
DIAG_BAUD = 115200
SHALLOW_DB = 30.0
LAG_DB = 3.0


@dataclass
class Read:
    pas: int
    step: int
    i: int
    q: int
    tone: float
    carrier: float
    raw: float
    dbc: float
    best: float
    best_value: int


@dataclass
class Pass:
    pas: int
    axis: str
    value: int
    best: float
    valid: bool


@dataclass
class Band:
    name: str
    start: tuple[int, int] | None = None
    reads: list[Read] = field(default_factory=list)
    passes: list[Pass] = field(default_factory=list)
    done: tuple[int, int, float] | None = None
    aborted: bool = False


def parse(lines) -> tuple["OrderedDict[str, Band]", bool]:
    """Parse CARRCAL lines; the last run in the text wins for each band."""
    bands: "OrderedDict[str, Band]" = OrderedDict()
    ended = False
    for raw in lines:
        line = raw.strip()
        k = line.find("CARRCAL,")
        if k < 0:
            continue
        f = line[k:].split(",")
        kind = f[1] if len(f) > 1 else ""
        try:
            if kind == "BAND":
                bands[f[2]] = Band(f[2], start=(int(f[3]), int(f[4])))
                bands.move_to_end(f[2])
            elif kind == "READ":
                b = bands.setdefault(f[2], Band(f[2]))
                b.reads.append(Read(int(f[3]), int(f[4]), int(f[5]), int(f[6]), float(f[7]),
                                    float(f[8]), float(f[9]), float(f[10]), float(f[11]), int(f[12])))
            elif kind == "PASS":
                b = bands.setdefault(f[2], Band(f[2]))
                b.passes.append(Pass(int(f[3]), f[4], int(f[5]), float(f[6]), f[7] == "1"))
            elif kind == "DONE":
                b = bands.setdefault(f[2], Band(f[2]))
                b.done = (int(f[3]), int(f[4]), float(f[5]))
            elif kind == "ABORT":
                bands.setdefault(f[2], Band(f[2])).aborted = True
            elif kind == "END":
                ended = True
        except (IndexError, ValueError):
            continue  # a line cut short on the serial port
    return bands, ended


def flags(b: Band) -> list[str]:
    out = []
    by_pass = defaultdict(list)
    for r in b.reads:
        by_pass[r.pas].append(r)
    for p in b.passes:
        if not p.valid:
            out.append(f"invalid(p{p.pas})")
            continue
        swept = [(r.i if p.pas % 2 == 0 else r.q) for r in by_pass[p.pas]]
        if p.pas >= 2 and swept and p.value in (min(swept), max(swept)):
            out.append(f"edge(p{p.pas})")
        at = [r for r in by_pass[p.pas] if (r.i if p.pas % 2 == 0 else r.q) == p.value]
        if at and abs(at[-1].dbc - at[-1].raw) > LAG_DB:
            out.append(f"lag(p{p.pas}:{at[-1].dbc:.0f}/{at[-1].raw:.0f})")
    if b.done and b.done[2] < SHALLOW_DB:
        out.append("shallow")
    if b.aborted:
        out.append("aborted")
    return out


def summary(bands: "OrderedDict[str, Band]", ended: bool) -> str:
    rows = [f"{'band':5} {'start I,Q':>13} {'final I,Q':>13} {'dBc':>6}  "
            f"{'pass values (best dBc)':52} flags"]
    for b in bands.values():
        start = f"{b.start[0]},{b.start[1]}" if b.start else "?"
        final = f"{b.done[0]},{b.done[1]}" if b.done else "-"
        dbc = f"{b.done[2]:.1f}" if b.done else "-"
        passes = " ".join(f"{p.axis}{p.value}({p.best:.0f})" for p in b.passes)
        rows.append(f"{b.name:5} {start:>13} {final:>13} {dbc:>6}  {passes:52} {' '.join(flags(b))}")
    rows.append("run complete" if ended else "run not complete (no END line)")
    return "\n".join(rows)


def plot(bands: "OrderedDict[str, Band]", path: str) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    names = [n for n, b in bands.items() if b.reads]
    if not names:
        return
    fig, axes = plt.subplots(len(names), 4, figsize=(16, 2.6 * len(names)), squeeze=False)
    for row, name in enumerate(names):
        b = bands[name]
        for k, axis in enumerate("IQ"):
            kept_fine = [p.value for p in b.passes if p.pas == k + 2]
            for zoom in (False, True):
                ax = axes[row][2 * k + zoom]
                for pas in range(k, 6, 2):
                    rs = [r for r in b.reads if r.pas == pas]
                    if not rs:
                        continue
                    x = [(r.i if axis == "I" else r.q) for r in rs]
                    line, = ax.plot(x, [r.dbc for r in rs], "-", lw=1, label=f"pass {pas}")
                    ax.plot(x, [r.raw for r in rs], ".", ms=2, color=line.get_color())
                    kept = [p for p in b.passes if p.pas == pas]
                    if kept and kept[0].valid:
                        ax.axvline(kept[0].value, color=line.get_color(), lw=0.8, ls=":")
                if zoom and kept_fine:
                    ax.set_xlim(kept_fine[0] - 150, kept_fine[0] + 150)
                ax.set_title(f"{name}: {axis} offset" + (", around the null" if zoom else ""), fontsize=9)
                ax.set_ylabel("carrier below tone, dB", fontsize=8)
                ax.tick_params(labelsize=7)
                ax.grid(alpha=0.3)
                if not zoom:
                    ax.legend(fontsize=7)
    fig.suptitle("Carrier null: line = smoothed reading, dots = raw, dotted = value kept", fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, 0.985))
    fig.savefig(path, dpi=110)
    print(f"plot written to {path}")


def record(port: str, out: str) -> list[str]:
    import serial

    lines = []
    print(f"recording {port} into {out}; press 14 on the radio's Transmit IQ screen (Ctrl-C to stop)")
    with serial.Serial(port, DIAG_BAUD, timeout=1) as ser, open(out, "w") as f:
        try:
            while True:
                raw = ser.readline().decode(errors="replace").strip()
                if "CARRCAL," not in raw:
                    continue
                line = raw[raw.find("CARRCAL,"):]
                f.write(line + "\n")
                f.flush()
                lines.append(line)
                kind = line.split(",")[1]
                if kind in ("BAND", "PASS", "DONE", "ABORT", "END"):
                    print(time.strftime("%H:%M:%S"), line)
                if kind in ("END", "ABORT"):
                    break
        except KeyboardInterrupt:
            print("stopped")
    return lines


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    rec = sub.add_parser("record", help="record a run from the radio")
    rec.add_argument("--port", default=DIAG_PORT)
    rec.add_argument("--out", default=time.strftime("carrier_%Y%m%d_%H%M%S.csv"))
    rec.add_argument("--plot")
    summ = sub.add_parser("summary", help="summarise a recorded log")
    summ.add_argument("log")
    summ.add_argument("--plot")
    args = ap.parse_args()

    if args.cmd == "record":
        lines = record(args.port, args.out)
    else:
        with open(args.log, errors="replace") as f:
            lines = f.readlines()
    bands, ended = parse(lines)
    if not bands:
        print("no CARRCAL lines found")
        return 1
    print(summary(bands, ended))
    if args.plot:
        plot(bands, args.plot)
    return 0


if __name__ == "__main__":
    sys.exit(main())
