// The bench node's decisions: absence timer, hold, ceiling mirror, warnings, the bench side of the lease, alarms,
// the keys and menu, LEDs, sounds and screen pages.
//
// Plain C++ with no Arduino or ESPHome dependency, so it runs unchanged in the PC tests (../test/test_core.cpp).
// Time is milliseconds from millis(); every comparison uses wrap-safe unsigned subtraction.
//
// The core never does I/O. Each loop pass the caller:
//   - feeds inputs: tick(now, present, radar_ok, sense_live, quiet), key_down()/key_up(), remote_on()/remote_off();
//   - asks take_op(now) for the next lease request, sends it to the Shelly, and reports lease_result();
//   - reads outputs: led(), tone(), page() and the getters.
// The design and the lease contract are in the Firmware Gospel (docs/firmware-gospel.md).
#pragma once

#include <cstdint>

namespace bench {

enum class Key : uint8_t { On = 0, Override = 1, Fn = 2 };
enum class Op : uint8_t { None, Status, Renew, OnPress, OnRemote, Off };
enum class Phase : uint8_t { Off, On, Hold, Warning, Alarm, Unknown };
enum class Why : uint8_t { None, Absence, Hold, Ceiling, Link };
enum class Fault : uint8_t { None, StuckOn, LostLive, SenseDead, RadarDead };
enum class Page : uint8_t { Dark, Status, Timers, Sensor, Menu, HoldToCut, Warning, Alarm, Toast };
enum class Toast : uint8_t { None, TurnedOn, TurnedOff, CutAbsence, CutHold, CutCeiling, CutLink, CeilingReset,
                             HoldStarted, HoldCancelled, MenuUnchanged, WarningAnswered, AlarmSilenced, ShellyRefused,
                             RemoteOn, RemoteOff, Adopted, CutNotDone };

// One lease answer from the Shelly script (the Firmware Gospel's lease contract).
struct Answer {
  bool ok = false;
  bool ceiling = false;      // why == "ceiling"
  bool on = false;
  uint32_t lease_s = 0;
  uint32_t ceil_s = 0;
};

// A note to play now: hz == 0 means silence.
struct Tone {
  uint16_t hz = 0;
  bool alarm = false;   // alarms play at full volume, even in quiet hours
};

struct Config {
  uint32_t away_s = 1200;          // absence before the cut is due
  uint32_t warn_s = 300;           // warning length before any automatic cut
  uint32_t ceil_s = 14400;         // ceiling after the last ON press or warning answer (the Shelly caps it at 4 h)
  uint32_t lease_s = 300;          // toggle_after asked of the Shelly on each renewal
  uint32_t renew_s = 30;           // renewal period
  uint32_t retry_s = 5;            // retry period after a failed request
  uint32_t status_s = 10;          // status poll period while the bar is off
  uint32_t mismatch_s = 15;        // Shelly vs bar sense disagreement allowed before a fault
  uint32_t link_lost_s = 65;       // no answer this long = link lost (two renewals and a retry)
  uint32_t press_gap_ms = 2000;    // at most one ceiling-reset request per this period
  uint32_t in_flight_max_ms = 15000; // a request unanswered this long is abandoned
  uint32_t short_max_ms = 400;     // shorter than this is a short press
  uint32_t long_ms = 700;          // OVERRIDE and FN long press
  uint32_t hold_to_cut_ms = 1500;  // ON held this long cuts the bar
  uint32_t menu_idle_ms = 15000;   // menu untouched this long closes with no change
  uint32_t screen_ms = 20000;      // screen awake this long after the last event
  uint32_t toast_ms = 2000;
  uint32_t step_min = 5;           // menu step in minutes mode
  uint32_t default_hold_min = 30;
  uint16_t base_hz = 2200;         // the buzzer's chosen frequency
  bool quiet_mutes_final = true;   // in quiet hours, mute the warning sounds from the second mark on
  // Sounds. Every pitch is built from base_hz; a step or a rise is in % of it.
  bool clicks = true;              // key clicks
  uint32_t click_ms = 12;
  bool confirm_on = true;          // short high note when a press is accepted
  uint32_t confirm_ms = 70;
  bool cut_on = true;              // falling sweep at a cut
  uint32_t cut_ms = 450;
  bool m1_on = true;               // mark 1: at the start of a warning
  uint32_t m1_notes = 2, m1_note_ms = 130, m1_step_pct = 20, m1_repeats = 1;
  uint32_t reminder_s = 0;         // repeat mark 1 this often during a warning (0 = once)
  bool m2_on = true;               // mark 2: m2_at_s before the cut
  uint32_t m2_at_s = 60, m2_notes = 3, m2_note_ms = 110, m2_step_pct = 22, m2_repeats = 2;
  bool fin_on = true;              // final beeps: one a second for the last fin_s seconds
  uint32_t fin_s = 15, fin_start_ms = 80, fin_end_ms = 250, fin_rise_pct = 50, fin_double_s = 5;
  uint32_t alarm_hi_pct = 130, alarm_period_ms = 250;
  // LEDs. Levels are 0..1 here; the caller scales them by its brightness settings.
  uint32_t led_live = 0;           // bar live: 0 on, 1 breathe, 2 dim
  uint32_t led_dead = 0;           // bar dead: 0 off, 1 dim, 2 breathe
  uint32_t led_dim_pct = 10;
  uint32_t hold_fx = 0;            // OVERRIDE during a hold: 0 breathe, 1 blink, 2 steady
  uint32_t breathe_ms = 3000, breathe_min_pct = 15;
  uint32_t warn_blink_ms = 1000, warn_fast_ms = 400;   // all LEDs blink in a warning; fast in the final beeps
  uint32_t alarm_fx = 0;           // 0 chase, 1 all blink
  uint32_t alarm_step_ms = 150;
  uint32_t menu_blink_ms = 1000;   // ON blinks while the menu is open
  bool page_timers = true, page_sensor = true;   // the pages FN steps through after the status page
  bool wake_first = false;         // a press on the idle (dark) screen only wakes it (screen mode "on after a key")
};

class Core {
 public:
  Config cfg;

  // ---- inputs ---------------------------------------------------------------------------------------------

  void begin(uint32_t now) {
    now_ = now;
    boot_at_ = now;
    last_seen_ = now;
    last_attempt_ = now - 1000000;   // ask the Shelly at once
  }

  void tick(uint32_t now, bool present, bool radar_ok, bool sense_live, bool quiet) {
    now_ = now;
    quiet_ = quiet;
    radar_ok_ = radar_ok;
    present_ = present && radar_ok;
    sense_ = sense_live;
    if (present_) last_seen_ = now;
    if (test_running_ && now - test_at_ >= test_len_ms()) test_running_ = false;
    if (test_running_ && (warning_active() || alarm_on())) test_running_ = false;   // the real thing wins
    run_keys();
    run_menu();
    run_hold();
    run_warnings();
    // A warning closes an open menu: a hidden menu would time out unseen.
    if (warning_active() && menu_open_) menu_open_ = false;
    // A key already held when a warning starts answers it too: he is at the bench. Not a press that only woke the
    // screen, and not one already held past long_ms (a stuck key must never answer warnings).
    if (warning_active()) {
      for (int i = 0; i < 3; i++) {
        if (down_[i] && !consumed_[i] && !long_done_[i]) { consumed_[i] = true; answer_warning(); break; }
      }
    }
    run_faults();
  }

  void key_down(Key k) {
    uint8_t i = (uint8_t) k;
    down_[i] = true;
    down_at_[i] = now_;
    long_done_[i] = false;
    consumed_[i] = false;
    was_dark_ = page() == Page::Dark;
    wake();
    click_at_ = now_;
    if (warning_active()) {                            // any key answers a warning, and does nothing else
      last_seen_ = now_;                               // a press proves he is there
      consumed_[i] = true;
      answer_warning();
      return;
    }
    if (was_dark_ && cfg.wake_first) {                 // the idle screen: this press only wakes it
      consumed_[i] = true;
      return;
    }
    // Only ON says "I'm here": it restarts the absence clock and the ceiling.
    if (k == Key::On) {
      last_seen_ = now_;
      if (want_on_ && !menu_open_) request_press();
    }
  }

  void key_up(Key k) {
    uint8_t i = (uint8_t) k;
    if (!down_[i]) return;
    down_[i] = false;
    uint32_t held = now_ - down_at_[i];
    if (consumed_[i] || long_done_[i]) { if (k == Key::On) hold_to_cut_ = false; return; }
    if (k == Key::On && held >= cfg.short_max_ms) {
      hold_to_cut_ = false;
      // In the menu, and with the bar off, ON means "yes" however long it is pressed.
      if (menu_open_ || !want_on_) { short_press(k); return; }
      toast(Toast::CutNotDone);                        // let go before the cut: say so
      return;
    }
    if (held >= cfg.long_ms) return;
    short_press(k);
  }

  // Home Assistant (convenience only).
  void remote_on() {
    if (want_on_) return;                              // never resets a running ceiling
    switch_on(false);
    toast(Toast::RemoteOn);
  }
  void remote_off() {
    if (!want_on_) return;
    switch_off(Toast::RemoteOff);
  }

  // Web page actions.
  // Web page and Home Assistant are not physical presses: they never reset the ceiling.
  void web_on() {
    if (want_on_) return;
    switch_on(false);
    toast(Toast::TurnedOn);
  }
  void web_off() { if (want_on_) switch_off(Toast::TurnedOff); else { pending_off_ = true; } }
  void web_hold(uint32_t minutes) {
    if (minutes == 0) { if (hold_active_) { hold_active_ = false; toast(Toast::HoldCancelled); } return; }
    set_hold(minutes, false);
  }
  // A bench node restart while the bar was on: given before the first Shelly answer.
  void restore(uint32_t seen_ago_ms, uint32_t hold_left_ms) {
    restore_pending_ = true;
    restore_seen_ago_ms_ = seen_ago_ms;
    restore_hold_ms_ = hold_left_ms;
  }
  uint32_t seen_ago_ms() const { return now_ - last_seen_; }
  uint32_t hold_left_ms() const { return hold_active_ ? (uint32_t) ((int32_t) (hold_end_ - now_) > 0 ? hold_end_ - now_ : 0) : 0; }
  void silence_alarm() {
    if (alarm_on()) { silenced_ = true; toast(Toast::AlarmSilenced); }
    else if (minor_fault()) { fault_silenced_ = true; toast(Toast::AlarmSilenced); }
  }
  // Plays one sound for the web page: 0 the warning sequence, 1 mark 1, 2 mark 2, 3 the final beeps, 4 the cut,
  // 5 the alarm (3 s), 6 a click, 7 the confirm note.
  bool test_sound(uint32_t which = 0) {
    if (warning_active() || alarm_on()) return false;   // never over a real warning or alarm
    test_at_ = now_;
    test_which_ = which > 7 ? 0 : which;
    test_running_ = true;
    return true;
  }
  bool test_playing() const { return test_running_; }
  void wake_screen() { wake(); }

  // ---- lease ----------------------------------------------------------------------------------------------

  // The next request for the Shelly, or Op::None. Only one is in flight at a time.
  Op take_op(uint32_t now) {
    now_ = now;
    if (in_flight_ != Op::None) {
      // An answer that never comes (the link task stuck) must not latch the lease off forever.
      if (now - last_attempt_ < cfg.in_flight_max_ms) return Op::None;
      in_flight_ = Op::None;
      last_failed_ = true;
      fails_++;
    }
    uint32_t since = now - last_attempt_;
    uint32_t gap = last_failed_ ? cfg.retry_s * 1000 : 0;
    Op op = Op::None;
    if (pending_off_ && since >= gap) op = Op::Off;
    else if (pending_press_ && since >= gap) op = Op::OnPress;
    else if (pending_remote_ && since >= gap) op = Op::OnRemote;
    else if (want_on_ && since >= (last_failed_ ? cfg.retry_s : cfg.renew_s) * 1000) op = Op::Renew;
    else if (!want_on_ && since >= (last_failed_ ? cfg.retry_s : cfg.status_s) * 1000) op = Op::Status;
    if (op != Op::None) { in_flight_ = op; last_attempt_ = now; seq_++; }
    return op;
  }

  // The answer to the request take_op() returned. got == false: no answer (timeout, HTTP error, bad JSON).
  // seq: the value of seq() when the request was taken; 0 = the current one. A late answer to an
  // abandoned request is ignored.
  void lease_result(Op op, bool got, const Answer &a, uint32_t now, uint32_t seq = 0) {
    now_ = now;
    if (seq != 0 && seq != seq_) return;
    if (in_flight_ == Op::None) return;
    in_flight_ = Op::None;
    if (!got) {
      last_failed_ = true;
      fails_++;
      return;                                          // the pending flag stays set: retried in retry_s
    }
    last_failed_ = false;
    fails_ = 0;
    last_ok_ = now;
    ever_ok_ = true;
    shelly_on_ = a.on ? 1 : 0;
    // A status answer applies no timer (lease_s 0): the script keeps one of at most lease_s running.
    if (a.lease_s) lease_until_ = now + a.lease_s * 1000;
    else if (a.on && (int32_t) (lease_until_ - now) <= 0) lease_until_ = now + cfg.lease_s * 1000;
    if (a.on) { ceil_end_ = now + a.ceil_s * 1000; ceil_known_ = true; }
    else ceil_known_ = false;

    switch (op) {
      case Op::Off:
        pending_off_ = false;
        break;
      case Op::OnPress:
        pending_press_ = false;
        if (!a.ok || !a.on) refused(a);
        break;
      case Op::OnRemote:
        pending_remote_ = false;
        if (!a.ok || !a.on) refused(a);
        break;
      case Op::Renew:
        if (!a.ok || !a.on) refused(a);
        break;
      case Op::Status:
        if (a.on && !want_on_ && !pending_off_ && (off_at_ == 0 || now - off_at_ > cfg.status_s * 1000)) {
          // Switched on from elsewhere (the Shelly app or page): take it over, with a warning before any cut.
          want_on_ = true;
          last_seen_ = now;
          if (restore_pending_) {
            // The bench node restarted while the bar was on: resume the absence clock and the hold.
            restore_pending_ = false;
            uint32_t ago = restore_seen_ago_ms_;
            if (ago > cfg.away_s * 1000) ago = cfg.away_s * 1000;
            last_seen_ = now - ago;
            // The hold carries on to its end; its total restarts at what was left.
            if (restore_hold_ms_) {
              hold_active_ = hold_set_ = true;
              hold_end_ = now + restore_hold_ms_;
              hold_len_s_ = (restore_hold_ms_ + 999) / 1000;   // rounded up, as hold_left_s() is
            }
          }
          toast(Toast::Adopted);
        }
        break;
      case Op::None:
        break;
    }
    if (want_on_ && !a.on && op != Op::OnPress && op != Op::OnRemote && !pending_press_ && !pending_remote_) {
      // Off from elsewhere (the Shelly app, its own ceiling, a lease that ran out): follow it.
      want_on_ = false;
      clear_session();
    }
  }

  // ---- outputs --------------------------------------------------------------------------------------------

  Phase phase() const {
    if (alarm_on()) return Phase::Alarm;
    if (!want_on_ && !ever_ok_) return Phase::Unknown;   // no Shelly answer yet: never claim "off"
    if (!want_on_) return Phase::Off;
    if (warning_active()) return Phase::Warning;
    if (hold_active_) return Phase::Hold;
    return Phase::On;
  }

  bool want_on() const { return want_on_; }
  int shelly_on() const { return ever_ok_ ? shelly_on_ : -1; }
  bool sense() const { return sense_; }
  bool present() const { return present_; }
  bool radar_ok() const { return radar_ok_; }
  bool link_ok() const { return ever_ok_ && now_ - last_ok_ <= cfg.link_lost_s * 1000; }
  bool ever_answered() const { return ever_ok_; }
  uint32_t since_answer_s() const { return ever_ok_ ? (now_ - last_ok_) / 1000 : 0; }
  uint32_t seen_ago_s() const { return (now_ - last_seen_) / 1000; }
  uint32_t fails() const { return fails_; }
  uint32_t seq() const { return seq_; }

  // Seconds until the absence (or hold) cut is due; -1 when the bar is off.
  int32_t away_due_s() const {
    if (!want_on_) return -1;
    int32_t d = (int32_t) (absence_due() - now_);
    return d > 0 ? (d + 999) / 1000 : 0;
  }
  bool hold_active() const { return hold_active_; }
  uint32_t hold_left_s() const { return hold_active_ ? left_s(hold_end_) : 0; }
  uint32_t hold_total_s() const { return hold_active_ ? hold_len_s_ : 0; }
  uint32_t ceil_left_s() const { return want_on_ && ceil_known_ ? left_s(ceil_end_) : 0; }
  uint32_t lease_left_s() const { return want_on_ && ever_ok_ ? left_s(lease_until_) : 0; }

  bool warning_active() const { return warn_why() != Why::None; }
  Why warn_why() const {
    // The warning that ends first is the one shown.
    Why w = Why::None;
    uint32_t end = 0;
    auto take = [&](bool on, Why why, uint32_t e) {
      if (on && (w == Why::None || (int32_t) (e - end) < 0)) { w = why; end = e; }
    };
    take(ah_active_, ah_why_, ah_end_);
    take(ceil_warn_, Why::Ceiling, ceil_end_);
    take(link_warn_, Why::Link, lease_until_);
    return w;
  }
  uint32_t warn_left_s() const { return warning_active() ? left_s(warn_end()) : 0; }
  uint32_t warn_total_s() const { return cfg.warn_s; }

  Fault fault() const {
    if (stuck_on_) return Fault::StuckOn;
    if (lost_live_) return Fault::LostLive;
    if (sense_dead_) return Fault::SenseDead;
    if (!radar_ok_) return Fault::RadarDead;
    return Fault::None;
  }
  bool alarm_on() const { return stuck_on_ || lost_live_; }   // the faults that sound the siren
  bool alarm_silenced() const { return silenced_; }
  // The bar sense or the radar silent: not an emergency (the bar is safe; a dead radar counts as nobody seen),
  // but someone must fix it. Two beeps a minute after the first minute, quiet hours included, and the status page
  // shown, until FN is held.
  bool minor_fault() const { return !alarm_on() && (sense_dead_ || !radar_ok_); }
  bool fault_silenced() const { return fault_silenced_; }

  // LED level 0..1 for one key.
  float led(Key k) const {
    uint8_t i = (uint8_t) k;
    if (alarm_on()) {
      uint32_t st = cfg.alarm_step_ms ? cfg.alarm_step_ms : 150;
      if (cfg.alarm_fx == 1) return ((now_ / st) % 2) == 0 ? 1.0f : 0.0f;                  // all blink
      return ((now_ / st) % 3) == i ? 1.0f : 0.0f;                                          // chase
    }
    if (warning_active()) {
      uint32_t period = warn_left_s() <= cfg.fin_s ? cfg.warn_fast_ms : cfg.warn_blink_ms;
      if (period < 50) period = 50;
      return (now_ % period) < period / 2 ? 1.0f : 0.0f;                                    // all blink
    }
    if (menu_open_) {
      uint32_t p = cfg.menu_blink_ms < 100 ? 100 : cfg.menu_blink_ms;
      if (k == Key::On) return (now_ % p) < p / 2 ? 1.0f : 0.15f;                           // "ON confirms"
      return 1.0f;
    }
    float dim = cfg.led_dim_pct / 100.0f;
    bool live = sense_ || shelly_on() == 1;
    if (!live) return cfg.led_dead == 1 ? dim : cfg.led_dead == 2 ? breathe() : 0.0f;     // bar dead
    if (k == Key::On && hold_to_cut_) {
      float p = hold_to_cut_progress();
      return p >= 1.0f ? 0.0f : 1.0f - p;                                                   // fades as the cut nears
    }
    if (k == Key::Override && hold_active_) {
      uint32_t bp = cfg.breathe_ms < 200 ? 200 : cfg.breathe_ms;
      if (cfg.hold_fx == 1) return (now_ % bp) < bp / 2 ? 1.0f : 0.0f;
      if (cfg.hold_fx == 2) return 1.0f;
      return breathe();
    }
    return cfg.led_live == 1 ? breathe() : cfg.led_live == 2 ? dim : 1.0f;                // bar live
  }

  // What the buzzer plays now.
  Tone tone() const {
    Tone t;
    uint32_t b = cfg.base_hz;
    // Alarm: two-tone siren, never muted, until silenced.
    if (alarm_on() && !silenced_) {
      uint32_t p = cfg.alarm_period_ms ? cfg.alarm_period_ms : 250;
      t.hz = (uint16_t) (((now_ / p) % 2) ? b * cfg.alarm_hi_pct / 100 : b);
      t.alarm = true;
      return t;
    }
    if (test_running_ && !warning_active()) return test_tone();   // a real warning always wins
    if (warning_active()) {
      uint32_t left_ms = (uint32_t) (warn_end() - now_);
      bool mute = quiet_ && cfg.quiet_mutes_final;
      uint32_t m2 = cfg.m2_at_s * 1000;
      uint32_t since = now_ - warn_start();
      // Mark 1 always plays at the start; its reminders only until mark 2.
      if (cfg.m1_on && (since < m1_ms() || left_ms > m2)) {
        if (cfg.reminder_s && since >= m1_ms()) since %= cfg.reminder_s * 1000;
        Tone x = pattern(since, b, cfg.m1_notes, cfg.m1_note_ms, cfg.m1_step_pct, cfg.m1_repeats);
        if (x.hz) return x;
      }
      if (cfg.m2_on && !mute && left_ms <= m2) {
        Tone x = pattern(m2 - left_ms, b, cfg.m2_notes, cfg.m2_note_ms, cfg.m2_step_pct, cfg.m2_repeats);
        if (x.hz) return x;
      }
      if (cfg.fin_on && !mute && left_ms <= cfg.fin_s * 1000) return final_beep(left_ms);
      return t;
    }
    if (minor_fault() && !fault_silenced_ && minor_since_ && now_ - minor_since_ >= 60000) {   // quiet hours too
      uint32_t ph = (now_ - minor_since_) % 60000;
      if (ph < 120 || (ph >= 240 && ph < 360)) { t.hz = (uint16_t) b; return t; }   // two short beeps
    }
    if (cfg.cut_on && cut_at_ != 0 && now_ - cut_at_ < cfg.cut_ms && !(quiet_ && cfg.quiet_mutes_final))
      return cut_sweep(now_ - cut_at_);
    if (cfg.confirm_on && confirm_at_ != 0 && now_ - confirm_at_ < cfg.confirm_ms) { t.hz = (uint16_t) (b * 6 / 5); return t; }
    if (cfg.clicks && click_at_ != 0 && now_ - click_at_ < cfg.click_ms) { t.hz = (uint16_t) b; return t; }
    return t;
  }

  Page page() const {
    if (alarm_on()) return Page::Alarm;
    if (warning_active()) return Page::Warning;
    if (hold_to_cut_) return Page::HoldToCut;
    if (menu_open_) return Page::Menu;
    if (toast_ != Toast::None && now_ - toast_at_ < cfg.toast_ms) return Page::Toast;
    if ((int32_t) (screen_until_ - now_) > 0) return page_;
    return Page::Dark;
  }
  Toast last_toast() const { return toast_; }
  bool menu_open() const { return menu_open_; }
  bool menu_hours() const { return hours_; }
  uint32_t menu_minutes() const { return menu_min_; }
  float hold_to_cut_progress() const {
    if (!hold_to_cut_) return 0.0f;
    float p = (float) (now_ - down_at_[0] - cfg.short_max_ms) / (float) (cfg.hold_to_cut_ms - cfg.short_max_ms);
    return p < 0 ? 0 : p > 1.0f ? 1.0f : p;
  }
  bool key_is_down(Key k) const { return down_[(uint8_t) k]; }
  uint32_t max_hold_min() const { return cfg.ceil_s / 60; }

 private:
  // ---- helpers --------------------------------------------------------------------------------------------
  uint32_t left_s(uint32_t end) const {
    int32_t d = (int32_t) (end - now_);
    return d > 0 ? (uint32_t) (d + 999) / 1000 : 0;
  }
  static uint32_t round_up(uint32_t v, uint32_t s) { return ((v + s - 1) / s) * s; }
  void wake() { screen_until_ = now_ + cfg.screen_ms; }
  void toast(Toast t) { toast_ = t; toast_at_ = now_; wake(); }

  float breathe() const {
    uint32_t period = cfg.breathe_ms < 200 ? 200 : cfg.breathe_ms, half = period / 2;
    uint32_t t = now_ % period;
    float x = t < half ? (float) t / half : (float) (period - t) / half;
    float lo = cfg.breathe_min_pct / 100.0f;
    return lo + (1.0f - lo) * x * x;                       // squared: looks even to the eye
  }

  uint32_t absence_due() const {
    uint32_t due = last_seen_ + cfg.away_s * 1000;
    if (hold_set_ && (int32_t) (hold_end_ - due) > 0) due = hold_end_;
    return due;
  }
  uint32_t warn_end() const {
    switch (warn_why()) {
      case Why::Absence: case Why::Hold: return ah_end_;
      case Why::Ceiling: return ceil_end_;
      case Why::Link: return lease_until_;
      default: return now_;
    }
  }
  uint32_t warn_start() const {
    switch (warn_why()) {
      case Why::Absence: case Why::Hold: return ah_start_;
      case Why::Ceiling: return ceil_warn_start_;
      case Why::Link: return link_warn_start_;
      default: return now_;
    }
  }

  // ---- sounds ---------------------------------------------------------------------------------------------
  // n rising notes, each note_ms long with a gap of a third of a note, each step_pct higher than the one before;
  // the group is played reps times, with one note of silence between groups. Silence once it is done.
  static Tone pattern(uint32_t t, uint32_t base, uint32_t n, uint32_t note_ms, uint32_t step_pct, uint32_t reps) {
    Tone x;
    if (n == 0 || reps == 0 || note_ms == 0) return x;
    uint32_t slot = note_ms + note_ms / 3, group = n * slot + note_ms;
    if (t >= group * reps) return x;
    uint32_t r = t % group, i = r / slot;
    if (i < n && r % slot < note_ms) x.hz = (uint16_t) (base * (100 + i * step_pct) / 100);
    return x;
  }
  static uint32_t pattern_ms(uint32_t n, uint32_t note_ms, uint32_t reps) {
    return (n * (note_ms + note_ms / 3) + note_ms) * reps;
  }
  // One beep a second for the last fin_s seconds: longer and higher as the cut nears; double beeps at the end.
  Tone final_beep(uint32_t left_ms) const {
    Tone x;
    uint32_t n = cfg.fin_s ? cfg.fin_s : 1;
    uint32_t sec = (left_ms + 999) / 1000;
    if (sec > n) sec = n;
    uint32_t in = 1000 - (left_ms % 1000 == 0 ? 1000 : left_ms % 1000);   // ms into this second
    uint32_t k = n - sec, span = n > 1 ? n - 1 : 1;                         // k = 0 at the first beep
    int32_t len = (int32_t) cfg.fin_start_ms +
                  ((int32_t) cfg.fin_end_ms - (int32_t) cfg.fin_start_ms) * (int32_t) k / (int32_t) span;
    uint32_t hz = cfg.base_hz + cfg.base_hz * cfg.fin_rise_pct / 100 * k / span;
    if (sec <= cfg.fin_double_s) {
      if (in < 110 || (in >= 170 && in < 280)) x.hz = (uint16_t) (hz * 9 / 8);
    } else if ((int32_t) in < len) {
      x.hz = (uint16_t) hz;
    }
    return x;
  }
  Tone cut_sweep(uint32_t t) const {
    Tone x;
    uint32_t len = cfg.cut_ms ? cfg.cut_ms : 1;
    x.hz = (uint16_t) (cfg.base_hz * 6 / 5 - t * cfg.base_hz / 2 / len);     // 1.2x down to 0.7x
    return x;
  }
  uint32_t m1_ms() const { return pattern_ms(cfg.m1_notes, cfg.m1_note_ms, cfg.m1_repeats); }
  uint32_t m2_ms() const { return pattern_ms(cfg.m2_notes, cfg.m2_note_ms, cfg.m2_repeats); }
  uint32_t test_len_ms() const {
    switch (test_which_) {
      case 1: return m1_ms();
      case 2: return m2_ms();
      case 3: return cfg.fin_s * 1000;
      case 4: return cfg.cut_ms;
      case 5: return 3000;
      case 6: return cfg.click_ms;
      case 7: return cfg.confirm_ms;
      default: return m1_ms() + 600 + m2_ms() + 600 + cfg.fin_s * 1000;
    }
  }
  Tone test_tone() const {
    uint32_t t = now_ - test_at_, b = cfg.base_hz;
    Tone x;
    switch (test_which_) {
      case 1: return pattern(t, b, cfg.m1_notes, cfg.m1_note_ms, cfg.m1_step_pct, cfg.m1_repeats);
      case 2: return pattern(t, b, cfg.m2_notes, cfg.m2_note_ms, cfg.m2_step_pct, cfg.m2_repeats);
      case 3: return t < cfg.fin_s * 1000 ? final_beep(cfg.fin_s * 1000 - t) : x;
      case 4: return t < cfg.cut_ms ? cut_sweep(t) : x;
      case 5:
        x.hz = (uint16_t) (((now_ / (cfg.alarm_period_ms ? cfg.alarm_period_ms : 250)) % 2) ? b * cfg.alarm_hi_pct / 100 : b);
        x.alarm = true;
        return x;
      case 6: x.hz = (uint16_t) b; return x;
      case 7: x.hz = (uint16_t) (b * 6 / 5); return x;
      default: {
        // The whole warning, compressed: mark 1, mark 2, then the final beeps.
        uint32_t a = m1_ms(), m = m2_ms();
        if (t < a) return pattern(t, b, cfg.m1_notes, cfg.m1_note_ms, cfg.m1_step_pct, cfg.m1_repeats);
        if (t < a + 600) return x;
        t -= a + 600;
        if (t < m) return pattern(t, b, cfg.m2_notes, cfg.m2_note_ms, cfg.m2_step_pct, cfg.m2_repeats);
        if (t < m + 600) return x;
        t -= m + 600;
        return t < cfg.fin_s * 1000 ? final_beep(cfg.fin_s * 1000 - t) : x;
      }
    }
  }

  // ---- on, off, press --------------------------------------------------------------------------------------
  void switch_on(bool press) {
    want_on_ = true;
    last_seen_ = now_;
    clear_session();
    if (press) pending_press_ = true; else pending_remote_ = true;
    pending_off_ = false;
    ceil_known_ = false;
  }
  void switch_off(Toast why) {
    want_on_ = false;
    pending_off_ = true;
    pending_press_ = pending_remote_ = false;
    off_at_ = now_;
    clear_session();
    toast(why);
  }
  void cut(Toast why) {
    switch_off(why);
    cut_at_ = now_;
  }
  void clear_session() {
    hold_active_ = hold_set_ = false;
    ah_active_ = ceil_warn_ = link_warn_ = false;
    menu_open_ = false;
  }
  void request_press() {
    if (now_ - last_press_req_ < cfg.press_gap_ms && last_press_req_ != 0) return;
    last_press_req_ = now_;
    pending_press_ = true;
    if (ceil_known_) ceil_end_ = now_ + cfg.ceil_s * 1000;   // optimistic; the Shelly's answer is the truth
    ceil_warn_ = false;
  }
  void refused(const Answer &a) {
    if (want_on_) {
      want_on_ = false;
      off_at_ = now_;
      clear_session();
      if (a.ceiling) { cut_at_ = now_; toast(Toast::CutCeiling); } else toast(Toast::ShellyRefused);
    }
    pending_press_ = pending_remote_ = false;
  }
  void answer_warning() {
    Why w = warn_why();
    if (w == Why::Link) return;                        // nothing a key can do: the Shelly is out of reach
    last_seen_ = now_;
    ah_active_ = false;
    if (w == Why::Ceiling || w == Why::Absence || w == Why::Hold) { last_press_req_ = 0; request_press(); }
    confirm_at_ = now_;
    toast(Toast::WarningAnswered);
  }

  // ---- hold and menu ---------------------------------------------------------------------------------------
  // press: set from the keys (a physical press: restarts the ceiling) or from the web page (does not).
  void set_hold(uint32_t minutes, bool press) {
    if (minutes > max_hold_min()) minutes = max_hold_min();
    last_hold_min_ = minutes;
    hold_chosen_ = true;
    if (!want_on_) switch_on(press);                   // clears the session: the hold is set after it
    else if (press) { last_press_req_ = 0; request_press(); }
    hold_active_ = hold_set_ = true;
    hold_end_ = now_ + minutes * 60000;
    hold_len_s_ = minutes * 60;
    ah_active_ = false;
    confirm_at_ = now_;
    toast(Toast::HoldStarted);
  }
  void run_hold() {
    if (hold_active_ && (int32_t) (now_ - hold_end_) >= 0) hold_active_ = false;   // hold_set_ stays: due = max()
  }
  void open_menu() {
    menu_open_ = true;
    menu_touched_ = now_;
    menu_min_ = hold_active_ ? round_up(hold_left_s() / 60 + 1, cfg.step_min) : hold_chosen_ ? last_hold_min_ : cfg.default_hold_min;
    if (menu_min_ > max_hold_min()) menu_min_ = max_hold_min();
    hours_ = menu_min_ >= 120;
  }
  void run_menu() {
    if (menu_open_ && now_ - menu_touched_ >= cfg.menu_idle_ms) { menu_open_ = false; toast(Toast::MenuUnchanged); }
  }
  void confirm_menu() {
    menu_open_ = false;
    if (menu_min_ == 0) {
      if (hold_active_) { hold_active_ = hold_set_ = false; toast(Toast::HoldCancelled); }
      else toast(Toast::MenuUnchanged);
      return;
    }
    set_hold(menu_min_, true);
  }

  // ---- keys -----------------------------------------------------------------------------------------------
  void run_keys() {
    if (menu_open_) hold_to_cut_ = false;            // the menu owns ON while it is open
    // ON held: hold-to-cut, only while the bar is on.
    if (down_[0] && !long_done_[0] && !consumed_[0] && want_on_ && !menu_open_) {
      uint32_t held = now_ - down_at_[0];
      hold_to_cut_ = held >= cfg.short_max_ms;
      if (held >= cfg.hold_to_cut_ms) { long_done_[0] = true; hold_to_cut_ = false; switch_off(Toast::TurnedOff); cut_at_ = now_; }
    }
    // OVERRIDE long, in the menu: minutes <-> hours.
    if (down_[1] && !long_done_[1] && !consumed_[1] && now_ - down_at_[1] >= cfg.long_ms) {
      long_done_[1] = true;
      if (menu_open_) {
        hours_ = !hours_;
        if (hours_) menu_min_ = round_up(menu_min_ == 0 ? 60 : menu_min_, 60);
        if (menu_min_ > max_hold_min()) menu_min_ = max_hold_min();
        menu_touched_ = now_;
      }
    }
    // FN long: silence the alarm, else close the menu, else screen off.
    if (down_[2] && !long_done_[2] && !consumed_[2] && now_ - down_at_[2] >= cfg.long_ms) {
      long_done_[2] = true;
      if (alarm_on() && !silenced_) { silenced_ = true; toast(Toast::AlarmSilenced); }
      else if (menu_open_) { menu_open_ = false; toast(Toast::MenuUnchanged); }
      else if (minor_fault() && !fault_silenced_) { fault_silenced_ = true; toast(Toast::AlarmSilenced); }
      else { screen_until_ = now_; page_ = Page::Status; }
    }
  }

  void short_press(Key k) {
    if (menu_open_) {
      menu_touched_ = now_;
      uint32_t step = hours_ ? 60 : cfg.step_min;
      if (k == Key::Override) { menu_min_ += step; if (menu_min_ > max_hold_min()) menu_min_ = max_hold_min(); }
      else if (k == Key::Fn) menu_min_ = menu_min_ > step ? menu_min_ - step : 0;
      else confirm_menu();
      return;
    }
    switch (k) {
      case Key::On:
        if (!want_on_) { switch_on(true); toast(Toast::TurnedOn); }
        else toast(Toast::CeilingReset);               // the ceiling request went out on key_down
        confirm_at_ = now_;
        return;
      case Key::Override:
        open_menu();
        return;
      case Key::Fn:
        if (was_dark_) { page_ = Page::Status; return; }
        // Status -> timers -> radar -> status, skipping the pages that are turned off.
        for (int i = 0; i < 3; i++) {
          page_ = page_ == Page::Status ? Page::Timers : page_ == Page::Timers ? Page::Sensor : Page::Status;
          if (page_ == Page::Status || (page_ == Page::Timers && cfg.page_timers) ||
              (page_ == Page::Sensor && cfg.page_sensor)) break;
        }
        return;
    }
  }

  // ---- warnings and cuts ----------------------------------------------------------------------------------
  void run_warnings() {
    if (!want_on_) { ah_active_ = ceil_warn_ = link_warn_ = false; return; }
    const uint32_t warn = cfg.warn_s * 1000;

    // Absence or hold.
    uint32_t due = absence_due();
    bool hold_reason = hold_set_ && hold_end_ == due;
    if (!ah_active_) {
      if ((int32_t) (now_ - (due - warn)) >= 0) {
        ah_active_ = true;
        ah_why_ = hold_reason ? Why::Hold : Why::Absence;
        ah_start_ = now_;
        ah_end_ = (int32_t) (due - (now_ + warn)) > 0 ? due : now_ + warn;   // always a full warning
      }
    } else if ((int32_t) ((due - warn) - now_) > 0) {
      ah_active_ = false;                                                     // he came back
    }
    if (ah_active_ && (int32_t) (now_ - ah_end_) >= 0) {
      cut(ah_why_ == Why::Hold ? Toast::CutHold : Toast::CutAbsence);
      return;
    }

    // Ceiling (the Shelly cuts there by itself; the bench node warns and follows).
    if (ceil_known_ && shelly_on_ == 1) {
      bool in = (int32_t) (now_ - (ceil_end_ - warn)) >= 0;
      if (in && !ceil_warn_) { ceil_warn_ = true; ceil_warn_start_ = now_; }
      if (!in) ceil_warn_ = false;
      if (ceil_warn_ && (int32_t) (now_ - ceil_end_) >= 0) { cut(Toast::CutCeiling); return; }
    } else {
      ceil_warn_ = false;
    }

    // Link lost: the Shelly will cut by itself when its timer runs out. Nothing answers this warning.
    bool lost = ever_ok_ && now_ - last_ok_ > cfg.link_lost_s * 1000;
    if (lost && !link_warn_) { link_warn_ = true; link_warn_start_ = now_; }
    if (!lost) link_warn_ = false;
    if (link_warn_ && (int32_t) (now_ - lease_until_) >= 0) {
      want_on_ = false;                                // the Shelly's own timer has cut it
      off_at_ = now_;
      cut_at_ = now_;
      clear_session();
      toast(Toast::CutLink);
    }
  }

  // ---- faults ---------------------------------------------------------------------------------------------
  void run_faults() {
    const uint32_t mm = cfg.mismatch_s * 1000;
    // Bar live although it should be off: welded contacts, or a Shelly hung with the relay on.
    bool should_be_off = (ever_ok_ && shelly_on_ == 0) || (!want_on_ && off_at_ != 0);
    if (should_be_off && sense_) { if (!stuck_since_) stuck_since_ = now_ | 1; }
    else stuck_since_ = 0;
    stuck_on_ = stuck_since_ && now_ - stuck_since_ > mm;
    // No answer for longer than a whole lease, yet the bar is still live.
    uint32_t ref = ever_ok_ ? last_ok_ : boot_at_;
    lost_live_ = sense_ && now_ - ref > (cfg.lease_s * 1000 + mm);
    // The Shelly says on, the bar sense says dead.
    if (ever_ok_ && shelly_on_ == 1 && link_ok() && !sense_) { if (!dead_since_) dead_since_ = now_ | 1; }
    else dead_since_ = 0;
    sense_dead_ = dead_since_ && now_ - dead_since_ > mm;
    if (!alarm_on()) silenced_ = false;
    if (!minor_fault()) { minor_since_ = 0; minor_wakes_ = 0; fault_silenced_ = false; }
    else {
      if (!minor_since_) minor_since_ = now_ | 1;
      uint32_t n = (now_ - minor_since_) / 60000;   // show the status page (and its FAULT line) with each beep
      if (n >= 1 && n != minor_wakes_ && !fault_silenced_) { minor_wakes_ = n; page_ = Page::Status; wake(); }
    }
  }

  // ---- state ----------------------------------------------------------------------------------------------
  uint32_t now_ = 0, boot_at_ = 0;
  bool quiet_ = false, radar_ok_ = false, present_ = false, sense_ = false;
  uint32_t last_seen_ = 0;

  bool want_on_ = false;
  int shelly_on_ = -1;
  bool ever_ok_ = false, last_failed_ = false;
  uint32_t fails_ = 0, last_ok_ = 0, last_attempt_ = 0, off_at_ = 0;
  uint32_t lease_until_ = 0, ceil_end_ = 0;
  bool ceil_known_ = false;
  Op in_flight_ = Op::None;
  bool pending_off_ = false, pending_press_ = false, pending_remote_ = false;
  uint32_t last_press_req_ = 0;

  bool hold_active_ = false, hold_set_ = false;
  uint32_t hold_end_ = 0, last_hold_min_ = 30;
  bool hold_chosen_ = false;
  uint32_t hold_len_s_ = 0;
  bool restore_pending_ = false;
  uint32_t restore_seen_ago_ms_ = 0, restore_hold_ms_ = 0;
  uint32_t seq_ = 0;

  bool ah_active_ = false;
  Why ah_why_ = Why::None;
  uint32_t ah_start_ = 0, ah_end_ = 0;
  bool ceil_warn_ = false, link_warn_ = false;
  uint32_t ceil_warn_start_ = 0, link_warn_start_ = 0;

  bool stuck_on_ = false, lost_live_ = false, sense_dead_ = false, silenced_ = false, fault_silenced_ = false;
  uint32_t minor_since_ = 0, minor_wakes_ = 0;
  uint32_t stuck_since_ = 0, dead_since_ = 0;

  bool down_[3] = {}, long_done_[3] = {}, consumed_[3] = {};
  uint32_t down_at_[3] = {};
  bool hold_to_cut_ = false, was_dark_ = true;
  bool menu_open_ = false, hours_ = false;
  uint32_t menu_min_ = 30, menu_touched_ = 0;

  Page page_ = Page::Status;
  uint32_t screen_until_ = 0;
  Toast toast_ = Toast::None;
  uint32_t toast_at_ = 0, click_at_ = 0, confirm_at_ = 0, cut_at_ = 0, test_at_ = 0;
  bool test_running_ = false;
  uint32_t test_which_ = 0;
};

}  // namespace bench
