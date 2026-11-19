#include "board.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "soc/usb_wrap_struct.h"
#include "soc/gpio_struct.h"
#include "hal/gpio_ll.h"

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG || CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
#error "Use UART0 console: onboard USB-C is connected through CH343."
#endif

static const char *TAG = "board";
static i2c_master_bus_handle_t s_i2c;
const int board_button_pins[8] = {
    BOARD_BUTTON_UP, BOARD_BUTTON_DOWN, BOARD_BUTTON_LEFT, BOARD_BUTTON_RIGHT,
    BOARD_BUTTON_A, BOARD_BUTTON_B, BOARD_BUTTON_C, BOARD_BUTTON_START
};
const char *const board_button_names[8] = {"UP", "DOWN", "LEFT", "RIGHT", "A", "B", "C", "START"};

esp_err_t board_init(void)
{
    /* Set inactive latch values before enabling outputs. GPIO26 selection also
       disables the USB-FS pad in IDF gpio_ll_func_sel(). No USB stack is started. */
    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_LCD_CS, 1), TAG, "LCD CS");
    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_SD_POWER, 1), TAG, "SD power off");
    const gpio_config_t outputs = {
        .pin_bit_mask = BOARD_LCD_MASK | PIN_BIT(BOARD_AMP_ENABLE) |
                        PIN_BIT(BOARD_SD_POWER) | PIN_BIT(BOARD_C6_RESET),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), TAG, "outputs");
    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_LCD_CS, 1), TAG, "LCD CS high");
    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_LCD_RST, 0), TAG, "LCD held in reset");
    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_AMP_ENABLE, 0), TAG, "amplifier off");
    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_SD_POWER, 1), TAG, "SD power off");
    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_C6_RESET, 0), TAG, "unused C6 held in reset");

    const gpio_config_t keys = {
        .pin_bit_mask = BOARD_BUTTON_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&keys), TAG, "buttons");
    if (USB_WRAP.otg_conf.usb_pad_enable != 0) {
        ESP_LOGE(TAG, "USB-FS still owns GPIO26/27");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "PIN_AUDIT PASS controls=13 usb_fs_pad=off console=UART37/38");

    const i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&bus, &s_i2c);
}

i2c_master_bus_handle_t board_i2c(void)
{
    return s_i2c;
}

esp_err_t board_probe_codec(void)
{
    const esp_err_t err = i2c_master_probe(s_i2c, BOARD_CODEC_ADDR, 100);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "CODEC_I2C PASS addr=0x18 SDA=7 SCL=8 (playback not tested)");
    } else {
        ESP_LOGE(TAG, "CODEC_I2C FAIL: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t board_verify_pinmux(void)
{
    for (unsigned i = 0; i < 8; ++i) {
        bool pu, pd, ie, oe, peripheral_oe, inverted, od, sleep;
        uint32_t drive, function, signal;
        gpio_ll_get_io_config(&GPIO, board_button_pins[i], &pu, &pd, &ie, &oe,
                             &peripheral_oe, &inverted, &od, &drive, &function, &signal, &sleep);
        if (!pu || pd || !ie || oe) {
            ESP_LOGE(TAG, "PINMUX_CHECK FAIL GPIO%d button=%s pu=%d pd=%d ie=%d oe=%d",
                     board_button_pins[i], board_button_names[i], pu, pd, ie, oe);
            return ESP_ERR_INVALID_STATE;
        }
    }
    ESP_RETURN_ON_FALSE(USB_WRAP.otg_conf.usb_pad_enable == 0, ESP_ERR_INVALID_STATE,
                        TAG, "USB-FS re-enabled on GPIO26");
    ESP_LOGI(TAG, "PINMUX_CHECK PASS all_8_buttons=input_pullup usb_fs=off");
    return ESP_OK;
}
