// The idle pig: a pixel-art pig that drifts sideways over the screen when nothing else is shown. It proves the
// node is alive by moving, and shows the state at a glance:
//   bar off                         idle: passing the time, a new scene every pig_scene_ms;
//   bar on, you seen                working at the bench, a new scene every pig_scene_ms (also during a hold);
//   hold running, you not seen      held: in sunglasses, waiting;
//   bar on, you not seen            unseen: one scene per step of the countdown to the warning - looking (first
//                                   third), looking harder (second third), worried (two thirds to five sixths), panic (last sixth).
// The sprites and scenes are in pig_sprites.inc and pig_scenes.inc (kept as drawn, so a new set can replace them
// whole). This file only picks the scene. Plain C++ on gfx, like the other
// screens, so the PC renderer draws it too.
#include "pig.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "gfx.h"

namespace screens {
using namespace gfx;

namespace {

// Draws a sprite with its top-left at (x, y); flip mirrors it left-right. The width is the first row's.
void sprite(int x, int y, const char *const *rows, int n, bool flip = false) {
  int w = (int) strlen(rows[0]);
  for (int r = 0; r < n; r++)
    for (int c = 0; c < w; c++)
      if (rows[r][c] == '#') pixel(flip ? x + w - 1 - c : x + c, y + r);
}

#include "pig_sprites.inc"
#include "pig_scenes.inc"
constexpr int BOX_W = 64;                    // each scene fills a 64 x 64 box: full height, drifting sideways

}  // namespace

// The step of the countdown to the warning (1 to 4): thirds of the time, the last sixth for the last step.
static uint8_t unseen_step(const Model &m) {
  int32_t before_warn = (int32_t) m.cut_total_s - (int32_t) m.warn_total_s;
  float worry = before_warn > 0 ? 1.0f - (float) (m.cut_s - (int32_t) m.warn_total_s) / (float) before_warn : 1.0f;
  if (worry < 1.0f / 3.0f) return 1;
  if (worry < 2.0f / 3.0f) return 2;
  if (worry < 5.0f / 6.0f) return 3;
  return 4;
}

void pig(const Model &m) {
  float s = m.now_ms / 1000.0f;
  int x = (int) ((128 - BOX_W) * (0.5f + 0.5f * sinf(s / 53.0f)));
  PigState state;
  uint8_t step = 0;
  if (m.phase == Phase::Off) state = PIG_IDLE;
  else if (m.present || (m.cut_s < 0 && !m.hold_active)) state = PIG_WORKING;
  else if (m.hold_active) state = PIG_HELD;
  else { state = PIG_UNSEEN; step = unseen_step(m); }
  int n = 0;
  for (int i = 0; i < PIG_SCENE_COUNT; i++) n += PIG_SCENES[i].state == state && PIG_SCENES[i].step == step;
  if (n == 0) return;
  uint32_t per = m.pig_scene_ms ? m.pig_scene_ms : 20000;
  int pick = (int) ((m.now_ms / per) % (uint32_t) n);
  for (int i = 0; i < PIG_SCENE_COUNT; i++) {
    if (PIG_SCENES[i].state != state || PIG_SCENES[i].step != step) continue;
    if (pick-- == 0) { PIG_SCENES[i].draw(x, 0, m.now_ms); return; }
  }
}

}  // namespace screens
