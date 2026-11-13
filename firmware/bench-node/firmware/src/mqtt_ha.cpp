// MQTT / Home Assistant module, over IDF's esp-mqtt client (bundled in the Arduino core, nothing to install).
// The event callback runs in the MQTT task and only sets flags / copies short payloads under lock. Everything
// else -- discovery, state publishing, ha_snapshot()/ha_command() -- runs in mqtt_loop() on the loop task.
#include "ha_iface.h"
#include "console.h"
#include "frigate.h"
#include "mqtt_client.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <WiFi.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

namespace {

constexpr const char *AVAIL_TOPIC = "bench_node/status";
constexpr const char *STATE_TOPIC = "bench_node/state";
constexpr const char *CMD_TOPIC = "bench_node/bar/set";
constexpr uint32_t STATE_PERIOD_MS = 10000;

enum class ModState : uint8_t { Disabled, Connecting, Up, Error };

// Data handed out of the MQTT task by the event handler, under lock. Kept tiny: no String, no allocation.
struct EventBox {
  portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
  volatile bool connected = false;
  volatile bool disconnected = false;
  volatile bool error = false;
  volatile bool cmd_pending = false;
  char error_text[48] = {0};
  char cmd_buf[8] = {0};
};

// One row per HA entity. extra holds any discovery fields beyond the common ones, with its own trailing comma.
// val_tpl is Jinja read from the bench_node/state JSON; it only ever uses single quotes so it needs no escaping.
struct DiscEnt {
  const char *component;
  const char *object;
  const char *name;
  const char *extra;
  const char *val_tpl;
};

const DiscEnt kEntities[] = {
    {"switch", "bar", "Bar", "\"cmd_t\":\"bench_node/bar/set\",",
     "{{ 'ON' if value_json.shelly_on == 1 else 'OFF' }}"},
    {"binary_sensor", "bar_sense", "Bar sense", "\"dev_cla\":\"power\",",
     "{{ 'ON' if value_json.sense_live else 'OFF' }}"},
    {"binary_sensor", "presence", "Presence", "\"dev_cla\":\"occupancy\",",
     "{{ 'ON' if value_json.seen else 'OFF' }}"},
    {"binary_sensor", "alarm", "Alarm", "\"dev_cla\":\"problem\",",
     "{{ 'ON' if value_json.alarm else 'OFF' }}"},
    {"sensor", "phase", "Phase", "", "{{ value_json.phase }}"},
    {"sensor", "power", "Power", "\"unit_of_meas\":\"W\",\"dev_cla\":\"power\",\"stat_cla\":\"measurement\",",
     "{{ value_json.watts }}"},
    {"sensor", "away_due", "Absence cut in", "\"unit_of_meas\":\"min\",",
     "{{ ((value_json.away_due_s / 60) | round(1)) if value_json.away_due_s >= 0 else none }}"},
    {"sensor", "hold_left", "Hold left", "\"unit_of_meas\":\"min\",",
     "{{ (value_json.hold_left_s / 60) | round(1) }}"},
    {"sensor", "ceil_left", "Ceiling left", "\"unit_of_meas\":\"min\",",
     "{{ (value_json.ceil_left_s / 60) | round(1) }}"},
    {"sensor", "warn_left", "Warning left", "\"unit_of_meas\":\"s\",", "{{ value_json.warn_left_s }}"},
    {"sensor", "camera", "Camera check", "", "{{ value_json.camera }}"},
    {"sensor", "camera_owner_ago", "Owner last seen by the camera", "\"unit_of_meas\":\"s\",",
     "{{ value_json.camera_owner_ago_s if value_json.camera_owner_ago_s >= 0 else none }}"},
    {"sensor", "shelly_temp", "Shelly temperature",
     "\"unit_of_meas\":\"\\u00b0C\",\"dev_cla\":\"temperature\",", "{{ value_json.shelly_temp_c }}"},
};

// Safe copy of the fields that force an immediate state publish on change. HaSnapshot::phase is only a pointer,
// and main.cpp may reuse one buffer across calls -- so the "last published" record must hold real copies, never
// the pointer itself, or a change could silently compare a buffer against itself.
struct LastPub {
  char phase[12];
  char camera[12];
  int shelly_on;
  bool sense_live, seen, alarm;
};

EventBox g_evt;
esp_mqtt_client_handle_t g_client = nullptr;
ModState g_state = ModState::Disabled;
ModState g_prev_state = ModState::Disabled;
char g_err[48] = "";
char g_id[16] = "";  // device_mac(), lowercase, no colons
bool g_pending_start = false;
bool g_announced = false;  // subscribe + discovery + "online" done for the current connection
bool g_have_last = false;
LastPub g_last{};
uint32_t g_last_pub_ms = 0;

// ---- the camera (Frigate) ----
// Messages arrive in the MQTT task, in pieces when long (frigate/stats is several KB): they are put back together
// in g_rx, parsed there, and applied to the tracker under g_frg_mx. The loop task reads the tracker under the
// same mutex. A message longer than g_rx is dropped.
frigate::Tracker g_frg;
SemaphoreHandle_t g_frg_mx = nullptr;
char g_rx[16384];
char g_rx_topic[96];
bool g_rx_want = false;

bool frg_lock() { return g_frg_mx != nullptr && xSemaphoreTake(g_frg_mx, pdMS_TO_TICKS(50)) == pdTRUE; }
void frg_unlock() { xSemaphoreGive(g_frg_mx); }

// frigate/<camera>/status/detect, from the camera name in the local copy of the settings (no shared buffer).
bool is_detect_topic(const char *topic, const char *camera) {
  size_t cl = strlen(camera);
  return strncmp(topic, "frigate/", 8) == 0 && strncmp(topic + 8, camera, cl) == 0 &&
         strcmp(topic + 8 + cl, "/status/detect") == 0;
}

// Runs in the MQTT task, on one whole message.
void frigate_message(const char *topic, const char *data, size_t len) {
  if (!frg_lock()) return;
  frigate::Config c = g_frg.cfg;
  frg_unlock();
  uint32_t now = millis();
  if (strcmp(topic, "frigate/events") == 0) {
    frigate::Event e = frigate::parse_event(data, len, c);
    if (e.ok && frg_lock()) { g_frg.event(e, now); frg_unlock(); }
  } else if (strcmp(topic, "frigate/stats") == 0) {
    double fps = 0;
    bool dets = true;
    bool found = frigate::parse_stats_fps(data, len, c, fps, &dets);
    if (frg_lock()) { g_frg.stats(found, fps, now, dets); frg_unlock(); }
  } else if (strcmp(topic, "frigate/available") == 0) {
    bool online = len == 6 && memcmp(data, "online", 6) == 0;
    if (frg_lock()) { g_frg.available(online); frg_unlock(); }
  } else if (is_detect_topic(topic, c.camera)) {
    bool online = len == 6 && memcmp(data, "online", 6) == 0;
    if (frg_lock()) { g_frg.detect(online, now); frg_unlock(); }
  }
}

// Puts a message's pieces back together (MQTT task).
void frigate_piece(const esp_mqtt_event_handle_t event) {
  if (event->current_data_offset == 0) {
    g_rx_want = false;
    if (event->topic != nullptr && event->topic_len > 8 && event->topic_len < (int) sizeof(g_rx_topic) &&
        memcmp(event->topic, "frigate/", 8) == 0 && event->total_data_len >= 0 &&
        event->total_data_len < (int) sizeof(g_rx)) {
      memcpy(g_rx_topic, event->topic, event->topic_len);
      g_rx_topic[event->topic_len] = '\0';
      g_rx_want = true;
    }
  }
  if (!g_rx_want || event->current_data_offset < 0 || event->data_len < 0) return;
  size_t off = (size_t) event->current_data_offset, n = (size_t) event->data_len, total = (size_t) event->total_data_len;
  if (off + n > total || total >= sizeof(g_rx)) { g_rx_want = false; return; }
  memcpy(g_rx + off, event->data, n);
  if (off + n == total) {
    g_rx[total] = '\0';
    frigate_message(g_rx_topic, g_rx, total);
    g_rx_want = false;
  }
}

void ensure_id() {
  if (g_id[0] != '\0') return;
  const char *mac = device_mac();
  size_t j = 0;
  for (size_t i = 0; mac[i] != '\0' && j < sizeof(g_id) - 1; i++) {
    if (mac[i] == ':') continue;
    g_id[j++] = (char) tolower((unsigned char) mac[i]);
  }
  g_id[j] = '\0';
}

void fmt_float(char *buf, size_t n, float v) {
  if (isnan(v)) snprintf(buf, n, "null");
  else snprintf(buf, n, "%.1f", (double) v);
}

// Runs in the MQTT task. Only ever sets flags or copies a few bytes under lock -- no publish, no ha_*, no
// Con.printf, no String, no allocation.
void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
  (void) arg;
  (void) base;
  auto *event = (esp_mqtt_event_handle_t) event_data;

  switch ((esp_mqtt_event_id_t) event_id) {
    case MQTT_EVENT_CONNECTED:
      portENTER_CRITICAL(&g_evt.lock);
      g_evt.connected = true;
      portEXIT_CRITICAL(&g_evt.lock);
      break;
    case MQTT_EVENT_DISCONNECTED:
      portENTER_CRITICAL(&g_evt.lock);
      g_evt.disconnected = true;
      portEXIT_CRITICAL(&g_evt.lock);
      break;
    case MQTT_EVENT_ERROR: {
      char text[sizeof(g_evt.error_text)];
      esp_mqtt_error_codes_t *e = event->error_handle;
      if (e == nullptr) {
        snprintf(text, sizeof(text), "error");
      } else if (e->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        snprintf(text, sizeof(text), "refused rc=%d", (int) e->connect_return_code);
      } else {
        snprintf(text, sizeof(text), "transport err=0x%x sock=%d", (int) e->esp_tls_last_esp_err,
                 e->esp_transport_sock_errno);
      }
      portENTER_CRITICAL(&g_evt.lock);
      memcpy(g_evt.error_text, text, sizeof(g_evt.error_text));
      g_evt.error = true;
      portEXIT_CRITICAL(&g_evt.lock);
      break;
    }
    case MQTT_EVENT_DATA:
      if (event->topic != nullptr && event->current_data_offset == 0 &&
          event->topic_len == (int) strlen(CMD_TOPIC) && memcmp(event->topic, CMD_TOPIC, event->topic_len) == 0) {
        size_t n = (size_t) event->data_len;
        if (n > sizeof(g_evt.cmd_buf) - 1) n = sizeof(g_evt.cmd_buf) - 1;
        portENTER_CRITICAL(&g_evt.lock);
        memcpy(g_evt.cmd_buf, event->data, n);
        g_evt.cmd_buf[n] = '\0';
        g_evt.cmd_pending = true;
        portEXIT_CRITICAL(&g_evt.lock);
      }
      frigate_piece(event);
      break;
    default:
      break;
  }
}

// What mqtt_loop() needs out of the event box for one pass, copied out under lock in one go.
struct Drained {
  bool connected, disconnected, error, got_cmd;
  char err[sizeof(EventBox::error_text)];
  char cmd[sizeof(EventBox::cmd_buf)];
};

Drained drain_events() {
  Drained d{};
  portENTER_CRITICAL(&g_evt.lock);
  d.connected = g_evt.connected; g_evt.connected = false;
  d.disconnected = g_evt.disconnected; g_evt.disconnected = false;
  d.error = g_evt.error; g_evt.error = false;
  if (d.error) memcpy(d.err, g_evt.error_text, sizeof(d.err));
  d.got_cmd = g_evt.cmd_pending; g_evt.cmd_pending = false;
  if (d.got_cmd) memcpy(d.cmd, g_evt.cmd_buf, sizeof(d.cmd));
  portEXIT_CRITICAL(&g_evt.lock);
  return d;
}

void start_if_wifi_ready() {
  if (g_client != nullptr && g_pending_start && WiFi.status() == WL_CONNECTED) {
    esp_mqtt_client_start(g_client);
    g_pending_start = false;
  }
}

// Shared by mqtt_begin() and mqtt_restart(): decide disabled/enabled from the current settings and, if enabled,
// create (but not necessarily start -- see start_if_wifi_ready()) a fresh client.
void setup_client() {
  int32_t on = setting_int("mqtt_on");
  const char *host = setting_text("mqtt_host");
  if (on == 0 || host == nullptr || host[0] == '\0') {
    g_client = nullptr;
    g_state = ModState::Disabled;
    return;
  }

  ensure_id();
  int32_t port = setting_int("mqtt_port");
  if (port <= 0) port = 1883;
  const char *user = setting_text("mqtt_user");
  const char *pass = setting_text("mqtt_pass");

  char client_id[24];
  snprintf(client_id, sizeof(client_id), "bn-%s", g_id);

  esp_mqtt_client_config_t cfg = {};
  cfg.host = host;
  cfg.port = (uint32_t) port;
  cfg.client_id = client_id;
  cfg.username = (user != nullptr && user[0] != '\0') ? user : nullptr;
  cfg.password = (pass != nullptr && pass[0] != '\0') ? pass : nullptr;
  cfg.lwt_topic = AVAIL_TOPIC;
  cfg.lwt_msg = "offline";
  cfg.lwt_qos = 1;
  cfg.lwt_retain = 1;
  cfg.keepalive = 30;

  g_client = esp_mqtt_client_init(&cfg);
  if (g_client == nullptr) {
    g_state = ModState::Error;
    snprintf(g_err, sizeof(g_err), "init failed");
    return;
  }
  esp_mqtt_client_register_event(g_client, MQTT_EVENT_ANY, mqtt_event_handler, nullptr);
  g_announced = false;
  g_have_last = false;
  g_err[0] = '\0';
  g_state = ModState::Connecting;
  g_pending_start = true;
  start_if_wifi_ready();  // may start right away if WiFi is already up
}

void publish_discovery() {
  for (const DiscEnt &e : kEntities) {
    char topic[96];
    snprintf(topic, sizeof(topic), "homeassistant/%s/%s_%s/config", e.component, g_id, e.object);
    char payload[640];
    snprintf(payload, sizeof(payload),
             "{\"name\":\"%s\",\"uniq_id\":\"%s_%s\",\"stat_t\":\"%s\","
             "\"val_tpl\":\"%s\",\"avty_t\":\"%s\",%s"
             "\"dev\":{\"identifiers\":[\"%s\"],\"name\":\"Bench node\",\"model\":\"ESP32-S3 bench node\","
             "\"sw_version\":\"%s\"}}",
             e.name, g_id, e.object, STATE_TOPIC, e.val_tpl, AVAIL_TOPIC, e.extra, g_id, firmware_version());
    esp_mqtt_client_publish(g_client, topic, payload, 0, 1, 1);
  }
}

void publish_state(const HaSnapshot &s) {
  char w[16], t[16];
  fmt_float(w, sizeof(w), s.watts);
  fmt_float(t, sizeof(t), s.shelly_temp_c);
  char payload[400];
  snprintf(payload, sizeof(payload),
           "{\"phase\":\"%s\",\"shelly_on\":%d,\"sense_live\":%s,\"seen\":%s,\"alarm\":%s,"
           "\"watts\":%s,\"shelly_temp_c\":%s,\"away_due_s\":%d,\"hold_left_s\":%d,"
           "\"ceil_left_s\":%d,\"warn_left_s\":%d,\"camera\":\"%s\",\"camera_owner_ago_s\":%d}",
           s.phase, s.shelly_on, s.sense_live ? "true" : "false", s.seen ? "true" : "false",
           s.alarm ? "true" : "false", w, t, s.away_due_s, s.hold_left_s, s.ceil_left_s, s.warn_left_s, s.camera,
           s.camera_owner_ago_s);
  esp_mqtt_client_publish(g_client, STATE_TOPIC, payload, 0, 0, 1);
}

bool changed_from_last(const HaSnapshot &s) {
  if (!g_have_last) return true;
  return strcmp(s.phase, g_last.phase) != 0 || s.shelly_on != g_last.shelly_on ||
         s.sense_live != g_last.sense_live || s.seen != g_last.seen || s.alarm != g_last.alarm ||
         strcmp(s.camera, g_last.camera) != 0;
}

void remember(const HaSnapshot &s) {
  snprintf(g_last.phase, sizeof(g_last.phase), "%s", s.phase);
  snprintf(g_last.camera, sizeof(g_last.camera), "%s", s.camera);
  g_last.shelly_on = s.shelly_on;
  g_last.sense_live = s.sense_live;
  g_last.seen = s.seen;
  g_last.alarm = s.alarm;
  g_have_last = true;
}

}  // namespace

void mqtt_begin() {
  if (g_frg_mx == nullptr) g_frg_mx = xSemaphoreCreateMutex();
  frigate_apply_settings();
  setup_client();
}

void frigate_apply_settings() {
  frigate::Config c;
  auto text = [](char *dst, size_t n, const char *key, const char *def) {
    const char *v = setting_text(key);
    snprintf(dst, n, "%s", (v != nullptr && v[0] != '\0') ? v : def);
  };
  text(c.camera, sizeof(c.camera), "frg_cam", "bench");
  text(c.zone, sizeof(c.zone), "frg_zone", "bench_seat");
  text(c.model, sizeof(c.model), "frg_model", "Bench_person");
  text(c.cls, sizeof(c.cls), "frg_class", "owner");
  text(c.face, sizeof(c.face), "frg_face", "");
  if (g_frg_mx == nullptr || xSemaphoreTake(g_frg_mx, portMAX_DELAY) != pdTRUE) return;   // never drop a change
  bool cam_changed = strcmp(c.camera, g_frg.cfg.camera) != 0;
  g_frg.cfg = c;
  g_frg.enabled = setting_int("frg_on") != 0;
  g_frg.night_off = setting_int("frg_night") != 0;
  g_frg.veto_ms = (uint32_t) setting_int("frg_veto_s") * 1000;
  g_frg.dead_ms = (uint32_t) setting_int("frg_dead_s") * 1000;
  g_frg.seat_ms = (uint32_t) setting_int("frg_seat_s") * 1000;
  g_frg.empty_ms = (uint32_t) setting_int("frg_empty_s") * 1000;
  if (cam_changed) g_frg.link_lost();          // the heartbeat and stats seen so far were another camera's
  frg_unlock();
}

bool frigate_veto(bool quiet) {
  if (!frg_lock()) return false;               // cannot tell: radar only
  uint32_t now = millis();
  g_frg.tick(now, quiet);
  bool v = g_frg.veto(now);
  frg_unlock();
  return v;
}

void frigate_status(FrigateStatus &out) {
  out = FrigateStatus{};
  if (!frg_lock()) return;
  uint32_t now = millis();
  out.on = g_frg.enabled;
  out.live = g_frg.live();
  out.veto = g_frg.veto(now);
  out.unsure = g_frg.unsure();
  out.owner_ago_s = g_frg.live() && g_frg.seen() ? (int) g_frg.owner_ago_s(now) : -1;   // -1: not seen yet
  out.tracks = g_frg.tracks();
  frg_unlock();
}

void mqtt_restart() {
  if (g_client != nullptr) {
    if (g_state == ModState::Up) esp_mqtt_client_publish(g_client, AVAIL_TOPIC, "offline", 0, 1, 1);
    esp_mqtt_client_stop(g_client);
    esp_mqtt_client_destroy(g_client);
    g_client = nullptr;
  }
  portENTER_CRITICAL(&g_evt.lock);
  g_evt.connected = g_evt.disconnected = g_evt.error = g_evt.cmd_pending = false;
  g_evt.error_text[0] = '\0';
  portEXIT_CRITICAL(&g_evt.lock);
  setup_client();
}

void mqtt_loop() {
  if (g_state == ModState::Disabled && g_client == nullptr) return;  // disabled: cheap, nothing to do

  Drained d = drain_events();
  if (d.disconnected) {
    g_state = ModState::Connecting;
    g_announced = false;
    if (frg_lock()) { g_frg.link_lost(); frg_unlock(); }   // messages missed meanwhile: start over
  }
  if (d.error) { g_state = ModState::Error; snprintf(g_err, sizeof(g_err), "%s", d.err); }
  if (d.connected) { g_state = ModState::Up; g_err[0] = '\0'; }

  if (d.got_cmd) {
    if (strcmp(d.cmd, "ON") == 0) ha_command(HaCmd::BarOn);
    else if (strcmp(d.cmd, "OFF") == 0) ha_command(HaCmd::BarOff);
  }

  if (g_prev_state != g_state) {
    Con.printf("mqtt: %s%s%s\n", mqtt_state(), g_err[0] ? " " : "", g_err);
    g_prev_state = g_state;
  }

  start_if_wifi_ready();

  if (g_state != ModState::Up) return;

  if (!g_announced) {
    esp_mqtt_client_subscribe(g_client, CMD_TOPIC, 1);
    if (setting_int("frg_on")) {
      char det[96];
      const char *cam = setting_text("frg_cam");
      snprintf(det, sizeof(det), "frigate/%s/status/detect", (cam != nullptr && cam[0]) ? cam : "bench");
      esp_mqtt_client_subscribe(g_client, "frigate/available", 0);
      esp_mqtt_client_subscribe(g_client, "frigate/events", 0);
      esp_mqtt_client_subscribe(g_client, "frigate/stats", 0);
      esp_mqtt_client_subscribe(g_client, det, 0);
    }
    publish_discovery();
    esp_mqtt_client_publish(g_client, AVAIL_TOPIC, "online", 0, 1, 1);
    g_announced = true;
    g_have_last = false;
  }

  HaSnapshot cur;
  ha_snapshot(cur);
  uint32_t now = millis();
  if (changed_from_last(cur) || (now - g_last_pub_ms) >= STATE_PERIOD_MS) {
    publish_state(cur);
    remember(cur);
    g_last_pub_ms = now;
  }
}

const char *mqtt_state() {
  switch (g_state) {
    case ModState::Disabled: return "off";
    case ModState::Connecting: return "connecting";
    case ModState::Up: return "up";
    case ModState::Error: return "error";
  }
  return "off";
}

const char *mqtt_error() { return g_err; }
