// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_check.h>

#include "waveshare-epaper.h"

#include "sample_image2in15.h"



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
#else
#if CONFIG_IDF_TARGET_ESP32S3
const gpio_num_t CS_PIN = GPIO_NUM_10;
const gpio_num_t CLK_PIN = GPIO_NUM_12;
const gpio_num_t DIN_PIN = GPIO_NUM_11;

const gpio_num_t PWR_PIN = GPIO_NUM_5;
const gpio_num_t BUSY_PIN = GPIO_NUM_9;
const gpio_num_t RST_PIN = GPIO_NUM_13;
const gpio_num_t DATA_CMD_PIN = GPIO_NUM_14;

const gpio_num_t LA_TRIGGER_PIN = GPIO_NUM_6; // Logic Analyzer trigger pin
#else
#if CONFIG_IDF_TARGET_ESP32C3
const gpio_num_t CS_PIN = GPIO_NUM_1;
const gpio_num_t CLK_PIN = GPIO_NUM_2;
const gpio_num_t DIN_PIN = GPIO_NUM_3;

const gpio_num_t PWR_PIN = ;
const gpio_num_t BUSY_PIN = ;
const gpio_num_t RST_PIN = ;
const gpio_num_t DATA_CMD_PIN = ;
#endif
#endif
#endif


    // Display resolution
#define EPD_2IN15G_WIDTH       160
#define EPD_2IN15G_HEIGHT      296



// Handle to the Waveshare ePaper display driver
waveshare_epaper_handle_t waveshare_epaper_handle = NULL;



esp_err_t blank_display(waveshare_epaper_handle_t waveshare_epaper_handle, uint16_t width, uint16_t height, uint8_t* image, size_t image_size) {
    // Clear
    for (uint16_t pixel_height = 0; pixel_height < height; pixel_height++) {
        for (uint16_t pixel_width = 0; pixel_width < width; pixel_width++) {
            image[pixel_width + pixel_height * width] = (0x01 << 6) | (0x01 << 4) | (0x01 << 2) | 0x01;
        }
    }

    ESP_ERROR_CHECK(waveshare_epaper_display_buffer(waveshare_epaper_handle, image, image_size));
    ESP_ERROR_CHECK(waveshare_epaper_display_on_off(waveshare_epaper_handle, true, false));
    ESP_ERROR_CHECK(waveshare_epaper_display_refresh(waveshare_epaper_handle));

    ESP_ERROR_CHECK(waveshare_epaper_display_power_off_and_sleep(waveshare_epaper_handle));

    return ESP_OK;
}


esp_err_t draw_raw_image(waveshare_epaper_handle_t waveshare_epaper_handle, const uint8_t* raw_image, uint16_t width, uint16_t height, uint8_t* image, size_t image_size) {
    // Copy the sample image to a DMA capable memory buffer
    memcpy(image, raw_image, image_size);

    ESP_ERROR_CHECK(waveshare_epaper_display_buffer(waveshare_epaper_handle, image, image_size));
    ESP_ERROR_CHECK(waveshare_epaper_display_on_off(waveshare_epaper_handle, true, false));
    ESP_ERROR_CHECK(waveshare_epaper_display_refresh(waveshare_epaper_handle));

    ESP_ERROR_CHECK(waveshare_epaper_display_power_off_and_sleep(waveshare_epaper_handle));

    return ESP_OK;
}













void app_main(void) {

    // Logic Analyzer trigger
    // Configure CS pin - The pin level is initially set to HIGH to deselect the device
    gpio_config_t cs_io_conf = {
        .pin_bit_mask = BIT64(LA_TRIGGER_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&cs_io_conf));
    ESP_ERROR_CHECK(gpio_set_level(LA_TRIGGER_PIN, 0));
    vTaskDelay(pdMS_TO_TICKS(1));
    ESP_ERROR_CHECK(gpio_set_level(LA_TRIGGER_PIN, 1));



    // Allocate a buffer (DMA capable)
    uint16_t width = (EPD_2IN15G_WIDTH % 4 == 0)? (EPD_2IN15G_WIDTH / 4 ): (EPD_2IN15G_WIDTH / 4 + 1);
    uint16_t height = EPD_2IN15G_HEIGHT;
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

    ESP_LOGI(TAG, "Initialize Waveshare ePaper display driver");
    ESP_ERROR_CHECK(waveshare_epaper_driver_init(&ePaperInitConfig, &waveshare_epaper_handle));

    // ESP_ERROR_CHECK(reset_epaper_hardware(waveshare_epaper_handle));

    //ESP_ERROR_CHECK(test_spi_performance(waveshare_epaper_handle));
    ESP_ERROR_CHECK(waveshare_epaper_configure_display(waveshare_epaper_handle));


#define DRAW_TEST_PATTERN 0
#define READ_FROM_DISPLAY 0

    // ------------------------------------------------------------------------------------------------------
    // Read metadata from display
    // ------------------------------------------------------------------------------------------------------
#if READ_FROM_DISPLAY
    // TODO: Should be done after a data transfert
    // bool data_stop = false;
    // ESP_ERROR_CHECK(waveshare_epaper_read_data_stop(waveshare_epaper_handle, &data_stop));

    uint16_t internal_temp = 0;
    ESP_ERROR_CHECK(waveshare_epaper_read_temperature(waveshare_epaper_handle, true, &internal_temp));

    uint16_t external_temp = 0;
    ESP_ERROR_CHECK(waveshare_epaper_read_temperature(waveshare_epaper_handle, false, &external_temp));

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
    // Currently uses two bits per pixel (2.15in Hat G)
    //
    // * 00 -> Black
    // * 01 -> White
    // * 10 -> Yellow
    // * 11 -> Red
    // ------------------------------------------------------------------------------------------------------
#if DRAW_TEST_PATTERN
    for (uint16_t pixel_height = 0; pixel_height < EPD_2IN15G_HEIGHT; pixel_height++) {
        for (uint16_t pixel_width = 0; pixel_width < EPD_2IN15G_WIDTH; pixel_width++) {

            size_t byte_index = (pixel_width / 4) + (pixel_height * width);
            uint8_t pixel_value = 0;

            // Alternate colors for testing
            if ((pixel_width + pixel_height) % 4 == 0) {
                pixel_value = 0x00; // Black
            } else if ((pixel_width + pixel_height) % 4 == 1) {
                pixel_value = 0x01; // White
            } else if ((pixel_width + pixel_height) % 4 == 2) {
                pixel_value = 0x02; // Yellow
            } else {
                pixel_value = 0x03; // Red
            }

            // Each byte contains 4 pixels (2 bits per pixel)
            uint8_t shift = (3 - (pixel_width % 4)) * 2;
            image[byte_index] &= ~(0x03 << shift); // Clear the bits
            image[byte_index] |= (pixel_value << shift); // Set the new value
        }
    }

    ESP_ERROR_CHECK(waveshare_epaper_display_buffer(waveshare_epaper_handle, image, image_size));
    ESP_ERROR_CHECK(waveshare_epaper_display_on_off(waveshare_epaper_handle, true, false));
    ESP_ERROR_CHECK(waveshare_epaper_display_refresh(waveshare_epaper_handle));

    ESP_ERROR_CHECK(waveshare_epaper_display_power_off_and_sleep(waveshare_epaper_handle));
#else
    ESP_ERROR_CHECK(draw_raw_image(waveshare_epaper_handle, gImage_2in15g, EPD_2IN15G_WIDTH, EPD_2IN15G_HEIGHT, image, image_size));
#endif



//    ESP_ERROR_CHECK(blank_display(waveshare_epaper_handle, width, height, image, image_size));

    do {
        vTaskDelay(pdMS_TO_TICKS(1000));
    } while (true);


    // TODO: Bring device in reset hardware
    ESP_ERROR_CHECK(set_epaper_power(waveshare_epaper_handle, false));

    // Shutdown Waveshare ePaper display driver and SPI bus
    ESP_ERROR_CHECK(waveshare_epaper_driver_free(waveshare_epaper_handle));
    waveshare_epaper_handle = NULL;

    ESP_ERROR_CHECK(spi_bus_free(SPI_HOSTID));
}