// The OLED screens. Layout: 128x64, kept 2 px inside the left and right edges and 1 px inside the top and bottom
// (the burn-in shift moves the image by up to 1 px each way). An inverted header bar names the screen; big seven-segment digits carry the one number that matters.
#include "screens.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "gfx.h"
#include "pig.h"

namespace screens {
using namespace gfx;

static constexpr int L = 2, R = 125;            // safe area, x
static constexpr float PI_F = 3.14159265f;

// ---- helpers ------------------------------------------------------------------------------------------------
// "M:SS" under an hour, "H:MM" above (with an h shown by the caller); returns true for hours.
static bool fmt_time(uint32_t s, char *buf, size_t n) {
  if (s >= 3600) { snprintf(buf, n, "%u:%02u", (unsigned) (s / 3600), (unsigned) (s % 3600 / 60)); return true; }
  snprintf(buf, n, "%u:%02u", (unsigned) (s / 60), (unsigned) (s % 60));
  return false;
}
static void fmt_short(uint32_t s, char *buf, size_t n) {    // small text: "3h12" or "4:59"
  if (s >= 3600) snprintf(buf, n, "%uh%02u", (unsigned) (s / 3600), (unsigned) (s % 3600 / 60));
  else snprintf(buf, n, "%u:%02u", (unsigned) (s / 60), (unsigned) (s % 60));
}
static float frac(uint32_t left, uint32_t total) { return total ? (float) left / (float) total : 0; }
static bool blink(const Model &m, uint32_t period) { return (m.now_ms % period) < period / 2; }

// The icon table is private to gfx; the person drawn at 2x uses these copies.
static const uint8_t PERSON[8] = {0x38, 0x38, 0x10, 0x7C, 0x10, 0x10, 0x28, 0x44};
static const uint8_t PERSON_O[8] = {0x38, 0x28, 0x10, 0x7C, 0x10, 0x10, 0x28, 0x44};
static void bitmap8x(int x, int y, const uint8_t *rows, int s) {
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++)
      if (rows[r] & (0x80 >> c)) fill(x + c * s, y + r * s, s, s);
}

static void header(const char *title, Icon ic, const Model &m) {
  fill(L, 1, R - L + 1, 11);
  icon(L + 2, 3, ic, false);
  text(L + 13, 3, title, 1, false);
  int x = R - 9;
  icon(x, 3, m.link_ok && m.wifi_ok ? Icon::Wifi : Icon::NoLink, false);
  if (m.quiet) { x -= 10; icon(x, 3, Icon::Moon, false); }
  if (m.clock_ok) {
    char c[8];
    snprintf(c, sizeof(c), "%02u:%02u", m.hh, m.mm);
    if (L + 13 + text_width(title) + 6 < x - 3 - text_width(c)) text_right(x - 3, 3, c, 1, false);
  }
}

// One small item: an icon and a short value.
static void item(int x, int y, uint8_t what, const Model &m, bool right) {
  char v[24] = "";
  Icon ic = Icon::Clock;
  switch (what) {
    case I_CUT: if (m.cut_s >= 0) { fmt_short(m.cut_s, v, sizeof(v)); } ic = Icon::PersonOutline; break;
    case I_LIMIT: if (m.limit_s) fmt_short(m.limit_s, v, sizeof(v)); ic = Icon::Clock; break;
    case I_HOLD: if (m.hold_active) fmt_short(m.hold_s, v, sizeof(v)); ic = Icon::Hourglass; break;
    case I_LEASE: if (m.lease_s) fmt_short(m.lease_s, v, sizeof(v)); ic = Icon::Wifi; break;
    case I_POWER: if (m.watts >= 0) snprintf(v, sizeof(v), "%.0f W", m.watts); ic = Icon::Bolt; break;
    case I_PRESENCE:
      if (m.present) snprintf(v, sizeof(v), "here");
      else fmt_short(m.seen_ago_s, v, sizeof(v));
      ic = m.present ? Icon::Person : Icon::PersonOutline;
      break;
    case I_CLOCK: if (m.clock_ok) snprintf(v, sizeof(v), "%02u:%02u", m.hh, m.mm); ic = Icon::Clock; break;
    case I_ADDRESS: snprintf(v, sizeof(v), "%s", m.address); ic = Icon::Wifi; break;
    case I_TEMP: if (m.temp_c >= 0) snprintf(v, sizeof(v), "%.0f C", m.temp_c); ic = Icon::Power; break;
    default: return;
  }
  if (!v[0]) snprintf(v, sizeof(v), "--");
  if (what == I_ADDRESS) { if (right) text_right(x, y, v); else text(x, y, v); return; }   // no room for an icon
  if (right) { int w = text_width(v); text(x - w, y, v); icon(x - w - 10, y - 1, ic); }
  else { icon(x, y - 1, ic); text(x + 10, y, v); }
}

static const char *item_label(uint8_t what) {
  switch (what) {
    case I_CUT: return "CUT IN";
    case I_LIMIT: return "LIMIT IN";
    case I_HOLD: return "HOLD";
    case I_LEASE: return "LEASE";
    case I_POWER: return "POWER";
    case I_PRESENCE: return "LAST SEEN";
    case I_CLOCK: return "TIME";
    case I_TEMP: return "SHELLY";
    default: return "";
  }
}

// The big number of the status page, with its progress bar.
static void big_item(const Model &m, int x, int w) {
  char v[16] = "--";
  bool hours = false;
  float f = -1;
  switch (m.big) {
    case I_CUT: if (m.cut_s >= 0) { hours = fmt_time(m.cut_s, v, sizeof(v)); f = frac(m.cut_s, m.cut_total_s); } break;
    case I_LIMIT: hours = fmt_time(m.limit_s, v, sizeof(v)); f = frac(m.limit_s, m.limit_total_s); break;
    case I_HOLD: if (m.hold_active) { hours = fmt_time(m.hold_s, v, sizeof(v)); f = frac(m.hold_s, m.hold_total_s); } break;
    case I_LEASE: hours = fmt_time(m.lease_s, v, sizeof(v)); f = frac(m.lease_s, m.lease_total_s); break;
    case I_POWER: if (m.watts >= 0) snprintf(v, sizeof(v), "%.0f", m.watts); break;
    case I_CLOCK: if (m.clock_ok) snprintf(v, sizeof(v), "%02u:%02u", m.hh, m.mm); break;
    case I_TEMP: if (m.temp_c >= 0) snprintf(v, sizeof(v), "%.0f", m.temp_c); break;
    case I_PRESENCE: fmt_time(m.present ? 0 : m.seen_ago_s, v, sizeof(v)); break;
    case I_ADDRESS: text(x, 15, "ADDRESS"); text(x + (w - text_width(m.address)) / 2, 30, m.address); return;
    default: break;
  }
  text(x, 15, item_label(m.big));
  const char *unit = hours ? "h" : m.big == I_POWER ? "W" : m.big == I_TEMP ? "C" : "";
  int bw = big_width(v, 22) + (unit[0] ? 8 : 0);
  int bx = x + (w - bw) / 2;
  big_text(bx, 24, v, 21);
  if (unit[0]) text(bx + bw - 5, 38, unit);
  if (f >= 0) bar(x, 47, w, 5, f);
}

// The person at the left of the status page: filled and pulsing when seen, outlined with "ago" when not.
static void presence(const Model &m, int x, int y) {
  if (m.present) {
    bitmap8x(x, y, PERSON, 2);
    int r = 9 + (int) (m.now_ms % 1600 / 400);       // 9..12 px, a slow heartbeat
    circle(x + 8, y + 8, r, false);
  } else {
    bitmap8x(x, y, PERSON_O, 2);
    char a[12];
    fmt_short(m.seen_ago_s, a, sizeof(a));
    text(x + 8 - text_width(a) / 2, y + 20, a);
  }
}

// ---- the screens ---------------------------------------------------------------------------------------------
static void status(const Model &m) {
  if (m.phase == Phase::Unknown) {
    header("CHECKING", Icon::Radar, m);
    int a = (int) (m.now_ms / 4 % 360);
    arc(64, 34, 10, a, a + 270);
    arc(64, 34, 9, a, a + 270);
    text_center(51, "asking the Shelly");
    return;
  }
  if (m.phase == Phase::Off) {
    header("OFF", Icon::Power, m);
    circle(22, 34, 11, false);
    circle(22, 34, 10, false);
    fill(18, 20, 9, 6, false);
    fill(21, 21, 3, 13);
    text(44, 22, "The bar is off");
    text(44, 34, "Press ON to");
    text(44, 43, "turn it on");
    if (m.fault[0]) { fill(L, 53, R - L + 1, 9); text_center(54, m.fault, 1, false); }
    else item(L, 55, I_CLOCK, m, false);
    return;
  }
  header(m.phase == Phase::Hold ? "HOLD" : "ON", m.phase == Phase::Hold ? Icon::Hourglass : Icon::Bolt, m);
  presence(m, L + 3, 19);
  big_item(m, 30, R - 30);
  if (m.fault[0]) { fill(L, 53, R - L + 1, 9); text_center(54, m.fault, 1, false); return; }
  item(L + 2, 55, m.foot_l, m, false);
  item(R, 55, m.foot_r, m, true);
}

static void timers(const Model &m) {
  header("TIMERS", Icon::Clock, m);
  struct Row { Icon ic; const char *label; bool on; uint32_t left, total; };
  Row rows[4] = {
    {m.present ? Icon::Person : Icon::PersonOutline, "Cut in", m.cut_s >= 0, (uint32_t) (m.cut_s < 0 ? 0 : m.cut_s), m.cut_total_s},
    {Icon::Hourglass, "Hold", m.hold_active, m.hold_s, m.hold_total_s},
    {Icon::Clock, "Limit", m.limit_s > 0, m.limit_s, m.limit_total_s},
    {m.link_ok ? Icon::Wifi : Icon::NoLink, "Lease", m.lease_s > 0, m.lease_s, m.lease_total_s},
  };
  for (int i = 0; i < 4; i++) {
    int y = 14 + i * 12;          // last row ends at y 59
    icon(L + 1, y, rows[i].ic);
    text(L + 12, y + 1, rows[i].label);
    char v[12] = "--";
    if (rows[i].on) fmt_short(rows[i].left, v, sizeof(v));
    text_right(R, y + 1, v);
    if (rows[i].on) {
      int w = (int) ((R - (L + 12)) * frac(rows[i].left, rows[i].total));
      hline(L + 12, y + 9, w);
    } else {
      dotted_hline(L + 12, y + 9, R - (L + 12));
    }
  }
}

static void radar(const Model &m) {
  header("RADAR", Icon::Radar, m);
  // Top-down view: the radar at the top, its cone fanning down over the bench, one arc per gate (0.75 m).
  int cx = 84, cy = 13, gates = m.max_gate + 1;   // the cone on the right, readings on the left
  int step = gates ? 48 / gates : 12;
  for (int g = 1; g <= gates; g++) arc(cx, cy, g * step, 235, 305);
  line(cx, cy, cx + (int) (cosf(235 * PI_F / 180) * gates * step), cy - (int) (sinf(235 * PI_F / 180) * gates * step));
  line(cx, cy, cx + (int) (cosf(305 * PI_F / 180) * gates * step), cy - (int) (sinf(305 * PI_F / 180) * gates * step));
  // The sweep.
  float a = (235 + (m.now_ms / 20) % 70) * PI_F / 180;
  line(cx, cy, cx + (int) (cosf(a) * gates * step), cy - (int) (sinf(a) * gates * step));
  if (m.present && m.radar_ok) {
    int r = (int) (m.dist_cm * step / 75.0f);
    if (r > gates * step) r = gates * step;
    circle(cx, cy + r, blink(m, 600) ? 3 : 2, true);
  }
  char v[16];
  if (!m.radar_ok) { fill(L, 52, R - L + 1, 11); text_center(54, "NO RADAR DATA", 1, false); return; }
  if (m.present) snprintf(v, sizeof(v), "%u.%02um", m.dist_cm / 100, m.dist_cm % 100);
  else snprintf(v, sizeof(v), "nobody");
  text(L, 14, v);
  snprintf(v, sizeof(v), "mov %u", m.move_en);
  text(L, 25, v);
  snprintf(v, sizeof(v), "stl %u", m.still_en);
  text(L, 34, v);
}

static void menu(const Model &m) {
  header("STAY ON FOR", Icon::Hourglass, m);
  char v[16];
  const char *unit = "";
  if (m.menu_min == 0) {
    text_center(16, m.hold_active ? "CANCEL" : "NO HOLD", 2);
    text_center(34, m.hold_active ? "the running hold" : "nothing to set");
  } else {
    if (m.menu_hours || m.menu_min >= 60) {
      snprintf(v, sizeof(v), "%u:%02u", (unsigned) (m.menu_min / 60), (unsigned) (m.menu_min % 60));
      unit = "h";
    } else {
      snprintf(v, sizeof(v), "%u", (unsigned) m.menu_min);
      unit = "min";
    }
    int bw = big_width(v, 22), uw = text_width(unit), x = (128 - bw - uw - 4) / 2;
    big_text(x, 15, v, 21);
    text(x + bw + 4, 29, unit);
    text_center(38, m.menu_hours ? "steps of 1 h" : "steps of 5 min");
  }
  hline(L, 46, R - L + 1);
  text_center(47, m.menu_hours ? "ON ok  OVR +1h FN -1h" : "ON ok   OVR +   FN -");
  text_center(55, m.menu_hours ? "hold OVR:min FN:leave" : "hold OVR:h  FN:leave");
}

static void hold_to_cut(const Model &m) {
  header("TURNING OFF", Icon::Power, m);
  circle(64, 26, 9, false);
  circle(64, 26, 8, false);
  fill(60, 15, 9, 5, false);
  fill(63, 16, 3, 10);
  bar(L + 8, 40, R - L - 16, 9, m.hold_to_cut);
  text_center(53, "keep holding ON");
}

static void warning(const Model &m) {
  bool final = m.warn_s <= 15;
  const char *why = m.why == Why::Ceiling ? "4 H LIMIT" : m.why == Why::Hold ? "HOLD ENDING"
                    : m.why == Why::Link ? "NO LINK" : "NOBODY SEEN";
  Icon ic = m.why == Why::Ceiling ? Icon::Clock : m.why == Why::Hold ? Icon::Hourglass
            : m.why == Why::Link ? Icon::NoLink : Icon::PersonOutline;
  // Header row, and a border that pulses.
  int tw = text_width(why) + 12;
  icon((128 - tw) / 2, 3, ic);
  text((128 - tw) / 2 + 12, 4, why);
  if (blink(m, final ? 400 : 1000)) { rect(1, 1, 126, 62); rect(2, 2, 124, 60); }
  char v[16];
  bool hours = fmt_time(m.warn_s, v, sizeof(v));
  big_center(15, v, 24);
  if (hours) text(110, 32, "h");
  bar(L + 6, 43, R - L - 12, 6, frac(m.warn_s, m.warn_total_s));
  text_center(53, m.why == Why::Link ? "Shelly unreachable" : "press any key");
  if (final && blink(m, 1000)) invert(-1, -1, 130, 66);
}

static void alarm(const Model &m) {
  // A warning triangle, the message, and the whole screen flashing until silenced.
  line(16, 6, 4, 30); line(16, 6, 28, 30); hline(4, 30, 25);
  line(16, 8, 6, 29); line(16, 8, 26, 29);
  fill(15, 13, 3, 10); fill(15, 25, 3, 3);
  text(36, 6, "ALARM", 2);
  text(36, 24, m.alarm_stuck ? "Bar live but" : "Shelly lost,");
  text(36, 33, m.alarm_stuck ? "Shelly off" : "bar still live");
  fill(L, 43, R - L + 1, 10);
  text_center(45, "UNPLUG THE BAR", 1, false);
  text_center(55, m.alarm_silenced ? "silenced" : "hold FN: silence");
  if (!m.alarm_silenced && blink(m, 500)) invert(-1, -1, 130, 66);
}

static void toast(const Model &m) {
  round_rect(10, 16, 108, 32, 6, true);
  round_rect(12, 18, 104, 28, 5, true, false);
  icon(18, 28, m.toast_good ? Icon::Check : Icon::Bell);
  // One line if it fits in the box, else two, broken at the last space that fits.
  const int box = 84;
  int w = text_width(m.toast);
  if (w <= box) { text(30 + (box - w) / 2, 29, m.toast); return; }
  char a[32], b[32];
  int n = (int) strlen(m.toast), cut = 0;
  for (int i = 0; i < n && i < 31; i++) if (m.toast[i] == ' ' && i * 6 - 1 <= box) cut = i;
  if (cut == 0) cut = box / 6;
  snprintf(a, sizeof(a), "%.*s", cut, m.toast);
  snprintf(b, sizeof(b), "%s", m.toast + cut + (m.toast[cut] == ' ' ? 1 : 0));
  text(30 + (box - text_width(a)) / 2, 24, a);
  text(30 + (box - text_width(b)) / 2, 34, b);
}

static void boot(const Model &m) {
  circle(64, 20, 11, false);
  circle(64, 20, 10, false);
  fill(60, 7, 9, 6, false);
  fill(63, 8, 3, 11);
  text_center(33, "bench node");
  text_center(42, m.version);
  text_center(51, m.commit);
  int x = (int) (m.now_ms / 15 % 140) - 12;
  fill(x, 58, 12, 2);
}

// Idle: a small widget that drifts slowly over the screen (no burn-in) and still says whether you are seen and
// how long until the cut.
static void ambient(const Model &m) {
  if (m.pig) { pig(m); return; }
  const int ww = 58, wh = 20;
  float t = m.now_ms / 1000.0f;
  int x = L + (int) ((R - L - ww) * (0.5f + 0.5f * sinf(t / 47.0f)));
  int y = 1 + (int) ((62 - wh) * (0.5f + 0.5f * sinf(t / 29.0f + 1.0f)));
  // The radar cone and the person in it.
  arc(x + 9, y + 1, 17, 240, 300);
  line(x + 9, y + 1, x + 1, y + 15);
  line(x + 9, y + 1, x + 17, y + 15);
  if (m.present) {
    int r = 2 + (int) ((m.now_ms % 2000) / 700);
    circle(x + 9, y + 11, r, true);
  } else {
    circle(x + 9, y + 11, 2, false);
  }
  char v[16];
  if (m.phase == Phase::Hold && m.hold_active) { fmt_short(m.hold_s, v, sizeof(v)); icon(x + 22, y + 1, Icon::Hourglass); }
  else if (m.cut_s >= 0 && !m.present) { fmt_short(m.cut_s, v, sizeof(v)); icon(x + 22, y + 1, Icon::Clock); }
  else { snprintf(v, sizeof(v), "on"); icon(x + 22, y + 1, Icon::Bolt); }
  text(x + 32, y + 2, v);
  if (m.limit_s && m.limit_total_s) hline(x + 22, y + 13, (int) (34 * frac(m.limit_s, m.limit_total_s)));
  dotted_hline(x + 22, y + 15, 34);
}

void draw(const Model &m) {
  clear();
  switch (m.view) {
    case View::Dark: break;
    case View::Ambient: ambient(m); break;
    case View::Boot: boot(m); break;
    case View::Status: status(m); break;
    case View::Timers: timers(m); break;
    case View::Radar: radar(m); break;
    case View::Menu: menu(m); break;
    case View::HoldToCut: hold_to_cut(m); break;
    case View::Warning: warning(m); break;
    case View::Alarm: alarm(m); break;
    case View::Toast: toast(m); break;
  }
}

bool animated(const Model &m) {
  switch (m.view) {
    case View::Ambient: case View::Boot: case View::Radar: case View::Warning: case View::Alarm: return true;
    case View::Status: return m.present || m.phase == Phase::Unknown;
    default: return false;
  }
}

}  // namespace screens
