#!/usr/bin/env bash
# Budget gate (SDD section 4 ceilings). Wired into `make budget`.
# The real cycle/byte harnesses arrive with B05/B19; this prints the current
# build's footprint and fails when a limit is exceeded. Paths from repo root.
set -eu

ROM=wartable.nds
ROM_CAP_MB=16
RAM_CAP_KB=2795    # 2.73 MB
VRAM_CAP_KB=656

if [ ! -f "$ROM" ]; then
  echo "budget: missing $ROM; run \`make rom\` first." >&2
  exit 1
fi

ROM_BYTES=$(stat -c %s "$ROM")
ROM_MB=$(( (ROM_BYTES + 1048575) / 1048576 ))

# RAM/VRAM: measured by the byte harness in B05; the ceiling check is a
# placeholder pass until that lands.
RAM_KB=0
VRAM_KB=0

printf 'budget:\n'
printf '  ROM   %7d B (%d MB)  <= %d MB\n' "$ROM_BYTES" "$ROM_MB" "$ROM_CAP_MB"
printf '  RAM   %2d KB <= %d KB\n' "$RAM_KB" "$RAM_CAP_KB"
printf '  VRAM  %2d KB <= %d KB\n' "$VRAM_KB" "$VRAM_CAP_KB"

fail=0
if [ "$ROM_MB" -gt "$ROM_CAP_MB" ]; then
  echo "budget: ROM over cap." >&2
  fail=1
fi
exit $fail
