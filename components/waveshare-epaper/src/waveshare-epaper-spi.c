// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#include <esp_check.h>
#include <esp_log.h>

#include "waveshare-epaper.h"
#include "waveshare-epaper-context.h"
#include "waveshare-epaper-spi.h"



static const char* TAG = "wepd_spi";




typedef struct waveshare_epaper_spi_isr_context {
    waveshare_epaper_handle_t handle;
    command_data_level_t level;
} waveshare_epaper_spi_isr_context_t;


static esp_err_t waveshare_epaper_spi_send_private(waveshare_epaper_handle_t handle, uint8_t command, const void* data, uint32_t data_len, bool acquire_bus, bool wait_on_busy);

esp_err_t wait_for_non_busy(waveshare_epaper_context_t* pDisplay);

static void setup_gpio_interrupt(gpio_num_t busy_pin, gpio_isr_t isr_handler);
static esp_err_t wait_for_non_busy_polling(waveshare_epaper_context_t* pDisplay);
static esp_err_t wait_for_non_busy_interupt(waveshare_epaper_context_t* pDisplay);
static void IRAM_ATTR gpio_isr_handler_eventgroup(void* arg);
static esp_err_t wait_for_pin_high_event_group();



static void pre_spi_transaction_isr_callback(spi_transaction_t* t) {
    waveshare_epaper_spi_isr_context_t* userContext = (waveshare_epaper_spi_isr_context_t*)t->user;
    esp_err_t err = gpio_set_level(userContext->handle->hw_config.data_cmd_io_num, userContext->level);
    if (err != ESP_OK) {
        ESP_EARLY_LOGE(TAG, "Failed to set DC line pre-SPI transaction");
    }
}

static void post_spi_transaction_isr_callback(spi_transaction_t* t) {
    waveshare_epaper_spi_isr_context_t* userContext = (waveshare_epaper_spi_isr_context_t*)t->user;
    esp_err_t err = gpio_set_level(userContext->handle->hw_config.data_cmd_io_num, COMMAND_LEVEL);
    if (err != ESP_OK) {
        ESP_EARLY_LOGE(TAG, "Failed to reset DC line post-SPI transaction");
    }
}


esp_err_t waveshare_epaper_spi_init_private(const waveshare_epaper_config_t* config, waveshare_epaper_context_t* pDisplay) {
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

    return spi_bus_add_device(config->spi_cfg.host_id, &spiDeviceInterfaceConfig, &pDisplay->spi_device_handle);
}


esp_err_t waveshare_epaper_spi_send(waveshare_epaper_handle_t handle, uint8_t command, const void* data, uint32_t data_len, bool wait_on_busy) {
    return waveshare_epaper_spi_send_private(handle, command, data, data_len, true, wait_on_busy);
}

esp_err_t waveshare_epaper_spi_send_exclusive(waveshare_epaper_handle_t handle, uint8_t command, const void* data, uint32_t data_len, bool wait_on_busy) {
    return waveshare_epaper_spi_send_private(handle, command, data, data_len, false, wait_on_busy);
}


static esp_err_t waveshare_epaper_spi_send_private(waveshare_epaper_handle_t handle, uint8_t command, const void* data, uint32_t data_len, bool acquire_bus, bool wait_on_busy) {
    esp_err_t err = ESP_OK;
    if (acquire_bus) {
        err = spi_device_acquire_bus(handle->spi_device_handle, portMAX_DELAY);
    }
    
        waveshare_epaper_spi_isr_context_t isrContextCommand = {
            .handle = handle,
            .level = COMMAND_LEVEL
        };
        
        // Transaction for the command - DMA in SPI_DEVICE_HALFDUPLEX mode allowed since we only set SPI_TRANS_USE_TXDATA
        spi_transaction_t commandTransaction = {
            .flags = SPI_TRANS_USE_TXDATA,
            .cmd = 0,
            .addr = 0,
            .length = sizeof(command) * 8,
            .rxlength = 0,
            .override_freq_hz = 0,
            .user = (void*)&isrContextCommand,
            //.tx_buffer = NULL,
            .tx_data[0] = command,
            .rx_buffer = NULL
        };

        err = spi_device_polling_transmit(handle->spi_device_handle, &commandTransaction);


        // Transaction for the data- DMA in SPI_DEVICE_HALFDUPLEX mode allowed since we only set SPI_TRANS_USE_TXDATA
        if ((data != NULL) && (data_len > 0)) {
            bool useTxData = data_len <= 4;
            spi_transaction_t dataTransaction = {
                .flags = useTxData ? SPI_TRANS_USE_TXDATA : 0,
                .cmd = 0,
                .addr = 0,
                .length = data_len * 8, // length in bits
                .rxlength = 0,
                .override_freq_hz = 0,
                .user = (void*)&isrContextCommand,
                
                //.tx_buffer = data,
                //.rx_buffer = NULL
            };
            isrContextCommand.level = DATA_LEVEL;

            if (useTxData) {
                memcpy(dataTransaction.tx_data, data, data_len);
            } else {
                dataTransaction.tx_buffer = data;
            }

            err = spi_device_polling_transmit(handle->spi_device_handle, &dataTransaction);
        }

    if (acquire_bus) {
        spi_device_release_bus(handle->spi_device_handle);
    }

    // TODO: Wait for BUSY to because "available"
    if (wait_on_busy) {
        err = wait_for_non_busy(handle);
    }

    return err;
}


esp_err_t wait_for_non_busy(waveshare_epaper_context_t* pDisplay) {
    return  wait_for_non_busy_polling(pDisplay);
}


esp_err_t waveshare_epaper_spi_send_with_response(waveshare_epaper_handle_t handle, uint8_t command, uint8_t* response_buffer, uint32_t response_buffer_len) {
    esp_err_t err = ESP_OK;

    // List of commands with a "read phase"
    // R11 - Data Stop (DSP) [1 bytes W / 1 byte R]
    // R40 - Temperature Sensor Command (TSC) [1 byte W / 2 bytes R]
    // R43 - Temperature Sensor Read (TSR) [1 byte W / 2 bytes R]
    // R51 - Low Power Detection (LPD) [1 bytes W / 1 byte R]
    // R70 - Revision (REV) [1 byte W / 3 bytes R]
    // R81 - VCOM VCOM Value (VV) [1 bytes W / 1 byte R]
    // R92 - Read MTP Data (RMTP) [1 bytes W / N byte R] where N <= 0x17FF + 1 (dummy)
    // R9E - Revision2 (REV2) [1 bytes W / 1 byte R]
    // R9F - Read MTP Reserved bytes [1 bytes W / N byte R] where N = 0x63 + 1 (dummy)


    waveshare_epaper_spi_isr_context_t isrContextCommand = {
        .handle = handle,
        .level = COMMAND_LEVEL
    };


    //
    // Commands with data read back are performed in 3-wire SPI mode (MISO == MOSI)
    //   * MOSI | <Command> | N/A    | N/A    | N/A    | N/A | N/A    |
    //   * MOSI | N/A       | <Data> | <Data> | <Data> | ... | <Data> |
    //

// TODO; Will this work if we do not toggle C/D line to HIGH after the transmit ?

    //
    // In SPI_DEVICE_HALFDUPLEX mode, it is not possible to have both a TX and RX phase in the same transaction
    // We send the command using "cmd" and perform a "data read only" SPI transaction
    // 

    // Transaction to trigger the read
    bool useRxData = response_buffer_len <= 4;
    spi_transaction_ext_t commandTransaction = {
        .base = {
            .flags = SPI_TRANS_VARIABLE_CMD | (useRxData ? SPI_TRANS_USE_RXDATA : 0),
            .cmd = command,
            .addr = 0,
            .length = response_buffer_len * 8, // Specify the transaction length. In this case only the size of read since we are setting up the command with .cmd
            .rxlength = response_buffer_len * 8,
            .override_freq_hz = 0,
            .user = (void*)&isrContextCommand,
            .tx_buffer = NULL,
            .rx_buffer = useRxData ? NULL : (void*) response_buffer
            // .rx_data = { 0 }
        },
        .command_bits = 8,
        .address_bits = 0,
        .dummy_bits = 0
    };

    err = spi_device_polling_transmit(handle->spi_device_handle, (spi_transaction_t*) &commandTransaction);

    if (err == ESP_OK) {
        // TODO; Can we avoid the copy ? Is it desirable to always pass the buffer even for 1 byte
        if (useRxData) {
            memcpy(response_buffer, commandTransaction.base.rx_data, response_buffer_len);
        }
    }

    return err;

}


// BUSY LOW -> idle
// BUSY HIGH -> busy

//-------------------- polling based ------------------------
static esp_err_t wait_for_non_busy_polling(waveshare_epaper_context_t* pDisplay) {
    // EXPECT: 1 to 10ms ?
    while (!gpio_get_level(pDisplay->hw_config.busy_io_num)) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    return ESP_OK;
}
// --------------------------------------------------------

// -------------------- semnaphore based ------------------------
static DMA_ATTR SemaphoreHandle_t gpio_semaphore = NULL;
static bool irq_attr_initialized = false;

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(gpio_semaphore, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static void setup_gpio_interrupt(gpio_num_t busy_pin, gpio_isr_t isr_handler) {
    if (irq_attr_initialized) {
        return;
    }

    gpio_semaphore = xSemaphoreCreateBinary();
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << busy_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&io_conf);
    
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    gpio_isr_handler_add(busy_pin, isr_handler, NULL);

    irq_attr_initialized = true;
}

static esp_err_t wait_for_non_busy_interupt(waveshare_epaper_context_t* pDisplay) {
    // EXPECT: 10 to 100 micro seconds ? (10 to 50 micro seconds overhead)
    xSemaphoreTake(gpio_semaphore, portMAX_DELAY);
    return ESP_OK;
}

// use with
//  setup_gpio_interrupt(handle->hw_config.busy_io_num, gpio_isr_handler);
//  wait_for_non_busy_interupt(handle);
// -------------------------------------------------------------


// -------------------- EventGroup based ------------------------
static EventGroupHandle_t gpio_event_group;
#define PIN_HIGH_BIT BIT0


static void IRAM_ATTR gpio_isr_handler_eventgroup(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xEventGroupSetBitsFromISR(gpio_event_group, PIN_HIGH_BIT, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static esp_err_t wait_for_pin_high_event_group() {
    xEventGroupWaitBits(gpio_event_group, PIN_HIGH_BIT, 
                        pdTRUE, pdFALSE, portMAX_DELAY);
            return ESP_OK;
}
// Use with
//  setup_gpio_interrupt(handle->hw_config.busy_io_num, gpio_isr_handler_eventgroup);
//  wait_for_pin_high_event_group();
// --------------------------------------------------------------

// ULP ?