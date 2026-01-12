// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_check.h>

#include "waveshare-epaper.h"


const char* TAG = "wepd_main";

//
// NOTE: For maximum performance, prefer IO MUX over GPIO Matrix routing
//  * See https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_master.html#gpio-matrix-routing
//

// SPI Host ID
const spi_host_device_t SPI_HOSTID = SPI2_HOST;

// SPI pins - Depends on the chip and the board
#if CONFIG_IDF_TARGET_ESP32
const gpio_num_t CS_LOAD_PIN = GPIO_NUM_19;
const gpio_num_t CLK_PIN = GPIO_NUM_18;
const gpio_num_t DIN_PIN = GPIO_NUM_16;

const gpio_num_t PWR_PIN = ;
const gpio_num_t BUSY_PIN = ;
const gpio_num_t RST_PIN = ;
const gpio_num_t DATA_CMD_PIN = ;
#else
#if CONFIG_IDF_TARGET_ESP32S3
const gpio_num_t CS_LOAD_PIN = GPIO_NUM_10;
const gpio_num_t CLK_PIN = GPIO_NUM_12;
const gpio_num_t DIN_PIN = GPIO_NUM_11;

const gpio_num_t PWR_PIN = GPIO_NUM_5;
const gpio_num_t BUSY_PIN = GPIO_NUM_9;
const gpio_num_t RST_PIN = GPIO_NUM_13;
const gpio_num_t DATA_CMD_PIN = GPIO_NUM_14;
#else
#if CONFIG_IDF_TARGET_ESP32C3
const gpio_num_t CS_LOAD_PIN = GPIO_NUM_1;
const gpio_num_t CLK_PIN = GPIO_NUM_2;
const gpio_num_t DIN_PIN = GPIO_NUM_3;

const gpio_num_t PWR_PIN = ;
const gpio_num_t BUSY_PIN = ;
const gpio_num_t RST_PIN = ;
const gpio_num_t DATA_CMD_PIN = ;
#endif
#endif
#endif


// Handle to the Waveshare ePaper display driver
waveshare_epaper_handle_t waveshare_epaper_handle = NULL;


void app_main(void) {
    // Configure SPI bus to communicate with Waveshare ePaper displays
    spi_bus_config_t spiBusConfig = {
        .mosi_io_num = DIN_PIN,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = CLK_PIN,

        .data2_io_num = GPIO_NUM_NC,
        .data3_io_num = GPIO_NUM_NC,

        .max_transfer_sz = SOC_SPI_MAXIMUM_BUFFER_SIZE,
        .flags = SPICOMMON_BUSFLAG_MASTER,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOSTID, &spiBusConfig, SPI_DMA_CH_AUTO));

    // Initialize the Waveshare ePaper display driver
    waveshare_epaper_config_t ePaperInitConfig = {
        .spi_cfg = {
            .host_id = SPI_HOSTID,

            .clock_source = SPI_CLK_SRC_DEFAULT,
            .clock_speed_hz = 2 * 1000000,

            .spics_io_num = CS_LOAD_PIN,
            .queue_size = 8
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

    ESP_ERROR_CHECK(set_epaper_power(waveshare_epaper_handle, false));

    do {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    } while (true);


    // Shutdown Waveshare ePaper display driver and SPI bus
    ESP_ERROR_CHECK(waveshare_epaper_driver_free(waveshare_epaper_handle));
    waveshare_epaper_handle = NULL;

    ESP_ERROR_CHECK(spi_bus_free(SPI_HOSTID));
}