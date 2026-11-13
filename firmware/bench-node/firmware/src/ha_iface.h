// Interface between the MQTT / Home Assistant module (mqtt_ha.cpp) and the rest of the firmware.
// Everything here is called from the Arduino loop() task only. The MQTT client's own event task must not call
// these: it only queues incoming commands, which mqtt_loop() hands over.
#pragma once
#include <Arduino.h>

// What Home Assistant is shown. Filled by main.cpp.
struct HaSnapshot {
  const char *phase;        // "off" | "on" | "hold" | "warning" | "alarm"
  int shelly_on;            // 1 on, 0 off, -1 unknown (no answer)
  bool sense_live;          // the bar sense (PC817) says the bar is live
  bool seen;                // presence now
  bool alarm;               // an alarm is active
  const char *alarm_code;   // "" when none
  const char *alarm_text;
  float watts;              // NAN when unknown
  float shelly_temp_c;      // NAN when unknown
  int away_due_s;           // seconds until the absence cut is due, -1 when not counting
  int hold_left_s;          // 0 when no hold
  int ceil_left_s;          // 0 when off
  int warn_left_s;          // 0 when no warning
  const char *camera;       // the camera check: "off" | "radar only" | "grace" | "owner seen" | "veto" | "unsure"
  int camera_owner_ago_s;   // since the owner was last counted in the seat; -1 when not seen since Frigate came up
};

// main.cpp provides these.
void ha_snapshot(HaSnapshot &out);
enum class HaCmd : uint8_t { BarOn, BarOff };
void ha_command(HaCmd cmd);                 // BarOn = remote on (never resets a running ceiling)
const char *setting_text(const char *key);  // "mqtt_host", "mqtt_user", "mqtt_pass"; "" when unset
int32_t setting_int(const char *key);       // "mqtt_on" (0/1), "mqtt_port"
const char *device_mac();                   // "AA:BB:CC:DD:EE:FF"
const char *firmware_version();             // for the HA device card

// mqtt_ha.cpp provides these.
void mqtt_begin();                          // call once after WiFi is started
void mqtt_loop();                           // call every loop() pass; cheap when nothing to do
void mqtt_restart();                        // settings changed: reconnect with the new ones
const char *mqtt_state();                   // "off" | "connecting" | "up" | "error"
const char *mqtt_error();                   // last error text, "" when none

// The camera (Frigate, read over the same MQTT link; frigate.h has the rules).
struct FrigateStatus { bool on, live, veto, unsure; int owner_ago_s, tracks; };
void frigate_apply_settings();              // read the frg_* settings; call after any of them changes
bool frigate_veto(bool quiet);              // call every pass: true = the radar's presence does not count now
void frigate_status(FrigateStatus &out);
