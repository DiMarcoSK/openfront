#!/usr/bin/env bash
# rtk.sh - RTK for this project, driven from WSL through cmd.exe interop.
# Everything the agent runs locally ends up token-compressed; a broken call
# degrades to the unfiltered output (and a diagnostic line), never to a fail.
# Usage: ./tools/rtk.sh <rtk-command> [args...]
set -u

case "${1:-}" in
  "") echo "usage: $0 <rtk-command> [args...]" >&2; exit 2 ;;
esac

RTK_WIN='C:\Users\black\AppData\Local\Microsoft\WinGet\Packages\rtk-ai.rtk_Microsoft.Winget.Source_8wekyb3d8bbwe\rtk.exe'
RTK_IX='/mnt/c/Users/black/AppData/Local/Microsoft/WinGet/Packages/rtk-ai.rtk_Microsoft.Winget.Source_8wekyb3d8bbwe/rtk.exe'
if [ ! -f "$RTK_IX" ]; then
  echo "rtk not found at $RTK_IX; install it with 'winget install rtk-ai.rtk'" >&2
  exit 2
fi

# Normalise the common problem: paths and cwd from WSL to Windows view.
cd_to_windows() {
  printf 'C:%s' "${PWD#/mnt/c}" | sed 's|/|\\|g'
}

cwd_win=$(cd_to_windows)
cd_dev_dir=$cwd_win

# rtk reads .rtk/filters.toml from process cwd, so the wrapper preserves it.
cmd.exe /c "cd /d $cd_dev_dir && $RTK_WIN $*" 2>&1 | grep -v "^Microsoft Windows \[Version\|^\s*$"
