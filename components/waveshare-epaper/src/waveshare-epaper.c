// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#include <esp_check.h>


#include "waveshare-epaper-logtag.h"

#include "waveshare-epaper.h"
#include "waveshare-epaper-context.h"
#include "waveshare-epaper-spi.h"

#include "waveshare-2in15-epaper-commands.h"

#include "waveshare-epaper-config-validation.h"



// TODO: Reorder functions adequately



static inline esp_err_t waveshare_epaper_set_hardware_power_private(gpio_num_t power_pin, waveshare_epaper_hardware_power_state_t power);
static inline esp_err_t waveshare_epaper_set_hardware_reset_private(gpio_num_t rst_pin, waveshare_epaper_hardware_reset_state_t reset);




static inline esp_err_t enable_gpio_pins_private(const waveshare_epaper_config_t* config);
static inline esp_err_t disable_gpio_pins_private(const waveshare_epaper_config_t* config);
static esp_err_t configure_gpio_pins_private(const waveshare_epaper_config_t* config, bool enable);



static esp_err_t waveshare_epaper_sleep_private(waveshare_epaper_handle_t handle);
static esp_err_t waveshare_epaper_power_on_off_private(waveshare_epaper_handle_t handle, bool on, bool enableEpd);


static void free_driver_memory_private(waveshare_epaper_handle_t handle);


esp_err_t waveshare_epaper_driver_init(const waveshare_epaper_config_t* config, waveshare_epaper_handle_t* handle) {
    if (config == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "config must not be NULL");
#endif
        return ESP_ERR_INVALID_ARG;
    }
    if (handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_ARG;
    }
    
    // Always clear return values even if we later fail
    *handle = NULL;

    // Validate configuration
    ESP_RETURN_ON_ERROR(validate_waveshare_epaper_configuration_private(config), WaveshareEPaperLogTag, "Invalid configuration");

    // Allocate space for our handle
    waveshare_epaper_context_t* pDisplay = heap_caps_calloc(1, sizeof(waveshare_epaper_context_t), MALLOC_CAP_DMA /*MALLOC_CAP_DEFAULT*/);
    if (pDisplay == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Allocate semaphore for BUSY pin interrupt handling
    pDisplay->gpio_isr_context.busy_semaphore_handle = xSemaphoreCreateBinaryStatic(&pDisplay->gpio_isr_context.busy_semaphore);

    // Configure GPIO pins to communicate with the Waveshare ePaper display
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(enable_gpio_pins_private(config), cleanup, WaveshareEPaperLogTag, "Failed to configure GPIO pins");

    // Add an SPI device on the given bus - We accept the SPI bus configuration as is
    ESP_GOTO_ON_ERROR(waveshare_epaper_spi_init_private(config, pDisplay), cleanup, WaveshareEPaperLogTag, "Failed to configure SPI Master");

    pDisplay->hw_config = config->hw_config;
    *handle = pDisplay;

    return ret;

cleanup:
    // No additional memory to release - Semaphore was allocated statically inside the context structure
    pDisplay->gpio_isr_context.busy_semaphore_handle = NULL;

// TODO: Shutdown GPIO
    free_driver_memory_private(pDisplay);
    return ret;
}

esp_err_t waveshare_epaper_driver_free(waveshare_epaper_handle_t handle) {
    if (handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must have been initialized with waveshare_epaper_driver_init()");
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

    // Remove the device from the bus, cleanup interrupt handlers ...
    esp_err_t err = waveshare_epaper_spi_free_private(handle);
    if (err != ESP_OK) {
        firstError = firstError == ESP_OK ? err : firstError;
        ESP_LOGW(WaveshareEPaperLogTag, "Failed to cleanup SPI bus -> (%d)", err);
    }

    // Shutdown GPIO pins - TODO: re-enable this
    // err = disable_gpio_pins_private(&handle->hw_config);
    // TODO: Handle error right - This is vibe coded++
    if (err != ESP_OK) {
         firstError = firstError == ESP_OK ? err : firstError;
        ESP_LOGW(WaveshareEPaperLogTag, "Failed to disable GPIO pins (%d)", err);
    }

    // Release memory
    free_driver_memory_private(handle);    
    return firstError;
}




esp_err_t waveshare_epaper_hardware_power_on_and_deassert_reset(waveshare_epaper_handle_t handle, const TickType_t power_on_wait_time) {
    if (handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must have been initialized with waveshare_epaper_driver_init()");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    if (power_on_wait_time == portMAX_DELAY) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "power_on_wait_time set to portMAX_DELAY would cause an infinite block");
#endif
        return ESP_ERR_INVALID_ARG;
    }

    // Turn the ePaper display power on - Some models are equipped with a power control pin to physically turn power on or off
    if (handle->hw_config.pwr_io_num != GPIO_NUM_NC) {
        ESP_RETURN_ON_ERROR(waveshare_epaper_set_hardware_power_private(handle->hw_config.pwr_io_num, WAVESHARE_EPAPER_HARDWARE_POWER_ON), WaveshareEPaperLogTag, "Failed to turn ePaper power on");
        vTaskDelay(power_on_wait_time);
    }

    // Set the hardware GPIO RESET pin to HIGH (not in reset) to enable the display
    esp_err_t err = waveshare_epaper_set_hardware_reset_private(handle->hw_config.rst_io_num, WAVESHARE_EPAPER_HARDWARE_NORMAL);
    
    
    // TODO: Externalize this - Consider making INTR based
    // Wait for the device to be ready - The BUSY GPIO pin is HIGH when the device is ready
    while (!gpio_get_level(handle->hw_config.busy_io_num)) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    return err;
}


esp_err_t waveshare_epaper_hardware_power_off_and_assert_reset(waveshare_epaper_handle_t handle, const TickType_t power_off_wait_time) {
    if (handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must have been initialized with waveshare_epaper_driver_init()");
#endif
        return ESP_ERR_INVALID_STATE;
    }
    if (power_off_wait_time == portMAX_DELAY) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "power_off_wait_time set to portMAX_DELAY would cause an infinite block");
#endif
        return ESP_ERR_INVALID_ARG;
    }

    // Turn the ePaper display power off - Some models are equipped with a power control pin to physically turn power on or off
    if (handle->hw_config.pwr_io_num != GPIO_NUM_NC) {
        ESP_RETURN_ON_ERROR(waveshare_epaper_set_hardware_power_private(handle->hw_config.pwr_io_num, WAVESHARE_EPAPER_HARDWARE_POWER_OFF), WaveshareEPaperLogTag, "Failed to turn off ePaper power");
        vTaskDelay(power_off_wait_time);
    }

    // Set the hardware GPIO RESET pin to LOW (in reset) to disable the display - The display is potentially powered off so it might not respond to it
    ESP_RETURN_ON_ERROR(waveshare_epaper_set_hardware_reset_private(handle->hw_config.rst_io_num, WAVESHARE_EPAPER_HARDWARE_RESET), WaveshareEPaperLogTag, "Failed to assert RESET pin to put display in reset");
    return ESP_OK;
}


esp_err_t waveshare_epaper_set_hardware_power(waveshare_epaper_handle_t handle, waveshare_epaper_hardware_power_state_t power) {
    if (handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must have been initialized with waveshare_epaper_driver_init()");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_OK;
    if (handle->hw_config.pwr_io_num != GPIO_NUM_NC) {
        err = waveshare_epaper_set_hardware_power_private(handle->hw_config.pwr_io_num, power);
    }

    return err;
}


esp_err_t waveshare_epaper_set_hardware_reset(waveshare_epaper_handle_t handle, waveshare_epaper_hardware_reset_state_t reset) {
    if (handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must have been initialized with waveshare_epaper_driver_init()");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    return waveshare_epaper_set_hardware_reset_private(handle->hw_config.rst_io_num, reset);
}

esp_err_t waveshare_epaper_display_on_refresh_display_off(waveshare_epaper_handle_t handle, bool enter_deepsleep) {
    if (handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must have been initialized with waveshare_epaper_driver_init()");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    // TODO: Do we need to wait here ?
    while (!gpio_get_level(handle->hw_config.busy_io_num)) { // Data sheet asks to loop when Busy = LOW and proceed when Busy = HIGH
        vTaskDelay(pdMS_TO_TICKS(1));
    }

// TODO: Do we need to wait for BUSY to proceed here ?


#define DISCRETE_COMMANDS 1
#if DISCRETE_COMMANDS

    esp_err_t err = waveshare_epaper_display_on_off(handle, true, false);
    // Full display refresh
    err = waveshare_epaper_display_refresh(handle);

    // Software power off and put the display to sleep
    err = waveshare_epaper_display_power_off_and_sleep(handle);

    // // Turn hardware power to display off - Put the RESET Line low (RESET)
    // err = waveshare_epaper_hardware_power_off_and_assert_reset(handle, pdMS_TO_TICKS(50));

    return ESP_OK;

#else

    // Trigger AUTO sequence 0x17 (PON -> DRF -> POF -> DSLP) or 0xA5 (PON -> DRF -> POF) depending on whether we want to enter deep sleep or not
    // TODO: Do we need to wait for BUSY on deep sleep request as well ?

// TODO: When deep sleep is desired, we need to send the command separately as the sequence does not fire busy

    return waveshare_epaper_spi_send_private(handle, WAVESHARE_EPD_CMD_AUTO_SEQUENCE, (const uint8_t[]){ enter_deepsleep ? 0xA7 : 0xA5 }, 1, true, true);

#endif

    // TODO: Attach to display read
    // TODO: Should be done after a data transfer
    // bool data_stop = false;
    // ESP_ERROR_CHECK(waveshare_epaper_read_data_stop(waveshare_epaper_handle, &data_stop));



// esp_err_t reset_epaper_hardware(waveshare_epaper_handle_t handle) {
//     if (handle->spi_device_handle == NULL) {
// #if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
//         ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
// #endif
//         return ESP_ERR_INVALID_STATE;
//     }

//     // ESP_RETURN_ON_ERROR(gpio_set_level(handle->hw_config.rst_io_num, 1), WaveshareEPaperLogTag, "Failed to set RESET pin high");

//     esp_err_t err = waveshare_epaper_set_hardware_reset_private(handle->hw_config.rst_io_num, WAVESHARE_EPAPER_HARDWARE_NORMAL);
//     vTaskDelay(pdMS_TO_TICKS(200)); // TODO: 200 ms - Verify timings

//     err = waveshare_epaper_set_hardware_reset_private(handle->hw_config.rst_io_num, WAVESHARE_EPAPER_HARDWARE_RESET);
//     vTaskDelay(pdMS_TO_TICKS(2));  // TODO 2ms - Verify timings

//     err = waveshare_epaper_set_hardware_reset_private(handle->hw_config.rst_io_num, WAVESHARE_EPAPER_HARDWARE_NORMAL);
//     vTaskDelay(pdMS_TO_TICKS(200)); // TODO: 200 ms - Verify timings

//     return err;
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

    esp_err_t err = waveshare_epaper_power_on_off_private(handle, on, enableEpd);

    // Wait for High
    while (!gpio_get_level(handle->hw_config.busy_io_num)) { // Data sheet asks to loop when Busy = LOW and proceed when Busy = HIGH
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    return err;
}


esp_err_t waveshare_epaper_display_power_off_and_sleep(waveshare_epaper_handle_t handle) {
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    while (!gpio_get_level(handle->hw_config.busy_io_num)) { // Data sheet asks to loop when Busy = LOW and proceed when Busy = HIGH
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_RETURN_ON_ERROR(waveshare_epaper_power_on_off_private(handle, false, false), WaveshareEPaperLogTag, "Failed to power off ePaper display");
    // Wait for High
    while (!gpio_get_level(handle->hw_config.busy_io_num)) { // Data sheet asks to loop when Busy = LOW and proceed when Busy = HIGH
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    //ESP_RETURN_ON_ERROR(waveshare_epaper_sleep_private(handle), WaveshareEPaperLogTag, "Failed to put ePaper display to sleep");

    ESP_RETURN_ON_ERROR(waveshare_epaper_spi_send_private(handle, WAVESHARE_EPD_CMD_DEEP_SLEEP , (const uint8_t[]){ 0xA5 }, 1, true, false), WaveshareEPaperLogTag, "Failed to put ePaper display to sleep");
    vTaskDelay(pdMS_TO_TICKS(200));

    return ESP_OK;
}


static esp_err_t waveshare_epaper_sleep_private(waveshare_epaper_handle_t handle) {
    return waveshare_epaper_spi_send_private(handle, WAVESHARE_EPD_CMD_DEEP_SLEEP , (const uint8_t[]){ 0xA5 }, 1, true, true);
}


static esp_err_t waveshare_epaper_power_on_off_private(waveshare_epaper_handle_t handle, bool on, bool enableEpd) {
    waveshare_epaper_command_t command = on ? WAVESHARE_EPD_CMD_POWER_ON : WAVESHARE_EPD_CMD_POWER_OFF;
    uint8_t data[1];
    data[0] = on ? 0x06 : (enableEpd ? 0x01 : 0x00);
    return waveshare_epaper_spi_send_private(handle, command, data, sizeof(data) / sizeof(data[0]), true, true);
}






typedef struct init_sequence_item {
    uint8_t command;
    uint8_t data[8];
    uint8_t data_length;
} init_sequence_item_t;


static DMA_ATTR init_sequence_item_t init_sequence[] = {
    {
        // Dithering Start (Sierra Lite)
        .command = 0x4D,
        .data = { 0x78 },
        .data_length = 1
    },
    {
        // Panel Setting (PSR)
        .command = 0x00,
        .data = { 0x0F, 0x29 },
        .data_length = 2
    },
    {
        // Power Settings (PWR)
        .command = 0x01,
        .data = { 0x07, 0x00 },
        .data_length = 2
    },
    {
        // Power Off Sequence Settings (PFS)
        .command = 0x03,
        .data = { 0x10, 0x54, 0x44 },
        .data_length = 3
    },

    // TODO: Different from GitHub example
    // EPD_2IN15G_SendCommand(0x06);
    // EPD_2IN15G_SendData(0x0F);	
    // EPD_2IN15G_SendData(0x0A);	
    // EPD_2IN15G_SendData(0x2F);   	
    // EPD_2IN15G_SendData(0x25);   	
    // EPD_2IN15G_SendData(0x22);      	
    // EPD_2IN15G_SendData(0x2E);   	
    // EPD_2IN15G_SendData(0x21);  
    {
        // Booster Soft Start (BTST)
        .command = 0x06,
        .data = { 0x05, 0x00, 0x3F, 0x0A, 0x25, 0x12, 0x1A },
        .data_length = 7
    },

    // TODO: Missing
    // Temperature Sensor Calibration (TSE)
    // EPD_2IN15G_SendCommand(0x41);
    // EPD_2IN15G_SendData(0x00);	

    {
        // VCOM and Data Interval Setting (VDI)
        .command = 0x50,
        .data = { 0x37 },
        .data_length = 1
    },
    {
        // TCON Settings
        .command = 0x60,
        .data = { 0x02, 0x02 },
        .data_length = 2
    },

    //TODO: Symbolic version of the commands
    // EPD_2IN15G_SendCommand(0x61);
    // EPD_2IN15G_SendData(EPD_2IN15G_WIDTH/256);		
    // EPD_2IN15G_SendData(EPD_2IN15G_WIDTH%256);		
    // EPD_2IN15G_SendData(EPD_2IN15G_HEIGHT/256);		
    // EPD_2IN15G_SendData(EPD_2IN15G_HEIGHT%256); 	
    {
        // Resolution Settings (TRES)
        .command = 0x61,
        .data = { 0x00, 0xA0, 0x01, 0x28 },
        .data_length = 4
    },


    // TODO: Missing
    // Gate /Source Start Setting (GSST)
    // EPD_2IN15G_SendCommand(0x65);
    // EPD_2IN15G_SendData(0x00);
    // EPD_2IN15G_SendData(0x00);
    // EPD_2IN15G_SendData(0x00);
    // EPD_2IN15G_SendData(0x00);

    // TODO: Missing
    // EPD_2IN15G_SendCommand(0xE0);
    // EPD_2IN15G_SendData(0x00);

    {
        // TSBDRY - Temperature boundary phase C2
        .command = 0xE7,
        .data = { 0x1C },
        .data_length = 1
    },
    {
        // Power Savings (PWS)
        .command = 0xE3,
        .data = { 0x22 },
        .data_length = 1
    },
    {
        // ?
        .command = 0xB4,
        .data = { 0xD0 },
        .data_length = 1
    },
    {
        // ?
        .command = 0xB5,
        .data = { 0x03 },
        .data_length = 1
    },
    {
        // AUTO Sequence
        .command = 0xE9,
        .data = { 0x01 },
        .data_length = 1
    },

    // TODO: Different - PLL. They disable dynamic frame rate and set refresh to 50Hz
    // the code below enable dynamic frame rate and set refresh rate to 12.5Hz
    // EPD_2IN15G_SendCommand(0x30);
    // EPD_2IN15G_SendData(0x02);    
    {
        // PLL Control (PLL)
        .command = 0x30,
        .data = { 0x08 },
        .data_length = 1
    }

    // TODO: Missing
    //EPD_2IN15G_SendCommand(0x04);
};

esp_err_t waveshare_epaper_configure_display(waveshare_epaper_handle_t handle) {
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    // // Take the device off RESET
    // esp_err_t err = gpio_set_level(handle->hw_config.rst_io_num, 1);
    // vTaskDelay(pdMS_TO_TICKS(200)); // TODO: 200 ms - Verify timings

    
    esp_err_t err = spi_device_acquire_bus(handle->spi_device_handle, portMAX_DELAY);

        for (uint16_t index = 0; index < sizeof(init_sequence) / sizeof(init_sequence_item_t); index++) {
            const init_sequence_item_t* item = &init_sequence[index];
            bool isLast = (index == (sizeof(init_sequence) / sizeof(init_sequence_item_t)) - 1);
            err = waveshare_epaper_spi_send_private(handle, item->command, item->data, item->data_length, false, isLast);
            if (err != ESP_OK){
                break;
            }
        }

    spi_device_release_bus(handle->spi_device_handle);

    return err;
}


esp_err_t waveshare_epaper_send_data_buffer(waveshare_epaper_handle_t handle, const uint8_t* buffer, size_t buffer_length) {
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = waveshare_epaper_spi_send_private(handle, WAVESHARE_EPD_CMD_DATA_START_TRANSMISSION, buffer, buffer_length, true, true);

    // TODO: EPD_2IN15G_ReadBusyH()
    return err;
}

esp_err_t waveshare_epaper_display_refresh(waveshare_epaper_handle_t handle) {
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    // Wait for display to be ready
    while (!gpio_get_level(handle->hw_config.busy_io_num)) { // Data sheet asks to loop when Busy = LOW and proceed when Busy = HIGH
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // Refresh display - We choose VCOM follows LUTC (0x00)
    esp_err_t err = waveshare_epaper_spi_send_private(handle, WAVESHARE_EPD_CMD_DISPLAY_REFRESH, (const uint8_t[]){ 0x00 }, 1, true, true);

    // while (!gpio_get_level(handle->hw_config.busy_io_num)) { // Data sheet asks to loop when Busy = LOW and proceed when Busy = HIGH
    //     vTaskDelay(pdMS_TO_TICKS(1));
    // }

    return err;
}



esp_err_t test_spi_performance(waveshare_epaper_handle_t handle) {
    // TODO: REMOVE. this is temporary to test the fastest way to send data over SPI with the logic analyzer
    return waveshare_epaper_spi_send_private(handle, 0x4D, (const uint8_t[]){0x78}, 1, true, true);
}





esp_err_t waveshare_epaper_read_data_stop(waveshare_epaper_handle_t handle, bool* data_stop) {
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    if (data_stop == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "data_stop must not be NULL");
#endif
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer = 0;
    ESP_RETURN_ON_ERROR(waveshare_epaper_spi_send_and_receive_private(handle, WAVESHARE_EPD_CMD_DATA_STOP, &buffer, 1), WaveshareEPaperLogTag, "Failed to read Data Stop (DSP)");
    *data_stop = (buffer & 0x80) != 0;
    return ESP_OK;
}

esp_err_t waveshare_epaper_read_temperature(waveshare_epaper_handle_t handle, const waveshare_epaper_temperature_sensor_t sensor, uint16_t* temperature) {
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    if ((sensor != WAVESHARE_EPAPER_TEMPERATURE_INTERNAL_SENSOR) && (sensor != WAVESHARE_EPAPER_TEMPERATURE_EXTERNAL_SENSOR)) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "Invalid temperature sensor");
#endif
        return ESP_ERR_INVALID_ARG;
    }

    if (temperature == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "temperature must not be NULL");
#endif
        return ESP_ERR_INVALID_ARG;
    }

// TODO: Internal is default. External requires changing calibration to the external sensor not implemented right now
    uint8_t buffer[2] = {0};
    ESP_RETURN_ON_ERROR(waveshare_epaper_spi_send_and_receive_private(handle, WAVESHARE_EPD_CMD_TEMPERATURE_SENSOR_COMMAND, buffer, sizeof(buffer) / sizeof(buffer[0])), WaveshareEPaperLogTag, "Failed to read Temperature (TSC)");
    // TODO: There is an enum for this
    // TODO: Pass other bits
    *temperature = buffer[0];
    return ESP_OK;
}

esp_err_t waveshare_epaper_read_low_power_state(waveshare_epaper_handle_t handle, bool* low_power_state) {
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    if (low_power_state == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "low_power_state must not be NULL");
#endif
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer = 0;
    ESP_RETURN_ON_ERROR(waveshare_epaper_spi_send_and_receive_private(handle, WAVESHARE_EPD_CMD_LOW_POWER_DETECTION, &buffer, 1), WaveshareEPaperLogTag, "Failed to read Low Power State (LPD)");
    // TODO: Enum. Low Power = 0. Normal = 1
    *low_power_state = !((buffer & 0x01) != 0);
    return ESP_OK;
}

esp_err_t waveshare_epaper_read_revision(waveshare_epaper_handle_t handle, uint32_t* revision) {
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    if (revision == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "revision must not be NULL");
#endif
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer[3] = {0};
    ESP_RETURN_ON_ERROR(waveshare_epaper_spi_send_and_receive_private(handle, WAVESHARE_EPD_CMD_REVISION, buffer, sizeof(buffer) / sizeof(buffer[0])), WaveshareEPaperLogTag, "Failed to read Revision (REV)");
    // TODO: Confirm format
    *revision = (buffer[0] * 1000) + (buffer[1] * 100) + buffer[2];
    return ESP_OK;
}

esp_err_t waveshare_epaper_read_vcom(waveshare_epaper_handle_t handle, uint8_t* vcom) {
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    if (vcom == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "vcom must not be NULL");
#endif
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer = 0;
    ESP_RETURN_ON_ERROR(waveshare_epaper_spi_send_and_receive_private(handle, WAVESHARE_EPD_CMD_VCOM_VALUE, &buffer, 1), WaveshareEPaperLogTag, "Failed to read VCOM Voltage (VV)");
    // TODO: Create enum for this
    *vcom = buffer;
    return ESP_OK;
}

esp_err_t waveshare_epaper_read_revision2(waveshare_epaper_handle_t handle, uint8_t* revision2) {
    if (handle->spi_device_handle == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "handle must not be NULL");
#endif
        return ESP_ERR_INVALID_STATE;
    }

    if (revision2 == NULL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "revision2 must not be NULL");
#endif
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer = 0;
    ESP_RETURN_ON_ERROR(waveshare_epaper_spi_send_and_receive_private(handle, WAVESHARE_EPD_CMD_REVISION_2, &buffer, 1), WaveshareEPaperLogTag, "Failed to read Revision (REV2)");
    *revision2 = buffer;
    return ESP_OK;
}







static inline esp_err_t enable_gpio_pins_private(const waveshare_epaper_config_t* config) {
    return configure_gpio_pins_private(config, true);
}

static inline esp_err_t disable_gpio_pins_private(const waveshare_epaper_config_t* config) {
    return configure_gpio_pins_private(config, false);
}

static esp_err_t configure_gpio_pins_private(const waveshare_epaper_config_t* config, bool enable) {
    esp_err_t ret = ESP_OK;

    // Install ISR service if not already done - ESP_ERR_INVALID_STATE means "service already installed"
    ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    ESP_RETURN_ON_FALSE((ret == ESP_OK) || (ret == ESP_ERR_INVALID_STATE), ESP_OK, WaveshareEPaperLogTag, "GPIO ISR service installation failed");

    // Configure CS pin - The pin level is initially set to HIGH to deselect the device
    gpio_config_t cs_io_conf = {
        .pin_bit_mask = BIT64(config->spi_cfg.spics_io_num),
        .mode = enable ? GPIO_MODE_OUTPUT : GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_GOTO_ON_ERROR(gpio_config(&cs_io_conf), cleanup, WaveshareEPaperLogTag, "Failed to configure GPIO for CS pin");
    ESP_GOTO_ON_ERROR(gpio_set_level(config->spi_cfg.spics_io_num, 1), cleanup, WaveshareEPaperLogTag, "Failed to set level (1) for CS pin");

    // Configure POWER pin as output - The pin level is initially set to LOW (POWER OFF)
    if (config->hw_config.pwr_io_num != GPIO_NUM_NC) {
        gpio_config_t pwr_io_conf = {
            .pin_bit_mask = BIT64(config->hw_config.pwr_io_num),
            .mode = enable ? GPIO_MODE_OUTPUT : GPIO_MODE_DISABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_GOTO_ON_ERROR(gpio_config(&pwr_io_conf), cleanup, WaveshareEPaperLogTag, "Failed to configure GPIO for POWER pin");
        ESP_GOTO_ON_ERROR(waveshare_epaper_set_hardware_power_private(config->hw_config.pwr_io_num, WAVESHARE_EPAPER_HARDWARE_POWER_OFF), cleanup, WaveshareEPaperLogTag, "Failed to set level (0) for POWER pin");
    }

    // Configure BUSY pin as input with interrupt on LOW -> HIGH
    gpio_config_t busy_io_conf = {
        .pin_bit_mask = BIT64(config->hw_config.busy_io_num),
        .mode = enable ? GPIO_MODE_INPUT : GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE
    };
    ESP_GOTO_ON_ERROR(gpio_config(&busy_io_conf), cleanup, WaveshareEPaperLogTag, "Failed to configure GPIO for BUSY pin");
    ESP_GOTO_ON_ERROR(gpio_intr_disable(config->hw_config.busy_io_num), cleanup, WaveshareEPaperLogTag, "Failed to disable interrupts on BUSY pin");
    
    // Configure RESET pin - The pin level is initially set to LOW to hold the device in RESET
    gpio_config_t rst_io_conf = {
        .pin_bit_mask = BIT64(config->hw_config.rst_io_num),
        .mode = enable ? GPIO_MODE_OUTPUT : GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_GOTO_ON_ERROR(gpio_config(&rst_io_conf), cleanup, WaveshareEPaperLogTag, "Failed to configure GPIO for RESET pin");
    ESP_GOTO_ON_ERROR(waveshare_epaper_set_hardware_reset_private(config->hw_config.rst_io_num, WAVESHARE_EPAPER_HARDWARE_RESET), cleanup, WaveshareEPaperLogTag, "Failed to set level (1) for RESET pin");
    
    // Configure Data/Command pin - The pin level is initially set to LOW
    if (config->hw_config.data_cmd_io_num != GPIO_NUM_NC) {
        gpio_config_t data_cmd_io_conf = {
            .pin_bit_mask = BIT64(config->hw_config.data_cmd_io_num),
            .mode = enable ? GPIO_MODE_OUTPUT : GPIO_MODE_DISABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_GOTO_ON_ERROR(gpio_config(&data_cmd_io_conf), cleanup, WaveshareEPaperLogTag, "Failed to configure GPIO for Data/Command pin");
        ESP_GOTO_ON_ERROR(gpio_set_level(config->hw_config.data_cmd_io_num, COMMAND_LEVEL), cleanup, WaveshareEPaperLogTag, "Failed to set level (0) for Data/Command pin");
    }

    return ret;

cleanup:
// TODO: Collect and Log Errors during cleanup

    gpio_reset_pin(config->spi_cfg.spics_io_num);

    if (config->hw_config.pwr_io_num != GPIO_NUM_NC) {
        gpio_reset_pin(config->hw_config.pwr_io_num);
    }

    gpio_reset_pin(config->hw_config.busy_io_num);
    gpio_reset_pin(config->hw_config.rst_io_num);

    if (config->hw_config.data_cmd_io_num != GPIO_NUM_NC) {
        gpio_reset_pin(config->hw_config.data_cmd_io_num);
    }

    return ret;
}

static inline esp_err_t waveshare_epaper_set_hardware_power_private(gpio_num_t power_pin, waveshare_epaper_hardware_power_state_t power) {
    return gpio_set_level(power_pin, power == WAVESHARE_EPAPER_HARDWARE_POWER_ON ? 1 : 0);
}

static inline esp_err_t waveshare_epaper_set_hardware_reset_private(gpio_num_t rst_pin, waveshare_epaper_hardware_reset_state_t reset) {
    return gpio_set_level(rst_pin, reset == WAVESHARE_EPAPER_HARDWARE_NORMAL ? 1 : 0);
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
