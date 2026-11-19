#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$project_dir"
mode="${1:-board-check}"
case "$mode" in
    board-check|play|peripheral-test) build_dir="build-$mode" ;;
    rom-test) build_dir=".private/build-rom-test" ;;
    *) printf '%s\n' "Usage: bash scripts/build.sh {board-check|play|peripheral-test|rom-test [ROM]}" >&2; exit 2 ;;
esac
build_dir="${P4GAME_BUILD_DIR:-$build_dir}"
set +u
. scripts/idf-env.sh >/dev/null
set -u
sdk_version=$(idf.py --version)
if [[ "$sdk_version" != "ESP-IDF v5.4.4" ]]; then
    printf 'Expected ESP-IDF v5.4.4, got %s\n' "$sdk_version" >&2
    exit 1
fi
args=(-B "$build_dir" -DIDF_TARGET=esp32p4
      "-DSDKCONFIG=$project_dir/$build_dir/sdkconfig"
      "-DSDKCONFIG_DEFAULTS=$project_dir/sdkconfig.defaults;$project_dir/configs/$mode.defaults")
if [[ "$mode" == "rom-test" ]]; then
    [[ -f "${2:-}" ]] || { printf '%s\n' "Provide the ROM path for the private test build." >&2; exit 2; }
    rom_path=$(python3 -c 'from pathlib import Path; import sys; print(Path(sys.argv[1]).resolve())' "$2")
    args+=("-DP4GAME_TEST_ROM=$rom_path")
fi
idf.py "${args[@]}" build
