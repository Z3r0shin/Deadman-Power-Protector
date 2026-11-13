// HLK-LD2410C presence sensor on UART1 (256000 baud): frame parser, configuration commands, calibration.
// Called from the loop() task only.
#pragma once
#include <Arduino.h>

struct RadarState {
  uint8_t state = 0;                    // 0 none, 1 moving, 2 still, 3 both
  uint16_t move_cm = 0, still_cm = 0, detect_cm = 0;
  uint8_t move_en = 0, still_en = 0;
  bool engineering = false;
  uint8_t gate_move[9] = {}, gate_still[9] = {};
  uint32_t frames = 0, bad = 0, last_frame = 0, fps = 0;
  char version[24] = "";
  bool params_ok = false;               // parameters read back from the sensor
  uint8_t max_move_gate = 0, max_still_gate = 0, sens_move[9] = {}, sens_still[9] = {};
  uint16_t hold_s = 0;
  uint32_t acks = 0, failed_acks = 0;
};

struct RadarCal {
  enum Stage : uint8_t { Idle, Wait, Sample, Done } stage = Idle;
  uint32_t until = 0;                   // millis() when this stage ends
  uint8_t max_move[9] = {}, max_still[9] = {};
  uint32_t samples = 0;
};

extern RadarState radar;
extern RadarCal radar_cal;

void radar_begin(int rx_pin, int tx_pin);
void radar_loop();                      // parse frames; run calibration; fps
bool radar_alive(uint32_t dead_ms);     // a frame arrived within dead_ms
bool radar_present();                   // a target above the minimum energies for radar_min_ms, from a fresh frame (settings)
void radar_ask_version();
void radar_read_params();
void radar_restart();
void radar_engineering(bool on);
void radar_apply_settings();            // write max gates, hold and every gate threshold from the settings table
void radar_cal_start(uint32_t wait_s);  // wait (leave the room), then sample 60 s, then set thresholds and apply
void radar_cal_abort();
bool radar_command(const String &c, String &reply);   // console: rg/rm/re/rv/rp/rr
void radar_set_idle(void (*fn)());      // called every few ms while a command waits on the sensor
