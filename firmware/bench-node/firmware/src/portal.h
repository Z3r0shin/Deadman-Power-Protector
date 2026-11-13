#pragma once
#include <Arduino.h>

// portal.cpp
void portal_begin();
void portal_loop();
const char *portal_mode();              // "house WiFi", "own access point", "joining"
const char *portal_address();           // what to type in the browser

// main.cpp, for the portal
void build_state(String &o);                                  // appends the live state fields (the Firmware Gospel)
bool do_action(const String &a, const String &v, String &reply);
bool command(const String &line, String &reply);              // the console dispatcher, USB and web
void settings_changed(const char *key);
const char *firmware_version();
bool firmware_on_trial();                                     // a new firmware not confirmed yet: no new upload
