// The lease link to the Shelly script (the Firmware Gospel's lease contract), in its own task so a slow or
// absent Shelly never stalls
// the keys, the radar or the warning sounds.
//   loop() -> shelly_send(op)          at most one request in flight (the core guarantees it)
//   loop() <- shelly_take_result(...)  the answer, or got = false
#pragma once
#include <Arduino.h>

#include "bench_core.h"

struct ShellyReading {
  float w = NAN, v = NAN, a = NAN, t = NAN;
  int script_id = -1;                   // -1 until the "lease" script is found
  char error[64] = "no request yet";
  char ver[16] = "";
};

void shelly_begin();
void shelly_send(bench::Op op, uint32_t lease_s, uint32_t ceil_s, uint32_t seq);
bool shelly_take_result(bench::Op &op, bool &got, bench::Answer &a, uint32_t &seq);
void shelly_reading(ShellyReading &out);
const char *op_name(bench::Op op);
