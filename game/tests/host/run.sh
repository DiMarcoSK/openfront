#!/usr/bin/env bash
# Host (gcc) unit tests. Wired into `make test-host`; the DS code builds
# on devkitARM separately (see Makefile `rom`).
set -eu

cd "$(dirname "$0")/../.." || exit 1

BIN_DIR="tests/host/bin"
CFLAGS="-Wall -Wextra -std=c11 -Iinclude -Isrc -Itests/host"
BIN_DIR="$(pwd)/$BIN_DIR"
mkdir -p "$BIN_DIR"
SAVE_INC="-Isrc/save -Isrc/ui"

build() {
  # $1 = test source, $2 = module sources
  local t="$1"
  shift
  gcc $CFLAGS "$t" "$@" $SAVE_INC -o "$BIN_DIR/$(basename "$t" .c)" -lm
}

run() {
  "$BIN_DIR/$(basename "$1" .c)"
}

# B03: the single conduit (save.c + slots are linked for the coalesced
# boundary now; ui_screen/ui_input are stubs inside the test).
build tests/host/test_queue.c src/queue/queue.c src/input/input.c \
      src/sim/sim.c src/save/save.c src/save/slots.c src/save/crc32.c
run tests/host/test_queue.c

# B06: the save core (slots + crc32; save.c is the thin shell consumed by
# the boot path and shares the high-level state with the emulator build).
build tests/host/test_save.c src/save/slots.c src/save/crc32.c
run tests/host/test_save.c

# B07: boot -> title -> campaign handoff (spec 01 T008/T014-T018, T021).
build tests/host/test_title.c src/ui/title.c src/queue/queue.c \
      src/input/input.c src/sim/sim.c src/save/save.c src/save/slots.c \
      src/save/crc32.c
run tests/host/test_title.c

# B08: campaign list, affordability, ticker, 3-tap recruit handoff.
build tests/host/test_campaign.c src/ui/campaign.c src/queue/queue.c \
      src/input/input.c src/sim/sim.c src/save/save.c src/save/slots.c \
      src/save/crc32.c
run tests/host/test_campaign.c

# B09: base slice (muster, chest, pending cap, autoform, swap).
build tests/host/test_base.c src/ui/base.c src/queue/queue.c \
      src/input/input.c src/sim/sim.c src/save/save.c src/save/slots.c \
      src/save/crc32.c
run tests/host/test_base.c

# B10: battle deterministic seal, Continue-once, Retry, reboot restores.
build tests/host/test_battle.c src/queue/queue.c src/input/input.c \
      src/sim/sim.c src/save/save.c src/save/slots.c src/save/crc32.c
run tests/host/test_battle.c

echo "host tests passed."
