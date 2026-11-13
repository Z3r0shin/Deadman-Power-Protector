// LD2410C serial protocol. Frame formats were checked against an LD2410C running firmware V2.68.25070917:
// version, parameter read-back and engineering frames all decoded as expected.
#include "radar.h"

#include "console.h"
#include "settings.h"

RadarState radar;
RadarCal radar_cal;

static constexpr uint32_t CAL_SAMPLE_MS = 60000;
static uint8_t rx[96];
static size_t rx_len = 0;
static bool eng_before_cal = false;
static void (*idle_hook)() = nullptr;

void radar_set_idle(void (*fn)()) { idle_hook = fn; }

static void send(uint16_t cmd, const uint8_t *val = nullptr, uint16_t val_len = 0) {
  const uint8_t head[4] = {0xFD, 0xFC, 0xFB, 0xFA}, tail[4] = {0x04, 0x03, 0x02, 0x01};
  uint16_t len = 2 + val_len;
  Serial1.write(head, 4);
  Serial1.write((uint8_t) len); Serial1.write((uint8_t) (len >> 8));
  Serial1.write((uint8_t) cmd); Serial1.write((uint8_t) (cmd >> 8));
  if (val_len) Serial1.write(val, val_len);
  Serial1.write(tail, 4);
  Serial1.flush();
  // The sensor needs a moment between commands. Keep the keys, the warning and the lease running
  // meanwhile: a full settings write takes about 2 s.
  uint32_t t0 = millis();
  while (millis() - t0 < 60) {
    if (idle_hook) idle_hook();
    delay(2);
  }
}

static void put_word_dword(uint8_t *p, uint16_t word, uint32_t dword) {
  p[0] = word; p[1] = word >> 8;
  p[2] = dword; p[3] = dword >> 8; p[4] = dword >> 16; p[5] = dword >> 24;
}

// Configuration mode wraps every command.
static void config(uint16_t cmd, const uint8_t *val = nullptr, uint16_t val_len = 0) {
  const uint8_t enable[2] = {0x01, 0x00};
  send(0x00FF, enable, 2);
  send(cmd, val, val_len);
  if (cmd != 0x00A3) send(0x00FE);      // a restart leaves configuration mode by itself
}

static void report(const uint8_t *d, uint16_t len) {
  // A report ends with 0x55 0x00; anything else is a damaged frame and changes nothing.
  if (len < 13 || d[1] != 0xAA || d[len - 2] != 0x55 || d[len - 1] != 0x00) { radar.bad++; return; }
  radar.engineering = d[0] == 0x01;
  radar.state = d[2];
  radar.move_cm = d[3] | d[4] << 8;
  radar.move_en = d[5];
  radar.still_cm = d[6] | d[7] << 8;
  radar.still_en = d[8];
  radar.detect_cm = d[9] | d[10] << 8;
  if (radar.engineering && len >= 13) {
    uint8_t n = d[11] > 8 ? 8 : d[11], m = d[12] > 8 ? 8 : d[12];
    for (int g = 0; g <= n && 13 + g < len; g++) radar.gate_move[g] = d[13 + g];
    for (int g = 0; g <= m && 14 + n + g < len; g++) radar.gate_still[g] = d[14 + n + g];
    if (radar_cal.stage == RadarCal::Sample) {
      for (int g = 0; g < 9; g++) {
        if (radar.gate_move[g] > radar_cal.max_move[g]) radar_cal.max_move[g] = radar.gate_move[g];
        if (radar.gate_still[g] > radar_cal.max_still[g]) radar_cal.max_still[g] = radar.gate_still[g];
      }
      radar_cal.samples++;
    }
  }
  radar.frames++;
  radar.last_frame = millis();
}

static void ack(const uint8_t *d, uint16_t len) {
  if (len < 4) return;
  uint16_t cmd = d[0] | d[1] << 8, status = d[2] | d[3] << 8;
  radar.acks++;
  if (status) { radar.failed_acks++; Con.printf("Radar: command 0x%04X FAILED (status %u)\n", cmd & 0xFF, status); }
  if (cmd == 0x01A0 && len >= 12) {
    snprintf(radar.version, sizeof(radar.version), "V%u.%02u.%02X%02X%02X%02X", d[7], d[6], d[11], d[10], d[9], d[8]);
    Con.printf("Radar firmware %s\n", radar.version);
  } else if (cmd == 0x0161 && len >= 6 && d[4] == 0xAA) {
    uint8_t n = d[5] > 8 ? 8 : d[5];
    if (len >= (uint16_t) (12 + 2 * n)) {
      radar.max_move_gate = d[6];
      radar.max_still_gate = d[7];
      for (int g = 0; g <= n; g++) { radar.sens_move[g] = d[8 + g]; radar.sens_still[g] = d[9 + n + g]; }
      radar.hold_s = d[10 + 2 * n] | d[11 + 2 * n] << 8;
      radar.params_ok = true;
    }
  }
}

void radar_loop() {
  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    if (rx_len < 4) {
      const bool rep = (rx_len == 0 && b == 0xF4) || (rx_len > 0 && rx[0] == 0xF4 && b == 0xF4 - rx_len);
      const bool ak = (rx_len == 0 && b == 0xFD) || (rx_len > 0 && rx[0] == 0xFD && b == 0xFD - rx_len);
      if (rep || ak) rx[rx_len++] = b; else rx_len = (b == 0xF4 || b == 0xFD) ? (rx[0] = b, 1) : 0;
      continue;
    }
    rx[rx_len++] = b;
    if (rx_len >= 6) {
      uint16_t len = rx[4] | rx[5] << 8;
      if (len > sizeof(rx) - 10) { radar.bad++; rx_len = 0; continue; }
      if (rx_len == (size_t) len + 10) {
        // The frame's tail must match its head (reports F8 F7 F6 F5, command answers 04 03 02 01).
        const uint8_t *t = rx + 6 + len;
        bool tail_ok = rx[0] == 0xF4 ? (t[0] == 0xF8 && t[1] == 0xF7 && t[2] == 0xF6 && t[3] == 0xF5)
                                     : (t[0] == 0x04 && t[1] == 0x03 && t[2] == 0x02 && t[3] == 0x01);
        if (!tail_ok) radar.bad++;
        else if (rx[0] == 0xF4) report(rx + 6, len);
        else ack(rx + 6, len);
        rx_len = 0;
      }
    }
  }

  static uint32_t fps_at = 0, frames_prev = 0;
  uint32_t now = millis();
  if (now - fps_at >= 1000) { radar.fps = radar.frames - frames_prev; frames_prev = radar.frames; fps_at = now; }

  // Calibration stages.
  if (radar_cal.stage == RadarCal::Wait && (int32_t) (now - radar_cal.until) >= 0) {
    memset(radar_cal.max_move, 0, 9);
    memset(radar_cal.max_still, 0, 9);
    radar_cal.samples = 0;
    radar_cal.stage = RadarCal::Sample;
    radar_cal.until = now + CAL_SAMPLE_MS;
    Con.println("Radar calibration: sampling the empty room for 60 s");
  } else if (radar_cal.stage == RadarCal::Sample && (int32_t) (now - radar_cal.until) >= 0) {
    if (radar_cal.samples < 100) {
      Con.printf("Radar calibration FAILED: only %u per-gate frames\n", radar_cal.samples);
      radar_cal.stage = RadarCal::Idle;
    } else {
      int margin = setting_int("radar_margin");
      char key[12];
      for (int g = 0; g < 9; g++) {
        snprintf(key, sizeof(key), "radar_m%d", g);
        setting_set_int(key, min(100, radar_cal.max_move[g] + margin));
        snprintf(key, sizeof(key), "radar_s%d", g);
        setting_set_int(key, min(100, radar_cal.max_still[g] + margin));
      }
      Con.printf("Radar calibration done: %u frames. Thresholds set to the room's maximum + %d.\n",
                 radar_cal.samples, margin);
      radar_cal.stage = RadarCal::Done;
      radar_apply_settings();
    }
    if (!eng_before_cal) radar_engineering(false);
  }
}

void radar_begin(int rx_pin, int tx_pin) {
  Serial1.setRxBufferSize(1024);
  Serial1.begin(256000, SERIAL_8N1, rx_pin, tx_pin);
  radar_ask_version();
  radar_apply_settings();
}

bool radar_alive(uint32_t dead_ms) { return radar.frames && millis() - radar.last_frame < dead_ms; }
// A target counts only with enough energy. The sensor's own target flag also fires on weak echoes close to it:
// in an empty room it was seen to report "still" at 61 cm with energy 4-6 and "moving" at 80-90 cm with energy 13,
// which kept the absence clock from ever running out. A person seated at the bench reads still energy 100.
// It must also last: a target counts only once it has stayed above those energies for radar_min_ms. The echo
// near the radar was seen to cross energy 30 for single frames (0.1 s), twice in 3 min, which was enough to
// restart the absence clock; a person at the bench reads still energy 100 without a break.
bool radar_present() {
  static uint32_t since = 0;                       // when the target started counting (0: it does not)
  // A frame older than 0.3 s (the sensor sends 10 a second) is stale: a settings write or a restart froze it.
  if (millis() - radar.last_frame > 300) { since = 0; return false; }
  // A desk project with the same module (github.com/slastra/deskpresence) ignores energies under 40 (its empty
  // room: single frames up to 38). No distance limit: a seated person's still target sometimes reads 30 cm, at
  // energy 100.
  bool moving = (radar.state & 1) && radar.move_en >= setting_int("radar_min_mv");
  bool still = (radar.state & 2) && radar.still_en >= setting_int("radar_min_st");
  if (!moving && !still) { since = 0; return false; }
  uint32_t now = millis();
  if (since == 0) since = now ? now : 1;
  return now - since >= (uint32_t) setting_int("radar_min_ms");
}
void radar_ask_version() { config(0x00A0); }
void radar_read_params() { config(0x0061); }
void radar_restart() { config(0x00A3); }
void radar_engineering(bool on) { config(on ? 0x0062 : 0x0063); }

void radar_apply_settings() {
  uint8_t v[18];
  put_word_dword(v, 0x0000, setting_int("radar_mg"));
  put_word_dword(v + 6, 0x0001, setting_int("radar_sg"));
  put_word_dword(v + 12, 0x0002, setting_int("radar_hold"));
  config(0x0060, v, sizeof(v));
  char km[12], ks[12];
  for (int g = 0; g < 9; g++) {
    snprintf(km, sizeof(km), "radar_m%d", g);
    snprintf(ks, sizeof(ks), "radar_s%d", g);
    put_word_dword(v, 0x0000, g);
    put_word_dword(v + 6, 0x0001, setting_int(km));
    put_word_dword(v + 12, 0x0002, setting_int(ks));
    config(0x0064, v, sizeof(v));
  }
  radar_read_params();
  Con.println("Radar: settings written to the sensor");
}

void radar_cal_start(uint32_t wait_s) {
  eng_before_cal = radar.engineering;
  if (!radar.engineering) radar_engineering(true);
  radar_cal.stage = RadarCal::Wait;
  radar_cal.until = millis() + constrain(wait_s, 5u, 600u) * 1000;
  Con.printf("Radar calibration: leave the room; sampling starts in %u s\n", (unsigned) constrain(wait_s, 5u, 600u));
}

void radar_cal_abort() {
  if (radar_cal.stage == RadarCal::Wait || radar_cal.stage == RadarCal::Sample) {
    radar_cal.stage = RadarCal::Idle;
    if (!eng_before_cal) radar_engineering(false);
    Con.println("Radar calibration aborted: thresholds unchanged");
  }
}

bool radar_command(const String &c, String &reply) {
  if (c.length() < 2 || c[0] != 'r') return false;
  char r = c[1];
  if (r == 'v') { radar_ask_version(); reply = "radar: version asked"; }
  else if (r == 'p') { radar_read_params(); reply = "radar: parameters asked"; }
  else if (r == 'r') { radar_restart(); reply = "radar: restart asked"; }
  else if (r == 'e') { radar_engineering(c.substring(2).toInt() != 0); reply = "radar: engineering mode asked"; }
  else if (r == 'a') { radar_apply_settings(); reply = "radar: settings applied"; }
  else if (r == 'c') { radar_cal_start(c.length() > 2 ? c.substring(2).toInt() : 30); reply = "radar: calibration started"; }
  else return false;
  return true;
}
