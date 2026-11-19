set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/prepare_core.py" "${CMAKE_CURRENT_LIST_DIR}/upstream.json")
set(core "${CMAKE_CURRENT_BINARY_DIR}/core")
execute_process(
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/prepare_core.py" --output "${core}"
    RESULT_VARIABLE prepare_result
)
if(NOT prepare_result EQUAL 0)
    message(FATAL_ERROR "Gwenesis source verification/port preparation failed")
endif()
idf_component_register(
    SRCS
        "gwenesis_port.c"
        "${core}/src/bus/gwenesis_bus.c"
        "${core}/src/cpus/M68K/m68kcpu.c"
        "${core}/src/cpus/Z80/Z80.c"
        "${core}/src/io/gwenesis_io.c"
        "${core}/src/sound/z80inst.c"
        "${core}/src/sound/gwenesis_sn76489.c"
        "${core}/src/sound/ym2612.c"
        "${core}/src/vdp/gwenesis_vdp_gfx.c"
        "${core}/src/vdp/gwenesis_vdp_mem.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS
        "${core}/src/bus" "${core}/src/cpus/M68K" "${core}/src/cpus/Z80"
        "${core}/src/io" "${core}/src/sound" "${core}/src/vdp" "${core}/src/savestate"
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    GWENESIS_AUDIO_ACCURATE=1 GWENESIS_AUDIO_CAPACITY=2048)
# Upstream code predates current IDF warnings; do not apply these suppressions
# to the application/platform code.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3 -fno-strict-aliasing -include stdint.h
    -Wno-error -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable
    -Wno-misleading-indentation -Wno-incompatible-pointer-types -Wno-format)
