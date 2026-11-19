#include "buttons.h"
#include <stdatomic.h>
#include "board.h"
#include "button_filter.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static atomic_uint s_pressed;

static void poll_buttons(void *arg)
{
    (void)arg;
    button_filter_t filter = {0};

#if CONFIG_P4GAME_PERIPHERAL_TEST
    uint8_t previous = 0;
#endif
    TickType_t next = xTaskGetTickCount();
    for (;;) {
        uint8_t raw = 0;
        for (unsigned key = 0; key < 8; ++key) {
            if (gpio_get_level(board_button_pins[key]) == 0) {
                raw |= 1U << key;
            }
        }
        const uint8_t stable = button_filter_update(&filter, raw, (uint32_t)esp_timer_get_time());
        atomic_store_explicit(&s_pressed, stable, memory_order_relaxed);

#if CONFIG_P4GAME_PERIPHERAL_TEST
        if (stable != previous) {
            ESP_LOGI("buttons", "pressed=0x%02x (UP DOWN LEFT RIGHT A B C START)", stable);
        }
        previous = stable;
#endif
        vTaskDelayUntil(&next, pdMS_TO_TICKS(1));
    }
}

esp_err_t buttons_start(void)
{
    return xTaskCreatePinnedToCore(poll_buttons, "buttons", 2048, NULL, 4, NULL, 1) == pdPASS ?
           ESP_OK : ESP_ERR_NO_MEM;
}

uint8_t buttons_pressed(void)
{
    return (uint8_t)atomic_load_explicit(&s_pressed, memory_order_relaxed);
}
