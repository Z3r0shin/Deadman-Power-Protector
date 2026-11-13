// lease.js - Shelly 1PM Gen4 (fw 2.1.0, Gen2+ scripting / mJS) on-device script.
//
// WHAT IT DOES
//   Implements the Shelly side of the Power Protector lease contract: the bench node
//   renews permission to keep the power bar on; if renewals stop, this script lets the
//   relay's own flip-back timer turn it off within lease_s. A ceiling (max minutes since
//   the last physical key press) is enforced here too, so it survives a bench-node bug.
//   This script is sole authority for the ceiling and for applying toggle_after; the bench
//   node only calls the endpoint below. Source of the contract: the Firmware Gospel
//   (docs/firmware-gospel.md, "The lease contract").
//
// THE CONTRACT (as the Firmware Gospel sets it out)
//   Transport: HTTP GET http://<shelly>/script/<id>/lease?op=<op>&...  (<id> from Script.List,
//   name "lease"). No auth.
//     op=renew   l=lease_s                  relay off -> refuse why:"off", stays off.
//                                            ceiling passed -> relay off, why:"ceiling".
//                                            else Switch.Set on:true toggle_after:min(l, ceiling left).
//     op=on      press=1|0, l=lease_s, c=ceiling_s
//                                            press=1 (physical key): ceiling end = now + c;
//                                              relay on with toggle_after.
//                                            press=0 (remote): off -> same as a press (fresh
//                                              period); on -> same as renew (no ceiling reset).
//     op=off     (none)                     relay off; ceiling cleared.
//     op=status  (none)                     nothing changes.
//   Bounds enforced here regardless of what the bench sends: l 30..600 (default 300);
//   c 60..14400 (default 14400). No caller can set a ceiling above 4 h.
//   Every answer is JSON, HTTP 200:
//     {"ok":true|false,"why":"off"|"ceiling"|"bad_op"|"",
//      "on":bool,"lease_s":N,"ceil_s":N,"w":W,"v":V,"a":A,"t":C,"ver":"lease-1"}
//   lease_s = the toggle_after just applied (0 if none); ceil_s = seconds left to the
//   ceiling (0 if off).
//   Every 5 s, regardless of the endpoint:
//     - relay on and no toggle_after timer running (switched on from the Shelly app, its web
//       page, or HA) -> reapply toggle_after = 300 s and start a ceiling if none;
//     - relay on and the ceiling passed -> relay off.
//   A Switch.Set lands a moment after it is sent. While an off is on its way the relay counts as off
//   (renew refused why:"off"; remote on = a fresh period), and the 5 s check waits for every Switch.Set
//   on its way, up to 10 s.
//   Ceiling time base: device uptime (no clock needed). The ceiling is kept in RAM only. When the
//   script starts with the relay already on (non-power-loss reboot, OTA, script restart), the
//   ceiling is RESTART_CEIL_S (600 s), never a fresh 4 h: a key press (press=1) restores it.
//   Only press=1 resets a running ceiling; the bench node sends press=1 for physical keys only.
//
// INSTALL
//   Using the Shelly RPC (e.g. over the local HTTP RPC endpoint, or `mos` / curl from a
//   workstation on the LAN):
//     1. Script.Create             {"name":"lease"}                 -> returns {"id":N}
//     2. Script.PutCode            {"id":N,"code":"<contents of this file>"}
//        (PutCode has a payload-size limit per call; chunk with "append":true if needed)
//     3. Script.SetConfig          {"id":N,"config":{"enable":true}}
//     4. Script.Start              {"id":N}
//   The bench node then reads the script id with Script.List (match name "lease") and calls
//   http://<shelly>/script/<id>/lease?op=...
//
// SHELLY API DOCS USED (each cited at its call site below; summarized here):
//   Scripts overview / language:
//     https://shelly-api-docs.shelly.cloud/gen2/Scripts/Overview/
//     https://shelly-api-docs.shelly.cloud/gen2/Scripts/LanguageReference/
//   Shelly.call / Shelly.getComponentStatus:
//     https://shelly-api-docs.shelly.cloud/gen2/Scripts/APIs/Shelly/
//   Timer.set / Timer.clear:
//     https://shelly-api-docs.shelly.cloud/gen2/Scripts/APIs/Timer/
//   HTTPServer.registerEndpoint:
//     https://shelly-api-docs.shelly.cloud/gen2/Scripts/APIs/HTTPServer/
//   Switch.Set / Switch.GetStatus fields (output, toggle_after, timer_started_at,
//   timer_duration, apower, voltage, current, temperature):
//     https://shelly-api-docs.shelly.cloud/gen2/ComponentsAndServices/Switch/
//   Sys status (uptime):
//     https://shelly-api-docs.shelly.cloud/gen2/ComponentsAndServices/Sys/
//
// mJS NOTES (why the code is shaped this way)
//   - Only "let" is used for declarations (var/const avoided - the docs disagree on var, agree
//     on let).
//   - No arrow functions, no template literals, no classes, no try/catch-requiring patterns.
//   - All comparisons are === / !==.
//   - mJS closures over a still-running outer function's locals are not relied on: every
//     async callback (Shelly.call, Timer.set) is a named top-level function, and anything it
//     needs is passed through "userdata" rather than captured. This also keeps nesting under
//     the 2-3 level anonymous-function limit (we use none).
//   - No `op=renew` path ever calls Switch.Set with on:true unless the relay was already on.

let SWITCH_ID = 0;

let L_DEFAULT = 300;
let L_MIN = 30;
let L_MAX = 600;

let C_DEFAULT = 14400;
let C_MIN = 60;
let C_MAX = 14400;

let PERIODIC_MS = 5000;
let REAPPLY_LEASE_S = 300; // toggle_after reapplied when found on with no timer running
// Ceiling given when the script starts and finds the relay already on (a Shelly reboot other than
// a power loss, an OTA update, or a script restart). Short on purpose: it never grants a fresh 4 h
// without a key press. 600 s covers the bench node's longest warning, so the cut is still warned.
let RESTART_CEIL_S = 600;

// In-memory ceiling end, as an absolute device-uptime second count. null = no ceiling tracked
// (relay is believed off). Kept in RAM only: a restart gives the short RESTART_CEIL_S instead.
let ceil_end = null;

// Switch.Set calls sent and not answered yet. Shelly.call returns at once; the relay changes, and the callback
// runs, a moment later, so until then the switch status still shows the old state. While an off is on its way
// the relay counts as off; while any call is on its way, the 5 s check waits, so it never acts on a status that
// is about to change. Calls unanswered for 10 s are given up on: the generation
// changes, and an answer that comes after that no longer counts.
let inflight = 0;
let offs_inflight = 0;
let inflight_since = 0;
let gen = 0;
let INFLIGHT_GIVE_UP_S = 10;

// ---- small helpers -------------------------------------------------------

function getSwitchStatus() {
  // Shelly.getComponentStatus(type_or_key, id) -> status object or null.
  // https://shelly-api-docs.shelly.cloud/gen2/Scripts/APIs/Shelly/
  return Shelly.getComponentStatus("switch:0");
}

function getUptime() {
  // Sys status field "uptime" (seconds since last reboot).
  // https://shelly-api-docs.shelly.cloud/gen2/ComponentsAndServices/Sys/
  let s = Shelly.getComponentStatus("sys");
  return s.uptime;
}

// Every Switch.Set goes through here; every callback calls switchDone(userdata) first.
function setSwitch(params, cb, userdata) {
  let ud = userdata;
  if (ud === null) {
    ud = {};
  }
  ud.gen = gen;
  ud.off = !params.on;
  if (inflight === 0) {
    inflight_since = getUptime();
  }
  inflight = inflight + 1;
  if (ud.off) {
    offs_inflight = offs_inflight + 1;
  }
  Shelly.call("Switch.Set", params, cb, ud);
}

function switchDone(ud) {
  if (ud.gen !== gen) {
    return; // answered after it was given up on: it no longer counts
  }
  inflight = inflight - 1;
  if (ud.off) {
    offs_inflight = offs_inflight - 1;
  }
  if (inflight < 0) {
    inflight = 0;
  }
  if (offs_inflight < 0) {
    offs_inflight = 0;
  }
}

// An off that failed: if nothing has set a ceiling since, the ceiling counts as passed, so the 5 s check (or the
// next renew) tries the off again.
function offFailed() {
  if (ceil_end === null) {
    ceil_end = getUptime();
  }
}

// The relay as the handlers must see it: an off on its way counts as off already.
function relayOn(sw) {
  return sw.output && offs_inflight === 0;
}

// Query values from the bench node are plain ASCII (letters and digits), so no percent-decoding is
// done. Only String.indexOf and String.slice are used, the two string methods the Shelly docs show
// in their own examples; split/charAt/parseInt/unary + are avoided on purpose.
let DIGITS = "0123456789";

// A whole non-negative integer, or def when str is missing or holds anything but digits.
function parseDigits(str, def) {
  if (typeof str !== "string" || str.length === 0 || str.length > 9) {
    return def;
  }
  let n = 0;
  let i = 0;
  while (i < str.length) {
    let d = DIGITS.indexOf(str.slice(i, i + 1));
    if (d < 0) {
      return def;
    }
    n = n * 10 + d;
    i = i + 1;
  }
  return n;
}

function clampNum(str, def, lo, hi) {
  let n = parseDigits(str, def);
  if (n < lo) {
    n = lo;
  }
  if (n > hi) {
    n = hi;
  }
  return n;
}

// "op=renew&l=300" -> {op:"renew", l:"300"}. Walks the string with indexOf/slice only.
function parseQuery(qs) {
  let result = {};
  if (typeof qs !== "string") {
    return result;
  }
  let start = 0;
  while (start < qs.length) {
    let amp = qs.indexOf("&", start);
    if (amp < 0) {
      amp = qs.length;
    }
    let pair = qs.slice(start, amp);
    let eq = pair.indexOf("=");
    if (eq < 0) {
      result[pair] = "";
    } else {
      result[pair.slice(0, eq)] = pair.slice(eq + 1);
    }
    start = amp + 1;
  }
  return result;
}

function ceilLeftFor(sw, up) {
  if (!relayOn(sw)) {
    return 0;
  }
  if (ceil_end === null) {
    return 0;
  }
  let left = ceil_end - up;
  if (left < 0) {
    left = 0;
  }
  return left;
}

function reply(response, ok, why, on, lease_s, ceil_s, sw) {
  let t = null;
  let w = null;
  let v = null;
  let a = null;
  if (sw) {
    if (typeof sw.apower !== "undefined") {
      w = sw.apower;
    }
    if (typeof sw.voltage !== "undefined") {
      v = sw.voltage;
    }
    if (typeof sw.current !== "undefined") {
      a = sw.current;
    }
    if (sw.temperature && typeof sw.temperature.tC !== "undefined") {
      t = sw.temperature.tC;
    }
  }
  let obj = {
    ok: ok,
    why: why,
    on: on,
    lease_s: lease_s,
    ceil_s: ceil_s,
    w: w,
    v: v,
    a: a,
    t: t,
    ver: "lease-1"
  };
  response.code = 200;
  response.body = JSON.stringify(obj);
  response.send();
}

// ---- op handlers ----------------------------------------------------------

function doRenew(l, response) {
  let sw = getSwitchStatus();
  if (!relayOn(sw)) {
    reply(response, false, "off", false, 0, 0, sw);
    return;
  }
  let up = getUptime();
  if (ceil_end === null) {
    // Invariant should already hold (relay on => ceiling set, see init()/periodicCheck()).
    // If it is somehow missing, take the short restart ceiling, never a fresh 4 h.
    ceil_end = up + RESTART_CEIL_S;
  }
  if (up >= ceil_end) {
    ceil_end = null;
    setSwitch({ id: SWITCH_ID, on: false }, onRenewCeilingOffDone, { response: response });
    return;
  }
  let ceilLeft = ceil_end - up;
  let applied = l;
  if (applied > ceilLeft) {
    applied = ceilLeft;
  }
  if (applied < 0) {
    applied = 0;
  }
  // renew never turns the relay on from off (checked above); it only ever refreshes
  // toggle_after on a relay that is already on.
  setSwitch(
    { id: SWITCH_ID, on: true, toggle_after: applied },
    onRenewSetDone,
    { response: response, applied: applied, up: up }
  );
}

function onRenewSetDone(result, error_code, error_message, userdata) {
  switchDone(userdata);
  if (error_code !== 0) {
    print("lease: Switch.Set (renew) failed:", error_message);
  }
  let sw = getSwitchStatus();
  reply(userdata.response, true, "", true, userdata.applied, ceilLeftFor(sw, userdata.up), sw);
}

function onRenewCeilingOffDone(result, error_code, error_message, userdata) {
  switchDone(userdata);
  if (error_code !== 0) {
    print("lease: Switch.Set (ceiling off) failed:", error_message);
    offFailed();
  }
  reply(userdata.response, false, "ceiling", false, 0, 0, getSwitchStatus());
}

function startFreshCeilingAndOn(up, c, l, response) {
  ceil_end = up + c;
  let applied = l;
  if (applied > c) {
    applied = c;
  }
  setSwitch(
    { id: SWITCH_ID, on: true, toggle_after: applied },
    onOnSetDone,
    { response: response, applied: applied, up: up }
  );
}

function onOnSetDone(result, error_code, error_message, userdata) {
  switchDone(userdata);
  if (error_code !== 0) {
    print("lease: Switch.Set (on) failed:", error_message);
  }
  let sw = getSwitchStatus();
  reply(userdata.response, true, "", true, userdata.applied, ceilLeftFor(sw, userdata.up), sw);
}

function doOn(press, l, c, response) {
  let up = getUptime();
  if (press === 1) {
    // Physical key: always a fresh ceiling, never lengthened-only - an explicit reset.
    startFreshCeilingAndOn(up, c, l, response);
    return;
  }
  // Remote (press=0).
  let sw = getSwitchStatus();
  if (!relayOn(sw)) {
    startFreshCeilingAndOn(up, c, l, response);
  } else {
    doRenew(l, response);
  }
}

function doOff(response) {
  ceil_end = null;
  setSwitch({ id: SWITCH_ID, on: false }, onOffSetDone, { response: response });
}

function onOffSetDone(result, error_code, error_message, userdata) {
  switchDone(userdata);
  if (error_code !== 0) {
    print("lease: Switch.Set (off) failed:", error_message);
    offFailed();
  }
  reply(userdata.response, true, "", false, 0, 0, getSwitchStatus());
}

function doStatus(response) {
  let sw = getSwitchStatus();
  let up = getUptime();
  reply(response, true, "", relayOn(sw), 0, ceilLeftFor(sw, up), sw);
}

function doBadOp(response) {
  let sw = getSwitchStatus();
  let up = getUptime();
  reply(response, false, "bad_op", relayOn(sw), 0, ceilLeftFor(sw, up), sw);
}

// ---- HTTP endpoint ----------------------------------------------------------

function onLeaseRequest(request, response, userdata) {
  let q = parseQuery(request.query);
  let op = q.op;
  let l = clampNum(q.l, L_DEFAULT, L_MIN, L_MAX);
  let c = clampNum(q.c, C_DEFAULT, C_MIN, C_MAX);
  let press = clampNum(q.press, 0, 0, 1);

  if (op === "renew") {
    doRenew(l, response);
  } else if (op === "on") {
    doOn(press, l, c, response);
  } else if (op === "off") {
    doOff(response);
  } else if (op === "status") {
    doStatus(response);
  } else {
    doBadOp(response);
  }
}

// ---- 5 s periodic check ----------------------------------------------------

function periodicCheck() {
  let sw = getSwitchStatus();
  let up = getUptime();
  if (inflight > 0) {
    if (up - inflight_since < INFLIGHT_GIVE_UP_S) {
      return;
    }
    print("lease: no answer to a Switch.Set for", up - inflight_since, "s; checking again");
    if (offs_inflight > 0) {
      offFailed(); // a lost off: the ceiling counts as passed, so the off is tried again
    }
    gen = gen + 1;
    inflight = 0;
    offs_inflight = 0;
  }
  if (sw.output) {
    let timerRunning = typeof sw.timer_duration !== "undefined";
    if (!timerRunning) {
      // Someone switched it on outside the lease (Shelly app/web page/HA). Reapply the
      // flip-back timer and start a ceiling if none is tracked.
      setSwitch(
        { id: SWITCH_ID, on: true, toggle_after: REAPPLY_LEASE_S },
        onPeriodicReapplyDone,
        null
      );
      if (ceil_end === null) {
        ceil_end = up + C_MAX;
      }
    } else {
      if (ceil_end !== null && up >= ceil_end) {
        setSwitch({ id: SWITCH_ID, on: false }, onPeriodicCeilingOffDone, null);
        ceil_end = null;
      }
    }
  } else {
    if (ceil_end !== null) {
      ceil_end = null;
    }
  }
}

function onPeriodicReapplyDone(result, error_code, error_message, userdata) {
  switchDone(userdata);
  if (error_code !== 0) {
    print("lease: periodic reapply failed:", error_message);
  }
}

function onPeriodicCeilingOffDone(result, error_code, error_message, userdata) {
  switchDone(userdata);
  if (error_code !== 0) {
    print("lease: periodic ceiling-off failed:", error_message);
    offFailed();
  }
}

// ---- startup ----------------------------------------------------------------

function init() {
  let sw = getSwitchStatus();
  if (sw.output) {
    // Started with the relay already on: whatever ceiling ran before is unknown. Never grant a fresh
    // 4 h here; a short ceiling makes the bench node warn, and one key press (press=1) restores it.
    ceil_end = getUptime() + RESTART_CEIL_S;
  } else {
    ceil_end = null;
  }
  Timer.set(PERIODIC_MS, true, periodicCheck, null);
  HTTPServer.registerEndpoint("lease", onLeaseRequest, null);
}

init();
