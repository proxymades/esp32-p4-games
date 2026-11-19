#pragma once
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#define BOARD_LCD_SCLK 2
#define BOARD_LCD_MOSI 3
#define BOARD_LCD_CS 26
#define BOARD_LCD_DC 5
#define BOARD_LCD_RST 20
#define BOARD_BUTTON_UP 21
#define BOARD_BUTTON_DOWN 22
#define BOARD_BUTTON_LEFT 23
#define BOARD_BUTTON_RIGHT 32
#define BOARD_BUTTON_A 46
#define BOARD_BUTTON_B 47
#define BOARD_BUTTON_C 48
#define BOARD_BUTTON_START 4
#define BOARD_I2C_SDA 7
#define BOARD_I2C_SCL 8
#define BOARD_I2S_DOUT 9
#define BOARD_I2S_WS 10
#define BOARD_I2S_DIN 11
#define BOARD_I2S_BCLK 12
#define BOARD_I2S_MCLK 13
#define BOARD_AMP_ENABLE 53
#define BOARD_SD_D0 39
#define BOARD_SD_D1 40
#define BOARD_SD_D2 41
#define BOARD_SD_D3 42
#define BOARD_SD_CLK 43
#define BOARD_SD_CMD 44
#define BOARD_SD_POWER 45
#define BOARD_C6_RESET 54
#define BOARD_CODEC_ADDR 0x18

#define PIN_BIT(pin) (UINT64_C(1) << (pin))
#define BOARD_LCD_MASK (PIN_BIT(BOARD_LCD_SCLK) | PIN_BIT(BOARD_LCD_MOSI) | PIN_BIT(BOARD_LCD_CS) | PIN_BIT(BOARD_LCD_DC) | PIN_BIT(BOARD_LCD_RST))
#define BOARD_BUTTON_MASK (PIN_BIT(BOARD_BUTTON_UP) | PIN_BIT(BOARD_BUTTON_DOWN) | PIN_BIT(BOARD_BUTTON_LEFT) | PIN_BIT(BOARD_BUTTON_RIGHT) | PIN_BIT(BOARD_BUTTON_A) | PIN_BIT(BOARD_BUTTON_B) | PIN_BIT(BOARD_BUTTON_C) | PIN_BIT(BOARD_BUTTON_START))
#define BOARD_PROTECTED_MASK (PIN_BIT(0) | PIN_BIT(1) | PIN_BIT(6) | PIN_BIT(7) | PIN_BIT(8) | PIN_BIT(9) | PIN_BIT(10) | PIN_BIT(11) | PIN_BIT(12) | PIN_BIT(13) | PIN_BIT(14) | PIN_BIT(15) | PIN_BIT(16) | PIN_BIT(17) | PIN_BIT(18) | PIN_BIT(19) | PIN_BIT(24) | PIN_BIT(25) | PIN_BIT(27) | PIN_BIT(28) | PIN_BIT(29) | PIN_BIT(30) | PIN_BIT(31) | PIN_BIT(33) | PIN_BIT(34) | PIN_BIT(35) | PIN_BIT(36) | PIN_BIT(37) | PIN_BIT(38) | PIN_BIT(39) | PIN_BIT(40) | PIN_BIT(41) | PIN_BIT(42) | PIN_BIT(43) | PIN_BIT(44) | PIN_BIT(45) | PIN_BIT(49) | PIN_BIT(50) | PIN_BIT(51) | PIN_BIT(52) | PIN_BIT(53) | PIN_BIT(54))
_Static_assert(__builtin_popcountll(BOARD_LCD_MASK | BOARD_BUTTON_MASK) == 13, "Duplicate control GPIO");
_Static_assert(((BOARD_LCD_MASK | BOARD_BUTTON_MASK) & BOARD_PROTECTED_MASK) == 0, "Control GPIO conflicts with onboard peripheral");

extern const int board_button_pins[8];
extern const char *const board_button_names[8];
esp_err_t board_init(void);
i2c_master_bus_handle_t board_i2c(void);
esp_err_t board_probe_codec(void);

esp_err_t board_verify_pinmux(void);
