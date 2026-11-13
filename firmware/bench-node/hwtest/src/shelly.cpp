// Talks to the Shelly from its own task, so a slow or absent Shelly never stalls the keys or the radar.
// Polls Switch.GetStatus every 3 s; requests from the portal or the console are queued and sent in order.
#include <HTTPClient.h>
#include <WiFi.h>

#include "app.h"
#include "console.h"

ShellyState shelly;
static SemaphoreHandle_t lock;
static QueueHandle_t requests;
static char host[40];
static constexpr uint32_t POLL_MS = 3000;

struct Path { char p[96]; };

// Number after "key": in a JSON reply, or NAN.
static float num_after(const String &s, const char *key) {
  int i = s.indexOf(key);
  if (i < 0) return NAN;
  return strtof(s.c_str() + i + strlen(key), nullptr);
}

static void get(const char *path) {
  if (WiFi.status() != WL_CONNECTED) {
    xSemaphoreTake(lock, portMAX_DELAY);
    shelly.reachable = false;
    snprintf(shelly.error, sizeof(shelly.error), "no house WiFi");
    xSemaphoreGive(lock);
    return;
  }
  const uint32_t sent_at = millis();
  HTTPClient http;
  http.setTimeout(2500);
  http.setConnectTimeout(2500);
  String url = String("http://") + host + path;
  http.begin(url);
  int code = http.GET();
  String body = code > 0 ? http.getString() : String();
  http.end();

  bool status = strstr(path, "GetStatus") != nullptr;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (code == 200) {
    shelly.reachable = true;
    shelly.error[0] = 0;
    if (status) {
      shelly.polled_at = millis();
      shelly.output = body.indexOf("\"output\":true") >= 0;
      shelly.apower = num_after(body, "\"apower\":");
      shelly.voltage = num_after(body, "\"voltage\":");
      shelly.current = num_after(body, "\"current\":");
      shelly.freq = num_after(body, "\"freq\":");
      shelly.temp_c = num_after(body, "\"tC\":");
    } else {
      snprintf(shelly.last_reply, sizeof(shelly.last_reply), "%s -> %s", path, body.c_str());
    }
  } else {
    shelly.reachable = false;
    if (code > 0) snprintf(shelly.error, sizeof(shelly.error), "HTTP %d", code);
    else snprintf(shelly.error, sizeof(shelly.error), "no answer (%s)", HTTPClient::errorToString(code).c_str());
    if (!status) snprintf(shelly.last_reply, sizeof(shelly.last_reply), "%s -> %s", path, shelly.error);
  }
  xSemaphoreGive(lock);
  if (!status)
    Con.printf("%8u  Shelly %s -> %s (sent at %u)\n", (unsigned) millis(), path,
               code == 200 ? body.c_str() : shelly.error, (unsigned) sent_at);
}

static void task(void *) {
  uint32_t next_poll = 0;
  Path req;
  for (;;) {
    if (xQueueReceive(requests, &req, pdMS_TO_TICKS(100)) == pdTRUE) {
      get(req.p);
      next_poll = 0;                    // show the effect at once
    }
    if ((int32_t) (millis() - next_poll) >= 0) {
      get("/rpc/Switch.GetStatus?id=0");
      next_poll = millis() + POLL_MS;
    }
  }
}

void shelly_begin(const char *h) {
  snprintf(host, sizeof(host), "%s", h);
  lock = xSemaphoreCreateMutex();
  requests = xQueueCreate(4, sizeof(Path));
  xTaskCreatePinnedToCore(task, "shelly", 6144, nullptr, 1, nullptr, 0);
}

void shelly_request(const char *path) {
  Path p;
  snprintf(p.p, sizeof(p.p), "%s", path);
  xQueueSend(requests, &p, 0);
}

void shelly_snapshot(ShellyState &out) {
  xSemaphoreTake(lock, portMAX_DELAY);
  out = shelly;
  xSemaphoreGive(lock);
}
