// Tiny JSON writers for the web API. Each helper appends `"key":value,`; end_obj() drops the last comma.
#pragma once
#include <Arduino.h>

inline void jesc(String &o, const char *s) {
  o += '"';
  for (; *s; s++) {
    char c = *s;
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if ((uint8_t) c < 0x20 || (uint8_t) c == 0x7F) continue;   // control characters dropped; UTF-8 kept
    else o += c;
  }
  o += '"';
}
inline void jarr(String &o, const uint8_t *v, int n) {
  o += '[';
  for (int i = 0; i < n; i++) { if (i) o += ','; o += v[i]; }
  o += ']';
}
inline void kv(String &o, const char *k, long v) { o += '"'; o += k; o += "\":"; o += v; o += ','; }
// NaN or infinity is not JSON: one bad number would stop the page from reading anything.
inline void kf(String &o, const char *k, float v, int dec = 1) {
  o += '"'; o += k; o += "\":";
  if (isfinite(v)) o += String(v, dec); else o += "null";
  o += ',';
}
inline void ks(String &o, const char *k, const char *v) { o += '"'; o += k; o += "\":"; jesc(o, v); o += ','; }
inline void kopen(String &o, const char *k) { o += '"'; o += k; o += "\":{"; }
inline void end_obj(String &o) {
  if (o.endsWith(",")) o.remove(o.length() - 1);
  o += '}';
}
