// WiFi and the web portal.
//
// House WiFi from NVS (namespace "net"), else the built-in default (secrets.h). With none, or the house network lost
// for 30 s, the node raises its own access point "Bench-Node-Setup" at http://192.168.4.1/ while it keeps trying.
// The page and every API need the portal account (HTTP Basic, secrets.h). The API is in the Firmware Gospel
// (docs/firmware-gospel.md, the web page).
// Handlers run on the loop() task (handleClient() is called from loop()), so they may touch the core directly.
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "console.h"
#include "json.h"
#include "portal.h"
#include "settings.h"
#include "page_gz.h"
#include "secrets.h"

// The network name, from secrets.h; "bench-node" when MDNS_NAME is missing or left empty.
#ifdef MDNS_NAME
static constexpr char MDNS_SET[] = MDNS_NAME;
#else
static constexpr char MDNS_SET[] = "";
#endif
static constexpr const char *HOSTNAME = MDNS_SET[0] ? MDNS_SET : "bench-node";
static constexpr char AP_SSID[] = "Bench-Node-Setup";
static constexpr uint32_t STA_GIVE_UP_MS = 30000;     // house network lost this long: raise the access point
static constexpr uint32_t AP_LINGER_MS = 60000;       // back home this long: drop the access point
static constexpr uint32_t OTA_STALL_MS = 15000;
static bool ota_on_trial = false;

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
  String o;
  o.reserve(5200);
  o += '{';
  ks(o, "mode", portal_mode());
  ks(o, "ip", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "");
  ks(o, "ssid", sta_ssid.c_str());
  kv(o, "rssi", WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  kv(o, "served", served);
  build_state(o);                                    // main.cpp: the device's live state
  if (server.hasArg("log")) {
    static char buf[3073];
    uint32_t next;
    uint32_t from = Con.copySince(strtoul(server.arg("log").c_str(), nullptr, 10), buf, sizeof(buf) - 1, next);
    kopen(o, "log"); kv(o, "from", from); kv(o, "next", next); ks(o, "t", buf); end_obj(o); o += ',';
  }
  end_obj(o);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", o);
}

static void h_schema() {
  if (!auth()) return;
  String o;
  o.reserve(9000);
  o += '{';
  ks(o, "fw", firmware_version());
  o += "\"tabs\":[";
  for (size_t i = 0; i < SETTING_TABS_COUNT; i++) { if (i) o += ','; jesc(o, SETTING_TABS[i]); }
  o += "],\"rows\":[";
  for (size_t i = 0; i < SETTINGS_COUNT; i++) {
    const SettingRow &r = SETTINGS[i];
    if (i) o += ',';
    o += '{';
    ks(o, "k", r.key); ks(o, "l", r.label); ks(o, "u", r.unit); ks(o, "t", r.tab); kv(o, "kind", r.kind);
    kv(o, "lo", r.lo); kv(o, "hi", r.hi); kv(o, "st", r.step); kv(o, "def", r.def);
    if (r.choices) {
      o += "\"ch\":[";
      String c = r.choices;
      int start = 0, n = 0;
      while (start <= (int) c.length()) {
        int bar = c.indexOf('|', start);
        if (bar < 0) bar = c.length();
        if (n++) o += ',';
        jesc(o, c.substring(start, bar).c_str());
        start = bar + 1;
      }
      o += "],";
    }
    if (r.help) ks(o, "help", r.help);
    end_obj(o);
  }
  o += "],\"acts\":[";
  for (size_t i = 0; i < SETTING_ACTIONS_COUNT; i++) {
    const SettingAction &a = SETTING_ACTIONS[i];
    if (i) o += ',';
    o += '{';
    ks(o, "t", a.tab); ks(o, "l", a.label); ks(o, "a", a.action); ks(o, "v", a.value);
    end_obj(o);
  }
  o += "]}";
  server.send(200, "application/json", o);
}

static void h_values() {
  if (!auth()) return;
  String o = "{";
  for (size_t i = 0; i < SETTINGS_COUNT; i++) {
    const SettingRow &r = SETTINGS[i];
    if (r.kind == K_PASS) {
      String k = String(r.key) + "_set";
      kv(o, k.c_str(), *setting_text(r.key) ? 1 : 0);
      ks(o, r.key, "");
    } else if (r.kind == K_TEXT) ks(o, r.key, setting_text(r.key));
    else kv(o, r.key, setting_int(r.key));
  }
  end_obj(o);
  server.send(200, "application/json", o);
}

static void h_set() {
  if (!auth()) return;
  String k = server.arg("k"), v = server.arg("v"), stored, why;
  bool clamped = false;
  if (!setting_set(k.c_str(), v, stored, clamped, why)) {
    String o = "{\"ok\":0,\"m\":";
    jesc(o, why.c_str());
    o += '}';
    server.send(200, "application/json", o);
    return;
  }
  Con.printf("Setting %s = %s%s\n", k.c_str(), setting_row(k.c_str())->kind == K_PASS ? "(hidden)" : stored.c_str(),
             clamped ? " (clamped)" : "");
  settings_changed(k.c_str());                       // main.cpp applies it at once
  String o = "{\"ok\":1,\"v\":";
  const SettingRow *r = setting_row(k.c_str());
  if (r->kind == K_TEXT || r->kind == K_PASS) jesc(o, stored.c_str()); else o += stored;
  if (clamped) o += ",\"clamped\":1";
  o += '}';
  server.send(200, "application/json", o);
}

static void h_act() {
  if (!auth()) return;
  String reply;
  bool ok = do_action(server.arg("a"), server.arg("v"), reply);
  Con.printf("%8u  web action %s %s -> %s\n", (unsigned) millis(), server.arg("a").c_str(), server.arg("v").c_str(),
             reply.c_str());
  String o = "{\"ok\":";
  o += ok ? 1 : 0;
  o += ",\"m\":";
  jesc(o, reply.c_str());
  o += '}';
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
  server.send(200, "application/json",
              String("{\"ok\":1,\"m\":\"saved - rebooting to join it; find it at http://") + HOSTNAME + ".local/\"}");
  reboot_at = millis() + 1000;
}

static void h_reboot() {
  if (!auth()) return;
  server.send(200, "application/json", "{\"ok\":1,\"m\":\"rebooting\"}");
  reboot_at = millis() + 1000;
}

static void h_ota_done() {
  if (!auth()) return;
  if (ota_on_trial) {
    ota_on_trial = false;
    server.send(409, "application/json",
                "{\"ok\":0,\"m\":\"the running firmware is still on trial: wait a minute after its start\"}");
    return;
  }
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
    if (firmware_on_trial()) {                                     // the slot to write holds the way back
      ota_failed = ota_on_trial = true;
      Con.println("Firmware upload refused: the running firmware is still on trial");
      return;
    }
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
  server.on("/api/schema", HTTP_GET, h_schema);
  server.on("/api/values", HTTP_GET, h_values);
  server.on("/api/set", HTTP_POST, h_set);
  server.on("/api/act", HTTP_POST, h_act);
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
