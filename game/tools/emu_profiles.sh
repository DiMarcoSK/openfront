#!/usr/bin/env bash
# Emulator save profiles (B05; spec-01 R8/R14, spec-06 R5, ADR-0002).
# The card surface the ROM expects is a 512 B EEPROM-equivalent block with
# DLDI available; both emulators must hand the ROM those bytes so `make run`
# and the save suite behave identically.
#   - melonDS 1.1: `Advance Save-> Raw save type` defaults to auto; a raw
#     .sav file of 512 bytes is mapped as EEPROM when present.
#   - DeSmuME: Preferences -> Emulation -> Save type = EEPROM 512 bytes.
# Usage:
#   emu_profiles.sh check   verify local ROM + profile presence (CI-safe)
#   emu_profiles.sh seed melonds|desmume  create an empty correct .sav
set -eu
EMU=${2:-}
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SAV="$ROOT/wartable.sav"

print_help() {
	sed -n '1,20p' "$0" | sed 's/^# \{1\}\?//'
}

check() {
	local ok=1
	if [ ! -f "$ROOT/wartable.nds" ]; then
		echo "profiles: wartable.nds missing (run make rom)."; ok=0
	fi
	if [ -f "$SAV" ] && [ "$(stat -c %s "$SAV")" -ne 512 ]; then
		echo "profiles: $SAV exists but is not 512 B (EEPROM size)."; ok=0
	fi
	if [ $ok -ne 0 ]; then
		echo "profiles: ROM expects DLDI + EEPROM 512 B; see tools/emu_profiles.sh header."
	fi
	return $(( 1 - ok ))
}

seed() {
	case "$EMU" in
	melonds|desmume)
		if [ -s "$SAV" ] && [ "$(stat -c %s "$SAV")" -ne 512 ]; then
			echo "profiles: refusing to overwrite a non-EEPROM $SAV with different size." >&2
			exit 1
		fi
		dd if=/dev/zero of="$SAV" bs=512 count=1 2>/dev/null
		echo "profiles: seeded $SAV (512 B EEPROM, both slots blank)."
		;;
	*)
		print_help; exit 1;;
	esac
}

case "${1:-help}" in
	check) check;;
	seed) seed;;
	*) print_help;;
esac
