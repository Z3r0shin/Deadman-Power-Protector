// Host tests for bench_ui.h. Build and run: make -C tests
#include "../bench_ui.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace bench;

static int failures = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
      failures++;                                                         \
    }                                                                     \
  } while (0)

// Presses a key for held_ms, ticking every 10 ms, and returns the merged requests.
static Requests press(Ui &ui, Key k, uint32_t &now, uint32_t held_ms) {
  Requests all;
  auto merge = [&](const Requests &r) {
    all.turn_on |= r.turn_on;
    all.turn_off |= r.turn_off;
    all.ceiling_reset |= r.ceiling_reset;
    if (r.hold_set) { all.hold_set = true; all.hold_ms = r.hold_ms; }
  };
  ui.key_down(k, now);
  for (uint32_t t = 0; t < held_ms; t += 10) { now += 10; merge(ui.tick(now)); }
  merge(ui.key_up(k, now));
  now += 50;
  merge(ui.tick(now));
  return all;
}

static void wait(Ui &ui, uint32_t &now, uint32_t ms, Requests *out = nullptr) {
  for (uint32_t t = 0; t < ms; t += 100) {
    now += 100;
    Requests r = ui.tick(now);
    if (out) { out->turn_off |= r.turn_off; out->turn_on |= r.turn_on; }
  }
}

static void test_on_off() {
  Ui ui; uint32_t now = 1000;
  Requests r = press(ui, Key::On, now, 100);
  CHECK(r.turn_on);
  ui.set_bar_live(true, now);
  for (Key k : {Key::On, Key::Override, Key::Fn}) CHECK(ui.led(k, now) == 1.0f);  // lit = live

  // Short ON while live: ceiling restarts, no cut.
  wait(ui, now, 60000);
  r = press(ui, Key::On, now, 100);
  CHECK(r.ceiling_reset && !r.turn_off);
  CHECK(ui.ceiling_left_ms(now) > Ui::CEILING_MS - 1000);

  // ON held 1 s then released: no cut.
  r = press(ui, Key::On, now, 1000);
  CHECK(!r.turn_off && !r.turn_on && !r.ceiling_reset);

  // ON held 1.6 s: cut.
  r = press(ui, Key::On, now, 1600);
  CHECK(r.turn_off);
  ui.set_bar_live(false, now);
  for (Key k : {Key::On, Key::Override, Key::Fn}) CHECK(ui.led(k, now) == 0.0f);  // dark = dead
}

static void test_menu_hold() {
  Ui ui; uint32_t now = 1000;
  ui.set_bar_live(true, now);
  press(ui, Key::Override, now, 100);
  CHECK(ui.menu_open());
  CHECK(ui.menu_minutes() == Ui::DEFAULT_HOLD_MIN);
  press(ui, Key::Override, now, 100);  // +5
  press(ui, Key::Override, now, 100);  // +5
  press(ui, Key::Fn, now, 100);        // -5
  CHECK(ui.menu_minutes() == 35);
  Requests r = press(ui, Key::On, now, 100);
  CHECK(!ui.menu_open());
  CHECK(r.hold_set && r.hold_ms == 35UL * 60000 && r.ceiling_reset);
  CHECK(ui.hold_active());
  // OVERRIDE breathes, the others stay lit.
  CHECK(ui.led(Key::On, now) == 1.0f);
  float a = ui.led(Key::Override, now), b = ui.led(Key::Override, now + 1500);
  CHECK(a != b);
  // Hold runs out.
  wait(ui, now, 35UL * 60000 + 200);
  CHECK(!ui.hold_active());
}

static void test_menu_hours_and_cap() {
  Ui ui; uint32_t now = 1000;
  ui.set_bar_live(true, now);
  press(ui, Key::Override, now, 100);       // open, 30 min
  press(ui, Key::Override, now, 800);       // long: hours mode, rounds up to 60
  CHECK(ui.menu_hours());
  CHECK(ui.menu_minutes() == 60);
  for (int i = 0; i < 10; i++) press(ui, Key::Override, now, 100);
  CHECK(ui.menu_minutes() == Ui::MAX_HOLD_MIN);  // capped at the ceiling
}

static void test_menu_idle_closes_unchanged() {
  Ui ui; uint32_t now = 1000;
  ui.set_bar_live(true, now);
  press(ui, Key::Override, now, 100);
  press(ui, Key::Override, now, 100);
  Requests r;
  wait(ui, now, Ui::MENU_IDLE_MS + 200, &r);
  CHECK(!ui.menu_open());
  CHECK(!ui.hold_active());
  CHECK(!r.turn_on && !r.turn_off);
}

static void test_menu_zero_cancels_hold() {
  Ui ui; uint32_t now = 1000;
  ui.set_bar_live(true, now);
  press(ui, Key::Override, now, 100);
  press(ui, Key::On, now, 100);
  CHECK(ui.hold_active());
  press(ui, Key::Override, now, 100);                       // reopens on what is left
  CHECK(ui.menu_minutes() == 30);
  for (int i = 0; i < 8; i++) press(ui, Key::Fn, now, 100); // down to 0
  CHECK(ui.menu_minutes() == 0);
  Requests r = press(ui, Key::On, now, 100);
  CHECK(r.hold_set && r.hold_ms == 0);
  CHECK(!ui.hold_active());
}

static void test_hold_from_off_turns_on() {
  Ui ui; uint32_t now = 1000;
  press(ui, Key::Override, now, 100);
  Requests r = press(ui, Key::On, now, 100);
  CHECK(r.turn_on && r.hold_set);
}

static void test_warning() {
  Ui ui; uint32_t now = 1000;
  ui.set_bar_live(true, now);
  // Answered by any key: no cut, ceiling restarts, the key's own action is not run.
  ui.start_warning(60000, now);
  CHECK(ui.page() == Page::Warning);
  wait(ui, now, 20000);
  Requests r = press(ui, Key::Override, now, 100);
  CHECK(r.ceiling_reset && !r.turn_off);
  CHECK(!ui.menu_open());
  // Unanswered: cut at the end.
  ui.start_warning(60000, now);
  r = Requests();
  wait(ui, now, 59000, &r);
  CHECK(!r.turn_off);
  wait(ui, now, 2000, &r);
  CHECK(r.turn_off);
}

static void test_warning_sound() {
  Ui ui; uint32_t now = 1000;
  ui.set_bar_live(true, now);
  ui.start_warning(60000, now);
  CHECK(ui.warning_sound(now + 55000));   // last 10 s: continuous
  int beeps = 0;
  for (uint32_t t = 0; t < 30000; t += 10) beeps += ui.warning_sound(now + t) ? 1 : 0;
  CHECK(beeps > 0 && beeps < 200);        // early: short chirps only
}

static void test_alarm_silence() {
  Ui ui; uint32_t now = 1000;
  ui.set_bar_live(true, now);
  ui.set_alarm(true);
  press(ui, Key::Fn, now, 800);
  CHECK(ui.toast() == Toast::AlarmSilenced);
  CHECK(ui.alarm_active());               // silenced, not cleared
}

static void test_fn_pages() {
  Ui ui; uint32_t now = 1000;
  ui.set_bar_live(true, now);
  CHECK(ui.page() == Page::Dark);
  press(ui, Key::Fn, now, 100);
  CHECK(ui.page() == Page::Status);       // first press only wakes
  press(ui, Key::Fn, now, 100);
  CHECK(ui.page() == Page::Timers);
  press(ui, Key::Fn, now, 100);
  CHECK(ui.page() == Page::Sensor);
  wait(ui, now, Ui::SCREEN_MS + 200);
  CHECK(ui.page() == Page::Dark);
}

static void test_millis_wrap() {
  Ui ui; uint32_t now = 0xFFFFFFFFu - 5000;
  ui.set_bar_live(true, now);
  press(ui, Key::Override, now, 100);
  press(ui, Key::On, now, 100);
  CHECK(ui.hold_active());
  wait(ui, now, 10000);                   // crosses the wrap
  CHECK(ui.hold_active());
  CHECK(ui.hold_left_ms(now) < 30UL * 60000 && ui.hold_left_ms(now) > 29UL * 60000);
}

int main() {
  test_on_off();
  test_menu_hold();
  test_menu_hours_and_cap();
  test_menu_idle_closes_unchanged();
  test_menu_zero_cancels_hold();
  test_hold_from_off_turns_on();
  test_warning();
  test_warning_sound();
  test_alarm_silence();
  test_fn_pages();
  test_millis_wrap();
  if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
  std::printf("all tests passed\n");
  return 0;
}
