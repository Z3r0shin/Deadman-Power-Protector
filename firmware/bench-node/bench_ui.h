// Bench node user interface: three keys (ON, OVERRIDE, FN), their LEDs, the buzzer and the OLED pages.
//
// Plain C++ with no ESPHome dependency, so it runs unchanged in the host tests (tests/test_ui.cpp).
// Time is passed in as milliseconds (millis()); differences use unsigned arithmetic, so wrap-around is safe.
//
// This class decides what a key press means and what the LEDs, buzzer and screen show. It does not talk to
// the Shelly. It raises requests (turn ON, turn OFF, ceiling reset, hold started) that the caller acts on.
// Part of the ESPHome draft, not used: the bench node firmware (firmware/src/bench_core.h) replaced it.
#pragma once

#include <cstdint>

namespace bench {

enum class Key : uint8_t { On = 0, Override = 1, Fn = 2 };
enum class Page : uint8_t { Dark, Status, Timers, Sensor, Menu, HoldToCut, Warning, Toast };
enum class Toast : uint8_t { None, TurnedOn, TurnedOff, CeilingReset, HoldStarted, HoldCancelled, MenuUnchanged,
                             WarningAnswered, AlarmSilenced };
enum class Tone : uint8_t { None, Click, Confirm, Cut, Chirp, Alarm };

struct Requests {
  bool turn_on = false;        // bar ON (also a physical press: resets the ceiling)
  bool turn_off = false;       // bar OFF
  bool ceiling_reset = false;  // physical press while live: restart the ceiling
  bool hold_set = false;       // hold Y changed; hold_ms holds the new length (0 = cancelled)
  uint32_t hold_ms = 0;
};

class Ui {
 public:
  // Timings. Starting values, to be tuned; the lease build makes them adjustable from the portal.
  static constexpr uint32_t SHORT_MAX_MS = 400;      // a press shorter than this is a short press
  static constexpr uint32_t LONG_MS = 700;           // OVERRIDE and FN long press
  static constexpr uint32_t HOLD_TO_CUT_MS = 1500;   // ON held this long cuts the bar
  static constexpr uint32_t MENU_IDLE_MS = 15000;    // menu untouched this long closes with no change
  static constexpr uint32_t SCREEN_MS = 20000;       // screen stays awake this long after the last event
  static constexpr uint32_t TOAST_MS = 2000;
  static constexpr uint32_t STEP_MIN = 5;            // menu step in minutes mode
  static constexpr uint32_t STEP_HOUR_MIN = 60;      // menu step in hours mode
  static constexpr uint32_t DEFAULT_HOLD_MIN = 30;
  static constexpr uint32_t CEILING_MS = 4UL * 60 * 60 * 1000;
  static constexpr uint32_t MAX_HOLD_MIN = CEILING_MS / 60000;  // a hold cannot outlast the ceiling

  // ---- inputs ---------------------------------------------------------------------------------------

  void set_bar_live(bool live, uint32_t now) {
    if (live && !bar_live_) ceiling_start_ = now;
    if (!live) { hold_end_ = 0; hold_active_ = false; warning_active_ = false; }
    bar_live_ = live;
  }

  void key_down(Key k, uint32_t now) {
    uint8_t i = idx(k);
    down_[i] = true;
    down_at_[i] = now;
    long_done_[i] = false;
    now_ = now;
    was_dark_ = page() == Page::Dark;
    wake(now);
    tone_ = Tone::Click;
  }

  // Returns what the caller must do because of this release.
  Requests key_up(Key k, uint32_t now) {
    Requests r;
    uint8_t i = idx(k);
    if (!down_[i]) return r;
    down_[i] = false;
    uint32_t held = now - down_at_[i];
    if (long_done_[i]) return r;  // its long action already ran in tick()
    if (k == Key::On && held >= SHORT_MAX_MS) { hold_to_cut_ = false; return r; }  // let go before the cut
    if (held >= LONG_MS) return r;
    short_press(k, now, r);
    return r;
  }

  // Call often (every 10-20 ms). Runs the held-key actions and the timeouts.
  Requests tick(uint32_t now) {
    Requests r;
    now_ = now;
    // ON held: hold-to-cut, only while the bar is live.
    if (down_[0] && !long_done_[0] && bar_live_) {
      uint32_t held = now - down_at_[0];
      hold_to_cut_ = held >= SHORT_MAX_MS;
      if (held >= HOLD_TO_CUT_MS) {
        long_done_[0] = true;
        hold_to_cut_ = false;
        menu_open_ = false;
        r.turn_off = true;
        show_toast(Toast::TurnedOff, now);
        tone_ = Tone::Cut;
      }
    }
    // OVERRIDE long: in the menu, switch minutes <-> hours.
    if (down_[1] && !long_done_[1] && now - down_at_[1] >= LONG_MS) {
      long_done_[1] = true;
      if (menu_open_) {
        hours_ = !hours_;
        if (hours_) menu_min_ = round_up(menu_min_ == 0 ? 60 : menu_min_, 60);
        clamp_menu();
        menu_touched_ = now;
        menu_changed_ = true;
      }
    }
    // FN long: silence the alarm; otherwise put the screen to sleep.
    if (down_[2] && !long_done_[2] && now - down_at_[2] >= LONG_MS) {
      long_done_[2] = true;
      if (alarm_active_ && !alarm_silenced_) {
        alarm_silenced_ = true;
        show_toast(Toast::AlarmSilenced, now);
      } else if (menu_open_) {
        close_menu(now, false);
      } else {
        screen_until_ = now;
        page_ = Page::Status;
      }
    }
    // Menu left untouched: close with no change (fail-safe: no hold is set by accident).
    if (menu_open_ && now - menu_touched_ >= MENU_IDLE_MS) close_menu(now, false);
    // Hold Y ran out.
    if (hold_active_ && (int32_t)(now - hold_end_) >= 0) hold_active_ = false;
    // Warning ran out unanswered: cut.
    if (warning_active_ && (int32_t)(now - warning_end_) >= 0) {
      warning_active_ = false;
      r.turn_off = true;
      show_toast(Toast::TurnedOff, now);
      tone_ = Tone::Cut;
    }
    return r;
  }

  // Warning before an automatic cut. The test build starts it from the portal.
  void start_warning(uint32_t length_ms, uint32_t now) {
    if (!bar_live_) return;
    warning_active_ = true;
    warning_end_ = now + length_ms;
    wake(now);
  }

  void set_alarm(bool active) {
    if (!active) alarm_silenced_ = false;
    alarm_active_ = active;
  }

  // ---- outputs --------------------------------------------------------------------------------------

  // LED level 0..1 for one key. "On" levels are scaled by the caller's brightness setting.
  float led(Key k, uint32_t now) const {
    uint8_t i = idx(k);
    if (alarm_active_) return ((now / 150) % 3) == i ? 1.0f : 0.0f;                // chase
    if (warning_active_) return ((now / 250) % 2) == 0 ? 1.0f : 0.0f;              // all blink, 2 Hz
    if (menu_open_) {
      if (k == Key::On) return ((now / 500) % 2) == 0 ? 1.0f : 0.15f;              // "press ON to confirm"
      return 1.0f;                                                                 // the +/- keys
    }
    if (!bar_live_) return 0.0f;                                                   // dark = bar dead
    if (k == Key::On && hold_to_cut_) {
      uint32_t held = now - down_at_[0];
      float p = (float)(held - SHORT_MAX_MS) / (float)(HOLD_TO_CUT_MS - SHORT_MAX_MS);
      return p >= 1.0f ? 0.0f : 1.0f - p;                                          // fades out as the cut nears
    }
    if (k == Key::Override && hold_active_) return breathe(now);
    return 1.0f;                                                                   // lit = bar live
  }

  Page page() const {
    if (warning_active_) return Page::Warning;
    if (hold_to_cut_) return Page::HoldToCut;
    if (menu_open_) return Page::Menu;
    if (toast_ != Toast::None && now_ - toast_at_ < TOAST_MS) return Page::Toast;
    if ((int32_t)(screen_until_ - now_) > 0 || alarm_active_) return page_;
    return Page::Dark;
  }

  // Tone to play now, or Tone::None. Reading it clears it.
  Tone take_tone() { Tone t = tone_; tone_ = Tone::None; return t; }

  // Warning chirp cadence: every 5 s, then every 2 s under 20 s left, then continuous under 10 s left.
  bool warning_sound(uint32_t now) const {
    if (!warning_active_) return false;
    uint32_t left = warning_end_ - now;
    if (left <= 10000) return true;
    uint32_t period = left <= 20000 ? 2000 : 5000;
    return (left % period) < 120;
  }

  bool bar_live() const { return bar_live_; }
  bool menu_open() const { return menu_open_; }
  bool menu_hours() const { return hours_; }
  uint32_t menu_minutes() const { return menu_min_; }
  bool hold_active() const { return hold_active_; }
  uint32_t hold_left_ms(uint32_t now) const { return hold_active_ ? hold_end_ - now : 0; }
  uint32_t ceiling_left_ms(uint32_t now) const {
    if (!bar_live_) return 0;
    uint32_t used = now - ceiling_start_;
    return used >= CEILING_MS ? 0 : CEILING_MS - used;
  }
  uint32_t warning_left_ms(uint32_t now) const { return warning_active_ ? warning_end_ - now : 0; }
  float hold_to_cut_progress(uint32_t now) const {
    if (!hold_to_cut_) return 0.0f;
    float p = (float)(now - down_at_[0] - SHORT_MAX_MS) / (float)(HOLD_TO_CUT_MS - SHORT_MAX_MS);
    return p > 1.0f ? 1.0f : p;
  }
  Toast toast() const { return toast_; }
  bool alarm_active() const { return alarm_active_; }
  bool alarm_silenced() const { return alarm_silenced_; }

 private:
  static uint8_t idx(Key k) { return (uint8_t)k; }
  static uint32_t round_up(uint32_t v, uint32_t step) { return ((v + step - 1) / step) * step; }

  static float breathe(uint32_t now) {
    // Triangle wave, 3 s period, 15 % to 100 %.
    uint32_t t = now % 3000;
    float x = t < 1500 ? t / 1500.0f : (3000 - t) / 1500.0f;
    return 0.15f + 0.85f * x * x;  // squared: looks even to the eye
  }

  void wake(uint32_t now) {
    now_ = now;
    screen_until_ = now + SCREEN_MS;
  }

  void show_toast(Toast t, uint32_t now) {
    toast_ = t;
    toast_at_ = now;
    wake(now);
  }

  void clamp_menu() {
    if (menu_min_ > MAX_HOLD_MIN) menu_min_ = MAX_HOLD_MIN;
  }

  void open_menu(uint32_t now) {
    menu_open_ = true;
    menu_changed_ = false;
    menu_touched_ = now;
    // Start from what is running, else from the last hold that was set.
    menu_min_ = hold_active_ ? round_up((hold_end_ - now) / 60000 + 1, STEP_MIN) : last_hold_min_;
    hours_ = menu_min_ >= 120;
    clamp_menu();
  }

  void close_menu(uint32_t now, bool confirm) {
    menu_open_ = false;
    if (!confirm) { show_toast(Toast::MenuUnchanged, now); return; }
  }

  void short_press(Key k, uint32_t now, Requests &r) {
    // An unanswered warning takes the press, whatever the key: one press keeps him working.
    if (warning_active_) {
      warning_active_ = false;
      r.ceiling_reset = true;
      ceiling_start_ = now;
      show_toast(Toast::WarningAnswered, now);
      tone_ = Tone::Confirm;
      return;
    }
    if (menu_open_) {
      menu_touched_ = now;
      uint32_t step = hours_ ? STEP_HOUR_MIN : STEP_MIN;
      switch (k) {
        case Key::Override: menu_min_ += step; clamp_menu(); menu_changed_ = true; return;
        case Key::Fn: menu_min_ = menu_min_ > step ? menu_min_ - step : 0; menu_changed_ = true; return;
        case Key::On: confirm_menu(now, r); return;
      }
    }
    switch (k) {
      case Key::On:
        if (!bar_live_) {
          r.turn_on = true;
          show_toast(Toast::TurnedOn, now);
        } else {
          r.ceiling_reset = true;
          ceiling_start_ = now;
          show_toast(Toast::CeilingReset, now);
        }
        tone_ = Tone::Confirm;
        return;
      case Key::Override:
        open_menu(now);
        return;
      case Key::Fn:
        // First press wakes the screen on the status page; further presses page through.
        if (was_dark_) page_ = Page::Status;
        else page_ = page_ == Page::Status ? Page::Timers : page_ == Page::Timers ? Page::Sensor : Page::Status;
        return;
    }
  }

  void confirm_menu(uint32_t now, Requests &r) {
    menu_open_ = false;
    r.hold_set = true;
    if (menu_min_ == 0) {
      r.hold_ms = 0;
      if (hold_active_) { hold_active_ = false; show_toast(Toast::HoldCancelled, now); }
      else show_toast(Toast::MenuUnchanged, now);
      return;
    }
    // Setting a hold is a physical press: bar ON if needed, ceiling restarted.
    if (!bar_live_) r.turn_on = true; else r.ceiling_reset = true;
    ceiling_start_ = now;
    last_hold_min_ = menu_min_;
    hold_active_ = true;
    hold_end_ = now + menu_min_ * 60000UL;
    r.hold_ms = menu_min_ * 60000UL;
    show_toast(Toast::HoldStarted, now);
    tone_ = Tone::Confirm;
  }

  bool bar_live_ = false;
  bool down_[3] = {false, false, false};
  bool long_done_[3] = {false, false, false};
  uint32_t down_at_[3] = {0, 0, 0};
  bool hold_to_cut_ = false;

  bool menu_open_ = false;
  bool menu_changed_ = false;
  bool hours_ = false;
  uint32_t menu_min_ = DEFAULT_HOLD_MIN;
  uint32_t menu_touched_ = 0;
  uint32_t last_hold_min_ = DEFAULT_HOLD_MIN;

  bool hold_active_ = false;
  uint32_t hold_end_ = 0;
  uint32_t ceiling_start_ = 0;

  bool warning_active_ = false;
  uint32_t warning_end_ = 0;
  bool alarm_active_ = false;
  bool alarm_silenced_ = false;

  Page page_ = Page::Status;
  bool was_dark_ = true;
  uint32_t screen_until_ = 0;
  uint32_t now_ = 0;
  Toast toast_ = Toast::None;
  uint32_t toast_at_ = 0;
  Tone tone_ = Tone::None;
};

}  // namespace bench
