// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#pragma once


/**
 * @brief Commands supported by Waveshare 2.15in e-Paper HAT+ (G) display.
 */
typedef enum {
    WAVESHARE_EPD_CMD_PANEL_SETTING                        = 0x00,
    WAVESHARE_EPD_CMD_POWER_SETTING                        = 0x01,
    WAVESHARE_EPD_CMD_POWER_OFF                            = 0x02,
    WAVESHARE_EPD_CMD_POWER_OFF_SEQUENCE_SETTINGS          = 0x03,
    WAVESHARE_EPD_CMD_POWER_ON                             = 0x04,
    WAVESHARE_EPD_CMD_BOOSTER_SOFT_START                   = 0x06,
    WAVESHARE_EPD_CMD_DEEP_SLEEP                           = 0x07,
    WAVESHARE_EPD_CMD_DATA_START_TRANSMISSION              = 0x10,
    WAVESHARE_EPD_CMD_DATA_STOP                            = 0x11,
    WAVESHARE_EPD_CMD_DISPLAY_REFRESH                      = 0x12,
    WAVESHARE_EPD_CMD_AUTO_SEQUENCE                        = 0x17,
    WAVESHARE_EPD_CMD_PLL_CONTROL                          = 0x30,
    WAVESHARE_EPD_CMD_TEMPERATURE_SENSOR_COMMAND           = 0x40,
    WAVESHARE_EPD_CMD_TEMPERATURE_SENSOR_CALIBRATION       = 0x41,
    WAVESHARE_EPD_CMD_TEMPERATURE_SENSOR_WRITE             = 0x42,
    WAVESHARE_EPD_CMD_TEMPERATURE_SENSOR_READ              = 0x43,
    WAVESHARE_EPD_CMD_VCOM_DATA_INTERVAL_SETTINGS          = 0x50,
    WAVESHARE_EPD_CMD_LOW_POWER_DETECTION                  = 0x51,
    WAVESHARE_EPD_CMD_RESOLUTION_SETTINGS                  = 0x61,
    WAVESHARE_EPD_CMD_GATE_SOURCE_START_SETTINGS           = 0x65,
    WAVESHARE_EPD_CMD_REVISION                             = 0x70,
    WAVESHARE_EPD_CMD_AUTO_MEASURE_VCOM                    = 0x80,
    WAVESHARE_EPD_CMD_VCOM_VALUE                           = 0x81,
    WAVESHARE_EPD_CMD_VCOM_DC_SETTINGS                     = 0x82,
    WAVESHARE_EPD_CMD_PARTIAL_WINDOW                       = 0x83,
    WAVESHARE_EPD_CMD_PROGRAM_MODE                         = 0x90,
    WAVESHARE_EPD_CMD_ACTIVE_PROGRAM                       = 0x91,
    WAVESHARE_EPD_CMD_READ_MTP_DATA                        = 0x92,
    WAVESHARE_EPD_CMD_REVISION_2                           = 0x9E,
    WAVESHARE_EPD_CMD_READ_MTP_RESERVED_BYTES              = 0x9F,
    WAVESHARE_EPD_CMD_POWER_SAVING                         = 0xE3,
    WAVESHARE_EPD_CMD_LVD_VOLTAGE_SELECTION                = 0xE4
} waveshare_epaper_command_t;