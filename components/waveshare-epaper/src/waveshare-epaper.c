// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#include <esp_check.h>

#include "waveshare-epaper.h"
#include "waveshare-epaper-context.h"
#include "waveshare-epaper-spi.h"


static const char* WaveshareEPaperLogTag = "wepd";


static inline esp_err_t enable_gpio_pins_private(const waveshare_epaper_hw_config_t* hw_config);
static inline esp_err_t disable_gpio_pins_private(const waveshare_epaper_hw_config_t* hw_config);
static esp_err_t configure_gpio_pins_private(const waveshare_epaper_hw_config_t* hw_config, bool enable);

static esp_err_t set_epaper_power_private(gpio_num_t power_pin, bool enable);


static void free_driver_memory_private(waveshare_epaper_handle_t handle);


esp_err_t waveshare_epaper_driver_init(const waveshare_epaper_config_t* config, waveshare_epaper_handle_t* handle) {
    if (handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_ARG;
    }
    
    // Always clear return values even if we later fail
    *handle = NULL;

    // TODO: Check configuration
    //ESP_RETURN_ON_ERROR(check_driver_configuration_private(config), WaveshareEPaperLogTag, "Invalid configuration");

    // Allocate space for our handle
    waveshare_epaper_context_t* pDisplay = heap_caps_calloc(1, sizeof(waveshare_epaper_context_t), MALLOC_CAP_DEFAULT);
    if (pDisplay == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Configure GPIO pins to communicate with the Waveshare ePaper display
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(enable_gpio_pins_private(&config->hw_config), cleanup, WaveshareEPaperLogTag, "Failed to configure GPIO pins");

    // Turn on the power on the ePaper display - Some models are equipped with a power control pin to physically turn power on or off
    ESP_GOTO_ON_ERROR(set_epaper_power_private(config->hw_config.pwr_io_num, true), cleanup, WaveshareEPaperLogTag, "Failed to turn on ePaper power");

    
    // Allocate space for the command buffer - Only allocate dynamic memory if the command buffer cannot fit in spi_transaction_t.tx_data which is a uint8_t[4]
    // pLedMax7219->commands.use_inline_buffer = config->hw_config.chain_length * sizeof(max7219_command_t) <= sizeof(uint8_t[4]);
    // if (!pLedMax7219->commands.use_inline_buffer) {
    //     pLedMax7219->commands.commands_buffer = heap_caps_calloc(config->hw_config.chain_length, sizeof(max7219_command_t), MALLOC_CAP_DMA);
    //     ESP_GOTO_ON_FALSE(pLedMax7219->commands.commands_buffer != NULL, ESP_ERR_NO_MEM, cleanup, WaveshareEPaperLogTag, "Could not allocate memory for command buffer");
    // }

    // Add an SPI device on the given bus - We accept the SPI bus configuration as is
    ESP_GOTO_ON_ERROR(waveshare_epaper_spi_init_private(config, pDisplay), cleanup, WaveshareEPaperLogTag, "Failed to configure SPI Master");

    pDisplay->hw_config = config->hw_config;
    *handle = pDisplay;

    return ret;

cleanup:
// TODO: Shutdown GPIO
    free_driver_memory_private(pDisplay);
    return ret;
}

esp_err_t waveshare_epaper_driver_free(waveshare_epaper_handle_t handle) {
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    // Track the first error we encounter so we can return it to the caller - We do try to detach all aspects of the driver regardless of which step failed
    esp_err_t firstError = ESP_OK;

    // Put all MAX7219 / MAX7221 cascaded on the chain in shutdown mode before freeing the driver
    // esp_err_t err = led_driver_max7219_set_chain_mode(handle, MAX7219_SHUTDOWN_MODE);
    // if (err != ESP_OK) {
    //     firstError = firstError == ESP_OK ? err : firstError;
    //     ESP_LOGW(WaveshareEPaperLogTag, "Failed to set MAX7219/MAX7221 in shutdown mode (%d)", err);
    // }

    // Remove the device from the bus
    esp_err_t err = spi_bus_remove_device(handle->spi_device_handle);
    if (err != ESP_OK) {
        firstError = firstError == ESP_OK ? err : firstError;
        ESP_LOGW(WaveshareEPaperLogTag, "Failed to spi_bus_remove_device() -> (%d)", err);
    }

    // SHutdown GPIO pins
    err = disable_gpio_pins_private(&handle->hw_config);
    // TODO: Handle error right - This is vibe coded++
    if (err != ESP_OK) {
         firstError = firstError == ESP_OK ? err : firstError;
        ESP_LOGW(WaveshareEPaperLogTag, "Failed to disable GPIO pins (%d)", err);
    }

    // Release memory
    free_driver_memory_private(handle);    
    return firstError;
}

esp_err_t set_epaper_power(waveshare_epaper_handle_t handle, bool on) {
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    if (handle->hw_config.pwr_io_num == GPIO_NUM_NC) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "POWER pin is not configured (GPIO_NUM_NC)");
#endif
        return ESP_ERR_INVALID_ARG;
    }

    return set_epaper_power_private(handle->hw_config.pwr_io_num, on);
}


esp_err_t reset_epaper_hardware(waveshare_epaper_handle_t handle) {
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = gpio_set_level(handle->hw_config.rst_io_num, 1);
    vTaskDelay(pdMS_TO_TICKS(200 / portTICK_PERIOD_MS)); // TODO: 200 ms - Verify timings
    gpio_set_level(handle->hw_config.rst_io_num, 0);
    vTaskDelay(pdMS_TO_TICKS(2 / portTICK_PERIOD_MS));  // TODO 2ms - Verify timings
    gpio_set_level(handle->hw_config.rst_io_num, 1);
    vTaskDelay(pdMS_TO_TICKS(200 / portTICK_PERIOD_MS)); // TODO: 200 ms - Verify timings
    return err;
}


esp_err_t configure_the_thing(waveshare_epaper_handle_t handle) {
    return waveshare_epaper_spi_send(handle, 0x4D , (const uint8_t[]){0x78}, 1);
}





static inline esp_err_t enable_gpio_pins_private(const waveshare_epaper_hw_config_t* hw_config) {
    return configure_gpio_pins_private(hw_config, true);
}

static inline esp_err_t disable_gpio_pins_private(const waveshare_epaper_hw_config_t* hw_config) {
    return configure_gpio_pins_private(hw_config, false);
}

static esp_err_t configure_gpio_pins_private(const waveshare_epaper_hw_config_t* hw_config, bool enable) {
    esp_err_t ret = ESP_OK;

    // Configure POWER pin as output - The pin level is initially set to LOW
    if (hw_config->pwr_io_num != GPIO_NUM_NC) {
        ESP_GOTO_ON_ERROR(gpio_set_direction(hw_config->pwr_io_num, enable ? GPIO_MODE_OUTPUT : GPIO_MODE_DISABLE), cleanup, WaveshareEPaperLogTag, "Failed to set direction for POWER pin");
        ESP_GOTO_ON_ERROR(gpio_set_level(hw_config->pwr_io_num, 0), cleanup, WaveshareEPaperLogTag, "Failed to set level (0) for POWER pin");
    }

    // Conf:gure BUSY pin as input
    // TODO: Enable interupts
    ESP_GOTO_ON_ERROR(gpio_set_direction(hw_config->busy_io_num, enable ? GPIO_MODE_INPUT : GPIO_MODE_DISABLE), cleanup, WaveshareEPaperLogTag, "Failed to set direction for BUSY pin");
    
    // Configure RESET pin - The pin level is initially set to HIGH
    ESP_GOTO_ON_ERROR(gpio_set_direction(hw_config->rst_io_num, enable ? GPIO_MODE_OUTPUT : GPIO_MODE_DISABLE), cleanup, WaveshareEPaperLogTag, "Failed to set direction for RESET pin");
    ESP_GOTO_ON_ERROR(gpio_set_level(hw_config->rst_io_num, 1), cleanup, WaveshareEPaperLogTag, "Failed to set level (1) for RESET pin");
    
    // Configure Data/Command pin - The pin level is initially set to LOW
    if (hw_config->data_cmd_io_num != GPIO_NUM_NC) {
        ESP_GOTO_ON_ERROR(gpio_set_direction(hw_config->data_cmd_io_num, enable ? GPIO_MODE_OUTPUT : GPIO_MODE_DISABLE), cleanup, WaveshareEPaperLogTag, "Failed to set direction for Data/Command pin");
        ESP_GOTO_ON_ERROR(gpio_set_level(hw_config->data_cmd_io_num, 0), cleanup, WaveshareEPaperLogTag, "Failed to set level (0) for Data/Command pin");
    }

    return ret;

cleanup:
// TODO: COllect and Log Errors during cleanup
    if (hw_config->pwr_io_num != GPIO_NUM_NC) {
        gpio_set_direction(hw_config->pwr_io_num, GPIO_MODE_DISABLE);
    }

    gpio_set_direction(hw_config->busy_io_num, GPIO_MODE_DISABLE);

    gpio_set_direction(hw_config->rst_io_num, GPIO_MODE_DISABLE);

    if (hw_config->pwr_io_num != GPIO_NUM_NC) {
        gpio_set_direction(hw_config->pwr_io_num, GPIO_MODE_DISABLE);
    }

    return ret;
}





static esp_err_t set_epaper_power_private(gpio_num_t power_pin, bool enable) {
    return power_pin != GPIO_NUM_NC ? gpio_set_level(power_pin, enable ? 1 : 0) : ESP_OK;
}


static void free_driver_memory_private(waveshare_epaper_handle_t handle) {
    if (handle != NULL) {

        // if (!handle->commands.use_inline_buffer && (handle->commands.commands_buffer != NULL)) {
        //     heap_caps_free(handle->commands.commands_buffer);
        //     handle->commands.commands_buffer = NULL;
        // }
        
        heap_caps_free(handle);
    }
} 
