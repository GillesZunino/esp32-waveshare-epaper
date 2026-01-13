// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#include <esp_check.h>

#include "waveshare-epaper.h"
#include "waveshare-epaper-context.h"
#include "waveshare-epaper-spi.h"




static void pre_spi_transaction_isr_callback(spi_transaction_t* t) {
    // ESP_EARLY_LOGV(TAG, "cs high %d.", ((eeprom_context_t*)t->user)->cfg.cs_io);
    // gpio_set_level(((eeprom_context_t*)t->user)->cfg.cs_io, 1);
}

static void post_spi_transaction_isr_callback(spi_transaction_t* t) {
    // gpio_set_level(((eeprom_context_t*)t->user)->cfg.cs_io, 0);
    // ESP_EARLY_LOGV(TAG, "cs low %d.", ((eeprom_context_t*)t->user)->cfg.cs_io);
}





esp_err_t waveshare_epaper_spi_init_private(const waveshare_epaper_config_t* config, waveshare_epaper_context_t* pDisplay) {
    // Add an SPI device on the given bus - We accept the SPI bus configuration as is
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
            .flags = 0,
            .cmd = command,
            .addr = 0,
            .length = 8,
            .rxlength = 0,
            .override_freq_hz = 0,
            .user = NULL,
            .tx_buffer = NULL,
            .rx_buffer = NULL
        };

        spi_device_queue_trans(handle->spi_device_handle, &commandTransaction, portMAX_DELAY);

        // Transaction for the data
        spi_transaction_t dataTransaction = {
            .flags = 0,
            .cmd = 0,
            .addr = 0,
            .length = data_len * 8, // length in bits
            .rxlength = 0,
            .override_freq_hz = 0,
            .user = NULL,
            .tx_buffer = data,
            .rx_buffer = NULL
        };

        spi_device_queue_trans(handle->spi_device_handle, &dataTransaction, portMAX_DELAY);

        spi_transaction_t* pCommandTransactionResult;
        spi_transaction_t* pDataTransactionResult;

        spi_device_get_trans_result(handle->spi_device_handle, &pCommandTransactionResult, portMAX_DELAY);
        spi_device_get_trans_result(handle->spi_device_handle, &pDataTransactionResult, portMAX_DELAY);

    spi_device_release_bus(handle->spi_device_handle);
    
    return err;

}

