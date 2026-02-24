// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_check.h>
// #include <esp_task_wdt.h>

#include "waveshare-epaper.h"
#include "test_patterns.h"
#include "images/sample_image2in15.h"



const char* TAG = "wepd_main";

//
// NOTE: For maximum performance, prefer IO MUX over GPIO Matrix routing
//  * See https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_master.html#gpio-matrix-routing
//

// SPI Host ID
const spi_host_device_t SPI_HOSTID = SPI2_HOST;

// SPI pins - Depends on the chip and the board
#if CONFIG_IDF_TARGET_ESP32
const gpio_num_t CS_PIN = GPIO_NUM_19;
const gpio_num_t CLK_PIN = GPIO_NUM_18;
const gpio_num_t DIN_PIN = GPIO_NUM_16;

const gpio_num_t PWR_PIN = ;
const gpio_num_t BUSY_PIN = ;
const gpio_num_t RST_PIN = ;
const gpio_num_t DATA_CMD_PIN = ;

const gpio_num_t LOGIC_ANALYZER_TRIGGER_PIN = ;
#else
#if CONFIG_IDF_TARGET_ESP32S3
const gpio_num_t CS_PIN = GPIO_NUM_10;
const gpio_num_t CLK_PIN = GPIO_NUM_12;
const gpio_num_t DIN_PIN = GPIO_NUM_11;

const gpio_num_t PWR_PIN = GPIO_NUM_5;
const gpio_num_t BUSY_PIN = GPIO_NUM_9;
const gpio_num_t RST_PIN = GPIO_NUM_13;
const gpio_num_t DATA_CMD_PIN = GPIO_NUM_14;

const gpio_num_t LOGIC_ANALYZER_TRIGGER_PIN = GPIO_NUM_6;
#else
#if CONFIG_IDF_TARGET_ESP32C3
const gpio_num_t CS_PIN = GPIO_NUM_1;
const gpio_num_t CLK_PIN = GPIO_NUM_2;
const gpio_num_t DIN_PIN = GPIO_NUM_3;

const gpio_num_t PWR_PIN = ;
const gpio_num_t BUSY_PIN = ;
const gpio_num_t RST_PIN = ;
const gpio_num_t DATA_CMD_PIN = ;

const gpio_num_t LOGIC_ANALYZER_TRIGGER_PIN = ;
#endif
#endif
#endif


// Display resolution
#define EPD_2IN15G_WIDTH       160
#define EPD_2IN15G_HEIGHT      296


// Time to wait after physically powering on a Waveshare ePaper display
const TickType_t PowerOnDelayTicks = pdMS_TO_TICKS(50);
// Time to wait after physically powering off a Waveshare ePaper display
const TickType_t PowerOffDelayTicks = pdMS_TO_TICKS(10);


// Handle to the Waveshare ePaper display driver
waveshare_epaper_handle_t waveshare_epaper_handle = NULL;



esp_err_t blank_display(waveshare_epaper_handle_t waveshare_epaper_handle, uint16_t width, uint16_t height, uint8_t* image, size_t image_size) {
    // Set all pixels to white
    memset(image, 0x55, image_size);
    return waveshare_epaper_send_data_buffer(waveshare_epaper_handle, image, image_size);
}


esp_err_t draw_raw_image(waveshare_epaper_handle_t waveshare_epaper_handle, const uint8_t* raw_image, uint16_t width, uint16_t height, uint8_t* image, size_t image_size) {
    // Copy the sample image to a DMA capable memory buffer
    memcpy(image, raw_image, image_size);
    return waveshare_epaper_send_data_buffer(waveshare_epaper_handle, image, image_size);
}





static void safe_watchdog_wait(uint32_t seconds) {
    // Wait in slices to avoid triggering the watchdog - We use CONFIG_ESP_TASK_WDT_TIMEOUT_S (default 5s - customized to 20s) -1 to avoid waking up too frequently
    const uint32_t slice_seconds = CONFIG_ESP_TASK_WDT_TIMEOUT_S - 1;
    const TickType_t slice_ticks = pdMS_TO_TICKS(slice_seconds * 1000);
    const uint32_t total_slices = seconds / slice_seconds;
    for (uint32_t slice_count = 0; slice_count < total_slices; slice_count++) {
        vTaskDelay(slice_ticks);
    }

    uint32_t remaining_seconds = seconds % slice_seconds;
    if (remaining_seconds > 0) {
        vTaskDelay(pdMS_TO_TICKS(remaining_seconds * 1000));
    }
}


#ifdef ENABLE_LOGIC_ANALYZER
    #define TRIGGER_LOGIC_ANALYZER() trigger_logic_analyzer(LOGIC_ANALYZER_TRIGGER_PIN, pdMS_TO_TICKS(40))
#else
    #define TRIGGER_LOGIC_ANALYZER() ((void)0)
#endif

esp_err_t trigger_logic_analyzer(gpio_num_t trigger_pin, TickType_t pulse_length_ticks) {
    static bool is_trigger_pin_configured = false;
    if (!is_trigger_pin_configured) {
        gpio_config_t cs_io_conf = {
            .pin_bit_mask = BIT64(trigger_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };

        ESP_RETURN_ON_ERROR(gpio_config(&cs_io_conf), TAG, "Failed to configure GPIO for Logic Analyzer trigger pin");
        is_trigger_pin_configured = true;
    }

    // Create a "guard time" in which we force the GPIO pin to LOW
    ESP_LOGI(TAG, "[LOGIC ANALYZER] -> _");
    ESP_RETURN_ON_ERROR(gpio_set_level(trigger_pin, 0), TAG, "Failed to set Logic Analyzer trigger pin low");
    vTaskDelay(pulse_length_ticks);

    // Create the GPIO pulse from LOW to HIGH and back to LOW
    ESP_LOGI(TAG, "[LOGIC ANALYZER] -> _|");
    ESP_RETURN_ON_ERROR(gpio_set_level(trigger_pin, 1), TAG, "Failed to set Logic Analyzer trigger pin high");
    vTaskDelay(pulse_length_ticks);
    ESP_LOGI(TAG, "[LOGIC ANALYZER] -> |_");
    ESP_RETURN_ON_ERROR(gpio_set_level(trigger_pin, 0), TAG, "Failed to set Logic Analyzer trigger pin low");
    return ESP_OK;
}



void app_main(void) {

TRIGGER_LOGIC_ANALYZER();

    // Allocate a buffer (DMA capable)
    uint16_t width = (EPD_2IN15G_WIDTH % 4 == 0) ? (EPD_2IN15G_WIDTH / 4) : (EPD_2IN15G_WIDTH / 4 + 1);
    uint16_t height = EPD_2IN15G_HEIGHT;
    // TODO: Make sure this is correctly aligned when allocated
    size_t image_size = width * height;
    uint8_t* image = heap_caps_calloc(1, image_size, MALLOC_CAP_DMA);


    // Configure SPI bus to communicate with Waveshare ePaper displays
    spi_bus_config_t spiBusConfig = {
        .mosi_io_num = DIN_PIN,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = CLK_PIN,

        .data2_io_num = GPIO_NUM_NC,
        .data3_io_num = GPIO_NUM_NC,

        // SPI Max transfer size MUST be at least the size of one image buffer
        .max_transfer_sz = image_size,
        .flags = SPICOMMON_BUSFLAG_MASTER,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOSTID, &spiBusConfig, SPI_DMA_CH_AUTO));

    // Initialize the Waveshare ePaper display driver
    waveshare_epaper_config_t ePaperInitConfig = {
        .spi_cfg = {
            .host_id = SPI_HOSTID,

            .clock_source = SPI_CLK_SRC_DEFAULT,
            .clock_speed_hz = 10 * 1000000,

            .input_delay_ns = 0,
            .sample_point = SPI_SAMPLING_POINT_PHASE_0,

            .spics_io_num = CS_PIN
        },
        .hw_config = {
            .pwr_io_num = PWR_PIN,
            .busy_io_num = BUSY_PIN,
            .rst_io_num = RST_PIN,
            .data_cmd_io_num = DATA_CMD_PIN
        }
    };

    // Initialize the ePaper display driver - This holds the RESET pin low (in reset) and the POWER pin low (in power down)
    ESP_LOGI(TAG, "Initialize Waveshare ePaper display driver");
    ESP_ERROR_CHECK(waveshare_epaper_driver_init(&ePaperInitConfig, &waveshare_epaper_handle));

    // Power the display on (if there is a programmable GPIO pin for power control) and release from reset
    ESP_LOGI(TAG, "Hardware power on and release reset for Waveshare ePaper display");
    ESP_ERROR_CHECK(waveshare_epaper_hardware_power_on_and_deassert_reset(waveshare_epaper_handle, PowerOnDelayTicks));

    // ------------------------------------------------------------------------------------------------------
    // Read metadata from display
    // ------------------------------------------------------------------------------------------------------
#if READ_FROM_DISPLAY
    uint16_t internal_temp = 0;
    ESP_ERROR_CHECK(waveshare_epaper_read_temperature(waveshare_epaper_handle, WAVESHARE_EPAPER_TEMPERATURE_INTERNAL_SENSOR, &internal_temp));

    uint16_t external_temp = 0;
    ESP_ERROR_CHECK(waveshare_epaper_read_temperature(waveshare_epaper_handle, WAVESHARE_EPAPER_TEMPERATURE_EXTERNAL_SENSOR, &external_temp));

    bool low_power_state = false;
    ESP_ERROR_CHECK(waveshare_epaper_read_low_power_state(waveshare_epaper_handle, &low_power_state));

    uint32_t revision = 0;
    ESP_ERROR_CHECK(waveshare_epaper_read_revision(waveshare_epaper_handle, &revision));

    uint8_t vcom = 0;
    ESP_ERROR_CHECK(waveshare_epaper_read_vcom(waveshare_epaper_handle, &vcom));

    uint8_t revision2 = 0;
    ESP_ERROR_CHECK(waveshare_epaper_read_revision2(waveshare_epaper_handle, &revision2));
#endif
    // ------------------------------------------------------------------------------------------------------

    // Configure the display to receive an image - This is required after every poower on / reset
    ESP_ERROR_CHECK(waveshare_epaper_configure_display(waveshare_epaper_handle));

    do {
        // ------------------------------------------------------------------------------------------------------
        // The display is assumed to have been powered on, taken our of reset and configured to display content
        // It is NOT required for the display high voltage to be on (aka 'software power on')
        // ------------------------------------------------------------------------------------------------------

        // Show the test pattern for 180s
        ESP_LOGI(TAG, "Drawing test pattern");
        ESP_ERROR_CHECK(draw_test_pattern(waveshare_epaper_handle, EPD_2IN15G_WIDTH, EPD_2IN15G_HEIGHT, image, image_size));
        ESP_ERROR_CHECK(waveshare_epaper_display_on_refresh_display_off(waveshare_epaper_handle, true));
        ESP_ERROR_CHECK(waveshare_epaper_hardware_power_off_and_assert_reset(waveshare_epaper_handle, PowerOffDelayTicks));
        safe_watchdog_wait(180);

TRIGGER_LOGIC_ANALYZER();

        // Power on physically, reset the device and re-configure
        ESP_ERROR_CHECK(waveshare_epaper_hardware_power_on_and_deassert_reset(waveshare_epaper_handle, PowerOnDelayTicks));
        ESP_ERROR_CHECK(waveshare_epaper_configure_display(waveshare_epaper_handle));

        // Show the test image for 180s
        ESP_LOGI(TAG, "Drawing Espressif test image");
        ESP_ERROR_CHECK(draw_raw_image(waveshare_epaper_handle, gImage_2in15g, EPD_2IN15G_WIDTH, EPD_2IN15G_HEIGHT, image, image_size));
        ESP_ERROR_CHECK(waveshare_epaper_display_on_refresh_display_off(waveshare_epaper_handle, true));
        ESP_ERROR_CHECK(waveshare_epaper_hardware_power_off_and_assert_reset(waveshare_epaper_handle, PowerOffDelayTicks));
        safe_watchdog_wait(180);

TRIGGER_LOGIC_ANALYZER();

        // Power on physically, reset the device and re-configure
        ESP_ERROR_CHECK(waveshare_epaper_hardware_power_on_and_deassert_reset(waveshare_epaper_handle, PowerOnDelayTicks));
        ESP_ERROR_CHECK(waveshare_epaper_configure_display(waveshare_epaper_handle));

        // Blank display for 180s
        ESP_LOGI(TAG, "Blanking display");
        ESP_ERROR_CHECK(blank_display(waveshare_epaper_handle, width, height, image, image_size));
        ESP_ERROR_CHECK(waveshare_epaper_display_on_refresh_display_off(waveshare_epaper_handle, true));
        ESP_ERROR_CHECK(waveshare_epaper_hardware_power_off_and_assert_reset(waveshare_epaper_handle, PowerOffDelayTicks));
        safe_watchdog_wait(180);

TRIGGER_LOGIC_ANALYZER();

        // Prepare the display for the next loop iteration
        ESP_ERROR_CHECK(waveshare_epaper_hardware_power_on_and_deassert_reset(waveshare_epaper_handle, PowerOnDelayTicks));
        ESP_ERROR_CHECK(waveshare_epaper_configure_display(waveshare_epaper_handle));
    } while (true);


    // Blank the display as it might be left unused for long periods of time
    ESP_ERROR_CHECK(blank_display(waveshare_epaper_handle, width, height, image, image_size));
    ESP_ERROR_CHECK(waveshare_epaper_display_on_refresh_display_off(waveshare_epaper_handle, true));

    // Hardware power the display off and hold it in reset
    ESP_ERROR_CHECK(waveshare_epaper_hardware_power_off_and_assert_reset(waveshare_epaper_handle, PowerOffDelayTicks));

    // Shutdown Waveshare ePaper display driver and SPI bus
    ESP_ERROR_CHECK(waveshare_epaper_driver_free(waveshare_epaper_handle));
    waveshare_epaper_handle = NULL;

    ESP_ERROR_CHECK(spi_bus_free(SPI_HOSTID));
}