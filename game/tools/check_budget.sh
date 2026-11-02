#!/usr/bin/env bash
# Budget gate (SDD-4/SDD-5 ceilings), wired into `make budget` (B05).
# Measured facts only: ROM size, RAM/VRAM from the byte harness
# (tools/check_bytes.py), the cycle harness (tools/check_cycles.py) with
# the 8/8 contrast pairs coming with `make budget --contrast`... contrast
# lives in `make validate` and the CI recipe below.
set -u

ROM=wartable.nds
ROM_CAP_MB=16

if [ ! -f "$ROM" ]; then
  echo "budget: missing $ROM; run \`make rom\` first." >&2
  exit 1
fi

ROM_BYTES=$(stat -c %s "$ROM")
ROM_MB=$(( (ROM_BYTES + 1048575) / 1048576 ))

printf 'budget:\n'
printf '  ROM   %7d B (%d MB)  <= %d MB\n' "$ROM_BYTES" "$ROM_MB" "$ROM_CAP_MB"

fail=0
if [ "$ROM_MB" -gt "$ROM_CAP_MB" ]; then
  echo "budget: ROM over cap." >&2
  fail=1
fi

python3 tools/check_bytes.py || fail=1
python3 tools/check_cycles.py || fail=1

exit $fail
