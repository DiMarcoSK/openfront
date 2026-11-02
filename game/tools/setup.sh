#!/usr/bin/env bash
# Seeds C:/devkitPro/winbin with plain-name copies of the Windows devkitARM
# drivers (as, ld, cc1, cc1plus, collect2, grit, ndstool, ...) so the .exe
# driver can CreateProcess them while running inside WSL / MSYS2 interop.
# Idempotent; safe to run on a fresh machine or in CI.
# Requires C:\devkitPro to be present (devkitPro updater 3.0.3 installed).
set -eu

DEST=C:/devkitPro/winbin
SRC=/mnt/c/devkitPro

mkdir -p "$DEST"
echo "seeding $DEST"
for base in as ld ar ranlib nm objcopy size strings c++ cpp gcc g++; do
  if [ -f "$SRC/devkitARM/bin/arm-none-eabi-$base.exe" ]; then
    cp -f "$SRC/devkitARM/bin/arm-none-eabi-$base.exe" "$DEST/$base"
  fi
done
for sub in cc1 cc1plus collect2; do
  cp -f "$SRC/devkitARM/libexec/gcc/arm-none-eabi/16.1.0/$sub.exe" "$DEST/$sub"
done
cp -f "$SRC/devkitARM/lib/gcc/arm-none-eabi/16.1.0/sync-none.specs" "$DEST/sync-none.specs"
for tool in grit ndstool bin2s; do
  cp -f "$SRC/tools/bin/$tool.exe" "$DEST/$tool"
done
echo "B01 toolchain ready: $(ls -1 $DEST | wc -l) files."
