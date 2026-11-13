// SSD1306 128x64 over I2C. Draw with gfx.h / screens.h, then oled_flush().
#pragma once
#include <Arduino.h>

bool oled_begin(uint8_t contrast);     // finds the screen at 0x3C/0x3D; false when absent
uint8_t oled_address();                // 0 when absent
void oled_contrast(uint8_t c);
void oled_dim(bool dim);               // pre-charge and VCOMH low too: contrast alone barely dims an SSD1306
void oled_flip(bool upside_down);
void oled_invert(bool on);
void oled_flush();
