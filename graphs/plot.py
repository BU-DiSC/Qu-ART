#!/usr/bin/env python3
"""Parse test_kfp output and draw the two bar charts (x = the 4 workloads).

  timings.png  — grouped bars: QuART_kfp<3,FF> vs ART insertion time (ms).
  stats.png    — stacked bars: FP_INSERT / BRIDGE / NO_MATCH classification counts.

Usage: plot.py <times_raw.txt> <stats_raw.txt> <out_dir>
"""
import re
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

WL_RE = re.compile(r"^Workload:\s*(.+?)\s*$")
QUART_RE = re.compile(r"^QuART_kfp<3,FF>:\s*([\d.]+)\s*ms")
ART_RE = re.compile(r"^ART:\s*([\d.]+)\s*ms")
STATS_RE = re.compile(r"FP_INSERT=(\d+)\s+BRIDGE=(\d+)\s+NO_MATCH=(\d+)")


def parse_times(path):
    """Return ordered [(workload, quart_ms, art_ms), ...]."""
    rows, wl, quart = [], None, None
    for line in open(path):
        line = line.strip()
        m = WL_RE.match(line)
        if m:
            wl, quart = m.group(1), None
            continue
        m = QUART_RE.match(line)
        if m:
            quart = float(m.group(1))
            continue
        m = ART_RE.match(line)
        if m and wl is not None:
            rows.append((wl, quart, float(m.group(1))))
            wl = None
    return rows


def parse_stats(path):
    """Return ordered [(workload, fp_insert, bridge, no_match), ...]."""
    rows, wl = [], None
    for line in open(path):
        line = line.strip()
        m = WL_RE.match(line)
        if m:
            wl = m.group(1)
            continue
        m = STATS_RE.search(line)
        if m and wl is not None:
            rows.append((wl, int(m.group(1)), int(m.group(2)), int(m.group(3))))
            wl = None
    return rows


def plot_timings(rows, out):
    labels = [r[0] for r in rows]
    quart = [r[1] for r in rows]
    art = [r[2] for r in rows]
    x = np.arange(len(labels))
    w = 0.38

    fig, ax = plt.subplots(figsize=(9, 5.5))
    b1 = ax.bar(x - w / 2, quart, w, label="QuART_kfp<3,FF>", color="#2b8cbe")
    b2 = ax.bar(x + w / 2, art, w, label="ART", color="#bdbdbd")
    ax.set_ylabel("Insertion time (ms, lower is better)")
    ax.set_title("Insertion time by workload (stats OFF — clean timing)")
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=12, ha="right")
    ax.legend()
    ax.bar_label(b1, fmt="%.0f", padding=2, fontsize=8)
    ax.bar_label(b2, fmt="%.0f", padding=2, fontsize=8)
    # Annotate speedup over ART above each pair.
    for i, (_, q, a) in enumerate(rows):
        if q:
            ax.text(x[i], max(q, a) * 1.06, f"{a / q:.2f}x",
                    ha="center", fontsize=9, fontweight="bold", color="#08589e")
    ax.margins(y=0.18)
    fig.tight_layout()
    fig.savefig(out, dpi=130)
    print("wrote", out)


def plot_stats(rows, out):
    labels = [r[0] for r in rows]
    fp = np.array([r[1] for r in rows], dtype=float)
    bridge = np.array([r[2] for r in rows], dtype=float)
    nomatch = np.array([r[3] for r in rows], dtype=float)
    x = np.arange(len(labels))
    M = 1e6  # show counts in millions

    fig, ax = plt.subplots(figsize=(9, 5.5))
    p1 = ax.bar(x, fp / M, label="FP_INSERT", color="#31a354")
    p2 = ax.bar(x, bridge / M, bottom=fp / M, label="BRIDGE", color="#fdae6b")
    p3 = ax.bar(x, nomatch / M, bottom=(fp + bridge) / M, label="NO_MATCH",
                color="#de2d26")
    ax.set_ylabel("Inserts (millions)")
    ax.set_title("k-fp classification by workload (stats ON)")
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=12, ha="right")
    ax.legend()
    # Percent-of-total label for the dominant FP_INSERT segment.
    total = fp + bridge + nomatch
    for i in range(len(labels)):
        if total[i] > 0:
            ax.text(x[i], (fp[i] / M) / 2, f"{100 * fp[i] / total[i]:.1f}%",
                    ha="center", va="center", fontsize=8, color="white",
                    fontweight="bold")
    fig.tight_layout()
    fig.savefig(out, dpi=130)
    print("wrote", out)


def main():
    times_path, stats_path, out_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    trows = parse_times(times_path)
    srows = parse_stats(stats_path)
    if not trows:
        sys.exit(f"no timing rows parsed from {times_path}")
    if not srows:
        sys.exit(f"no stat rows parsed from {stats_path}")
    plot_timings(trows, f"{out_dir}/timings.png")
    plot_stats(srows, f"{out_dir}/stats.png")


if __name__ == "__main__":
    main()
