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

# B03: the single conduit (queue/input/sim only; save.c's boot path pulls
# the backend + slot protocol, so it is linked in the B06 suite instead).
build tests/host/test_queue.c src/queue/queue.c src/input/input.c src/sim/sim.c
run tests/host/test_queue.c

# B06: the save core (slots + crc32; save.c is the thin shell consumed by
# the boot path and shares the high-level state with the emulator build).
build tests/host/test_save.c src/save/slots.c src/save/crc32.c
run tests/host/test_save.c

# B07: boot -> title -> campaign handoff (spec 01 T008/T014-T018, T021).
build tests/host/test_title.c src/ui/title.c src/save/save.c \
      src/save/slots.c src/save/crc32.c src/sim/sim.c
run tests/host/test_title.c

echo "host tests passed."
