#!/usr/bin/env python3
"""ADR-0007 POC sweep gate (B19; spec 04 T015, ADR-0007 confirmation).

Runs the host sweep bench (tests/host/poc_sweep) over the ADR-0007
range (2,000..6,000 entities, constant 1,000), converts the counted
unit ops with the same documented conservative coefficient the B05
cycle harness uses (check_cycles.CYCLES_PER_OP), and decides the cap:

  measured cap = the highest swept N where BOTH
    - cycles/unit  <= CYCLES_PER_UNIT_CAP      (150)
    - cycles/frame <= CYCLES_PER_FRAME_BUDGET  (300,000)
  hold. The cap may land BELOW the ADR floor (ADR-0007: "cap may land
  below 2,000 if the POC measures worse") but is never enlarged past
  the sweep.

It then enforces "0 units shipped above the measured gate": the
POC_POOL_SHIP_CAP claim inside src/sim/poc_pool.h must be <= the
measured cap. CI uses this file (`make poc`).

Modes:
  check_poc.py              # build not required here; run sweep + gate
  check_poc.py --selftest   # feed a deliberately-failing sweep and
                            # prove the gate BITES (exit must be 1)
"""
from __future__ import annotations

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SWEEP = ROOT / "tests" / "host" / "bin" / "poc_sweep"
POOL_HEADER = ROOT / "src" / "sim" / "poc_pool.h"

# Single source of truth with the B05 harness.
sys.path.insert(0, str(ROOT / "tools"))
from check_cycles import (  # noqa: E402
    CYCLES_PER_FRAME_BUDGET, CYCLES_PER_OP, CYCLES_PER_UNIT_CAP,
)

ROW = re.compile(
    r"^poc n=(\d+) steps=(\d+) ops=(\d+) ops_per_unit=(\d+)/(\d+) ",
    re.MULTILINE,
)


def parse_rows(text: str) -> list[dict[str, int]]:
    rows = []
    for m in ROW.finditer(text):
        n, steps, ops, ops_num, ops_den = (int(g) for g in m.groups())
        rows.append({"n": n, "steps": steps, "ops": ops,
                     "ops_num": ops_num, "ops_den": ops_den})
    return rows


def gate(rows: list[dict[str, int]]) -> tuple[int, int]:
    failures: list[str] = []
    passing: list[int] = []
    print(f"poc sweep (ADR-0007): ops -> conservative cycles "
          f"(CYCLES_PER_OP={CYCLES_PER_OP}; gates "
          f"{CYCLES_PER_UNIT_CAP}/unit, "
          f"{CYCLES_PER_FRAME_BUDGET}/frame)")
    for row in rows:
        n = row["n"]
        ops_per_unit = row["ops_num"] / row["ops_den"]
        cyc_unit = ops_per_unit * CYCLES_PER_OP
        cyc_frame = cyc_unit * n
        within = (cyc_unit <= CYCLES_PER_UNIT_CAP and
                  cyc_frame <= CYCLES_PER_FRAME_BUDGET)
        # A row beyond the cap is not a violation - it is the evidence
        # that bounds the cap (ADR-0007 "or the cap drops"). The hard
        # check is ship-cap <= measured, below.
        print(f"  n={n:5d}  ops/unit={ops_per_unit:5.1f}  "
              f"cyc/unit={cyc_unit:6.1f}  cyc/frame={cyc_frame:9.0f}  "
              f"{'in-cap' if within else 'cap-bound'}")
        if within:
            passing.append(n)
    measured = max(passing) if passing else 0
    print(f"  measured cap (highest swept N passing both gates): {measured}")

    # "0 units shipped above the measured gate": the pool header may
    # not claim more than the sweep measured.
    m = re.search(r"#define\s+POC_POOL_SHIP_CAP\s+\(\(uint32_t\)(\d+)u?\)",
                  POOL_HEADER.read_text())
    if m is None:
        failures.append(
            "POC_POOL_SHIP_CAP not found in src/sim/poc_pool.h")
    else:
        ship = int(m.group(1))
        if ship > measured:
            failures.append(
                f"ship cap {ship} > measured cap {measured}: ADR-0007 "
                "forbids shipping above the measured gate")
        else:
            print(f"  ship cap {ship} <= measured cap {measured} "
                  "(0 units above the measured gate)")

    if failures:
        print("\n".join(f"- {f}" for f in failures))
        print(f"poc: {len(failures)} violation(s).")
        return 1, measured
    print("poc: cap measured and enforced (ADR-0007 gates hold).")
    return 0, measured


SELFTEST_BAD = (
    "poc n=2000 steps=16 ops=256000 ops_per_unit=16000/2000 fold=1 alive=0,0\n"
    "poc n=3000 steps=16 ops=384000 ops_per_unit=24000/3000 fold=1 alive=0,0\n"
    "poc n=4000 steps=16 ops=512000 ops_per_unit=32000/4000 fold=1 alive=0,0\n"
    "poc n=5000 steps=16 ops=640000 ops_per_unit=40000/5000 fold=1 alive=0,0\n"
    "poc n=6000 steps=16 ops=768000 ops_per_unit=48000/6000 fold=1 alive=0,0\n"
    "poc_sweep range=2000-6000 step=1000 done\n"
)


def selftest() -> int:
    """The gate must not only pass: prove it BITES. The injected data
    stands at 8 ops/unit x 30 = 240 cycles/unit, over the 150 gate at
    EVERY N, so the measured cap must come out 0 and the exit code
    must be 1 - which also rejects any header claiming the ADR floor."""
    print("selftest: deliberately-failing sweep (8 ops/unit -> 240 "
          "cycles/unit at every N):")
    rc, measured = gate(parse_rows(SELFTEST_BAD))
    if rc == 0:
        print("selftest: gate did NOT bite on a failing sweep.")
        return 1
    if measured != 0:
        print("selftest: measured cap must be 0 on a failing sweep.")
        return 1
    print("selftest: gate bites correctly (exit 1, cap 0).")
    # And the ship-cap fitness function: the same data must reject a
    # header claiming the ADR floor.
    ship = re.search(r"POC_POOL_SHIP_CAP", POOL_HEADER.read_text())
    if ship is None:
        print("selftest: POC_POOL_SHIP_CAP vanished from poc_pool.h.")
        return 1
    return 0


def main() -> int:
    if "--selftest" in sys.argv[1:]:
        return selftest()
    if not SWEEP.exists():
        print("poc: no sweep bench at tests/host/bin/poc_sweep; "
              "`make poc` builds it first.")
        return 1
    proc = subprocess.run([str(SWEEP)], capture_output=True, text=True)
    if proc.returncode != 0:
        print(f"poc: sweep exited {proc.returncode}")
        print(proc.stdout[-400:])
        return 1
    rows = parse_rows(proc.stdout)
    if not rows:
        print("poc: sweep printed no data rows.")
        return 1
    if "poc_sweep range" not in proc.stdout:
        print("poc: sweep did not finish its range.")
        return 1
    gate_rc, _ = gate(rows)
    return gate_rc


if __name__ == "__main__":
    sys.exit(main())
