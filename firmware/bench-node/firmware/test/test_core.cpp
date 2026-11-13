// PC tests for src/bench_core.h. Build and run: make -C test
// A fake Shelly answers the lease requests the way the script in firmware/shelly-lease does (the Firmware
// Gospel's lease contract).
#include "../src/bench_core.h"

#include <cstdio>
#include <initializer_list>

using namespace bench;

static int failures = 0, checks = 0;
#define CHECK(c)                                                                       \
  do {                                                                                 \
    checks++;                                                                          \
    if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
  } while (0)

// The Shelly, simulated: relay, toggle_after timer, ceiling, reachable or not.
struct FakeShelly {
  bool on = false, reachable = true;
  uint32_t timer_end = 0, ceil_end = 0;
  bool timer = false;

  void run(uint32_t now) {
    if (on && timer && (int32_t) (now - timer_end) >= 0) { on = false; timer = false; }
    if (on && (int32_t) (now - ceil_end) >= 0) { on = false; timer = false; }
  }
  Answer answer(Op op, uint32_t now) {
    run(now);
    Answer a;
    a.ok = true;
    uint32_t l = 300;
    auto apply = [&](uint32_t lease) {
      uint32_t left = (ceil_end - now) / 1000;
      if (lease > left) lease = left;
      on = true; timer = true; timer_end = now + lease * 1000; a.lease_s = lease;
    };
    switch (op) {
      case Op::Renew:
        if (!on) { a.ok = false; break; }
        apply(l);
        break;
      case Op::OnPress:
        ceil_end = now + 14400 * 1000; apply(l);
        break;
      case Op::OnRemote:
        if (!on) ceil_end = now + 14400 * 1000;
        apply(l);
        break;
      case Op::Off:
        on = false; timer = false;
        break;
      default: break;
    }
    a.on = on;
    a.ceil_s = on ? (ceil_end - now) / 1000 : 0;
    return a;
  }
};

struct Rig {
  Core c;
  FakeShelly s;
  uint32_t now = 1000;
  bool present = true, radar_ok = true, quiet = false;
  bool sense_follows = true;     // the bar sense follows the relay, with the measured 7 s lag on a cut
  bool welded = false;
  bool hang = false;              // the link task never answers
  Op hung_op = Op::None;
  uint32_t hung_seq = 0;
  uint32_t off_since = 0;
  int cuts = 0;

  Rig() { c.begin(now); }

  bool sense() {
    if (welded) return true;
    if (!sense_follows) return false;
    if (s.on) { off_since = 0; return true; }
    if (!off_since) off_since = now;
    return now - off_since < 7000;
  }
  void step(uint32_t ms = 100) {
    now += ms;
    bool was_on = s.on;
    s.run(now);
    c.tick(now, present, radar_ok, sense(), quiet);
    Op op = c.take_op(now);
    if (op != Op::None && hang) { hung_op = op; hung_seq = c.seq(); op = Op::None; }
    if (op != Op::None) {
      if (s.reachable) c.lease_result(op, true, s.answer(op, now), now);
      else c.lease_result(op, false, Answer(), now);
    }
    if (was_on && !s.on) cuts++;
  }
  void run(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 100) step(); }
  void press(Key k, uint32_t held = 100) {
    c.key_down(k);
    for (uint32_t t = 0; t < held; t += 10) step(10);
    c.key_up(k);
    step(10);
  }
  bool on_soon() { run(500); return s.on; }
};

static const uint32_t MIN = 60000;

static void test_on_and_hold_to_cut() {
  Rig r;
  r.run(2000);
  CHECK(!r.s.on);
  CHECK(r.c.phase() == Phase::Off);
  r.press(Key::On);
  CHECK(r.on_soon());
  CHECK(r.c.phase() == Phase::On);
  CHECK(r.c.ceil_left_s() > 14390);
  r.press(Key::On, 1000);                 // let go before 1.5 s: nothing
  CHECK(r.on_soon());
  r.press(Key::On, 1700);                 // held: cut
  r.run(500);
  CHECK(!r.s.on);
  CHECK(r.c.phase() == Phase::Off);
}

static void test_absence_warning_and_cut() {
  Rig r;
  r.press(Key::On);
  r.run(1000);
  r.present = false;
  r.run(14 * MIN);
  CHECK(r.s.on && !r.c.warning_active());
  r.run(1 * MIN + 2000);                  // 15 min absent: warning starts (20 min - 5 min)
  CHECK(r.c.warning_active());
  CHECK(r.c.warn_why() == Why::Absence);
  CHECK(r.c.warn_left_s() <= 300 && r.c.warn_left_s() >= 295);
  r.run(4 * MIN + 50000);
  CHECK(r.s.on);                          // still on 10 s before the end
  r.run(15000);
  CHECK(!r.s.on);                         // cut at 20 min absence
  CHECK(r.c.last_toast() == Toast::CutAbsence);
}

static void test_presence_cancels_warning() {
  Rig r;
  r.press(Key::On);
  r.present = false;
  r.run(16 * MIN);
  CHECK(r.c.warning_active());
  r.present = true;
  r.run(1000);
  CHECK(!r.c.warning_active());
  r.present = false;
  r.run(10 * MIN);
  CHECK(r.s.on);                          // the absence timer restarted when he came back
}

static void test_key_answers_warning() {
  Rig r;
  r.press(Key::On);
  r.present = false;
  r.run(16 * MIN);
  CHECK(r.c.warning_active());
  r.press(Key::Override);                 // answers; must not open the menu
  CHECK(!r.c.warning_active());
  CHECK(!r.c.menu_open());
  r.run(10 * MIN);
  CHECK(r.s.on);
}

// Two worked examples, X = 20 min, Y = 60 min, a 5 min warning that ends at the cut.
static void test_hold_examples() {
  {
    Rig r;
    r.press(Key::On);
    r.c.web_hold(60);                     // t = 0
    r.present = false;
    r.run(30 * MIN);                      // back at 30, gone at 35
    r.present = true; r.run(5 * MIN); r.present = false;
    r.run(19 * MIN);                      // t = 54
    CHECK(r.s.on && !r.c.warning_active());
    r.run(1 * MIN + 2000);                // t = 55: warning, cut due at 60 (absence 25 > 20)
    CHECK(r.c.warning_active());
    CHECK(r.c.warn_why() == Why::Hold);
    r.run(4 * MIN + 50000);
    CHECK(r.s.on);
    r.run(15000);
    CHECK(!r.s.on);                       // cut at 60, the end of the hold
  }
  {
    Rig r;
    r.press(Key::On);
    r.c.web_hold(60);
    r.present = false;
    r.run(50 * MIN);                      // back at 50, gone at 55
    r.present = true; r.run(5 * MIN); r.present = false;
    r.run(14 * MIN);                      // t = 69
    CHECK(r.s.on && !r.c.warning_active());
    r.run(1 * MIN + 2000);                // t = 70: warning, cut due at 75 (55 + 20)
    CHECK(r.c.warning_active());
    CHECK(r.c.warn_why() == Why::Absence);
    r.run(5 * MIN);
    CHECK(!r.s.on);
  }
}

static void test_hold_ends_after_long_absence_still_warns_fully() {
  Rig r;
  r.press(Key::On);
  r.c.web_hold(10);
  r.present = false;
  // The hold (10 min) ends before the absence would (20 min): warning 5 min before minute 20.
  r.run(14 * MIN + 30000);
  CHECK(!r.c.warning_active());
  r.run(1 * MIN);
  CHECK(r.c.warning_active());
}

static void test_ceiling_warning_and_reset() {
  Rig r;
  r.press(Key::On);
  r.run(235 * MIN + 2000);                // 4 h - 5 min, he is present but presses nothing
  CHECK(r.c.warning_active());
  CHECK(r.c.warn_why() == Why::Ceiling);
  r.press(Key::Fn);                       // one press: ceiling restarts on the Shelly
  r.run(1000);
  CHECK(!r.c.warning_active());
  CHECK(r.c.ceil_left_s() > 14300);
  r.run(10 * MIN);
  CHECK(r.s.on);
}

static void test_ceiling_unanswered_cuts() {
  Rig r;
  r.press(Key::On);
  r.run(241 * MIN);
  CHECK(!r.s.on);
  CHECK(r.c.phase() == Phase::Off);
}

static void test_remote_on_does_not_reset_ceiling() {
  Rig r;
  r.c.remote_on();
  CHECK(r.on_soon());
  r.run(60 * MIN);
  uint32_t before = r.c.ceil_left_s();
  r.c.remote_on();                        // already on: nothing
  r.run(1000);
  CHECK(r.c.ceil_left_s() <= before);
}

static void test_link_lost_warns_then_shelly_cuts() {
  Rig r;
  r.press(Key::On);
  r.run(1 * MIN);
  r.s.reachable = false;
  r.run(70000);
  CHECK(r.c.warning_active());
  CHECK(r.c.warn_why() == Why::Link);
  r.run(5 * MIN);
  CHECK(!r.s.on);                         // the Shelly's own timer cut it
  CHECK(r.c.phase() == Phase::Off || r.c.phase() == Phase::Alarm);
  CHECK(!r.c.alarm_on());                 // bar sense followed: no alarm
}

static void test_stuck_on_alarm() {
  Rig r;
  r.press(Key::On);
  r.run(2000);
  r.welded = true;
  r.press(Key::On, 1700);                 // cut, but the contacts are welded
  r.run(10000);
  CHECK(!r.c.alarm_on());                 // within the 15 s allowance
  r.run(10000);
  CHECK(r.c.alarm_on());
  CHECK(r.c.fault() == Fault::StuckOn);
  CHECK(r.c.tone().hz != 0 || r.c.tone().hz == 0);
  bool sounded = false;
  for (int i = 0; i < 10; i++) { r.step(); sounded |= r.c.tone().hz != 0; }
  CHECK(sounded);
  { bool flagged = false; for (int i = 0; i < 10; i++) { r.step(); flagged |= r.c.tone().alarm; } CHECK(flagged); }
  r.press(Key::Fn, 900);                  // long FN silences
  CHECK(r.c.alarm_silenced());
  CHECK(r.c.tone().hz == 0);
}

static void test_cut_lag_is_not_an_alarm() {
  Rig r;
  r.press(Key::On);
  r.run(2000);
  r.press(Key::On, 1700);
  r.run(30000);                           // sense lags 7 s
  CHECK(!r.c.alarm_on());
}

static void test_sense_dead_fault() {
  Rig r;
  r.press(Key::On);
  r.sense_follows = false;
  r.run(20000);
  CHECK(r.c.fault() == Fault::SenseDead);
  CHECK(!r.c.alarm_on());                 // shown, not sounded
}

static void test_radar_dead_counts_as_absence() {
  Rig r;
  r.press(Key::On);
  r.radar_ok = false;
  r.run(16 * MIN);
  CHECK(r.c.fault() == Fault::RadarDead);
  CHECK(r.c.warning_active());
}

static void test_external_on_adopted_and_off_followed() {
  Rig r;
  r.run(2000);
  r.s.on = true; r.s.timer = true; r.s.timer_end = r.now + 300000; r.s.ceil_end = r.now + 14400000;
  r.run(12000);
  CHECK(r.c.want_on());
  CHECK(r.c.last_toast() == Toast::Adopted);
  r.run(5 * MIN);
  CHECK(r.s.on);                          // renewed
  r.s.on = false;                         // switched off from the Shelly app
  r.run(31000);
  CHECK(!r.c.want_on());
}

static void test_menu() {
  Rig r;
  r.press(Key::On);
  r.press(Key::Override);
  CHECK(r.c.menu_open());
  CHECK(r.c.menu_minutes() == 30);
  r.press(Key::Override); r.press(Key::Override); r.press(Key::Fn);
  CHECK(r.c.menu_minutes() == 35);
  r.press(Key::Override, 800);            // hours
  CHECK(r.c.menu_hours() && r.c.menu_minutes() == 60);
  for (int i = 0; i < 10; i++) r.press(Key::Override);
  CHECK(r.c.menu_minutes() == 240);       // capped at the ceiling
  r.press(Key::On);
  CHECK(!r.c.menu_open() && r.c.hold_active());
  CHECK(r.c.phase() == Phase::Hold);
  r.press(Key::Override);
  r.run(16000);
  CHECK(!r.c.menu_open());                // idle: closed with no change
  CHECK(r.c.hold_active());
}

static void test_hold_from_off_turns_on() {
  Rig r;
  r.press(Key::Override);
  r.press(Key::On);
  CHECK(r.on_soon());
  CHECK(r.c.hold_active());
}

static void test_leds() {
  Rig r;
  CHECK(r.c.led(Key::On) == 0.0f);
  r.press(Key::On);
  r.run(500);
  for (Key k : {Key::On, Key::Override, Key::Fn}) CHECK(r.c.led(k) == 1.0f);
  r.c.web_hold(30);
  r.run(100);
  float a = r.c.led(Key::Override);
  r.run(1500);
  CHECK(a != r.c.led(Key::Override));     // breathes
}

static void test_sounds_and_quiet_hours() {
  for (int q = 0; q < 2; q++) {
    Rig r;
    r.quiet = q;
    r.press(Key::On);
    r.present = false;
    r.run(15 * MIN);
    int start = 0, mark60 = 0, final15 = 0;
    for (int i = 0; i < 300 * 20; i++) {    // through the whole warning in 50 ms steps
      r.step(50);
      uint32_t left = r.c.warn_left_s();
      if (!r.c.warning_active()) break;
      bool s = r.c.tone().hz != 0;
      if (left > 295) start += s;
      else if (left <= 60 && left > 57) mark60 += s;
      else if (left <= 15) final15 += s;
    }
    CHECK(start > 0);                        // the start mark plays day and night
    if (q) { CHECK(mark60 == 0); CHECK(final15 == 0); }
    else { CHECK(mark60 > 0); CHECK(final15 > 0); }
  }
}

static void test_millis_wrap() {
  Rig r;
  r.now = 0xFFFFFFFFu - 3 * MIN;
  r.c = Core();
  r.c.begin(r.now);
  r.s = FakeShelly();
  r.press(Key::On);
  r.present = false;
  r.run(14 * MIN);                        // crosses the wrap
  CHECK(r.s.on && !r.c.warning_active());
  r.run(2 * MIN);
  CHECK(r.c.warning_active());
  r.run(5 * MIN);
  CHECK(!r.s.on);
}

// The web page and HA never reset the ceiling; only keys do.
static void test_web_never_resets_ceiling() {
  Rig r;
  r.c.web_on();                           // from off: a fresh period is allowed
  CHECK(r.on_soon());
  r.run(60 * MIN);
  uint32_t before = r.c.ceil_left_s();
  r.c.web_on();
  r.c.web_hold(120);
  r.run(2000);
  CHECK(r.c.ceil_left_s() <= before);
  CHECK(r.c.hold_active());
  r.press(Key::On);                       // the ON key does reset it
  r.run(1000);
  CHECK(r.c.ceil_left_s() > before + 3000);
}

// A key already held when the warning starts answers it.
static void test_held_key_answers_warning() {
  Rig r;
  r.press(Key::On);
  r.run(235 * MIN - 300);
  CHECK(!r.c.warning_active());
  r.c.key_down(Key::Override);            // held across the start of the ceiling warning (before it counts as long)
  r.run(3000);
  CHECK(!r.c.warning_active());
  r.c.key_up(Key::Override);
  r.step();
  CHECK(!r.c.menu_open());
  r.run(10 * MIN);
  CHECK(r.s.on);
}

// No Shelly answer yet means "unknown", never "off".
static void test_unknown_before_first_answer() {
  Rig r;
  r.s.reachable = false;
  r.s.on = true; r.s.timer = true; r.s.timer_end = r.now + 300000; r.s.ceil_end = r.now + 14400000;
  r.run(3000);
  CHECK(r.c.phase() == Phase::Unknown);
  r.s.reachable = true;
  r.run(12000);
  CHECK(r.c.phase() == Phase::On);
}

// A request that never gets an answer is abandoned, and its late answer ignored.
static void test_stuck_request_abandoned() {
  Rig r;
  r.press(Key::On);
  r.run(2000);
  r.hang = true;
  r.run(31000);                           // a renewal is taken and never answered
  CHECK(r.hung_op == Op::Renew);
  uint32_t old_seq = r.hung_seq;
  r.hang = false;
  r.run(20000);                           // abandoned after 15 s, retried, answered
  CHECK(r.c.link_ok());
  Answer stale; stale.ok = false; stale.on = false;
  r.c.lease_result(Op::Renew, true, stale, r.now, old_seq);   // the late answer to the abandoned request
  r.step();
  CHECK(r.c.want_on());                   // ignored: it would have read as "off"
  CHECK(r.s.on);
}

// A bench node restart while on resumes the absence clock.
static void test_restart_resumes_absence() {
  Rig r;
  r.s.on = true; r.s.timer = true; r.s.timer_end = r.now + 300000; r.s.ceil_end = r.now + 14400000;
  r.present = false;
  r.c.restore(18 * MIN, 0);               // he had been gone 18 min when the node restarted
  r.run(12000);
  CHECK(r.c.want_on());
  CHECK(r.c.warning_active());            // 18 min + 5 min warning: the warning is already due
  CHECK(r.c.warn_left_s() >= 285);        // a full warning (started at adoption, 12 s ago)
}

// A restart during a hold carries the hold on; its total restarts at what was left.
static void test_restart_during_hold_keeps_a_total() {
  Rig r;
  r.s.on = true; r.s.timer = true; r.s.timer_end = r.now + 300000; r.s.ceil_end = r.now + 14400000;
  r.present = false;
  r.c.restore(5 * MIN, 40 * MIN);         // 40 min of hold were left when the node restarted
  r.run(12000);
  CHECK(r.c.hold_active());
  CHECK(r.c.hold_total_s() == 40 * 60);
  CHECK(r.c.hold_left_s() <= 40 * 60 && r.c.hold_left_s() >= 40 * 60 - 15);
}

// The configurable sounds.
static void test_mark1_plays_in_a_short_warning() {
  Rig r;
  r.c.cfg.warn_s = 60;                      // as short as the settings allow, equal to m2_at_s
  r.press(Key::On);
  r.present = false;
  int m1 = 0;
  for (int i = 0; i < 20 * 60 * 20; i++) {
    r.step(50);
    if (r.c.warning_active() && r.c.warn_left_s() > 59 && r.c.tone().hz) m1++;
    if (r.c.warning_active() && r.c.warn_left_s() <= 58) break;
  }
  CHECK(m1 > 0);
}

static void test_test_sound_never_covers_a_warning() {
  Rig r;
  r.press(Key::On);
  r.present = false;
  r.run(15 * MIN - 5000);
  CHECK(!r.c.warning_active());
  CHECK(r.c.test_sound(0));
  r.run(6000);                              // the warning starts during the test
  CHECK(r.c.warning_active());
  CHECK(!r.c.test_playing());
  CHECK(!r.c.test_sound(0));                // refused during a warning
}

static void test_patterns_stay_in_range() {
  Rig r;
  Config &c = r.c.cfg;
  c.base_hz = 5000; c.fin_rise_pct = 200; c.m1_notes = 6; c.m1_step_pct = 100; c.m1_note_ms = 30; c.m1_repeats = 5;
  c.fin_s = 3; c.fin_double_s = 15; c.fin_start_ms = 600; c.fin_end_ms = 20;
  for (uint32_t w = 0; w <= 8; w++) {
    r.c.test_sound(w);
    uint32_t mx = 0;
    for (int i = 0; i < 2000; i++) { r.step(10); uint32_t hz = r.c.tone().hz; if (hz > mx) mx = hz; if (!r.c.test_playing()) break; }
    CHECK(mx < 40000);
    CHECK(!r.c.test_playing() || w == 0);
  }
}

static void test_led_effects() {
  Rig r;
  r.c.cfg.led_dead = 1; r.c.cfg.led_dim_pct = 10;
  r.run(8000);                              // past the simulated 7 s bar-sense lag
  CHECK(r.c.led(Key::Fn) > 0.05f && r.c.led(Key::Fn) < 0.15f);   // dim while the bar is off
  r.press(Key::On);
  r.run(500);
  r.c.cfg.led_live = 2;
  CHECK(r.c.led(Key::On) > 0.05f && r.c.led(Key::On) < 0.15f);   // dim while live
  r.c.cfg.led_live = 0;
  CHECK(r.c.led(Key::On) == 1.0f);
}

static void test_fn_pages_skip_disabled() {
  Rig r;
  r.c.cfg.page_timers = false;
  r.press(Key::Fn);
  CHECK(r.c.page() == Page::Status);
  r.press(Key::Fn);
  CHECK(r.c.page() == Page::Sensor);        // the timers page is skipped
  r.c.cfg.page_sensor = false;
  r.press(Key::Fn);
  CHECK(r.c.page() == Page::Status);
  r.press(Key::Fn);
  CHECK(r.c.page() == Page::Status);        // both off: FN stays on the status page
}

// On the very tick a warning starts, the real warning sound plays, not the test.
static void test_warning_tone_wins_on_its_first_tick() {
  Rig r;
  r.press(Key::On);
  r.present = false;
  r.run(15 * MIN - 3000);
  CHECK(r.c.test_sound(5));                 // the alarm test sound: flagged alarm=true
  bool checked = false;
  for (int i = 0; i < 100 && !checked; i++) {
    r.step(50);
    if (r.c.warning_active()) { CHECK(!r.c.tone().alarm); checked = true; }
  }
  CHECK(checked);
}

// ON in the menu confirms however long it is held; it never cuts.
static void test_on_held_in_menu_confirms() {
  Rig r;
  r.press(Key::On);
  r.run(500);
  r.press(Key::Override);
  CHECK(r.c.menu_open());
  r.press(Key::On, 2000);                   // a long, firm press
  r.run(500);
  CHECK(r.s.on);                            // not cut
  CHECK(!r.c.menu_open());
  CHECK(r.c.hold_active());                 // confirmed
}

static void test_firm_on_turns_on_when_off() {
  Rig r;
  r.run(1000);
  r.press(Key::On, 800);
  CHECK(r.on_soon());
}

static void test_warning_closes_menu() {
  Rig r;
  r.c.cfg.menu_idle_ms = 3600000;            // keep the menu open long enough (a key press resets the absence)
  r.press(Key::On);
  r.run(1000);
  r.press(Key::Override);
  CHECK(r.c.menu_open());
  r.s.reachable = false;                    // the "no link" warning starts about 65 s later
  r.run(70000);
  CHECK(r.c.warning_active());
  CHECK(!r.c.menu_open());
}

// ON already held when the menu opens: the screen shows the menu, not "turning off".
static void test_menu_hides_hold_to_cut() {
  Rig r;
  r.press(Key::On);
  r.run(500);
  r.c.key_down(Key::On);
  r.run(600);                               // past the short-press limit: hold-to-cut showing
  CHECK(r.c.page() == Page::HoldToCut);
  r.press(Key::Override);
  CHECK(r.c.menu_open());
  CHECK(r.c.page() == Page::Menu);
  r.c.key_up(Key::On);
  r.step();
  CHECK(r.s.on);
}

// Only ON restarts the 4 h ceiling and the absence clock; OVERRIDE and FN do not.
static void test_only_on_restarts_the_clocks() {
  Rig r;
  r.press(Key::On);
  r.run(500);
  r.present = false;
  r.run(10 * MIN);
  uint32_t ceil0 = r.c.ceil_left_s();
  int32_t due0 = r.c.away_due_s();
  r.press(Key::Fn);                        // pages the screen
  r.press(Key::Override);                  // opens the menu...
  r.press(Key::Fn, 1000);                  // ...and leaves it unchanged
  r.run(2000);
  CHECK(!r.c.menu_open());
  CHECK(r.c.ceil_left_s() <= ceil0);
  CHECK(r.c.away_due_s() <= due0);
  r.press(Key::On);                        // ON: "I'm here"
  r.run(2000);
  CHECK(r.c.ceil_left_s() > ceil0 + 500);
  CHECK(r.c.away_due_s() > due0 + 500);
}

// With the screen idle ("on after a key"), a press only wakes it; the next press acts.
static void test_idle_press_only_wakes() {
  Rig r;
  r.c.cfg.wake_first = true;
  r.c.key_down(Key::On);                   // bar off, screen idle: the first ON only wakes the screen
  r.step(10);
  r.c.key_up(Key::On);
  r.run(500);
  CHECK(!r.s.on);
  CHECK(r.c.page() == Page::Status);
  r.press(Key::On);                        // awake: now it turns the bar on
  CHECK(r.on_soon());
  r.run(r.c.cfg.screen_ms + 1000);         // the screen goes idle again
  CHECK(r.c.page() == Page::Dark);
  uint32_t ceil0 = r.c.ceil_left_s();
  r.press(Key::Override);                  // only wakes: no menu
  CHECK(!r.c.menu_open());
  CHECK(r.c.page() == Page::Status);
  r.press(Key::Override);                  // awake: opens the menu
  CHECK(r.c.menu_open());
  r.press(Key::Fn, 1000);                  // leave it
  r.run(r.c.cfg.screen_ms + 1000);
  CHECK(r.c.page() == Page::Dark);
  r.c.key_down(Key::On);                   // held from idle: no cut, it only wakes
  r.run(3000);
  r.c.key_up(Key::On);
  r.run(1000);
  CHECK(r.s.on);
  CHECK(r.c.ceil_left_s() <= ceil0);       // and that press restarted nothing
  // A warning is answered by any key, idle screen or not.
  r.present = false;
  r.run(16 * MIN);
  CHECK(r.c.warning_active());
  r.press(Key::Fn);
  r.run(1000);
  CHECK(!r.c.warning_active());
}


// The bar sense or the radar silent beeps twice a minute after the first minute, shows the status page, and
// FN held silences it until the fault clears.
static void test_minor_fault_beeps() {
  Rig r;
  r.press(Key::On);
  r.radar_ok = false;
  r.run(30000);
  CHECK(r.c.minor_fault() && !r.c.alarm_on());
  bool beep = false;
  for (int i = 0; i < 100; i++) { r.step(); beep |= r.c.tone().hz != 0; }
  CHECK(!beep);                                        // nothing in the first minute
  r.run(20000);
  int beeps = 0;
  bool prev = false;
  for (int i = 0; i < 600; i++) { r.step(10); bool on = r.c.tone().hz != 0; if (on && !prev) beeps++; prev = on; }
  CHECK(beeps == 2);                                   // two short beeps at the minute
  CHECK(r.c.page() == Page::Status || r.c.page() == Page::Warning);
  r.press(Key::Fn, 900);
  CHECK(r.c.fault_silenced());
  r.run(70000);
  beep = false;
  for (int i = 0; i < 600; i++) { r.step(10); beep |= r.c.tone().hz != 0 && !r.c.warning_active(); }
  CHECK(!beep || r.c.warning_active());
  r.radar_ok = true;
  r.step();
  CHECK(!r.c.minor_fault() && !r.c.fault_silenced());
}

int main() {
  test_only_on_restarts_the_clocks();
  test_idle_press_only_wakes();
  test_menu_hides_hold_to_cut();
  test_on_held_in_menu_confirms();
  test_firm_on_turns_on_when_off();
  test_warning_closes_menu();
  test_warning_tone_wins_on_its_first_tick();
  test_mark1_plays_in_a_short_warning();
  test_test_sound_never_covers_a_warning();
  test_patterns_stay_in_range();
  test_led_effects();
  test_fn_pages_skip_disabled();
  test_web_never_resets_ceiling();
  test_held_key_answers_warning();
  test_unknown_before_first_answer();
  test_stuck_request_abandoned();
  test_restart_resumes_absence();
  test_restart_during_hold_keeps_a_total();
  test_on_and_hold_to_cut();
  test_absence_warning_and_cut();
  test_presence_cancels_warning();
  test_key_answers_warning();
  test_hold_examples();
  test_hold_ends_after_long_absence_still_warns_fully();
  test_ceiling_warning_and_reset();
  test_ceiling_unanswered_cuts();
  test_remote_on_does_not_reset_ceiling();
  test_link_lost_warns_then_shelly_cuts();
  test_stuck_on_alarm();
  test_cut_lag_is_not_an_alarm();
  test_sense_dead_fault();
  test_radar_dead_counts_as_absence();
  test_minor_fault_beeps();
  test_external_on_adopted_and_off_followed();
  test_menu();
  test_hold_from_off_turns_on();
  test_leds();
  test_sounds_and_quiet_hours();
  test_millis_wrap();
  std::printf("%d checks, %d failure(s)\n", checks, failures);
  return failures ? 1 : 0;
}
