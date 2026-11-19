#include "display.h"
#include <inttypes.h>
#include <stdatomic.h>
#include <string.h>
#include "board.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define WIDTH 320
#define HEIGHT 240
#define BAND_LINES 16
typedef struct {
    uint16_t *pixels;
    unsigned frame;
    unsigned height;
} display_job_t;
static const char *TAG = "display";
static QueueHandle_t s_free;
static QueueHandle_t s_ready;
static SemaphoreHandle_t s_dma_free;
static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_band[2];
static atomic_uint s_presented;
static atomic_uint s_replaced;
static atomic_uint s_errors;
static atomic_uint s_last_us;

static bool IRAM_ATTR color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *context)
{
    (void)io;
    (void)event;
    (void)context;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_dma_free, &woken);
    return woken == pdTRUE;
}

static esp_err_t push_frame(const uint16_t *frame, unsigned height)
{
    esp_err_t status = ESP_OK;
    unsigned band = 0;
    static unsigned previous_height;
    const bool full = previous_height != height;
    const int top = full ? 0 : (HEIGHT - (int)height) / 2;
    const int bottom = full ? HEIGHT : top + (int)height;
    previous_height = height;
    for (int y = top; y < bottom; y += BAND_LINES) {
        const int lines = bottom - y < BAND_LINES ? bottom - y : BAND_LINES;
        xSemaphoreTake(s_dma_free, portMAX_DELAY);
        for (int p = 0; p < WIDTH * lines; ++p) {
            const uint16_t pixel = frame[y * WIDTH + p];
            s_band[band][p] = (uint16_t)((pixel >> 8) | (pixel << 8));
        }
        status = esp_lcd_panel_draw_bitmap(s_panel, 0, y, WIDTH, y + lines, s_band[band]);
        if (status != ESP_OK) {
            xSemaphoreGive(s_dma_free);
            break;
        }
        band ^= 1;
    }
    /* Both bounce buffers must be returned before source-frame ownership ends. */
    xSemaphoreTake(s_dma_free, portMAX_DELAY);
    xSemaphoreTake(s_dma_free, portMAX_DELAY);
    xSemaphoreGive(s_dma_free);
    xSemaphoreGive(s_dma_free);
    return status;
}

static void display_worker(void *arg)
{
    (void)arg;
    for (;;) {
        display_job_t job;
        xQueueReceive(s_ready, &job, portMAX_DELAY);
        const int64_t start = esp_timer_get_time();
        if (push_frame(job.pixels, job.height) == ESP_OK) {
            atomic_fetch_add(&s_presented, 1);
        } else {
            atomic_fetch_add(&s_errors, 1);
        }
        atomic_store(&s_last_us, (unsigned)(esp_timer_get_time() - start));
        xQueueSend(s_free, &job.pixels, portMAX_DELAY);
    }
}

esp_err_t display_init(void)
{
    for (unsigned i = 0; i < 2; ++i) {
        s_band[i] = heap_caps_malloc(WIDTH * BAND_LINES * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        ESP_RETURN_ON_FALSE(s_band[i], ESP_ERR_NO_MEM, TAG, "DMA bounce buffers");
    }
    s_free = xQueueCreate(2, sizeof(uint16_t *));
    s_ready = xQueueCreate(1, sizeof(display_job_t));
    s_dma_free = xSemaphoreCreateCounting(2, 2);
    ESP_RETURN_ON_FALSE(s_free && s_ready && s_dma_free, ESP_ERR_NO_MEM, TAG, "display queues");
    for (unsigned i = 0; i < 2; ++i) {
        uint16_t *pixels = heap_caps_calloc(WIDTH * HEIGHT, sizeof(*pixels), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ESP_RETURN_ON_FALSE(pixels, ESP_ERR_NO_MEM, TAG, "framebuffers");
        xQueueSend(s_free, &pixels, portMAX_DELAY);
    }
    const spi_bus_config_t bus = {
        .sclk_io_num = BOARD_LCD_SCLK, .mosi_io_num = BOARD_LCD_MOSI, .miso_io_num = -1,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = WIDTH * BAND_LINES * 2,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "SPI2");
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BOARD_LCD_CS, .dc_gpio_num = BOARD_LCD_DC,
        .pclk_hz = CONFIG_P4GAME_LCD_CLOCK_HZ,
        .spi_mode = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .trans_queue_depth = 4, .on_color_trans_done = color_done,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io), TAG, "LCD IO");
    const esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = BOARD_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &dev, &s_panel), TAG, "ST7789");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "inversion");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, true), TAG, "landscape");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, false), TAG, "orientation");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, 0, 0), TAG, "offset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "display on");
    ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(display_worker, "lcd_out", 4096, NULL, 3, NULL, 1) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "display task");
    ESP_LOGI(TAG, "DISPLAY_CONFIGURED ST7789 320x240 SPI=%dHz (panel visibility requires physical check)",
             CONFIG_P4GAME_LCD_CLOCK_HZ);
    return ESP_OK;
}

uint16_t *display_acquire(void)
{
    uint16_t *pixels = NULL;
    if (xQueueReceive(s_free, &pixels, 0) == pdTRUE) {
        return pixels;
    }
    return NULL;
}

esp_err_t display_submit(uint16_t *pixels, unsigned frame, unsigned height)
{
    if (height != 224 && height != 240) {
        xQueueSend(s_free, &pixels, portMAX_DELAY);
        return ESP_ERR_INVALID_ARG;
    }
    display_job_t superseded;
    if (xQueueReceive(s_ready, &superseded, 0) == pdTRUE) {
        atomic_fetch_add(&s_replaced, 1);
        xQueueSend(s_free, &superseded.pixels, portMAX_DELAY);
    }
    const display_job_t job = {.pixels = pixels, .frame = frame, .height = height};
    if (xQueueSend(s_ready, &job, 0) != pdTRUE) {
        xQueueSend(s_free, &pixels, portMAX_DELAY);
        return ESP_FAIL;
    }
    return ESP_OK;
}

void display_stats(void)
{
    ESP_LOGI(TAG, "DISPLAY_STATS presented=%u replaced=%u errors=%u last_transfer_us=%u",
             atomic_load(&s_presented), atomic_load(&s_replaced),
             atomic_load(&s_errors), atomic_load(&s_last_us));
}

esp_err_t display_test_pattern(void)
{
    uint16_t *pixels = display_acquire();
    if (pixels == NULL) {
        return ESP_ERR_NO_MEM;
    }
    static const uint16_t bars[8] = {0xffff, 0xffe0, 0x07ff, 0x07e0, 0xf81f, 0xf800, 0x001f, 0};
    for (unsigned y = 0; y < HEIGHT; ++y) {
        for (unsigned x = 0; x < WIDTH; ++x) {
            pixels[y * WIDTH + x] = bars[x / 40];
        }
    }
    pixels[0] = 0xf800;
    pixels[WIDTH - 1] = 0x07e0;
    pixels[(HEIGHT - 1) * WIDTH] = 0x001f;
    pixels[WIDTH * HEIGHT - 1] = 0xffff;
    return display_submit(pixels, 0, 240);
}
