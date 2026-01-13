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
    int spics_io_num;                   ///< CS GPIO pin for this device, or `GPIO_NUM_NC` (-1) if not used
    int queue_size;                     ///< SPI transaction queue size. See 'spi_device_queue_trans()'
} waveshare_epaper_spi_config_t;

/**
 * @brief Waveshare ePaper display hardware configuration.
 */
typedef struct waveshare_epaper_hw_config {
    gpio_num_t pwr_io_num;       ///< Power control GPIO pin or `GPIO_NUM_NC` (-1) if not used
    gpio_num_t busy_io_num;      ///< Busy signal GPIO pin
    gpio_num_t rst_io_num;       ///< Reset GPIO pin
    gpio_num_t data_cmd_io_num;  ///< Data/Command select GPIO pin or `GPIO_NUM_NC` (-1) if not used
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
 * @brief Turn the power of the Waveshare ePaper display on or off.
 * 
 * @param[in] handle Handle to the Waveshare ePaper display driver
 * @param[in] on Boolean value to turn the display on (`true`) or off (`false`)
 * 
 * @note Uses the POWER GPIO pin configured during driver initialization. Does nothing if the pin was set to `GPIO_NUM_NC`.
 * 
 * @return
 *      - ESP_OK: Successfully set the power state
 *      - ESP_ERR_INVALID_STATE: Driver is not installed or in an invalid state
 *      - ESP_ERR_INVALID_ARG: Invalid argument, e.g. POWER pin set to `GPIO_NUM_NC`
 */
esp_err_t set_epaper_power(waveshare_epaper_handle_t handle, bool on);

esp_err_t reset_epaper_hardware(waveshare_epaper_handle_t handle);


esp_err_t waveshare_epaper_display_sleep(waveshare_epaper_handle_t handle);

esp_err_t waveshare_epaper_display_on_off(waveshare_epaper_handle_t handle, bool on, bool enableEpd);




esp_err_t configure_the_thing(waveshare_epaper_handle_t handle);





#ifdef __cplusplus
}
#endif