#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$project_dir"
mode="${1:-}"
port="${2:-}"
case "$mode" in
    board-check) config_key=P4GAME_BOARD_CHECK; build_dir=build-board-check ;;
    peripheral-test) config_key=P4GAME_PERIPHERAL_TEST; build_dir=build-peripheral-test ;;
    play) config_key=P4GAME_PLAY; build_dir=build-play ;;
    rom-test) config_key=P4GAME_ROM_TEST; build_dir=.private/build-rom-test ;;
    *) printf '%s\n' "Usage: bash scripts/flash.sh {board-check|peripheral-test|play|rom-test} /dev/cu.usbmodem..." >&2; exit 2 ;;
esac
build_dir="${P4GAME_BUILD_DIR:-$build_dir}"
[[ -n "$port" && -e "$port" ]] || { printf '%s\n' "Select the connected board's serial port." >&2; exit 2; }
[[ -f "$build_dir/flash_args" && -f "$build_dir/sdkconfig" ]] || { printf '%s\n' "Build this mode first." >&2; exit 2; }
if lsof "$port" >/dev/null 2>&1; then
    printf '%s\n' "Serial port is busy. Close its monitor before flashing." >&2
    exit 1
fi
set +u
. scripts/idf-env.sh >/dev/null
set -u
python -c 'from pathlib import Path; import sys; s=Path(sys.argv[1]).read_text().splitlines(); assert "CONFIG_"+sys.argv[2]+"=y" in s, "Build mode does not match requested mode"' "$build_dir/sdkconfig" "$config_key"
cd "$build_dir"
python -m esptool --chip esp32p4 -p "$port" -b 460800 \
    --before default_reset --after hard_reset write_flash @flash_args
