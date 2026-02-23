// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------

#include <esp_check.h>

#include "waveshare-epaper.h"


esp_err_t draw_test_pattern(waveshare_epaper_handle_t waveshare_epaper_handle, uint16_t width, uint16_t height, uint8_t* image, size_t image_size);