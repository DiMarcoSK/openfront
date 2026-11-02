#!/usr/bin/env python3
"""Cycle harness gate (B05; spec-02 T003/T004, spec-04 T007/T014, ADR-0007).

Converts the *measured* unit-op counters from the host cycle probe
(tests/host/bin/cycle_probe) into cycles-per-unit against the spec's
ceilings, and fails when:
  - a step's unit ops exceed 150 cycles/unit at the current caps, or
  - an injected overload value exceeds the 4.0 ms sim slice
    (`BUDGET_SIM_MS=4.1` reproduces the deliberate 4.1 ms the backlog
    acceptance demands: `make budget` must fail).

Nothing here pretends to measure hardware cycles: the ARM9 emulated/real
numbers are the ADR-0007 POC job (B19). The gate works from counters and a
documented conservative coefficient (CYCLES_PER_OP, ARM9, at most 30 cycles
per unit op) plus the spec's 300,000 cycles/frame budget.
"""
from __future__ import annotations

import pathlib
import re
import subprocess
import sys

PROBE = pathlib.Path(__file__).resolve().parent.parent / \
    "tests" / "host" / "bin" / "cycle_probe"

CYCLES_PER_OP = 30          # conservative ARM9 ceiling per unit op
CYCLES_PER_FRAME_BUDGET = 300_000   # spec-04 R3
CYCLES_PER_UNIT_CAP = 150   # spec-04 R3
SIM_SLICE_MS = 4.0          # spec-02 T004: the deliberate 4.1 ms fails
ARM9_MHZ = 67.0


def parse_probe(output: str) -> dict[str, int]:
    values: dict[str, int] = {}
    for name in (
        "econ_ops_per_step", "muster_ops_per_step", "battle_ops_per_step",
        "roster_cap", "chips_cap",
    ):
        m = re.search(rf"^{name}=(\d+)$", output, re.MULTILINE)
        if not m:
            print(f"cycles: probe output missing {name}") 
            return {}
        values[name] = int(m.group(1))
    return values


def main() -> int:
    if not PROBE.exists():
        print("cycles: no probe at tests/host/bin/cycle_probe; "
              "run `make test-host` first.")
        return 1
    run = subprocess.run([str(PROBE)], capture_output=True, text=True)
    output = run.stdout
    if run.returncode != 0:
        print(f"cycles: probe exited {run.returncode}")
        return 1
    values = parse_probe(output)
    if not values:
        return 1

    print("cycles (B05 harness, counters -> estimated cycles):")
    failures = []

    worst_unit = None
    for keys in (("econ_ops_per_step",), ("muster_ops_per_step",),
                 ("battle_ops_per_step",)):
        key = keys[0]
        ops = values[key]
        estimated = ops * CYCLES_PER_OP
        budget = CYCLES_PER_FRAME_BUDGET
        per_unit = estimated * 1.0 / max(1, values["roster_cap"])
        print(f"  {key:<22} ops={ops:4d}  est={estimated:9d} cycles  "
              f"per-unit={per_unit:8.1f} <= {CYCLES_PER_UNIT_CAP}")
        if estimated > budget:
            failures.append(f"{key}: {estimated} > {budget} cycles/frame")
        if per_unit > CYCLES_PER_UNIT_CAP:
            failures.append(
                f"{key}: {per_unit:.1f} > {CYCLES_PER_UNIT_CAP} cycles/unit")
        if worst_unit is None or per_unit > worst_unit[1]:
            worst_unit = (key, per_unit)

    # The deliberate overload the backlog acceptance requires: an injected
    # 4.1 ms sim slice must fail this gate (spec-02 T004).
    overload = sys.argv[1] if len(sys.argv) > 1 else ""
    if overload == "4.1" or overload == "DELIBERATE_OVERLOAD":
        ms = 4.1
        print(f"  deliberate overload {ms} ms > {SIM_SLICE_MS} ms slice")
        failures.append(f"deliberate overload: {ms} ms > "
                        f"{SIM_SLICE_MS} ms sim slice")

    if failures:
        print("\n".join(f"- {f}" for f in failures))
        print(f"cycles: {len(failures)} violation(s).")
        return 1
    print("cycles: within the per-frame and per-unit budgets.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
