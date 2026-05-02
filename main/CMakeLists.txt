# -----------------------------------------------------------------------------------
# Copyright 2026, Gilles Zunino
# -----------------------------------------------------------------------------------

set(srcs
        "main.c"
        "test_patterns.c"
        
        "images/sample_image2in15.c"
)

idf_component_register(
	SRCS "${srcs}"
	INCLUDE_DIRS ""
        PRIV_REQUIRES esp_driver_usb_serial_jtag spi_flash esp_driver_gpio waveshare-epaper
)
