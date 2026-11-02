#!/usr/bin/env bash
# Creates C:/devkitPro/winbin (plain-name copies of needed devkitARM exes) so
# the Windows .exe driver can CreateProcess them from within WSL interop.
# Idempotently guarded, so CI can call it early.
set -u
/bin/echo "#B01 begins to copy" || true
TARGET_SYS=/mnt/c/devkitPro/winbin
mkdir -p "$TARGET_SYS"
for b in as ld ar ranlib nm objcopy size strings c++ cpp; do
  s=/mnt/c/devkitPro/devkitARM/bin/arm-none-eabi-$b.exe
  [ -f "$s" ] && cp -f "$s" "$TARGET_SYS/$b"
done
for b in gcc g++ cpp; do
  cp -f /mnt/c/devkitPro/devkitARM/bin/arm-none-eabi-$b.exe "$TARGET_SYS/$b"
done
cp -f /mnt/c/devkitPro/devkitARM/libexec/gcc/arm-none-eabi/16.1.0/cc1.exe    "$TARGET_SYS/cc1"
cp -f /mnt/c/devkitPro/devkitARM/libexec/gcc/arm-none-eabi/16.1.0/cc1plus.exe "$TARGET_SYS/cc1plus"
cp -f /mnt/c/devkitPro/devkitARM/libexec/gcc/arm-none-eabi/16.1.0/collect2.exe "$TARGET_SYS/collect2"
cp -f /mnt/c/devkitPro/devkitARM/lib/gcc/arm-none-eabi/16.1.0/sync-none.specs "$TARGET_SYS/sync-none.specs"
cp -f /mnt/c/devkitPro/tools/bin/grit.exe "$TARGET_SYS/grit"
cp -f /mnt/c/devkitPro/tools/bin/ndstool.exe "$TARGET_SYS/ndstool"
cp -f /mnt/c/devkitPro/tools/bin/bin2s.exe "$TARGET_SYS/bin2s"
/bin/echo "#B01 tools seeded"
