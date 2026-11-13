// Bench node firmware (ESP32-S3-DevKitC-1 N16R8), Arduino.
//
// The decisions are in bench_core.h (tested on a PC). This file wires the core to the hardware: keys, key LEDs,
// buzzer, OLED, the presence sensor, the bar sense, the lease link to the Shelly, the web portal, Home Assistant
// over MQTT, and network time for quiet hours. Design and lease contract: the Firmware Gospel
// (docs/firmware-gospel.md).
// Pins come from the pin file, pins.h; names are the hardware's signal names.
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>

#include <esp_ota_ops.h>

#include "bench_core.h"
#include "console.h"
#include "ha_iface.h"
#include "json.h"
#include "gfx.h"
#include "oled.h"
#include "screen_light.h"
#include "screens.h"
#include "portal.h"
#include "radar.h"
#include "secrets.h"
#include "settings.h"
#include "shelly.h"

using namespace bench;

ConsoleTee Con;
static Core core;

#include "version.h"                                 // FW_VERSION, FW_VERSION_SHORT, FW_COMMIT (scripts/version.py)
static char fw_full[48];

// ---- Pins (from the pin file) -----------------------------------------------------------------------------
#include "pins.h"
constexpr int KEY_PINS[3] = {KEY_ON, KEY_OVERRIDE, KEY_FN};
constexpr int LED_PINS[3] = {LED_ON, LED_OVERRIDE, LED_FN};
const char *const KEY_NAMES[3] = {"ON", "OVERRIDE", "FN"};
constexpr int LED_CH[3] = {0, 1, 2};
constexpr int BUZ_CH = 6;                          // its own LEDC timer

static const char TZ_MONTREAL[] = "EST5EDT,M3.2.0,M11.1.0";

static bool key_state[3] = {};
static uint32_t key_change[3] = {};
static uint8_t led_level[3] = {};
static uint16_t buz_hz_now = 0;
static int buz_vol_now = -1;
static constexpr int ALARM_VOL = 50;     // the buzzer at full volume (the top of buz_vol)
static bool sense_live = false;
static bool time_started = false;
static int led_test = 0;                           // web page LED test: 0 off, 1 all on, 2 all off, 3 chase, 4 breathe
static uint32_t led_test_until = 0;
static int oled_test = 0;                          // web page screen test: 0 off, 1 pattern, 2 blank, 3 white, 4 warning page
static uint32_t oled_test_until = 0;
static void screen_settings(bool force);
static char mac_str[18];
static bool on_trial = false;                    // a new firmware not confirmed yet (see trial_begin)
static uint32_t restart_at = 0;                    // a restart asked for, once the web reply has gone out

// ---- Settings -> core -------------------------------------------------------------------------------------
static void apply_settings() {
  Config &c = core.cfg;
  c.away_s = setting_int("away_min") * 60;
  c.warn_s = setting_int("warn_min") * 60;
  c.ceil_s = setting_int("ceil_min") * 60;
  c.lease_s = setting_int("lease_s");
  c.renew_s = setting_int("renew_s");
  c.mismatch_s = setting_int("mismatch_s");
  c.link_lost_s = c.renew_s * 2 + 5;
  c.hold_to_cut_ms = setting_int("hold_to_cut_ms");
  c.long_ms = setting_int("long_ms");
  c.menu_idle_ms = setting_int("menu_idle_s") * 1000;
  c.screen_ms = setting_int("screen_s") * 1000;
  c.wake_first = setting_int("scr_mode") == 0;           // "on after a key": the first press only wakes
  c.step_min = setting_int("hold_step_min");
  c.default_hold_min = setting_int("hold_start_min");
  c.base_hz = setting_int("buz_hz");
  c.clicks = setting_int("clicks");
  c.click_ms = setting_int("click_ms");
  c.confirm_on = setting_int("confirm_on");
  c.confirm_ms = setting_int("confirm_ms");
  c.cut_on = setting_int("cut_on");
  c.cut_ms = setting_int("cut_ms");
  c.m1_on = setting_int("m1_on");
  c.m1_notes = setting_int("m1_notes");
  c.m1_note_ms = setting_int("m1_note_ms");
  c.m1_step_pct = setting_int("m1_step_pct");
  c.m1_repeats = setting_int("m1_repeats");
  c.reminder_s = setting_int("reminder_s");
  c.m2_on = setting_int("m2_on");
  c.m2_at_s = setting_int("m2_at_s");
  c.m2_notes = setting_int("m2_notes");
  c.m2_note_ms = setting_int("m2_note_ms");
  c.m2_step_pct = setting_int("m2_step_pct");
  c.m2_repeats = setting_int("m2_repeats");
  c.fin_on = setting_int("fin_on");
  c.fin_s = setting_int("fin_s");
  c.fin_start_ms = setting_int("fin_start_ms");
  c.fin_end_ms = setting_int("fin_end_ms");
  c.fin_rise_pct = setting_int("fin_rise_pct");
  c.fin_double_s = setting_int("fin_double_s");
  c.quiet_mutes_final = setting_int("quiet_mute");
  c.alarm_hi_pct = setting_int("alarm_hi_pct");
  c.alarm_period_ms = setting_int("alarm_period_ms");
  c.led_live = setting_int("led_live");
  c.led_dead = setting_int("led_dead");
  c.led_dim_pct = setting_int("led_dim_pct");
  c.hold_fx = setting_int("hold_fx");
  c.breathe_ms = setting_int("breathe_ms");
  c.breathe_min_pct = setting_int("breathe_min_pct");
  c.warn_blink_ms = setting_int("warn_blink_ms");
  c.warn_fast_ms = setting_int("warn_fast_ms");
  c.alarm_fx = setting_int("alarm_fx");
  c.alarm_step_ms = setting_int("alarm_step_ms");
  c.menu_blink_ms = setting_int("menu_blink_ms");
  c.page_timers = setting_int("scr_timers");
  c.page_sensor = setting_int("scr_sensor");
  // Relations the settings page enforces, re-checked here in case NVS holds an older combination.
  if (c.warn_s >= c.away_s) c.warn_s = c.away_s - 60;
  if (c.renew_s * 3 > c.lease_s) { c.renew_s = c.lease_s / 3; c.link_lost_s = c.renew_s * 2 + 5; }
}

void settings_changed(const char *key) {
  apply_settings();
  if (strncmp(key, "oled_", 5) == 0) screen_settings(true);
  if (strncmp(key, "mqtt_", 5) == 0) mqtt_restart();
  if (strncmp(key, "frg_", 4) == 0) {
    frigate_apply_settings();
    if (strcmp(key, "frg_on") == 0 || strcmp(key, "frg_cam") == 0) mqtt_restart();   // subscriptions change
  }
  if (strcmp(key, "radar_mg") == 0 || strcmp(key, "radar_sg") == 0 || strcmp(key, "radar_hold") == 0)
    radar_apply_settings();
}

// ---- Time and quiet hours ---------------------------------------------------------------------------------
static bool clock_now(struct tm &t) {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;              // not synced yet
  localtime_r(&now, &t);
  return true;
}

// Quiet only when the time is known: without it, nothing is muted.
static bool quiet_now() {
  if (!setting_int("quiet_on")) return false;
  struct tm t;
  if (!clock_now(t)) return false;
  int from = setting_int("quiet_from_h"), to = setting_int("quiet_to_h"), h = t.tm_hour;
  if (from == to) return false;
  return from < to ? (h >= from && h < to) : (h >= from || h < to);
}

// ---- Text helpers -----------------------------------------------------------------------------------------

static const char *phase_name(Phase p) {
  switch (p) {
    case Phase::Off: return "off";
    case Phase::On: return "on";
    case Phase::Hold: return "hold";
    case Phase::Warning: return "warning";
    case Phase::Alarm: return "alarm";
    case Phase::Unknown: return "unknown";
  }
  return "?";
}
static const char *why_name(Why w) {
  switch (w) {
    case Why::Absence: return "absence";
    case Why::Hold: return "hold";
    case Why::Ceiling: return "ceiling";
    case Why::Link: return "link";
    default: return "";
  }
}
static const char *fault_code(Fault f) {
  switch (f) {
    case Fault::StuckOn: return "stuck_on";
    case Fault::LostLive: return "lost_live";
    case Fault::SenseDead: return "sense_dead";
    case Fault::RadarDead: return "radar_dead";
    default: return "";
  }
}
static const char *fault_text(Fault f) {
  switch (f) {
    case Fault::StuckOn: return "The bar is still live although the Shelly is off: welded relay or hung Shelly. Unplug the bar.";
    case Fault::LostLive: return "No answer from the Shelly for longer than a lease, and the bar is still live. Unplug the bar.";
    case Fault::SenseDead: return "The Shelly says on but the bar sense says dead: check PS2 and the bar-sense cable.";
    case Fault::RadarDead: return "No data from the radar: it counts as absence.";
    default: return "";
  }
}
static const char *toast_text(Toast t) {
  switch (t) {
    case Toast::TurnedOn: return "Power ON";
    case Toast::TurnedOff: return "Power OFF";
    case Toast::CutAbsence: return "Cut: nobody seen";
    case Toast::CutHold: return "Cut: hold ended";
    case Toast::CutCeiling: return "Cut: 4 h limit";
    case Toast::CutLink: return "Cut: no link";
    case Toast::CeilingReset: return "Timer reset";
    case Toast::HoldStarted: return "Hold started";
    case Toast::HoldCancelled: return "Hold cancelled";
    case Toast::MenuUnchanged: return "No change";
    case Toast::WarningAnswered: return "OK, staying on";
    case Toast::AlarmSilenced: return "Alarm silenced";
    case Toast::ShellyRefused: return "Shelly said no";
    case Toast::RemoteOn: return "ON from HA";
    case Toast::RemoteOff: return "OFF from HA";
    case Toast::Adopted: return "ON from Shelly";
    case Toast::CutNotDone: return "Hold ON 1.5 s to cut";
    default: return "";
  }
}

// ---- The screen -------------------------------------------------------------------------------------------
// Contrast, flip and invert, sent only when they change. The contrast is decided here and only here, every frame:
// full during an alarm; the dimmed one (never above the quiet-hours one at night) while the idle screen is dim;
// else normal or quiet hours.
static bool screen_idle = false;               // the idle screen shows, dimmed (screen_light.h)
static light::Clock screen_light;              // bright, dim or dark while nothing is called up
static light::Level screen_level = light::Level::Bright;   // as last drawn: logged on change, shown on /api/state
static const char *level_name(light::Level l) {
  return l == light::Level::Bright ? "bright" : l == light::Level::Dim ? "dim" : "dark";
}
static void screen_settings(bool force) {
  static int contrast = -1, flip = -1, invert = -1, dimmed = -1;
  int quiet_c = setting_int("oled_quiet_c");
  int c = setting_int("oled_contrast");
  if (quiet_now()) c = quiet_c;
  if (screen_idle) {
    c = setting_int("amb_contrast");
    if (quiet_now() && quiet_c < c) c = quiet_c;
  }
  if (core.alarm_on()) c = 255;
  int f = setting_int("oled_flip"), i = setting_int("oled_invert");
  if (force || c != contrast) { oled_contrast(c); contrast = c; }
  int d = screen_idle && !core.alarm_on();
  if (force || d != dimmed) { oled_dim(d); dimmed = d; }
  if (force || f != flip) { oled_flip(f); flip = f; }
  if (force || i != invert) { oled_invert(i); invert = i; }
}

// Fills the screens' model from the device and draws the screen the core asks for.
static uint32_t boot_until = 0;

// The boot screen, drawn once as soon as the screen starts, so it shows during the rest of start-up (the main
// loop draws the screen only after it). setup() then keeps it up 1.5 s more, or it would flash by unseen.
static void draw_boot_screen() {
  screens::Model m;
  m.view = screens::View::Boot;
  m.now_ms = millis();
  m.version = FW_VERSION_SHORT;
  m.commit = FW_COMMIT;
  gfx::offset(0, 0);
  screens::draw(m);
  oled_flush();
}

static bool draw_screen() {
  using screens::View;
  screens::Model m;
  ShellyReading sh;
  shelly_reading(sh);
  const uint32_t now = millis();
  m.now_ms = now;
  switch (core.phase()) {
    case Phase::Off: m.phase = screens::Phase::Off; break;
    case Phase::On: m.phase = screens::Phase::On; break;
    case Phase::Hold: m.phase = screens::Phase::Hold; break;
    case Phase::Warning: m.phase = screens::Phase::Warning; break;
    case Phase::Alarm: m.phase = screens::Phase::Alarm; break;
    case Phase::Unknown: m.phase = screens::Phase::Unknown; break;
  }
  if (core.phase() == Phase::Warning || core.phase() == Phase::Alarm) m.phase = core.want_on() ? screens::Phase::On : screens::Phase::Off;
  m.cut_s = core.away_due_s();
  m.cut_total_s = core.cfg.away_s;
  m.hold_active = core.hold_active();
  m.hold_s = core.hold_left_s();
  m.hold_total_s = core.hold_total_s();
  m.limit_s = core.ceil_left_s();
  m.limit_total_s = core.cfg.ceil_s;
  m.lease_s = core.lease_left_s();
  m.lease_total_s = core.cfg.lease_s;
  m.warn_s = core.warn_left_s();
  m.warn_total_s = core.warn_total_s();
  switch (core.warn_why()) {
    case Why::Absence: m.why = screens::Why::Absence; break;
    case Why::Hold: m.why = screens::Why::Hold; break;
    case Why::Ceiling: m.why = screens::Why::Ceiling; break;
    case Why::Link: m.why = screens::Why::Link; break;
    default: m.why = screens::Why::None; break;
  }
  m.present = core.present();
  m.seen_ago_s = core.seen_ago_s();
  // The moving target's distance when it counts, else the still target's (display only).
  bool mv_counts = (radar.state & 1) && radar.move_en >= setting_int("radar_min_mv");
  m.dist_cm = mv_counts ? radar.move_cm : radar.still_cm;
  m.move_en = radar.move_en;
  m.still_en = radar.still_en;
  m.max_gate = setting_int("radar_mg");
  m.radar_ok = core.radar_ok();
  m.link_ok = core.link_ok();                    // with the bar off too: the Shelly is asked every 10 s
  m.wifi_ok = WiFi.status() == WL_CONNECTED;
  m.quiet = quiet_now();
  struct tm t;
  if (clock_now(t)) { m.clock_ok = true; m.hh = t.tm_hour; m.mm = t.tm_min; }
  m.watts = isfinite(sh.w) ? sh.w : -1;
  m.temp_c = isfinite(sh.t) ? sh.t : -1;
  m.address = portal_address();
  Fault f = core.fault();
  m.fault = f == Fault::SenseDead ? "FAULT: bar sense" : f == Fault::RadarDead ? "FAULT: radar" : "";
  m.alarm_silenced = core.alarm_silenced();
  m.alarm_stuck = f == Fault::StuckOn;
  m.menu_min = core.menu_minutes();
  m.menu_hours = core.menu_hours();
  m.hold_to_cut = core.hold_to_cut_progress();
  m.toast = toast_text(core.last_toast());
  m.toast_good = core.last_toast() != Toast::ShellyRefused && core.last_toast() != Toast::CutNotDone;
  m.big = setting_int("scr_l1");
  m.foot_l = setting_int("scr_l2");
  m.foot_r = setting_int("scr_l3");
  m.version = FW_VERSION_SHORT;                                   // "v.0.3 261102T2332Z": the screen's short form
  m.commit = FW_COMMIT;

  int mode = setting_int("scr_mode");
  switch (core.page()) {
    case Page::Dark: m.view = View::Dark; break;
    case Page::Status: m.view = View::Status; break;
    case Page::Timers: m.view = View::Timers; break;
    case Page::Sensor: m.view = View::Radar; break;
    case Page::Menu: m.view = View::Menu; break;
    case Page::HoldToCut: m.view = View::HoldToCut; break;
    case Page::Warning: m.view = View::Warning; break;
    case Page::Alarm: m.view = View::Alarm; break;
    case Page::Toast: m.view = View::Toast; break;
  }
  if (mode == 1 && m.view == View::Dark) m.view = View::Status;                       // always on
  if (mode == 2 && (m.view == View::Status || m.view == View::Timers || m.view == View::Radar || m.view == View::Toast))
    m.view = View::Dark;                                                                // warnings and menu only
  // Idle: the radar widget or the pig with the bar on; the relaxing pig with it off. Nothing while the bar's state
  // is unknown (the relaxing pig would say "off"), and nothing in the "warnings and menu only" mode.
  if (m.view == View::Dark && mode != 2 && core.phase() != Phase::Unknown) {
    int idle = core.want_on() ? setting_int("scr_idle") : (setting_int("scr_off") ? 2 : 0);
    if (idle) { m.view = View::Ambient; m.pig = idle == 2; }
  }
  // Nothing called up (the core's page is dark): the idle screen, bright, then dim, then dark.
  // Anything called up - status after a key, menu, warning, alarm, message - shows at full brightness and restarts
  // the bright time.
  screen_light.bright_ms = (uint32_t) setting_int("scr_bright_min") * 60000;
  screen_light.dark_ms = (uint32_t) setting_int("scr_dark_min") * 60000;
  screen_light.tick(core.present(), now);
  bool idle_page = core.page() == Page::Dark;
  if (!idle_page) screen_light.activity(now);
  light::Level level = idle_page ? screen_light.level(now) : light::Level::Bright;
  if (level == light::Level::Dark && mode == 1) level = light::Level::Dim;     // "always on": dim, never dark
  if (level == light::Level::Dark) m.view = View::Dark;
  m.pig_scene_ms = (uint32_t) setting_int("pig_scene_s") * 1000;
  if ((int32_t) (boot_until - now) > 0) m.view = View::Boot;

  bool testing = oled_test && (int32_t) (oled_test_until - now) > 0 && !core.warning_active() && !core.alarm_on();
  if (!testing) oled_test = 0;
  if (testing && oled_test == 4) { m.view = View::Warning; m.why = screens::Why::Absence; m.warn_s = 299; m.warn_total_s = 300; }

  // Against burn-in: move the whole image by up to 1 px each way, once a minute (the layout keeps a 2 px margin).
  if (setting_int("scr_shift")) {
    uint32_t mn = now / 60000;
    gfx::offset((int) (mn % 3) - 1, (int) ((mn / 3) % 3) - 1);
  } else {
    gfx::offset(0, 0);
  }
  screens::draw(m);
  if (testing && oled_test != 4) {
    gfx::offset(0, 0);
    gfx::clear();
    if (oled_test == 1) for (int x = 0; x < 128; x += 8) for (int y = 0; y < 64; y += 8) if (((x + y) / 8) % 2) gfx::fill(x, y, 8, 8);
    if (oled_test == 3) gfx::fill(0, 0, 128, 64);
  }
  screen_idle = level == light::Level::Dim && m.view != View::Boot && !testing;
  if (level != screen_level) {
    Con.printf("%8u  screen %s (seen %d, %us ago)\n", (unsigned) now, level_name(level), core.present() ? 1 : 0,
               (unsigned) core.seen_ago_s());
    screen_level = level;
  }
  screen_settings(false);
  oled_flush();
  return screens::animated(m);
}

// ---- Outputs ----------------------------------------------------------------------------------------------
static void drive_outputs() {
  // Alarms can't be turned off: full brightness, no trims, whatever the settings.
  const bool alarm = core.alarm_on();
  float bright = alarm ? 1.0f : setting_int("led_bright") / 100.0f;
  if (quiet_now() && !alarm) bright *= setting_int("led_quiet_pct") / 100.0f;
  static const char *const TRIM[3] = {"led_on_pct", "led_ovr_pct", "led_fn_pct"};
  bool testing = led_test && (int32_t) (led_test_until - millis()) > 0 && !core.warning_active() && !core.alarm_on();
  if (!testing) led_test = 0;
  for (int k = 0; k < 3; k++) {
    float lv = core.led((Key) k);
    if (testing) {
      uint32_t n = millis();
      lv = led_test == 1 ? 1.0f : led_test == 2 ? 0.0f : led_test == 3 ? (((n / 400) % 3) == (uint32_t) k ? 1.0f : 0.0f)
                                                       : (float) (n % 2000 < 1000 ? n % 1000 : 1000 - n % 1000) / 1000.0f;
    }
    uint8_t v = (uint8_t) (lv * bright * (alarm ? 100 : setting_int(TRIM[k])) / 100.0f * 255);
    if (v != led_level[k]) { led_level[k] = v; ledcWrite(LED_CH[k], v); }
  }
  Tone t = core.tone();
  // Quiet hours: everything but the alarms plays softly. Alarms always play at full volume.
  int vol = t.alarm ? ALARM_VOL : quiet_now() ? setting_int("quiet_vol") : setting_int("buz_vol");
  if (t.hz != buz_hz_now || (t.hz && vol != buz_vol_now)) {
    if (t.hz && t.hz != buz_hz_now) ledcChangeFrequency(BUZ_CH, t.hz, 8);
    ledcWrite(BUZ_CH, t.hz ? vol * 255 / 100 : 0);
    buz_hz_now = t.hz;
    buz_vol_now = vol;
  }
}

static void poll_inputs() {
  uint32_t now = millis();
  for (int k = 0; k < 3; k++) {
    bool down = digitalRead(KEY_PINS[k]) == LOW;
    if (down != key_state[k] && now - key_change[k] >= 20) {      // 20 ms debounce
      key_change[k] = now;
      key_state[k] = down;
      if (down) { core.key_down((Key) k); screen_light.activity(now); } else core.key_up((Key) k);
    }
  }
  bool live = digitalRead(BAR_LIVE) == LOW;
  if (live != sense_live) { sense_live = live; Con.printf("%8u  bar sense -> %s\n", (unsigned) now, live ? "LIVE" : "dead"); }
}

static void run_lease() {
  Op op;
  bool got;
  Answer a;
  uint32_t seq;
  if (shelly_take_result(op, got, a, seq)) {
    core.lease_result(op, got, a, millis(), seq);
    if (!got && core.fails() == 3) {
      ShellyReading sh;
      shelly_reading(sh);
      Con.printf("%8u  Shelly: 3 requests without an answer (%s)\n", (unsigned) millis(), sh.error);
    }
  }
  op = core.take_op(millis());
  if (op != Op::None) shelly_send(op, core.cfg.lease_s, core.cfg.ceil_s, core.seq());
}

// ---- Restart memory ---------------------------------------------------------------------------------------
// Survives a software reset, a crash or a brownout reset (not a power loss): lets a restart while the bar is on
// resume the absence clock and the hold instead of starting them again.
struct Kept { uint32_t magic, seen_ago_ms, hold_left_ms, check; };
static RTC_NOINIT_ATTR Kept kept;
static constexpr uint32_t KEPT_MAGIC = 0x7E57BE4C;

static void keep_state() {
  if (core.want_on()) {
    kept.seen_ago_ms = core.seen_ago_ms();
    kept.hold_left_ms = core.hold_left_ms();
    kept.magic = KEPT_MAGIC;
  } else {
    kept.magic = 0;
  }
  kept.check = kept.magic ^ kept.seen_ago_ms ^ kept.hold_left_ms;
}

static void restore_state() {
  esp_reset_reason_t r = esp_reset_reason();
  bool valid = kept.magic == KEPT_MAGIC && kept.check == (kept.magic ^ kept.seen_ago_ms ^ kept.hold_left_ms);
  if (r != ESP_RST_POWERON && valid) {
    core.restore(kept.seen_ago_ms, kept.hold_left_ms);
    Con.printf("Restart while on: resuming, last seen %u s ago, hold %u s left\n",
               (unsigned) (kept.seen_ago_ms / 1000), (unsigned) (kept.hold_left_ms / 1000));
  }
  kept.magic = 0;
}

// Someone is at the bench: the radar says so, and the camera does not veto it. The camera only takes presence
// away; Frigate down or silent means radar only (frigate.h).
static bool present_now() {
  bool radar = radar_present();
  bool veto = frigate_veto(quiet_now());               // called every time: it also keeps the camera's clock
  return radar && !veto;
}

// Keeps the keys, the warning and the lease running while something waits (the radar's settings write).
static void service_io() {
  poll_inputs();
  core.tick(millis(), present_now(), radar_alive(setting_int("radar_dead_s") * 1000), sense_live, quiet_now());
  run_lease();
  drive_outputs();
}

// ---- The live state for the web page (the Firmware Gospel, the web page) ----------------------------------
void build_state(String &o) {
  const uint32_t now = millis();
  ShellyReading sh;
  shelly_reading(sh);
  struct tm t;
  char clk[8] = "";
  if (clock_now(t)) snprintf(clk, sizeof(clk), "%02d:%02d", t.tm_hour, t.tm_min);
  kv(o, "up", now / 1000);
  kv(o, "heap", ESP.getFreeHeap());
  ks(o, "fw", fw_full);
  ks(o, "built", __DATE__ " " __TIME__);
  ks(o, "commit", FW_COMMIT);
  kv(o, "trial", on_trial);
  ks(o, "mac", mac_str);
  ks(o, "clock", clk);
  kv(o, "quiet", quiet_now());
  ks(o, "phase", phase_name(core.phase()));
  kopen(o, "bar");
  kv(o, "shelly", core.shelly_on());
  kv(o, "sense", sense_live);
  ks(o, "link", !core.ever_answered() ? "none" : core.link_ok() ? "ok" : "lost");
  kv(o, "ago", core.ever_answered() ? (long) core.since_answer_s() : -1);
  kv(o, "lease", core.lease_left_s());
  kv(o, "ceil", core.ceil_left_s());
  kf(o, "w", sh.w); kf(o, "v", sh.v); kf(o, "a", sh.a, 3); kf(o, "t", sh.t);
  ks(o, "err", sh.error);
  kv(o, "script", sh.script_id);
  end_obj(o); o += ',';
  kopen(o, "away");
  kv(o, "seen", core.present()); kv(o, "ago", core.seen_ago_s()); kv(o, "due", core.away_due_s());
  end_obj(o); o += ',';
  kopen(o, "screen"); ks(o, "level", level_name(screen_level)); end_obj(o); o += ',';
  kopen(o, "hold"); kv(o, "on", core.hold_active()); kv(o, "left", core.hold_left_s()); end_obj(o); o += ',';
  FrigateStatus fs;
  frigate_status(fs);
  kopen(o, "cam");
  kv(o, "on", fs.on); kv(o, "live", fs.live); kv(o, "veto", fs.veto); kv(o, "owner_ago", fs.owner_ago_s);
  kv(o, "tracks", fs.tracks); kv(o, "unsure", fs.unsure);
  end_obj(o); o += ',';
  kopen(o, "warn");
  kv(o, "on", core.warning_active()); ks(o, "why", why_name(core.warn_why()));
  kv(o, "left", core.warn_left_s()); kv(o, "total", core.warn_total_s());
  end_obj(o); o += ',';
  kopen(o, "alarm");
  kv(o, "on", core.alarm_on()); ks(o, "code", fault_code(core.fault())); ks(o, "text", fault_text(core.fault()));
  kv(o, "silenced", core.alarm_silenced());
  kv(o, "minor", core.minor_fault()); kv(o, "minor_silenced", core.fault_silenced());
  end_obj(o); o += ',';
  kopen(o, "radar");
  kv(o, "ok", core.radar_ok()); kv(o, "fps", radar.fps); kv(o, "st", radar.state);
  kv(o, "md", radar.move_cm); kv(o, "me", radar.move_en); kv(o, "sd", radar.still_cm); kv(o, "se", radar.still_en);
  kv(o, "dd", radar.detect_cm); kv(o, "out", digitalRead(RADAR_OUT) == HIGH); kv(o, "eng", radar.engineering);
  o += "\"gm\":"; jarr(o, radar.gate_move, 9); o += ",\"gs\":"; jarr(o, radar.gate_still, 9); o += ',';
  kopen(o, "par");
  kv(o, "ok", radar.params_ok); kv(o, "mg", radar.max_move_gate); kv(o, "sg", radar.max_still_gate);
  kv(o, "hold", radar.hold_s);
  o += "\"sm\":"; jarr(o, radar.sens_move, 9); o += ",\"ss\":"; jarr(o, radar.sens_still, 9);
  o += "},";
  ks(o, "ver", radar.version);
  kopen(o, "cal");
  static const char *const STAGES[] = {"", "wait", "sample", "done"};
  bool cal_on = radar_cal.stage == RadarCal::Wait || radar_cal.stage == RadarCal::Sample;
  kv(o, "on", cal_on); ks(o, "stage", STAGES[radar_cal.stage]);
  kv(o, "left", cal_on ? (long) max(0L, (long) (int32_t) (radar_cal.until - now) / 1000) : 0);
  o += "\"max_m\":"; jarr(o, radar_cal.max_move, 9); o += ",\"max_s\":"; jarr(o, radar_cal.max_still, 9);
  o += "}},";
  o += "\"keys\":["; for (int k = 0; k < 3; k++) { if (k) o += ','; o += key_state[k] ? 1 : 0; } o += "],";
  o += "\"leds\":"; jarr(o, led_level, 3); o += ',';
  kv(o, "buz", buz_hz_now != 0);
  kopen(o, "mqtt");
  kv(o, "on", setting_int("mqtt_on")); ks(o, "state", mqtt_state()); ks(o, "err", mqtt_error());
  end_obj(o); o += ',';
}

// ---- Actions (web page) -----------------------------------------------------------------------------------
bool do_action(const String &a, const String &v, String &reply) {
  long n = v.toInt();
  if (a == "bar.on") { core.web_on(); reply = "bar ON asked"; }
  else if (a == "bar.off") { core.web_off(); reply = "bar OFF asked"; }
  else if (a == "hold.set") {
    if (n < 0) { reply = "not a number of minutes"; return false; }
    core.web_hold(n);
    reply = n ? "hold set for " + String(min((long) core.max_hold_min(), n)) + " min" : "hold cancelled";
  }
  else if (a == "warn.test") {
    if (!core.test_sound()) { reply = "not during a warning or an alarm"; return false; }
    reply = "playing the whole warning";
  }
  else if (a == "alarm.silence") {
    if (!core.alarm_on() && !core.minor_fault()) { reply = "nothing to silence"; return false; }
    core.silence_alarm(); reply = "silenced";
  }
  else if ((a == "buz.beep" || a == "buz.test" || a == "led.test" || a == "oled.test") &&
           (core.warning_active() || core.alarm_on())) {
    reply = "not during a warning or an alarm";
    return false;
  }
  else if (a == "buz.beep") { core.test_sound(6); reply = "beep"; }
  else if (a == "buz.test") { core.test_sound(n); reply = "playing"; }
  else if (a == "led.test") { led_test = constrain(n, 0, 4); led_test_until = millis() + 15000; reply = n ? "LED test for 15 s" : "LEDs back to normal"; }
  else if (a == "oled.test") { oled_test = constrain(n, 0, 4); oled_test_until = millis() + 15000; reply = n ? "screen test for 15 s" : "screen back to normal"; }
  else if (a == "radar.cal") { radar_cal_start(v.length() ? n : 30); reply = "calibration: leave the room"; }
  else if (a == "radar.cal_abort") { radar_cal_abort(); reply = "calibration aborted"; }
  else if (a == "radar.read") { radar_read_params(); radar_ask_version(); reply = "sensor read"; }
  else if (a == "radar.restart") { radar_restart(); reply = "sensor restarting"; }
  else if (a == "radar.eng") { radar_engineering(n != 0); reply = n ? "per-gate data on" : "per-gate data off"; }
  else if (a == "radar.apply") { radar_apply_settings(); reply = "thresholds written to the sensor"; }
  else if (a == "cfg.reset") {
    reply = "settings reset to defaults: rebooting";
    Con.println("Settings reset to defaults from the web page");
    settings_reset();
    restart_at = millis() + 1000;                    // after the reply has gone out (loop())
  }
  else if (a == "sys.reboot") { reply = "rebooting"; Con.println("Reboot asked from the web page"); delay(200); ESP.restart(); }
  else { reply = "unknown action"; return false; }
  return true;
}

// ---- Console (USB and the web page) -----------------------------------------------------------------------
static const char HELP[] =
    "Commands: h help | s status | on | off | hold <min> | test (warning sounds) | silence\n"
    "  rv radar version | rp read params | ra apply thresholds | re0/re1 per-gate data | rr restart radar\n"
    "  rc<s> calibrate (leave the room within s seconds) | R reboot";

bool command(const String &raw, String &reply) {
  String c = raw;
  c.trim();
  if (c.isEmpty()) return false;
  if (c == "h" || c == "?") { reply = HELP; return true; }
  if (c == "s") {
    ShellyReading sh;
    shelly_reading(sh);
    char buf[200];
    snprintf(buf, sizeof(buf), "phase %s, shelly %d, sense %d, link %s, seen %d (%us ago), cut in %ds, ceiling %us, "
             "lease %us, fault %s, script id %d, %s",
             phase_name(core.phase()), core.shelly_on(), sense_live, core.link_ok() ? "ok" : "lost", core.present(),
             (unsigned) core.seen_ago_s(), (int) core.away_due_s(), (unsigned) core.ceil_left_s(),
             (unsigned) core.lease_left_s(), fault_code(core.fault()), sh.script_id, sh.error);
    reply = buf;
    return true;
  }
  if (c == "on") return do_action("bar.on", "", reply);
  if (c == "off") return do_action("bar.off", "", reply);
  if (c.startsWith("hold")) return do_action("hold.set", c.substring(4), reply);
  if (c == "test") return do_action("warn.test", "", reply);
  if (c == "silence") return do_action("alarm.silence", "", reply);
  if (c == "R") return do_action("sys.reboot", "", reply);
  if (radar_command(c, reply)) return true;
  reply = "unknown command '" + c + "' - h for help";
  return false;
}

// ---- Home Assistant hooks (ha_iface.h) --------------------------------------------------------------------
void ha_snapshot(HaSnapshot &s) {
  ShellyReading sh;
  shelly_reading(sh);
  s.phase = phase_name(core.phase());
  s.shelly_on = core.shelly_on();
  s.sense_live = sense_live;
  s.seen = core.present();
  s.alarm = core.alarm_on();
  s.alarm_code = fault_code(core.fault());
  s.alarm_text = fault_text(core.fault());
  s.watts = sh.w;
  s.shelly_temp_c = sh.t;
  s.away_due_s = core.away_due_s();
  s.hold_left_s = core.hold_left_s();
  s.ceil_left_s = core.ceil_left_s();
  s.warn_left_s = core.warn_left_s();
  FrigateStatus fs;
  frigate_status(fs);
  s.camera = !fs.on ? "off" : !fs.live ? "radar only" : fs.unsure ? "unsure" : fs.veto ? "veto"
           : fs.owner_ago_s < 0 ? "grace" : "owner seen";
  s.camera_owner_ago_s = fs.owner_ago_s;
}

void ha_command(HaCmd cmd) {
  Con.printf("%8u  Home Assistant: bar %s\n", (unsigned) millis(), cmd == HaCmd::BarOn ? "ON" : "OFF");
  if (cmd == HaCmd::BarOn) core.remote_on(); else core.remote_off();
}

const char *device_mac() { return mac_str; }
const char *firmware_version() { return fw_full; }

// ---- Setup and loop ---------------------------------------------------------------------------------------
// ---- A new firmware runs on trial -------------------------------------------------------------------------
// The Arduino core would mark a freshly uploaded image valid at start-up; this hook stops it. The image is marked
// valid only once it has run 60 s with the house WiFi up and the main loop (web page included) running. If it
// restarts before that - a crash, a hang caught by the watchdog, a power cut - the bootloader goes back to the
// previous firmware by itself. No new upload is accepted while on trial.
extern "C" bool verifyRollbackLater() { return true; }

bool firmware_on_trial() { return on_trial; }

static void trial_begin() {
  esp_ota_img_states_t st;
  const esp_partition_t *run = esp_ota_get_running_partition();
  on_trial = run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  if (on_trial) Con.println("New firmware on trial: confirmed after 60 s on the house WiFi, else the previous one returns");
}

static void trial_loop(uint32_t now) {
  if (!on_trial || now < 60000 || WiFi.status() != WL_CONNECTED) return;
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
    on_trial = false;
    Con.printf("%8u  New firmware confirmed\n", (unsigned) now);
  }
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);
  snprintf(fw_full, sizeof(fw_full), "%s", FW_VERSION);
  uint64_t mac = ESP.getEfuseMac();
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", (uint8_t) mac, (uint8_t) (mac >> 8),
           (uint8_t) (mac >> 16), (uint8_t) (mac >> 24), (uint8_t) (mac >> 32), (uint8_t) (mac >> 40));
  Con.println();
  Con.printf("=== Bench node %s, commit %s ===\n", FW_VERSION, FW_COMMIT);
  Con.printf("MAC %s, reset reason %d\n", mac_str, (int) esp_reset_reason());
  trial_begin();

  settings_begin();
  apply_settings();

  for (int k = 0; k < 3; k++) {
    pinMode(KEY_PINS[k], INPUT_PULLUP);
    ledcSetup(LED_CH[k], 5000, 8);
    ledcAttachPin(LED_PINS[k], LED_CH[k]);
    ledcWrite(LED_CH[k], 0);
  }
  pinMode(BAR_LIVE, INPUT_PULLUP);                 // an unplugged cable reads "dead"
  pinMode(RADAR_OUT, INPUT_PULLDOWN);
  ledcSetup(BUZ_CH, 2200, 8);
  ledcAttachPin(BUZZER, BUZ_CH);
  ledcWrite(BUZ_CH, 0);
  sense_live = digitalRead(BAR_LIVE) == LOW;

  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  if (oled_begin(setting_int("oled_contrast"))) {
    Con.printf("OLED at 0x%02X\n", oled_address());
    screen_settings(true);
    draw_boot_screen();
  }
  else Con.println("OLED NOT FOUND");
  screen_light.begin(millis());

  core.begin(millis());
  restore_state();
  shelly_begin();                                  // before the radar: its idle hook uses the lease link
  radar_set_idle(service_io);
  radar_begin(RADAR_to_S3, S3_to_RADAR);

  portal_begin();
  mqtt_begin();
  Con.println(HELP);
  Con.printf("Start-up took %u ms\n", (unsigned) millis());
  boot_until = millis() + 1500;                    // the boot screen stays up 1.5 s after start-up
}

void loop() {
  static String line;
  static uint32_t next_screen = 0, next_status = 0;
  const uint32_t now = millis();

  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (line.length()) { String reply; command(line, reply); if (reply.length()) Con.println(reply); }
      line = "";
    } else if (line.length() < 40) line += ch;
  }

  radar_loop();
  poll_inputs();
  bool radar_ok = radar_alive(setting_int("radar_dead_s") * 1000);
  core.tick(now, present_now(), radar_ok, sense_live, quiet_now());
  run_lease();
  drive_outputs();
  keep_state();
  portal_loop();
  mqtt_loop();
  trial_loop(now);
  if (restart_at && (int32_t) (now - restart_at) >= 0) { Con.println("Restarting"); delay(100); ESP.restart(); }

  if (!time_started && WiFi.status() == WL_CONNECTED) {
    configTzTime(TZ_MONTREAL, "pool.ntp.org", "time.cloudflare.com");
    time_started = true;
  }

  if (oled_address() && (int32_t) (now - next_screen) >= 0) next_screen = now + (draw_screen() ? 100 : 250);

  if ((int32_t) (now - next_status) >= 0) {
    next_status = now + 60000;
    String reply;
    command("s", reply);
    Con.printf("%8u  %s\n", (unsigned) now, reply.c_str());
  }
  delay(1);
}
