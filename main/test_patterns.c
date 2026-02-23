// -----------------------------------------------------------------------------------
// Copyright 2026, Gilles Zunino
// -----------------------------------------------------------------------------------


#include "test_patterns.h"



esp_err_t draw_test_pattern(waveshare_epaper_handle_t waveshare_epaper_handle, uint16_t width, uint16_t height, uint8_t* image, size_t image_size) {
    for (uint16_t pixel_height = 0; pixel_height < height; pixel_height++) {
        for (uint16_t pixel_width = 0; pixel_width < width; pixel_width++) {

            size_t byte_index = (pixel_width / 4) + (pixel_height * (width / 4));
            uint8_t pixel_value = 0;

// ------------------------------------------------------------------------------------------------------
// Currently uses two bits per pixel (2.15in Hat G)
//
// * 00 -> Black
// * 01 -> White
// * 10 -> Yellow
// * 11 -> Red
// ------------------------------------------------------------------------------------------------------

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

    return waveshare_epaper_send_data_buffer(waveshare_epaper_handle, image, image_size);
}