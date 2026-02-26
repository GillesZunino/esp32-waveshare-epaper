// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_check.h>

#include <lvgl.h>

#include "waveshare-epaper.h"
#include "lvgl_epaper_port.h"


// ----------------------------------------------------------------
// Display geometry
// ----------------------------------------------------------------
#define EPD_WIDTH       160
#define EPD_HEIGHT      296

// Number of 1/4-screen lines in each LVGL draw buffer
// 74 lines × 160 px × 2 bytes = 23 680 bytes per buffer (fits in DRAM)
#define LVGL_BUF_LINES  74

static const char *TAG = "lvgl_epd_port";


// ----------------------------------------------------------------
// Module-level state
// ----------------------------------------------------------------
static waveshare_epaper_handle_t s_epaper_handle    = NULL;
static uint8_t                  *s_epaper_buf       = NULL;
static size_t                    s_epaper_buf_len   = 0;

// Throttle state (accessed only from epaper_refresh_task after init)
static volatile bool     s_frame_dirty     = false;
static volatile bool     s_refresh_busy    = false;
static volatile int64_t  s_last_refresh_us = 0;   // µs from esp_timer_get_time()

// LVGL thread-safety
static SemaphoreHandle_t s_lvgl_mutex  = NULL;
static lv_display_t     *s_display     = NULL;

// LVGL draw buffers (static, allocated in DRAM)
static lv_color_t s_lvgl_buf1[EPD_WIDTH * LVGL_BUF_LINES];
static lv_color_t s_lvgl_buf2[EPD_WIDTH * LVGL_BUF_LINES];


// ----------------------------------------------------------------
// Color quantization
// ----------------------------------------------------------------

// Map an RGB565 pixel to the nearest native e-paper color code.
//   Code 0x00 = Black   (0,   0,   0)
//   Code 0x01 = White   (255, 255, 255)
//   Code 0x02 = Yellow  (255, 255, 0)
//   Code 0x03 = Red     (255, 0,   0)
//
// Two-step approach:
//   1. Classify pixel as achromatic (gray-scale) or chromatic (colored) based
//      on channel spread. Achromatic pixels come from font anti-aliasing and
//      plain backgrounds; chromatic pixels represent intentional color fills.
//   2. For achromatic pixels use a 75% luminance threshold (BT.601 weighted)
//      so that font edge pixels at ≥25% opacity map to Black instead of White.
//      This eliminates the "washed-out" blurriness caused by the previous
//      50%-luminance Euclidean boundary.
//      For chromatic pixels use squared Euclidean distance against all 4 colors.
static uint8_t rgb565_to_epaper_code(uint16_t rgb565) {
    // Unpack RGB565 → 5/6/5 component fields
    uint8_t r5 = (uint8_t)((rgb565 >> 11) & 0x1F);
    uint8_t g6 = (uint8_t)((rgb565 >>  5) & 0x3F);
    uint8_t b5 = (uint8_t)( rgb565        & 0x1F);

    // Expand to 8-bit per channel
    uint8_t r = (uint8_t)((r5 << 3) | (r5 >> 2));
    uint8_t g = (uint8_t)((g6 << 2) | (g6 >> 4));
    uint8_t b = (uint8_t)((b5 << 3) | (b5 >> 2));

    // Step 1: compute channel spread to classify achromatic vs. chromatic
    uint8_t ch_max = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
    uint8_t ch_min = (r < g) ? ((r < b) ? r : b) : ((g < b) ? g : b);

    if ((ch_max - ch_min) < 32u) {
        // Achromatic pixel (gray-scale): use BT.601 luminance threshold at 75%
        // (192/255). This ensures font edge pixels at ≥25% opacity → Black,
        // making anti-aliased text strokes appear bold and crisp.
        uint32_t luma = (299u * (uint32_t)r + 587u * (uint32_t)g + 114u * (uint32_t)b) / 1000u;
        return (luma < 192u) ? 0x00u : 0x01u;
    }

    // Chromatic pixel: find nearest e-paper color via squared Euclidean distance
#define SQ(v) ((int32_t)(v) * (int32_t)(v))

    int32_t d_black  = SQ(r)       + SQ(g)       + SQ(b);
    int32_t d_white  = SQ(255 - r) + SQ(255 - g) + SQ(255 - b);
    int32_t d_yellow = SQ(255 - r) + SQ(255 - g) + SQ(b);
    int32_t d_red    = SQ(255 - r) + SQ(g)       + SQ(b);

#undef SQ

    uint8_t  code = 0x00;
    int32_t  best = d_black;
    if (d_white  < best) { best = d_white;  code = 0x01; }
    if (d_yellow < best) { best = d_yellow; code = 0x02; }
    if (d_red    < best) {                  code = 0x03; }
    return code;
}


// ----------------------------------------------------------------
// LVGL flush callback
// ----------------------------------------------------------------

static void epaper_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
    const int32_t x1 = area->x1;
    const int32_t y1 = area->y1;
    const int32_t x2 = area->x2;
    const int32_t y2 = area->y2;
    const int32_t area_w = x2 - x1 + 1;

    const uint16_t *color_p = (const uint16_t *)px_map;
    const uint32_t  stride  = (uint32_t)(EPD_WIDTH / 4);  // bytes per row in e-paper buffer

    for (int32_t y = y1; y <= y2; y++) {
        for (int32_t x = x1; x <= x2; x++) {
            uint16_t rgb565    = color_p[(y - y1) * area_w + (x - x1)];
            uint8_t  epc       = rgb565_to_epaper_code(rgb565);
            uint32_t byte_idx  = (uint32_t)y * stride + (uint32_t)(x / 4);
            uint8_t  bit_shift = (uint8_t)(6 - ((x % 4) * 2));

            s_epaper_buf[byte_idx] = (uint8_t)(
                (s_epaper_buf[byte_idx] & ~(0x03u << bit_shift)) |
                ((epc & 0x03u) << bit_shift)
            );
        }
    }

    // Check whether this is the last flush of the current frame BEFORE
    // calling flush_ready(), as flush_ready() may trigger the next render.
    bool is_last = lv_display_flush_is_last(display);

    // Signal to LVGL that we are done with this region.
    // We return immediately — the actual hardware refresh is throttled separately.
    lv_display_flush_ready(display);

    if (is_last) {
        s_frame_dirty = true;
        ESP_LOGD(TAG, "Frame composed — marked dirty");
    }
}


// ----------------------------------------------------------------
// LVGL tick source (ESP timer, fires every 1 ms)
// ----------------------------------------------------------------

static void lvgl_tick_timer_cb(void *arg) {
    (void)arg;
    lv_tick_inc(1);
}


// ----------------------------------------------------------------
// LVGL handler task
// ----------------------------------------------------------------

static void lvgl_handler_task(void *pvParam) {
    (void)pvParam;
    ESP_LOGI(TAG, "LVGL handler task started");

    while (true) {
        lvgl_epaper_port_lock(portMAX_DELAY);
        uint32_t sleep_ms = lv_task_handler();
        lvgl_epaper_port_unlock();

        // Sleep for the time LVGL suggests, clamped to a reasonable range
        if (sleep_ms < 1)   sleep_ms = 1;
        if (sleep_ms > 10)  sleep_ms = 10;
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }
}


// ----------------------------------------------------------------
// E-paper hardware refresh task (throttled)
// ----------------------------------------------------------------

// Time to wait after physically powering on the display
#define POWER_ON_DELAY_TICKS   pdMS_TO_TICKS(50)
// Time to wait after physically powering off the display
#define POWER_OFF_DELAY_TICKS  pdMS_TO_TICKS(10)

static void epaper_refresh_task(void *pvParam) {
    (void)pvParam;
    ESP_LOGI(TAG, "E-paper refresh task started (min interval = %lu ms)", EPAPER_LVGL_MIN_REFRESH_MS);

    while (true) {
        // Sleep between checks — no need to poll more often than every 5 seconds
        vTaskDelay(pdMS_TO_TICKS(5000));

        if (!s_frame_dirty || s_refresh_busy) {
            continue;
        }

        int64_t now_us   = esp_timer_get_time();
        int64_t elapsed_ms = (now_us - s_last_refresh_us) / 1000;

        if (elapsed_ms < (int64_t)EPAPER_LVGL_MIN_REFRESH_MS) {
            continue;   // minimum interval not yet elapsed
        }

        // --------------------------------------------------------
        // Perform a full hardware refresh cycle
        // --------------------------------------------------------
        s_refresh_busy = true;
        s_frame_dirty  = false;
        ESP_LOGI(TAG, "Starting e-paper hardware refresh");

        esp_err_t err = ESP_OK;

        err = waveshare_epaper_hardware_power_on_and_deassert_reset(s_epaper_handle, POWER_ON_DELAY_TICKS);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Power-on failed: %s", esp_err_to_name(err));
            goto refresh_done;
        }

        err = waveshare_epaper_configure_display(s_epaper_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Configure display failed: %s", esp_err_to_name(err));
            goto refresh_done;
        }

        err = waveshare_epaper_send_data_buffer(s_epaper_handle, s_epaper_buf, s_epaper_buf_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Send data buffer failed: %s", esp_err_to_name(err));
            goto refresh_done;
        }

        // Refresh the display; do NOT enter deep-sleep so the driver remains
        // ready for the next refresh without re-initializing the SPI bus.
        err = waveshare_epaper_display_on_refresh_display_off(s_epaper_handle, /*enter_deepsleep=*/false);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Display refresh failed: %s", esp_err_to_name(err));
            goto refresh_done;
        }

        err = waveshare_epaper_hardware_power_off_and_assert_reset(s_epaper_handle, POWER_OFF_DELAY_TICKS);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Power-off failed: %s", esp_err_to_name(err));
        }

        ESP_LOGI(TAG, "E-paper hardware refresh complete");

refresh_done:
        s_last_refresh_us = esp_timer_get_time();
        s_refresh_busy    = false;
    }
}


// ----------------------------------------------------------------
// Public API
// ----------------------------------------------------------------

esp_err_t lvgl_epaper_port_init(waveshare_epaper_handle_t epaper_handle,
                                uint8_t *epaper_buf,
                                size_t   epaper_buf_len) {
    ESP_RETURN_ON_FALSE(epaper_handle  != NULL, ESP_ERR_INVALID_ARG, TAG, "epaper_handle is NULL");
    ESP_RETURN_ON_FALSE(epaper_buf     != NULL, ESP_ERR_INVALID_ARG, TAG, "epaper_buf is NULL");
    ESP_RETURN_ON_FALSE(epaper_buf_len  > 0,    ESP_ERR_INVALID_ARG, TAG, "epaper_buf_len is 0");

    s_epaper_handle  = epaper_handle;
    s_epaper_buf     = epaper_buf;
    s_epaper_buf_len = epaper_buf_len;

    // Pre-fill e-paper buffer with white (0x55 = 01 01 01 01 = White White White White)
    memset(s_epaper_buf, 0x55, s_epaper_buf_len);

    // ------------------------------------------------------------------
    // Initialize LVGL
    // ------------------------------------------------------------------
    ESP_LOGI(TAG, "Initializing LVGL");
    lv_init();

    // ------------------------------------------------------------------
    // Create mutex for thread-safe LVGL access
    // ------------------------------------------------------------------
    s_lvgl_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lvgl_mutex != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create LVGL mutex");

    // ------------------------------------------------------------------
    // Create display and register flush callback
    // lv_display_create() automatically initializes the default theme.
    // Animated transitions are compiled out via LV_THEME_DEFAULT_TRANSITION_TIME=0
    // in lv_conf.h — e-paper displays cannot render mid-transition frames.
    // ------------------------------------------------------------------
    s_display = lv_display_create(EPD_WIDTH, EPD_HEIGHT);
    ESP_RETURN_ON_FALSE(s_display != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create LVGL display");

    lv_display_set_flush_cb(s_display, epaper_flush_cb);

    // Two partial draw buffers (each covers LVGL_BUF_LINES rows)
    lv_display_set_buffers(s_display,
                           s_lvgl_buf1, s_lvgl_buf2,
                           sizeof(s_lvgl_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Apply the mono theme: flat fills, no gradients, no shadows — optimal for
    // a 4-color e-paper display.
    lv_theme_t *mono_theme = lv_theme_mono_init(s_display, false, LV_FONT_DEFAULT);
    lv_display_set_theme(s_display, mono_theme);

    // ------------------------------------------------------------------
    // Start LVGL tick source (ESP timer, 1 ms period)
    // ------------------------------------------------------------------
    esp_timer_handle_t tick_timer;
    const esp_timer_create_args_t tick_timer_args = {
        .callback        = lvgl_tick_timer_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "lvgl_tick",
        .skip_unhandled_events = true,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_timer_args, &tick_timer), TAG, "Failed to create LVGL tick timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, 1000 /* µs */), TAG, "Failed to start LVGL tick timer");

    // ------------------------------------------------------------------
    // Start LVGL handler task
    // ------------------------------------------------------------------
    BaseType_t ret = xTaskCreate(lvgl_handler_task,
                                 "lvgl_handler",
                                 8192,
                                 NULL,
                                 5,
                                 NULL);
    ESP_RETURN_ON_FALSE(ret == pdPASS, ESP_ERR_NO_MEM, TAG, "Failed to create LVGL handler task");

    // ------------------------------------------------------------------
    // Start e-paper hardware refresh task
    // ------------------------------------------------------------------
    ret = xTaskCreate(epaper_refresh_task,
                      "epaper_refresh",
                      4096,
                      NULL,
                      4,
                      NULL);
    ESP_RETURN_ON_FALSE(ret == pdPASS, ESP_ERR_NO_MEM, TAG, "Failed to create e-paper refresh task");

    ESP_LOGI(TAG, "LVGL e-paper port initialized (%dx%d, min_refresh=%lu ms)",
             EPD_WIDTH, EPD_HEIGHT, EPAPER_LVGL_MIN_REFRESH_MS);

    return ESP_OK;
}

void lvgl_epaper_port_lock(uint32_t timeout_ms) {
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    xSemaphoreTake(s_lvgl_mutex, ticks);
}

void lvgl_epaper_port_unlock(void) {
    xSemaphoreGive(s_lvgl_mutex);
}
