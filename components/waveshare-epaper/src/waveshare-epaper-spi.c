// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#include <esp_check.h>
#include <esp_log.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>

#include "waveshare-epaper-logtag.h"

#include "waveshare-epaper.h"
#include "waveshare-epaper-context.h"
#include "waveshare-epaper-spi.h"


// The display is available when BUSY is HIGH (1)
static const int DISPLAY_NOT_BUSY_LEVEL = 1;

static void pre_spi_transaction_isr_callback(spi_transaction_t* t);
static void post_spi_transaction_isr_callback(spi_transaction_t* t);
static void busy_gpio_isr_handler(void* arg);


esp_err_t waveshare_epaper_spi_init_private(const waveshare_epaper_config_t* config, waveshare_epaper_handle_t handle) {
    // Make sure BUSY interrupts are disabled - Add BUSY line GPIO ISR handler
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(gpio_intr_disable(config->hw_config.busy_io_num), cleanup, WaveshareEPaperLogTag, "Failed to disable BUSY GPIO interrupt");
    ESP_GOTO_ON_ERROR(gpio_isr_handler_add(config->hw_config.busy_io_num, busy_gpio_isr_handler, handle), cleanup, WaveshareEPaperLogTag, "Failed to add BUSY GPIO ISR handler");

    // Pre-initialize all SPI transactions we need in our device handle
    handle->spi_isr_context.cmd_transaction = (spi_transaction_t) {
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL,
        .cmd = 0,
        .addr = 0,
        .length = sizeof(uint8_t) * 8,
        .rxlength = 0,
        .override_freq_hz = 0,
        .user = (void*) handle,
        .rx_buffer = NULL
    };
    handle->spi_isr_context.data_transaction = (spi_transaction_t) {
        .flags = 0,
        .cmd = 0,
        .addr = 0,
        .length = 0,
        .rxlength = 0,
        .override_freq_hz = 0,
        .user = (void*) handle,
        .tx_buffer = NULL,
        .rx_buffer = NULL
    };

    // Add the target Waveshare ePaper device as an SPI device on the given bus
    spi_device_interface_config_t spiDeviceInterfaceConfig = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,

        //
        // Waveshare ePaper displays use Clock Polarity (CPOL) 0 and Clock Phase (CPHA) 0
        //
        .mode = 0,

        .clock_source = config->spi_cfg.clock_source,

        .clock_speed_hz = config->spi_cfg.clock_speed_hz,
        .input_delay_ns = config->spi_cfg.input_delay_ns,
        .sample_point = config->spi_cfg.sample_point,

        .spics_io_num = config->spi_cfg.spics_io_num,

        //
        // Waveshare ePaper displays use 3‑wire SPI (SDIO‑style) for data transfer
        // When using SPI_DEVICE_HALFDUPLEX, ESP32(X) devices only support DMA for TX only or RX only transactions
        //
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_3WIRE,
        .queue_size = 1,

        //
        // ISR callbacks used to set Command/Data line in 4-Wire SPI mode
        //
        .pre_cb = pre_spi_transaction_isr_callback,
        .post_cb = post_spi_transaction_isr_callback
    };

    ESP_GOTO_ON_ERROR(spi_bus_add_device(config->spi_cfg.host_id, &spiDeviceInterfaceConfig, &handle->spi_device_handle), cleanup, WaveshareEPaperLogTag, "Failed to add SPI device");
    return ESP_OK;

cleanup:
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_isr_handler_remove(config->hw_config.busy_io_num));
    return ret;
}

esp_err_t waveshare_epaper_spi_free_private(waveshare_epaper_handle_t handle) {
    // Track the first error we encounter so we can return it to the caller - We do try to detach all aspects of the driver regardless of which step failed
    esp_err_t firstError = ESP_OK;

    // Disable interrupts on BUSY line
    esp_err_t err = gpio_intr_disable(handle->hw_config.busy_io_num);
    if (err != ESP_OK) {
        firstError = firstError == ESP_OK ? err : firstError;
        ESP_LOGW(WaveshareEPaperLogTag, "Failed to disable BUSY GPIO interrupt (%d)", err);
    }

    // Remove ISR handler for BUSY line
    err = gpio_isr_handler_remove(handle->hw_config.busy_io_num);
    if (err != ESP_OK) {
        firstError = firstError == ESP_OK ? err : firstError;
        ESP_LOGW(WaveshareEPaperLogTag, "Failed to remove BUSY GPIO ISR handler (%d)", err);
    }

    // Remove SPI device from bus
    err = spi_bus_remove_device(handle->spi_device_handle);
    if (err != ESP_OK) {
        firstError = firstError == ESP_OK ? err : firstError;
        ESP_LOGW(WaveshareEPaperLogTag, "Failed to spi_bus_remove_device() -> (%d)", err);
    }

    return firstError;
}

esp_err_t waveshare_epaper_spi_send_private(waveshare_epaper_handle_t handle, uint8_t command, const uint8_t* data, size_t data_len, bool spi_bus_exclusive, bool wait_for_busy, TickType_t timeout_ticks) {
    // Disallow zero timeout when asked to wait for busy - A zero timeout would cause the function to return immediately without waiting at all
    ESP_RETURN_ON_FALSE(!wait_for_busy || (timeout_ticks > 0), ESP_ERR_INVALID_ARG, WaveshareEPaperLogTag, "timeout_ticks must be greater than zero");
    
    // Acquire the SPI bus exclusively if requested
    if (spi_bus_exclusive) {
        ESP_RETURN_ON_ERROR(spi_device_acquire_bus(handle->spi_device_handle, portMAX_DELAY), WaveshareEPaperLogTag, "Failed to acquire SPI bus");
    }

        // Enable interrupts on BUSY line if we need to wait for it later
        esp_err_t ret = ESP_OK;
        if (wait_for_busy) {
            // Clear the semaphore in case it was already signalled - Enable BUSY GPIO interrupt
            xSemaphoreTake(handle->gpio_isr_context.busy_semaphore_handle, 0);
            ESP_GOTO_ON_ERROR(gpio_intr_enable(handle->hw_config.busy_io_num), cleanup, WaveshareEPaperLogTag, "Failed to enable BUSY GPIO interrupt");
        }

        // Configure and send SPI transaction for Command
        handle->spi_isr_context.level = COMMAND_LEVEL;
        handle->spi_isr_context.cmd_transaction.tx_data[0] = command;
        ESP_GOTO_ON_ERROR(spi_device_polling_transmit(handle->spi_device_handle, &handle->spi_isr_context.cmd_transaction), cleanup, WaveshareEPaperLogTag, "Failed to send command over SPI");

        // Configure and send SPI transaction for data - DMA in SPI_DEVICE_HALFDUPLEX mode is allowed since we only set SPI_TRANS_USE_TXDATA
        if ((data != NULL) && (data_len > 0)) {
            handle->spi_isr_context.level = DATA_LEVEL;

            bool useTxData = data_len <= 4;
            handle->spi_isr_context.data_transaction.rxlength = 0;
            handle->spi_isr_context.data_transaction.rx_buffer = NULL;
            handle->spi_isr_context.data_transaction.flags = (useTxData ? SPI_TRANS_USE_TXDATA : 0) | SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL;
            handle->spi_isr_context.data_transaction.length = data_len * 8;
            if (useTxData) {
                memcpy(handle->spi_isr_context.data_transaction.tx_data, data, data_len);
            } else {
                handle->spi_isr_context.data_transaction.tx_buffer = data;
            }
            ESP_GOTO_ON_ERROR(spi_device_polling_transmit(handle->spi_device_handle, &handle->spi_isr_context.data_transaction), cleanup, WaveshareEPaperLogTag, "Failed to send data over SPI");
        }

        // Wait for the BUSY signal to transition to HIGH (from LOW)
        if (wait_for_busy) {
            // If the BUSY line is low right now, wait for it to go high
            if (gpio_get_level(handle->hw_config.busy_io_num) != DISPLAY_NOT_BUSY_LEVEL) {
                if (xSemaphoreTake(handle->gpio_isr_context.busy_semaphore_handle, timeout_ticks) == pdFALSE) {
            // Post-timeout re-check: the ISR and the FreeRTOS timeout expiry can race at the last tick
            // Re-reading the GPIO level here collapses that race window and avoids a false-negative timeout
                    if (gpio_get_level(handle->hw_config.busy_io_num) != DISPLAY_NOT_BUSY_LEVEL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
                        ESP_LOGE(WaveshareEPaperLogTag, "Timeout waiting for BUSY to go HIGH");
#endif
                        ret = ESP_ERR_TIMEOUT;
                    }
                }
            }
        }

cleanup:
    // Disable BUSY GPIO interrupt and drain any pending signal
    esp_err_t disable_err = gpio_intr_disable(handle->hw_config.busy_io_num);
    if (disable_err != ESP_OK) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
            ESP_LOGE(WaveshareEPaperLogTag, "Failed to disable BUSY GPIO interrupt: %s", esp_err_to_name(disable_err));
#endif
        if (ret == ESP_OK) {
            ret = disable_err;
        }
    }

    // Drain any stale semaphore signal left from a previous operation
    xSemaphoreTake(handle->gpio_isr_context.busy_semaphore_handle, 0);

    // Release the bus if we were asked to acquire it exclusively
    if (spi_bus_exclusive) {
        spi_device_release_bus(handle->spi_device_handle);
    }

    return ret;
}

esp_err_t waveshare_epaper_spi_send_and_receive_private(waveshare_epaper_handle_t handle, uint8_t command, uint8_t* response_buffer, uint32_t response_buffer_len) {
    // Acquire the SPI bus exclusively
    ESP_RETURN_ON_ERROR(spi_device_acquire_bus(handle->spi_device_handle, portMAX_DELAY), WaveshareEPaperLogTag, "Failed to acquire SPI bus");
    
        //
        // Commands with data read back are performed in 3-wire SPI mode (MISO == MOSI) or SPI_DEVICE_HALFDUPLEX
        //
        //   * MOSI | <Command> | N/A    | N/A    | N/A    | N/A | N/A    |
        //   * MOSI | N/A       | <Data> | <Data> | <Data> | ... | <Data> |
        //
        // In SPI_DEVICE_HALFDUPLEX mode, it is not possible to have both a TX and RX phase in the same transaction
        // We also need to toggle the C/D line to COMMAND_LEVEL for the command phase and DATA_LEVEL for the data phase
        //
        // We send two separate transactions with one phase each TX or RX. This allows DMA to be used for both transactions
        // In half duplex, DMA is only supported for transactions with a TX phase or an RX phase but not both at the same time
        //

// TODO: Wait for BUSY ?
        
        // Configure and send SPI transaction for Command
        esp_err_t ret = ESP_OK;
        handle->spi_isr_context.level = COMMAND_LEVEL;
        handle->spi_isr_context.cmd_transaction.tx_data[0] = command;
        ESP_GOTO_ON_ERROR(spi_device_polling_transmit(handle->spi_device_handle, &handle->spi_isr_context.cmd_transaction), cleanup, WaveshareEPaperLogTag, "Failed to send command over SPI");


        // Read back data from Waveshare ePaper device
        handle->spi_isr_context.level = DATA_LEVEL;

        bool useRxData = response_buffer_len <= 4;
        handle->spi_isr_context.data_transaction.length = 0;
        handle->spi_isr_context.data_transaction.tx_buffer = NULL;
        handle->spi_isr_context.data_transaction.flags = (useRxData ? SPI_TRANS_USE_RXDATA : 0) | SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL;
        handle->spi_isr_context.data_transaction.rxlength = response_buffer_len * 8;
        handle->spi_isr_context.data_transaction.rx_buffer = useRxData ? NULL : response_buffer;
        ESP_GOTO_ON_ERROR(spi_device_polling_transmit(handle->spi_device_handle, &handle->spi_isr_context.data_transaction), cleanup, WaveshareEPaperLogTag, "Failed to read data over SPI");
        
// TODO: Await for BUSY?
        if (ret == ESP_OK) {
            if (useRxData) {
                memcpy(response_buffer, handle->spi_isr_context.data_transaction.rx_data, response_buffer_len);
            }
        }

cleanup:
    spi_device_release_bus(handle->spi_device_handle);
    return ret;
}

esp_err_t waveshare_epaper_wait_for_display_ready_private(waveshare_epaper_handle_t handle, TickType_t timeout_ticks) {
    // Disallow zero timeout
    ESP_RETURN_ON_FALSE(timeout_ticks > 0, ESP_ERR_INVALID_ARG, WaveshareEPaperLogTag, "timeout_ticks must be greater than zero");

    // If BUSY is not set, nothing to do
    esp_err_t ret = ESP_OK;
    if (gpio_get_level(handle->hw_config.busy_io_num) == DISPLAY_NOT_BUSY_LEVEL) {
        goto done;
    }

    // Drain any stale semaphore signal left from a previous operation
    xSemaphoreTake(handle->gpio_isr_context.busy_semaphore_handle, 0);

    // Enable the BUSY rising-edge interrupt
    ESP_GOTO_ON_ERROR(gpio_intr_enable(handle->hw_config.busy_io_num), done, WaveshareEPaperLogTag, "Failed to enable BUSY GPIO interrupt");

    // Re-check: BUSY may have gone HIGH between the fast-path
    if (gpio_get_level(handle->hw_config.busy_io_num) != DISPLAY_NOT_BUSY_LEVEL) {
        if (xSemaphoreTake(handle->gpio_isr_context.busy_semaphore_handle, timeout_ticks) == pdFALSE) {
            // Post-timeout re-check: the ISR and the FreeRTOS timeout expiry can race at the last tick
            // Re-reading the GPIO level here collapses that race window and avoids a false-negative timeout
            if (gpio_get_level(handle->hw_config.busy_io_num) != DISPLAY_NOT_BUSY_LEVEL) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
                ESP_LOGE(WaveshareEPaperLogTag, "Timeout waiting for BUSY to go HIGH");
#endif
                ret = ESP_ERR_TIMEOUT;
            }
        }
    }

    // Disable BUSY interrupt and drain any spurious signal that arrived during cleanup
    esp_err_t disable_err = gpio_intr_disable(handle->hw_config.busy_io_num);
    if (disable_err != ESP_OK) {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_LOGE(WaveshareEPaperLogTag, "Failed to disable BUSY GPIO interrupt: %s", esp_err_to_name(disable_err));
#endif
        if (ret == ESP_OK) {
            ret = disable_err;
        }
    }

done:
    // Drain any stale semaphore signal left from a previous operation
    xSemaphoreTake(handle->gpio_isr_context.busy_semaphore_handle, 0);
    return ret;
}


// -----------------------------------------------------------------------------------------------------------------------------------
// SPI transactions ISR callbacks
//
static IRAM_ATTR void pre_spi_transaction_isr_callback(spi_transaction_t* t) {
    waveshare_epaper_handle_t handle = (waveshare_epaper_handle_t)t->user;
    esp_err_t err __attribute__((unused)) = gpio_set_level(handle->hw_config.data_cmd_io_num, handle->spi_isr_context.level);
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
    if (err != ESP_OK) {
        ESP_EARLY_LOGE(WaveshareEPaperLogTag, "Failed to set DC line pre-SPI transaction");
    }
#endif
}

static IRAM_ATTR void post_spi_transaction_isr_callback(spi_transaction_t* t) {
    waveshare_epaper_handle_t handle = (waveshare_epaper_handle_t)t->user;
    esp_err_t err __attribute__((unused)) = gpio_set_level(handle->hw_config.data_cmd_io_num, COMMAND_LEVEL);
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
    if (err != ESP_OK) {
        ESP_EARLY_LOGE(WaveshareEPaperLogTag, "Failed to reset DC line post-SPI transaction");
    }
#endif
}
// -----------------------------------------------------------------------------------------------------------------------------------


// -----------------------------------------------------------------------------------------------------------------------------------
// GPIO ISR handler for BUSY pin
//
static IRAM_ATTR void busy_gpio_isr_handler(void* arg) {
    waveshare_epaper_handle_t handle = (waveshare_epaper_handle_t) arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t semaphoreGiveOutcome = xSemaphoreGiveFromISR(handle->gpio_isr_context.busy_semaphore_handle, &xHigherPriorityTaskWoken);
    if (semaphoreGiveOutcome == pdTRUE) {
        // Request a FreeRTOS context switch if giving the semaphore unblocked a higher priority task (FreeRTOS cannot switch tasks inside of an ISR)
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    } else {
#if CONFIG_WAVESHARE_EPAPER_ENABLE_DEBUG_LOG
        ESP_EARLY_LOGE(WaveshareEPaperLogTag, "Failed to give BUSY semaphore from ISR");
#endif
    }
}
// -----------------------------------------------------------------------------------------------------------------------------------