// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#pragma once


#include <esp_err.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>


#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Handle to a Waveshare ePaper display context.
 */
struct waveshare_epaper_context;
typedef struct waveshare_epaper_context* waveshare_epaper_handle_t; ///< Handle to a Waveshare ePaper display context

/**
 * @brief Configuration of the SPI bus for MAX7219 / MAX7221 device.
 */
typedef struct waveshare_epaper_spi_config {
    spi_host_device_t host_id;          ///< SPI bus ID. Which buses are available depends on the specific device
    spi_clock_source_t clock_source;    ///< Select SPI clock source, `SPI_CLK_SRC_DEFAULT` by default
    int clock_speed_hz;                 ///< SPI clock speed in Hz. Derived from `clock_source`
    int input_delay_ns;                 ///< Maximum data valid time of slave. The time required between SCLK and MISO
    spi_sampling_point_t sample_point;  ///< SPI input data sampling point
    int spics_io_num;                   ///< CS GPIO pin for this device, or `GPIO_NUM_NC` (-1) if not used
} waveshare_epaper_spi_config_t;

/**
 * @brief Waveshare ePaper display hardware configuration.
 */
typedef struct waveshare_epaper_hw_config {
    gpio_num_t pwr_io_num;       ///< Power control GPIO pin or `GPIO_NUM_NC` (-1) if not used
    gpio_num_t busy_io_num;      ///< Busy signal GPIO pin
    gpio_num_t rst_io_num;       ///< Reset GPIO pin
    gpio_num_t data_cmd_io_num;  ///< Data/Command GPIO pin (4-Wire SPI mode) or `GPIO_NUM_NC` (-1) to use 3-Wire SPI mode
} waveshare_epaper_hw_config_t;

/**
 * @brief Configuration of Waveshare ePaper display device.
 */
typedef struct waveshare_epaper_config {
    waveshare_epaper_spi_config_t spi_cfg;       ///< SPI configuration for Waveshare ePaper display
    waveshare_epaper_hw_config_t hw_config;      ///< Waveshare ePaper display hardware configuration
} waveshare_epaper_config_t;



/**
 * @brief Initialize the Waveshare ePaper display driver.
 * 
 * @param[in]  config Pointer to a configuration structure for the Waveshare ePaper display driver
 * @param[out] handle Pointer to a memory location which receives the handle to the Waveshare ePaper display driver
 * 
 * @return
 *      - ESP_OK: Successfully installed driver
 *      - ESP_ERR_INVALID_ARG: Arguments are invalid, e.g. invalid clock source, ...
 *      - ESP_ERR_NO_MEM: Insufficient memory
 */
esp_err_t waveshare_epaper_driver_init(const waveshare_epaper_config_t* config, waveshare_epaper_handle_t* handle);

/**
 * @brief Free the Waveshare ePaper display driver.
 * 
 * @param[in] handle Handle to the Waveshare ePaper display driver
 * 
 * @return
 *      - ESP_OK: Successfully uninstalled the driver
 *      - ESP_ERR_INVALID_STATE: Driver is not installed or in an invalid state
 */
esp_err_t waveshare_epaper_driver_free(waveshare_epaper_handle_t handle);




/**
 * @brief Turn the Waveshare ePaper display power on or off via hardware GPIO,.
 * 
 * @param[in] handle Handle to the Waveshare ePaper display driver
 * @param[in] on Boolean value to turn the display on (`true`) or off (`false`)
 * @param[in] waitTime Time to wait after changing the power state before returning, in RTOS ticks
 * 
 * @note This requires the display to be equipped with a GPIO Power on/off line and the driver to be configured accordingly.
 *       Set the `pwr_io_num` field in `waveshare_epaper_hw_config_t` to the appropriate GPIO pin.
 * 
 * @return
 *      - ESP_OK: Successfully set the power state
 *      - ESP_ERR_INVALID_STATE: Driver is not installed or in an invalid state
 *      - ESP_ERR_INVALID_ARG: Invalid argument, e.g. POWER pin set to `GPIO_NUM_NC`
 */
esp_err_t waveshare_epaper_hardware_power_on_off(waveshare_epaper_handle_t handle, bool on, TickType_t waitTime);


esp_err_t reset_epaper_hardware(waveshare_epaper_handle_t handle);


esp_err_t waveshare_epaper_display_sleep(waveshare_epaper_handle_t handle);

esp_err_t waveshare_epaper_display_on_off(waveshare_epaper_handle_t handle, bool on, bool enableEpd);

esp_err_t waveshare_epaper_display_power_off_and_sleep(waveshare_epaper_handle_t handle);


esp_err_t waveshare_epaper_configure_display(waveshare_epaper_handle_t handle);

esp_err_t waveshare_epaper_display_buffer(waveshare_epaper_handle_t handle, const uint8_t* buffer, size_t buffer_length);




esp_err_t waveshare_epaper_display_refresh(waveshare_epaper_handle_t handle);


esp_err_t test_spi_performance(waveshare_epaper_handle_t handle);


esp_err_t waveshare_epaper_read_data_stop(waveshare_epaper_handle_t handle, bool* data_stop);
esp_err_t waveshare_epaper_read_temperature(waveshare_epaper_handle_t handle, bool internal, uint16_t* temperature);
// TODO: Not sure about R43 - Investigate
esp_err_t waveshare_epaper_read_low_power_state(waveshare_epaper_handle_t handle, bool* low_power_state);
esp_err_t waveshare_epaper_read_revision(waveshare_epaper_handle_t handle, uint32_t* revision);
esp_err_t waveshare_epaper_read_vcom(waveshare_epaper_handle_t handle, uint8_t* vcom);
// TODO: R92
esp_err_t waveshare_epaper_read_revision2(waveshare_epaper_handle_t handle, uint8_t* revision2);
// TODO: R9F



#ifdef __cplusplus
}
#endif