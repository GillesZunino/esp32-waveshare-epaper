set(srcs
    "src/waveshare-epaper.c"
)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_spi esp_driver_gpio
    PRIV_REQUIRES 
)
