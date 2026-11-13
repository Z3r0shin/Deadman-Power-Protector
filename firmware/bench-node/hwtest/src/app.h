// What main.cpp (the hardware) shares with portal.cpp (WiFi and the web page) and shelly.cpp.
// Everything here is touched only from the Arduino loop() task, except `shelly`, guarded in shelly.cpp.
#pragma once
#include <Arduino.h>

struct RadarState {
  uint8_t state = 0;                    // 0 none, 1 moving, 2 still, 3 both
  uint16_t move_cm = 0, still_cm = 0, detect_cm = 0;
  uint8_t move_en = 0, still_en = 0;
  bool engineering = false;
  uint8_t gates = 0, gate_move[9] = {}, gate_still[9] = {};
  uint32_t frames = 0, bad = 0, last_frame = 0, fps = 0;
  char version[24] = "";                // empty until the sensor answers
  // Parameters read back from the sensor (command 0x0061).
  bool params_ok = false;
  uint8_t max_move_gate = 0, max_still_gate = 0;
  uint8_t sens_move[9] = {}, sens_still[9] = {};
  uint16_t hold_s = 0;
  uint32_t acks = 0;
  uint16_t last_ack_cmd = 0, last_ack_status = 0;
};

struct ShellyState {
  bool reachable = false;
  uint32_t polled_at = 0;               // millis() of the last answer
  bool output = false;
  float apower = 0, voltage = 0, current = 0, temp_c = 0, freq = 0;
  char error[48] = "not polled yet";
  char last_reply[160] = "";
};

struct App {
  bool key_down[3] = {};
  uint32_t key_presses[3] = {};
  bool bar_live = false, out_pin = false;
  int probe_pullup = -1, probe_pulldown = -1;   // BAR_LIVE probe, -1 = not run
  int bar_gpio = 18;                    // which GPIO BAR_LIVE is read on
  int bar_pull = 1;                     // BAR_LIVE internal pull: 0 none, 1 up (normal), 2 down
  int led_mode = 0;                     // 0 follow keys, 1 all on, 2 breathe all, 3 chase, 4 manual
  uint8_t led_manual[3] = {0, 0, 0};
  uint8_t led_level[3] = {};            // what is being driven now
  uint32_t buz_hz = 2200;               // the buzzer's chosen frequency
  uint8_t buz_vol = 50;
  bool sweeping = false;
  uint32_t sweep_hz = 0;
  uint8_t oled_addr = 0;
  int oled_mode = 0;                    // 0 dashboard, 1 test pattern, 2 blank
  uint8_t oled_contrast = 0xCF;
  char i2c_found[64] = "";
  RadarState radar;
};

extern App app;
extern ShellyState shelly;

// main.cpp
bool command(const String &line, String &reply);   // the one dispatcher, for USB and the portal

// portal.cpp
void portal_begin();
void portal_loop();
const char *portal_mode();              // "house WiFi", "own access point", ...
const char *portal_address();           // what to type in the browser, for the OLED

// shelly.cpp
void shelly_begin(const char *host);
void shelly_request(const char *rpc_path);          // queued; done by the Shelly task
void shelly_snapshot(ShellyState &out);
