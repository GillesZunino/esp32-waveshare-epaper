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
 * @brief Configuration of the SPI bus for Waveshare ePaper displays.
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
 * @brief Hardware power states for Waveshare ePaper displays.
 */
typedef enum {
    WAVESHARE_EPAPER_HARDWARE_POWER_OFF = 0,
    WAVESHARE_EPAPER_HARDWARE_POWER_ON = 1,
} waveshare_epaper_hardware_power_state_t;

/**
 * @brief Hardware reset states for Waveshare ePaper displays.
 */
typedef enum {
    WAVESHARE_EPAPER_HARDWARE_RESET = 0,
    WAVESHARE_EPAPER_HARDWARE_NORMAL = 1,
} waveshare_epaper_hardware_reset_state_t;



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
 * @brief Hardware power on the e-paper display and deassert the RESET pin to HIGH (no reset).
 *
 * Hardware enable power to the e-paper display and releases it from its reset state,
 * allowing it to become operational. Wait for the specified duration before de-asserting the RESET line to
 * ensure the display has sufficient time to power up and stabilize.
 *
 * @param handle The handle to the e-paper display device.
 * @param power_on_wait_time The duration in FreeRTOS ticks to wait after powering on the display.
 * 
 * @attention This function toggles hardware GPIO pins. Separately, ePaper displays offers a 'power on/off'
 * software command which controls the charge pump, source and gate drivers, VCOM etc.
 * 
 * @note Some ePaper displays do not offer a programmable power on/off (hw_config.pwr_io_num set to GPIO_NUM_NC).
 * In this case, this function will only toggle the reset line to reset the display without controlling power.
 * 
 * @return ESP_OK if the operation was successful.
 * @return An error code if the operation failed (e.g., invalid handle, communication error).
 */
esp_err_t waveshare_epaper_hardware_power_on_and_deassert_reset(waveshare_epaper_handle_t handle, const TickType_t power_on_wait_time);

/**
 * @brief Hardware power off the e-paper display and assert the RESET pin to LOW (reset).
 *
 * Hardware disables power to the e-paper display and forces it into its reset state,
 * preventing it from operating. Wait for the specified duration before asserting the RESET line to
 * ensure the display has sufficient time to power down and stabilize.
 *
 * @param handle The handle to the e-paper display device.
 * @param power_off_wait_time The duration in FreeRTOS ticks to wait after powering off the display.
 * 
 * @attention This function toggles hardware GPIO pins. Separately, ePaper displays offers a 'power on/off'
 * software command which controls the charge pump, source and gate drivers, VCOM etc.
 * 
 * @note Some ePaper displays do not offer a programmable power on/off (hw_config.pwr_io_num set to GPIO_NUM_NC).
 * In this case, this function will only toggle the reset line to reset the display without controlling power.
 * 
 * @return ESP_OK if the operation was successful.
 * @return An error code if the operation failed (e.g., invalid handle, communication error).
 */
esp_err_t waveshare_epaper_hardware_power_off_and_assert_reset(waveshare_epaper_handle_t handle, const TickType_t power_off_wait_time);


/**
 * @brief Turn the Waveshare ePaper display power on or off via hardware GPIO.
 * 
 * @param[in] handle Handle to the Waveshare ePaper display driver
 * @param[in] power Hardware power state to set (`WAVESHARE_EPAPER_HARDWARE_POWER_ON` or `WAVESHARE_EPAPER_HARDWARE_POWER_OFF`)
 * 
 * @note This requires the display to be equipped with a GPIO Power on/off line and the driver to be configured accordingly.
 *       Set the `pwr_io_num` field in `waveshare_epaper_hw_config_t` to the appropriate GPIO pin. This function does nothing
 *       if the `pwr_io_num` field is set to `GPIO_NUM_NC` (-1).
 * 
 * @return
 *      - ESP_OK: Successfully set the power state
 *      - ESP_ERR_INVALID_ARG: Invalid argument, e.g. `handle == NULL`
 *      - ESP_ERR_INVALID_STATE: Driver is not installed or in an invalid state
 */
esp_err_t waveshare_epaper_set_hardware_power(waveshare_epaper_handle_t handle, waveshare_epaper_hardware_power_state_t power);

/**
 * @brief Assert or deassert the Waveshare ePaper display RESET line via hardware GPIO.
 * 
 * @param[in] handle Handle to the Waveshare ePaper display driver
 * @param[in] reset Hardware reset state to set (`WAVESHARE_EPAPER_HARDWARE_RESET` or `WAVESHARE_EPAPER_HARDWARE_NORMAL`)
 * 
 * @return
 *      - ESP_OK: Successfully set the reset state
 *      - ESP_ERR_INVALID_STATE: Driver is not installed or in an invalid state
 *      - ESP_ERR_INVALID_ARG: Invalid argument, e.g. `handle == NULL`
 */
esp_err_t waveshare_epaper_set_hardware_reset(waveshare_epaper_handle_t handle, waveshare_epaper_hardware_reset_state_t reset);



/**
 * @brief Turn the display on, refresh the panel, then power the display off and wait for the display to not be busy.
 *
 * @param[in] handle Handle to the Waveshare ePaper display driver
 * @param[in] enter_deepsleep If `true`, enter deep sleep at the end of the sequence;
 *                            if `false`, do not enter deep sleep
 * 
 * @note The display Charge Pump, VCOM ... are first powered on, the display is fully refreshed and the Charge Pump, VCOM ... is then turned off.
 * Optionally, the display can be set in deep sleep at the end of the sequence. This operation is triggered via one display auto command.

 * @return
 *      - ESP_OK: Sequence completed successfully
 *      - ESP_ERR_INVALID_ARG: Invalid argument, e.g. `handle == NULL`
 *      - ESP_ERR_INVALID_STATE: Driver is not installed or in an invalid state
 *      - Other `esp_err_t` values propagated from lower-level display operations
 */
esp_err_t waveshare_epaper_display_on_refresh_display_off(waveshare_epaper_handle_t handle, bool enter_deepsleep);









// TODO - advanced api
// set display(on / off)

esp_err_t waveshare_epaper_display_sleep(waveshare_epaper_handle_t handle);

esp_err_t waveshare_epaper_display_on_off(waveshare_epaper_handle_t handle, bool on, bool enableEpd);

esp_err_t waveshare_epaper_display_power_off_and_sleep(waveshare_epaper_handle_t handle);

esp_err_t waveshare_epaper_configure_display(waveshare_epaper_handle_t handle);

esp_err_t waveshare_epaper_send_data_buffer(waveshare_epaper_handle_t handle, const uint8_t* buffer, size_t buffer_length);

esp_err_t waveshare_epaper_display_refresh(waveshare_epaper_handle_t handle);


// esp_err_t test_spi_performance(waveshare_epaper_handle_t handle);

esp_err_t waveshare_epaper_read_data_stop(waveshare_epaper_handle_t handle, bool* data_stop);


/**
 * @brief Supported temperature sensors for Waveshare ePaper displays.
 */
typedef enum {
    WAVESHARE_EPAPER_TEMPERATURE_INTERNAL_SENSOR = 0,
    WAVESHARE_EPAPER_TEMPERATURE_EXTERNAL_SENSOR = 1
} waveshare_epaper_temperature_sensor_t;


esp_err_t waveshare_epaper_read_temperature(waveshare_epaper_handle_t handle, const waveshare_epaper_temperature_sensor_t sensor, uint16_t* temperature);


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