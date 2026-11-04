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
build tests/host/test_queue.c src/queue/queue.c src/loop/trace.c src/input/input.c src/sim/sim.c src/sim/field.c src/sim/war.c src/save/save.c src/save/slots.c src/save/crc32.c src/save/status.c
run tests/host/test_queue.c

# B06: the save core (slots + crc32; save.c is the thin shell consumed by
# the boot path and shares the high-level state with the emulator build).
build tests/host/test_save.c src/save/slots.c src/save/crc32.c
run tests/host/test_save.c

# B13: v1 -> v2 migration + selection byte 62 (spec 02 T013/T033).
build tests/host/test_save_migrate.c src/save/migrate.c src/save/slots.c src/save/crc32.c src/save/save.c src/queue/queue.c src/loop/trace.c src/input/input.c src/sim/sim.c src/sim/field.c src/sim/war.c src/save/status.c
run tests/host/test_save_migrate.c

# B07: boot -> title -> campaign handoff (spec 01 T008/T014-T018, T021).
build tests/host/test_title.c src/ui/title.c src/queue/queue.c src/loop/trace.c src/input/input.c src/sim/sim.c src/sim/field.c src/sim/war.c src/save/save.c src/save/slots.c src/save/crc32.c src/save/status.c src/ui/scene.c src/ui/strings.c src/ui/guide.c tests/host/render_stub.c
run tests/host/test_title.c

# B11: fault-injection harness - 100 flipped-byte boots in slot 1 ->
# 100 slot-2 fallbacks, 0 unplayable, 0 card writes (spec 01 T021).
build tests/host/test_fault_slot.c src/ui/title.c src/queue/queue.c src/loop/trace.c src/input/input.c src/sim/sim.c src/sim/field.c src/sim/war.c src/save/save.c src/save/slots.c src/save/crc32.c src/save/status.c src/ui/scene.c src/ui/strings.c src/ui/guide.c tests/host/render_stub.c
run tests/host/test_fault_slot.c

# B08: campaign list, affordability, ticker, 3-tap recruit handoff.
build tests/host/test_campaign.c src/ui/campaign.c src/queue/queue.c src/loop/trace.c src/input/input.c src/sim/sim.c src/sim/field.c src/sim/war.c src/save/save.c src/save/slots.c src/save/crc32.c src/save/status.c src/ui/scene.c src/ui/strings.c src/ui/guide.c tests/host/render_stub.c
run tests/host/test_campaign.c

# B09: base slice (muster, chest, pending cap, autoform, swap).
build tests/host/test_base.c src/ui/base.c src/ui/base_tier.c src/queue/queue.c src/loop/trace.c src/input/input.c src/sim/sim.c src/sim/field.c src/sim/war.c src/save/save.c src/save/slots.c src/save/crc32.c src/save/status.c src/ui/scene.c src/ui/strings.c src/ui/guide.c tests/host/render_stub.c
run tests/host/test_base.c

# B12: the scripted 3-tap path (campaign list -> Base stepper -> Confirm),
# 20/20 runs (spec 02 T034).
build tests/host/test_flow.c src/ui/campaign.c src/ui/base.c src/ui/base_tier.c src/queue/queue.c src/loop/trace.c src/input/input.c src/sim/sim.c src/sim/field.c src/sim/war.c src/save/save.c src/save/slots.c src/save/crc32.c src/save/status.c src/ui/scene.c src/ui/strings.c src/ui/guide.c tests/host/render_stub.c
run tests/host/test_flow.c

# B10: battle deterministic seal, Continue-once, Retry, reboot restores.
build tests/host/test_battle.c src/queue/queue.c src/loop/trace.c src/input/input.c src/sim/sim.c src/sim/field.c src/sim/war.c src/save/save.c src/save/slots.c src/save/crc32.c src/save/status.c
run tests/host/test_battle.c

# B14: the observability core (diag window, trace pump, overlay cycle).
build tests/host/test_trace.c src/loop/diag.c src/loop/trace.c
run tests/host/test_trace.c

# B17: the tier-band pixel test (geometry, numeral, pin shapes, repaints).
build tests/host/test_tier.c src/ui/base_tier.c
run tests/host/test_tier.c

# B20: speed x1/x2/x4 (R4 badge parity, R5 honest clamp, sub-steps).
build tests/host/test_speed.c src/queue/queue.c src/loop/trace.c src/input/input.c src/sim/sim.c src/sim/field.c src/sim/war.c src/save/save.c src/save/slots.c src/save/crc32.c src/save/status.c
run tests/host/test_speed.c

# B21: orders + one-tap undo (spec-04 R6-R12, T020-T025).
build tests/host/test_order.c src/queue/queue.c src/loop/trace.c src/input/input.c       src/sim/sim.c src/sim/field.c src/sim/war.c src/save/save.c src/save/slots.c src/save/crc32.c src/save/status.c
run tests/host/test_order.c

# B05 cycle harness probe (ops counters -> tools/check_cycles.py gate).
build tests/host/cycle_probe.c src/queue/queue.c src/loop/trace.c src/input/input.c src/sim/sim.c src/sim/field.c src/sim/war.c src/save/save.c src/save/slots.c src/save/crc32.c src/save/status.c


# B24: inline SaveStatus - transition table, rows per host, NO_CARD,
# FULL x2, VERIFY_FAIL Erase + Undo 5 s, 100 commits (spec-06 T018-T023).
build tests/host/test_save_status.c src/ui/settings.c src/ui/scene.c src/ui/strings.c src/ui/guide.c tests/host/render_stub.c src/queue/queue.c src/loop/trace.c src/input/input.c src/sim/sim.c src/sim/field.c src/sim/war.c src/save/save.c src/save/slots.c src/save/crc32.c src/save/status.c
run tests/host/test_save_status.c

# B22 + B23: order panel hold, BattleEnded, 1 save per battle end, resume
# at turn 1, 5 states, 100-battle golden hash, no-touch playthrough.
build tests/host/test_battle_flow.c src/ui/battle.c src/ui/scene.c src/ui/strings.c src/ui/guide.c tests/host/render_stub.c src/queue/queue.c src/loop/trace.c src/input/input.c src/sim/sim.c src/sim/field.c src/sim/war.c src/save/save.c src/save/slots.c src/save/crc32.c src/save/status.c
run tests/host/test_battle_flow.c

# B25: 1000 pulled-cart cuts, 10k fuzz images (CRC-valid hostile half),
# range clamps, the 10,000-unit DoS load and the command flood cap.
build tests/host/test_save_fuzz.c src/queue/queue.c src/loop/trace.c src/input/input.c src/sim/sim.c src/sim/field.c src/sim/war.c src/save/save.c src/save/slots.c src/save/crc32.c src/save/status.c
run tests/host/test_save_fuzz.c

# GAME_DESIGN 7-8: formation kind, terrain, morale, flanks, the general,
# the gods' rules + relics, Retry determinism with abilities.
build tests/host/test_battle_depth.c src/ui/scene.c src/ui/strings.c src/ui/guide.c tests/host/render_stub.c src/queue/queue.c src/loop/trace.c src/input/input.c src/sim/sim.c src/sim/field.c src/sim/war.c src/save/save.c src/save/slots.c src/save/crc32.c src/save/status.c
run tests/host/test_battle_depth.c

# ADR-0009: the endless campaign - threats, deadlines, retakes, wrath
# every 10 wins, eras, the save round trip and the replay.
build tests/host/test_war.c src/queue/queue.c src/loop/trace.c src/input/input.c src/sim/sim.c src/sim/field.c src/sim/war.c src/save/save.c src/save/slots.c src/save/crc32.c src/save/status.c
run tests/host/test_war.c

echo "host tests passed."
