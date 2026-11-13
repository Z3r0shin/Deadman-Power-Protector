// WiFi and the web portal for the hardware test.
//
// House WiFi from NVS (namespace "net"); with none stored, or the house network lost for 30 s, the node raises
// its own access point "Bench-Node-Setup" and serves the same page at http://192.168.4.1/ (a captive portal:
// phones open it by themselves). The page and every API need the portal account (HTTP Basic, secrets.h).
//
// Endpoints (all GET unless stated):
//   /                 the page, gzipped from flash (page_gz.h, built from data/portal.html), ETag + 304
//   /api/state?log=N  everything the node knows, plus console bytes from N (at most 3 kB)
//   POST /api/cmd     c=<command line>: the same dispatcher as the USB console; answers {"ok":..,"m":..}
//   /api/net          {"ssid":..}; POST ssid, pass (blank = keep): store, reboot 1 s later
//   POST /api/ota     multipart firmware image (field f): write the other slot, reboot
//   POST /api/reboot  reboot 1 s later
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "app.h"
#include "console.h"
#include "page_gz.h"
#include "secrets.h"

static constexpr char HOSTNAME[] = "bench-node";
static constexpr char AP_SSID[] = "Bench-Node-Setup";
static constexpr uint32_t STA_GIVE_UP_MS = 30000;     // house network lost this long: raise the access point
static constexpr uint32_t AP_LINGER_MS = 60000;       // back home this long: drop the access point
static constexpr uint32_t OTA_STALL_MS = 15000;

static WebServer server(80);
static DNSServer dns;
static String sta_ssid, sta_pass;
static bool ap_up = false;
static uint32_t sta_lost_at = 0, sta_ok_since = 0, reboot_at = 0;
static bool ota_active = false, ota_failed = false;
static uint32_t ota_touch = 0, served = 0;
static char address[40] = "WiFi starting";

const char *portal_address() { return address; }
const char *portal_mode() {
  if (WiFi.status() == WL_CONNECTED) return ap_up ? "house WiFi (+ own access point)" : "house WiFi";
  return ap_up ? "own access point" : "joining";
}

static void start_ap() {
  WiFi.mode(sta_ssid.length() ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(AP_SSID, PORTAL_PASS);
  dns.start(53, "*", WiFi.softAPIP());
  ap_up = true;
  Con.printf("Own access point up: join \"%s\" (password = the portal password), open http://192.168.4.1/\n", AP_SSID);
}

static void stop_ap() {
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  ap_up = false;
  Con.println("Own access point closed: back on the house WiFi");
}

static bool auth() {
  if (server.authenticate(PORTAL_USER, PORTAL_PASS)) return true;
  server.requestAuthentication(BASIC_AUTH, "Bench node");
  return false;
}

// ---- JSON helpers ---------------------------------------------------------------------------------------
static void jesc(String &o, const char *s) {
  o += '"';
  for (; *s; s++) {
    char c = *s;
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if ((uint8_t) c < 0x20 || (uint8_t) c > 0x7E) continue;   // console is ASCII; drop the rest
    else o += c;
  }
  o += '"';
}

static void jarr(String &o, const uint8_t *v, int n) {
  o += '[';
  for (int i = 0; i < n; i++) { if (i) o += ','; o += v[i]; }
  o += ']';
}

static void kv(String &o, const char *k, long v) { o += '"'; o += k; o += "\":"; o += v; o += ','; }
// Floats: NaN or infinity is not JSON, and one bad number would stop the page from reading anything.
static void kf(String &o, const char *k, float v, int dec = 1) {
  o += '"'; o += k; o += "\":";
  if (isfinite(v)) o += String(v, dec); else o += "null";
  o += ',';
}
static void ks(String &o, const char *k, const char *v) { o += '"'; o += k; o += "\":"; jesc(o, v); o += ','; }

static void end_obj(String &o) {
  if (o.endsWith(",")) o.remove(o.length() - 1);
  o += '}';
}

// ---- Handlers -------------------------------------------------------------------------------------------
static void h_root() {
  if (!auth()) return;
  served++;
  if (server.header("If-None-Match").indexOf(PAGE_ETAG) >= 0) { server.send(304); return; }
  server.sendHeader("ETag", "\"" PAGE_ETAG "\"");
  server.sendHeader("Cache-Control", "no-cache, must-revalidate");
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html; charset=utf-8", (const char *) PAGE_GZ, PAGE_GZ_LEN);
}

static void h_state() {
  if (!auth()) return;
  served++;
  const uint32_t now = millis();
  const RadarState &r = app.radar;
  ShellyState sh;
  shelly_snapshot(sh);
  String o;
  o.reserve(4600);
  o += '{';
  kv(o, "up", now / 1000);
  kv(o, "heap", ESP.getFreeHeap());
  ks(o, "mode", portal_mode());
  ks(o, "ip", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "");
  ks(o, "ssid", sta_ssid.c_str());
  kv(o, "rssi", WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  ks(o, "mac", WiFi.macAddress().c_str());
  ks(o, "built", __DATE__ " " __TIME__);
  kv(o, "reset", (long) esp_reset_reason());
  kv(o, "served", served);
  o += "\"keys\":["; for (int k = 0; k < 3; k++) { if (k) o += ','; o += app.key_down[k] ? 1 : 0; } o += "],";
  o += "\"presses\":["; for (int k = 0; k < 3; k++) { if (k) o += ','; o += app.key_presses[k]; } o += "],";
  kv(o, "bar", app.bar_live);
  kv(o, "out", app.out_pin);
  kv(o, "pull", app.bar_pull);
  kv(o, "bgpio", app.bar_gpio);
  o += "\"probe\":["; o += app.probe_pulldown; o += ','; o += app.probe_pullup; o += "],";
  o += "\"led\":{"; kv(o, "mode", app.led_mode); o += "\"lv\":"; jarr(o, app.led_level, 3); o += ",\"man\":";
  jarr(o, app.led_manual, 3); o += "},";
  o += "\"buz\":{"; kv(o, "hz", app.buz_hz); kv(o, "vol", app.buz_vol); kv(o, "sweep", app.sweeping);
  kv(o, "swhz", app.sweep_hz); end_obj(o); o += ',';
  o += "\"oled\":{"; kv(o, "addr", app.oled_addr); kv(o, "mode", app.oled_mode); kv(o, "c", app.oled_contrast);
  end_obj(o); o += ',';
  ks(o, "i2c", app.i2c_found);
  o += "\"radar\":{";
  kv(o, "fps", r.fps); kv(o, "frames", r.frames); kv(o, "bad", r.bad);
  kv(o, "age", r.frames ? (long) (now - r.last_frame) : -1);
  kv(o, "st", r.state); kv(o, "md", r.move_cm); kv(o, "me", r.move_en); kv(o, "sd", r.still_cm);
  kv(o, "se", r.still_en); kv(o, "dd", r.detect_cm); kv(o, "eng", r.engineering); kv(o, "gates", r.gates);
  o += "\"gm\":"; jarr(o, r.gate_move, 9); o += ",\"gs\":"; jarr(o, r.gate_still, 9); o += ',';
  ks(o, "ver", r.version); kv(o, "acks", r.acks);
  o += "\"ack\":["; o += r.last_ack_cmd; o += ','; o += r.last_ack_status; o += "],";
  o += "\"par\":{"; kv(o, "ok", r.params_ok); kv(o, "mg", r.max_move_gate); kv(o, "sg", r.max_still_gate);
  kv(o, "hold", r.hold_s); o += "\"sm\":"; jarr(o, r.sens_move, 9); o += ",\"ss\":"; jarr(o, r.sens_still, 9);
  o += "}},";
  o += "\"shelly\":{";
  kv(o, "ok", sh.reachable); kv(o, "age", sh.polled_at ? (long) (now - sh.polled_at) : -1); kv(o, "on", sh.output);
  kf(o, "w", sh.apower); kf(o, "v", sh.voltage); kf(o, "a", sh.current, 3);
  kf(o, "t", sh.temp_c); kf(o, "hz", sh.freq); ks(o, "err", sh.error); ks(o, "last", sh.last_reply); end_obj(o);
  o += ',';
  if (server.hasArg("log")) {
    static char buf[3073];
    uint32_t next;
    uint32_t from = Con.copySince(strtoul(server.arg("log").c_str(), nullptr, 10), buf, sizeof(buf) - 1, next);
    o += "\"log\":{"; kv(o, "from", from); kv(o, "next", next); ks(o, "t", buf); end_obj(o); o += ',';
  }
  end_obj(o);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", o);
}

static void h_cmd() {
  if (!auth()) return;
  served++;
  String c = server.arg("c"), reply;
  if (c.length() == 0 || c.length() > 60) { server.send(400, "application/json", "{\"ok\":0,\"m\":\"1 to 60 characters\"}"); return; }
  Con.printf("%8u  > %s\n", (unsigned) millis(), c.c_str());
  bool ok = command(c, reply);
  if (reply.length()) Con.println(reply);
  String o = "{\"ok\":";
  o += ok ? 1 : 0;
  o += ",\"m\":";
  jesc(o, reply.c_str());
  o += '}';
  server.send(200, "application/json", o);
}

static void h_net_get() {
  if (!auth()) return;
  String o = "{\"ssid\":";
  jesc(o, sta_ssid.c_str());
  o += '}';
  server.send(200, "application/json", o);
}

static void h_net_post() {
  if (!auth()) return;
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  ssid.trim();
  if (ssid.isEmpty()) { server.send(200, "application/json", "{\"ok\":0,\"m\":\"the network name is empty\"}"); return; }
  Preferences p;
  p.begin("net", false);
  p.putString("ssid", ssid);
  if (pass.length()) p.putString("pass", pass);
  p.end();
  Con.printf("House WiFi set to \"%s\"; rebooting to join it\n", ssid.c_str());
  server.send(200, "application/json", "{\"ok\":1,\"m\":\"saved - rebooting to join it; find it at http://bench-node.local/\"}");
  reboot_at = millis() + 1000;
}

static void h_reboot() {
  if (!auth()) return;
  server.send(200, "application/json", "{\"ok\":1,\"m\":\"rebooting\"}");
  reboot_at = millis() + 1000;
}

static void h_ota_done() {
  if (!auth()) return;
  if (ota_failed || Update.hasError()) {
    server.send(500, "application/json", "{\"ok\":0,\"m\":\"update failed - the old firmware stays\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":1,\"m\":\"update written - rebooting into it\"}");
  reboot_at = millis() + 1000;
}

static void h_ota_chunk() {
  HTTPUpload &u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    ota_failed = !server.authenticate(PORTAL_USER, PORTAL_PASS);   // the upload callback runs before the handler
    if (ota_failed) return;
    Con.printf("Firmware upload: %s\n", u.filename.c_str());
    ota_active = true;
    ota_touch = millis();
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { ota_failed = true; Update.printError(Con); }
  } else if (u.status == UPLOAD_FILE_WRITE) {
    ota_touch = millis();
    if (!ota_failed && Update.write(u.buf, u.currentSize) != u.currentSize) { ota_failed = true; Update.printError(Con); }
  } else if (u.status == UPLOAD_FILE_END) {
    ota_active = false;
    if (!ota_failed && Update.end(true)) Con.printf("Firmware upload: %u bytes written\n", u.totalSize);
    else { ota_failed = true; Update.printError(Con); }
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    ota_active = false;
    ota_failed = true;
    Update.abort();
    Con.println("Firmware upload aborted");
  }
}

static void h_other() {
  if (ap_up && !server.hostHeader().startsWith("192.168.4.1")) {   // captive portal
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302);
    return;
  }
  server.sendHeader("Location", "/");
  server.send(302);
}

// ---- Begin and loop -------------------------------------------------------------------------------------
void portal_begin() {
  Preferences p;
  p.begin("net", true);
  sta_ssid = p.getString("ssid", "");
  sta_pass = p.getString("pass", "");
  p.end();
  if (sta_ssid.isEmpty()) { sta_ssid = WIFI_SSID; sta_pass = WIFI_PASS; }   // built-in default (secrets.h)

  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);
  if (sta_ssid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());
    sta_lost_at = millis();
    Con.printf("Joining house WiFi \"%s\"\n", sta_ssid.c_str());
  } else {
    Con.println("No house WiFi stored");
    start_ap();
  }
  WiFi.setSleep(false);

  server.on("/", HTTP_GET, h_root);
  server.on("/api/state", HTTP_GET, h_state);
  server.on("/api/cmd", HTTP_POST, h_cmd);
  server.on("/api/net", HTTP_GET, h_net_get);
  server.on("/api/net", HTTP_POST, h_net_post);
  server.on("/api/reboot", HTTP_POST, h_reboot);
  server.on("/api/ota", HTTP_POST, h_ota_done, h_ota_chunk);
  server.on("/favicon.ico", HTTP_GET, [] { server.send(204); });
  server.onNotFound(h_other);
  const char *headers[] = {"If-None-Match"};
  server.collectHeaders(headers, 1);
  server.begin();
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
}

void portal_loop() {
  const uint32_t now = millis();
  if (ap_up) dns.processNextRequest();
  server.handleClient();

  if (reboot_at && (int32_t) (now - reboot_at) >= 0) { Con.println("Rebooting"); delay(100); ESP.restart(); }

  if (ota_active && now - ota_touch > OTA_STALL_MS) {
    Update.abort();
    ota_active = false;
    ota_failed = true;
    Con.println("Firmware upload stalled for 15 s: abandoned");
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!sta_ok_since) {
      sta_ok_since = now;
      snprintf(address, sizeof(address), "%s", WiFi.localIP().toString().c_str());
      Con.printf("On house WiFi: http://%s/  or  http://%s.local/\n", address, HOSTNAME);
    }
    sta_lost_at = 0;
    if (ap_up && now - sta_ok_since > AP_LINGER_MS) stop_ap();
  } else {
    if (sta_ok_since) { sta_ok_since = 0; Con.println("House WiFi lost"); }
    if (!sta_lost_at) sta_lost_at = now;
    if (!ap_up && now - sta_lost_at > STA_GIVE_UP_MS) start_ap();
    if (ap_up) snprintf(address, sizeof(address), "AP %s 192.168.4.1", AP_SSID);
    else snprintf(address, sizeof(address), "joining WiFi...");
  }
}
