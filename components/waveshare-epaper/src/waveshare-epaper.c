// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#include <esp_check.h>

#include "waveshare-epaper.h"
#include "waveshare-epaper-context.h"
#include "waveshare-epaper-spi.h"

#include "waveshare-2in15-epaper-commands.h"


static const char* WaveshareEPaperLogTag = "wepd";


static inline esp_err_t enable_gpio_pins_private(const waveshare_epaper_hw_config_t* hw_config);
static inline esp_err_t disable_gpio_pins_private(const waveshare_epaper_hw_config_t* hw_config);
static esp_err_t configure_gpio_pins_private(const waveshare_epaper_hw_config_t* hw_config, bool enable);

static esp_err_t set_epaper_power_private(gpio_num_t power_pin, bool enable);


static esp_err_t waveshare_epaper_sleep_private(waveshare_epaper_handle_t handle);
static esp_err_t waveshare_epaper_power_on_off_private(waveshare_epaper_handle_t handle, bool on, bool enableEpd);


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

    ESP_RETURN_ON_ERROR(gpio_set_level(handle->hw_config.rst_io_num, 1), WaveshareEPaperLogTag, "Failed to set RESET pin high");



    esp_err_t err = gpio_set_level(handle->hw_config.rst_io_num, 1);
    vTaskDelay(pdMS_TO_TICKS(200 / portTICK_PERIOD_MS)); // TODO: 200 ms - Verify timings
    gpio_set_level(handle->hw_config.rst_io_num, 0);
    vTaskDelay(pdMS_TO_TICKS(2 / portTICK_PERIOD_MS));  // TODO 2ms - Verify timings
    gpio_set_level(handle->hw_config.rst_io_num, 1);
    vTaskDelay(pdMS_TO_TICKS(200 / portTICK_PERIOD_MS)); // TODO: 200 ms - Verify timings

    return err;
}


esp_err_t waveshare_epaper_display_sleep(waveshare_epaper_handle_t handle) {
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    return waveshare_epaper_sleep_private(handle);
}


// TODO: Provide default for value. Default is false
esp_err_t waveshare_epaper_display_on_off(waveshare_epaper_handle_t handle, bool on, bool enableEpd) {
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    return waveshare_epaper_power_on_off_private(handle, on, enableEpd);
}


esp_err_t waveshare_epaper_display_power_off_and_sleep(waveshare_epaper_handle_t handle) {
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(waveshare_epaper_power_on_off_private(handle, false, false), WaveshareEPaperLogTag, "Failed to power off ePaper display");
    ESP_RETURN_ON_ERROR(waveshare_epaper_sleep_private(handle), WaveshareEPaperLogTag, "Failed to put ePaper display to sleep");

    return ESP_OK;
}


static esp_err_t waveshare_epaper_sleep_private(waveshare_epaper_handle_t handle) {
    return waveshare_epaper_spi_send(handle, WAVESHARE_EPD_CMD_DEEP_SLEEP , (const uint8_t[]){0xA5}, 1);
}


static esp_err_t waveshare_epaper_power_on_off_private(waveshare_epaper_handle_t handle, bool on, bool enableEpd) {
    waveshare_epaper_command_t command = on ? WAVESHARE_EPD_CMD_POWER_ON : WAVESHARE_EPD_CMD_POWER_OFF;
    uint8_t data[1];
    data[0] = on ? 0x06 : (enableEpd ? 0x01 : 0x00);
    return waveshare_epaper_spi_send(handle, command, data, sizeof(data) / sizeof(data[0]));
}






typedef struct init_sequence_item {
    uint8_t command;
    uint8_t data[16];
    uint8_t data_length;
} init_sequence_item_t;


static DMA_ATTR init_sequence_item_t init_sequence[] = {
    {
        .command = 0x4D,
        .data = { 0x78 },
        .data_length = 1
    },
    {
        .command = 0x00,
        .data = { 0x0F, 0x29 },
        .data_length = 2
    },
    {
        .command = 0x01,
        .data = { 0x07, 0x00 },
        .data_length = 2
    },
    {
        .command = 0x03,
        .data = { 0x10, 0x54, 0x44 },
        .data_length = 3
    },
    {
        .command = 0x06,
        .data = { 0x05, 0x00, 0x3F, 0x0A, 0x25, 0x12, 0x1A },
        .data_length = 7
    },
    {
        .command = 0x50,
        .data = { 0x37 },
        .data_length = 1
    },
    {
        .command = 0x60,
        .data = { 0x02,0x02 },
        .data_length = 2
    },
    {
        .command = 0x61,
        .data = { 0x00, 0xA0, 0x01, 0x28 },
        .data_length = 4
    },
    {
        .command = 0xE7,
        .data = { 0x1C },
        .data_length = 1
    },
    {
        .command = 0xE3,
        .data = { 0x22 },
        .data_length = 1
    },
    {
        .command = 0xB4,
        .data = { 0xD0 },
        .data_length = 1
    },
    {
        .command = 0xB5,
        .data = { 0x03 },
        .data_length = 1
    },
    {
        .command = 0xE9,
        .data = { 0x01 },
        .data_length = 1
    },
    {
        .command = 0x30,
        .data = { 0x08 },
        .data_length = 1
    }
};

esp_err_t waveshare_epaper_configure_display(waveshare_epaper_handle_t handle) {
    for (uint16_t index = 0; index < sizeof(init_sequence) / sizeof(init_sequence_item_t); index++) {
        const init_sequence_item_t* item = &init_sequence[index];
        ESP_RETURN_ON_ERROR(waveshare_epaper_spi_send(handle, item->command, item->data, item->data_length), WaveshareEPaperLogTag, "Failed to send init sequence command (%d) 0x%02X", index, item->command);
    }

    return ESP_OK;
}


esp_err_t test_spi_performance(waveshare_epaper_handle_t handle) {
    // TODO: REMOVE. this is temporary to test the fastest way to send data over SPI with the logic analyser
    return waveshare_epaper_spi_send(handle, 0x4D, (uint8_t[]){0x78}, 1);
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
