// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#pragma once


/**
 * @brief States for the Command/Data line when operating in 4-wire SPI mode.
 */
typedef enum command_data_level {
    COMMAND_LEVEL = 0,
    DATA_LEVEL = 1
} command_data_level_t;




esp_err_t waveshare_epaper_spi_init_private(const waveshare_epaper_config_t* config, waveshare_epaper_context_t* pDisplay);


esp_err_t waveshare_epaper_spi_send(waveshare_epaper_handle_t handle, uint8_t command, const void* data, uint32_t data_len, bool wait_on_busy);
esp_err_t waveshare_epaper_spi_send_exclusive(waveshare_epaper_handle_t handle, uint8_t command, const void* data, uint32_t data_len, bool wait_on_busy);

esp_err_t waveshare_epaper_spi_send_with_response(waveshare_epaper_handle_t handle, uint8_t command, uint8_t* response_buffer, uint32_t response_buffer_len);