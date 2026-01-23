set(srcs
    "src/waveshare-epaper-logtag.c"

    "src/waveshare-epaper-config-validation.c"
    
    "src/waveshare-epaper-spi.c"
    "src/waveshare-epaper.c"
)

idf_component_register(
    SRCS ${srcs}
    # LDFRAGMENTS "waveshare-epaper-spi.lf"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_spi esp_driver_gpio
    PRIV_REQUIRES
)
