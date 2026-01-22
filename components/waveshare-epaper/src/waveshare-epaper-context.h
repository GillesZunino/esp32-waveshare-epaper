// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#pragma once


#include "waveshare-epaper-spi.h"


/**
 * @brief Waveshare ePaper display context structure.
 * @note This structure is internal to the user and must only be accessed by the driver.
 */
typedef struct waveshare_epaper_context {
    waveshare_epaper_hw_config_t hw_config;
    spi_device_handle_t spi_device_handle;
    waveshare_epaper_spi_isr_context_t spi_isr_context;
    waveshare_epaper_gpio_isr_context_t gpio_isr_context;
} waveshare_epaper_context_t;