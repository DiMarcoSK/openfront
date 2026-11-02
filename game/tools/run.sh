#!/usr/bin/env bash
# `RUN=melonds make run` opens the ROM in melonDS. The emulator lives on the
# Windows side (the user's machine), so it is driven through WSL interop.
set -u
EMU=${RUN:-melonds}
ROM=${1:-wartable.nds}
if [ ! -f "$ROM" ]; then
  echo "run: no ROM at $ROM; run 'make rom' first." >&2
  exit 1
fi

if [ "$EMU" != "melonds" ] && [ "$EMU" != "melondes" ]; then
  echo "run: unsupported RUN=$EMU (only melonds is wired for now)." >&2
  exit 1
fi

CAND_IX="/mnt/c/Users/black/Downloads/melonDS-1.1-windows-x86_64 (1)/melonDS.exe"

if [ ! -f "$CAND_IX" ]; then
  echo "run: melonDS.exe not found at $CAND_IX; put it there or edit tools/run.sh." >&2
  exit 1
fi

# From /mnt/c/Users/... to C:\Users\...\wartable.nds
ROM_WIN="C:${PWD#/mnt/c}/$ROM"
ROM_WIN=$(echo "$ROM_WIN" | sed 's|/|\\\\|g')
EMU_WIN=$(echo "$CAND_IX" | sed 's|/|\\\\|g')
echo "run: launching melonDS with $ROM_WIN"
cmd.exe /c start "" "$EMU_WIN" "$ROM_WIN" >/dev/null 2>&1 || true
echo "run: spawned melonDS; the window stays open until you close it."
