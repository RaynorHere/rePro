"""
test_payload_chart.py  --  shushCopy test payload (matplotlib)

Simulates a typical exec/send/recv workflow:
  - Generates sample price trend data
  - Produces a multi-line PNG chart
  - Writes it to test_chart.png in the same directory

Exit code 0 on success, 1 on any error.
Dependencies: python3-matplotlib (apt), python3-numpy (apt)
"""

import sys
import os

try:
    import matplotlib
    matplotlib.use("Agg")   # non-interactive backend -- no display needed
    import matplotlib.pyplot as plt
    import numpy as np
except ImportError as exc:
    print(f"ERROR: missing dependency: {exc}", file=sys.stderr)
    print("Install with: apt-get install python3-matplotlib python3-numpy",
          file=sys.stderr)
    sys.exit(1)

QUARTERS = [
    "Q1\n2022", "Q2", "Q3", "Q4",
    "Q1\n2023", "Q2", "Q3", "Q4",
    "Q1\n2024", "Q2", "Q3", "Q4",
]

PRICES = {
    "Budget":    [549, 529, 519, 509, 499, 489, 479, 469, 459, 449, 439, 429],
    "Mid-range": [899, 919, 909, 929, 949, 939, 959, 979, 969, 989, 999, 1009],
    "Premium":   [1499, 1549, 1529, 1579, 1599, 1649, 1629, 1679, 1699, 1749, 1729, 1779],
}

COLORS = {
    "Budget":    "#2196F3",
    "Mid-range": "#FF9800",
    "Premium":   "#9C27B0",
}


def main():
    x = np.arange(len(QUARTERS))

    fig, ax = plt.subplots(figsize=(12, 6))

    for tier, prices in PRICES.items():
        ax.plot(x, prices,
                marker="o", linewidth=2, markersize=5,
                color=COLORS[tier], label=tier)
        ax.annotate(f"${prices[-1]}",
                    xy=(x[-1], prices[-1]),
                    xytext=(4, 0), textcoords="offset points",
                    va="center", fontsize=9, color=COLORS[tier])

    ax.set_xticks(x)
    ax.set_xticklabels(QUARTERS, fontsize=8)
    ax.set_ylabel("Average Price (USD)")
    ax.set_title("Commercial PC Price Trends  (2022 – 2024)")
    ax.legend(loc="center right")
    ax.grid(axis="y", linestyle="--", alpha=0.4)
    ax.set_ylim(300, 2000)

    fig.tight_layout()

    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_chart.png")
    try:
        fig.savefig(out_path, dpi=120)
    except OSError as exc:
        print(f"ERROR: could not write chart: {exc}", file=sys.stderr)
        return 1
    finally:
        plt.close(fig)

    print(f"OK: wrote {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
