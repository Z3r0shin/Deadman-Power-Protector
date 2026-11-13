// Monochrome 128x64 drawing into a frame buffer (SSD1306 page layout). Plain C++: the same code draws on the bench
// node and in the PC renderer (../tools/render.cpp), so every screen can be looked at before it is flashed.
#pragma once
#include <cstdint>

namespace gfx {

constexpr int W = 128, H = 64;

uint8_t *buffer();                         // W * H / 8 bytes, page by page
void clear();
void offset(int dx, int dy);               // shifts everything drawn after it (burn-in)
void pixel(int x, int y, bool on = true);
bool get(int x, int y);
void hline(int x, int y, int w, bool on = true);
void vline(int x, int y, int h, bool on = true);
void line(int x0, int y0, int x1, int y1, bool on = true);
void rect(int x, int y, int w, int h, bool on = true);          // outline
void fill(int x, int y, int w, int h, bool on = true);
void round_rect(int x, int y, int w, int h, int r, bool filled = false, bool on = true);
void circle(int cx, int cy, int r, bool filled = false, bool on = true);
void arc(int cx, int cy, int r, int a0, int a1, bool on = true);  // degrees, 0 = right, counter-clockwise
void invert(int x, int y, int w, int h);
void dotted_hline(int x, int y, int w);

// 5x7 text, scaled. Returns the width drawn.
int text(int x, int y, const char *s, int scale = 1, bool on = true);
int text_width(const char *s, int scale = 1);
void text_center(int y, const char *s, int scale = 1, bool on = true);
void text_right(int xr, int y, const char *s, int scale = 1, bool on = true);

// Big seven-segment style digits for countdowns: "12:34". h is the digit height (w = h / 2 + 2).
int big_text(int x, int y, const char *s, int h, bool on = true);
int big_width(const char *s, int h);
void big_center(int y, const char *s, int h, bool on = true);

// 8x8 icons.
enum class Icon : uint8_t { Person, PersonOutline, Bolt, Wifi, NoLink, Hourglass, Moon, Clock, Check, Power, Bell,
                            Radar, ArrowUp, ArrowDown };
void icon(int x, int y, Icon i, bool on = true);

// Progress bar: frame plus a fill of fraction 0..1.
void bar(int x, int y, int w, int h, float fraction);

}  // namespace gfx
