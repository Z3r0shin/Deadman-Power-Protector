// Renders every OLED screen to PBM images, so the layout can be looked at on a PC before it is flashed.
// Build and run: make -C tools   (then tools/topng.py turns the PBMs into scaled PNGs).
#include <cstdio>
#include <cstring>

#include "../src/gfx.h"
#include "../src/screens.h"

using namespace screens;

static void save(const char *name) {
  char path[128];
  snprintf(path, sizeof(path), "out/%s.pbm", name);
  FILE *f = fopen(path, "wb");
  fprintf(f, "P1\n%d %d\n", gfx::W, gfx::H);
  const uint8_t *b = gfx::buffer();
  for (int y = 0; y < gfx::H; y++) {
    for (int x = 0; x < gfx::W; x++) fputc((b[(y >> 3) * gfx::W + x] >> (y & 7)) & 1 ? '1' : '0', f);
    fputc('\n', f);
  }
  fclose(f);
}

static Model base() {
  Model m;
  m.now_ms = 12345;
  m.phase = Phase::On;
  m.cut_s = 1200; m.cut_total_s = 1200;
  m.limit_s = 3 * 3600 + 52 * 60; m.limit_total_s = 14400;
  m.lease_s = 283; m.lease_total_s = 300;
  m.present = true; m.dist_cm = 162; m.move_en = 23; m.still_en = 100;
  m.clock_ok = true; m.hh = 13; m.mm = 47;
  m.watts = 42; m.temp_c = 44;
  m.address = "192.0.2.10";
  m.version = "v.1.1 261027T1530Z";
  m.commit = "1a7135c";
  return m;
}

int main() {
  Model m = base();
  m.view = View::Status; draw(m); save("01_status_here");
  m.present = false; m.seen_ago_s = 312; m.cut_s = 888; draw(m); save("02_status_away");
  m.phase = Phase::Hold; m.hold_active = true; m.hold_s = 2700; m.hold_total_s = 3600; m.big = I_HOLD; draw(m); save("03_status_hold");
  m = base(); m.view = View::Status; m.phase = Phase::Off; m.cut_s = -1; draw(m); save("04_status_off");
  m.phase = Phase::Unknown; draw(m); save("05_status_unknown");
  m = base(); m.view = View::Status; m.quiet = true; m.link_ok = false; m.fault = "sense_dead"; draw(m); save("06_status_fault_quiet");
  m = base(); m.view = View::Timers; m.present = false; m.cut_s = 600; m.hold_active = true; m.hold_s = 1500; m.hold_total_s = 3600; draw(m); save("07_timers");
  m = base(); m.view = View::Radar; draw(m); save("08_radar");
  m.present = false; draw(m); save("09_radar_nobody");
  m = base(); m.view = View::Menu; m.menu_min = 45; draw(m); save("10_menu_min");
  m.menu_min = 90; m.menu_hours = true; draw(m); save("11_menu_hours");
  m.menu_min = 0; m.menu_hours = false; m.hold_active = true; draw(m); save("12_menu_cancel");
  m = base(); m.view = View::HoldToCut; m.hold_to_cut = 0.6f; draw(m); save("13_hold_to_cut");
  m = base(); m.view = View::Warning; m.why = Why::Absence; m.warn_s = 254; m.warn_total_s = 300; m.now_ms = 100; draw(m); save("14_warning");
  m.why = Why::Ceiling; m.now_ms = 600; draw(m); save("15_warning_ceiling_blinkoff");
  m.why = Why::Absence; m.warn_s = 9; m.now_ms = 200; draw(m); save("16_warning_final_inverted");
  m.why = Why::Link; m.warn_s = 180; m.now_ms = 100; draw(m); save("17_warning_link");
  m = base(); m.view = View::Alarm; m.now_ms = 700; draw(m); save("18_alarm");
  m.alarm_stuck = false; m.now_ms = 100; draw(m); save("19_alarm_lost_inverted");
  m.alarm_silenced = true; draw(m); save("20_alarm_silenced");
  m = base(); m.view = View::Toast; m.toast = "OK, staying on"; draw(m); save("21_toast");
  m = base(); m.view = View::Boot; m.now_ms = 900; draw(m); save("22_boot");
  m = base(); m.view = View::Ambient; m.now_ms = 40000; draw(m); save("23_ambient_here");
  m.present = false; m.cut_s = 742; m.now_ms = 95000; draw(m); save("24_ambient_away");
  m = base(); m.view = View::Status; m.big = I_LIMIT; m.foot_l = I_PRESENCE; m.foot_r = I_CLOCK; draw(m); save("25_status_limit_big");
  // As the device shifts them against burn-in: the extreme corners.
  for (int s = 0; s < 2; s++) {
    gfx::offset(s ? 1 : -1, s ? 1 : -1);
    m = base(); m.view = View::Status; draw(m); save(s ? "90_shift_down_status" : "90_shift_up_status");
    m = base(); m.view = View::Menu; m.menu_min = 45; draw(m); save(s ? "91_shift_down_menu" : "91_shift_up_menu");
    m = base(); m.view = View::Timers; m.cut_s = 600; draw(m); save(s ? "92_shift_down_timers" : "92_shift_up_timers");
  }
  gfx::offset(0, 0);
  m = base(); m.view = View::Status; m.foot_r = I_ADDRESS; m.big = I_ADDRESS; draw(m); save("26_status_address");
  m = base(); m.view = View::Toast; m.toast = "Hold ON 1.5 s to cut"; m.toast_good = false; draw(m); save("27_toast_long");
  m = base(); m.view = View::Toast; m.toast = "Cut: nobody seen"; draw(m); save("28_toast_cut");
  for (int s = 0; s < 2; s++) {
    gfx::offset(s ? 1 : -1, s ? 1 : -1);
    m = base(); m.view = View::Warning; m.why = Why::Absence; m.warn_s = 254; m.now_ms = 100; draw(m);
    save(s ? "93_shift_down_warning" : "93_shift_up_warning");
    m.warn_s = 9; m.now_ms = 200; draw(m); save(s ? "94_shift_down_final" : "94_shift_up_final");
  }
  gfx::offset(0, 0);
  // The idle pig: each scene of each state, at two moments.
  {
    struct Shot { const char *name; Phase ph; bool present, hold; int32_t cut; int scenes; };
    const Shot shots[] = {
      {"idle", Phase::Off, false, false, -1, 7},
      {"work", Phase::On, true, false, 1200, 6},
      {"work_hold", Phase::Hold, true, true, 3000, 1},
      {"held", Phase::Hold, false, true, 3000, 1},
      {"unseen1_look", Phase::On, false, false, 1100, 1},
      {"unseen2_harder", Phase::On, false, false, 800, 1},
      {"unseen3_worried", Phase::On, false, false, 520, 1},
      {"unseen4_panic", Phase::On, false, false, 350, 1},
    };
    for (const Shot &sh : shots)
      for (int i = 0; i < sh.scenes; i++)
        for (int k = 0; k < 2; k++) {
          m = base(); m.view = View::Ambient; m.pig = true; m.phase = sh.ph; m.present = sh.present;
          m.hold_active = sh.hold; m.hold_total_s = 3600; m.hold_s = 1800;
          m.cut_s = sh.cut; m.cut_total_s = 1200; m.warn_total_s = 300;
          m.pig_scene_ms = 20000; m.now_ms = (uint32_t) i * 20000 + 1500 + (uint32_t) k * 3700;
          char name[48];
          snprintf(name, sizeof(name), "p_%s_%d%c", sh.name, i, k ? 'b' : 'a');
          draw(m); save(name);
        }
  }
  printf("rendered\n");
  return 0;
}
