#!/usr/bin/env bash
# tools/memory.sh - mines the project and the debate cases into MemPalace.
# `make memory` runs it; `make memory-check` prints the top recall so the
# agent knows the wing exists. Drawers store verbatim with dedup, so the
# same call is safe to repeat every session.
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WING=${WING:-openfront-war-table}

case "${1:-mine}" in
  mine)
    echo ">> mining $ROOT/cases"
    mempalace mine "$ROOT/cases" --wing "$WING" --agent opencode 2>&1 | tail -4
    echo ">> mining $ROOT/game"
    mempalace mine "$ROOT/game" --wing "$WING" --agent opencode 2>&1 | tail -4
    echo "memory mined into wing $WING"
    ;;
  check)
    mempalace search "War Table" --wing "$WING" --results 3
    ;;
  *)
    echo "usage: $0 [mine|check]" >&2
    exit 2
    ;;
esac
