#!/usr/bin/env bash
# ci.sh - the blocking CI job (B25 spec-06 T016/T029, B26 SDD-8).
# Every fitness function in one exit code: the ROM builds with 0 warnings,
# module isolation + palette + network + licence gates, every host suite
# (incl. the 1000-cut matrix, the 10k fuzz and the 100-battle golden
# hash), the byte/cycle budget, the 8/8 contrast pairs, the POC cap gate,
# the battle balance gate (GAME_DESIGN 16) and the grayscale fixture. Any red step fails the job. `make ci` runs it.
set -u
cd "$(dirname "$0")/.." || exit 1

fail=0
step() {
  local name="$1"
  shift
  printf 'ci: %-14s ' "$name"
  local out
  if out=$("$@" 2>&1); then
    echo "ok"
  else
    echo "FAIL"
    echo "$out" | tail -25
    fail=1
  fi
}

rom_no_warnings() {
  local log
  log=$(make rom 2>&1) || { echo "$log"; return 1; }
  if echo "$log" | grep -qE "warning:"; then
    echo "$log" | grep -E "warning:"
    return 1
  fi
}

step rom            rom_no_warnings
step include-graph  python3 tools/check_modules.py
step test-host      bash tests/host/run.sh
step budget         ./tools/check_budget.sh
step contrast       python3 tools/contrast.py
step poc            make -s poc
step balance        make -s balance
step grayscale      python3 tools/grayscale.py

if [ "$fail" -ne 0 ]; then
  echo "ci: RED - fix the failing step above."
  exit 1
fi
echo "ci: GREEN - all fitness functions pass."
