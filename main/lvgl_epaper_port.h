// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#pragma once

#include <stdint.h>
#include <stddef.h>

#include <esp_err.h>

#include "waveshare-epaper.h"


// Minimum time (milliseconds) between two consecutive hardware e-paper refreshes.
// E-paper displays degrade faster when refreshed too frequently.
#define EPAPER_LVGL_MIN_REFRESH_MS   120000UL    // 120 seconds

/**
 * @brief Initialize LVGL and bind it to the Waveshare e-paper display.
 *
 * Must be called AFTER:
 *   waveshare_epaper_driver_init()
 *   waveshare_epaper_hardware_power_on_and_deassert_reset()
 *   waveshare_epaper_configure_display()
 *
 * Allocates LVGL draw buffers, registers the flush callback, starts the LVGL
 * tick ESP timer, the LVGL handler FreeRTOS task, and the e-paper hardware
 * refresh FreeRTOS task.
 *
 * @param epaper_handle  Valid handle from waveshare_epaper_driver_init()
 * @param epaper_buf     DMA-capable buffer of exactly epaper_buf_len bytes.
 *                       Ownership is kept by the caller; must remain valid for
 *                       the lifetime of the port.
 * @param epaper_buf_len Size of epaper_buf in bytes (must be 11840 for the 2.15" display)
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t lvgl_epaper_port_init(waveshare_epaper_handle_t epaper_handle,
                                uint8_t *epaper_buf,
                                size_t   epaper_buf_len);

/**
 * @brief Acquire the LVGL mutex.
 *
 * Must be called before any lv_* API calls from user code running in a
 * different task than the LVGL handler task.
 *
 * @param timeout_ms Timeout in milliseconds. Use portMAX_DELAY (cast to uint32_t)
 *                   to wait indefinitely.
 */
void lvgl_epaper_port_lock(uint32_t timeout_ms);

/**
 * @brief Release the LVGL mutex. */
void lvgl_epaper_port_unlock(void);
