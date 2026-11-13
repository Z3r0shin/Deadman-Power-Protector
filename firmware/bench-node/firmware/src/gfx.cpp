#include "gfx.h"

#include <cmath>
#include <cstring>

#include "font5x7.inc"

namespace gfx {

static uint8_t fb[W * H / 8];
static int ox = 0, oy = 0;

uint8_t *buffer() { return fb; }
void clear() { memset(fb, 0, sizeof(fb)); }
void offset(int dx, int dy) { ox = dx; oy = dy; }

void pixel(int x, int y, bool on) {
  x += ox;
  y += oy;
  if (x < 0 || x >= W || y < 0 || y >= H) return;
  if (on) fb[(y >> 3) * W + x] |= 1 << (y & 7);
  else fb[(y >> 3) * W + x] &= ~(1 << (y & 7));
}

bool get(int x, int y) {
  x += ox;
  y += oy;
  if (x < 0 || x >= W || y < 0 || y >= H) return false;
  return fb[(y >> 3) * W + x] & (1 << (y & 7));
}

void hline(int x, int y, int w, bool on) { for (int i = 0; i < w; i++) pixel(x + i, y, on); }
void vline(int x, int y, int h, bool on) { for (int i = 0; i < h; i++) pixel(x, y + i, on); }

void line(int x0, int y0, int x1, int y1, bool on) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
  for (;;) {
    pixel(x0, y0, on);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * e;
    if (e2 >= dy) { e += dy; x0 += sx; }
    if (e2 <= dx) { e += dx; y0 += sy; }
  }
}

void rect(int x, int y, int w, int h, bool on) {
  hline(x, y, w, on);
  hline(x, y + h - 1, w, on);
  vline(x, y, h, on);
  vline(x + w - 1, y, h, on);
}

void fill(int x, int y, int w, int h, bool on) {
  for (int j = 0; j < h; j++) hline(x, y + j, w, on);
}

void round_rect(int x, int y, int w, int h, int r, bool filled, bool on) {
  for (int j = 0; j < h; j++) {
    int inset = 0;
    if (j < r) inset = r - (int) lroundf(sqrtf((float) (r * r - (r - j) * (r - j))));
    else if (j >= h - r) inset = r - (int) lroundf(sqrtf((float) (r * r - (j - (h - 1 - r)) * (j - (h - 1 - r)))));
    if (filled || j == 0 || j == h - 1) hline(x + inset, y + j, w - 2 * inset, on);
    else { pixel(x + inset, y + j, on); pixel(x + w - 1 - inset, y + j, on); }
  }
}

void circle(int cx, int cy, int r, bool filled, bool on) {
  int x = r, y = 0, e = 1 - r;
  while (x >= y) {
    if (filled) {
      hline(cx - x, cy + y, 2 * x + 1, on); hline(cx - x, cy - y, 2 * x + 1, on);
      hline(cx - y, cy + x, 2 * y + 1, on); hline(cx - y, cy - x, 2 * y + 1, on);
    } else {
      pixel(cx + x, cy + y, on); pixel(cx - x, cy + y, on); pixel(cx + x, cy - y, on); pixel(cx - x, cy - y, on);
      pixel(cx + y, cy + x, on); pixel(cx - y, cy + x, on); pixel(cx + y, cy - x, on); pixel(cx - y, cy - x, on);
    }
    y++;
    if (e < 0) e += 2 * y + 1; else { x--; e += 2 * (y - x) + 1; }
  }
}

void arc(int cx, int cy, int r, int a0, int a1, bool on) {
  int steps = r * 4 + 8;
  for (int i = 0; i <= steps; i++) {
    float a = (a0 + (a1 - a0) * (float) i / steps) * 3.14159265f / 180.0f;
    pixel(cx + (int) lroundf(cosf(a) * r), cy - (int) lroundf(sinf(a) * r), on);
  }
}

void invert(int x, int y, int w, int h) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) pixel(x + i, y + j, !get(x + i, y + j));
}

void dotted_hline(int x, int y, int w) { for (int i = 0; i < w; i += 2) pixel(x + i, y, true); }

int text(int x, int y, const char *s, int scale, bool on) {
  int x0 = x;
  for (; *s; s++, x += 6 * scale) {
    char c = (*s < 32 || *s > 126) ? '?' : *s;
    const uint8_t *g = FONT5X7[c - 32];
    for (int col = 0; col < 5; col++)
      for (int row = 0; row < 7; row++)
        if (g[col] & (1 << row)) fill(x + col * scale, y + row * scale, scale, scale, on);
  }
  return x - x0;
}

int text_width(const char *s, int scale) { int n = (int) strlen(s); return n ? n * 6 * scale - scale : 0; }
void text_center(int y, const char *s, int scale, bool on) { text((W - text_width(s, scale)) / 2, y, s, scale, on); }
void text_right(int xr, int y, const char *s, int scale, bool on) { text(xr - text_width(s, scale), y, s, scale, on); }

// Seven segments, a..g, with slanted ends: drawn as filled bars.
static const uint8_t SEGS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

static void seg_h(int x, int y, int len, int t, bool on) {
  for (int i = 0; i < t; i++) {
    int k = i < t / 2 ? t / 2 - i : i - t / 2;    // taper the ends
    hline(x + k, y + i, len - 2 * k, on);
  }
}
static void seg_v(int x, int y, int len, int t, bool on) {
  for (int i = 0; i < t; i++) {
    int k = i < t / 2 ? t / 2 - i : i - t / 2;
    vline(x + i, y + k, len - 2 * k, on);
  }
}

static int digit_w(int h) { return h / 2 + 2; }

static void big_digit(int x, int y, int d, int h, bool on) {
  int w = digit_w(h), t = h >= 20 ? 3 : 2, mid = y + h / 2 - t / 2;
  uint8_t m = SEGS[d];
  int hl = w - 2, vl = h / 2;
  if (m & 0x01) seg_h(x + 1, y, hl, t, on);                     // a
  if (m & 0x02) seg_v(x + w - t, y + 1, vl, t, on);             // b
  if (m & 0x04) seg_v(x + w - t, y + h / 2, vl, t, on);         // c
  if (m & 0x08) seg_h(x + 1, y + h - t, hl, t, on);             // d
  if (m & 0x10) seg_v(x, y + h / 2, vl, t, on);                 // e
  if (m & 0x20) seg_v(x, y + 1, vl, t, on);                     // f
  if (m & 0x40) seg_h(x + 1, mid, hl, t, on);                   // g
}

int big_width(const char *s, int h) {
  int w = 0;
  for (; *s; s++) w += (*s == ':' || *s == '.') ? (h >= 20 ? 6 : 4) : (*s == ' ' ? digit_w(h) / 2 : digit_w(h) + 3);
  return w > 0 ? w - 3 : 0;
}

int big_text(int x, int y, const char *s, int h, bool on) {
  int x0 = x, t = h >= 20 ? 3 : 2;
  for (; *s; s++) {
    if (*s >= '0' && *s <= '9') { big_digit(x, y, *s - '0', h, on); x += digit_w(h) + 3; }
    else if (*s == ':') { fill(x + 1, y + h / 3 - t / 2, t, t, on); fill(x + 1, y + 2 * h / 3 - t / 2, t, t, on); x += h >= 20 ? 6 : 4; }
    else if (*s == '.') { fill(x + 1, y + h - t, t, t, on); x += h >= 20 ? 6 : 4; }
    else if (*s == '-') { seg_h(x + 1, y + h / 2 - t / 2, digit_w(h) - 2, t, on); x += digit_w(h) + 3; }
    else x += digit_w(h) / 2;
  }
  return x - x0;
}

void big_center(int y, const char *s, int h, bool on) { big_text((W - big_width(s, h)) / 2, y, s, h, on); }

// 8x8 icons, one byte per row, MSB on the left.
static const uint8_t ICONS[][8] = {
  {0x38, 0x38, 0x10, 0x7C, 0x10, 0x10, 0x28, 0x44},  // Person
  {0x38, 0x28, 0x10, 0x7C, 0x10, 0x10, 0x28, 0x44},  // PersonOutline
  {0x0C, 0x18, 0x30, 0x7E, 0x0C, 0x18, 0x30, 0x20},  // Bolt
  {0x00, 0x7E, 0x81, 0x3C, 0x42, 0x18, 0x18, 0x00},  // Wifi
  {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81},  // NoLink
  {0xFE, 0x44, 0x28, 0x10, 0x28, 0x54, 0xFE, 0x00},  // Hourglass
  {0x1C, 0x30, 0x60, 0x60, 0x60, 0x30, 0x1C, 0x00},  // Moon
  {0x3C, 0x42, 0x91, 0x91, 0x9D, 0x81, 0x42, 0x3C},  // Clock
  {0x00, 0x01, 0x02, 0x04, 0x88, 0x50, 0x20, 0x00},  // Check
  {0x18, 0x5A, 0x99, 0x99, 0x81, 0x81, 0x42, 0x3C},  // Power
  {0x18, 0x3C, 0x3C, 0x3C, 0x7E, 0xFF, 0x00, 0x18},  // Bell
  {0x00, 0x81, 0x42, 0x24, 0x24, 0x18, 0x18, 0x00},  // Radar (a cone)
  {0x10, 0x38, 0x7C, 0xFE, 0x38, 0x38, 0x38, 0x00},  // ArrowUp
  {0x38, 0x38, 0x38, 0xFE, 0x7C, 0x38, 0x10, 0x00},  // ArrowDown
};

void icon(int x, int y, Icon i, bool on) {
  const uint8_t *g = ICONS[(int) i];
  for (int row = 0; row < 8; row++)
    for (int col = 0; col < 8; col++)
      if (g[row] & (0x80 >> col)) pixel(x + col, y + row, on);
}

void bar(int x, int y, int w, int h, float f) {
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  rect(x, y, w, h);
  int fw = (int) lroundf((w - 4) * f);
  if (fw > 0) fill(x + 2, y + 2, fw, h - 4);
}

}  // namespace gfx
