// shelly_mock.js — Node test double for the Gen2+ scripting environment lease.js runs in.
//
// Approximation, not emulation: this runs lease.js under Node's V8 via vm.createContext(),
// which is a JS superset of mJS. It gives correct behavioral coverage of the contract logic,
// but cannot catch real mJS-engine-only failures (e.g. the "too many nested anonymous
// functions" parser crash) — lease.js was written and manually checked to avoid those,
// and node --check is run on it separately.
//
// Everything here is a pure in-process fake: no network, no real filesystem beyond what the
// test file itself needs, no timers tied to wall-clock time. A single fake clock (in whole
// milliseconds) drives both Timer.set/Timer.clear and the simulated Switch.Set toggle_after
// flip-back, so tests advance time deterministically with tick(ms).

"use strict";

const vm = require("vm");
const fs = require("fs");

function createMock(opts) {
  opts = opts || {};

  const state = {
    nowMs: typeof opts.nowMs === "number" ? opts.nowMs : 0,
    switch: {
      output: !!opts.initialOn,
      apower: typeof opts.apower === "number" ? opts.apower : 12.3,
      voltage: typeof opts.voltage === "number" ? opts.voltage : 120.1,
      current: typeof opts.current === "number" ? opts.current : 0.1,
      temperature: { tC: typeof opts.tempC === "number" ? opts.tempC : 25, tF: 77 }
      // timer_started_at / timer_duration are added only while a flip-back timer is armed,
      // matching the real Switch.GetStatus behavior (fields present only while active).
      // https://shelly-api-docs.shelly.cloud/gen2/ComponentsAndServices/Switch/
    },
    kvs: {}, // key -> value, seeded from opts.kvsSeed below
    timers: [], // {id, fireAt, intervalMs|null, cb, userdata}
    nextTimerId: 1,
    switchOffTimerId: null,
    // When true, Switch.Set calls wait in pendingSwitch until flushSwitch(), as on the device, where
    // Shelly.call returns at once and the relay changes, and the callback runs, a moment later.
    deferSwitch: false,
    pendingSwitch: [],
    log: []
  };
  if (opts.kvsSeed) {
    const keys = Object.keys(opts.kvsSeed);
    for (let i = 0; i < keys.length; i++) {
      state.kvs[keys[i]] = opts.kvsSeed[keys[i]];
    }
  }

  const endpoints = {};

  function uptimeS() {
    return Math.floor(state.nowMs / 1000);
  }

  function scheduleTimer(periodMs, repeat, cb, userdata) {
    const id = state.nextTimerId++;
    state.timers.push({
      id: id,
      fireAt: state.nowMs + periodMs,
      intervalMs: repeat ? periodMs : null,
      cb: cb,
      userdata: userdata
    });
    return id;
  }

  function clearTimer(id) {
    const before = state.timers.length;
    state.timers = state.timers.filter(function (t) {
      return t.id !== id;
    });
    return state.timers.length < before;
  }

  // Advance the fake clock by deltaMs, firing every due timer along the way, in
  // chronological order, re-arming repeating ones. Mirrors Timer.set's documented
  // (period, repeat, cb, userdata) -> handle contract.
  // https://shelly-api-docs.shelly.cloud/gen2/Scripts/APIs/Timer/
  function tick(deltaMs) {
    const target = state.nowMs + deltaMs;
    for (;;) {
      let next = null;
      for (let i = 0; i < state.timers.length; i++) {
        const t = state.timers[i];
        if (t.fireAt <= target && (next === null || t.fireAt < next.fireAt)) {
          next = t;
        }
      }
      if (next === null) {
        break;
      }
      state.nowMs = next.fireAt;
      if (next.intervalMs !== null) {
        next.fireAt = state.nowMs + next.intervalMs;
      } else {
        state.timers = state.timers.filter(function (t) {
          return t.id !== next.id;
        });
      }
      next.cb(next.userdata);
    }
    state.nowMs = target;
  }

  function switchStatusSnapshot() {
    const s = {
      output: state.switch.output,
      apower: state.switch.apower,
      voltage: state.switch.voltage,
      current: state.switch.current,
      temperature: { tC: state.switch.temperature.tC, tF: state.switch.temperature.tF }
    };
    if (typeof state.switch.timer_duration !== "undefined") {
      s.timer_started_at = state.switch.timer_started_at;
      s.timer_duration = state.switch.timer_duration;
    }
    return s;
  }

  function armSwitchOffTimer(seconds) {
    if (state.switchOffTimerId !== null) {
      clearTimer(state.switchOffTimerId);
      state.switchOffTimerId = null;
    }
    if (seconds > 0) {
      state.switchOffTimerId = scheduleTimer(seconds * 1000, false, function () {
        state.switchOffTimerId = null;
        state.switch.output = false;
        delete state.switch.timer_started_at;
        delete state.switch.timer_duration;
      }, null);
    }
  }

  function switchSet(params, cb, userdata) {
    const wasOn = state.switch.output;
    state.switch.output = !!params.on;
    if (params.on && typeof params.toggle_after === "number" && params.toggle_after > 0) {
      state.switch.timer_started_at = Math.floor(state.nowMs / 1000);
      state.switch.timer_duration = params.toggle_after;
      armSwitchOffTimer(params.toggle_after);
    } else {
      delete state.switch.timer_started_at;
      delete state.switch.timer_duration;
      if (state.switchOffTimerId !== null) {
        clearTimer(state.switchOffTimerId);
        state.switchOffTimerId = null;
      }
    }
    cb({ was_on: wasOn }, 0, "", userdata);
  }

  function kvsSet(params, cb, userdata) {
    state.kvs[params.key] = params.value;
    cb({ etag: "e" + state.nowMs, rev: 1 }, 0, "", userdata);
  }

  function kvsGet(params, cb, userdata) {
    if (Object.prototype.hasOwnProperty.call(state.kvs, params.key)) {
      cb({ etag: "e0", value: state.kvs[params.key] }, 0, "", userdata);
    } else {
      cb(null, 404, "not found", userdata);
    }
  }

  // Shelly.call dispatch — same (method, params, callback, userdata) -> undefined signature,
  // callback (result, error_code, error_message, userdata), as the real API.
  // https://shelly-api-docs.shelly.cloud/gen2/Scripts/APIs/Shelly/
  function shellyCall(method, params, cb, userdata) {
    if (method === "Switch.Set" && state.deferSwitch) {
      state.pendingSwitch.push({ params: params, cb: cb, userdata: userdata });
    } else if (method === "Switch.Set") {
      switchSet(params, cb, userdata);
    } else if (method === "KVS.Set") {
      kvsSet(params, cb, userdata);
    } else if (method === "KVS.Get") {
      kvsGet(params, cb, userdata);
    } else {
      cb(null, 404, "unknown method in mock: " + method, userdata);
    }
  }

  function getComponentStatus(key) {
    if (key === "sys") {
      return { uptime: uptimeS() };
    }
    if (key === "switch:0") {
      return switchStatusSnapshot();
    }
    return null;
  }

  // --- sandbox object exposed to lease.js as the mJS global environment ---
  const sandbox = {
    Shelly: {
      call: shellyCall,
      getComponentStatus: getComponentStatus
    },
    Timer: {
      set: scheduleTimer,
      clear: clearTimer
    },
    HTTPServer: {
      registerEndpoint: function (name, cb, userdata) {
        endpoints[name] = { cb: cb, userdata: userdata };
        return "/script/1/" + name;
      }
    },
    print: function () {
      state.log.push(Array.prototype.slice.call(arguments).join(" "));
    }
  };

  function load(scriptPath) {
    const code = fs.readFileSync(scriptPath, "utf8");
    const context = vm.createContext(sandbox);
    // The Shelly engine's string support is narrower than V8's: remove what lease.js must not use,
    // so a call to any of them fails here as it would on the device.
    vm.runInContext(
      "delete String.prototype.split; delete String.prototype.charAt; delete String.prototype.charCodeAt;" +
        "delete String.prototype.replace; delete String.prototype.substring; delete String.prototype.substr;" +
        "parseInt = undefined; parseFloat = undefined; String.fromCharCode = undefined;",
      context
    );
    vm.runInContext(code, context, { filename: scriptPath });
  }

  // Apply the waiting Switch.Set calls in the order they were made, each with its callback.
  function flushSwitch() {
    while (state.pendingSwitch.length > 0) {
      const c = state.pendingSwitch.shift();
      switchSet(c.params, c.cb, c.userdata);
    }
  }

  // Like callEndpoint, but the answer may come later (after flushSwitch). Returns the response's state.
  function callEndpointPending(name, query) {
    const ep = endpoints[name];
    const request = { method: "GET", query: query || "", headers: [], body: null };
    const out = { sent: false, json: null };
    const response = {
      code: 200,
      body: "",
      headers: [],
      send: function () {
        out.sent = true;
        out.json = JSON.parse(response.body);
      }
    };
    ep.cb(request, response, ep.userdata);
    return out;
  }

  // Build a request object matching HTTPServer's documented request fields (method, query,
  // headers, body) and a response object (code, body, headers, send()) and invoke the
  // registered endpoint synchronously (the mock's Shelly.call/KVS callbacks all fire
  // synchronously, so by the time registerEndpoint's handler returns any chained async work
  // has already completed and response.send() has already been called).
  // https://shelly-api-docs.shelly.cloud/gen2/Scripts/APIs/HTTPServer/
  function callEndpoint(name, query) {
    const ep = endpoints[name];
    if (!ep) {
      throw new Error("endpoint not registered: " + name);
    }
    const request = { method: "GET", query: query || "", headers: [], body: null };
    let sent = false;
    const response = {
      code: 200,
      body: "",
      headers: [],
      send: function () {
        sent = true;
      }
    };
    ep.cb(request, response, ep.userdata);
    if (!sent) {
      throw new Error("endpoint '" + name + "' never called response.send()");
    }
    return { code: response.code, json: JSON.parse(response.body) };
  }

  return {
    state: state,
    load: load,
    tick: tick,
    callEndpoint: callEndpoint,
    callEndpointPending: callEndpointPending,
    flushSwitch: flushSwitch,
    uptimeS: uptimeS,
    switchStatusSnapshot: switchStatusSnapshot,
    hasEndpoint: function (name) {
      return !!endpoints[name];
    }
  };
}

module.exports = { createMock: createMock };
