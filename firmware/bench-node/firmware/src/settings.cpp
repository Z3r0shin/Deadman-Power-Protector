#include "settings.h"

#include <Preferences.h>

#include "console.h"

const char *const SETTING_TABS[] = {"Timing", "Sound", "Lights", "Screen", "Radar", "Camera", "Network"};
const size_t SETTING_TABS_COUNT = sizeof(SETTING_TABS) / sizeof(SETTING_TABS[0]);

// clang-format off
const SettingRow SETTINGS[] = {
  // Timing
  {"away_min", "Absence before the cut", "min", "Timing", K_NUM, 6, 240, 1, 20, nullptr,
   "Time since the radar last saw you before the bar is cut. The warning starts before this, and ends at the cut."},
  {"warn_min", "Warning before any automatic cut", "min", "Timing", K_NUM, 1, 10, 1, 5, nullptr,
   "Kept shorter than the absence delay. Any key answers it."},
  {"ceil_min", "Ceiling after the last ON press", "min", "Timing", K_NUM, 30, 240, 5, 240, nullptr,
   "The bar never stays on longer than this after the last ON press (or a key answering a warning). The Shelly enforces at most 4 h whatever is set."},
  {"hold_start_min", "Hold menu starts at", "min", "Timing", K_NUM, 5, 240, 5, 30, nullptr, nullptr},
  {"hold_step_min", "Hold menu step (minutes mode)", "min", "Timing", K_NUM, 1, 30, 1, 5, nullptr, nullptr},
  {"lease_s", "Lease length on the Shelly", "s", "Timing", K_NUM, 60, 600, 10, 300, nullptr,
   "If the bench node stops renewing, the Shelly cuts by itself after this."},
  {"renew_s", "Lease renewal period", "s", "Timing", K_NUM, 10, 120, 5, 30, nullptr,
   "At most a third of the lease length, so two renewals can fail before the Shelly cuts."},
  {"mismatch_s", "Shelly vs bar sense allowance", "s", "Timing", K_NUM, 10, 120, 1, 15, nullptr,
   "How long the bar sense may disagree with the Shelly before it is a fault. Measured lag after a cut: about 7 s."},
  {"hold_to_cut_ms", "ON held to cut", "ms", "Timing", K_NUM, 800, 5000, 100, 1500, nullptr, nullptr},
  {"long_ms", "Long press (OVERRIDE, FN)", "ms", "Timing", K_NUM, 400, 2000, 50, 700, nullptr, nullptr},
  {"menu_idle_s", "Menu closes untouched after", "s", "Timing", K_NUM, 5, 60, 1, 15, nullptr, nullptr},
  // Sound
  {"buz_hz", "Buzzer base frequency", "Hz", "Sound", K_NUM, 1000, 5000, 50, 2200, nullptr,
   "Every warning note is built from this one."},
  {"buz_vol", "Buzzer volume (duty)", "%", "Sound", K_NUM, 0, 50, 1, 50, nullptr, nullptr},
  {"clicks", "Key clicks", "", "Sound", K_BOOL, 0, 1, 1, 1, nullptr, nullptr},
  {"quiet_vol", "Volume in quiet hours (duty)", "%", "Sound", K_NUM, 0, 20, 1, 2, nullptr,
   "Every sound except the alarms plays at this volume during quiet hours."},
  {"quiet_on", "Quiet hours", "", "Sound", K_BOOL, 0, 1, 1, 1, nullptr,
   "During quiet hours, warning sounds at or under 1 minute left are muted and the rest play softly; lights and "
   "screen still warn. "
   "Alarms always sound. Needs the network time; without it, nothing is muted."},
  {"quiet_from_h", "Quiet from", "h", "Sound", K_NUM, 0, 23, 1, 22, nullptr, "Montreal time."},
  {"quiet_to_h", "Quiet until", "h", "Sound", K_NUM, 0, 23, 1, 7, nullptr, nullptr},
  {"click_ms", "Key click length", "ms", "Sound", K_NUM, 2, 60, 1, 12, nullptr, nullptr},
  {"confirm_on", "Confirm note when a press is accepted", "", "Sound", K_BOOL, 0, 1, 1, 1, nullptr, nullptr},
  {"confirm_ms", "Confirm note length", "ms", "Sound", K_NUM, 20, 300, 5, 70, nullptr, nullptr},
  {"cut_on", "Sound at a cut", "", "Sound", K_BOOL, 0, 1, 1, 1, nullptr, "A falling sweep."},
  {"cut_ms", "Cut sound length", "ms", "Sound", K_NUM, 100, 2000, 50, 450, nullptr, nullptr},
  {"m1_on", "Warning start: play", "", "Sound", K_BOOL, 0, 1, 1, 1, nullptr, "Mark 1, when a warning begins."},
  {"m1_notes", "Warning start: notes", "", "Sound", K_NUM, 1, 6, 1, 2, nullptr, "Rising notes."},
  {"m1_note_ms", "Warning start: note length", "ms", "Sound", K_NUM, 30, 500, 10, 130, nullptr, nullptr},
  {"m1_step_pct", "Warning start: step between notes", "%", "Sound", K_NUM, 0, 100, 1, 20, nullptr, nullptr},
  {"m1_repeats", "Warning start: repeats", "", "Sound", K_NUM, 1, 5, 1, 1, nullptr, nullptr},
  {"reminder_s", "Warning start: repeat every", "s", "Sound", K_NUM, 0, 300, 10, 0, nullptr,
   "0 = once. Otherwise mark 1 plays again this often until mark 2."},
  {"m2_on", "Second mark: play", "", "Sound", K_BOOL, 0, 1, 1, 1, nullptr, nullptr},
  {"m2_at_s", "Second mark: before the cut", "s", "Sound", K_NUM, 15, 240, 5, 60, nullptr, nullptr},
  {"m2_notes", "Second mark: notes", "", "Sound", K_NUM, 1, 6, 1, 3, nullptr, nullptr},
  {"m2_note_ms", "Second mark: note length", "ms", "Sound", K_NUM, 30, 500, 10, 110, nullptr, nullptr},
  {"m2_step_pct", "Second mark: step between notes", "%", "Sound", K_NUM, 0, 100, 1, 22, nullptr, nullptr},
  {"m2_repeats", "Second mark: repeats", "", "Sound", K_NUM, 1, 5, 1, 2, nullptr, nullptr},
  {"fin_on", "Final beeps: play", "", "Sound", K_BOOL, 0, 1, 1, 1, nullptr, "One beep a second just before the cut."},
  {"fin_s", "Final beeps: last", "s", "Sound", K_NUM, 3, 60, 1, 15, nullptr, nullptr},
  {"fin_start_ms", "Final beeps: first beep length", "ms", "Sound", K_NUM, 20, 600, 10, 80, nullptr, nullptr},
  {"fin_end_ms", "Final beeps: last beep length", "ms", "Sound", K_NUM, 20, 900, 10, 250, nullptr, nullptr},
  {"fin_rise_pct", "Final beeps: pitch rise", "%", "Sound", K_NUM, 0, 200, 5, 50, nullptr, nullptr},
  {"fin_double_s", "Final beeps: double beeps for the last", "s", "Sound", K_NUM, 0, 15, 1, 5, nullptr, nullptr},
  {"quiet_mute", "Quiet hours: mute from the second mark on", "", "Sound", K_BOOL, 0, 1, 1, 1, nullptr,
   "Off = everything plays in quiet hours, softly."},
  {"alarm_hi_pct", "Alarm: high note", "%", "Sound", K_NUM, 100, 200, 5, 130, nullptr, "Of the base frequency."},
  {"alarm_period_ms", "Alarm: note length", "ms", "Sound", K_NUM, 50, 2000, 10, 250, nullptr,
   "Alarms always play at full volume, even in quiet hours."},
  // Lights
  {"led_bright", "LED brightness", "%", "Lights", K_NUM, 0, 100, 1, 60, nullptr, nullptr},
  {"led_quiet_pct", "LED brightness in quiet hours", "%", "Lights", K_NUM, 0, 100, 1, 100, nullptr,
   "Of the normal brightness."},
  {"led_on_pct", "ON key LED", "%", "Lights", K_NUM, 0, 100, 1, 100, nullptr, "Trim for this LED."},
  {"led_ovr_pct", "OVERRIDE key LED", "%", "Lights", K_NUM, 0, 100, 1, 100, nullptr, nullptr},
  {"led_fn_pct", "FN key LED", "%", "Lights", K_NUM, 0, 100, 1, 100, nullptr, nullptr},
  {"led_live", "Bar live", "", "Lights", K_SELECT, 0, 2, 1, 0, "Steady|Breathe|Dim", nullptr},
  {"led_dead", "Bar off", "", "Lights", K_SELECT, 0, 2, 1, 0, "Off|Dim|Breathe", nullptr},
  {"led_dim_pct", "Dim level", "%", "Lights", K_NUM, 1, 60, 1, 10, nullptr, nullptr},
  {"hold_fx", "OVERRIDE during a hold", "", "Lights", K_SELECT, 0, 2, 1, 0, "Breathe|Blink|Steady", nullptr},
  {"breathe_ms", "Breathe period", "ms", "Lights", K_NUM, 500, 10000, 100, 3000, nullptr, nullptr},
  {"breathe_min_pct", "Breathe low point", "%", "Lights", K_NUM, 0, 90, 1, 15, nullptr, nullptr},
  {"warn_blink_ms", "Warning blink period", "ms", "Lights", K_NUM, 200, 3000, 50, 1000, nullptr,
   "All LEDs blink during a warning."},
  {"warn_fast_ms", "Final beeps blink period", "ms", "Lights", K_NUM, 50, 1000, 10, 400, nullptr, nullptr},
  {"alarm_fx", "Alarm", "", "Lights", K_SELECT, 0, 1, 1, 0, "Chase|All blink", nullptr},
  {"alarm_step_ms", "Alarm step", "ms", "Lights", K_NUM, 50, 1000, 10, 150, nullptr, nullptr},
  {"menu_blink_ms", "ON blink in the menu", "ms", "Lights", K_NUM, 200, 3000, 50, 1000, nullptr, nullptr},
  // Screen
  {"oled_contrast", "Contrast", "", "Screen", K_NUM, 0, 255, 1, 207, nullptr, nullptr},
  {"oled_quiet_c", "Contrast in quiet hours", "", "Screen", K_NUM, 0, 255, 1, 60, nullptr, nullptr},
  {"scr_mode", "Screen", "", "Screen", K_SELECT, 0, 2, 1, 0, "On after a key|Always on|Warnings and menu only",
   "Warnings, alarms and the menu always show. \"On after a key\": the first press on the idle screen only wakes it. "
   "\"Warnings and menu only\" also hides the idle widget and the pig."},
  {"screen_s", "Stays on after a key", "s", "Screen", K_NUM, 5, 300, 1, 20, nullptr, nullptr},
  {"scr_idle", "When idle with the bar on", "", "Screen", K_SELECT, 0, 2, 1, 2, "Dark|Small moving widget|Pig",
   "Both move over the screen against burn-in. The widget shows whether you are seen and the time to the cut; the "
   "pig works at the bench."},
  {"scr_off", "When idle with the bar off", "", "Screen", K_SELECT, 0, 1, 1, 1, "Dark|Pig relaxing",
   "The pig passes the time: coffee, a nap, a bath, reading, music, and more."},
  {"scr_bright_min", "Idle screen full bright for", "min", "Screen", K_NUM, 1, 240, 1, 20, nullptr,
   "After a key, a page shown, or your arrival (seen after this long away). Then dim while you are seen."},
  {"scr_dark_min", "Then dim for, when you are away", "min", "Screen", K_NUM, 1, 240, 1, 20, nullptr,
   "Then dark until you are seen or a key is pressed. Warnings, alarms and the menu always show at full brightness."},
  {"amb_contrast", "Contrast when dimmed", "", "Screen", K_NUM, 0, 255, 1, 40, nullptr,
   "In quiet hours, never above the quiet-hours contrast."},
  {"pig_scene_s", "Pig: next scene after", "s", "Screen", K_NUM, 5, 300, 5, 20, nullptr, nullptr},
  {"scr_l1", "Status page: big number", "", "Screen", K_SELECT, 0, 9, 1, 1,
   "Nothing|Cut in|Limit in|Hold left|Lease|Power|Presence|Clock|Address|Shelly temperature", nullptr},
  {"scr_l2", "Status page: bottom left", "", "Screen", K_SELECT, 0, 9, 1, 2,
   "Nothing|Cut in|Limit in|Hold left|Lease|Power|Presence|Clock|Address|Shelly temperature", nullptr},
  {"scr_l3", "Status page: bottom right", "", "Screen", K_SELECT, 0, 9, 1, 5,
   "Nothing|Cut in|Limit in|Hold left|Lease|Power|Presence|Clock|Address|Shelly temperature", nullptr},
  {"scr_timers", "FN pages: timers", "", "Screen", K_BOOL, 0, 1, 1, 1, nullptr, nullptr},
  {"scr_sensor", "FN pages: radar", "", "Screen", K_BOOL, 0, 1, 1, 1, nullptr, nullptr},
  {"oled_flip", "Upside down", "", "Screen", K_BOOL, 0, 1, 1, 0, nullptr, nullptr},
  {"oled_invert", "Inverted (dark on light)", "", "Screen", K_BOOL, 0, 1, 1, 0, nullptr, nullptr},
  {"scr_shift", "Shift the image a little each minute", "", "Screen", K_BOOL, 0, 1, 1, 1, nullptr,
   "Against burn-in when the screen stays on."},
  // Radar
  {"radar_dead_s", "Radar silent = fault after", "s", "Radar", K_NUM, 2, 30, 1, 5, nullptr,
   "A silent radar counts as absence: the warning and cut proceed."},
  {"radar_margin", "Calibration margin", "", "Radar", K_NUM, 0, 50, 1, 10, nullptr,
   "Threshold = the empty room's highest energy on that gate + this margin."},
  {"radar_mg", "Max moving gate", "gate", "Radar", K_NUM, 2, 8, 1, 4, nullptr, "0.75 m per gate."},
  {"radar_sg", "Max still gate", "gate", "Radar", K_NUM, 2, 8, 1, 4, nullptr, nullptr},
  {"radar_hold", "Radar hold time", "s", "Radar", K_NUM, 0, 60, 1, 5, nullptr, nullptr},
  {"radar_min_mv", "Minimum moving energy to count you", "", "Radar", K_NUM, 0, 100, 1, 40, nullptr,
   "The sensor also reports weak echoes near it as targets; below this they are ignored."},
  {"radar_min_st", "Minimum still energy to count you", "", "Radar", K_NUM, 0, 100, 1, 40, nullptr,
   "Seated at the bench you read about 100; the false targets seen so far read 4 to 6."},
  {"radar_min_ms", "A target must last", "ms", "Radar", K_NUM, 0, 10000, 100, 2000, nullptr,
   "Above those energies without a break, before it counts as you. Kills one-frame echoes."},
  {"radar_m0", "Moving threshold, gate 0", "", "Radar", K_NUM, 0, 100, 1, 50, nullptr, nullptr},
  {"radar_m1", "Moving threshold, gate 1", "", "Radar", K_NUM, 0, 100, 1, 50, nullptr, nullptr},
  {"radar_m2", "Moving threshold, gate 2", "", "Radar", K_NUM, 0, 100, 1, 40, nullptr, nullptr},
  {"radar_m3", "Moving threshold, gate 3", "", "Radar", K_NUM, 0, 100, 1, 30, nullptr, nullptr},
  {"radar_m4", "Moving threshold, gate 4", "", "Radar", K_NUM, 0, 100, 1, 20, nullptr, nullptr},
  {"radar_m5", "Moving threshold, gate 5", "", "Radar", K_NUM, 0, 100, 1, 15, nullptr, nullptr},
  {"radar_m6", "Moving threshold, gate 6", "", "Radar", K_NUM, 0, 100, 1, 15, nullptr, nullptr},
  {"radar_m7", "Moving threshold, gate 7", "", "Radar", K_NUM, 0, 100, 1, 15, nullptr, nullptr},
  {"radar_m8", "Moving threshold, gate 8", "", "Radar", K_NUM, 0, 100, 1, 15, nullptr, nullptr},
  {"radar_s0", "Still threshold, gate 0", "", "Radar", K_NUM, 0, 100, 1, 0, nullptr, nullptr},
  {"radar_s1", "Still threshold, gate 1", "", "Radar", K_NUM, 0, 100, 1, 0, nullptr, nullptr},
  {"radar_s2", "Still threshold, gate 2", "", "Radar", K_NUM, 0, 100, 1, 40, nullptr, nullptr},
  {"radar_s3", "Still threshold, gate 3", "", "Radar", K_NUM, 0, 100, 1, 40, nullptr, nullptr},
  {"radar_s4", "Still threshold, gate 4", "", "Radar", K_NUM, 0, 100, 1, 30, nullptr, nullptr},
  {"radar_s5", "Still threshold, gate 5", "", "Radar", K_NUM, 0, 100, 1, 30, nullptr, nullptr},
  {"radar_s6", "Still threshold, gate 6", "", "Radar", K_NUM, 0, 100, 1, 20, nullptr, nullptr},
  {"radar_s7", "Still threshold, gate 7", "", "Radar", K_NUM, 0, 100, 1, 20, nullptr, nullptr},
  {"radar_s8", "Still threshold, gate 8", "", "Radar", K_NUM, 0, 100, 1, 20, nullptr, nullptr},
  // Network
  {"frg_on", "The camera checks the radar", "", "Camera", K_BOOL, 0, 1, 1, 1, nullptr,
   "With Frigate running, the radar counts you only if the camera has seen you in the seat lately. "
   "Frigate down or silent: the radar alone decides. Needs MQTT on."},
  {"frg_veto_s", "Not seen in the seat for", "s", "Camera", K_NUM, 10, 600, 5, 60, nullptr,
   "Then the radar's presence stops counting, and the absence countdown runs."},
  {"frg_seat_s", "Counted as in the seat after", "s", "Camera", K_NUM, 0, 600, 5, 60, nullptr,
   "Someone taken for you counts only after this long in the seat, so a walk past does not. 0: at once."},
  {"frg_empty_s", "Seat empty this long ends \"it's you\"", "s", "Camera", K_NUM, 1, 60, 1, 5, nullptr,
   "While someone stays in the seat, a new track there counts as you. Seen empty longer than this, the next one "
   "must be recognised again."},
  {"frg_dead_s", "Frigate counts as down after", "s", "Camera", K_NUM, 15, 300, 5, 30, nullptr,
   "Without its camera heartbeat (every 10 s). Then: radar only."},
  {"frg_night", "Stand down in quiet hours", "", "Camera", K_BOOL, 0, 1, 1, 0, nullptr,
   "For a camera that cannot see in the dark."},
  {"frg_cam", "Frigate camera", "", "Camera", K_TEXT, 0, 31, 0, 0, nullptr, "Empty: bench."},
  {"frg_zone", "Seat zone", "", "Camera", K_TEXT, 0, 31, 0, 0, nullptr, "Empty: bench_seat."},
  {"frg_face", "Your name in the face library", "", "Camera", K_TEXT, 0, 63, 0, 0, nullptr,
   "Exactly as in Frigate. Empty: faces are not used."},
  {"frg_model", "Person classifier", "", "Camera", K_TEXT, 0, 31, 0, 0, nullptr, "Empty: Bench_person."},
  {"frg_class", "Its class for you", "", "Camera", K_TEXT, 0, 31, 0, 0, nullptr, "Empty: owner."},
  {"shelly_host", "Shelly address", "", "Network", K_TEXT, 0, 63, 0, 0, nullptr, "IP address of the Shelly."},
  {"mqtt_on", "Home Assistant (MQTT)", "", "Network", K_BOOL, 0, 1, 1, 0, nullptr, "Convenience only."},
  {"mqtt_host", "MQTT broker", "", "Network", K_TEXT, 0, 63, 0, 0, nullptr, nullptr},
  {"mqtt_port", "MQTT port", "", "Network", K_NUM, 1, 65535, 1, 1883, nullptr, nullptr},
  {"mqtt_user", "MQTT user", "", "Network", K_TEXT, 0, 63, 0, 0, nullptr, nullptr},
  {"mqtt_pass", "MQTT password", "", "Network", K_PASS, 0, 63, 0, 0, nullptr, nullptr},
};
// clang-format on
const size_t SETTINGS_COUNT = sizeof(SETTINGS) / sizeof(SETTINGS[0]);

// Buttons shown at the top of a settings tab.
const SettingAction SETTING_ACTIONS[] = {
  {"Sound", "Play the whole warning", "buz.test", "0"},
  {"Sound", "Warning start", "buz.test", "1"},
  {"Sound", "Second mark", "buz.test", "2"},
  {"Sound", "Final beeps", "buz.test", "3"},
  {"Sound", "Cut", "buz.test", "4"},
  {"Sound", "Alarm (3 s)", "buz.test", "5"},
  {"Sound", "Click", "buz.test", "6"},
  {"Sound", "Confirm", "buz.test", "7"},
  {"Lights", "Test: all on", "led.test", "1"},
  {"Lights", "Test: all off", "led.test", "2"},
  {"Lights", "Test: each in turn", "led.test", "3"},
  {"Lights", "Test: breathe", "led.test", "4"},
  {"Lights", "Back to normal", "led.test", "0"},
  {"Screen", "Test pattern", "oled.test", "1"},
  {"Screen", "All white", "oled.test", "3"},
  {"Screen", "Blank", "oled.test", "2"},
  {"Screen", "Show the warning page", "oled.test", "4"},
  {"Screen", "Back to normal", "oled.test", "0"},
};
const size_t SETTING_ACTIONS_COUNT = sizeof(SETTING_ACTIONS) / sizeof(SETTING_ACTIONS[0]);

static int32_t ints[sizeof(SETTINGS) / sizeof(SETTINGS[0])];
static String texts[sizeof(SETTINGS) / sizeof(SETTINGS[0])];
static uint32_t generation = 1;

static int index_of(const char *key) {
  for (size_t i = 0; i < SETTINGS_COUNT; i++)
    if (strcmp(SETTINGS[i].key, key) == 0) return (int) i;
  return -1;
}

const SettingRow *setting_row(const char *key) {
  int i = index_of(key);
  return i < 0 ? nullptr : &SETTINGS[i];
}

static bool is_text(const SettingRow &r) { return r.kind == K_TEXT || r.kind == K_PASS; }

// NVS keys are at most 15 characters: every key in the table fits.
void settings_begin() {
  Preferences p;
  p.begin("cfg", true);
  for (size_t i = 0; i < SETTINGS_COUNT; i++) {
    const SettingRow &r = SETTINGS[i];
    if (is_text(r)) texts[i] = p.getString(r.key, "");
    else ints[i] = constrain(p.getInt(r.key, r.def), r.lo, r.hi);
  }
  p.end();
}

int32_t setting_int(const char *key) {
  int i = index_of(key);
  if (i < 0) { Con.printf("BUG: unknown setting %s\n", key); return 0; }
  return ints[i];
}

const char *setting_text(const char *key) {
  int i = index_of(key);
  return i < 0 ? "" : texts[i].c_str();
}

static void store(int i) {
  Preferences p;
  p.begin("cfg", false);
  if (is_text(SETTINGS[i])) p.putString(SETTINGS[i].key, texts[i]);
  else p.putInt(SETTINGS[i].key, ints[i]);
  p.end();
  generation++;
}

bool setting_set(const char *key, const String &value, String &stored, bool &clamped, String &why) {
  clamped = false;
  int i = index_of(key);
  if (i < 0) { why = "no such setting"; return false; }
  const SettingRow &r = SETTINGS[i];
  if (is_text(r)) {
    String v = value;
    v.trim();
    if ((int) v.length() > r.hi) { why = "too long (at most " + String(r.hi) + " characters)"; return false; }
    texts[i] = v;
    store(i);
    stored = r.kind == K_PASS ? "" : v;
    return true;
  }
  String v = value;
  v.trim();
  v.replace(',', '.');
  char *end = nullptr;
  double d = strtod(v.c_str(), &end);
  if (v.isEmpty() || !end || *end) { why = "not a number"; return false; }
  if (!isfinite(d) || fabs(d) > 1e9) { why = "out of range"; return false; }
  int32_t n = (int32_t) lround(d);
  if (r.step > 1) n = (int32_t) lround((double) n / r.step) * r.step;
  int32_t c = constrain(n, r.lo, r.hi);
  clamped = c != n;
  // Cross-checks: the warning must fit inside the absence delay.
  if (strcmp(key, "warn_min") == 0 && c >= setting_int("away_min")) { why = "the warning must be shorter than the absence delay"; return false; }
  if (strcmp(key, "away_min") == 0 && c <= setting_int("warn_min")) { why = "the absence delay must be longer than the warning"; return false; }
  // The ceiling must be longer than the absence delay, or the absence delay never matters.
  if (strcmp(key, "ceil_min") == 0 && c <= setting_int("away_min")) { why = "the ceiling must be longer than the absence delay"; return false; }
  if (strcmp(key, "away_min") == 0 && c >= setting_int("ceil_min")) { why = "the absence delay must be shorter than the ceiling"; return false; }
  // Renewals must come well inside the lease, or the Shelly cuts with no warning.
  if (strcmp(key, "lease_s") == 0 && c < 3 * setting_int("renew_s")) { why = "the lease must be at least 3 renewal periods"; return false; }
  if (strcmp(key, "renew_s") == 0 && 3 * c > setting_int("lease_s")) { why = "renewals must come at least 3 times per lease"; return false; }
  ints[i] = c;
  store(i);
  stored = String(c);
  return true;
}

bool setting_set_int(const char *key, int32_t v) {
  int i = index_of(key);
  if (i < 0 || is_text(SETTINGS[i])) return false;
  ints[i] = constrain(v, SETTINGS[i].lo, SETTINGS[i].hi);
  store(i);
  return true;
}

uint32_t settings_generation() { return generation; }

// Every setting back to its default. The house WiFi (namespace "net") and the login (secrets.h)
// are not settings and stay. The caller restarts the board, which then reads the defaults.
void settings_reset() {
  Preferences p;
  p.begin("cfg", false);
  p.clear();
  p.end();
}
