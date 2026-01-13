// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#pragma once

esp_err_t waveshare_epaper_spi_init_private(const waveshare_epaper_config_t* config, waveshare_epaper_context_t* pDisplay);

esp_err_t waveshare_epaper_spi_send(waveshare_epaper_handle_t handle, uint8_t command, const void* data, uint32_t data_len);
