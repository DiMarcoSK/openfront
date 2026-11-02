#!/usr/bin/env bash
# Host (gcc) unit tests. Wired into `make test-host`; the DS code builds
# on devkitARM separately (see Makefile `rom`).
set -eu

cd "$(dirname "$0")/../.." || exit 1

BIN_DIR="tests/host/bin"
CFLAGS="-Wall -Wextra -std=c11 -Iinclude -Isrc -Itests/host"
BIN="$BIN_DIR/test_queue"

mkdir -p "$BIN_DIR"
gcc $CFLAGS tests/host/test_queue.c \
  src/queue/queue.c src/input/input.c src/sim/sim.c src/save/save.c \
  -o "$BIN" -lm
"$BIN"
echo "host tests passed."
