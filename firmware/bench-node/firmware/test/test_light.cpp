// PC tests for src/screen_light.h: when the idle screen is bright, dim or dark.
#include <cstdio>

#include "../src/screen_light.h"

static int checks = 0, failures = 0;
#define CHECK(x)                                                         \
  do {                                                                   \
    checks++;                                                            \
    if (!(x)) { failures++; printf("FAIL test_light.cpp:%d: %s\n", __LINE__, #x); } \
  } while (0)

using light::Clock;
using light::Level;
static const uint32_t MIN = 60000;

// Runs the clock a second at a time from `from` to `to`, someone seen or not.
static void run(Clock &c, uint32_t from, uint32_t to, bool seen) {
  for (uint32_t d = 0; d <= to - from; d += 1000) c.tick(seen, from + d);   // counts steps: works across the wrap
}

static void test_at_the_bench() {
  Clock c;
  uint32_t t0 = 5000;
  c.begin(t0);
  run(c, t0, t0 + 20 * MIN - 1000, true);
  CHECK(c.level(t0 + 20 * MIN - 1000) == Level::Bright);   // the first 20 minutes
  run(c, t0 + 20 * MIN, t0 + 300 * MIN, true);
  CHECK(c.level(t0 + 300 * MIN) == Level::Dim);            // then dim for as long as he stays
  c.activity(t0 + 300 * MIN);                               // a key
  CHECK(c.level(t0 + 300 * MIN + 1000) == Level::Bright);
  run(c, t0 + 300 * MIN, t0 + 320 * MIN, true);
  CHECK(c.level(t0 + 320 * MIN) == Level::Dim);
}

static void test_away() {
  Clock c;
  uint32_t t0 = 0;
  c.begin(t0);
  run(c, t0, t0 + 30 * MIN, true);                          // at the bench, dim from 20 min
  run(c, t0 + 30 * MIN + 1000, t0 + 69 * MIN, false);       // leaves at 30 min
  CHECK(c.level(t0 + 69 * MIN) == Level::Dim);              // dim: 39 min since last seen
  run(c, t0 + 69 * MIN, t0 + 71 * MIN, false);
  CHECK(c.level(t0 + 71 * MIN) == Level::Dark);             // dark from 40 min after last seen
  run(c, t0 + 71 * MIN, t0 + 600 * MIN, false);
  CHECK(c.level(t0 + 600 * MIN) == Level::Dark);
  c.tick(true, t0 + 600 * MIN + 1000);                      // back: an arrival
  CHECK(c.level(t0 + 600 * MIN + 1000) == Level::Bright);
}

static void test_short_absence_is_not_an_arrival() {
  Clock c;
  c.begin(0);
  run(c, 0, 30 * MIN, true);                                // dim at the bench
  run(c, 30 * MIN + 1000, 49 * MIN, false);                 // 19 min away
  run(c, 49 * MIN + 1000, 50 * MIN, true);                  // back
  CHECK(c.level(50 * MIN) == Level::Dim);                   // not 20 min away: stays dim
  run(c, 50 * MIN + 1000, 75 * MIN, false);                 // away again
  run(c, 75 * MIN + 1000, 76 * MIN, true);                  // back after 25 min away: an arrival
  CHECK(c.level(76 * MIN) == Level::Bright);
}

static void test_away_from_the_start_and_a_key() {
  Clock c;
  c.begin(0);
  run(c, 0, 19 * MIN, false);
  CHECK(c.level(19 * MIN) == Level::Bright);                // just started: bright for 20 min
  run(c, 19 * MIN, 21 * MIN, false);
  CHECK(c.level(21 * MIN) == Level::Dim);
  run(c, 21 * MIN, 41 * MIN, false);
  CHECK(c.level(41 * MIN) == Level::Dark);
  c.activity(41 * MIN);                                     // a key wakes it, nobody seen
  CHECK(c.level(41 * MIN + 1000) == Level::Bright);
  run(c, 41 * MIN, 62 * MIN, false);
  CHECK(c.level(62 * MIN) == Level::Dark);                  // 20 bright minutes, then dark again (long unseen)
}

static void test_settings_and_wrap() {
  Clock c;
  c.bright_ms = 5 * MIN; c.dark_ms = 1 * MIN;
  uint32_t t0 = 0xFFFFFFFFu - 2 * MIN;                      // millis about to wrap
  c.begin(t0);
  run(c, t0, t0 + 4 * MIN, false);
  CHECK(c.level(t0 + 4 * MIN) == Level::Bright);
  run(c, t0 + 4 * MIN, t0 + 5 * MIN + 1000, false);
  CHECK(c.level(t0 + 5 * MIN + 1000) == Level::Dim);
  run(c, t0 + 5 * MIN + 1000, t0 + 7 * MIN, false);
  CHECK(c.level(t0 + 7 * MIN) == Level::Dark);
  Clock d;                                                  // 60 days unseen: still dark, never "just now"
  d.begin(0);
  for (uint32_t day = 0; day < 60; day++) for (uint32_t m = 0; m < 1440; m++) d.tick(false, day * 1440 * MIN + m * MIN);
  CHECK(d.level(60u * 1440u * MIN) == Level::Dark);
}

int main() {
  test_at_the_bench();
  test_away();
  test_short_absence_is_not_an_arrival();
  test_away_from_the_start_and_a_key();
  test_settings_and_wrap();
  printf("%d checks, %d failure(s)\n", checks, failures);
  return failures ? 1 : 0;
}
