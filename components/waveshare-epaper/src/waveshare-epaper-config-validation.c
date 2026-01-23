// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------


#include <esp_check.h>

#include "waveshare-epaper-logtag.h"
#include "waveshare-epaper-config-validation.h"


esp_err_t validate_waveshare_epaper_configuration_private(const waveshare_epaper_config_t* config) {
    // We use GPIO interrupts to monitor the BUSY line - SPI1 host cannot be used with interrupts
    if (config->spi_cfg.host_id == SPI1_HOST) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "Choose a different SPI host than SPI1. Waveshare ePaper driver uses GPIO interrupts to monitor the display's BUSY line which SPI1 does not support");
#endif
        return ESP_ERR_INVALID_ARG;
    }

    // Basic SPI Host configuration validation
    if (config->spi_cfg.spics_io_num == GPIO_NUM_NC) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "SPI CS pin must be configured (not GPIO_NUM_NC)");
#endif
        return ESP_ERR_INVALID_ARG;
    }

    // Basic Waveshare GPIO configuration validation
    if (config->hw_config.busy_io_num == GPIO_NUM_NC) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "BUSY pin must be configured (not GPIO_NUM_NC)");
#endif
        return ESP_ERR_INVALID_ARG;
    }

    if (config->hw_config.rst_io_num == GPIO_NUM_NC) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "RESET pin must be configured (not GPIO_NUM_NC)");
#endif
        return ESP_ERR_INVALID_ARG;
    }

    // TODO: Add more validation as needed

    return ESP_OK;
}