#!/usr/bin/env bash
# Source from bash. EIM's activation file assumes an interactive shell, so use
# its documented environment output rather than sourcing it from another script.
if [[ -n "${IDF_PATH:-}" && -f "$IDF_PATH/tools/cmake/project.cmake" ]]; then
    . "$IDF_PATH/export.sh"
elif [[ -f "$HOME/.espressif/tools/activate_idf_v5.4.4.sh" ]]; then
    p4game_saved_path="$PATH"
    while IFS='=' read -r p4game_key p4game_value; do
        case "$p4game_key" in
            PATH) export PATH="$p4game_value:$p4game_saved_path" ;;
            SYSTEM_PATH) ;;
            IDF_PATH|IDF_TOOLS_PATH|IDF_PYTHON_ENV_PATH|IDF_COMPONENT_LOCAL_STORAGE_URL|ESP_ROM_ELF_DIR|OPENOCD_SCRIPTS|ESP_IDF_VERSION|IDF_VERSION)
                export "$p4game_key=$p4game_value" ;;
        esac
    done < <(bash "$HOME/.espressif/tools/activate_idf_v5.4.4.sh" -e)
    [[ -n "${IDF_PATH:-}" ]] || return 1
    export PATH="$IDF_PATH/tools:$PATH"
    unset p4game_key p4game_value p4game_saved_path
elif [[ -f "$HOME/esp/esp-idf/export.sh" ]]; then
    . "$HOME/esp/esp-idf/export.sh"
else
    printf '%s\n' "ESP-IDF 5.4.4 not found; set IDF_PATH to its installation." >&2
    return 1
fi
