#!/usr/bin/env python3

"""Compare two Google Benchmark JSON result files as a Markdown table."""

from __future__ import annotations

import argparse
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path
from typing import Any

TIME_UNIT_TO_NS = {
    "ns": 1.0,
    "us": 1_000.0,
    "ms": 1_000_000.0,
    "s": 1_000_000_000.0,
}


def samples(path: Path) -> dict[str, list[float]]:
    document: dict[str, Any] = json.loads(path.read_text(encoding="utf-8"))
    grouped: dict[str, list[float]] = defaultdict(list)
    for row in document.get("benchmarks", []):
        if row.get("run_type", "iteration") != "iteration":
            continue
        time_unit = str(row.get("time_unit", "ns"))
        try:
            scale = TIME_UNIT_TO_NS[time_unit]
        except KeyError as error:
            raise ValueError(f"unsupported benchmark time unit: {time_unit}") from error
        grouped[str(row["run_name"])].append(float(row["real_time"]) * scale)
    return dict(grouped)


def coefficient_of_variation(values: list[float]) -> float:
    if len(values) < 2:
        return 0.0
    mean = statistics.fmean(values)
    return 0.0 if mean == 0.0 else statistics.stdev(values) / mean


def format_delta(value: float) -> str:
    return "—" if not math.isfinite(value) else f"{value:+.2f}%"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()

    baseline = samples(arguments.baseline)
    candidate = samples(arguments.candidate)
    names = sorted(set(baseline) & set(candidate))
    lines = [
        "| Benchmark | Baseline median (ns) | Candidate median (ns) | Delta | Baseline CV | Candidate CV |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for name in names:
        baseline_median = statistics.median(baseline[name])
        candidate_median = statistics.median(candidate[name])
        delta = (
            math.nan
            if baseline_median == 0.0
            else (candidate_median / baseline_median - 1.0) * 100.0
        )
        lines.append(
            f"| `{name}` | {baseline_median:.3f} | {candidate_median:.3f} | "
            f"{format_delta(delta)} | {coefficient_of_variation(baseline[name]):.2%} | "
            f"{coefficient_of_variation(candidate[name]):.2%} |"
        )
    output = "\n".join(lines) + "\n"
    if arguments.output is None:
        print(output, end="")
    else:
        arguments.output.write_text(output, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
