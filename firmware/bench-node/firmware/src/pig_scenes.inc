// The pig's scenes, 18 of them: 7 idle, 6 working at a hobby or electronics bench,
// 1 held, 4 unseen. Included by pig.cpp inside its namespace, after pig_sprites.inc and after its
// sprite(x, y, rows, n, flip); replace this file whole with a new set of scenes.
//
// Each scene is a function void NAME(int x, int y, uint32_t t). It draws one frame inside a 64 x 64 box whose
// top-left corner is (x, y), at time t in milliseconds. It keeps no state: the same t always gives the same
// frame, and t may run for days (a single odd frame when it wraps after 49 days). The box is the full height of
// the screen, so it can only move sideways: x from 0 to 64, y = 0. Nothing is drawn outside the box at any t.
//
// Uses only gfx calls (pixel, hline, fill, circle, ...) and sinf, cosf and lroundf from <cmath>, which pig.cpp
// includes before its namespace. No allocation and no doubles.
//
// PIG_SCENES at the end lists every scene with its name and state, for the code that picks which one plays.

// ---------------------------------------------------------------- shared helpers

static int pig_abs(int v) { return v < 0 ? -v : v; }


static uint32_t h32(uint32_t v) {            // integer hash: same input, same output, so frames stay stateless
  v ^= v >> 16; v *= 0x7feb352dU; v ^= v >> 15; v *= 0x846ca68bU; v ^= v >> 16;
  return v;
}

// Draws a 64-row picture; when `in` is true, rows above `split` rise 1 px (breathing in) and row split-1 is
// drawn twice so the body stretches instead of tearing. Rows from `split` down never move.
static void body(int x, int y, const char *const *rows, int split, bool in) {
  if (!in) { sprite(x, y, rows, 64); return; }
  sprite(x, y - 1, rows, split);
  sprite(x, y + split - 1, rows + split - 1, 1);
  sprite(x, y + split, rows + split, 64 - split);
}

// Breath: in for `hold` ms out of every `period` ms.
static bool breathing_in(uint32_t t, uint32_t period, uint32_t start, uint32_t hold) {
  uint32_t p = t % period;
  return p >= start && p < start + hold;
}

// A wisp of steam or smoke: dots rising from (bx, by) up to `h` px, gaps moving up, sway growing with height.
static void wisp(int bx, int by, uint32_t t, int h, float phase, float grow) {
  int k = (int) (t / 180);
  for (int i = 0; i < h; i++) {
    if ((((i - k) % 4) + 4) % 4 == 0) continue;                       // the moving gap
    if (i > h - 3 && ((t / 400 + (uint32_t) i) & 1)) continue;          // the top thins out
    float a = (float) (t % 2880) * (6.2832f / 2880.0f);                 // sway angle, one turn per 2.88 s
    int dx = (int) lroundf(sinf(i * 0.7f - a + phase) * (0.6f + i * grow));
    pixel(bx + dx, by - i);
  }
}

// A thick stroke: filled circles of radius r along the line, starting a fraction `from` (0..1) of the way.
static void stroke(int x0, int y0, int x1, int y1, int r, bool on, float from = 0.0f) {
  int n = pig_abs(x1 - x0) > pig_abs(y1 - y0) ? pig_abs(x1 - x0) : pig_abs(y1 - y0);
  if (n == 0) n = 1;
  for (int i = (int) (from * n); i <= n; i++) {
    int cx = x0 + (x1 - x0) * i / n, cy = y0 + (y1 - y0) * i / n;
    if (r == 0) pixel(cx, cy, on); else circle(cx, cy, r, true, on);
  }
}

// ----------------------------------------------------------------

// --- Idle: coffee. Steam rises from the mug; the pig breathes slowly. Now and then it lifts the mug to its
// snout for a sip, the arm moving with the hand and mug, then lowers it. ---
static void coffee_sip_pose(int x, int y, int dx, int dy) {   // hand and mug moved (dx, dy) from the drawing
  const int sx = x + 34, sy = y + 37;                  // shoulder
  int hx = x + 43 + dx, hy = y + 39 + dy;              // hand
  // The arm stays behind the body: no edge where it crosses the body, so only the hand shows, beside the chest.
  fill(x + 39, y + 33, 3, 2);                          // close the drawn elbow notch
  stroke(sx, sy, hx, hy, 2, true);                     // the arm, lit like the body
  pixel(x + 42, y + 39);                               // close the drawn notch where the hand used to rest
  fill(x + 43 + dx, y + 36 + dy, 6, 7, false);         // clear behind the hand and handle
  vline(x + 42 + dx, y + 36 + dy, 7, false);           // one dark line between the chest and the hand
  fill(x + 49 + dx, y + 31 + dy, 10, 13, false);       // clear behind the mug
  sprite(x + dx, y + dy, COFFEE_MUG, 64);
  fill(x + 43 + dx, y + 37 + dy, 5, 4);                // the hand, solid
  hline(x + 43 + dx, y + 41 + dy, 4);
}

static void idle_coffee(int x, int y, uint32_t t) {
  bool in = breathing_in(t, 3600, 1100, 1500);
  const uint32_t SLOT = 4800;                          // a sip in one slot in three, never two slots running:
  uint32_t n = t / SLOT, p = t % SLOT;                 // about one every 21 s, at least 9.6 s apart
  bool sip_slot = h32(n * 13 + 5) % 3 == 0 && h32((n - 1) * 13 + 5) % 3 != 0;
  bool sip = sip_slot && p >= 1200 && p < 3500;
  int b = in ? 1 : 0;
  if (sip) {
    uint32_t q = p - 1200;                             // 0-400 rising, 400-1900 at the snout, 1900-2300 lowering
    bool up = q >= 400 && q < 1900;
    body(x, y, COFFEE_NOMUG, 44, in);
    coffee_sip_pose(x, y - b, up ? -1 : 0, up ? -8 : -4);
    return;                                            // no steam while the mug moves or is at the snout
  }
  body(x, y, COFFEE, 44, in);
  int top = y + 30 - b;
  int g = sip_slot && p >= 3500 ? (int) (p - 3500) : 1000;   // after a sip the steam grows back over 0.8 s
  wisp(x + 52, top, t, g < 800 ? 1 + g / 80 : 10, 0.0f, 0.12f);
  wisp(x + 56, top, t + 700, g < 800 ? g / 100 : 8, 2.1f, 0.10f);
}

// ----------------------------------------------------------------

// --- Idle: asleep on a cushion. Breathing; z's drift up, grow, then shrink away; a snot bubble swells and shrinks with the
// breath, and now and then pops. ---
static const char *const Z_SMALL[] = {"###", "..#", ".#.", "#..", "###"};
static const char *const Z_BIG[] = {"####", "...#", "..#.", ".#..", "#...", "####"};
static const char *const Z_TINY[] = {"##.", ".#.", ".##"};   // a z near its end, before the last dot

static void idle_sleep(int x, int y, uint32_t t) {
  bool in = breathing_in(t, 4500, 1300, 2200);
  body(x, y, SLEEP, 40, in);
  // The snot bubble: grows on the breath out, shrinks on the breath in. Now and then it pops at its biggest
  // and stays gone until the next breath out.
  uint32_t q = (t + 1000) % 4500, breath = (t + 1000) / 4500;      // q 0-2299 breath out, 2300-4499 in
  bool pops = h32(breath * 7 + 5) % 4 == 0;
  int r = q < 2300 ? 1 + (int) (q * 4 / 2300) : 4 - (int) ((q - 2300) * 4 / 2200);   // 1 to 4 out, 4 to 1 in
  int cx = x + 55 + r, cy = y + 34 - r - (in ? 1 : 0);
  if (pops && q >= 2000 && q < 2300) {                  // pop: a ring of droplets
    cx = x + 59; cy = y + 30;
    pixel(cx - 4, cy); pixel(cx + 4, cy); pixel(cx, cy - 4); pixel(cx, cy + 3);
    pixel(cx - 3, cy - 3); pixel(cx + 3, cy - 3); pixel(cx + 3, cy + 3);
  } else if (!(pops && q >= 2300)) {
    if (r == 1) { pixel(cx, cy); pixel(cx + 1, cy); pixel(cx, cy - 1); }
    else { circle(cx, cy, r, false); pixel(cx - 1, cy - 1); }   // outline, with a glint inside
  }
  for (int i = 0; i < 3; i++) {
    uint32_t p = (t + i * 1500) % 4500;                 // each z lives 4.5 s, one every 1.5 s
    float f = p / 4500.0f;
    int zx = x + 52 + (int) (f * 7) + (int) lroundf(sinf(f * 6.0f + i) * 1.2f);
    int zy = y + 20 - (int) (f * 17);
    // small, big, then shrinking away at the top (never blinking out): small again, tiny, a dot
    if (f < 0.45f || (f >= 0.72f && f < 0.82f)) sprite(zx, zy, Z_SMALL, 5);
    else if (f < 0.72f) sprite(zx, zy, Z_BIG, 6);
    else if (f < 0.91f) sprite(zx, zy + 1, Z_TINY, 3);
    else pixel(zx + 1, zy + 2);
  }
}

// ----------------------------------------------------------------

// --- Idle: bath. Steam rises; the duck bobs and the water ripples; the pig breathes, eyes closed happily, smiling.
// Every 24 s it hums: three notes rise from its forehead and shrink away. Now and then the duck turns round, or a
// bubble comes up and pops (never while it hums). ---
static const char *const bath_duck[] = {   // the duck, 13 x 12, top-left at (42, 24)
  "...####......",
  "..######.....",
  "..##.###.....",
  "##.#####.....",
  ".##.####.....",
  "....###.....#",
  "...####....##",
  ".############",
  ".####.####.##",
  ".####.####.##",
  "..####....##.",
  "...########.."};

static const char *const bath_note1[] = {".##.", ".#.#", ".#..", "##..", "##.."};          // eighth note
static const char *const bath_note2[] = {".#####", ".#...#", ".#...#", "##..##", "##..##"};  // beamed pair
static const char *const bath_note1_s[] = {".##", ".#.", "##."};              // the same, smaller, as they go
static const char *const bath_note2_s[] = {".####", ".#..#", "##.##"};

static void bath_face(int x, int y) {                // happy face over the drawn one; y already holds the breath
  fill(x + 27, y + 16, 2, 3);                          // fill the drawn eye
  pixel(x + 25, y + 18, false); pixel(x + 26, y + 17, false); pixel(x + 27, y + 16, false);   // ^ closed eye,
  pixel(x + 28, y + 17, false); pixel(x + 29, y + 18, false);                                 // 5 wide
  pixel(x + 29, y + 23, false); pixel(x + 30, y + 23, false);   // smile: the mouth runs back
  pixel(x + 28, y + 22, false); pixel(x + 27, y + 21, false);   // and curves up at its corner
}

static void idle_bath(int x, int y, uint32_t t) {
  bool in = breathing_in(t, 4800, 1500, 1800);
  body(x, y, BATH, 24, in);                            // only the head rises: the duck starts at row 24
  bath_face(x, y - (in ? 1 : 0));
  if (in) { pixel(x + 29, y + 23, false); pixel(x + 30, y + 23, false); }            // row 23 is drawn twice on the in-breath: smile it too
  wisp(x + 44, y + 19, t, 13, 0.0f, 0.12f);            // steam, three wisps out of step
  wisp(x + 49, y + 20, t + 900, 15, 2.0f, 0.10f);
  wisp(x + 54, y + 19, t + 1900, 12, 4.1f, 0.12f);
  // the duck: sinks 1 px for half of every 2.4 s; turned round in a rare slot
  uint32_t slot = t / 7200, p = t % 7200;
  bool turned = h32(slot * 13 + 5) % 3 == 0 && p >= 1200 && p < 5400;
  bool low = t % 2400 >= 1200;
  if (low) sprite(x + 42, y + 25, bath_duck, 11, turned);
  else sprite(x + 42, y + 24, bath_duck, 12, turned);
  // ripples: the water line by the pig with a gap running along it; the dash right of the duck rocks
  int k = (int) (t / 450 % 4);
  for (int i = 0; i < 6; i++) if ((i + k) % 4 != 0) pixel(x + 42 - i, y + 31);
  int d = t % 1600 < 800 ? 0 : 1;
  hline(x + 55 + d, y + 34, 2);
  // the hum: every 24 s, from 1 s to 7.2 s in, three notes rise up and left from the forehead, in the dark corner
  // away from the steam, and shrink away; no bubble in the first 9.6 s of the 24 s, so the two never meet
  uint32_t hq = t % 24000;
  for (int i = 0; i < 3; i++) {
    uint32_t s0 = 1000 + i * 1600;
    if (hq < s0 || hq >= s0 + 3000) continue;
    uint32_t q = hq - s0;
    int nx = x + 6 - (int) (q * 6 / 3000), ny = y + 8 - (int) (q * 8 / 3000);
    // near the top each note shrinks away (never blinks out): smaller, a two-pixel speck, a dot
    if (q < 2100) { if (i & 1) sprite(nx, ny, bath_note2, 5); else sprite(nx, ny, bath_note1, 5); }
    else if (q < 2450) { if (i & 1) sprite(nx, ny + 1, bath_note2_s, 3); else sprite(nx, ny + 1, bath_note1_s, 3); }
    else if (q < 2750) { pixel(nx + 1, ny + 2); pixel(nx + 2, ny + 1); }
    else pixel(nx + 1, ny + 2);
  }
  // a bubble now and then: rises out of the water by the duck's beak, swells, pops
  uint32_t bp = t % 4800;
  if (hq >= 9600 && h32(t / 4800 * 7 + 1) % 4 == 0 && bp >= 2400 && bp < 3600) {
    uint32_t q = bp - 2400;
    if (q < 400) pixel(x + 39, y + 30);
    else if (q < 800) circle(x + 39, y + 28, 1);
    else if (q < 1000) { pixel(x + 37, y + 27); pixel(x + 41, y + 27); pixel(x + 39, y + 25); pixel(x + 38, y + 29); pixel(x + 40, y + 29); }
  }
}

// ----------------------------------------------------------------

// --- Idle: meditating. Very slow deep breaths; sparkles twinkle around the pig; now and then it levitates. ---
static const int MEDITATE_SPOTS[8][2] = {{54, 20}, {12, 29}, {9, 15}, {57, 8}, {59, 33}, {6, 40}, {50, 3}, {16, 5}};

static void meditate_star(int cx, int cy, int s) {     // s: 0 dot, 1 small plus, 2 hollow plus as drawn, 3 with tips
  if (s == 0) { pixel(cx, cy); return; }
  if (s == 1) { pixel(cx, cy); pixel(cx - 1, cy); pixel(cx + 1, cy); pixel(cx, cy - 1); pixel(cx, cy + 1); return; }
  for (int d = 1; d <= 2; d++) { pixel(cx - d, cy); pixel(cx + d, cy); pixel(cx, cy - d); pixel(cx, cy + d); }
  if (s == 3) { pixel(cx - 3, cy); pixel(cx + 3, cy); pixel(cx, cy - 3); pixel(cx, cy + 3); pixel(cx, cy); }
}

static void idle_meditate(int x, int y, uint32_t t) {
  // Levitation: in about one 16.8 s slot in three, the pig rises 1 then 2 px for a few seconds and settles.
  uint32_t slot = t / 16800, q = t % 16800;
  int lift = 0;
  if (h32(slot * 5 + 11) % 3 == 0 && q >= 4000 && q < 9600) lift = (q < 4600 || q >= 9000) ? 1 : 2;
  bool in = breathing_in(t, 5600, 1200, 2800);         // slow, deep: in for 2.8 s of every 5.6 s
  body(x, y - lift, MEDITATE, 44, in);                 // rows above 44 rise; the crossed legs stay put
  if (lift == 2) for (int i = 22; i <= 46; i += 2) pixel(x + i, y + 59);   // a faint shadow shows the gap
  // Sparkles: three, each on its own period, popping up at a hashed spot, growing, holding, shrinking.
  static const uint32_t P[3] = {2400, 3360, 4200};
  for (int i = 0; i < 3; i++) {
    uint32_t c = t / P[i], f = (t % P[i]) * 20 / P[i]; // f: 0..19
    int s;
    if (f < 2) s = 0; else if (f < 4) s = 1; else if (f < 8) s = 2; else if (f < 10) s = lift ? 3 : 2;
    else if (f < 12) s = 1; else if (f < 13) s = 0; else continue;   // dark for the last third
    if (f >= 4 && f < 10 && lift) s = 3;               // brighter while it floats
    const int *sp = MEDITATE_SPOTS[i + 3 * (h32(c * 3 + i * 101) % (i == 2 ? 2 : 3))];   // own spots per star
    meditate_star(x + sp[0], y + sp[1], s);
  }
}

// ----------------------------------------------------------------

// --- Idle: reading in the armchair. The pig breathes (head, arms and book; the chair and ottoman stay still);
// a page turns every 7.5 s; rare blinks. Now and then the pig nods off: the head dips, the eyes close, a z
// floats up, then it wakes with a start. ---
static const char *const read_z[] = {"###", "..#", ".#.", "###"};
static const char *const read_z_tiny[] = {"##.", ".#.", ".##"};   // the z shrinking away
// The page turn's first and last steps, as (x, y) points drawn lit: the corner curling up from the spine, and the
// page landing on the left page.
static const int8_t READ_PAGE_LIFT[10][2] = {{40, 22}, {40, 21}, {41, 20}, {41, 21}, {42, 18}, {42, 19}, {42, 20},
                                             {43, 17}, {43, 18}, {44, 17}};
static const int8_t READ_PAGE_LAND[9][2] = {{33, 20}, {33, 21}, {34, 21}, {35, 21}, {36, 21}, {37, 21}, {38, 21},
                                            {39, 22}, {40, 22}};

// A line of the turning page: lit on the dark background, dark where it crosses the solid left page.
static void read_page_line(int x, int y, int x0, int y0, int x1, int y1) {
  int n = pig_abs(x1 - x0) > pig_abs(y1 - y0) ? pig_abs(x1 - x0) : pig_abs(y1 - y0);
  if (n == 0) n = 1;
  for (int i = 0; i <= n; i++) {
    int px = x0 + (x1 - x0) * i / n, py = y0 + (y1 - y0) * i / n;
    bool onpage = px >= 31 && px <= 38 && py >= 24 && py <= 33;
    pixel(x + px, y + py, !onpage);
  }
}

static void idle_read(int x, int y, uint32_t t) {
  uint32_t slot = t / 15000, p = t % 15000;
  bool nod = h32(slot * 11 + 2) % 3 == 0;              // about one slot in three
  int dip = 0;                                         // head drop while dozing
  if (nod && p >= 4000 && p < 10000) dip = p < 4500 ? 1 : 2;
  bool in = breathing_in(t, 4500, 1200, 1700);
  sprite(x, y, READ, 64);                              // chair, ottoman, legs: never move
  body(x, y, READ_TORSO, 36, in);                      // arms and book breathe
  int hy = y - (in ? 1 : 0) + dip;
  sprite(x, hy, READ_HEAD, 64);
  bool shut = dip > 0 || t % 9000 < 150;              // eyes shut while dozing, or a rare blink
  if (nod && p >= 10000 && p < 10150) shut = true;    // and a blink as it wakes
  if (shut) fill(x + 23, hy + 17, 2, 1);
  int by = in ? -1 : 0;                                // the book rises with the breath
  if (dip == 2) {                                      // a z drifts up from the nodding head
    uint32_t q = (p - 4500) % 2750;
    int zx = x + 37 + (int) (q / 700), zy = y + 10 - (int) (q / 350);
    if (q < 1900) sprite(zx, zy, read_z, 4);            // then it shrinks away: tiny, a dot, gone
    else if (q < 2200) sprite(zx, zy + 1, read_z_tiny, 3);
    else if (q < 2500) pixel(zx + 1, zy + 2);
  }
  // the page turn, 250 ms a step: its corner curls up off the right page, the page stands upright, then it
  // lands on the left page, its edge just showing over the book's top (steps 1 and 3 as drawn)
  uint32_t pp = t % 7500;
  if (dip == 0 && pp >= 3000 && pp < 3750) {
    int f = (int) ((pp - 3000) / 250);
    if (f == 1) {
      read_page_line(x, y + by, 40, 22, 41, 13);
      read_page_line(x, y + by, 41, 13, 41, 24);
      read_page_line(x, y + by, 40, 33, 41, 24);
    } else {
      const int8_t (*pts)[2] = f == 0 ? READ_PAGE_LIFT : READ_PAGE_LAND;
      int n = f == 0 ? 10 : 9;
      for (int i = 0; i < n; i++) pixel(x + pts[i][0], y + by + pts[i][1]);
    }
  }
}

// ----------------------------------------------------------------

// --- Idle: music on the deck chair. Notes rise from the headphones and shrink away; the raised foot taps the beat and
// the head nods every other beat; the chair never moves. Now and then a burst of notes. ---
static const char *const MUSIC_NOTE1[] = {".##.", ".#.#", ".#..", "##..", "##.."};   // eighth note
static const char *const MUSIC_NOTE2[] = {".#####", ".#...#", ".#...#", "##..##", "##..##"};  // beamed pair
static const char *const MUSIC_NOTE1_S[] = {".##", ".#.", "##."};               // the same, smaller, as they go
static const char *const MUSIC_NOTE2_S[] = {".####", ".#..#", "##.##"};

static void idle_music(int x, int y, uint32_t t) {
  const uint32_t BEAT = 560;
  uint32_t b = t / BEAT, p = t % BEAT;
  int nod = (b & 1) == 0 && p < 260 ? 1 : 0;           // head dips 1 px on every other beat
  sprite(x, y, MUSIC_REST, 64);
  sprite(x, y + nod, MUSIC_HEAD, 64);
  sprite(x, y - (p < 180 ? 1 : 0), MUSIC_FOOT, 64);    // the foot taps up on each beat
  // Notes: three on the way at once, each lives 4.48 s, rising from the headphones, drifting right, swaying.
  for (int i = 0; i < 3; i++) {
    uint32_t q = (t + i * 1500) % 4480, k = (t + i * 1500) / 4480;
    float f = q / 4480.0f;
    int nx = x + 34 + (int) (h32(k * 7 + i) % 4) + (int) (f * 18) + (int) lroundf(sinf(f * 7.0f + i) * 1.5f);
    int ny = y + 21 - (int) (f * 21);
    // near the top each note shrinks away (never blinks out): smaller, a two-pixel speck, a dot
    if (f < 0.76f) { if (i & 1) sprite(nx, ny, MUSIC_NOTE2, 5); else sprite(nx, ny, MUSIC_NOTE1, 5); }
    else if (f < 0.86f) { if (i & 1) sprite(nx, ny + 1, MUSIC_NOTE2_S, 3); else sprite(nx, ny + 1, MUSIC_NOTE1_S, 3); }
    else if (f < 0.94f) { pixel(nx + 1, ny + 2); pixel(nx + 2, ny + 1); }
    else pixel(nx + 1, ny + 2);
  }
  // Rare: in about one 8.96 s slot in four, a burst of small notes fans out over 2 s.
  uint32_t slot = t / 8960, s = t % 8960;
  if (h32(slot * 9 + 5) % 4 == 0 && s < 2240) {
    float f = s / 2240.0f;
    for (int i = 0; i < 5; i++) {
      float a = -0.4f + i * 0.45f;                     // fan from up-left to right
      int nx = x + 30 + (int) lroundf(sinf(a) * f * 26), ny = y + 16 - (int) lroundf(cosf(a) * f * 14);
      if (f < 0.7f) { pixel(nx + 1, ny - 2); pixel(nx + 1, ny - 1); pixel(nx, ny); pixel(nx + 1, ny); }   // tiny note
      else if (f < 0.85f) { pixel(nx, ny); pixel(nx + 1, ny - 1); }                                        // shrinking
      else pixel(nx, ny);
    }
  }
}

// ----------------------------------------------------------------

// --- Idle: the plain pig stands happy, smiling, breathing slowly; the tail wags now and then; a small
// butterfly flutters about above the pig and in front of the snout. About every 40 s it lands on the tip of the
// snout, opens and closes its wings slowly, and the pig goes cross-eyed looking at it; then it flies off. ---
static const char *const butterfly_TAIL_UP[] = {
  "....###.", "...#..##", "...#..##", "#..#.##.", ".#.##...", "..#.###.", ".....##."};   // as drawn, rows 19-25
static const char *const butterfly_TAIL_DN[] = {
  ".....##.", "..#.###.", ".#.##...", "#..#.##.", "...#..##", "...#..##", "....###."};   // swung down, rows 25-31
static const char *const butterfly_WINGS[3][4] = {
  {"##.##", "#####", ".###.", "##.##"},              // open
  {".#.#.", "#####", ".###.", ".#.#."},              // half
  {".#.#.", ".#.#.", "..#..", "..#.."}};             // closed, wings up

// The butterfly, centre (cx, cy), wing pose w, with a dark edge so it reads over the filled pig; when it sits,
// no edge below its bottom row, so the snout under its feet stays whole.
static void butterfly_fly(int cx, int cy, int w, bool sits) {
  const char *const *s = butterfly_WINGS[w];
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 5; c++)
      if (s[r][c] == '#') {
        int px = cx - 2 + c, py = cy - 2 + r;
        pixel(px - 1, py, false); pixel(px + 1, py, false); pixel(px, py - 1, false);
        if (!(sits && r == 3)) pixel(px, py + 1, false);
      }
  sprite(cx - 2, cy - 2, s, 4);
}

static float butterfly_wave(uint32_t t, uint32_t period, float phase) {
  return sinf((float) (t % period) * (6.2832f / (float) period) + phase);
}

static float butterfly_ease(float f) { return f <= 0 ? 0 : f >= 1 ? 1 : f * f * (3 - 2 * f); }

static void idle_butterfly(int x, int y, uint32_t t) {
  bool in = breathing_in(t, 5000, 1500, 2200);
  int dy = in ? -1 : 0;
  body(x, y, BUTTERFLY, 44, in);                       // legs (rows 44 down) stay planted
  // Tail: hangs curled as drawn; in most 4 s slots (picked by hash) it wags down-up-down-up, 250 ms each.
  uint32_t tw = t % 4000;
  if (h32(t / 4000 * 5 + 1) % 3 != 0 && tw < 1000 && (tw / 250) % 2 == 0) sprite(x + 2, y + 25 + dy, butterfly_TAIL_DN, 7);
  else sprite(x + 2, y + 19 + dy, butterfly_TAIL_UP, 7);
  // Smile: the mouth corner turns up into the cheek.
  pixel(x + 48, y + 37 + dy, false); pixel(x + 47, y + 36 + dy, false);

  // Landing: once per 40 s slot, at a hashed time. Approach 2.5 s, sit 3 s, leave 2 s.
  uint32_t slot = t / 40000, off = 6000 + h32(slot * 13 + 7) % 14000, p = t % 40000;
  int ph = p >= off ? (int) (p - off) : -1;           // ms into the landing, or -1 before it
  float w = 0;                                          // 0 wandering, 1 on the snout
  if (ph >= 0 && ph < 2500) w = butterfly_ease(ph / 2500.0f);
  else if (ph >= 2500 && ph < 5500) w = 1;
  else if (ph >= 5500 && ph < 7500) w = 1 - butterfly_ease((ph - 5500) / 2000.0f);
  bool sitting = ph >= 2500 && ph < 5500;
  bool cross = ph >= 2000 && ph < 6000;

  // Eye: as drawn; a happy squint 3 s in most 10 s slots (picked by hash); cross-eyed while the butterfly
  // comes in and sits: the near eye and the far one peeking past it, both pupils turned in and down.
  if (cross) {
    int ey = y + dy;
    fill(x + 45, ey + 28, 3, 4);                       // erase the drawn eye
    rect(x + 44, ey + 27, 4, 5, false);                // near eye: ring, white, pupil front-low
    pixel(x + 46, ey + 29, false); pixel(x + 46, ey + 30, false);
    hline(x + 49, ey + 27, 4, false);                  // far eye: top and back edge, its front hidden by the snout
    vline(x + 49, ey + 28, 3, false);
    hline(x + 49, ey + 31, 3, false);
    pixel(x + 50, ey + 29, false); pixel(x + 50, ey + 30, false);   // its pupil, back-low
  } else if (h32(t / 10000 * 3 + 2) % 4 != 0 && t % 10000 >= 5000 && t % 10000 < 8000) {
    fill(x + 44, y + 27 + dy, 5, 5);
    pixel(x + 44, y + 31 + dy, false); pixel(x + 45, y + 30 + dy, false); pixel(x + 46, y + 29 + dy, false);
    pixel(x + 47, y + 30 + dy, false); pixel(x + 48, y + 31 + dy, false);
  }

  // Wandering path: slow sweeps over the pig, dipping down in front of the snout when it is at the right.
  float fx = 31 + 21 * butterfly_wave(t, 40000, 0) + 7 * butterfly_wave(t, 8000, 1.0f);
  float fy = 6 + 2.5f * butterfly_wave(t, 5000, 0) + 1.5f * butterfly_wave(t, 10000, 2.0f);
  float front = fx > 50 ? (fx - 50) / 9.0f : 0;
  fy += (front > 1 ? 1 : front) * 15 * (0.5f + 0.5f * butterfly_wave(t, 20000, 0.5f));
  int wing = (t / 250) % 4 == 0 ? 0 : (t / 250) % 4 == 2 ? 2 : 1;   // open, half, closed, half, 250 ms each
  if (wing == 0) fy -= 1;                              // a lift on each downstroke
  fx += (58 - fx) * w; fy += (28 + dy - fy) * w;
  int cx = (int) lroundf(fx), cy = (int) lroundf(fy);
  if (sitting) { cx = 58; cy = 28 + dy; wing = (ph / 700) % 2 ? 0 : 2; }   // feet on the snout's top, rising with the breath; wings open and close slowly
  cx = cx < 3 ? 3 : cx > 60 ? 60 : cx;                // keep it, and its dark edge, inside the box
  cy = cy < 3 ? 3 : cy > 58 ? 58 : cy;
  butterfly_fly(x + cx, y + cy, wing, sitting);
}

// ----------------------------------------------------------------

// --- Working: hammer on the anvil. The arm swings from the shoulder: raised behind, over the head, strike,
// sparks; now and then a pause with the hammer up to look at the work, and now and then a hard hit with a big
// burst of sparks. ---
static void hammer_arm(int x, int y, int hx, int hy, int cx, int cy) {   // hand (hx, hy), head centre (cx, cy)
  const int sx = x + 17, sy = y + 30;                  // shoulder
  hx += x; hy += y; cx += x; cy += y;
  stroke(sx, sy, hx, hy, 3, false, 0.4f);              // dark edge, so the arm reads against the body
  stroke(hx, hy, cx, cy, 1, false);
  fill(cx - 4, cy - 3, 8, 5, false);                   // no dark row under the head, so it lands on the horn
  stroke(sx, sy, hx, hy, 2, true);                     // the arm, 5 px wide like the drawing's
  stroke(hx, hy, cx, cy, 0, true);                     // the handle
  fill(cx - 3, cy - 2, 6, 4);                          // the head
}

static void work_anvil(int x, int y, uint32_t t) {
  sprite(x, y, ANVIL, 64);
  const uint32_t CYCLE = 1100;
  uint32_t c = t / CYCLE, p = t % CYCLE;
  bool pause = h32(c * 7 + 3) % 6 == 0;                // about one cycle in six: hold the hammer up
  if (pause || p < 400) {
    hammer_arm(x, y, 17, 21, 10, 12);                  // raised behind the head, as drawn
  } else if (p < 600 || p >= 900) {
    hammer_arm(x, y, 22, 22, 26, 11);                  // over the head
  } else {
    hammer_arm(x, y, 27, 31, 38, 35);                  // down on the anvil's horn
  }
  // Sparks on every strike: a flash at the horn, then sparks flying up and out for 450 ms, while the arm
  // already lifts. Now and then a hard hit throws twice as many, higher.
  if (pause || p < 600 || p >= 1050) return;
  uint32_t dt = p - 600;
  if (dt < 250) {                                      // the flash where the hammer meets the horn
    pixel(x + 42, y + 31); pixel(x + 44, y + 33); pixel(x + 45, y + 35); pixel(x + 34, y + 31);
  }
  bool big = h32(c * 5 + 1) % 6 == 0;
  for (int i = 0; i < (big ? 22 : 14); i++) {
    uint32_t r = h32(c * 31 + i);
    float vx = ((int) (r & 31) - 12) / (big ? 550.0f : 700.0f);   // px per ms, mostly away from the pig
    float vy = ((big ? 30 : 20) + ((r >> 6) & 15)) / 400.0f;      // rise 4-8 px; 9-15 px on a hard hit
    float px = 41 + vx * dt, py = 35 - vy * dt + 0.00015f * dt * dt;
    if (py < 37 && dt < 300 + (r >> 12) % 150) pixel(x + (int) px, y + (int) py);   // each fades at its own time
  }
}

// ----------------------------------------------------------------

// --- Working: soldering. The iron hand works along the board: the tip goes down on a joint, the joint glints and
// smoke rises for about 3 s, then thins out; the iron lifts and moves to the next joint, and pauses there with no
// smoke. Two joints, back and forth: the arm never stretches out to a third. Head and
// shoulders breathe; the table stays still. Now and then,
// while the tip is on a joint, a big puff of flux smoke. ---
static const uint32_t SOLDER_JOINT = 4800, SOLDER_DOWN = 3200, SOLDER_MOVE = 600;   // per joint: down, move, pause
static const int8_t SOLDER_TX[2] = {48, 52}, SOLDER_TY[2] = {37, 38};               // the joints on the board

static void solder_tip(uint32_t t, int &tx, int &ty) {   // the tip's place at time t
  uint32_t n = t / SOLDER_JOINT, p = t % SOLDER_JOINT;
  int j = (int) (n % 2), k = (int) ((n + 1) % 2);
  if (p < SOLDER_DOWN) { tx = SOLDER_TX[j]; ty = SOLDER_TY[j]; return; }
  if (p < SOLDER_DOWN + SOLDER_MOVE) {                   // lifted, travelling to the next joint
    int q = (int) (p - SOLDER_DOWN);
    tx = SOLDER_TX[j] + (SOLDER_TX[k] - SOLDER_TX[j]) * q / (int) SOLDER_MOVE;
    ty = SOLDER_TY[j] + (SOLDER_TY[k] - SOLDER_TY[j]) * q / (int) SOLDER_MOVE - 3;
    return;
  }
  tx = SOLDER_TX[k]; ty = SOLDER_TY[k] - 2;              // the pause: held just above the next joint
}

static void work_solder(int x, int y, uint32_t t) {
  bool in = breathing_in(t, 3600, 900, 1400);
  body(x, y, SOLDER, 30, in);                          // only rows above 30 rise: the table starts at row 34
  fill(x + 30, y + 30, 6, 8);                          // the chest behind the arm
  uint32_t n = t / SOLDER_JOINT, p = t % SOLDER_JOINT;
  int tx, ty;
  solder_tip(t, tx, ty);
  // The arm from the shoulder to the hand, the iron from the hand down to the tip.
  const int sx = x + 29, sy = y + 33;
  int hx = x + tx - 9, hy = y + ty - 3;
  stroke(sx, sy, hx, hy, 2, false, 0.15f);             // dark edge so the arm reads against the body
  stroke(sx, sy, hx, hy, 1, true);                     // the arm
  circle(hx, hy, 2, true);                             // the hand round the handle
  stroke(hx + 2, hy + 1, x + tx, y + ty, 1, false);    // dark edge round the iron
  stroke(hx, hy, x + tx - 3, y + ty - 1, 0, true);     // the handle...
  stroke(hx + 1, hy - 1, x + tx - 4, y + ty - 2, 0, true);
  stroke(x + tx - 3, y + ty - 1, x + tx, y + ty, 0, true);   // ...and the thin tip
  if (p >= SOLDER_DOWN) return;
  // On the joint: it glints, smoke climbs, holds, then thins out before the iron lifts.
  if (p < 700 || h32(t / 240) % 4 == 0) {
    pixel(x + tx + 2, y + ty); pixel(x + tx + 1, y + ty + 1); pixel(x + tx + 3, y + ty + 1); pixel(x + tx + 2, y + ty + 2);
  }
  int h = p < 600 ? 3 + (int) (p / 60) : p < 2200 ? 13 : p < 3000 ? 13 - (int) ((p - 2200) / 62) : 0;
  if (h > 0) {
    wisp(x + tx + 1, y + ty - 2, t, h, 0.0f, 0.16f);
    if (h > 5) wisp(x + tx + 3, y + ty - 2, t + 1100, h - 5, 1.7f, 0.14f);
  }
  if (h32(n * 11 + 3) % 4 == 0 && p >= 300 && p < 2700) {   // now and then a big puff: flux boils off the joint
    uint32_t ps = p - 300;
    int hh = ps < 1200 ? 8 + (int) (ps / 100) : ps < 1800 ? 20 : 20 - (int) ((ps - 1800) / 30);   // climbs, holds, fades
    wisp(x + tx, y + ty - 2, t + 400, hh, 3.1f, 0.22f);
    wisp(x + (tx > 52 ? tx - 5 : tx + 5), y + ty - 2, t + 1700, hh - 4, 0.9f, 0.20f);   // left of the last joint: room to sway
    if (ps >= 1800 && ps < 2100) pixel(x + tx + 3, y + ty - 24);   // the last wisp breaks off
  }
}

// ----------------------------------------------------------------

// --- Working: welding. The torch hand travels slowly along the bar, the arc flickers, sparks fly and fall, a
// bead grows behind the torch; then the torch goes quickly back while the bead cools. Now and then the arc pops in the
// middle of a pass and throws a big shower of sparks. ---
static const uint32_t WELD_CYCLE = 10700, WELD_PASS = 8000, WELD_HOLD = 1500;   // pass, hold, 1.2 s return
static const int WELD_X0 = 40, WELD_X1 = 54;          // the tip's travel along the bar

// The drawn bar's top edge, row by column for x 38-57, read from the picture (it has a flat step at x 47-49).
static const int8_t WELD_TOPS[20] = {56, 55, 55, 54, 54, 53, 53, 52, 52, 51, 51, 51, 50, 50, 49, 49, 48, 48, 47, 47};
static int weld_top(int tx) { return WELD_TOPS[tx < 38 ? 0 : tx > 57 ? 19 : tx - 38]; }

static int weld_tip_x(uint32_t p) {                    // p: time in the cycle
  if (p < WELD_PASS) return WELD_X0 + (int) (p * (WELD_X1 - WELD_X0) / WELD_PASS);
  if (p < WELD_PASS + WELD_HOLD) return WELD_X1;
  return WELD_X1 - (int) ((p - WELD_PASS - WELD_HOLD) * (WELD_X1 - WELD_X0) / (WELD_CYCLE - WELD_PASS - WELD_HOLD));
}

static bool weld_arc(uint32_t t) {                     // the arc burns during the pass
  return t % WELD_CYCLE < WELD_PASS;
}

static void work_weld(int x, int y, uint32_t t) {
  uint32_t c = t / WELD_CYCLE, p = t % WELD_CYCLE;
  sprite(x, y, WELD, 64);
  // The bead: dots in the bar's middle from the start to the tip; while the torch goes back it cools, dots
  // dropping out one by one.
  int tx = weld_tip_x(p), ty = weld_top(tx) - 1;
  int end = p < WELD_PASS ? tx : WELD_X1;
  int gone = p < WELD_PASS + WELD_HOLD ? 0 : (int) ((p - WELD_PASS - WELD_HOLD) * 9 / (WELD_CYCLE - WELD_PASS - WELD_HOLD));
  for (int bx = WELD_X0 + 2, n = 0; bx <= end; bx += 2, n++) {   // even columns: each has a hollow (odd 49, 51 do not)
    if (gone && (int) (h32(c * 31 + n) % 8) < gone) continue;
    pixel(x + bx, y + weld_top(bx) + 1);
  }
  // The arm: from the shoulder to the hand, the torch from the hand down to the tip.
  const int sx = x + 30, sy = y + 39;
  int hx = x + tx - 7, hy = y + ty - 6;
  stroke(sx, sy, hx, hy, 3, false, 0.35f);             // dark edge so the arm reads against the body
  stroke(sx, sy, hx, hy, 2, true);                     // the arm
  stroke(hx + 2, hy + 2, x + tx, y + ty, 1, false);    // dark edge round the torch
  for (int bx = tx - 2; bx <= tx + 2; bx++) pixel(x + bx, y + weld_top(bx));   // the torch's edge spares the bar
  stroke(hx, hy, x + tx, y + ty, 0, true);             // the torch
  pixel(hx + 1, hy); pixel(hx, hy + 1);                 // its grip, a little thicker
  if (!weld_arc(t)) return;
  // The arc: a cross at the tip that flickers in size every 80 ms.
  int ax = x + tx + 1, ay = y + ty;
  uint32_t r = h32(t / 80 + 977);
  int s = r % 4;
  pixel(ax, ay);
  if (s >= 1) { pixel(ax - 1, ay); pixel(ax + 1, ay); pixel(ax, ay - 1); }
  if (s >= 2) { pixel(ax - 2, ay); pixel(ax + 2, ay); pixel(ax, ay - 2); }
  if (s == 3) { pixel(ax - 3, ay); pixel(ax + 3, ay); pixel(ax, ay - 3); pixel(ax - 2, ay - 2); pixel(ax + 2, ay - 2); }
  // Sparks: a small group every 120 ms, each spark flying up and out and falling for 480 ms. In about one
  // pass in three the arc pops for 600 ms mid-pass: bigger groups that fly twice as high.
  for (int g = 0; g < 4; g++) {
    uint32_t k = t / 120 - g, t0 = k * 120, dt = t - t0;
    if (!weld_arc(t0)) continue;
    uint32_t pc = t0 % WELD_CYCLE;
    bool pop = h32((t0 / WELD_CYCLE) * 13 + 7) % 3 == 0 && pc >= 3600 && pc < 4200;
    int ox = weld_tip_x(pc) + 1, oy = weld_top(ox - 1) - 1;
    for (int i = 0; i < (pop ? 10 : 5); i++) {
      uint32_t h = h32(k * 7 + i);
      if (!pop && h % 5 == 0) continue;
      float vx = ((int) (h & 15) - (pop ? 4 : 2)) / 900.0f;   // forward and up, away from the arm and mask
      float vy = ((pop ? 20 : 10) + ((h >> 5) & 15)) / 300.0f; // rise 2-14 px; 9-28 px when it pops
      float px = ox + vx * dt, py = oy - vy * dt + 0.00012f * dt * dt;
      if (px < 47.0f && py < 31.0f) continue;          // never on or beside the mask's right edge
      if (py <= oy + 4) pixel(x + (int) px, y + (int) py);
    }
  }
}

// ----------------------------------------------------------------

// --- Working: tinkering. Tap-tap with a small mallet, then a pause; the arm swings from the shoulder and the
// small part on the table hops at each tap; now and then the pig stops and leans in to look. ---
static void tinker_arm(int x, int y, int hx, int hy, int cx, int cy) {    // hand (hx, hy), mallet head (cx, cy)
  const int sx = x + 24, sy = y + 32;                  // shoulder
  hx += x; hy += y; cx += x; cy += y;
  stroke(sx, sy, hx, hy, 3, false, 0.3f);              // dark edge, so the arm reads against the body
  stroke(hx, hy, cx, cy, 1, false);
  fill(cx - 3, cy - 2, 7, 4, false);
  stroke(sx, sy, hx, hy, 2, true);                     // the arm
  stroke(hx, hy, cx, cy, 0, true);                     // the handle
  fill(cx - 2, cy - 2, 5, 4);                          // the mallet head
}

static void work_tinker(int x, int y, uint32_t t) {
  const uint32_t CYCLE = 1400;
  uint32_t c = t / CYCLE, p = t % CYCLE;
  bool look = h32(c * 97 + 11) % 20 == 0;              // about one cycle in twenty: stop and lean in to look
  if (look && p >= 200 && p < 1200) {
    sprite(x + 1, y + 1, TINKER, 27);                  // head and ears lean 1 px forward and down
    sprite(x, y + 28, TINKER + 28, 36);
    sprite(x, y + 27, TINKER + 27, 1);
  } else {
    sprite(x, y, TINKER, 64);
  }
  bool hop = !look && ((p >= 200 && p < 350) || (p >= 550 && p < 700));   // each pose 150-200 ms: seen at 10 fps
  fill(x + 40, y + 31 - hop, 5, 3, false);             // the small part: a nut with a pin, clear of the table lines
  fill(x + 41, y + 33 - hop, 3, 1);
  pixel(x + 42, y + 32 - hop);
  if (look) {
    tinker_arm(x, y, 41, 29, 46, 23);                  // held up while the head leans in
  } else if (p >= 750) {
    tinker_arm(x, y, 40, 29, 45, 23);                  // raised, as drawn
  } else if (p < 200 || (p >= 350 && p < 550)) {
    tinker_arm(x, y, 37, 32, 42, 30);                  // tap: the head lands on the part
  } else {
    tinker_arm(x, y, 39, 30, 44, 26);                  // lifted between taps
  }
}

// ----------------------------------------------------------------

// --- Working: 3D printer. The print head runs side to side on its gantry and lays a small model (a little house) down
// layer by layer (8 layers, 28.8 s), stepping up a layer each time; then it parks and the finished part sparkles. The pig
// watches the nozzle with a smile (its eye follows), breathes and blinks. Every finish makes it glad: it hops twice,
// its eyes close happily, its tail wiggles, sparkles twinkle round the part and the pig and a heart rises over its
// head; one finish in three it
// hops a third time. ---
static const int8_t printer_layer[8][2] = {          // x range of each layer, bottom (y 42) up
  {39, 46}, {39, 46}, {39, 46}, {39, 46}, {40, 45}, {41, 44}, {42, 43}, {42, 43}};   // a little house

static void printer_head(int x, int y, int hx, int tip) {   // gantry rail, carriage, nozzle tip at (hx, tip)
  hline(x + 35, y + tip - 5, 15);                      // the rail, wall to rod
  rect(x + hx - 2, y + tip - 4, 5, 3);                 // carriage, hollow so it reads as a part of the machine
  vline(x + hx, y + tip - 1, 2);                       // nozzle
}

static void printer_spark(int x, int y, bool plus) {  // a twinkle, 3 x 3: a plus, or an x
  pixel(x, y);
  if (plus) { pixel(x - 1, y); pixel(x + 1, y); pixel(x, y - 1); pixel(x, y + 1); }
  else { pixel(x - 1, y - 1); pixel(x + 1, y - 1); pixel(x - 1, y + 1); pixel(x + 1, y + 1); }
}

static const char *const printer_heart[] = {".#.#.", "#####", "#####", ".###.", "..#.."};

static void work_printer(int x, int y, uint32_t t) {
  const uint32_t LAYER = 3600, NL = 8, CYCLE = LAYER * NL + 3600;
  uint32_t c = t / CYCLE, p = t % CYCLE;
  bool done = p >= LAYER * NL;
  // the pig: hops twice at every finish (three times in one finish of three), breathes, eye follows the nozzle
  int hop = 0;
  uint32_t dq = done ? p - LAYER * NL : 0;
  if (done) {
    if ((dq >= 300 && dq < 550) || (dq >= 800 && dq < 1050)) hop = 2;
    if (h32(c * 5 + 3) % 3 == 0 && dq >= 1300 && dq < 1550) hop = 2;
  }
  bool in = breathing_in(t, 3600, 900, 1300);
  sprite(x, y, PRINTER, 64);                           // the printer frame never moves
  body(x, y - hop, PRINTER_PIG, 44, in);
  int ey = y + 35 - hop - (in ? 1 : 0);
  // bed
  hline(x + 37, y + 43, 12);
  hline(x + 38, y + 44, 10);
  int hx, tip, look;
  if (!done) {
    uint32_t L = p / LAYER, q = p % LAYER;
    int xa = printer_layer[L][0], xb = printer_layer[L][1];
    if (L & 1) { int s = xa; xa = xb; xb = s; }        // odd layers go right to left
    int ly = 42 - (int) L;
    for (uint32_t k = 0; k < L; k++)                   // finished layers
      hline(x + printer_layer[k][0], y + 42 - (int) k, printer_layer[k][1] - printer_layer[k][0] + 1);
    if (q < LAYER / 2) {                               // printing pass: lay the line behind the nozzle
      hx = xa + (xb - xa) * (int) q / (int) (LAYER / 2);
      int a = xa < hx ? xa : hx, b = xa < hx ? hx : xa;
      hline(x + a, y + ly, b - a + 1);
    } else {                                           // return pass over the fresh layer
      hx = xb + (xa - xb) * (int) (q - LAYER / 2) / (int) (LAYER / 2);
      int a = xa < xb ? xa : xb;
      hline(x + a, y + ly, pig_abs(xb - xa) + 1);
    }
    tip = ly - 1;
  } else {                                             // done: head parked up left (sparkles drawn below)
    for (uint32_t k = 0; k < NL; k++)
      hline(x + printer_layer[k][0], y + 42 - (int) k, printer_layer[k][1] - printer_layer[k][0] + 1);
    hx = 38; tip = 35;
  }
  printer_head(x, y, hx, tip);
  look = hx >= 42 ? 1 : 0;                             // eye looks further right when the nozzle is right
  int fy = ey - y - 35;                                // face offset: hop and breath
  fill(x + 24, ey, 3, 2);                              // fill the drawn eye
  if (done) {                                          // happy closed eye: a ^ 5 wide, 3 tall, set 1 px higher
    pixel(x + 22, y + fy + 35, false); pixel(x + 23, y + fy + 34, false); pixel(x + 24, y + fy + 33, false);
    pixel(x + 25, y + fy + 34, false); pixel(x + 26, y + fy + 35, false);
    pixel(x + 24, y + fy + 37, false);                 // the smile's corner rises one more px
  } else if (t % 5400 < 150) fill(x + 24 + look, ey + 1, 2, 1, false);   // blink
  else fill(x + 24 + look, ey, 2, 2, false);           // pupil where it looks
  for (int i = 26; i <= 28; i++) pixel(x + i, y + fy + 39, false);   // smile: the mouth runs back
  pixel(x + 25, y + fy + 38, false);                   // and turns up at its corner
  if (done) {
    if (dq < 2400 && (dq / 150) & 1) {                 // tail wiggle: the curl flips up and down
      fill(x, y + fy + 30, 4, 5, false);
      pixel(x + 2, y + fy + 30); pixel(x + 1, y + fy + 31); pixel(x + 3, y + fy + 31); pixel(x + 1, y + fy + 32);
      pixel(x + 2, y + fy + 32); pixel(x, y + fy + 33); pixel(x + 2, y + fy + 33); pixel(x + 3, y + fy + 34);
    }
    uint32_t sp = dq / 250;                            // five sparkles, each flipping plus <-> x out of step
    printer_spark(x + 43, y + 32, sp & 1);             // over the part
    printer_spark(x + 47, y + 34, !(sp & 1));
    printer_spark(x + 11, y + 20, (sp + 1) % 3 != 0);   // round the pig
    printer_spark(x + 29, y + 21, sp % 3 != 0);
    printer_spark(x + 5, y + 24, (sp / 2) & 1);
    if (dq >= 200 && dq < 2800) {                      // a heart pops up over the head, rises, flickers out
      uint32_t hq = dq - 200;
      if (!(hq > 2000 && (hq / 120) & 1)) sprite(x + 16, y + 17 - (int) (hq * 10 / 2600), printer_heart, 5);
    }
  }
}

// ----------------------------------------------------------------

// --- Working: leatherwork at the desk. The front hand holds a stamp on the leather; the other arm swings a
// mallet from behind the head down onto the stamp, with a small rebound. Each strike leaves a mark, the stamp
// moves on 1 px, and a zigzag row of marks grows, then the leather is clear again. Now and then, in the pause,
// the pig blows the dust away. ---
static const int8_t leather_back[18] = {19, 19, 19, 17, 15, 15, 14, 14, 13, 13, 12, 12, 11, 11, 11, 11, 11, 11};

static void leather_arm(int x, int y, int hx, int hy, int cx, int cy) {   // hand (hx, hy), mallet head (cx, cy)
  const int sx = x + 22, sy = y + 36;                  // shoulder
  hx += x; hy += y; cx += x; cy += y;
  stroke(sx, sy, hx, hy, 3, false, 0.45f);             // dark edge, so the arm reads against the body
  stroke(hx, hy, cx, cy, 1, false);
  fill(cx - 4, cy - 3, 8, 5, false);
  stroke(sx, sy, hx, hy, 2, true);                     // the arm
  stroke(hx, hy, cx, cy, 0, true);                     // the handle
  fill(cx - 3, cy - 2, 6, 4);                          // the mallet head
}

static void work_leather(int x, int y, uint32_t t) {
  sprite(x, y, LEATHER, 64);
  for (int r = 0; r < 18; r++)                         // fill in where the drawn arm crossed the back
    hline(x + leather_back[r], y + 21 + r, 26 - leather_back[r]);

  const uint32_t CYCLE = 1000;
  uint32_t c = t / CYCLE, p = t % CYCLE;
  int k = (int) (c % 8);                               // strikes 0-5 move along the leather; 6 and 7 pause
  int sx = 44 + (k < 6 ? k : 5);                       // the stamp's column
  bool strike = k < 6;
  bool blow = !strike && h32(c * 7 + 1) % 3 == 0;      // the rare event: blow the dust away

  hline(x + 41, y + 42, 11);                           // the leather, its edge up against the tool block
  vline(x + 41, y + 42, 3);
  int marks = k < 6 ? k + (p >= 450) : 6;
  for (int j = 0; j < marks; j++) pixel(x + 44 + j, y + 43 + (j & 1));

  // front arm, fist and stamp
  int drop = strike && p >= 450 && p < 650;            // the stamp sinks 1 px under the blow
  stroke(x + 35, y + 38, x + sx - 1, y + 38, 3, false, 0.3f);
  circle(x + sx, y + 38, 3, true, false);
  stroke(x + 35, y + 38, x + sx - 1, y + 38, 2, true);
  circle(x + sx, y + 38, 2, true, true);
  vline(x + sx, y + 34 + drop, 8 - drop);              // the stamp's shaft
  hline(x + sx - 1, y + 34 + drop, 3);                 // its striking cap
  vline(x + sx - 1, y + 35 + drop, 1, false); vline(x + sx + 1, y + 35 + drop, 1, false);

  if (!strike || p < 350 || p >= 880) {
    leather_arm(x, y, 17, 22, 13, 18);                 // raised behind the head, as drawn
  } else if (p < 450 || p >= 780) {
    leather_arm(x, y, 28, 22, 34, 7);                  // up over the head, clear of the crown
  } else if (p < 650) {
    leather_arm(x, y, sx - 7, 33 + drop, sx, 32 + drop);  // down on the stamp
  } else {
    leather_arm(x, y, sx - 7, 32, sx, 31);             // small rebound
  }

  if (blow && p >= 200 && p < 900) {                   // a puff from the snout, dust drifting off
    uint32_t d = p - 200;
    if (d < 300) { pixel(x + 46, y + 27); pixel(x + 47, y + 28); }
    for (int i = 0; i < 5; i++) {
      uint32_t r = h32(c * 17 + i);
      int px = 43 + (int) (r % 8) + (int) (d / 70), py = 41 - (int) (d / (180 + (r >> 8) % 120));
      if (px < 63 && ((d / 100 + i) % 3)) pixel(x + px, y + py);
    }
  }
}

// ----------------------------------------------------------------

// --- Held: the cool pig waits, arms crossed, sunglasses on. The front hoof taps in bouts; slow breathing. Now and
// then one arm uncrosses and pushes the glasses up onto the forehead: the eye looks left, then right, and the
// glasses go back down. Rarely, a fart: the tail twitches and a few small puffs drift out behind. He stays cool. ---
struct CoolSpan { int8_t y, x0, x1; };
static const CoolSpan cool_glasses[] = {             // the sunglasses as drawn: dark on the lit face
  {15, 33, 43}, {16, 24, 46}, {17, 23, 46}, {18, 23, 24}, {18, 33, 43}, {19, 23, 24}, {19, 33, 43},
  {20, 33, 42}, {21, 34, 42}, {22, 35, 40}};

static void cool_hline(int x, int y, const CoolSpan &s, int dy, bool on, int xmax) {
  for (int i = s.x0; i <= s.x1 && i <= xmax; i++) pixel(x + i, y + s.y + dy, on);
}

static void cool_foot(int x, int y, bool up) {       // front hoof, rows 58-61; toe tilted up from the heel
  static const int8_t top[5] = {58, 58, 58, 58, 58}, bot[5] = {60, 61, 61, 60, 61};
  static const int8_t lift[5] = {0, 1, 2, 3, 3};
  for (int c = 0; c < 5; c++) {
    int d = up ? lift[c] : 0;
    vline(x + 40 + c, y + top[c] - d, bot[c] - top[c] + 1);
  }
}

static void cool_tail(int x, int y, int dy) {        // curly tail as drawn, rows 45-49
  hline(x + 10, y + 45 + dy, 3);
  pixel(x + 10, y + 46 + dy); pixel(x + 12, y + 46 + dy);
  pixel(x + 10, y + 47 + dy); pixel(x + 13, y + 47 + dy);
  hline(x + 10, y + 48 + dy, 4);
  hline(x + 12, y + 49 + dy, 2);
}

static void cool_arm(int x, int y, int ex, int ey, int hx, int hy) {   // the lifting arm, out in front:
  const int sx = x + 49, sy = y + 33;                // shoulder at the chest's front, elbow (ex, ey) past the
  ex += x; ey += y; hx += x; hy += y;                // snout, hoof (hx, hy) on the glasses' front end
  stroke(sx, sy, ex, ey, 3, false, 0.3f);            // dark edges, where the arm crosses the pig
  stroke(ex, ey, hx, hy, 2, false);
  round_rect(hx - 2, hy - 2, 6, 6, 2, true, false);
  stroke(sx, sy, ex, ey, 2, true);                   // upper arm
  stroke(ex, ey, hx, hy, 1, true);                   // forearm
  round_rect(hx - 1, hy - 1, 4, 4, 1, true, true);   // the hoof
}

static void cool_rest_arm(int x, int y) {             // the other forearm, resting alone across the belly
  fill(x + 17, y + 32, 32, 13);                       // clear the crossed-arm lines (the body's edge stays)
  line(x + 20, y + 37, x + 38, y + 35, false);        // its top edge only
  round_rect(x + 38, y + 34, 7, 7, 2, false, false);  // the round hoof at its end
  vline(x + 38, y + 36, 3);                           // open where it joins the forearm
  line(x + 20, y + 37, x + 18, y + 39, false);        // the elbow's round end
  pixel(x + 43, y + 37, false);                       // the split of the hoof
}

static void cool_ring(int cx, int cy, int r, int fade, uint32_t seed) {   // a small puff ring; fade 0-100
  static const int8_t r1[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
  static const int8_t r2[12][2] = {{-1, -2}, {0, -2}, {1, -2}, {2, -1}, {2, 0}, {2, 1},
                                   {1, 2}, {0, 2}, {-1, 2}, {-2, 1}, {-2, 0}, {-2, -1}};
  int n = r == 1 ? 4 : 12;
  for (int k = 0; k < n; k++) {
    if ((int) (h32(seed * 29 + k) % 100) < fade) continue;
    const int8_t *p = r == 1 ? r1[k] : r2[k];
    pixel(cx + p[0], cy + p[1]);
  }
}

static void held_cool(int x, int y, uint32_t t) {
  bool in = breathing_in(t, 4000, 1400, 1600);
  int b = in ? 1 : 0;
  const uint32_t SLOT = 12000;                       // at most one event per slot, picked by hash: a glasses
  uint32_t n = t / SLOT, p = t % SLOT;               // lift in 42 % of slots (about every 29 s), a fart in 24 %
  uint32_t r = h32(n * 7 + 1) % 100;                 // (about every 50 s); both end inside their slot, so a
  uint32_t e0 = 1500 + h32(n * 11 + 3) % 4000;       // fart never comes during or right after a lift
  bool lift = r < 42 && p >= e0 && p < e0 + 5050;
  bool fart = r >= 42 && r < 66 && p >= e0 && p < e0 + 3300;
  uint32_t g0 = e0, f0 = e0;

  body(x, y, lift ? COOL_LIFT : COOL, 56, in);

  // Foot: bouts of 3-5 taps, one every 500 ms (toe up 250 ms), then a rest, in 3 s bouts.
  uint32_t bt = t % 3000, taps = 3 + h32(t / 3000 * 5 + 2) % 3;
  cool_foot(x, y, bt / 500 < taps && bt % 500 < 250);

  // Tail: twitches as the fart starts.
  int tdy = 0;
  if (fart) { uint32_t q = p - f0; if (q < 250 || (q >= 500 && q < 800)) tdy = -2; }   // two flicks
  cool_tail(x, y - b, tdy);

  if (fart) {                                        // three small puff rings drift out behind and fade
    uint32_t q = p - f0;
    // One after another along the same path, back and slightly up, 700 ms (about 6 px) apart.
    for (int i = 0; i < 3; i++) {
      int st = 400 + i * 700;
      int a = (int) q - st;
      if (a < 0 || a >= 1500) continue;
      int cx = 15 - 8 * a / 1000, cy = 54 - 2 * a / 1000;
      int fade = a < 900 ? 0 : (a - 900) / 6;
      cool_ring(x + cx, y + cy, a < 400 ? 1 : 2, fade, n * 3 + i);
    }
  }

  if (!lift) return;
  uint32_t q = p - g0;
  // Poses: reach 0-250, grab 250-550, half up 550-850, up 850-4250 (eye: centre, left, centre, right, centre),
  // half down 4250-4500, grab 4500-4800, reach 4800-5050.
  int gdy = 0, ex = 59, ey = 24, hx = 49, hy = 17;  // elbow, hoof
  if (q < 250 || q >= 4800) { ex = 58; ey = 28; hx = 55; hy = 20; }
  else if (q < 550 || q >= 4500) { }
  else if (q < 850 || q >= 4250) { gdy = -3; ey = 23; hy = 14; }
  else { gdy = -7; ey = 22; hy = 10; }
  int Y = y - b;
  // Fill the face where the glasses were, then draw them at their new height.
  if (gdy != 0) {
    for (const CoolSpan &s : cool_glasses) cool_hline(x, Y, s, 0, true, 45);
    for (const CoolSpan &s : cool_glasses) cool_hline(x, Y, s, gdy, false, 63);
  }
  cool_rest_arm(x, Y);
  if (gdy == -7) {                                    // the eye, looking around
    round_rect(x + 35, Y + 17, 8, 4, 1, false, false);
    int u = q - 850;
    int px = u < 600 ? 38 : u < 1600 ? 36 : u < 1900 ? 38 : u < 2900 ? 40 : 38;
    fill(x + px, Y + 18, 2, 2, false);
  }
  cool_arm(x, Y, ex, ey, hx, hy);
}

// ----------------------------------------------------------------

// --- Unseen helpers, shared by seek, search, worry and panic (seek is always built first). ---
// The plain pig is drawn in "pig columns" c 0-63 as in its picture (facing right). A pose says which
// way it faces (f: mirrored), a small shift s, and how far each of four upright slices drops (d), so the rear
// can sit, the head can dip and the pig can tip forward. Legs, the far ear, the face marks and the tail are
// drawn in code over the sprite, all through the same mapping, so they flip and shift with the pig.
struct unseen_K { int x, y, s; bool f; int d[4]; bool v; };  // d: slice drops, c 0-21, 22-33, 34-43, 44-63;
                                                             // v: on its back (rows turned over, r -> 67 - r)
struct unseen_Pig { const char *const *all; const char *const *sl[4]; };

static int unseen_X(const unseen_K &k, int c) { return k.f ? k.x + k.s - 1 + 63 - c : k.x + k.s + c; }
static int unseen_Y(const unseen_K &k, int r) { return k.v ? k.y + 67 - r : k.y + r; }
static void unseen_px(const unseen_K &k, int c, int r, bool on = true) { pixel(unseen_X(k, c), unseen_Y(k, r), on); }
static void unseen_hl(const unseen_K &k, int c, int r, int w, bool on = true) {
  if (w > 0) hline(k.f ? unseen_X(k, c + w - 1) : unseen_X(k, c), unseen_Y(k, r), w, on);
}
static void unseen_fill(const unseen_K &k, int c, int r, int w, int h, bool on = true) {
  if (w > 0 && h > 0) fill(k.f ? unseen_X(k, c + w - 1) : unseen_X(k, c), unseen_Y(k, k.v ? r + h - 1 : r), w, h, on);
}
static void unseen_line(const unseen_K &k, int c0, int r0, int c1, int r1, bool on = true) {
  line(unseen_X(k, c0), unseen_Y(k, r0), unseen_X(k, c1), unseen_Y(k, r1), on);
}
// Small picture in pig columns: '#' lit, 'o' dark, anything else left alone.
static void unseen_mask(const unseen_K &k, int c0, int r0, const char *const *rows, int n) {
  for (int r = 0; r < n; r++)
    for (int c = 0; rows[r][c]; c++)
      if (rows[r][c] == '#') unseen_px(k, c0 + c, r0 + r);
      else if (rows[r][c] == 'o') unseen_px(k, c0 + c, r0 + r, false);
}
// Text is never mirrored: only its place is.
static void unseen_text(const unseen_K &k, int c, int r, const char *s) {
  int w = text_width(s);
  text(k.f ? unseen_X(k, c + w - 1) : unseen_X(k, c), unseen_Y(k, r), s);
}
static void unseen_bang(const unseen_K &k, int c, int r) {   // a bold "!" 2 px wide, 8 tall
  unseen_fill(k, c, r, 2, 5); unseen_fill(k, c, r + 6, 2, 2);
}

// The body: one sprite when all slices sit level, else four slices. Then the belly's last row (cut with the legs).
static void unseen_body(const unseen_K &k, const unseen_Pig &p) {
  int sx = k.f ? k.x + k.s - 1 : k.x + k.s;
  if (k.v) { for (int r = 0; r < 64; r++) sprite(sx, unseen_Y(k, r + k.d[0]), p.all + r, 1, k.f); }
  else if (k.d[0] == k.d[1] && k.d[1] == k.d[2] && k.d[2] == k.d[3]) sprite(sx, k.y + k.d[0], p.all, 64, k.f);
  else for (int i = 0; i < 4; i++) sprite(sx, k.y + k.d[i], p.sl[i], 64, k.f);
  unseen_hl(k, 24, 47 + k.d[1], 10);
}

// Legs: 0 back far, 1 front far, 2 back near, 3 front near (far ones first, near ones get a dark edge).
// lift raises the hoof, lean moves the hoof forward (+) or back (-) of the hip.
struct unseen_Leg { int8_t c, w, g, sl; };
static const unseen_Leg unseen_LEGS[4] = {{22, 4, 52, 1}, {44, 4, 52, 3}, {13, 5, 53, 0}, {36, 5, 54, 2}};
static void unseen_leg(const unseen_K &k, int i, int lift, int lean) {
  const unseen_Leg &L = unseen_LEGS[i];
  int top = 46 + k.d[L.sl], g = L.g - lift;
  if (g < top + 2) g = top + 2;
  int n = g - top;
  for (int r = top; r <= g; r++) {
    int o = L.c + lean * (r - top) / n;
    if (i >= 2 && r > top) { unseen_px(k, o - 1, r, false); unseen_px(k, o + L.w, r, false); }
    if (r < g) unseen_hl(k, o, r, L.w);
    else { unseen_hl(k, o, r, L.w, false); unseen_px(k, o + 1, r); unseen_px(k, o + L.w - 2, r); }
  }
}
static void unseen_legs(const unseen_K &k, const int lift[4], const int lean[4]) {
  for (int i = 0; i < 4; i++) unseen_leg(k, i, lift[i], lean[i]);
}
// Standing, or one step of a walk: phase 0 lifts back-near and front-far, phase 1 the other pair.
static void unseen_walk(const unseen_K &k, int phase, int lift) {
  int up[4] = {0, 0, 0, 0}, ln[4] = {0, 0, 0, 0};
  if (phase >= 0) {
    int a = phase ? 0 : 1, b = phase ? 3 : 2;
    up[a] = up[b] = lift; ln[a] = ln[b] = 1;
  }
  unseen_legs(k, up, ln);
}

// The far ear (its drawn place is erased in every json). 0 as drawn, 1 pricked up, 2 drooping, 3 pinned back,
// 4 pinned back and flapping up. First the head's own edge where the ear was.
static const char *const unseen_EAR_DRAWN[] = {
    "...####.......", "##..####......", "####..####....", "#####..###....", "######..###...",
    "#######..##...", "########.###..", "########..##.."};          // c 47-60, rows 15-22
static const char *const unseen_EAR_DROOP[] = {
    "###..........", "..###........", "....o##......", ".....o##.....", "......o##....", ".......o###..",
    "........o###.", "........o###.", ".........o###", "..........o##", "..........o##", "...........o#"};  // c 47, rows 15-26
static const char *const unseen_EAR_UP[] = {
    "......##....", ".....####...", ".....#####..", "....o#####..", "....o######.", "...o#######.",
    "...o#######.", "..o########.", "..o#######..", "###o######..", "#####o###...", "#######o#..."};   // c 44, rows 6-17
static void unseen_ear(const unseen_K &k, int pose) {
  int h = k.d[3];
  if (pose == 0) { unseen_mask(k, 47, 15 + h, unseen_EAR_DRAWN, 8); return; }
  static const int8_t edge[8] = {46, 48, 50, 51, 52, 53, 54, 54};    // head's right edge, rows 15-22
  for (int r = 0; r < 8; r++) unseen_hl(k, 47, 15 + r + h, edge[r] - 46);
  if (pose == 1) {                                  // pricked: the drawn ear turned to point up
    unseen_mask(k, 44, 6 + h, unseen_EAR_UP, 12);
  } else if (pose == 2) {
    unseen_mask(k, 47, 15 + h, unseen_EAR_DROOP, 12);
  } else {                                          // pinned back along the crown, tip behind
    int tip = pose == 4 ? 8 : 10;
    for (int r = tip; r <= 15; r++) {
      int l = 35 + (r - tip) * 6 / (14 - tip + 1), rt = 36 + (r - tip) * 14 / (16 - tip);
      unseen_hl(k, l, r + h, rt - l + 1);
    }
    unseen_line(k, 39, tip + 3 + h, 47, 14 + h, false);
  }
}
// The near ear (outline on the head) hangs instead of standing: its outline turned upside down.
static const char *const unseen_NEAR[] = {
    "..###.....", ".#..##....", ".#...#....", ".#....#...", "#.....##..", "#......#..", "#......##.",
    "#.......#.", "#.........", "#.........", "##.......#", ".###....##", "....####.."};   // c 31: the drawn rows 29 up to 17
static void unseen_near_droop(const unseen_K &k) {
  int h = k.d[2];
  unseen_fill(k, 31, 17 + h, 10, 14);
  for (int r = 0; r < 13; r++)
    for (int c = 0; c < 10; c++)
      if (unseen_NEAR[r][9 - c] == '#') unseen_px(k, 31 + c, 18 + r + h, false);
}
// Face: eye 0 open, 1 shut, 2 wide, 3 open a little wider; brows 0 none, 1 worried, 2 frantic, 3 raised;
// mouth 0 as drawn (smiling), 1 down, 2 open, 3 straight (the drawn corner filled: neutral).
static void unseen_face(const unseen_K &k, int eye, int brow, int mouth) {
  int h = k.d[3];
  if (eye == 1) { unseen_fill(k, 45, 28 + h, 3, 4); unseen_hl(k, 45, 30 + h, 3, false); }
  if (eye == 3) { unseen_px(k, 45, 27 + h, false); unseen_px(k, 46, 27 + h, false); unseen_px(k, 47, 28 + h, false); }
  if (eye == 2) { unseen_fill(k, 44, 27 + h, 5, 5, false); unseen_px(k, 46, 29 + h); }
  if (brow == 1) { unseen_hl(k, 43, 26 + h, 2, false); unseen_hl(k, 45, 25 + h, 2, false); unseen_px(k, 47, 24 + h, false); }
  if (brow == 3) unseen_hl(k, 45, 25 + h, 3, false);
  if (brow == 2) unseen_line(k, 42, 26 + h, 47, 22 + h, false);
  if (mouth == 1) { unseen_px(k, 48, 38 + h, false); unseen_px(k, 47, 38 + h, false); }
  if (mouth == 2) unseen_fill(k, 47, 37 + h, 4, 2, false);
  if (mouth == 3) unseen_px(k, 49, 37 + h);
}
// Sniff: the nostrils flare (each 2 px wide), and with marks, two dashes drawn in from the air at the snout.
static void unseen_sniff(const unseen_K &k, uint32_t t, bool marks) {
  int h = k.d[3];
  unseen_px(k, 54, 33 + h, false); unseen_px(k, 54, 34 + h, false);
  unseen_px(k, 57, 33 + h, false); unseen_px(k, 57, 34 + h, false);
  if (!marks) return;
  int m = (int) (t / 250 % 3);
  unseen_px(k, 62 - m, 29 + h); unseen_hl(k, 61 - m, 26 + h, 2 - (m == 0));
}

// Pacing in place: per half period, four steps in the facing direction (shift -1 to 2) and one standing still,
// then it turns. ph is the walking phase, -1 while standing.
static void unseen_pace(uint32_t t, uint32_t step, int &s, bool &f, int &ph) {
  uint32_t q = t % (step * 10);
  f = q >= step * 5;
  int i = (int) (q % (step * 5) / step), j = i < 4 ? i : 3;
  s = f ? 2 - j : -1 + j;
  ph = i < 4 ? (i & 1) : -1;
}

// --- Unseen 1, "Looking": calm. Each 6 s the pig faces one way; within that, 1 s beats of standing (ears pricked),
// a step in place, a sniff (snout dips twice) or a glance the other way. Blinks. A "?" now and then.
// Rare: it sits down a moment. ---
static void unseen_seek(int x, int y, uint32_t t) {
  static const unseen_Pig P = {SEEK, {SEEK_S0, SEEK_S1, SEEK_S2, SEEK_S3}};
  const uint32_t W = 6000;
  uint32_t w = t / W, p = t % W, b = p / 1000, q = p % 1000;
  bool sit = h32(w * 11 + 15) % 6 == 0 && p >= 500 && p < 5500;
  int act = (int) (h32(w * 7 + b) % 6);              // 0-1 stand, 2-3 walk, 4 sniff, 5 glance back
  if (sit && (act == 2 || act == 3 || act == 4)) act = 0;
  unseen_K k = {x, y, 0, (h32(w * 3 + 1) & 1) != 0, {0, 0, 0, 0}, false};
  if (act == 5 && q < 750) k.f = !k.f;
  if (sit) { k.d[0] = 3; k.d[1] = 2; k.d[2] = 1; }
  bool dip = act == 4 && ((q >= 200 && q < 450) || (q >= 600 && q < 850));
  if (dip) k.d[3] = 1;
  unseen_body(k, P);
  unseen_ear(k, act == 0 || act == 1 || act == 5 || sit ? 1 : 0);
  if (sit) {                                         // haunches folded: back legs short, feet tucked forward
    int up[4] = {0, 0, 0, 0}, ln[4] = {1, 0, 2, 0};
    unseen_legs(k, up, ln);
  } else {
    unseen_walk(k, act == 2 || act == 3 ? (int) (q / 500) : -1, 1);
  }
  unseen_face(k, t % 3600 < 250 ? 1 : 0, 0, 3);
  if (dip) unseen_sniff(k, t, false);
  if (h32(w * 5 + 4) % 3 == 0 && p >= 1000 && p < 3000) unseen_text(k, 40, 1, "?");
}

// ----------------------------------------------------------------

// --- Unseen 2, "Looking harder": busier, and more alert than step 1 even when still: head held 1 px higher,
// eye a little wider under a raised brow, ears pricked all the time. 3 s per facing, 0.75 s beats: quick steps
// in place, sniffs (snout dips, dashes of air drawn in at the nose), a glance back. A "?" (or "??") often.
// Rare: stands tall, front up, sniffing the air. ---
static void unseen_search(int x, int y, uint32_t t) {
  static const unseen_Pig P = {SEARCH, {SEARCH_S0, SEARCH_S1, SEARCH_S2, SEARCH_S3}};
  const uint32_t W = 3000, R = 15000;
  uint32_t w = t / W, p = t % W, b = p / 750, q = p % 750;
  uint32_t rp = t % R;
  bool tall = h32(t / R * 13 + 5) % 4 == 0 && rp >= 6000 && rp < 9750;
  int act = (int) (h32(w * 7 + b + 1) % 5);         // 0 walk, 1-2 sniff, 3 glance back, 4 stand
  unseen_K k = {x, y, 0, (h32(w * 3 + 2) & 1) != 0, {0, 0, 0, -1}, false};
  if (tall) k.f = (h32((t - rp + 6000) / W * 3 + 2) & 1) != 0;   // keeps the facing it stood up in
  bool dip = false, sniffing = false;
  if (tall) {
    k.d[0] = 1; k.d[2] = -2; k.d[3] = -3;
    sniffing = (t / 250) & 1;
  } else {
    if (act == 3 && q < 500) k.f = !k.f;
    if (act == 1 || act == 2) { sniffing = true; dip = q < 250 || q >= 500; }
    if (dip) k.d[3] = 1;
  }
  unseen_body(k, P);
  unseen_ear(k, 1);
  unseen_walk(k, !tall && act == 0 ? (int) (q / 375) : -1, 2);
  unseen_face(k, t % 3750 < 250 ? 1 : 3, 3, 3);
  if (sniffing) unseen_sniff(k, t, true);
  if (!tall && h32(w * 5 + 3) % 2 == 0 && p < 2000) {
    unseen_text(k, 40, 1, "?");
    if (h32(w * 5 + 4) % 2 == 0 && p >= 750) unseen_text(k, 32, 3, "?");
  }
}

// ----------------------------------------------------------------

// --- Unseen 3, "Worried": ears droop, brows slant up at the nose, the mouth turns down. Paces in place: four
// slow steps one way, a stop, turn, four steps back. A sweat drop forms on the crown, slides down the hanging
// ear and falls past the snout. Rare: a big sigh, rising on the breath in and slumping on the way out, with a
// puff from the snout. ---
// The drop keeps time with the pacing (4 s): it beads while the pig faces left, slides down the hanging ear in
// 250 ms steps, and falls in front of the snout just after the turn, while the pig stands far enough left.
static void worry_drop(const unseen_K &k, uint32_t t) {
  static const int8_t PX[6] = {45, 49, 52, 55, 58, 60}, PY[6] = {14, 14, 15, 17, 19, 22};
  uint32_t u = (t + 1650) % 4000;
  int h = k.d[3];
  if (u < 250) { unseen_px(k, 45, 13 + h); unseen_px(k, 45, 14 + h); return; }           // it beads up
  if (u < 1750) {
    int i = (int) ((u - 250) / 250), c = PX[i], r = PY[i] + h;
    unseen_px(k, c, r - 2); unseen_hl(k, c - 1, r - 1, 3); unseen_hl(k, c - 1, r, 3);
    return;
  }
  if (u < 2250) {                                               // falls clear of the snout, on the dark
    int r = (u < 2000 ? 25 : 34) + h;
    unseen_px(k, 62, r); unseen_px(k, 62, r + 1); unseen_px(k, 62, r + 2);
  }
}

static void unseen_worry(int x, int y, uint32_t t) {
  static const unseen_Pig P = {WORRY, {WORRY_S0, WORRY_S1, WORRY_S2, WORRY_S3}};
  const uint32_t SW = 12000;
  uint32_t sp = t % SW;
  bool sigh = h32(t / SW * 7 + 11) % 3 == 0 && sp >= 6000 && sp < 8800;
  unseen_K k = {x, y, 0, false, {0, 0, 0, 0}, false};
  int ph;
  unseen_pace(sigh ? t - sp + 6000 : t, 400, k.s, k.f, ph);
  uint32_t u = sp - 6000;
  if (sigh) {
    ph = -1;
    int d = u < 900 ? -1 : u < 2600 ? 1 : 0;
    for (int i = 0; i < 4; i++) k.d[i] = d;
  }
  unseen_body(k, P);
  unseen_near_droop(k);
  unseen_ear(k, 2);
  unseen_walk(k, ph, 1);
  bool shut = (sigh && u >= 900 && u < 2200) || t % 6000 < 250;
  unseen_face(k, shut ? 1 : 0, 1, 1);
  if (sigh && u >= 1000 && u < 2400) {                          // the puff: three dots drifting down and away
    int v = (int) (u - 1000);
    for (int i = 0; i < 3; i++) {
      int c = 58 + (v + i * 300) / 400, r = 38 + (v + i * 200) / 280 + k.d[3];
      if (c <= 62 && (v / 250 + i) % 4) unseen_px(k, c, r);
    }
  } else {
    worry_drop(k, t);
  }
}

// ----------------------------------------------------------------

// --- Unseen 4, "Panic": runs back and forth (100 ms strides, legs flung out and tucked, turns every half
// second), then stops and trembles. Eyes wide, brows high, mouth open, ears pinned back and flapping, "!"
// flashing, sweat flying off, the tail spinning like a propeller. Rare: trips and lands on its nose, stars
// circling. ---
static void panic_tail(const unseen_K &k, uint32_t t) {
  static const int8_t V[4][2] = {{3, 0}, {2, -2}, {0, -3}, {-2, -2}};   // half a turn: the blade is two-ended
  int a = (int) (t / 250 % 4), b = (a + 2) % 4, h = k.d[0];
  unseen_line(k, 10, 23 + h, 7, 22 + h);                         // the stem
  unseen_line(k, 6 - V[a][0], 22 - V[a][1] + h, 6 + V[a][0], 22 + V[a][1] + h);
  unseen_px(k, 6 + V[b][0], 22 + V[b][1] + h);                   // blur across the blade
  unseen_px(k, 6 - V[b][0], 22 - V[b][1] + h);
}

static void unseen_panic(int x, int y, uint32_t t) {
  static const unseen_Pig P = {PANIC, {PANIC_S0, PANIC_S1, PANIC_S2, PANIC_S3}};
  const uint32_t C = 3750;
  uint32_t c = t / C, p = t % C;
  bool run = p < 2500;
  bool fall = !run && h32(c * 5 + 3) % 9 == 0;                   // about every 34 s
  uint32_t fu = p - 2500;
  bool back = fall && fu >= 250 && fu < 1000;                    // on its back, legs kicking
  bool tip = fall && !back;                                      // going over, and getting up
  unseen_K k = {x, y, 0, false, {0, 0, 0, 0}, false};
  int up[4] = {0, 0, 0, 0}, ln[4] = {0, 0, 0, 0};
  bool stride = (t / 250) & 1;
  if (run) {
    int ph;
    unseen_pace(t, 250, k.s, k.f, ph);
    if (stride) {                                                // flung out, off the ground
      for (int i = 0; i < 4; i++) k.d[i] = -1;
      ln[1] = ln[3] = 3; ln[0] = ln[2] = -3; up[0] = up[1] = up[2] = up[3] = 1;
    } else {                                                     // tucked under
      ln[1] = ln[3] = -2; ln[0] = ln[2] = 2;
    }
  } else {
    k.f = (h32(c * 3 + 1) & 1) != 0;
    uint32_t j = h32(t / 250 + 77);                              // trembling: a 1 px jolt picked per 250 ms
    k.s = (int) (j & 1);
    for (int i = 0; i < 4; i++) k.d[i] = (j & 6) ? 0 : -1;
    ln[1] = ln[3] = 1; ln[0] = ln[2] = -1;
    if (tip) {                                                   // tipping: nose down, rump up
      k.s = 0;
      k.d[0] = -2; k.d[1] = -1; k.d[2] = 1; k.d[3] = 3;
      up[1] = up[3] = 3;
    }
    if (back) {                                                  // flat on its back, four legs in the air
      k.s = 0; k.v = true;
      for (int i = 0; i < 4; i++) { k.d[i] = 0; ln[i] = (fu / 250) & 1 ? 1 : -1; }
    }
  }
  unseen_body(k, P);
  unseen_near_droop(k);
  if (!back) unseen_ear(k, run && stride ? 4 : 3);
  unseen_legs(k, up, ln);
  unseen_face(k, fall ? 1 : 2, fall ? 0 : 2, 2);
  panic_tail(k, t);
  if (run && stride) { unseen_px(k, 7, 52); unseen_px(k, 9, 51); unseen_px(k, 5, 53); }   // dust kicked back

  if (fall) {                                                    // stars over it, hopping round in 250 ms steps
    static const int8_t S[6][2] = {{40, 4}, {47, 2}, {54, 4}, {51, 8}, {43, 8}, {36, 7}};
    for (int i = 0; i < 3; i++) {
      const int8_t *s = S[(t / 250 + i * 2) % 6];
      int sc = s[0], sr = s[1];
      unseen_K o = k; o.v = false; o.d[3] = 0;
      unseen_px(o, sc, sr); unseen_px(o, sc - 1, sr); unseen_px(o, sc + 1, sr);
      unseen_px(o, sc, sr - 1); unseen_px(o, sc, sr + 1);
    }
    return;
  }
  uint32_t b = t / 250 % 3;                                      // "!" out front, clear of the ear and the sweat
  if (b != 2) unseen_bang(k, 52, 0);
  if (b != 0) unseen_bang(k, 57, 1);
  for (int i = 0; i < 3; i++) {                                  // sweat flung back off the crown, behind the ear
    uint32_t u = (t + i * 250) % 750, n = (t + i * 250) / 750, st = u / 250;
    uint32_t r = h32(n * 3 + i);
    int sc = 33 - (int) (r % 4) - (int) st * (4 + (int) ((r >> 4) % 3));
    int sr = 10 - (int) (r >> 8) % 3 - (st == 1 ? 3 : st == 2 ? 1 : 0);
    unseen_px(k, sc, sr); unseen_px(k, sc, sr + 1);
  }
}

// ---------------------------------------------------------------- the list

// When each scene plays:
//   PIG_IDLE      the bar is off;
//   PIG_WORKING   the bar is on and you are at the bench;
//   PIG_HELD      the bar is held on while you are away;
//   PIG_UNSEEN    the bar is on, you are not seen, and the countdown to the cut runs. These scenes are
//                 steps 1 to 4, more worried at each step: step 1 for the first third of the countdown, step 2
//                 for the second third, step 3 for the last third, step 4 for the last sixth.
enum PigState : uint8_t { PIG_IDLE, PIG_WORKING, PIG_HELD, PIG_UNSEEN };

struct PigScene {
  const char *name;
  PigState state;
  uint8_t step;                  // 1-4 for the PIG_UNSEEN scenes, 0 for the others
  void (*draw)(int x, int y, uint32_t t);
};

static const PigScene PIG_SCENES[] = {
    {"Coffee",         PIG_IDLE,     0, idle_coffee},
    {"Asleep",         PIG_IDLE,     0, idle_sleep},
    {"Bath",           PIG_IDLE,     0, idle_bath},
    {"Meditate",       PIG_IDLE,     0, idle_meditate},
    {"Reading",        PIG_IDLE,     0, idle_read},
    {"Music",          PIG_IDLE,     0, idle_music},
    {"Butterfly",      PIG_IDLE,     0, idle_butterfly},
    {"Anvil",          PIG_WORKING,  0, work_anvil},
    {"Soldering",      PIG_WORKING,  0, work_solder},
    {"Welding",        PIG_WORKING,  0, work_weld},
    {"Tinker",         PIG_WORKING,  0, work_tinker},
    {"3D printer",     PIG_WORKING,  0, work_printer},
    {"Leather",        PIG_WORKING,  0, work_leather},
    {"Sunglasses",     PIG_HELD,     0, held_cool},
    {"Looking",        PIG_UNSEEN,   1, unseen_seek},
    {"Looking harder", PIG_UNSEEN,   2, unseen_search},
    {"Worried",        PIG_UNSEEN,   3, unseen_worry},
    {"Panic",          PIG_UNSEEN,   4, unseen_panic},
};
static const int PIG_SCENE_COUNT = (int) (sizeof PIG_SCENES / sizeof PIG_SCENES[0]);
