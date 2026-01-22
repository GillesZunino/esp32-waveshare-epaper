// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#pragma once


/**
 * @brief States for the Command/Data line when operating in 4-wire SPI mode.
 */
typedef enum command_data_line_level {
    COMMAND_LEVEL = 0,
    DATA_LEVEL = 1
} command_data_line_level_t;


/**
 * @brief Context passed to SPI ISR callbacks to set the Command/Data line level.
 */
typedef struct waveshare_epaper_spi_isr_context {
    spi_transaction_t cmd_transaction;
    spi_transaction_t data_transaction;
    command_data_line_level_t level;
} waveshare_epaper_spi_isr_context_t;


/**
 * @brief Context for GPIO ISR handling of the BUSY pin.
 */
typedef struct waveshare_epaper_gpio_isr_context {
    StaticSemaphore_t busy_semaphore;
    SemaphoreHandle_t busy_semaphore_handle;
} waveshare_epaper_gpio_isr_context_t;



/**
 * @brief Initialize the SPI device and BUSY GPIO interrupt for a Waveshare ePaper panel.
 *
 * Sets the BUSY pin ISR so the driver can block on display readiness and attaches the ePaper
 * device to the configured SPI host with the correct half-duplex/3-wire options and ISR
 * callbacks that drive the Data/Command line in 4-wire mode.
 *
 * This is a driver-only utility; user applications must not call it directly.
 *
 * @param[in] config Pointer to the driver configuration containing SPI and GPIO settings. The
 *                   SPI bus must already be initialized, and the BUSY, CS, and D/C pins must
 *                   be configured.
 * @param[in,out] handle Driver handle; receives the SPI device handle and uses the BUSY ISR
 *                       context. The context must already contain the hardware config from
 *                       `config`.
 *
 * @return
 *      - ESP_OK on success
 *      - Error code from `gpio_intr_disable`, `gpio_isr_handler_add`, or `spi_bus_add_device`
 */
esp_err_t waveshare_epaper_spi_init_private(const waveshare_epaper_config_t* config, waveshare_epaper_handle_t handle);


/**
 * @brief Deinitialize the SPI device and BUSY GPIO interrupt for a Waveshare ePaper panel.
 *
 * Removes the SPI device from the bus, disables the BUSY pin ISR, and releases associated resources.
 * Reverses the setup performed by `waveshare_epaper_spi_init_private`.
 *
 * This is a driver-only utility; user applications must not call it directly.
 *
 * @param[in] handle Driver handle for the target display.
 *
 * @return
 *      - ESP_OK on success
 *      - Error code from `spi_bus_remove_device` or `gpio_isr_handler_remove`
 */
esp_err_t waveshare_epaper_spi_free_private(waveshare_epaper_handle_t handle);


/**
 * @brief Send a command with optional payload over SPI to the device.
 *
 * Manages Command/Data line levels, optional SPI bus exclusive acquisition, and optional BUSY pin waits.
 * 
 * This is a driver-only utility; user applications must not call it directly.
 *
 * @param[in] handle Driver handle for the target display.
 * @param[in] command Command byte to send.
 * @param[in] data Optional data buffer following the command (may be NULL when data_len is 0).
 * @param[in] data_len Length of the data buffer in bytes.
 * @param[in] spi_bus_exclusive Acquire/release the SPI bus within this call when true; set false
 *                              if the caller already holds the bus.
 * @param[in] wait_for_busy Wait for BUSY to clear after transmission when true.
 *
 * @return ESP_OK on success, or an error code from SPI/GPIO operations.
 */
esp_err_t waveshare_epaper_spi_send_private(waveshare_epaper_handle_t handle, uint8_t command, const uint8_t* data, size_t data_len, bool spi_bus_exclusive, bool wait_for_busy);


/**
 * @brief Send a command over SPI to the device and read back a response.
 *
 * Performs a command phase then a read phase in half-duplex mode while toggling the Command/Data
 * line appropriately.
 * 
 * This is a driver-only utility; user applications must not call it directly.
 *
 * @param[in] handle Driver handle for the target display.
 * @param[in] command Command byte to issue before reading.
 * @param[out] response_buffer Buffer to store the response.
 * @param[in] response_buffer_len Number of response bytes expected.
 *
 * @return ESP_OK on success, or an error code from SPI operations.
 */
esp_err_t waveshare_epaper_spi_send_and_receive_private(waveshare_epaper_handle_t handle, uint8_t command, uint8_t* response_buffer, uint32_t response_buffer_len);