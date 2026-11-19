idf_component_register(
    SRCS "app_main.c" "board.c" "diagnostics.c" "game.c" "checksum.c"
         "audio_resample.c" "audio_output.c" "buttons.c" "button_filter.c"
         "display.c" "storage.c"
    INCLUDE_DIRS "."
    REQUIRES gwenesis esp_psram esp_driver_gpio esp_driver_i2c esp_driver_i2s
             esp_timer esp_lcd esp_driver_spi fatfs sdmmc esp_driver_sdmmc esp_codec_dev mbedtls
)
if(CONFIG_P4GAME_ROM_TEST)
    if(NOT P4GAME_TEST_ROM OR NOT EXISTS "${P4GAME_TEST_ROM}")
        message(FATAL_ERROR "ROM test requires -DP4GAME_TEST_ROM=/path/to/owned/ROM.gen")
    endif()
    target_add_binary_data(${COMPONENT_LIB} "${P4GAME_TEST_ROM}" BINARY RENAME_TO test_rom)
endif()
