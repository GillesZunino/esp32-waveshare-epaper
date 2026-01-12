// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#pragma once


typedef struct waveshare_epaper_context {
    waveshare_epaper_hw_config_t hw_config;
    spi_device_handle_t spi_device_handle;
} waveshare_epaper_context_t;