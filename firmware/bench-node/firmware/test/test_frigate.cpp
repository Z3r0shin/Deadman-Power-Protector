// PC tests for src/frigate.h: the JSON reading of Frigate's messages, and when the camera vetoes the radar.
// The event texts follow the messages a running Frigate was seen to send (names replaced).
#include <cstdio>
#include <string>

#include "../src/frigate.h"

static int checks = 0, failures = 0;
#define CHECK(x)                                                         \
  do {                                                                   \
    checks++;                                                            \
    if (!(x)) { failures++; printf("FAIL test_frigate.cpp:%d: %s\n", __LINE__, #x); } \
  } while (0)

using namespace frigate;

static Config cfg() {
  Config c;
  snprintf(c.face, sizeof(c.face), "%s", "Al\xC3\xA9x");   // "Aléx" in UTF-8
  return c;
}

static std::string ev(const char *type, const char *id, const char *zones, const char *sub, const char *model,
                      const char *end_time = "null", const char *camera = "bench", const char *label = "person") {
  char b[1500];
  snprintf(b, sizeof(b),
           "{\"before\": {\"id\": \"%s\", \"camera\": \"%s\", \"current_zones\": [], \"sub_label\": null},"
           " \"after\": {\"id\": \"%s\", \"camera\": \"%s\", \"frame_time\": 1793116330.5, \"label\": \"%s\","
           " \"sub_label\": %s, \"top_score\": 0.93, \"false_positive\": false, \"start_time\": 1793114903.1,"
           " \"end_time\": %s, \"score\": 0.92, \"box\": [10, 20, 300, 400], \"area\": 1200,"
           " \"current_zones\": %s, \"entered_zones\": [\"bench_seat\"], \"stationary\": true,"
           " \"active\": false, \"attributes\": {\"face\": 0.8}, \"Bench_person\": %s,"
           " \"snapshot\": {\"frame_time\": 1.0, \"box\": [1, 2, 3, 4]}}, \"type\": \"%s\"}",
           id, camera, id, camera, label, sub, end_time, zones, model, type);
  return b;
}

static Event parse(const std::string &s) { return parse_event(s.c_str(), s.size(), cfg()); }

static void test_json_reader() {
  const char *j = "{\"a\": 1, \"b\": {\"c\": [\"x\", 2.5, null]}, \"d\": \"q\\\"uo\\u00e9\"}";
  Span root{j, strlen(j)};
  double v = 0;
  CHECK(num(get(root, "a"), v) && v == 1);
  CHECK(str_is(at(get(get(root, "b"), "c"), 0), "x"));
  CHECK(num(at(get(get(root, "b"), "c"), 1), v) && v == 2.5);
  CHECK(is_null(at(get(get(root, "b"), "c"), 2)));
  CHECK(!at(get(get(root, "b"), "c"), 3).ok());
  char b[16];
  CHECK(str(get(root, "d"), b, sizeof(b)) && strcmp(b, "q\"uo\xC3\xA9") == 0);
  CHECK(!get(root, "zz").ok());
  const char *bad = "{\"a\": [1, 2";                  // cut short: nothing is read from it
  CHECK(!get(Span{bad, strlen(bad)}, "a").ok());
  CHECK(!get(Span{"", 0}, "a").ok());
}

static void test_event_owner_by_classifier_and_face() {
  Event e = parse(ev("update", "1793.1-abc", "[\"bench_seat\"]", "null", "[\"owner\", 0.99]"));
  CHECK(e.ok && e.in_zone && e.owner == 1 && !e.end && strcmp(e.id, "1793.1-abc") == 0);
  e = parse(ev("update", "1793.1-abc", "[\"bench_seat\"]", "[\"Al\\u00e9x\", 0.97]", "null"));
  CHECK(e.ok && e.owner == 1);                         // the face name, escaped as Frigate's Python does
  e = parse(ev("update", "1793.1-abc", "[\"bench_seat\"]", "[\"Al\xC3\xA9x\", 0.97]", "null"));
  CHECK(e.ok && e.owner == 1);                         // and unescaped
  e = parse(ev("update", "1793.1-abc", "[\"bench_seat\"]", "[\"Someone\", 0.97]", "[\"none\", 0.95]"));
  CHECK(e.ok && e.owner == 0);
  e = parse(ev("new", "1793.1-abc", "[\"bench_seat\"]", "null", "null"));
  CHECK(e.ok && e.owner == -1);                        // not named yet
  e = parse(ev("update", "1793.1-abc", "[]", "null", "[\"owner\", 0.99]"));
  CHECK(e.ok && !e.in_zone);
}

static void test_event_filters() {
  CHECK(!parse(ev("update", "x", "[\"bench_seat\"]", "null", "null", "null", "other")).ok);   // another camera
  CHECK(!parse(ev("update", "x", "[\"bench_seat\"]", "null", "null", "null", "bench", "cat")).ok);
  CHECK(parse(ev("end", "x", "[\"bench_seat\"]", "null", "null")).end);
  CHECK(parse(ev("update", "x", "[\"bench_seat\"]", "null", "null", "1793116400.2")).end);
}

static void test_stats() {
  const char *s = "{\"cameras\": {\"bench\": {\"camera_fps\": 5.1, \"detection_fps\": 0.2}}, \"service\": {}}";
  double fps = 0;
  CHECK(parse_stats_fps(s, strlen(s), cfg(), fps) && fps > 5);
  const char *z = "{\"cameras\": {\"bench\": {\"camera_fps\": 0.0}}}";
  CHECK(parse_stats_fps(z, strlen(z), cfg(), fps) && fps == 0);
  const char *none = "{\"cameras\": {\"door\": {\"camera_fps\": 5.0}}}";
  CHECK(!parse_stats_fps(none, strlen(none), cfg(), fps));
}

// A Tracker with Frigate up and streaming at `now`.
static void up(Tracker &t, uint32_t now) {
  t.available(true);
  t.detect(true, now);
  t.stats(true, 5.0, now);
}

static void test_veto() {
  Tracker t;
  t.cfg = cfg();
  t.seat_ms = 0;                                       // the stay in the seat: test_seat_stay
  uint32_t now = 1000;
  t.tick(now, false);
  CHECK(!t.veto(now));                                 // nothing heard from Frigate: radar only
  up(t, now);
  t.tick(now, false);
  CHECK(t.live() && !t.veto(now));                     // just came up: a full minute's grace
  now += 59000; up(t, now); t.tick(now, false);
  CHECK(!t.veto(now));
  now += 2000; up(t, now); t.tick(now, false);
  CHECK(t.veto(now));                                  // a minute without the owner in the seat
  t.event(parse(ev("new", "a", "[\"bench_seat\"]", "null", "null")), now);
  t.tick(now, false);
  CHECK(t.veto(now));                                  // someone in the seat, not named: still vetoed
  t.event(parse(ev("update", "a", "[\"bench_seat\"]", "null", "[\"owner\", 0.99]")), now);
  t.tick(now, false);
  CHECK(!t.veto(now));                                 // the owner
  now += 300000; up(t, now); t.tick(now, false);
  CHECK(!t.veto(now));                                 // sitting still, no updates for 5 min: still the owner
  t.event(parse(ev("update", "a", "[\"bench_seat\"]", "null", "null")), now);
  t.tick(now, false);
  CHECK(!t.veto(now));                                 // an update without a name keeps the earlier verdict
  t.event(parse(ev("update", "a", "[]", "null", "null")), now);   // left the seat, still tracked
  now += 61000; up(t, now); t.tick(now, false);
  CHECK(t.veto(now));
  t.event(parse(ev("update", "a", "[\"bench_seat\"]", "null", "null")), now);
  t.tick(now, false);
  CHECK(!t.veto(now));                                 // back in the seat, same track: still the owner
  t.event(parse(ev("end", "a", "[\"bench_seat\"]", "null", "null")), now);
  now += 61000; up(t, now); t.tick(now, false);
  CHECK(t.veto(now) && t.tracks() == 0);               // tracking ended
}

static void test_cat_and_others_never_count() {
  Tracker t;
  t.cfg = cfg();
  t.seat_ms = 0;                                       // the stay in the seat: test_seat_stay
  uint32_t now = 1000;
  up(t, now); t.tick(now, false);
  t.event(parse(ev("new", "c", "[\"bench_seat\"]", "null", "null", "null", "bench", "cat")), now);
  t.event(parse(ev("update", "d", "[\"bench_seat\"]", "[\"Someone\", 0.95]", "[\"none\", 0.9]")), now);
  now += 61000; up(t, now); t.tick(now, false);
  CHECK(t.veto(now));
}

static void test_frigate_down_means_radar_only() {
  Tracker t;
  t.cfg = cfg();
  t.seat_ms = 0;                                       // the stay in the seat: test_seat_stay
  uint32_t now = 1000;
  up(t, now); t.tick(now, false);
  now += 61000; up(t, now); t.tick(now, false);
  CHECK(t.veto(now));
  now += 31000; t.tick(now, false);                    // no detect heartbeat for 31 s
  CHECK(!t.veto(now) && !t.live());
  up(t, now); t.tick(now, false);
  CHECK(!t.veto(now));                                 // back: a fresh minute's grace
  t.available(false); t.tick(now, false);
  CHECK(!t.veto(now));                                 // "stopped" or "offline"
  up(t, now); t.stats(true, 0.0, now); t.tick(now, false);
  CHECK(!t.live());                                    // camera not streaming
  up(t, now); t.detect(false, now); t.tick(now, false);
  CHECK(!t.live());                                    // detect "offline" / "disabled"
  up(t, now); t.tick(now, false);
  now += 151000; t.detect(true, now); t.tick(now, false);
  CHECK(!t.live());                                    // stats stale
  up(t, now); t.tick(now, false);
  t.link_lost(); t.tick(now, false);
  CHECK(!t.live() && !t.veto(now));                    // MQTT down
}

static void test_switches() {
  Tracker t;
  t.cfg = cfg();
  t.seat_ms = 0;                                       // the stay in the seat: test_seat_stay
  uint32_t now = 1000;
  up(t, now); t.tick(now, false);
  now += 61000; up(t, now); t.tick(now, false);
  CHECK(t.veto(now));
  t.night_off = true;
  t.tick(now, false);
  CHECK(t.veto(now));                                  // stand-down set, but not quiet hours
  t.tick(now, true);
  CHECK(!t.veto(now));                                 // quiet hours: stood down
  t.night_off = false;
  t.tick(now, true);
  CHECK(!t.veto(now));                                 // live again: a fresh minute's grace
  t.enabled = false;
  now += 61000; up(t, now); t.tick(now, false);
  CHECK(!t.veto(now));                                 // switched off
}

static void test_track_table_full() {
  Tracker t;
  t.cfg = cfg();
  t.seat_ms = 0;                                       // the stay in the seat: test_seat_stay
  uint32_t now = 1000;
  up(t, now); t.tick(now, false);
  t.event(parse(ev("update", "owner", "[\"bench_seat\"]", "null", "[\"owner\", 0.99]")), now);
  for (int i = 0; i < 20; i++) {                       // many passers-by: the oldest rows go first
    char id[8];
    snprintf(id, sizeof(id), "p%d", i);
    now += 100;
    t.event(parse(ev("new", id, "[]", "null", "null")), now);
  }
  CHECK(t.tracks() == Tracker::MAX_TRACKS);
  CHECK(t.owner_in_seat(now));                            // the seated owner is never the row evicted
  now += 61000; up(t, now); t.tick(now, false);
  CHECK(!t.veto(now));
}

// A table full of people in the seat means "unsure": no veto.
static void test_overflow_means_no_veto() {
  Tracker t;
  t.cfg = cfg();
  t.seat_ms = 0;                                       // the stay in the seat: test_seat_stay
  uint32_t now = 1000;
  up(t, now); t.tick(now, false);
  for (int i = 0; i < Tracker::MAX_TRACKS + 1; i++) {
    char id[8];
    snprintf(id, sizeof(id), "s%d", i);
    t.event(parse(ev("new", id, "[\"bench_seat\"]", "null", "null")), now);
  }
  now += 61000; up(t, now); t.tick(now, false);
  CHECK(!t.veto(now));                                 // the ninth could have been the owner
  t.event(parse(ev("end", "s0", "[]", "null", "null")), now);
  t.tick(now, false);
  CHECK(t.veto(now));                                  // a row freed: sure again, nobody named
}

// The stats come up to a minute after the rest; what the camera said meanwhile is kept.
static void test_events_kept_while_waiting_for_stats() {
  Tracker t;
  t.cfg = cfg();
  t.seat_ms = 0;                                       // the stay in the seat: test_seat_stay
  uint32_t now = 1000;
  t.available(true); t.detect(true, now); t.tick(now, false);
  t.event(parse(ev("update", "a", "[\"bench_seat\"]", "null", "[\"owner\", 0.99]")), now);
  now += 30000; t.detect(true, now); t.tick(now, false);
  CHECK(t.owner_in_seat(now));                            // not wiped while waiting
  t.stats(true, 5.0, now); t.tick(now, false);
  CHECK(t.live() && t.seen());
  now += 300000; t.detect(true, now); t.stats(true, 5.0, now); t.tick(now, false);
  CHECK(!t.veto(now));                                 // the still owner, no new event
}

static void test_grace_and_stale_rows() {
  Tracker t;
  t.cfg = cfg();
  t.seat_ms = 0;                                       // the stay in the seat: test_seat_stay
  uint32_t now = 1000;
  up(t, now); t.tick(now, false);
  CHECK(t.in_grace(now) && !t.seen());
  t.event(parse(ev("update", "a", "[\"bench_seat\"]", "null", "[\"owner\", 0.99]")), now);
  t.tick(now, false);
  CHECK(t.seen() && !t.in_grace(now));
  t.event(parse(ev("new", "b", "[]", "null", "null")), now);       // someone passing by
  for (int i = 0; i < 11; i++) { now += 60000; up(t, now); t.tick(now, false); }
  CHECK(t.tracks() == 1);                              // the passer-by's lost "end" is assumed...
  CHECK(t.owner_in_seat(now) && !t.veto(now));            // ...but the still owner is kept, however long
}

static void test_detectors() {
  const char *ok = "{\"detectors\": {\"tensorrt\": {\"inference_speed\": 9.5, \"pid\": 1}},"
                   " \"cameras\": {\"bench\": {\"camera_fps\": 5.0}}}";
  const char *stuck = "{\"detectors\": {\"tensorrt\": {\"inference_speed\": 0.0}},"
                      " \"cameras\": {\"bench\": {\"camera_fps\": 5.0}}}";
  double fps = 0;
  bool dets = false;
  CHECK(parse_stats_fps(ok, strlen(ok), cfg(), fps, &dets) && dets);
  CHECK(parse_stats_fps(stuck, strlen(stuck), cfg(), fps, &dets) && !dets);
  Tracker t;
  t.cfg = cfg();
  t.seat_ms = 0;                                       // the stay in the seat: test_seat_stay
  t.available(true); t.detect(true, 1000); t.stats(true, 5.0, 1000, false); t.tick(1000, false);
  CHECK(!t.live());                                    // a stalled detector: radar only
}

// Someone taken for the owner counts only after a minute in the seat (a walk past of 7 s could otherwise end a
// warning). Short steps out of the seat keep the stay going; a longer one starts it again.
static void test_seat_stay() {
  Tracker t;
  t.cfg = cfg();
  CHECK(t.seat_ms == 60000);                           // the default
  uint32_t now = 1000;
  up(t, now); t.tick(now, false);
  now += 61000; up(t, now); t.tick(now, false);
  CHECK(t.veto(now));
  t.event(parse(ev("new", "w", "[\"bench_seat\"]", "null", "[\"owner\", 0.9]")), now);   // walks in
  t.tick(now, false);
  CHECK(t.veto(now));                                  // not yet a minute in the seat
  now += 7000; t.event(parse(ev("update", "w", "[]", "null", "null")), now);              // and out, 7 s later
  now += 60000; up(t, now); t.tick(now, false);
  CHECK(t.veto(now) && !t.owner_in_seat(now));         // never counted
  t.event(parse(ev("update", "w", "[\"bench_seat\"]", "null", "null")), now);             // back, after 60 s out
  now += 59000; up(t, now); t.tick(now, false);
  CHECK(t.veto(now));                                  // a new stay: 59 s is not enough
  now += 1000; up(t, now); t.tick(now, false);
  CHECK(!t.veto(now) && t.owner_in_seat(now));         // a minute in the seat, no message needed
  now += 300000; up(t, now); t.tick(now, false);
  CHECK(!t.veto(now));                                 // sitting still
  t.event(parse(ev("update", "w", "[]", "null", "null")), now);                           // leans out...
  now += 10000; t.event(parse(ev("update", "w", "[\"bench_seat\"]", "null", "null")), now);   // ...10 s
  up(t, now); t.tick(now, false);
  CHECK(!t.veto(now) && t.owner_in_seat(now));         // the stay goes on
  t.event(parse(ev("update", "w", "[]", "null", "null")), now);
  for (int k = 0; k < 11; k++) { now += 1000; up(t, now); t.tick(now, false); }   // 11 s out, ticking: the carry ends
  t.event(parse(ev("update", "w", "[\"bench_seat\"]", "null", "null")), now);
  up(t, now); t.tick(now, false);
  CHECK(!t.owner_in_seat(now));                        // a new stay...
  CHECK(!t.veto(now));                                 // ...but the last minute in the seat still holds the veto off
  now += 55000; up(t, now); t.tick(now, false);
  CHECK(t.veto(now));                                  // 61 s since he last counted (carried 5 s after he left)
  now += 10000; up(t, now); t.tick(now, false);
  CHECK(!t.veto(now));                                 // the new stay reached a minute
  t.event(parse(ev("new", "x", "[\"bench_seat\"]", "null", "null")), now);                // someone else sits...
  t.event(parse(ev("end", "w", "[]", "null", "null")), now);
  now += 30000; t.event(parse(ev("update", "x", "[\"bench_seat\"]", "null", "[\"owner\", 0.97]")), now);  // ...named later
  now += 30000; up(t, now); t.tick(now, false);
  CHECK(t.owner_in_seat(now));                         // the stay counts from sitting down, not from the name
}


// Another face name never counts against him (a face behind him is read inside his box).
static void test_other_face_is_no_verdict() {
  Event e = parse(ev("update", "f", "[\"bench_seat\"]", "[\"Someone\", 0.99]", "null"));
  CHECK(e.ok && e.owner == -1);
  e = parse(ev("update", "f", "[\"bench_seat\"]", "[\"Someone\", 0.99]", "[\"owner\", 0.99]"));
  CHECK(e.ok && e.owner == 1);
  Tracker t;
  t.cfg = cfg();
  t.seat_ms = 0;
  uint32_t now = 1000;
  up(t, now); t.tick(now, false);
  t.event(parse(ev("update", "f", "[\"bench_seat\"]", "null", "[\"owner\", 0.99]")), now);
  t.event(parse(ev("update", "f", "[\"bench_seat\"]", "[\"Someone\", 0.99]", "null")), now);   // another face behind him
  now += 61000; up(t, now); t.tick(now, false);
  CHECK(!t.veto(now));                                 // still him
}

// "Him" carries across track changes while the seat never stays empty more than empty_ms.
static void test_carry_across_tracks() {
  Tracker t;
  t.cfg = cfg();
  t.seat_ms = 0;
  CHECK(t.empty_ms == 5000);                           // the default
  uint32_t now = 1000;
  up(t, now); t.tick(now, false);
  t.event(parse(ev("update", "a", "[\"bench_seat\"]", "null", "[\"owner\", 0.99]")), now);
  t.tick(now, false);
  CHECK(t.carried());
  now += 1000;
  t.event(parse(ev("new", "b", "[\"bench_seat\"]", "null", "null")), now);    // Frigate splits him: a new track
  t.event(parse(ev("end", "a", "[\"bench_seat\"]", "null", "null")), now);
  for (int i = 0; i < 20; i++) { now += 60000; up(t, now); t.tick(now, false); }   // 20 min still, no update
  CHECK(!t.veto(now) && t.carried() && t.tracks() == 1);   // the untagged row in the seat is kept while carried
  t.event(parse(ev("update", "b", "[]", "null", "null")), now);               // gets up
  now += 4000; up(t, now); t.tick(now, false);
  CHECK(t.carried());                                  // 4 s empty: still carried
  t.event(parse(ev("update", "b", "[\"bench_seat\"]", "null", "null")), now);   // back
  now += 1000; up(t, now); t.tick(now, false);
  CHECK(t.carried());
  t.event(parse(ev("end", "b", "[]", "null", "null")), now);                  // leaves
  now += 6000; up(t, now); t.tick(now, false);
  CHECK(!t.carried());                                 // seat empty more than 5 s
  t.event(parse(ev("new", "c", "[\"bench_seat\"]", "null", "null")), now);    // someone sits, untagged
  now += 61000; up(t, now); t.tick(now, false);
  CHECK(t.veto(now));                                  // needs a fresh tag: the radar does not count
  t.event(parse(ev("update", "c", "[\"bench_seat\"]", "null", "[\"owner\", 0.95]")), now);
  t.tick(now, false);
  CHECK(!t.veto(now) && t.carried());
}

// The classifier naming the one in the seat as someone else ends the carry at once.
static void test_carry_ends_on_rival() {
  Tracker t;
  t.cfg = cfg();
  t.seat_ms = 0;
  uint32_t now = 1000;
  up(t, now); t.tick(now, false);
  t.event(parse(ev("update", "a", "[\"bench_seat\"]", "null", "[\"owner\", 0.99]")), now);
  t.tick(now, false);
  t.event(parse(ev("new", "x", "[\"bench_seat\"]", "null", "null")), now);    // a swap with no empty moment
  t.event(parse(ev("end", "a", "[]", "null", "null")), now);
  now += 1000; up(t, now); t.tick(now, false);
  CHECK(t.carried());                                  // the known hole: inherited for now
  t.event(parse(ev("update", "x", "[\"bench_seat\"]", "null", "[\"none\", 0.95]")), now);
  t.tick(now, false);
  CHECK(!t.carried());
  now += 61000; up(t, now); t.tick(now, false);
  CHECK(t.veto(now));
  t.link_lost(); t.tick(now, false);
  CHECK(!t.carried());
}


// An owner row kept long after its last update (a lost "end") must not outweigh someone the classifier names
// as not him.
static void test_stale_owner_row_yields_to_rival() {
  Tracker t;
  t.cfg = cfg();
  t.seat_ms = 0;
  uint32_t now = 1000;
  up(t, now); t.tick(now, false);
  t.event(parse(ev("update", "a", "[\"bench_seat\"]", "null", "[\"owner\", 0.99]")), now);
  t.tick(now, false);
  for (int i = 0; i < 11; i++) { now += 60000; up(t, now); t.tick(now, false); }   // no word for 11 min
  CHECK(!t.veto(now));                                 // kept: he may be sitting still
  t.event(parse(ev("new", "x", "[\"bench_seat\"]", "null", "[\"none\", 0.95]")), now);
  now += 1000; up(t, now); t.tick(now, false);
  CHECK(!t.carried() && !t.owner_in_seat(now));        // the stale row went
  now += 61000; up(t, now); t.tick(now, false);
  CHECK(t.veto(now));
}

int main() {
  test_json_reader();
  test_event_owner_by_classifier_and_face();
  test_event_filters();
  test_stats();
  test_veto();
  test_cat_and_others_never_count();
  test_frigate_down_means_radar_only();
  test_switches();
  test_track_table_full();
  test_overflow_means_no_veto();
  test_events_kept_while_waiting_for_stats();
  test_grace_and_stale_rows();
  test_detectors();
  test_seat_stay();
  test_other_face_is_no_verdict();
  test_carry_across_tracks();
  test_carry_ends_on_rival();
  test_stale_owner_row_yields_to_rival();
  printf("%d checks, %d failure(s)\n", checks, failures);
  return failures ? 1 : 0;
}
