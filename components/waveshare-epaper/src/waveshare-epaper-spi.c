// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#include <esp_check.h>

#include "waveshare-epaper.h"
#include "waveshare-epaper-context.h"
#include "waveshare-epaper-spi.h"


#define COMMAND_LEVEL 0
#define DATA_LEVEL    1


static void pre_spi_transaction_isr_callback(spi_transaction_t* t) {
    // ESP_EARLY_LOGV(TAG, "cs high %d.", ((eeprom_context_t*)t->user)->cfg.cs_io);
    // gpio_set_level(((eeprom_context_t*)t->user)->cfg.cs_io, 1);

    uint32_t userContext = (uint32_t)t->user;
    uint32_t dcLevel = userContext == 1 ? COMMAND_LEVEL : DATA_LEVEL;
    // TODO: DO not hardcode the pin
    //ESP_EARLY_LOGV("ISR SPI", "d/c -> %d.", dcLevel);
    gpio_set_level(GPIO_NUM_14, dcLevel);
}

static void post_spi_transaction_isr_callback(spi_transaction_t* t) {
    // TODO: DO not hardcode the pin
    //ESP_EARLY_LOGV("ISR SPI", "d/c -> COMMAND_LEVEL");
    gpio_set_level(GPIO_NUM_14, COMMAND_LEVEL);
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

        // duty_cycle_pos
        // cs_ena_pretrans
        // cs_ena_posttrans

        .clock_speed_hz = config->spi_cfg.clock_speed_hz,
        .input_delay_ns = config->spi_cfg.input_delay_ns,

        // sample_point

        .spics_io_num = config->spi_cfg.spics_io_num,

        .flags = 0,
        .queue_size = config->spi_cfg.queue_size,

        .pre_cb = pre_spi_transaction_isr_callback,
        .post_cb = post_spi_transaction_isr_callback
    };

    return spi_bus_add_device(config->spi_cfg.host_id, &spiDeviceInterfaceConfig, &pDisplay->spi_device_handle);
}


esp_err_t waveshare_epaper_spi_send(waveshare_epaper_handle_t handle, uint8_t command, const void* data, uint32_t data_len) {
    
    esp_err_t err = ESP_OK;
    err = spi_device_acquire_bus(handle->spi_device_handle, portMAX_DELAY);

        // Transaction for the command
        spi_transaction_t commandTransaction = {
            .flags = SPI_TRANS_USE_TXDATA,
            .cmd = 0,
            .addr = 0,
            .length = sizeof(command) * 8,
            .rxlength = 0,
            .override_freq_hz = 0,
            .user = (void*)1,
            //.tx_buffer = NULL,
            .tx_data[0] = command,
            .rx_buffer = NULL
        };

        err = spi_device_polling_transmit(handle->spi_device_handle, &commandTransaction);

        // Transaction for the data
        if ((data != NULL) && (data_len > 0)) {
            bool useTxData = data_len <= 4;
            spi_transaction_t dataTransaction = {
                .flags = useTxData ? SPI_TRANS_USE_TXDATA : 0,
                .cmd = 0,
                .addr = 0,
                .length = data_len * 8, // length in bits
                .rxlength = 0,
                .override_freq_hz = 0,
                .user = (void*)10,
                .tx_buffer = data,
                .rx_buffer = NULL
            };

            if (useTxData) {
                memcpy(dataTransaction.tx_data, data, data_len);
            } else {
                dataTransaction.tx_buffer = data;
            }

            err = spi_device_polling_transmit(handle->spi_device_handle, &dataTransaction);
        }

    spi_device_release_bus(handle->spi_device_handle);

    // TODO: Wait for BUSY to because "available"
    
    return err;
}

