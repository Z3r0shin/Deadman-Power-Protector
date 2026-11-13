// Every OLED screen, drawn from one plain snapshot of the device (ScreenModel). No Arduino dependency: the PC renderer
// (../tools/render.cpp) draws the same screens to images.
#pragma once
#include <cstdint>

namespace screens {

enum class View : uint8_t { Dark, Ambient, Boot, Status, Timers, Radar, Menu, HoldToCut, Warning, Alarm, Toast };
enum class Phase : uint8_t { Off, On, Hold, Warning, Alarm, Unknown };
enum class Why : uint8_t { None, Absence, Hold, Ceiling, Link };

// One small item on the status page (big number or a footer slot).
enum Item : uint8_t { I_NONE, I_CUT, I_LIMIT, I_HOLD, I_LEASE, I_POWER, I_PRESENCE, I_CLOCK, I_ADDRESS, I_TEMP };

struct Model {
  View view = View::Dark;
  uint32_t now_ms = 0;               // drives the animations
  Phase phase = Phase::Off;
  // Countdowns, seconds; total = the full length, for progress bars.
  int32_t cut_s = -1;                // absence (or hold) cut due in; -1 = not counting
  uint32_t cut_total_s = 1200;
  uint32_t hold_s = 0, hold_total_s = 0;
  uint32_t limit_s = 0, limit_total_s = 14400;
  uint32_t lease_s = 0, lease_total_s = 300;
  uint32_t warn_s = 0, warn_total_s = 300;
  Why why = Why::None;
  bool present = false;
  uint32_t seen_ago_s = 0;
  uint16_t dist_cm = 0;              // radar target distance
  uint8_t move_en = 0, still_en = 0;
  uint8_t max_gate = 3;
  bool radar_ok = true;
  bool link_ok = true, wifi_ok = true;
  bool quiet = false;
  bool clock_ok = false;
  uint8_t hh = 0, mm = 0;
  float watts = -1, temp_c = -1;     // negative = unknown
  const char *address = "";
  const char *fault = "";            // short code, "" when none
  bool alarm_silenced = false;
  bool alarm_stuck = true;           // stuck_on (else lost_live)
  // Menu.
  uint32_t menu_min = 30;
  bool menu_hours = false;
  bool hold_active = false;
  float hold_to_cut = 0;             // 0..1
  const char *toast = "";
  bool toast_good = true;
  // Status page layout (settings scr_l1 big number, scr_l2 / scr_l3 footer).
  uint8_t big = I_CUT, foot_l = I_LIMIT, foot_r = I_POWER;
  const char *version = "";
  const char *commit = "";
  // Idle view: false = the radar widget, true = the pig (pig.cpp).
  bool pig = false;
  uint32_t pig_scene_ms = 20000;     // the pig moves on to its next scene after this long
};

void draw(const Model &m);           // clears the buffer and draws the whole screen
bool animated(const Model &m);       // true when the view moves: the caller refreshes it faster

}  // namespace screens
