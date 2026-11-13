// Bench node hardware test (ESP32-S3-DevKitC-1 N16R8), plain Arduino.
//
// Exercises every part fitted to the bench node. Reports on the USB serial console (115200), on the OLED,
// and on a password-protected web page (portal.cpp). No cut logic: the keys never switch the Shelly; only the
// portal's Shelly buttons do. Pins come from the firmware's pin file (firmware/src/pins.h); names are the hardware's signal names.
//
// Commands (USB serial, or the portal's Console tab; one per line). `h` lists them.
#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>

#include "app.h"
#include "console.h"
#include "secrets.h"

ConsoleTee Con;
App app;

// ---- Pins (from the pin file) -----------------------------------------------------------------------------
#include "../../firmware/src/pins.h"
// BAR_LIVE (low = bar live). The pin file says GPIO18; this test build can move it to GPIO8 from the portal,
// to help find a wiring fault on the bar sense. The choice is kept in NVS ("hw"/"bar_gpio").
constexpr int BAR_LIVE_PINS[] = {BAR_LIVE, 8};
static int bar_gpio = BAR_LIVE;              // the GPIO read as BAR_LIVE now

constexpr int KEY_PINS[3] = {KEY_ON, KEY_OVERRIDE, KEY_FN};
constexpr int LED_PINS[3] = {LED_ON, LED_OVERRIDE, LED_FN};
const char *const KEY_NAMES[3] = {"KEY_ON", "KEY_OVERRIDE", "KEY_FN"};

// LEDC channels: LEDs on 0-2, buzzer on 6 so it has its own timer (channel pairs share a timer).
constexpr int LED_CH[3] = {0, 1, 2};
constexpr int BUZ_CH = 6;

static const char HELP[] =
    "Commands: h help | i I2C scan | p probe BAR_LIVE | P<0-2> BAR_LIVE pull (none, up, down) | G<18|8> BAR_LIVE GPIO | b beep | s sweep 1500-5000 Hz | f<hz> | v<0-50 %>\n"
    "  l<0-4> LED mode (follow keys, all on, breathe, chase, manual) | L<led 0-2> <0-255> manual level\n"
    "  o<0-2> OLED (dashboard, test, blank) | c<0-255> OLED contrast\n"
    "  rv radar version | re<0/1> engineering | rp read params | rr restart radar\n"
    "  rg<gate|all> <move> <still> gate sensitivity | rm<move gate> <still gate> <hold s> max gates\n"
    "  S shelly status | S1 / S0 shelly on/off | St<s> shelly on, off again after s seconds | R reboot";

// ---- OLED: minimal SSD1306 128x64 driver with a 5x7 font --------------------------------------------------
static uint8_t fb[128 * 8];

static const uint8_t FONT[95][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x01,0x01},{0x3E,0x41,0x41,0x51,0x32},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x04,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x7F,0x20,0x18,0x20,0x7F},
  {0x63,0x14,0x08,0x14,0x63},{0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
  {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
  {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x08,0x14,0x54,0x54,0x3C},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x00,0x7F,0x10,0x28,0x44},
  {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
  {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
  {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
  {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x08,0x08,0x2A,0x1C,0x08},
};

static void oled_cmd(std::initializer_list<uint8_t> bytes) {
  Wire.beginTransmission(app.oled_addr);
  Wire.write(0x00);
  for (uint8_t b : bytes) Wire.write(b);
  Wire.endTransmission();
}

static void oled_init() {
  oled_cmd({0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00});
  oled_cmd({0xA1, 0xC8, 0xDA, 0x12, 0x81, app.oled_contrast, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF});
}

static void oled_flush() {
  if (!app.oled_addr) return;
  oled_cmd({0x21, 0x00, 0x7F, 0x22, 0x00, 0x07});
  for (size_t i = 0; i < sizeof(fb); i += 16) {
    Wire.beginTransmission(app.oled_addr);
    Wire.write(0x40);
    Wire.write(fb + i, 16);
    Wire.endTransmission();
  }
}

// Text line `row` (0-7), 21 characters of 6 px.
static void oled_text(int row, const char *s) {
  uint8_t *p = fb + row * 128;
  memset(p, 0, 128);
  for (int col = 0; *s && col < 21; s++, col++) {
    char c = (*s < 32 || *s > 126) ? '?' : *s;
    memcpy(p + col * 6, FONT[c - 32], 5);
  }
}

static void oled_printf(int row, const char *fmt, ...) {
  char buf[32];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  oled_text(row, buf);
}

static void i2c_scan() {
  app.i2c_found[0] = 0;
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      size_t n = strlen(app.i2c_found);
      snprintf(app.i2c_found + n, sizeof(app.i2c_found) - n, "%s0x%02X", found ? " " : "", a);
      found++;
      if ((a == 0x3C || a == 0x3D) && !app.oled_addr) app.oled_addr = a;
    }
  }
  if (!found) strcpy(app.i2c_found, "nothing");
  Con.printf("I2C scan: %s\n", app.i2c_found);
}

// ---- Buzzer ---------------------------------------------------------------------------------------------
static uint32_t beep_until = 0, sweep_next = 0;

static void buzzer(uint32_t hz) {
  if (hz == 0) { ledcWrite(BUZ_CH, 0); return; }
  ledcChangeFrequency(BUZ_CH, hz, 8);
  ledcWrite(BUZ_CH, app.buz_vol * 255 / 100);
}

static void beep(uint32_t ms) { beep_until = millis() + ms; buzzer(app.buz_hz); }

// ---- Radar (HLK-LD2410C) --------------------------------------------------------------------------------
// Frame formats as in the LD2410C serial protocol; the bench node firmware's radar.cpp checked them against this
// sensor. The console shows every ACK.
static RadarState &radar = app.radar;
static uint8_t rx[96];
static size_t rx_len = 0;

static void radar_send(uint16_t cmd, const uint8_t *val = nullptr, uint16_t val_len = 0) {
  const uint8_t head[4] = {0xFD, 0xFC, 0xFB, 0xFA}, tail[4] = {0x04, 0x03, 0x02, 0x01};
  uint16_t len = 2 + val_len;
  Serial1.write(head, 4);
  Serial1.write((uint8_t) len); Serial1.write((uint8_t) (len >> 8));
  Serial1.write((uint8_t) cmd); Serial1.write((uint8_t) (cmd >> 8));
  if (val_len) Serial1.write(val, val_len);
  Serial1.write(tail, 4);
  delay(60);
}

static void put_word_dword(uint8_t *p, uint16_t word, uint32_t dword) {
  p[0] = word; p[1] = word >> 8;
  p[2] = dword; p[3] = dword >> 8; p[4] = dword >> 16; p[5] = dword >> 24;
}

// Enter configuration mode, send one command, leave configuration mode.
static void radar_config(uint16_t cmd, const uint8_t *val = nullptr, uint16_t val_len = 0) {
  const uint8_t enable[2] = {0x01, 0x00};
  radar_send(0x00FF, enable, 2);
  radar_send(cmd, val, val_len);
  if (cmd != 0x00A3) radar_send(0x00FE);   // a restart leaves configuration mode by itself
}

static void radar_report(const uint8_t *d, uint16_t len) {
  // d[0] type (0x01 engineering, 0x02 basic), d[1] 0xAA, then the target block.
  if (len < 11 || d[1] != 0xAA) { radar.bad++; return; }
  radar.engineering = d[0] == 0x01;
  radar.state = d[2];
  radar.move_cm = d[3] | d[4] << 8;
  radar.move_en = d[5];
  radar.still_cm = d[6] | d[7] << 8;
  radar.still_en = d[8];
  radar.detect_cm = d[9] | d[10] << 8;
  if (radar.engineering && len >= 13) {
    uint8_t n = d[11];                  // highest moving gate
    radar.gates = n > 8 ? 8 : n;
    for (int g = 0; g <= radar.gates && 13 + g < len; g++) radar.gate_move[g] = d[13 + g];
    for (int g = 0; g <= radar.gates && 13 + n + 1 + g < len; g++) radar.gate_still[g] = d[13 + n + 1 + g];
  }
  radar.frames++;
  radar.last_frame = millis();
}

static void radar_ack(const uint8_t *d, uint16_t len) {
  if (len < 4) return;
  uint16_t cmd = d[0] | d[1] << 8;
  uint16_t status = d[2] | d[3] << 8;
  radar.acks++;
  radar.last_ack_cmd = cmd;
  radar.last_ack_status = status;
  if (cmd == 0x01A0 && len >= 12) {
    snprintf(radar.version, sizeof(radar.version), "V%u.%02u.%02X%02X%02X%02X", d[7], d[6], d[11], d[10], d[9], d[8]);
    Con.printf("Radar firmware %s\n", radar.version);
  } else if (cmd == 0x0161 && len >= 6 && d[4] == 0xAA) {
    uint8_t n = d[5] > 8 ? 8 : d[5];
    if (len >= (uint16_t) (12 + 2 * n)) {
      radar.max_move_gate = d[6];
      radar.max_still_gate = d[7];
      for (int g = 0; g <= n; g++) { radar.sens_move[g] = d[8 + g]; radar.sens_still[g] = d[9 + n + g]; }
      radar.hold_s = d[10 + 2 * n] | d[11 + 2 * n] << 8;
      radar.params_ok = true;
      Con.printf("Radar params: max gates move %u still %u, hold %u s\n", radar.max_move_gate, radar.max_still_gate,
                 radar.hold_s);
    }
  } else if (cmd != 0x01FF && cmd != 0x01FE) {
    Con.printf("Radar ACK 0x%04X status %u%s\n", cmd, status, status ? " (FAILED)" : "");
  }
}

// Frame parser: report frames F4 F3 F2 F1 ... F8 F7 F6 F5, command ACKs FD FC FB FA ... 04 03 02 01.
static void radar_poll() {
  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    if (rx_len < 4) {
      const bool rep = (rx_len == 0 && b == 0xF4) || (rx_len > 0 && rx[0] == 0xF4 && b == 0xF4 - rx_len);
      const bool ack = (rx_len == 0 && b == 0xFD) || (rx_len > 0 && rx[0] == 0xFD && b == 0xFD - rx_len);
      if (rep || ack) rx[rx_len++] = b; else rx_len = (b == 0xF4 || b == 0xFD) ? (rx[0] = b, 1) : 0;
      continue;
    }
    rx[rx_len++] = b;
    if (rx_len >= 6) {
      uint16_t len = rx[4] | rx[5] << 8;
      if (len > sizeof(rx) - 10) { radar.bad++; rx_len = 0; continue; }
      if (rx_len == (size_t) len + 10) {
        if (rx[0] == 0xF4) radar_report(rx + 6, len); else radar_ack(rx + 6, len);
        rx_len = 0;
      }
    }
  }
}

// ---- Keys, LEDs, inputs ---------------------------------------------------------------------------------
static uint32_t key_since[3] = {}, key_change[3] = {};

static void poll_keys() {
  uint32_t now = millis();
  for (int k = 0; k < 3; k++) {
    bool down = digitalRead(KEY_PINS[k]) == LOW;  // key to GND, internal pull-up (assumed wiring)
    if (down != app.key_down[k] && now - key_change[k] >= 20) {   // 20 ms debounce
      key_change[k] = now;
      app.key_down[k] = down;
      if (down) {
        key_since[k] = now;
        app.key_presses[k]++;
        beep(15);
        Con.printf("%8u  %s down\n", now, KEY_NAMES[k]);
      } else {
        uint32_t held = now - key_since[k];
        Con.printf("%8u  %s up after %u ms (%s)\n", now, KEY_NAMES[k], held, held < 400 ? "short" : "long");
      }
    }
  }
  bool live = digitalRead(bar_gpio) == LOW;
  if (live != app.bar_live) { app.bar_live = live; Con.printf("%8u  BAR_LIVE -> %s\n", now, live ? "LIVE (low)" : "dead (high)"); }
  bool out = digitalRead(RADAR_OUT) == HIGH;
  if (out != app.out_pin) { app.out_pin = out; Con.printf("%8u  RADAR_OUT -> %s\n", now, out ? "high" : "low"); }
}

// BAR_LIVE's internal pull. 1 (pull-up) is normal: an unplugged cable then reads "dead".
// 0 (none) lets a meter read only what the cable delivers.
static void set_bar_pull(int mode) {
  app.bar_pull = mode;
  pinMode(bar_gpio, mode == 0 ? INPUT : mode == 2 ? INPUT_PULLDOWN : INPUT_PULLUP);
}

// Moves BAR_LIVE to another GPIO. The old pin is left as a plain input (no pull, drives nothing).
static bool set_bar_gpio(int gpio, bool save) {
  bool allowed = false;
  for (int p : BAR_LIVE_PINS) allowed |= p == gpio;
  if (!allowed) return false;
  if (gpio != bar_gpio) pinMode(bar_gpio, INPUT);
  bar_gpio = gpio;
  set_bar_pull(app.bar_pull);
  app.bar_gpio = gpio;
  if (save) { Preferences p; p.begin("hw", false); p.putInt("bar_gpio", gpio); p.end(); }
  return true;
}

// Reads BAR_LIVE with the internal pull-down, then the pull-up. Floating = follows the pull; driven = does not.
static void probe_bar_live() {
  pinMode(bar_gpio, INPUT_PULLDOWN);
  delay(5);
  app.probe_pulldown = digitalRead(bar_gpio);
  pinMode(bar_gpio, INPUT_PULLUP);
  delay(5);
  app.probe_pullup = digitalRead(bar_gpio);
  set_bar_pull(app.bar_pull);           // back to the chosen pull
  const char *verdict = app.probe_pullup == 0 ? "something drives it LOW (the PC817, or a fault)"
                        : app.probe_pulldown == 1 ? "something drives it HIGH"
                                                  : "floating: nothing connected";
  Con.printf("BAR_LIVE (GPIO%d) probe: with pull-down %d, with pull-up %d -> %s\n", bar_gpio, app.probe_pulldown, app.probe_pullup, verdict);
}

static void drive_leds() {
  uint32_t now = millis();
  uint32_t t = now % 3000;
  float x = t < 1500 ? t / 1500.0f : (3000 - t) / 1500.0f;
  uint8_t breathe = (uint8_t) (255 * (0.05f + 0.95f * x * x));
  for (int k = 0; k < 3; k++) {
    uint8_t v = 0;
    switch (app.led_mode) {
      case 0: v = app.key_down[k] ? 255 : 0; break;
      case 1: v = 255; break;
      case 2: v = breathe; break;
      case 3: v = ((now / 300) % 3) == (uint32_t) k ? 255 : 0; break;
      case 4: v = app.led_manual[k]; break;
    }
    if (v != app.led_level[k]) { app.led_level[k] = v; ledcWrite(LED_CH[k], v); }
  }
}

// ---- The command dispatcher (USB serial and the portal) -------------------------------------------------
static int arg_int(const String &s, int from, int fallback) {
  String t = s.substring(from);
  t.trim();
  return t.length() ? t.toInt() : fallback;
}

bool command(const String &raw, String &reply) {
  String c = raw;
  c.trim();
  reply = "";
  if (c.isEmpty()) return false;
  char k = c[0];
  if (k == 'h' || k == '?') { reply = HELP; }
  else if (k == 'i') { i2c_scan(); reply = String("I2C: ") + app.i2c_found; }
  else if (k == 'p') { probe_bar_live(); reply = "BAR_LIVE probed"; }
  else if (k == 'G') {
    int g = arg_int(c, 1, -1);
    if (!set_bar_gpio(g, true)) { reply = "BAR_LIVE can be on GPIO18 or GPIO8 only"; return false; }
    probe_bar_live();
    reply = "BAR_LIVE now on GPIO" + String(g) + " (saved)";
  }
  else if (k == 'P') {
    set_bar_pull(constrain(arg_int(c, 1, 1), 0, 2));
    static const char *const PULLS[3] = {"none (pin floats unless the cable drives it)", "pull-up (normal)", "pull-down"};
    reply = String("BAR_LIVE internal pull: ") + PULLS[app.bar_pull];
  }
  else if (k == 'b') { beep(300); reply = "beep"; }
  else if (k == 's') { app.sweeping = true; app.sweep_hz = 1500; sweep_next = 0; reply = "sweep 1500-5000 Hz"; }
  else if (k == 'f') { app.buz_hz = constrain(arg_int(c, 1, app.buz_hz), 200, 10000); beep(300); reply = "buzzer " + String(app.buz_hz) + " Hz"; }
  else if (k == 'v') { app.buz_vol = constrain(arg_int(c, 1, app.buz_vol), 0, 50); beep(300); reply = "volume " + String(app.buz_vol) + " %"; }
  else if (k == 'l') { app.led_mode = constrain(arg_int(c, 1, (app.led_mode + 1) % 5), 0, 4); reply = "LED mode " + String(app.led_mode); }
  else if (k == 'L') {
    int led = arg_int(c, 1, -1), sp = c.indexOf(' ');
    if (led < 0 || led > 2 || sp < 0) { reply = "usage: L<0-2> <0-255>"; return false; }
    app.led_manual[led] = constrain(arg_int(c, sp, 0), 0, 255);
    app.led_mode = 4;
    reply = "LED " + String(led) + " = " + String(app.led_manual[led]);
  }
  else if (k == 'o') { app.oled_mode = constrain(arg_int(c, 1, 0), 0, 2); reply = "OLED mode " + String(app.oled_mode); }
  else if (k == 'c') {
    app.oled_contrast = constrain(arg_int(c, 1, 0xCF), 0, 255);
    if (app.oled_addr) oled_cmd({0x81, app.oled_contrast});
    reply = "OLED contrast " + String(app.oled_contrast);
  }
  else if (k == 'r' && c.length() >= 2) {
    char r = c[1];
    if (r == 'v') { radar_config(0x00A0); reply = "radar: version asked"; }
    else if (r == 'e') { radar_config(arg_int(c, 2, 1) ? 0x0062 : 0x0063); reply = "radar: engineering mode asked"; }
    else if (r == 'p') { radar_config(0x0061); reply = "radar: parameters asked"; }
    else if (r == 'r') { radar_config(0x00A3); reply = "radar: restart asked"; }
    else if (r == 'g') {
      // rg<gate|all> <move> <still>
      String rest = c.substring(2);
      rest.trim();
      int s1 = rest.indexOf(' '), s2 = rest.lastIndexOf(' ');
      if (s1 < 0 || s2 == s1) { reply = "usage: rg<gate 0-8|all> <move 0-100> <still 0-100>"; return false; }
      String g = rest.substring(0, s1);
      uint32_t gate = g == "all" ? 0xFFFF : (uint32_t) constrain(g.toInt(), 0, 8);
      uint8_t v[18];
      put_word_dword(v, 0x0000, gate);
      put_word_dword(v + 6, 0x0001, constrain(rest.substring(s1 + 1, s2).toInt(), 0, 100));
      put_word_dword(v + 12, 0x0002, constrain(rest.substring(s2 + 1).toInt(), 0, 100));
      radar_config(0x0064, v, sizeof(v));
      radar_config(0x0061);
      reply = "radar: gate sensitivity sent";
    }
    else if (r == 'm') {
      // rm<max move gate> <max still gate> <hold s>
      String rest = c.substring(2);
      rest.trim();
      int s1 = rest.indexOf(' '), s2 = rest.lastIndexOf(' ');
      if (s1 < 0 || s2 == s1) { reply = "usage: rm<move gate 2-8> <still gate 2-8> <hold s>"; return false; }
      uint8_t v[18];
      put_word_dword(v, 0x0000, constrain(rest.substring(0, s1).toInt(), 2, 8));
      put_word_dword(v + 6, 0x0001, constrain(rest.substring(s1 + 1, s2).toInt(), 2, 8));
      put_word_dword(v + 12, 0x0002, constrain(rest.substring(s2 + 1).toInt(), 0, 65535));
      radar_config(0x0060, v, sizeof(v));
      radar_config(0x0061);
      reply = "radar: max gates and hold sent";
    }
    else { reply = "unknown radar command"; return false; }
  }
  else if (k == 'S') {
    if (c == "S") { shelly_request("/rpc/Switch.GetStatus?id=0"); reply = "shelly: status asked"; }
    else if (c == "S1") { shelly_request("/rpc/Switch.Set?id=0&on=true"); reply = "shelly: ON sent"; }
    else if (c == "S0") { shelly_request("/rpc/Switch.Set?id=0&on=false"); reply = "shelly: OFF sent"; }
    else if (c.startsWith("St")) {
      int s = constrain(arg_int(c, 2, 60), 5, 3600);
      char path[64];
      snprintf(path, sizeof(path), "/rpc/Switch.Set?id=0&on=true&toggle_after=%d", s);
      shelly_request(path);
      reply = "shelly: ON, auto-OFF in " + String(s) + " s";
    }
    else { reply = "unknown shelly command"; return false; }
  }
  else if (k == 'R') { Con.println("Rebooting in 300 ms"); delay(300); ESP.restart(); }
  else { reply = "unknown command '" + c + "' - h for help"; return false; }
  return true;
}

// ---- Setup and loop -------------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);

  for (int k = 0; k < 3; k++) {
    pinMode(KEY_PINS[k], INPUT_PULLUP);
    ledcSetup(LED_CH[k], 5000, 8);
    ledcAttachPin(LED_PINS[k], LED_CH[k]);
  }
  {
    Preferences p;
    p.begin("hw", true);
    int g = p.getInt("bar_gpio", BAR_LIVE);
    p.end();
    if (!set_bar_gpio(g, false)) set_bar_gpio(BAR_LIVE, false);   // pull-up: an unplugged cable reads "dead"
  }
  Con.printf("BAR_LIVE on GPIO%d\n", bar_gpio);
  pinMode(RADAR_OUT, INPUT_PULLDOWN);   // no sensor reads low
  ledcSetup(BUZ_CH, app.buz_hz, 8);
  ledcAttachPin(BUZZER, BUZ_CH);
  ledcWrite(BUZ_CH, 0);

  uint64_t mac = ESP.getEfuseMac();
  Con.println();
  Con.println("=== Bench node hardware test ===");
  Con.printf("Built %s %s\n", __DATE__, __TIME__);
  Con.printf("Chip %s rev %d, %d cores, %u MHz, reset reason %d\n", ESP.getChipModel(), ESP.getChipRevision(),
             ESP.getChipCores(), ESP.getCpuFreqMHz(), (int) esp_reset_reason());
  Con.printf("Base MAC %02X:%02X:%02X:%02X:%02X:%02X\n", (uint8_t) mac, (uint8_t) (mac >> 8), (uint8_t) (mac >> 16),
             (uint8_t) (mac >> 24), (uint8_t) (mac >> 32), (uint8_t) (mac >> 40));
  Con.printf("Flash %u MB\n", ESP.getFlashChipSize() / (1024 * 1024));

  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  i2c_scan();
  if (app.oled_addr) { oled_init(); Con.printf("OLED at 0x%02X\n", app.oled_addr); }
  else Con.println("OLED not found (expected at 0x3C)");

  Serial1.setRxBufferSize(1024);
  Serial1.begin(256000, SERIAL_8N1, RADAR_to_S3, S3_to_RADAR);
  radar_config(0x00A0);                 // its firmware version: proves both UART directions
  radar_config(0x0061);                 // its parameters

  probe_bar_live();

  // Start-up check: each LED in turn, one beep.
  for (int k = 0; k < 3; k++) { ledcWrite(LED_CH[k], 255); delay(250); ledcWrite(LED_CH[k], 0); }
  beep(200);

  shelly_begin(SHELLY_HOST);
  portal_begin();
  Con.println(HELP);
}

void loop() {
  static String line;
  static uint32_t next_status = 0, next_oled = 0, frames_at = 0, frames_prev = 0;
  uint32_t now = millis();

  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (line.length()) { String reply; command(line, reply); if (reply.length()) Con.println(reply); }
      line = "";
    } else if (line.length() < 40) line += ch;
  }

  radar_poll();
  poll_keys();
  drive_leds();
  portal_loop();

  if (app.sweeping && (int32_t) (now - sweep_next) >= 0) {
    if (app.sweep_hz > 5000) { app.sweeping = false; buzzer(0); Con.println("Sweep done"); }
    else { buzzer(app.sweep_hz); Con.printf("  sweep %u Hz\n", app.sweep_hz); app.sweep_hz += 100; sweep_next = now + 400; }
  } else if (!app.sweeping && beep_until && (int32_t) (now - beep_until) >= 0) {
    beep_until = 0;
    buzzer(0);
  }

  if (now - frames_at >= 1000) { radar.fps = radar.frames - frames_prev; frames_prev = radar.frames; frames_at = now; }

  if ((int32_t) (now - next_status) >= 0) {
    next_status = now + 5000;
    Con.printf("%8u  keys %d%d%d  BAR_LIVE %s  OUT %d  radar %u fps, bad %u, state %u, move %u cm/%u, still %u cm/%u\n",
               now, app.key_down[0], app.key_down[1], app.key_down[2], app.bar_live ? "live" : "dead", app.out_pin,
               radar.fps, radar.bad, radar.state, radar.move_cm, radar.move_en, radar.still_cm, radar.still_en);
  }

  if (app.oled_addr && (int32_t) (now - next_oled) >= 0) {
    next_oled = now + 200;
    static const char *const STATES[4] = {"none", "moving", "still", "both"};
    if (app.oled_mode == 2) {
      memset(fb, 0, sizeof(fb));
    } else if (app.oled_mode == 1) {
      for (int x = 0; x < 128; x++) for (int p = 0; p < 8; p++) fb[p * 128 + x] = ((x / 8 + p) % 2) ? 0xFF : 0x00;
    } else {
      oled_printf(0, "%s", portal_address());
      oled_printf(1, "ON %s  OVR %s  FN %s", app.key_down[0] ? "X" : "-", app.key_down[1] ? "X" : "-", app.key_down[2] ? "X" : "-");
      oled_printf(2, "BAR_LIVE %s", app.bar_live ? "LIVE" : "dead");
      oled_printf(3, "Radar %u fps %s", radar.fps, radar.fps ? "" : "NO DATA");
      oled_printf(4, "Target %s OUT %d", STATES[radar.state & 3], app.out_pin);
      oled_printf(5, "Move  %3u cm  E%3u", radar.move_cm, radar.move_en);
      oled_printf(6, "Still %3u cm  E%3u", radar.still_cm, radar.still_en);
      oled_printf(7, "Buz %u Hz  LED %d", app.buz_hz, app.led_mode);
    }
    oled_flush();
  }
}
