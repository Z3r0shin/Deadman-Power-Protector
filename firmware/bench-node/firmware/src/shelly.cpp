#include "shelly.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include "console.h"
#include "secrets.h"
#include "settings.h"

using bench::Answer;
using bench::Op;

struct Request { Op op; uint32_t lease_s, ceil_s, seq; };
struct Result { Op op; bool got; Answer a; uint32_t seq; };

static QueueHandle_t requests, results;
static SemaphoreHandle_t lock;
static ShellyReading reading;
static constexpr uint32_t TIMEOUT_MS = 3000;

const char *op_name(Op op) {
  switch (op) {
    case Op::Status: return "status";
    case Op::Renew: return "renew";
    case Op::OnPress: return "on (press)";
    case Op::OnRemote: return "on (remote)";
    case Op::Off: return "off";
    default: return "none";
  }
}

static String host() {
  const char *h = setting_text("shelly_host");
  return String(*h ? h : SHELLY_HOST);
}

static void set_error(const char *fmt, const char *arg) {
  xSemaphoreTake(lock, portMAX_DELAY);
  snprintf(reading.error, sizeof(reading.error), fmt, arg);
  xSemaphoreGive(lock);
}

static bool get(const String &url, int &code, String &body) {
  HTTPClient http;
  http.setTimeout(TIMEOUT_MS);
  http.setConnectTimeout(TIMEOUT_MS);
  if (!http.begin(url)) { code = -1; return false; }
  code = http.GET();
  body = code > 0 ? http.getString() : String();
  http.end();
  return code == 200;
}

// Value after "key": in a flat JSON answer. Returns false when the key is absent.
static bool num(const String &s, const char *key, float &out) {
  String k = String("\"") + key + "\":";
  int i = s.indexOf(k);
  if (i < 0) return false;
  const char *p = s.c_str() + i + k.length();
  while (*p == ' ') p++;
  if (strncmp(p, "null", 4) == 0) { out = NAN; return true; }
  char *end;
  out = strtof(p, &end);
  return end != p;
}
static bool flag(const String &s, const char *key, bool &out) {
  String k = String("\"") + key + "\":";
  int i = s.indexOf(k);
  if (i < 0) return false;
  const char *p = s.c_str() + i + k.length();
  while (*p == ' ') p++;
  if (strncmp(p, "true", 4) == 0) { out = true; return true; }
  if (strncmp(p, "false", 5) == 0) { out = false; return true; }
  return false;
}

static int find_script() {
  int code;
  String body;
  if (!get("http://" + host() + "/rpc/Script.List", code, body)) return -1;
  // {"scripts":[{"id":1,"name":"lease","enable":true,"running":true},...]}
  int n = body.indexOf("\"name\":\"lease\"");
  if (n < 0) return -1;
  int idpos = body.lastIndexOf("\"id\":", n);
  if (idpos < 0) return -1;
  return body.substring(idpos + 5).toInt();
}

static Result run(const Request &r) {
  Result res{r.op, false, Answer(), r.seq};
  if (WiFi.status() != WL_CONNECTED) { set_error("%s", "no house WiFi"); return res; }
  int id;
  xSemaphoreTake(lock, portMAX_DELAY);
  id = reading.script_id;
  xSemaphoreGive(lock);
  if (id < 0) {
    id = find_script();
    if (id < 0) { set_error("%s", "the lease script is not on the Shelly (or it is unreachable)"); return res; }
    xSemaphoreTake(lock, portMAX_DELAY);
    reading.script_id = id;
    xSemaphoreGive(lock);
    Con.printf("Shelly: lease script found, id %d\n", id);
  }
  String q;
  switch (r.op) {
    case Op::Status: q = "op=status"; break;
    case Op::Renew: q = "op=renew&l=" + String(r.lease_s); break;
    case Op::OnPress: q = "op=on&press=1&l=" + String(r.lease_s) + "&c=" + String(r.ceil_s); break;
    case Op::OnRemote: q = "op=on&press=0&l=" + String(r.lease_s) + "&c=" + String(r.ceil_s); break;
    case Op::Off: q = "op=off"; break;
    default: return res;
  }
  int code;
  String body;
  bool ok = get("http://" + host() + "/script/" + String(id) + "/lease?" + q, code, body);
  if (!ok) {
    char e[48];
    if (code > 0) snprintf(e, sizeof(e), "HTTP %d", code);
    else snprintf(e, sizeof(e), "no answer (%s)", HTTPClient::errorToString(code).c_str());
    set_error("%s", e);
    if (code == 404) { xSemaphoreTake(lock, portMAX_DELAY); reading.script_id = -1; xSemaphoreGive(lock); }
    return res;
  }
  Answer &a = res.a;
  float f;
  bool good = flag(body, "ok", a.ok) && flag(body, "on", a.on);
  if (num(body, "lease_s", f) && f >= 0) a.lease_s = (uint32_t) f;
  if (num(body, "ceil_s", f) && f >= 0) a.ceil_s = (uint32_t) f;
  a.ceiling = body.indexOf("\"why\":\"ceiling\"") >= 0;
  if (!good) { set_error("%s", "an answer that is not the lease contract"); return res; }
  res.got = true;
  xSemaphoreTake(lock, portMAX_DELAY);
  reading.error[0] = 0;
  if (num(body, "w", f)) reading.w = f;
  if (num(body, "v", f)) reading.v = f;
  if (num(body, "a", f)) reading.a = f;
  if (num(body, "t", f)) reading.t = f;
  int vi = body.indexOf("\"ver\":\"");
  if (vi >= 0) snprintf(reading.ver, sizeof(reading.ver), "%s", body.substring(vi + 7, body.indexOf('"', vi + 7)).c_str());
  xSemaphoreGive(lock);
  if (r.op != Op::Status && r.op != Op::Renew)
    Con.printf("%8u  Shelly %s -> %s\n", (unsigned) millis(), op_name(r.op), body.c_str());
  return res;
}

static void task(void *) {
  Request r;
  for (;;) {
    if (xQueueReceive(requests, &r, portMAX_DELAY) == pdTRUE) {
      Result res = run(r);
      xQueueSend(results, &res, portMAX_DELAY);
    }
  }
}

void shelly_begin() {
  lock = xSemaphoreCreateMutex();
  requests = xQueueCreate(2, sizeof(Request));
  results = xQueueCreate(2, sizeof(Result));
  xTaskCreatePinnedToCore(task, "shelly", 6144, nullptr, 1, nullptr, 0);
}

void shelly_send(Op op, uint32_t lease_s, uint32_t ceil_s, uint32_t seq) {
  Request r{op, lease_s, ceil_s, seq};
  if (xQueueSend(requests, &r, 0) != pdTRUE) Con.printf("%8u  Shelly: link task busy, request dropped\n", (unsigned) millis());
}

bool shelly_take_result(Op &op, bool &got, Answer &a, uint32_t &seq) {
  Result r;
  if (xQueueReceive(results, &r, 0) != pdTRUE) return false;
  op = r.op;
  got = r.got;
  a = r.a;
  seq = r.seq;
  return true;
}

void shelly_reading(ShellyReading &out) {
  xSemaphoreTake(lock, portMAX_DELAY);
  out = reading;
  xSemaphoreGive(lock);
}
