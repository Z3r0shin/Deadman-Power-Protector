// SSD1306 128x64 over I2C: start-up, settings and sending gfx's frame buffer. Drawing is in gfx.cpp.
#include "oled.h"

#include <Wire.h>

#include "gfx.h"

static uint8_t addr = 0;

static void cmd(std::initializer_list<uint8_t> bytes) {
  Wire.beginTransmission(addr);
  Wire.write(0x00);
  for (uint8_t b : bytes) Wire.write(b);
  Wire.endTransmission();
}

bool oled_begin(uint8_t contrast) {
  for (uint8_t a : {0x3C, 0x3D}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { addr = a; break; }
  }
  if (!addr) return false;
  cmd({0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00});
  cmd({0xA1, 0xC8, 0xDA, 0x12, 0x81, contrast, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF});
  gfx::clear();
  oled_flush();
  return true;
}

uint8_t oled_address() { return addr; }
void oled_contrast(uint8_t c) { if (addr) cmd({0x81, c}); }
// Contrast alone barely dims the panel; a short pre-charge and the lowest VCOMH do (about a tenth of the
// brightness: github.com/thePunderWoman/SnipsControllers/pull/79, github.com/arendst/Tasmota/discussions/22999).
// Not dimmed: the values oled_begin sets.
void oled_dim(bool dim) { if (addr) cmd({0xD9, (uint8_t) (dim ? 0x11 : 0xF1), 0xDB, (uint8_t) (dim ? 0x00 : 0x40)}); }
void oled_flip(bool f) { if (addr) cmd({(uint8_t) (f ? 0xA0 : 0xA1), (uint8_t) (f ? 0xC0 : 0xC8)}); }
void oled_invert(bool on) { if (addr) cmd({(uint8_t) (on ? 0xA7 : 0xA6)}); }

void oled_flush() {
  if (!addr) return;
  const uint8_t *fb = gfx::buffer();
  cmd({0x21, 0x00, 0x7F, 0x22, 0x00, 0x07});
  for (size_t i = 0; i < gfx::W * gfx::H / 8; i += 16) {
    Wire.beginTransmission(addr);
    Wire.write(0x40);
    Wire.write(fb + i, 16);
    Wire.endTransmission();
  }
}
