// Frigate (the camera system): reads its MQTT messages and says whether the bench's user ("the owner" below and
// in the code) is in the bench seat.
// The camera only ever takes presence away, never adds it: when Frigate is running and has not seen the owner in
// the seat for veto_ms, the radar's presence does not count. Frigate down, silent or unsure: the radar alone
// decides. The owner counts as in the seat only after seat_ms there, so someone walking past who is taken for
// him does not end the countdown. Plain C++, no Arduino: the PC tests use it as is (test/test_frigate.cpp).
//
// Messages used (Frigate's MQTT docs, https://docs.frigate.video/integrations/mqtt/, and the messages a running
// Frigate was seen to send):
//   frigate/available               "online" / "stopped" / "offline" (its last will)
//   frigate/<camera>/status/detect  "online" every 10 s while the camera is analysed
//   frigate/stats                   JSON every 60 s; cameras.<camera>.camera_fps > 0 while the camera streams
//   frigate/events                  JSON per tracked object: type new/update/end, after.{id, camera, label,
//                                   current_zones, sub_label ["name", score], <model> ["class", score], end_time}
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace frigate {

// ---- a minimal JSON reader: finds values by key in one object, without building anything ------------------
struct Span {
  const char *p;
  size_t n;
  Span() : p(nullptr), n(0) {}
  Span(const char *ptr, size_t len) : p(ptr), n(len) {}   // the device's compiler wants it spelled out
  bool ok() const { return p != nullptr; }
};

inline size_t ws(const char *s, size_t n, size_t i) {
  while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
  return i;
}

// Index just past the value that starts at i, or n when the text ends first (an error: callers reject it).
inline size_t skip_value(const char *s, size_t n, size_t i) {
  i = ws(s, n, i);
  if (i >= n) return n;
  if (s[i] == '"') {
    for (i++; i < n; i++) {
      if (s[i] == '\\') i++;
      else if (s[i] == '"') return i + 1;
    }
    return n;
  }
  if (s[i] == '{' || s[i] == '[') {
    int depth = 0;
    while (i < n) {
      char c = s[i];
      if (c == '"') { i = skip_value(s, n, i); continue; }
      if (c == '{' || c == '[') depth++;
      else if (c == '}' || c == ']') { if (--depth == 0) return i + 1; }
      i++;
    }
    return n;
  }
  while (i < n && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' && s[i] != '\n' && s[i] != '\r' &&
         s[i] != '\t')
    i++;
  return i;
}

// The value of `key` in the object `obj` (top level of that object only); not ok() when absent or malformed.
inline Span get(Span obj, const char *key) {
  const char *s = obj.p;
  size_t n = obj.n, i = ws(s, n, 0);
  if (!s || i >= n || s[i] != '{') return {};
  size_t klen = strlen(key);
  for (i++;;) {
    i = ws(s, n, i);
    if (i >= n || s[i] != '"') return {};
    size_t ks = i + 1, ke = skip_value(s, n, i);
    if (ke >= n) return {};
    i = ws(s, n, ke);
    if (i >= n || s[i] != ':') return {};
    i = ws(s, n, i + 1);
    size_t ve = skip_value(s, n, i);
    if (ve >= n) return {};
    if (ke - 1 - ks == klen && memcmp(s + ks, key, klen) == 0) return Span{s + i, ve - i};
    i = ws(s, n, ve);
    if (i < n && s[i] == ',') { i++; continue; }
    return {};
  }
}

inline bool is_null(Span v) { return !v.ok() || (v.n >= 4 && memcmp(v.p, "null", 4) == 0); }

// Element `k` of the array `arr`; not ok() when absent.
inline Span at(Span arr, int k) {
  const char *s = arr.p;
  size_t n = arr.n, i = ws(s, n, 0);
  if (!s || i >= n || s[i] != '[') return {};
  for (i++;; k--) {
    i = ws(s, n, i);
    if (i >= n || s[i] == ']') return {};
    size_t ve = skip_value(s, n, i);
    if (ve >= n) return {};
    if (k == 0) return Span{s + i, ve - i};
    i = ws(s, n, ve);
    if (i < n && s[i] == ',') { i++; continue; }
    return {};
  }
}

// Member `k` of the object `obj` (its key and value); false when absent.
inline bool member(Span obj, int k, Span &key, Span &val) {
  const char *s = obj.p;
  size_t n = obj.n, i = ws(s, n, 0);
  if (!s || i >= n || s[i] != '{') return false;
  for (i++;; k--) {
    i = ws(s, n, i);
    if (i >= n || s[i] != '"') return false;
    size_t ke = skip_value(s, n, i);
    if (ke >= n) return false;
    size_t ks = i;
    i = ws(s, n, ke);
    if (i >= n || s[i] != ':') return false;
    i = ws(s, n, i + 1);
    size_t ve = skip_value(s, n, i);
    if (ve >= n) return false;
    if (k == 0) { key = Span(s + ks, ke - ks); val = Span(s + i, ve - i); return true; }
    i = ws(s, n, ve);
    if (i < n && s[i] == ',') { i++; continue; }
    return false;
  }
}

// Calls fn(element) for each element of the array `arr`, in one pass; stops when fn returns true.
template <typename F> inline bool each(Span arr, F fn) {
  const char *s = arr.p;
  size_t n = arr.n, i = ws(s, n, 0);
  if (!s || i >= n || s[i] != '[') return false;
  for (i++;;) {
    i = ws(s, n, i);
    if (i >= n || s[i] == ']') return false;
    size_t ve = skip_value(s, n, i);
    if (ve >= n) return false;
    if (fn(Span(s + i, ve - i))) return true;
    i = ws(s, n, ve);
    if (i < n && s[i] == ',') { i++; continue; }
    return false;
  }
}

inline void put_utf8(char *out, size_t cap, size_t &o, uint32_t cp, bool &fit) {
  char b[4];
  size_t m;
  if (cp < 0x80) { b[0] = (char) cp; m = 1; }
  else if (cp < 0x800) { b[0] = (char) (0xC0 | cp >> 6); b[1] = (char) (0x80 | (cp & 0x3F)); m = 2; }
  else if (cp < 0x10000) {
    b[0] = (char) (0xE0 | cp >> 12); b[1] = (char) (0x80 | ((cp >> 6) & 0x3F)); b[2] = (char) (0x80 | (cp & 0x3F));
    m = 3;
  } else {
    b[0] = (char) (0xF0 | cp >> 18); b[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
    b[2] = (char) (0x80 | ((cp >> 6) & 0x3F)); b[3] = (char) (0x80 | (cp & 0x3F)); m = 4;
  }
  if (o + m >= cap) { fit = false; return; }
  memcpy(out + o, b, m);
  o += m;
}

inline int hex4(const char *p) {
  int v = 0;
  for (int k = 0; k < 4; k++) {
    char c = p[k];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= c - '0';
    else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
    else return -1;
  }
  return v;
}

// The string value `v`, unescaped to UTF-8 (\uXXXX included: Frigate's Python may escape every non-ASCII
// character). False when `v` is not a string or does not fit.
inline bool str(Span v, char *out, size_t cap) {
  if (!v.ok() || v.n < 2 || v.p[0] != '"' || cap == 0) return false;
  size_t o = 0, end = v.n - 1;
  bool fit = true;
  for (size_t i = 1; i < end && fit; i++) {
    char c = v.p[i];
    if (c != '\\') {
      if (o + 1 >= cap) return false;
      out[o++] = c;
      continue;
    }
    if (++i >= end) return false;
    switch (v.p[i]) {
      case 'n': c = '\n'; break;
      case 't': c = '\t'; break;
      case 'r': c = '\r'; break;
      case 'b': c = '\b'; break;
      case 'f': c = '\f'; break;
      case 'u': {
        if (i + 4 >= end) return false;
        int u = hex4(v.p + i + 1);
        if (u < 0) return false;
        i += 4;
        uint32_t cp = (uint32_t) u;
        if (cp >= 0xD800 && cp < 0xDC00 && i + 6 < end && v.p[i + 1] == '\\' && v.p[i + 2] == 'u') {
          int lo = hex4(v.p + i + 3);
          if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (uint32_t) (lo - 0xDC00); i += 6; }
        }
        put_utf8(out, cap, o, cp, fit);
        continue;
      }
      default: c = v.p[i]; break;            // \" \\ \/
    }
    if (o + 1 >= cap) return false;
    out[o++] = c;
  }
  if (!fit) return false;
  out[o] = '\0';
  return true;
}

inline bool str_is(Span v, const char *want) {
  char b[64];
  return want && want[0] && str(v, b, sizeof(b)) && strcmp(b, want) == 0;
}

inline bool num(Span v, double &out) {
  if (!v.ok() || v.n == 0 || v.n > 31 || is_null(v)) return false;
  char b[32];
  memcpy(b, v.p, v.n);
  b[v.n] = '\0';
  char *e = nullptr;
  out = strtod(b, &e);
  return e != b;
}

// ---- what the bench node looks for -------------------------------------------------------------------------
struct Config {
  char camera[32] = "bench";
  char zone[32] = "bench_seat";
  char model[32] = "Bench_person";   // the person classifier's name, as Frigate spells it
  char cls[32] = "owner";            // the classifier's class for the owner
  char face[64] = "";                // the owner's name in the face library (UTF-8); "" = do not use faces
};

// One frigate/events message, reduced to what matters.
struct Event {
  bool ok = false;        // a parsable event for our camera and a person
  char id[48] = "";
  bool end = false;       // tracking ended (or a false positive): forget it
  bool in_zone = false;   // in the seat zone now
  int owner = -1;         // 1 named as the owner, 0 named as someone/something else, -1 not named in this message
};

// The owner verdict of one recogniser's value: ["name", score], or a plain "name", or null.
inline int verdict(Span v, const char *want) {
  if (is_null(v) || !want || !want[0]) return -1;
  Span name = v.p[0] == '[' ? at(v, 0) : v;
  if (is_null(name)) return -1;
  return str_is(name, want) ? 1 : 0;
}

inline Event parse_event(const char *s, size_t n, const Config &c) {
  Event e;
  Span root{s, n};
  Span after = get(root, "after");
  if (!after.ok()) return e;
  if (!str_is(get(after, "camera"), c.camera) || !str_is(get(after, "label"), "person")) return e;
  if (!str(get(after, "id"), e.id, sizeof(e.id)) || !e.id[0]) return e;
  Span fp = get(after, "false_positive");
  e.end = str_is(get(root, "type"), "end") || !is_null(get(after, "end_time")) ||
          (fp.ok() && fp.n >= 4 && memcmp(fp.p, "true", 4) == 0);
  e.in_zone = each(get(after, "current_zones"), [&](Span z) { return str_is(z, c.zone); });
  int face = verdict(get(after, "sub_label"), c.face);
  int model = verdict(get(after, c.model), c.cls);
  // Another face name never counts against him: a face behind him is read inside his box.
  e.owner = (face == 1 || model == 1) ? 1 : model == 0 ? 0 : -1;
  e.ok = true;
  return e;
}

// From frigate/stats: cameras.<camera>.camera_fps (false when not found), and whether every detector reported in
// detectors.<name>.inference_speed is working (> 0). A stats message with no detectors section leaves that true.
inline bool parse_stats_fps(const char *s, size_t n, const Config &c, double &fps, bool *detectors_ok = nullptr) {
  Span root(s, n);
  if (detectors_ok) {
    *detectors_ok = true;
    Span dets = get(root, "detectors"), key, val;
    for (int k = 0; member(dets, k, key, val); k++) {
      double ms = 0;
      if (!num(get(val, "inference_speed"), ms) || !(ms > 0)) *detectors_ok = false;
    }
  }
  Span cam = get(get(root, "cameras"), c.camera);
  return num(get(cam, "camera_fps"), fps);
}

// ---- the state kept between messages -----------------------------------------------------------------------
class Tracker {
 public:
  Config cfg;
  bool enabled = true;           // the camera checks the radar at all
  bool night_off = false;        // stand down in quiet hours (off by default)
  uint32_t veto_ms = 60000;      // owner not seen in the seat this long, while Frigate runs: radar presence ignored
  uint32_t dead_ms = 30000;      // no detect heartbeat this long: Frigate counts as down
  uint32_t stats_ms = 150000;    // no stats this long: down (they come every 60 s)
  uint32_t row_ms = 600000;      // a tracked person not updated this long is forgotten (a lost "end")
  uint32_t seat_ms = 60000;      // the owner counts as in the seat only after this long there
  uint32_t away_ms = 10000;      // out of the seat this long or less (leaning, the box jumping) keeps the stay going
  uint32_t empty_ms = 5000;      // seat seen empty longer than this: "him" is no longer carried to the next track

  static constexpr int MAX_TRACKS = 8;

  void available(bool online) {
    available_ = online;
    if (!online) clear();
  }
  void detect(bool online, uint32_t now) { detect_ok_ = online; detect_at_ = now; have_detect_ = true; }
  void stats(bool found, double fps, uint32_t now, bool detectors_ok = true) {
    stats_at_ = now;
    have_stats_ = true;
    fps_ok_ = found && fps > 0.5 && detectors_ok;
  }
  void link_lost() { available_ = have_detect_ = have_stats_ = false; clear(); }

  void event(const Event &e, uint32_t now) {
    if (!e.ok) return;
    int i = find(e.id);
    if (e.end) { if (i >= 0) t_[i].used = false; return; }
    if (i < 0) i = slot(e.id, now);
    if (i < 0) { overflow_ = true; return; }   // table full: unsure who is there, so no veto until a row frees
    Track &t = t_[i];
    if (e.in_zone && !t.in_zone && (!t.stayed || now - t.left_at > away_ms)) t.zone_at = now;   // a new stay
    if (e.in_zone) t.stayed = true;
    if (!e.in_zone && t.in_zone) t.left_at = now;
    t.in_zone = e.in_zone;
    if (e.owner >= 0) { t.owner = e.owner == 1; t.named = true; }
    t_[i].at = now;
  }

  // Frigate running and the camera streaming.
  bool healthy(uint32_t now) const {
    return available_ && have_detect_ && detect_ok_ && now - detect_at_ <= dead_ms && have_stats_ &&
           fps_ok_ && now - stats_at_ <= stats_ms;
  }
  // The owner in the seat for seat_ms (the stay counted from when that tracked person sat down).
  bool owner_in_seat(uint32_t now) const {
    for (const Track &t : t_)
      if (t.used && t.in_zone && t.owner && now - t.zone_at >= seat_ms) return true;
    return false;
  }
  bool in_grace(uint32_t now) const { return live_ && !seen_ && now - owner_at_ < veto_ms; }

  // Call often (every loop). Keeps the time the owner was last in the seat; a newly healthy Frigate gets a full
  // veto_ms before it can veto anything.
  void tick(uint32_t now, bool quiet) {
    bool h = healthy(now);
    bool live = enabled && h && !(night_off && quiet);
    if (live && !live_) { owner_at_ = now; seen_ = false; }   // the grace minute
    if (!h && healthy_) clear();                             // just went down: what it said is stale
    healthy_ = h;
    live_ = live;
    // A row not updated for row_ms lost its "end" - except the owner in the seat: Frigate may say nothing about a
    // person sitting still, so that row stays (at worst the camera stands down: radar only).
    // While "him" is carried, any row in the seat stays too, for the same reason.
    for (Track &t : t_)
      if (t.used && !(t.in_zone && (t.owner || carry_)) && now - t.at > row_ms) t.used = false;
    if (overflow_ && tracks() < MAX_TRACKS) overflow_ = false;
    // Carry "him" across track changes while the seat never stays empty more than empty_ms:
    // Frigate splits a seated person into new tracks that start untagged. The classifier naming the one in the
    // seat as someone else (and nobody in it as him) ends the carry at once.
    bool occupied = false, tagged = false, rival = false;
    for (const Track &t : t_) {
      if (!t.used || !t.in_zone) continue;
      occupied = true;
      if (t.owner) tagged |= now - t.at <= row_ms; else if (t.named) rival = true;
    }
    // A stale owner row (kept because a still person may get no update, but maybe a lost "end") does not outweigh
    // someone the classifier names as not him: that row goes.
    if (rival && !tagged)
      for (Track &t : t_)
        if (t.used && t.in_zone && t.owner && now - t.at > row_ms) t.used = false;
    if (occupied) seat_at_ = now;
    if (live && owner_in_seat(now)) carry_ = true;
    if (!live || (!occupied && now - seat_at_ > empty_ms) || (rival && !tagged)) carry_ = false;
    if (live && (owner_in_seat(now) || carry_)) { owner_at_ = now; seen_ = true; }
  }
  // The radar's presence does not count right now. Never while unsure (the table overflowed).
  bool veto(uint32_t now) const { return live_ && !overflow_ && now - owner_at_ >= veto_ms; }
  bool live() const { return live_; }
  bool unsure() const { return overflow_; }   // the table overflowed: no veto until a row frees
  bool carried() const { return carry_; }    // "him" carried from an earlier track while the seat stayed occupied
  bool seen() const { return seen_; }   // the owner was seen since Frigate came up (else owner_ago is the grace)
  uint32_t owner_ago_s(uint32_t now) const { return (now - owner_at_) / 1000; }
  int tracks() const {
    int k = 0;
    for (const Track &t : t_) k += t.used;
    return k;
  }

 private:
  struct Track {
    bool used = false, in_zone = false, owner = false;
    bool stayed = false;          // has been in the seat during this tracking
    bool named = false;           // a recogniser has said who this is (owner or not)
    char id[48] = "";
    uint32_t at = 0;
    uint32_t zone_at = 0;         // when the current stay in the seat began
    uint32_t left_at = 0;         // when it last left the seat
  };
  Track t_[MAX_TRACKS];
  bool available_ = false, have_detect_ = false, detect_ok_ = false, have_stats_ = false, fps_ok_ = false;
  bool live_ = false, healthy_ = false, seen_ = false, overflow_ = false, carry_ = false;
  uint32_t detect_at_ = 0, stats_at_ = 0, owner_at_ = 0, seat_at_ = 0;

  int find(const char *id) const {
    for (int i = 0; i < MAX_TRACKS; i++)
      if (t_[i].used && strcmp(t_[i].id, id) == 0) return i;
    return -1;
  }
  // A free row, else the stalest row of someone neither in the seat nor the owner; -1 when none can go.
  int slot(const char *id, uint32_t now) {
    int best = -1;
    for (int i = 0; i < MAX_TRACKS; i++) {
      if (!t_[i].used) { best = i; break; }
      if (t_[i].in_zone || t_[i].owner) continue;
      if (best < 0 || now - t_[i].at > now - t_[best].at) best = i;
    }
    if (best < 0) return -1;
    Track &t = t_[best];
    t = Track();
    t.used = true;
    snprintf(t.id, sizeof(t.id), "%s", id);
    return best;
  }
  void clear() {
    for (Track &t : t_) t.used = false;
    overflow_ = carry_ = false;
  }
};

}  // namespace frigate
