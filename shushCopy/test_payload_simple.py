"""
test_payload_simple.py  --  shushCopy test payload (stdlib only)

Simulates a lightweight data-processing script:
  - Generates fake PC price trend data
  - Computes simple statistics
  - Writes results to test_output.json in the same directory

Exit code 0 on success, 1 on any error.
"""

import json
import math
import sys
import os

QUARTERS = [
    "Q1-2022", "Q2-2022", "Q3-2022", "Q4-2022",
    "Q1-2023", "Q2-2023", "Q3-2023", "Q4-2023",
    "Q1-2024", "Q2-2024", "Q3-2024", "Q4-2024",
]

# Fake price data (USD) for three product tiers
PRICES = {
    "budget":    [549, 529, 519, 509, 499, 489, 479, 469, 459, 449, 439, 429],
    "midrange":  [899, 919, 909, 929, 949, 939, 959, 979, 969, 989, 999, 1009],
    "premium":   [1499, 1549, 1529, 1579, 1599, 1649, 1629, 1679, 1699, 1749, 1729, 1779],
}


def mean(values):
    return sum(values) / len(values)


def stddev(values):
    m = mean(values)
    variance = sum((v - m) ** 2 for v in values) / len(values)
    return math.sqrt(variance)


def pct_change(values):
    return ((values[-1] - values[0]) / values[0]) * 100.0


def main():
    stats = {}
    for tier, prices in PRICES.items():
        stats[tier] = {
            "start_price":  prices[0],
            "end_price":    prices[-1],
            "mean":         round(mean(prices), 2),
            "stddev":       round(stddev(prices), 2),
            "pct_change":   round(pct_change(prices), 2),
            "trend":        "down" if prices[-1] < prices[0] else "up",
        }

    output = {
        "quarters": QUARTERS,
        "prices":   PRICES,
        "stats":    stats,
        "summary":  (
            f"Budget tier fell {abs(stats['budget']['pct_change']):.1f}% "
            f"over the period. Premium rose "
            f"{stats['premium']['pct_change']:.1f}%."
        ),
    }

    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_output.json")
    try:
        with open(out_path, "w") as f:
            json.dump(output, f, indent=2)
    except OSError as exc:
        print(f"ERROR: could not write output file: {exc}", file=sys.stderr)
        return 1

    print(f"OK: wrote {out_path}")
    print(f"    {output['summary']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
