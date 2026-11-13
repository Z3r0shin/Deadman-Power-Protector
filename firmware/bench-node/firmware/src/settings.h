// Every adjustable value, described once. The web page draws itself from this table (/api/schema), /api/set clamps
// against it, and the values live in NVS (namespace "cfg"). Adding a setting is one row here.
#pragma once
#include <Arduino.h>

enum SetKind : uint8_t { K_NUM = 0, K_BOOL = 1, K_TEXT = 2, K_SELECT = 3, K_PASS = 4 };

struct SettingRow {
  const char *key;
  const char *label;
  const char *unit;
  const char *tab;
  SetKind kind;
  int32_t lo, hi, step, def;
  const char *choices;   // K_SELECT: labels separated by '|'
  const char *help;
};

struct SettingAction { const char *tab, *label, *action, *value; };

extern const SettingRow SETTINGS[];
extern const SettingAction SETTING_ACTIONS[];
extern const size_t SETTING_ACTIONS_COUNT;
extern const size_t SETTINGS_COUNT;
extern const char *const SETTING_TABS[];
extern const size_t SETTING_TABS_COUNT;

void settings_begin();                                   // load NVS
const SettingRow *setting_row(const char *key);
int32_t setting_int(const char *key);                    // also declared in ha_iface.h
const char *setting_text(const char *key);
// Stores a value. Returns false (with *why) when refused; *clamped when it was brought into range.
bool setting_set(const char *key, const String &value, String &stored, bool &clamped, String &why);
bool setting_set_int(const char *key, int32_t v);        // from firmware (calibration); clamps; no refusal
uint32_t settings_generation();                          // increments on every change
void settings_reset();                                   // erases every stored setting: defaults after a restart
